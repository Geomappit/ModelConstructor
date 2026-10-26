// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "MCTypes.h"

/** Removing a model, on its own or as the first half of a re-import. */
namespace MCRemove
{
	/** Lists what would be deleted, or why it cannot be. Changes nothing. */
	FMCRemovalPlan PlanRemoval(const FString& ScenePath, bool bForReimport);

	/**
	 * Deletes the model's packages. Closes the open level first when it is part of the model or streams
	 * part of it, unloads everything of the model, then deletes the files without loading them.
	 * With source control on, the engine delete is used instead, which loads the model first.
	 */
	FMCRemovalResult Remove(const FString& ScenePath, const FString& Fingerprint, bool bForReimport);

	/**
	 * Empty when the editor is ready to import into Folder: no play session, no assets loading or building,
	 * nothing of Packages in memory, and nothing left under Folder in the asset registry. Otherwise the reason.
	 */
	FString CheckEditorReady(const FString& Folder, const TArray<FString>& Packages);
}
