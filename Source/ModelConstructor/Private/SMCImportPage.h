// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "MCTemplates.h"
#include "MCTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

class SMCRunResult;
class UMCSettings;

/**
 * Import page: the Model box (template, source, destination, name) and the run with its results.
 * Settings are edited on the Settings page; every import uses the chosen template as saved.
 */
class SMCImportPage : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnTemplateNamed, const FString& /*Name*/);

	SLATE_BEGIN_ARGS(SMCImportPage) {}
		/** Fires after every import, successful or not, so other pages can refresh. */
		SLATE_EVENT(FSimpleDelegate, OnImported)
		/** Fires when the user asks to edit a template. */
		SLATE_EVENT(FOnTemplateNamed, OnEditTemplate)
		/** Fires after the page saves a new template. */
		SLATE_EVENT(FSimpleDelegate, OnTemplatesChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Reads the templates again, for changes made on the Settings page or on disk. */
	void RefreshTemplates();

	/** Chooses a template for the next import, asking first if that replaces settings no template holds. */
	void UseTemplate(const FString& Name);

private:
	using FItem = TSharedPtr<MCTemplates::FTemplate>;

	TSharedRef<SWidget> BuildModelBox();
	TSharedRef<SWidget> BuildTemplateRow();
	TSharedRef<SWidget> BuildNameRow();
	TSharedRef<SWidget> BuildTemplateBanner();
	TSharedRef<SWidget> BuildUnfinishedBanner();
	TSharedRef<SWidget> BuildRunArea();

	FItem CurrentItem() const;
	void OnPicked(FItem Item, ESelectInfo::Type SelectInfo);
	bool Apply(const FItem& Item);
	void OnSaveAsTemplate();

	FReply OnImport();
	void SetSourceFile(const FString& Path);
	void SetDestination(const FString& Path);
	void RefreshState();
	EActiveTimerReturnType OnRefreshTimer(double InCurrentTime, float InDeltaTime);

	FSimpleDelegate OnImported;
	FOnTemplateNamed OnEditTemplate;
	FSimpleDelegate OnTemplatesChanged;

	UMCSettings* Settings = nullptr;
	TArray<FItem> Items;
	TSharedPtr<SComboBox<FItem>> Combo;

	FText SummaryText;
	/** The template SummaryText describes. Items are replaced on every refresh, so a refresh makes a new summary. */
	FItem SummaryFor;
	FText ResolvedFolderText;
	bool bResolvedFolderOk = false;
	/** True when the saved settings equal the chosen template, so nothing is lost by applying it. */
	bool bMatchesTemplate = true;

	TSharedPtr<SMCRunResult> RunResult;
	/** The run the journal says never finished, or empty. */
	FString UnfinishedRun;
};
