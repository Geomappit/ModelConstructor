// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "SMCDialogs.h"
#include "MCTemplates.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "IContentBrowserSingleton.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

namespace MCDialogs
{
FSlateColor WarningColor()
{
	return FSlateColor(FLinearColor(0.9f, 0.7f, 0.2f));
}

bool ConfirmPlan(const FMCRemovalPlan& Plan, const FText& Title, const FText& Heading, const FText& AcceptText, const TArray<FString>& ExtraLines)
{
	TArray<FString> Classes;
	for (const TPair<FString, int32>& Pair : Plan.AssetsByClass)
	{
		Classes.Add(FString::Printf(TEXT("%d %s"), Pair.Value, *Pair.Key));
	}
	Classes.Sort();

	TArray<FString> Summary;
	Summary.Add(FString::Printf(TEXT("Folder: %s"), Plan.Folder.IsEmpty() ? TEXT("not identified") : *Plan.Folder));
	Summary.Add(FString::Printf(TEXT("On disk: %s"), Plan.DiskFolder.IsEmpty() ? TEXT("not identified") : *Plan.DiskFolder));
	Summary.Add(FString::Printf(TEXT("Assets: %d (%s)"), Plan.Packages.Num(), *FString::Join(Classes, TEXT(", "))));
	Summary.Add(FString::Printf(TEXT("Files deleted: %d, %s"), Plan.FileCount, *FText::AsMemory(static_cast<uint64>(FMath::Max<int64>(Plan.Bytes, 0))).ToString()));
	if (Plan.OtherFileCount > 0)
	{
		Summary.Add(FString::Printf(TEXT("Other files kept: %d. The folder stays on disk because of them."), Plan.OtherFileCount));
	}
	if (!Plan.OpenLevelToClose.IsEmpty())
	{
		Summary.Add(FString::Printf(TEXT("The open level %s is part of the model or streams it. It is closed first and a blank level opens."), *Plan.OpenLevelToClose));
	}
	if (Plan.UnsavedCount > 0)
	{
		Summary.Add(FString::Printf(TEXT("%d of these assets have unsaved changes, which are lost."), Plan.UnsavedCount));
	}
	if (Plan.bDeletesWithoutLoading)
	{
		Summary.Add(TEXT("The files are deleted straight from disk without loading them. This cannot be undone in the editor."));
	}
	Summary.Append(Plan.Notes);
	Summary.Append(ExtraLines);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			.Text(Heading)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(FText::FromString(FString::Join(Summary, LINE_TERMINATOR)))
		];

	if (Plan.Blockers.Num() > 0)
	{
		Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(WarningColor())
			.Text(FText::FromString(TEXT("This cannot run:") LINE_TERMINATOR + FString::Join(Plan.Blockers, LINE_TERMINATOR)))
		];
	}

	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
	[
		SNew(STextBlock).Text(FText::Format(LOCTEXT("PackageListHeading", "Packages that would be deleted ({0})"), FText::AsNumber(Plan.Packages.Num())))
	];
	Content->AddSlot().FillHeight(1.f).Padding(0.f, 0.f, 0.f, 6.f)
	[
		SNew(SMultiLineEditableTextBox)
		.IsReadOnly(true)
		.Text(FText::FromString(FString::Join(Plan.Packages, LINE_TERMINATOR)))
	];

	if (Plan.bCanRemove)
	{
		Content->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(FText::Format(LOCTEXT("PressToConfirm", "Nothing is deleted until you press {0}."), AcceptText))
		];
	}

	const bool bCanRun = Plan.bCanRemove;
	return RunModal(Title, Content, AcceptText, [bCanRun]() { return bCanRun; }, FVector2D(640.f, 600.f));
}

bool RunModal(const FText& Title, const TSharedRef<SWidget>& Content, const FText& AcceptText, TFunction<bool()> CanAccept, FVector2D Size)
{
	TSharedRef<bool> bAccepted = MakeShared<bool>(false);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(Title)
		.ClientSize(Size)
		.SizingRule(ESizingRule::UserSized)
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	const TWeakPtr<SWindow> WeakWindow = Window;
	auto Close = [WeakWindow]()
	{
		if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin())
		{
			Pinned->RequestDestroyWindow();
		}
	};

	Window->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(12.f, 12.f, 12.f, 8.f)
		[
			Content
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(12.f, 4.f, 12.f, 12.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get(), "PrimaryButton")
				.Text(AcceptText)
				.IsEnabled_Lambda([CanAccept]() { return !CanAccept || CanAccept(); })
				.OnClicked_Lambda([bAccepted, Close]()
				{
					*bAccepted = true;
					Close();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ModalCancel", "Cancel"))
				.OnClicked_Lambda([Close]()
				{
					Close();
					return FReply::Handled();
				})
			]
		]);

	FSlateApplication::Get().AddModalWindow(Window, FGlobalTabmanager::Get()->GetRootWindow());
	return *bAccepted;
}

