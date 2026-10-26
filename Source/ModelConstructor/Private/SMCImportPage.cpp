// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "SMCImportPage.h"
#include "SMCDialogs.h"
#include "SMCFields.h"
#include "SMCRunResult.h"
#include "MCLibrary.h"
#include "MCModels.h"
#include "MCRun.h"
#include "MCSettings.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

namespace
{
	/** Chosen on a fresh install, when the saved settings were never changed. */
	const TCHAR* FirstTemplate = TEXT("Datasmith Model");

	/** Page margin plus the Model box's inner padding and label gap, so the filler bar ends where the fields start. */
	constexpr float FillerWidth = 8.f + MCFields::LabelWidth + 8.f;
}

void SMCImportPage::Construct(const FArguments& InArgs)
{
	OnImported = InArgs._OnImported;
	OnEditTemplate = InArgs._OnEditTemplate;
	OnTemplatesChanged = InArgs._OnTemplatesChanged;
	Settings = GetMutableDefault<UMCSettings>();
	UnfinishedRun = MCRun::Journal::ReadUnfinished();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(MCFields::Gap, MCFields::Gap, MCFields::Gap, 0.f)
		[
			BuildUnfinishedBanner()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(MCFields::Gap, MCFields::Gap, MCFields::Gap, 0.f)
		[
			BuildTemplateBanner()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(MCFields::Gap)
		[
			BuildModelBox()
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(MCFields::Gap, 2.f * MCFields::Gap, MCFields::Gap, MCFields::Gap)
		[
			BuildRunArea()
		]
	];

	RefreshTemplates();
	if (Settings->TemplateName.IsEmpty())
	{
		// Settings that already equal a template are given its name. A fresh install starts on the first built-in.
		const FString Saved = MCTemplates::ExportTemplateSettings(*Settings);
		const FItem* Match = Items.FindByPredicate([&Saved](const FItem& Item) { return Item->Settings == Saved; });
		if (Match)
		{
			Settings->TemplateName = (*Match)->Name;
			Settings->SaveConfig();
		}
		else if (MCTemplates::HasFactoryTemplateSettings(*Settings))
		{
			if (const FItem* First = Items.FindByPredicate([](const FItem& Item) { return Item->Name == FirstTemplate; }))
			{
				MCTemplates::Apply(**First, *Settings);
				Settings->SaveConfig();
			}
		}
		RefreshTemplates();
	}
	// Folders can appear outside the tool, and scripts can change the settings.
	RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SMCImportPage::OnRefreshTimer));
}

