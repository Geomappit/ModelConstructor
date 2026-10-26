// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "MCTypes.h"
#include "Styling/SlateColor.h"
#include "Widgets/SWidget.h"

/** Windows, pickers and colours shared by the pages. */
namespace MCDialogs
{
	/** Orange used for blockers, changed sources and unsaved changes. */
	FSlateColor WarningColor();

	/** Shows Content in a modal window with an accept button and Cancel. True when the user accepted. */
	bool RunModal(const FText& Title, const TSharedRef<SWidget>& Content, const FText& AcceptText, TFunction<bool()> CanAccept, FVector2D Size);

	/**
	 * Shows what a removal or re-import plan will delete and asks the user to confirm.
	 * True when the user pressed the accept button and the plan can run.
	 */
	bool ConfirmPlan(const FMCRemovalPlan& Plan, const FText& Title, const FText& Heading, const FText& AcceptText, const TArray<FString>& ExtraLines);

	/**
	 * Asks for a template name, checked as the user types, and optionally a description.
	 * ExistingName is the template being renamed, so its own name is allowed. Empty when cancelled.
	 */
	FString AskTemplateName(const FText& Title, const FText& AcceptText, const FString& Initial, const FString& ExistingName, FString* InOutDescription);

	/** Opens a file dialog for a .udatasmith file. Empty when cancelled. */
	FString PickDatasmithFile(const TSharedPtr<SWidget>& Parent, const FString& Current);

	/** Opens a content folder picker. Returns a package path such as /Game/Models, or empty when cancelled. */
	FString PickContentFolder(const FString& Current);

	/** Content Browser pickers can report /All/... virtual paths; the importer needs package paths. */
	FString ToPackagePath(FString Path);
}