FString AskTemplateName(const FText& Title, const FText& AcceptText, const FString& Initial, const FString& ExistingName, FString* InOutDescription)
{
	TSharedRef<FString> Name = MakeShared<FString>(Initial.TrimStartAndEnd());
	TSharedRef<FString> Description = MakeShared<FString>(InOutDescription ? *InOutDescription : FString());
	TSharedRef<FString> Problem = MakeShared<FString>(MCTemplates::CheckName(*Name, ExistingName));

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
		[
			SNew(STextBlock).Text(LOCTEXT("TemplateNameLabel", "Name"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
		[
			SNew(SEditableTextBox)
			.Text(FText::FromString(*Name))
			.SelectAllTextWhenFocused(true)
			.HintText(LOCTEXT("TemplateNameHint", "For example: Site survey model"))
			.OnTextChanged_Lambda([Name, Problem, ExistingName](const FText& Text)
			{
				*Name = Text.ToString().TrimStartAndEnd();
				*Problem = MCTemplates::CheckName(*Name, ExistingName);
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(STextBlock)
			.ColorAndOpacity(WarningColor())
			.Text_Lambda([Problem, Name]() { return Name->IsEmpty() ? FText::GetEmpty() : FText::FromString(*Problem); })
		];

	if (InOutDescription)
	{
		Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
		[
			SNew(STextBlock).Text(LOCTEXT("TemplateDescriptionLabel", "Description (optional)"))
		];
		Content->AddSlot().AutoHeight()
		[
			SNew(SEditableTextBox)
			.Text(FText::FromString(*Description))
			.HintText(LOCTEXT("TemplateDescriptionHint", "What kind of model this is for"))
			.OnTextChanged_Lambda([Description](const FText& Text) { *Description = Text.ToString(); })
		];
	}

	const bool bAccepted = RunModal(Title, Content, AcceptText, [Problem]() { return Problem->IsEmpty(); }, FVector2D(460.f, InOutDescription ? 230.f : 160.f));
	if (!bAccepted)
	{
		return FString();
	}
	if (InOutDescription)
	{
		*InOutDescription = Description->TrimStartAndEnd();
	}
	return *Name;
}

FString PickDatasmithFile(const TSharedPtr<SWidget>& Parent, const FString& Current)
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FString();
	}
	TArray<FString> Files;
	const bool bPicked = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(Parent),
		LOCTEXT("SourceDialogTitle", "Choose a Datasmith file").ToString(),
		Current.IsEmpty() ? FString() : FPaths::GetPath(Current),
		FString(),
		TEXT("Datasmith file (*.udatasmith)|*.udatasmith"),
		EFileDialogFlags::None,
		Files);
	return bPicked && Files.Num() > 0 ? FPaths::ConvertRelativePathToFull(Files[0]) : FString();
}

FString PickContentFolder(const FString& Current)
{
	TSharedRef<FString> Selection = MakeShared<FString>(ToPackagePath(Current));

	FPathPickerConfig Config;
	Config.DefaultPath = *Selection;
	Config.bAllowContextMenu = true;
	Config.bAllowClassesFolder = false;
	Config.bFocusSearchBoxWhenOpened = true;
	Config.OnPathSelected = FOnPathSelected::CreateLambda([Selection](const FString& Path)
	{
		*Selection = Path;
	});

	FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	const TSharedRef<SWidget> Picker = ContentBrowser.Get().CreatePathPicker(Config);
	if (RunModal(LOCTEXT("DestinationDialogTitle", "Choose the destination folder"), Picker, LOCTEXT("PickerOk", "OK"),
		[Selection]() { return !Selection->IsEmpty(); }, FVector2D(480.f, 560.f)))
	{
		return ToPackagePath(*Selection);
	}
	return FString();
}

FString ToPackagePath(FString Path)
{
	Path.TrimStartAndEndInline();
	Path.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (Path.Len() > 1 && Path.EndsWith(TEXT("/")))
	{
		Path.LeftChopInline(1);
	}

	if (Path.StartsWith(TEXT("/All/EngineData/Plugins/")))
	{
		Path = Path.RightChop(FCString::Strlen(TEXT("/All/EngineData/Plugins")));
	}
	else if (Path.StartsWith(TEXT("/All/EngineData/")))
	{
		Path = Path.RightChop(FCString::Strlen(TEXT("/All/EngineData")));
	}
	else if (Path.StartsWith(TEXT("/All/Plugins/")))
	{
		Path = Path.RightChop(FCString::Strlen(TEXT("/All/Plugins")));
	}
	else if (Path.StartsWith(TEXT("/All/")))
	{
		Path = Path.RightChop(FCString::Strlen(TEXT("/All")));
	}
	return Path;
}
}

#undef LOCTEXT_NAMESPACE
