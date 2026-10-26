// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "SMCReimportPanel.h"
#include "SMCDialogs.h"
#include "SMCFields.h"
#include "SMCRunResult.h"
#include "MCLibrary.h"
#include "MCModels.h"
#include "MCSettings.h"
#include "MCTemplates.h"
#include "Editor.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

namespace
{
	/** Tall enough for every stage row and a few warning lines without scrolling the table. */
	constexpr float ReimportResultsHeight = 420.f;
}

namespace
{
	const FString LastImport(TEXT("@LastImport"));
}

void SMCReimportPanel::Construct(const FArguments& InArgs)
{
	OnReimported = InArgs._OnReimported;
	OnClose = InArgs._OnClose;
	RefreshTemplates();

	const TSharedRef<SWidget> Rows = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			MCFields::PathRow(LOCTEXT("SourceLabel", "Source file"),
				TAttribute<FText>::CreateLambda([this]() { return FText::FromString(SourcePath); }),
				[this](const FString& Text) { SourcePath = Text.TrimStartAndEnd(); },
				[this]()
				{
					const FString Picked = MCDialogs::PickDatasmithFile(AsShared(), SourcePath);
					if (!Picked.IsEmpty())
					{
						SourcePath = Picked;
					}
				})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			BuildSettingsRow()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(MCFields::LabelWidth + 8.f, 2.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text_Lambda([this]() { return SummaryText; })
		];

	const TSharedRef<SWidget> Heading = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			MCFields::Section(LOCTEXT("ReimportHeading", "Re-import"))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("CloseReimportTip", "Close the re-import section."))
			.OnClicked_Lambda([this]() { OnClose.ExecuteIfBound(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(LOCTEXT("CloseReimport", "Close"))
			]
		];

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			MCFields::Panel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					Heading
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(LOCTEXT("ReimportIntro", "Replaces the model with a fresh import of its source. The old version is removed first, then the import runs into the same folder, with the same name and the level at the same path. Levels that stream the model level keep working. Actors you added to the model level yourself are lost."))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
				[
					Rows
				],
				FMargin(8.f, 6.f, 8.f, 8.f))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, MCFields::Gap, 0.f, 0.f)
		[
			// The section sits in a scroll box, which gives no height to fill, so the results get their own.
			SAssignNew(RunResult, SMCRunResult)
			.EmptyText(LOCTEXT("NoReimportYet", "No re-import has run in this session."))
			.ResultsHeight(ReimportResultsHeight)
			.HeaderContent()
			[
				// The last thing to check before pressing Re-import: what it replaces, or why it cannot run.
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(this, &SMCReimportPanel::GetStatusText)
				.ColorAndOpacity_Lambda([this]() { return CanReimport() ? FSlateColor::UseForeground() : MCDialogs::WarningColor(); })
			]
			.LeadingButtons()
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get(), "PrimaryButton")
				.ContentPadding(FMargin(16.f, 2.f))
				.Text(LOCTEXT("ReimportButton", "Re-import..."))
				.ToolTipText(LOCTEXT("ReimportTip", "Shows what will be replaced and asks you to confirm. Then removes the old version, waits until the editor is ready, and imports again."))
				.IsEnabled_Lambda([this]() { return CanReimport(); })
				.OnClicked(this, &SMCReimportPanel::OnReimport)
			]
		]
	];
}

TSharedRef<SWidget> SMCReimportPanel::BuildSettingsRow()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			MCFields::Label(LOCTEXT("SettingsFromLabel", "Settings"))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SAssignNew(OptionCombo, SComboBox<FOption>)
			.OptionsSource(&Options)
			.ToolTipText(LOCTEXT("SettingsFromTip", "The settings the re-import uses: the ones recorded at the last import, or a template. To change one setting, save the last import settings as a template from More, edit it on the Settings page, then choose it here."))
			.OnGenerateWidget_Lambda([this](FOption Item) -> TSharedRef<SWidget>
			{
				const bool bUnavailable = *Item == LastImport && !(Current.IsValid() && Current->bHasSettings);
				return SNew(STextBlock)
					.Text(OptionLabel(*Item))
					.ColorAndOpacity(bUnavailable ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground());
			})
			.OnSelectionChanged_Lambda([this](FOption Item, ESelectInfo::Type SelectInfo)
			{
				if (!Item.IsValid() || SelectInfo == ESelectInfo::Direct)
				{
					return;
				}
				const bool bUnavailable = *Item == LastImport && !(Current.IsValid() && Current->bHasSettings);
				if (!bUnavailable)
				{
					Option = *Item;
					UpdateSummary();
				}
			})
			[
				SNew(STextBlock).Text_Lambda([this]() { return OptionLabel(Option); })
			]
		];
}