TSharedRef<SWidget> SMCImportPage::BuildModelBox()
{
	const TSharedRef<SWidget> Rows = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			BuildTemplateRow()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(MCFields::LabelWidth + 8.f, 2.f, 0.f, 6.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text_Lambda([this]() { return SummaryText; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			MCFields::PathRow(LOCTEXT("SourceLabel", "Source file"),
				TAttribute<FText>::CreateLambda([this]() { return FText::FromString(Settings->SourceFile.FilePath); }),
				[this](const FString& Text) { SetSourceFile(Text); },
				[this]()
				{
					const FString Picked = MCDialogs::PickDatasmithFile(AsShared(), Settings->SourceFile.FilePath);
					if (!Picked.IsEmpty())
					{
						SetSourceFile(Picked);
					}
				})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			MCFields::PathRow(LOCTEXT("DestinationLabel", "Destination"),
				TAttribute<FText>::CreateLambda([this]() { return FText::FromString(Settings->DestinationFolder.Path); }),
				[this](const FString& Text) { SetDestination(Text); },
				[this]()
				{
					const FString Picked = MCDialogs::PickContentFolder(Settings->DestinationFolder.Path);
					if (!Picked.IsEmpty())
					{
						SetDestination(Picked);
					}
				})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			BuildNameRow()
		];

	return MCFields::Group(LOCTEXT("ModelGroup", "Model"), Rows);
}

TSharedRef<SWidget> SMCImportPage::BuildTemplateRow()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			MCFields::Label(LOCTEXT("TemplateLabel", "Template"))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SAssignNew(Combo, SComboBox<FItem>)
			.OptionsSource(&Items)
			.OnSelectionChanged(this, &SMCImportPage::OnPicked)
			.ToolTipText_Lambda([this]()
			{
				const FItem Current = CurrentItem();
				return Current.IsValid() && !Current->Description.IsEmpty() ? FText::FromString(Current->Description)
					: LOCTEXT("TemplateTip", "Sets how the model is processed. Edit templates on the Settings page.");
			})
			.OnGenerateWidget_Lambda([](FItem Item) -> TSharedRef<SWidget>
			{
				return SNew(SHorizontalBox)
					.ToolTipText(FText::FromString(Item->Description))
					+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
					[
						SNew(STextBlock).Text(FText::FromString(Item->Name))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 2.f, 2.f, 2.f)
					[
						SNew(STextBlock)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text(Item->bBuiltIn ? LOCTEXT("BuiltInTag", "built-in") : LOCTEXT("ProjectTag", "project"))
					];
			})
			[
				SNew(STextBlock).Text_Lambda([this]()
				{
					const FItem Current = CurrentItem();
					return Current.IsValid() ? FText::FromString(Current->Name) : LOCTEXT("ChooseTemplate", "Choose a template");
				})
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("EditTemplate", "Edit..."))
			.ToolTipText(LOCTEXT("EditTemplateTip", "Open this template on the Settings page."))
			.OnClicked_Lambda([this]()
			{
				const FItem Current = CurrentItem();
				OnEditTemplate.ExecuteIfBound(Current.IsValid() ? Current->Name : FString());
				return FReply::Handled();
			})
		];
}

TSharedRef<SWidget> SMCImportPage::BuildNameRow()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SBox)
			.WidthOverride(MCFields::LabelWidth)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return Settings->bUseCustomName ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					Settings->bUseCustomName = State == ECheckBoxState::Checked;
					Settings->SaveConfig();
					RefreshState();
				})
				.ToolTipText(LOCTEXT("NameToggleTip", "Name the model yourself instead of using the file name."))
				[
					SNew(STextBlock).Text(LOCTEXT("NameLabel", "Model name"))
				]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SEditableTextBox)
			.IsEnabled_Lambda([this]() { return Settings->bUseCustomName; })
			.Text_Lambda([this]() { return FText::FromString(Settings->ModelName); })
			.HintText_Lambda([this]() { return FText::FromString(FPaths::GetBaseFilename(Settings->SourceFile.FilePath)); })
			.ToolTipText(LOCTEXT("NameTip", "Shown as written in Model Manager. The folder uses the same name with spaces and symbols replaced."))
			.OnTextChanged_Lambda([this](const FText& Text)
			{
				Settings->ModelName = Text.ToString();
				RefreshState();
			})
			.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
			{
				Settings->ModelName = Text.ToString();
				Settings->SaveConfig();
				RefreshState();
			})
		];
}

