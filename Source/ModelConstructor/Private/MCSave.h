// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "MCTypes.h"

class UMCSettings;

/** Checks before an import, and the save at the end of a run. */
namespace MCSave
{
	/**
	 * Checks the source file size, free disk space and free memory before anything changes.
	 * Returns false with OutError when the import must not start; adds warnings for the rest.
	 */
	bool Preflight(const FString& SourceFile, TArray<FString>& Warnings, FString& OutError);

	struct FRequest
	{
		FString Folder;
		FString ScenePath;
		/** Save the open level as well. Off for passes over a folder, unless the level is in that folder. */
		bool bSaveLevel = false;
		/** Record the package list and the settings on the scene asset before saving. */
		const UMCSettings* RecordSettings = nullptr;
		bool bRecordManifest = false;
	};

	/**
	 * Saves the model folder in batches: content first, then the scene asset, then the level once.
	 * Each batch is logged, so the Output Log shows where a save stopped.
	 */
	bool SaveModel(const FRequest& Request, FMCImportResult& Result, FMCStageResult& Stage);
}
