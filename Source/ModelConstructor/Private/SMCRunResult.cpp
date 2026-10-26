// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "SMCRunResult.h"
#include "SMCDialogs.h"
#include "ContentBrowserModule.h"
#include "FileHelpers.h"
#include "IContentBrowserSingleton.h"
#include "MCLibrary.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

namespace
{
	const FName ColStage(TEXT("Stage"));
	const FName ColState(TEXT("State"));
	const FName ColChanged(TEXT("Changed"));
	const FName ColAlreadyOk(TEXT("AlreadyOk"));
	const FName ColTotal(TEXT("Total"));
	const FName ColSeconds(TEXT("Seconds"));
	const FName ColDetail(TEXT("Detail"));

	FText StateText(EMCStageState State)
	{
		return StaticEnum<EMCStageState>()->GetDisplayNameTextByValue(static_cast<int64>(State));
	}

	FSlateColor StateColor(EMCStageState State)
	{
		switch (State)
		{
		case EMCStageState::Failed:
			return FSlateColor(FLinearColor(0.9f, 0.3f, 0.25f));
		case EMCStageState::Cancelled:
			return MCDialogs::WarningColor();
		case EMCStageState::Skipped:
		case EMCStageState::Pending:
			return FSlateColor::UseSubduedForeground();
		default:
			return FSlateColor::UseForeground();
		}
	}

	class SMCStageRow : public SMultiColumnTableRow<TSharedPtr<FMCStageResult>>
	{
	public:
		SLATE_BEGIN_ARGS(SMCStageRow) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedPtr<FMCStageResult> InItem)
		{
			Item = InItem;
			SMultiColumnTableRow<TSharedPtr<FMCStageResult>>::Construct(FSuperRowType::FArguments(), OwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
		{
			FText Text;
			FSlateColor Color = FSlateColor::UseForeground();
			// Only pass stages change assets; the other stages report a total alone.
			const bool bPassCounts = Item->Changed + Item->AlreadyOk > 0;
			if (Column == ColStage)
			{
				Text = FText::FromString(Item->Name);
			}
			else if (Column == ColState)
			{
				Text = StateText(Item->State);
				Color = StateColor(Item->State);
			}
			else if (Column == ColChanged)
			{
				Text = bPassCounts ? FText::AsNumber(Item->Changed) : FText::GetEmpty();
			}
			else if (Column == ColAlreadyOk)
			{
				Text = bPassCounts ? FText::AsNumber(Item->AlreadyOk) : FText::GetEmpty();
			}
			else if (Column == ColTotal)
			{
				Text = Item->Total > 0 ? FText::AsNumber(Item->Total) : FText::GetEmpty();
			}
			else if (Column == ColSeconds)
			{
				Text = Item->State == EMCStageState::Pending ? FText::GetEmpty() : FText::FromString(FString::Printf(TEXT("%.1f"), Item->Seconds));
			}
			else if (Column == ColDetail)
			{
				Text = FText::FromString(Item->Detail);
			}

			return SNew(SBox)
				.Padding(FMargin(4.f, 1.f))
				[
					SNew(STextBlock)
					.Text(Text)
					.ColorAndOpacity(Color)
					.ToolTipText(Column == ColDetail ? Text : FText::GetEmpty())
				];
		}

	private:
		TSharedPtr<FMCStageResult> Item;
	};
}

void SMCRunResult::Construct(const FArguments& InArgs)
{
	EmptyText = InArgs._EmptyText;

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 6.f, 8.f, 0.f)
		[
			InArgs._HeaderContent.Widget
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				InArgs._LeadingButtons.Widget
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenLevelButton", "Open level"))
				.ToolTipText(LOCTEXT("OpenLevelTip", "Open the level that holds the model from the last run."))
				.IsEnabled_Lambda([this]() { return LastResult.IsSet() && LastResult->bSucceeded && !LastResult->LevelPath.IsEmpty(); })
				.OnClicked(this, &SMCRunResult::OnOpenLevel)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("ShowButton", "Show in Content Browser"))
				.IsEnabled_Lambda([this]() { return LastResult.IsSet() && !LastResult->Folder.IsEmpty(); })
				.OnClicked(this, &SMCRunResult::OnShowInContentBrowser)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f, 8.f, 2.f)
		[
			SNew(STextBlock)
			.Text(InArgs._NoteText)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Visibility(InArgs._NoteText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 2.f)
		[
			SNew(STextBlock)
			.Text(this, &SMCRunResult::GetSummaryText)
			.AutoWrapText(true)
		];

	const TSharedRef<SWidget> Results =
		// Stages and warnings scroll on their own; drag the bar between them to give either more room.
		SNew(SSplitter)
		.Orientation(Orient_Vertical)
		+ SSplitter::Slot().Value(0.65f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(0.f)
			[
				SAssignNew(StageList, SListView<TSharedPtr<FMCStageResult>>)
				.ListItemsSource(&StageItems)
				.SelectionMode(ESelectionMode::None)
				.OnGenerateRow(this, &SMCRunResult::OnGenerateStageRow)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(ColStage).DefaultLabel(LOCTEXT("ColStage", "Stage")).FillWidth(0.18f)
					+ SHeaderRow::Column(ColState).DefaultLabel(LOCTEXT("ColState", "State")).FillWidth(0.1f)
					+ SHeaderRow::Column(ColChanged).DefaultLabel(LOCTEXT("ColChanged", "Changed")).FillWidth(0.08f)
					+ SHeaderRow::Column(ColAlreadyOk).DefaultLabel(LOCTEXT("ColAlreadyOk", "Already OK")).FillWidth(0.09f)
					+ SHeaderRow::Column(ColTotal).DefaultLabel(LOCTEXT("ColTotal", "Total")).FillWidth(0.07f)
					+ SHeaderRow::Column(ColSeconds).DefaultLabel(LOCTEXT("ColSeconds", "Seconds")).FillWidth(0.08f)
					+ SHeaderRow::Column(ColDetail).DefaultLabel(LOCTEXT("ColDetail", "Detail")).FillWidth(0.4f)
				)
			]
		]
		+ SSplitter::Slot().Value(0.35f)
		[
			SAssignNew(WarningsBox, SMultiLineEditableTextBox)
			.IsReadOnly(true)
			.AutoWrapText(true)
			.HintText(LOCTEXT("WarningsHint", "Warnings from the last run appear here."))
		];

	if (InArgs._ResultsHeight > 0.f)
	{
		Box->AddSlot().AutoHeight().Padding(8.f, 2.f, 8.f, 8.f)
		[
			SNew(SBox)
			.HeightOverride(InArgs._ResultsHeight)
			[
				Results
			]
		];
	}
	else
	{
		Box->AddSlot().FillHeight(1.f).Padding(8.f, 2.f, 8.f, 8.f)
		[
			Results
		];
	}

	ChildSlot
	[
		Box
	];
}

