// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

// The Model Manager's actions on the selected model, kept apart from the layout in SMCModelManager.cpp.

#include "SMCModelManager.h"
#include "SMCDialogs.h"
#include "MCLibrary.h"
#include "MCSettings.h"
#include "MCTemplates.h"
#include "ContentBrowserModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

TSharedRef<SWidget> SMCModelManager::MakeMoreMenu()
{
	auto Action = [this](TFunction<void()> Run, TFunction<bool()> CanRun)
	{
		return FUIAction(FExecuteAction::CreateLambda([Run]() { Run(); }), FCanExecuteAction::CreateLambda([CanRun]() { return CanRun(); }));
	};
	auto HasSelection = [this]() { return Selected().IsValid(); };
	auto HasSource = [this]() { const FEntry Item = Selected(); return Item.IsValid() && !Item->SourceFile.IsEmpty(); };
	auto HasSettings = [this]() { const FEntry Item = Selected(); return Item.IsValid() && Item->bHasSettings; };

	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(LOCTEXT("CopyPath", "Copy scene path"), LOCTEXT("CopyPathTip", "Copy the scene asset's object path."), FSlateIcon(),
		Action([this]() { if (const FEntry Item = Selected()) { FPlatformApplicationMisc::ClipboardCopy(*Item->ScenePath); } }, HasSelection));
	Menu.AddMenuEntry(LOCTEXT("CopySource", "Copy source path"), LOCTEXT("CopySourceTip", "Copy the path of the file the model was imported from."), FSlateIcon(),
		Action([this]() { if (const FEntry Item = Selected()) { FPlatformApplicationMisc::ClipboardCopy(*Item->SourceFile); } }, HasSource));
	Menu.AddSeparator();
	Menu.AddMenuEntry(LOCTEXT("RenameModel", "Rename..."), LOCTEXT("RenameModelTip", "Change the name shown for this model. No asset or folder is renamed."), FSlateIcon(),
		Action([this]() { OnRename(); }, HasSelection));
	Menu.AddMenuEntry(LOCTEXT("SaveAsTemplate", "Save import settings as template..."),
		LOCTEXT("SaveAsTemplateTip", "Make a project template from the settings this model was imported with, then open it on the Settings page."), FSlateIcon(),
		Action([this]() { OnSaveAsTemplate(); }, HasSettings));
	return Menu.MakeWidget();
}

void SMCModelManager::OnShowInContentBrowser()
{
	if (const FEntry Item = Selected())
	{
		FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		ContentBrowser.Get().SyncBrowserToFolders({ Item->Folder });
	}
}

void SMCModelManager::OnOpenLevel()
{
	TSharedPtr<FString> Level = LevelItems.Num() == 1 ? LevelItems[0] : nullptr;
	if (LevelList.IsValid() && LevelList->GetNumItemsSelected() > 0)
	{
		Level = LevelList->GetSelectedItems()[0];
	}
	if (Level.IsValid())
	{
		UMCLibrary::OpenLevel(*Level);
	}
}

void SMCModelManager::OnRename()
{
	const FEntry Item = Selected();
	if (!Item.IsValid())
	{
		return;
	}
	TSharedRef<FString> Name = MakeShared<FString>(Item->DisplayName);
	const TSharedRef<SWidget> Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("RenameBody", "The name is a label on the scene asset. Assets and folders keep their names. Leave it empty to show the folder name."))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SEditableTextBox)
			.Text(FText::FromString(*Name))
			.SelectAllTextWhenFocused(true)
			.OnTextChanged_Lambda([Name](const FText& Text) { *Name = Text.ToString(); })
		];

	if (MCDialogs::RunModal(LOCTEXT("RenameTitle", "Rename model"), Content, LOCTEXT("RenameAccept", "Rename"), nullptr, FVector2D(420.f, 160.f)))
	{
		FString Error;
		if (!UMCLibrary::SetModelDisplayName(Item->ScenePath, *Name, Error))
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
		}
		Refresh();
	}
}

void SMCModelManager::OnRemove()
{
	const FEntry Item = Selected();
	if (!Item.IsValid())
	{
		return;
	}
	const FMCRemovalPlan Plan = UMCLibrary::PlanRemoveModel(Item->ScenePath);
	const FText Heading = FText::Format(LOCTEXT("RemoveHeading", "Remove {0}?"), FText::FromString(Plan.DisplayName.IsEmpty() ? Item->DisplayName : Plan.DisplayName));
	if (!MCDialogs::ConfirmPlan(Plan, LOCTEXT("RemoveTitle", "Remove model"), Heading, LOCTEXT("RemoveAccept", "Remove"), {}))
	{
		return;
	}

	const FMCRemovalResult Result = UMCLibrary::RemoveModel(Plan.ScenePath, Plan.Fingerprint);
	FText Message;
	if (!Result.bSucceeded)
	{
		Message = FText::FromString(Result.Error);
	}
	else if (Result.bFolderRemoved)
	{
		Message = FText::Format(LOCTEXT("Removed", "Removed {0}: {1} assets deleted in {2} s."), FText::FromString(Plan.Folder),
			FText::AsNumber(Result.DeletedAssets), FText::AsNumber(FMath::RoundToInt(Result.Seconds)));
	}
	else
	{
		Message = FText::Format(LOCTEXT("RemovedFolderKept", "{1} assets deleted. {0} was kept on disk because it still holds files that are not assets."),
			FText::FromString(Plan.Folder), FText::AsNumber(Result.DeletedAssets));
	}
	FMessageDialog::Open(EAppMsgType::Ok, Message);
	bReimportOpen = false;
	Refresh();
}

void SMCModelManager::OnSaveAsTemplate()
{
	const FEntry Item = Selected();
	if (!Item.IsValid())
	{
		return;
	}
	TStrongObjectPtr<UMCSettings> Recorded(NewObject<UMCSettings>(GetTransientPackage(), NAME_None, RF_Transient));
	if (!MCTemplates::ApplyRecorded(Item->ScenePath, *Recorded))
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoRecord", "This model has no recorded settings. It was imported with an older version."));
		return;
	}
	FString Description = FString::Printf(TEXT("Settings %s was imported with."), *Item->DisplayName);
	const FString Name = MCDialogs::AskTemplateName(LOCTEXT("SaveRecordTitle", "Save import settings as template"), LOCTEXT("SaveRecordAccept", "Save"),
		Item->DisplayName + TEXT(" settings"), FString(), &Description);
	if (Name.IsEmpty())
	{
		return;
	}
	FString Error;
	if (!MCTemplates::Save(Name, Description, /*bIncludeDestination*/ false, *Recorded, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
		return;
	}
	OnTemplatesChanged.ExecuteIfBound();
	OnOpenSettings.ExecuteIfBound(Name);
}

#undef LOCTEXT_NAMESPACE