void SMCReimportPanel::RefreshTemplates()
{
	Options.Reset();
	Options.Add(MakeShared<FString>(LastImport));
	for (const FString& Name : UMCLibrary::GetTemplateNames())
	{
		Options.Add(MakeShared<FString>(Name));
	}
	if (OptionCombo.IsValid())
	{
		OptionCombo->RefreshOptions();
	}
	// A template that was renamed or deleted can no longer be used.
	if (Option != LastImport && !Options.ContainsByPredicate([this](const FOption& Item) { return *Item == Option; }))
	{
		Option = DefaultOption();
	}
	UpdateSummary();
}

FText SMCReimportPanel::OptionLabel(const FString& Value) const
{
	if (Value == LastImport)
	{
		if (Current.IsValid() && !Current->bHasSettings)
		{
			return LOCTEXT("LastMissing", "Last import settings (none recorded for this model)");
		}
		return Current.IsValid() && !Current->TemplateName.IsEmpty()
			? FText::Format(LOCTEXT("LastWithTemplate", "Last import settings ({0})"), FText::FromString(Current->TemplateName))
			: LOCTEXT("Last", "Last import settings");
	}
	return Value.IsEmpty() ? LOCTEXT("ChooseSettings", "Choose the settings") : FText::Format(LOCTEXT("TemplateOption", "Template: {0}"), FText::FromString(Value));
}

FString SMCReimportPanel::DefaultOption() const
{
	if (Current.IsValid() && Current->bHasSettings)
	{
		return LastImport;
	}
	const FString& ImportPage = GetDefault<UMCSettings>()->TemplateName;
	if (Options.ContainsByPredicate([&ImportPage](const FOption& Item) { return *Item == ImportPage; }))
	{
		return ImportPage;
	}
	return Options.Num() > 1 ? *Options[1] : FString();
}

void SMCReimportPanel::SetModel(const TSharedPtr<FMCSceneEntry>& Item)
{
	const bool bSameModel = Item.IsValid() && Current.IsValid() && Item->ScenePath == Current->ScenePath;
	Current = Item;
	if (!bSameModel)
	{
		SourcePath = Item.IsValid() ? Item->SourceFile : FString();
		Option = DefaultOption();
	}
	if (OptionCombo.IsValid())
	{
		const FOption* Match = Options.FindByPredicate([this](const FOption& Entry) { return *Entry == Option; });
		OptionCombo->SetSelectedItem(Match ? *Match : FOption());
	}
	UpdateSummary();
}

void SMCReimportPanel::UpdateSummary()
{
	if (!Current.IsValid() || Option.IsEmpty())
	{
		SummaryText = FText::GetEmpty();
		return;
	}
	if (Option == LastImport)
	{
		TStrongObjectPtr<UMCSettings> Recorded(NewObject<UMCSettings>(GetTransientPackage(), NAME_None, RF_Transient));
		if (!MCTemplates::ApplyRecorded(Current->ScenePath, *Recorded))
		{
			SummaryText = FText::GetEmpty();
			return;
		}
		MCTemplates::FTemplate FromRecord;
		FromRecord.Settings = MCTemplates::ExportTemplateSettings(*Recorded);
		SummaryText = FText::FromString(MCTemplates::Summarise(FromRecord));
		return;
	}
	const TOptional<MCTemplates::FTemplate> Template = MCTemplates::Find(Option);
	MCTemplates::FTemplate Shown = Template.IsSet() ? Template.GetValue() : MCTemplates::FTemplate();
	// A re-import keeps its folder, so a template's destination does not apply here.
	Shown.Destination.Reset();
	SummaryText = Template.IsSet() ? FText::FromString(MCTemplates::Summarise(Shown)) : FText::GetEmpty();
}

