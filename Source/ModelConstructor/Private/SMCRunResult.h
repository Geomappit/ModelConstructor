// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "MCTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SMultiLineEditableTextBox;

/** The result of the last run: summary, one row per stage, the warnings, and buttons to open the level or folder. */
class SMCRunResult : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMCRunResult) : _ResultsHeight(0.f) {}
		/** Shown before the first run. */
		SLATE_ARGUMENT(FText, EmptyText)
		/** Shown above the button row, such as where the run will import to. */
		SLATE_NAMED_SLOT(FArguments, HeaderContent)
		/** Extra content at the start of the button row, such as the page's run button. */
		SLATE_NAMED_SLOT(FArguments, LeadingButtons)
		/** Optional guidance shown under the button row. Hidden when empty. */
		SLATE_ARGUMENT(FText, NoteText)
		/** Fixed height for the stage table and warnings. 0 fills the space left, which needs a parent that gives a height, not a scroll box. */
		SLATE_ARGUMENT(float, ResultsHeight)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void ShowResult(const FMCImportResult& Result);

private:
	FText GetSummaryText() const;
	FReply OnOpenLevel();
	FReply OnShowInContentBrowser();
	TSharedRef<ITableRow> OnGenerateStageRow(TSharedPtr<FMCStageResult> Item, const TSharedRef<STableViewBase>& OwnerTable);

	FText EmptyText;
	TOptional<FMCImportResult> LastResult;
	TArray<TSharedPtr<FMCStageResult>> StageItems;
	TSharedPtr<SListView<TSharedPtr<FMCStageResult>>> StageList;
	TSharedPtr<SMultiLineEditableTextBox> WarningsBox;
};