void SMCRunResult::ShowResult(const FMCImportResult& Result)
{
	LastResult = Result;
	StageItems.Reset();
	for (const FMCStageResult& Stage : Result.Stages)
	{
		StageItems.Add(MakeShared<FMCStageResult>(Stage));
	}
	if (StageList.IsValid())
	{
		StageList->RequestListRefresh();
	}
	if (WarningsBox.IsValid())
	{
		WarningsBox->SetText(FText::FromString(FString::Join(Result.Warnings, LINE_TERMINATOR)));
	}
}

FText SMCRunResult::GetSummaryText() const
{
	if (!LastResult.IsSet())
	{
		return EmptyText;
	}
	const FMCImportResult& Result = LastResult.GetValue();
	if (!Result.bSucceeded)
	{
		return FText::Format(LOCTEXT("SummaryFailed", "Stopped: {0}"), FText::FromString(Result.Error));
	}
	const FText Format = Result.bReimport
		? LOCTEXT("SummaryReimported", "Re-imported into {0} in {1} s with {2} warnings.")
		: LOCTEXT("SummaryDone", "Finished {0} in {1} s with {2} warnings.");
	return FText::Format(Format,
		FText::FromString(Result.Folder),
		FText::AsNumber(FMath::RoundToInt(Result.TotalSeconds)),
		FText::AsNumber(Result.Warnings.Num()));
}

FReply SMCRunResult::OnOpenLevel()
{
	if (LastResult.IsSet() && !LastResult->LevelPath.IsEmpty())
	{
		UMCLibrary::OpenLevel(LastResult->LevelPath);
	}
	return FReply::Handled();
}

FReply SMCRunResult::OnShowInContentBrowser()
{
	if (LastResult.IsSet() && !LastResult->Folder.IsEmpty())
	{
		FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		ContentBrowser.Get().SyncBrowserToFolders({ LastResult->Folder });
	}
	return FReply::Handled();
}

TSharedRef<ITableRow> SMCRunResult::OnGenerateStageRow(TSharedPtr<FMCStageResult> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SMCStageRow, OwnerTable, Item);
}

#undef LOCTEXT_NAMESPACE
