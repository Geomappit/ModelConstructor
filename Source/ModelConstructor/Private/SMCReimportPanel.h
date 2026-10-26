// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "MCTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

class SMCRunResult;

/**
 * The re-import section of Model Manager: source file, where the settings come from, the run button and
 * the run results. Replaces the model with a fresh import into the same folder, name and level path.
 */
class SMCReimportPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMCReimportPanel) {}
		/** Fires after every re-import, successful or not. */
		SLATE_EVENT(FSimpleDelegate, OnReimported)
		/** Fires when the user closes the section. */
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Shows a model. Keeps the source and settings choice when it is the same model as before. */
	void SetModel(const TSharedPtr<FMCSceneEntry>& Item);

	/** Reads the template names again. */
	void RefreshTemplates();

private:
	using FOption = TSharedPtr<FString>;

	TSharedRef<SWidget> BuildSettingsRow();
	FText OptionLabel(const FString& Option) const;
	FString DefaultOption() const;
	void UpdateSummary();

	FText GetStatusText() const;
	bool CanReimport() const;
	FReply OnReimport();

	FSimpleDelegate OnReimported;
	FSimpleDelegate OnClose;

	TSharedPtr<FMCSceneEntry> Current;
	FString SourcePath;
	/** The last import's settings, or a template name. */
	FString Option;
	TArray<FOption> Options;
	TSharedPtr<SComboBox<FOption>> OptionCombo;
	FText SummaryText;
	TSharedPtr<SMCRunResult> RunResult;
};
