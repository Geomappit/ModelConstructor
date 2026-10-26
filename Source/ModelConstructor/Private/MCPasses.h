// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "MCTypes.h"

class UMCSettings;
class UStaticMesh;

DECLARE_LOG_CATEGORY_EXTERN(LogModelConstructor, Log, All);

namespace MCPasses
{
	struct FContext
	{
		FString Folder;
		const UMCSettings& Settings;
		TArray<FString>& Warnings;
		TMap<FString, int32>& Counters;
	};

	/**
	 * Each pass fills Stage. State is left Running on success; the runner marks it Done.
	 * Every pass works in batches and waits for each batch to finish building, so memory stays bounded.
	 */
	void RunMeshes(const FContext& Context, FMCStageResult& Stage);
	void RunTexture(const FContext& Context, FMCStageResult& Stage);
	void RunMaterial(const FContext& Context, FMCStageResult& Stage);
	void RunReport(const FContext& Context, FMCImportResult& Result, FMCStageResult& Stage);

	/** Waits for asynchronous mesh, texture and shader builds. */
	void FinishBuilds(FMCStageResult& Stage);

	/** Warns about packages in the folder that reference project content. Reads dependencies from saved files. */
	void CheckSelfContained(const FString& Folder, TArray<FString>& Warnings);

	void AddWarning(TArray<FString>& Warnings, const FString& Message);

	/** "N what: a, b, c and N more". Nothing is added when Names is empty. */
	void AddListWarning(TArray<FString>& Warnings, const FString& What, const TArray<FString>& Names);

	/** "N changed, N already correct" unless a detail was already set. */
	void SetCountDetail(FMCStageResult& Stage);

	/** Assets of a class under a folder, sorted by package name. */
	TArray<FAssetData> FindAssets(const FString& Folder, const UClass* Class);

	template <typename T>
	TArray<T*> LoadAssets(const FString& Folder)
	{
		TArray<T*> Out;
		for (const FAssetData& Data : FindAssets(Folder, T::StaticClass()))
		{
			if (T* Asset = Cast<T>(Data.GetAsset()))
			{
				Out.Add(Asset);
			}
		}
		return Out;
	}

	/** Triangles in the mesh's source: the Nanite triangle count when Nanite data is built, else LOD 0. */
	int64 SourceTriangles(const UStaticMesh* Mesh);
}