TSharedRef<SWidget> SMCImportPage::BuildTemplateBanner()
{
	return SNew(SBorder)
		.Visibility_Lambda([this]() { return bMatchesTemplate ? EVisibility::Collapsed : EVisibility::Visible; })
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(MCDialogs::WarningColor())
				.Text_Lambda([this]()
				{
					const FItem Current = CurrentItem();
					if (!Current.IsValid())
					{
						return LOCTEXT("SettingsNoTemplate", "Your saved settings are not in any template. Save them as a template to keep them, then choose it.");
					}
					return FText::Format(LOCTEXT("SettingsDiffer", "Your saved settings differ from {0}, probably from an older version. Imports use {0} as saved. Save the old settings as a template to keep them."),
						FText::FromString(Current->Name));
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SaveSettingsAsTemplate", "Save as template..."))
				.OnClicked_Lambda([this]() { OnSaveAsTemplate(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Visibility_Lambda([this]() { return CurrentItem().IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text(LOCTEXT("UseTemplateSettings", "Use the template"))
				.ToolTipText(LOCTEXT("UseTemplateSettingsTip", "Replace the old settings with the template's. This hides the note."))
				.OnClicked_Lambda([this]()
				{
					const FItem Current = CurrentItem();
					if (Current.IsValid())
					{
						MCModels::ImportSettings(Current->Settings, *Settings);
						Settings->SaveConfig();
						RefreshState();
					}
					return FReply::Handled();
				})
			]
		];
}

TSharedRef<SWidget> SMCImportPage::BuildUnfinishedBanner()
{
	return SNew(SBorder)
		.Visibility_Lambda([this]() { return UnfinishedRun.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(MCDialogs::WarningColor())
				.Text_Lambda([this]() { return FText::FromString(UnfinishedRun); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("DismissUnfinished", "Dismiss"))
				.OnClicked_Lambda([this]()
				{
					MCRun::Journal::Clear();
					UnfinishedRun.Reset();
					return FReply::Handled();
				})
			]
		];
}

TSharedRef<SWidget> SMCImportPage::BuildRunArea()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, MCFields::Gap, 0.f)
		[
			// Lines the run area up with the fields of the Model box above.
			SNew(SBox)
			.WidthOverride(FillerWidth - MCFields::Gap)
			[
				SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SAssignNew(RunResult, SMCRunResult)
			.EmptyText(LOCTEXT("NoRunYet", "No import has run in this session."))
			.NoteText(LOCTEXT("ImportTimeNote", "Import time depends on model size, mesh complexity and the template. Complex collision makes it longer. A medium-sized building can take 5 to 15 minutes to fully process."))
			.HeaderContent()
			[
				// The last thing to check before pressing Import: where it goes, or why it cannot run.
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this]() { return ResolvedFolderText; })
				.ColorAndOpacity_Lambda([this]() { return bResolvedFolderOk ? FSlateColor::UseForeground() : MCDialogs::WarningColor(); })
			]
			.LeadingButtons()
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get(), "PrimaryButton")
				.ContentPadding(FMargin(16.f, 2.f))
				.Text(LOCTEXT("ImportButton", "Import"))
				.IsEnabled_Lambda([this]() { return bResolvedFolderOk; })
				.OnClicked(this, &SMCImportPage::OnImport)
			]
		];
}

SMCImportPage::FItem SMCImportPage::CurrentItem() const
{
	const FItem* Found = Items.FindByPredicate([this](const FItem& Item) { return Item->Name.Equals(Settings->TemplateName, ESearchCase::IgnoreCase); });
	return Found ? *Found : FItem();
}

void SMCImportPage::RefreshTemplates()
{
	Items.Reset();
	for (MCTemplates::FTemplate& Template : MCTemplates::List())
	{
		Items.Add(MakeShared<MCTemplates::FTemplate>(MoveTemp(Template)));
	}
	if (Combo.IsValid())
	{
		Combo->RefreshOptions();
		Combo->SetSelectedItem(CurrentItem());
	}
	RefreshState();
}

void SMCImportPage::UseTemplate(const FString& Name)
{
	RefreshTemplates();
	const FItem* Found = Items.FindByPredicate([&Name](const FItem& Item) { return Item->Name.Equals(Name, ESearchCase::IgnoreCase); });
	if (Found && Apply(*Found) && Combo.IsValid())
	{
		Combo->SetSelectedItem(*Found);
	}
}

void SMCImportPage::OnPicked(FItem Item, ESelectInfo::Type SelectInfo)
{
	if (!Item.IsValid() || SelectInfo == ESelectInfo::Direct || Item == CurrentItem())
	{
		return;
	}
	if (!Apply(Item))
	{
		Combo->SetSelectedItem(CurrentItem());
	}
}

bool SMCImportPage::Apply(const FItem& Item)
{
	// Settings that no template holds are lost when a template is applied, so that is confirmed first.
	if (!bMatchesTemplate && MCTemplates::ExportTemplateSettings(*Settings) != Item->Settings)
	{
		const FText Message = FText::Format(LOCTEXT("ReplaceSettings", "Replace your saved settings with {0}?\n\nThey are not in any template. Choose Cancel, then Save as template, to keep them."),
			FText::FromString(Item->Name));
		if (FMessageDialog::Open(EAppMsgType::OkCancel, Message) != EAppReturnType::Ok)
		{
			return false;
		}
	}
	MCTemplates::Apply(*Item, *Settings);
	Settings->SaveConfig();
	RefreshState();
	return true;
}

void SMCImportPage::OnSaveAsTemplate()
{
	FString Description;
	const FString Name = MCDialogs::AskTemplateName(LOCTEXT("SaveSettingsTitle", "Save settings as template"), LOCTEXT("SaveSettingsAccept", "Save"),
		TEXT("My settings"), FString(), &Description);
	if (Name.IsEmpty())
	{
		return;
	}
	FString Error;
	if (!MCTemplates::Save(Name, Description, /*bIncludeDestination*/ false, *Settings, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
		return;
	}
	Settings->TemplateName = Name;
	Settings->SaveConfig();
	RefreshTemplates();
	OnTemplatesChanged.ExecuteIfBound();
}

FReply SMCImportPage::OnImport()
{
	const FItem Current = CurrentItem();
	if (!Current.IsValid())
	{
		return FReply::Handled();
	}
	// The template is read again as saved, without its destination, so a folder typed after choosing it stays.
	RefreshTemplates();
	const FItem Fresh = CurrentItem();
	if (!Fresh.IsValid())
	{
		return FReply::Handled();
	}
	MCModels::ImportSettings(Fresh->Settings, *Settings);
	Settings->TemplateName = Fresh->Name;
	Settings->SaveConfig();

	const FMCImportResult Result = UMCLibrary::ImportModel(Settings);
	UnfinishedRun.Reset();
	RunResult->ShowResult(Result);
	RefreshState();
	OnImported.ExecuteIfBound();
	return FReply::Handled();
}

void SMCImportPage::SetSourceFile(const FString& Path)
{
	const FString Trimmed = Path.TrimStartAndEnd();
	// A name typed for one model should not silently carry over to the next file.
	if (Trimmed != Settings->SourceFile.FilePath)
	{
		Settings->bUseCustomName = false;
	}
	Settings->SourceFile.FilePath = Trimmed;
	Settings->SaveConfig();
	RefreshState();
}

void SMCImportPage::SetDestination(const FString& Path)
{
	Settings->DestinationFolder.Path = MCDialogs::ToPackagePath(Path);
	Settings->SaveConfig();
	RefreshState();
}

void SMCImportPage::RefreshState()
{
	const FItem Current = CurrentItem();
	bMatchesTemplate = Current.IsValid() && MCTemplates::ExportTemplateSettings(*Settings) == Current->Settings;

	// The summary builds a settings object, so it is only made again when the template changes.
	if (Current != SummaryFor)
	{
		SummaryFor = Current;
		SummaryText = Current.IsValid() ? FText::FromString(MCTemplates::Summarise(*Current)) : LOCTEXT("NoTemplateSummary", "Choose a template. It sets how the model is processed.");
	}

	if (!Current.IsValid())
	{
		bResolvedFolderOk = false;
		ResolvedFolderText = LOCTEXT("NeedTemplate", "Choose a template in the Model box.");
		return;
	}
	FString Error;
	const FString Folder = UMCLibrary::ResolveModelFolder(Settings, Error);
	bResolvedFolderOk = !Folder.IsEmpty();
	ResolvedFolderText = bResolvedFolderOk
		? FText::Format(LOCTEXT("ResolvedFolder", "Imports into {0}"), FText::FromString(Folder))
		: FText::FromString(Error);
}

EActiveTimerReturnType SMCImportPage::OnRefreshTimer(double InCurrentTime, float InDeltaTime)
{
	RefreshState();
	return EActiveTimerReturnType::Continue;
}

#undef LOCTEXT_NAMESPACE