FText SMCReimportPanel::GetStatusText() const
{
	if (!Current.IsValid())
	{
		return LOCTEXT("ReimportChooseModel", "Choose a model in the list.");
	}
	if (Current->ImportId.IsEmpty())
	{
		return LOCTEXT("NotOurs", "This scene was not imported with Model Constructor, so it cannot be re-imported here.");
	}
	if (!Current->bCreatedLevel)
	{
		return LOCTEXT("NoOwnLevel", "This model was imported into a level that Model Constructor did not create, so it cannot be re-imported. Remove it and import it again instead.");
	}
	if (SourcePath.IsEmpty())
	{
		return LOCTEXT("NoSource", "Choose the source file.");
	}
	if (!FPaths::FileExists(SourcePath))
	{
		return FText::Format(LOCTEXT("SourceMissing", "Source file not found: {0}"), FText::FromString(SourcePath));
	}
	if (Option.IsEmpty())
	{
		return LOCTEXT("NoSettingsChoice", "Choose the settings to use.");
	}
	if (GEditor && GEditor->IsPlaySessionInProgress())
	{
		return LOCTEXT("StopPie", "Stop Play In Editor before re-importing.");
	}
	return FText::Format(LOCTEXT("ReimportReplaces", "Replaces {0}"), FText::FromString(Current->Folder));
}

bool SMCReimportPanel::CanReimport() const
{
	return Current.IsValid() && !Current->ImportId.IsEmpty() && Current->bCreatedLevel && !SourcePath.IsEmpty()
		&& FPaths::FileExists(SourcePath) && !Option.IsEmpty() && !(GEditor && GEditor->IsPlaySessionInProgress());
}

FReply SMCReimportPanel::OnReimport()
{
	if (!CanReimport())
	{
		return FReply::Handled();
	}
	const FString ScenePath = Current->ScenePath;
	const FMCRemovalPlan Plan = UMCLibrary::PlanReimportModel(ScenePath);

	TArray<FString> Extra;
	Extra.Add(FString::Printf(TEXT("The model level %s is deleted and made again at the same path, so levels that stream it keep working."), *Plan.LevelPath));
	Extra.Add(TEXT("Actors you added to the model level yourself are lost."));
	Extra.Add(FString::Printf(TEXT("Source: %s"), *SourcePath));
	Extra.Add(FString::Printf(TEXT("Settings: %s"), *OptionLabel(Option).ToString()));

	const FText Heading = FText::Format(LOCTEXT("ReimportConfirmHeading", "Re-import {0}?"), FText::FromString(Current->DisplayName));
	if (!MCDialogs::ConfirmPlan(Plan, LOCTEXT("ReimportTitle", "Re-import model"), Heading, LOCTEXT("ReimportAccept", "Re-import"), Extra))
	{
		return FReply::Handled();
	}

	// General settings come from the user's current ones; only the template part changes with the choice.
	TStrongObjectPtr<UMCSettings> Job(NewObject<UMCSettings>(GetTransientPackage(), NAME_None, RF_Transient));
	Job->CopySettingsFrom(*GetDefault<UMCSettings>());
	if (Option == LastImport)
	{
		MCTemplates::ApplyRecorded(ScenePath, *Job);
	}
	else if (const TOptional<MCTemplates::FTemplate> Template = MCTemplates::Find(Option))
	{
		MCModels::ImportSettings(Template->Settings, *Job);
		Job->TemplateName = Template->Name;
	}
	Job->SourceFile.FilePath = SourcePath;

	const FMCImportResult Result = UMCLibrary::ReimportModel(ScenePath, Plan.Fingerprint, Job.Get());
	RunResult->ShowResult(Result);
	OnReimported.ExecuteIfBound();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
