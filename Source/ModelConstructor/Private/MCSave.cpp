// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "MCSave.h"
#include "MCModels.h"
#include "MCTemplates.h"
#include "MCPasses.h"
#include "MCRun.h"
#include "MCSettings.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "LevelEditorSubsystem.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

namespace MCSave
{
namespace
{
	constexpr int64 MB = 1024 * 1024;

	/**
	 * Datasmith copies the whole scene description into one buffer that cannot pass 2 GB, and the
	 * editor stops when it does. The .udatasmith file is that description, so its size is a good guide.
	 */
	constexpr int64 RefuseSceneFileBytes = 1900 * MB;
	constexpr int64 WarnSceneFileBytes = 1000 * MB;

	/** The .udatasmith file plus the <Name>_Assets folder Datasmith exporters write beside it. */
	int64 SourceBytes(const FString& SourceFile)
	{
		int64 Bytes = FMath::Max<int64>(IFileManager::Get().FileSize(*SourceFile), 0);
		const FString AssetsFolder = FPaths::GetPath(SourceFile) / (FPaths::GetBaseFilename(SourceFile) + TEXT("_Assets"));
		if (IFileManager::Get().DirectoryExists(*AssetsFolder))
		{
			IFileManager::Get().IterateDirectoryRecursively(*AssetsFolder, [&Bytes](const TCHAR* Path, bool bIsDirectory)
			{
				if (!bIsDirectory)
				{
					Bytes += FMath::Max<int64>(IFileManager::Get().FileSize(Path), 0);
				}
				return true;
			});
		}
		return Bytes;
	}

	FString Megabytes(int64 Bytes)
	{
		return FString::Printf(TEXT("%lld MB"), Bytes / MB);
	}

	bool SaveBatch(const TArray<UPackage*>& Batch)
	{
		return Batch.Num() == 0 || UEditorLoadingAndSavingUtils::SavePackages(Batch, /*bOnlyDirty*/ true);
	}
}

bool Preflight(const FString& SourceFile, TArray<FString>& Warnings, FString& OutError)
{
	const int64 SceneBytes = IFileManager::Get().FileSize(*SourceFile);
	if (SceneBytes > RefuseSceneFileBytes)
	{
		OutError = FString::Printf(TEXT("%s is %s. Datasmith cannot handle a scene description this large (the limit is about 2 GB) and the editor would stop. Export the model in parts."),
			*FPaths::GetCleanFilename(SourceFile), *Megabytes(SceneBytes));
		return false;
	}
	if (SceneBytes > WarnSceneFileBytes)
	{
		MCPasses::AddWarning(Warnings, FString::Printf(TEXT("%s is %s. Scene descriptions near 2 GB can stop the editor; consider exporting the model in parts."),
			*FPaths::GetCleanFilename(SourceFile), *Megabytes(SceneBytes)));
	}

	const int64 Total = SourceBytes(SourceFile);

	uint64 DiskTotal = 0;
	uint64 DiskFree = 0;
	if (FPlatformMisc::GetDiskTotalAndFreeSpace(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()), DiskTotal, DiskFree)
		&& static_cast<int64>(DiskFree) < Total * 3)
	{
		MCPasses::AddWarning(Warnings, FString::Printf(TEXT("Only %s free on the project drive for a %s source. The import can need about three times the source size."),
			*Megabytes(static_cast<int64>(DiskFree)), *Megabytes(Total)));
	}

	const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
	if (static_cast<int64>(Memory.AvailablePhysical) < Total * 2)
	{
		MCPasses::AddWarning(Warnings, FString::Printf(TEXT("Only %s of memory free for a %s source. Close other programs, or lower the batch sizes under Run."),
			*Megabytes(static_cast<int64>(Memory.AvailablePhysical)), *Megabytes(Total)));
	}

	MCRun::LogStep(FString::Printf(TEXT("Source %s: scene file %s, %s in total"), *SourceFile, *Megabytes(SceneBytes), *Megabytes(Total)));
	return true;
}

bool SaveModel(const FRequest& Request, FMCImportResult& Result, FMCStageResult& Stage)
{
	const FString& Folder = Request.Folder;
	const FString ScenePackageName = FSoftObjectPath(Request.ScenePath).GetLongPackageName();
	const FString WorldPackageName = MCModels::CurrentMapPackage();
	UObject* Scene = Request.ScenePath.IsEmpty() ? nullptr : FSoftObjectPath(Request.ScenePath).TryLoad();

	// The record goes on the scene first, so it is saved with everything else in one pass.
	if (Scene && Request.RecordSettings)
	{
		MCModels::SetTag(Scene, MCModels::Tags::Settings, MCModels::ExportSettings(*Request.RecordSettings));
		MCModels::SetTag(Scene, MCModels::Tags::HasSettings, TEXT("true"));
		MCModels::SetTag(Scene, MCModels::Tags::Template, MCTemplates::Describe(*Request.RecordSettings));
		Scene->MarkPackageDirty();
	}
	if (Scene && Request.bRecordManifest)
	{
		const int32 Recorded = MCModels::WriteManifest(Scene, Folder);
		if (Recorded == 0)
		{
			MCPasses::AddWarning(Result.Warnings, TEXT("The list of imported assets was not recorded, so Browse cannot remove this model"));
		}
		Result.Counters.Add(TEXT("RecordedPackages"), Recorded);
	}

	const MCModels::FFolderScan Scan = MCModels::ScanFolder(Folder);
	TArray<UPackage*> Content;
	bool bWorldInFolder = false;
	for (const FName& Name : Scan.Packages)
	{
		const FString NameString = Name.ToString();
		if (NameString == WorldPackageName)
		{
			bWorldInFolder = true;
			continue;
		}
		if (NameString == ScenePackageName)
		{
			continue;
		}
		UPackage* Package = FindPackage(nullptr, *NameString);
		if (Package && Package->IsDirty())
		{
			Content.Add(Package);
		}
	}
	Content.Sort([](const UPackage& A, const UPackage& B) { return A.GetFName().LexicalLess(B.GetFName()); });

	const int32 BatchSize = FMath::Max(1, Request.RecordSettings ? Request.RecordSettings->SaveBatchSize : GetDefault<UMCSettings>()->SaveBatchSize);
	const int32 Batches = (Content.Num() + BatchSize - 1) / BatchSize;
	MCRun::LogStep(FString::Printf(TEXT("Saving %d changed packages in %s in %d batches"), Content.Num(), *Folder, Batches));

	// No cancel here: stopping halfway through a save leaves a folder that is harder to clean up than a finished one.
	FScopedSlowTask Task(static_cast<float>(Batches + 2), LOCTEXT("Saving", "Saving model"));
	for (int32 Start = 0; Start < Content.Num(); Start += BatchSize)
	{
		const int32 Count = FMath::Min(BatchSize, Content.Num() - Start);
		Task.EnterProgressFrame(1.f, FText::FromString(FString::Printf(TEXT("Saving packages %d-%d of %d"), Start + 1, Start + Count, Content.Num())));
		MCRun::LogStep(FString::Printf(TEXT("Saving packages %d-%d of %d, first %s"), Start + 1, Start + Count, Content.Num(), *Content[Start]->GetName()));

		const TArray<UPackage*> Batch(Content.GetData() + Start, Count);
		if (!SaveBatch(Batch))
		{
			return MCRun::Fail(Stage, FString::Printf(TEXT("Could not save packages %d-%d of %d in %s. See the Output Log."), Start + 1, Start + Count, Content.Num(), *Folder));
		}
	}

	Task.EnterProgressFrame(1.f, LOCTEXT("SavingScene", "Saving the scene asset"));
	if (UPackage* ScenePackage = ScenePackageName.IsEmpty() ? nullptr : FindPackage(nullptr, *ScenePackageName))
	{
		MCRun::LogStep(FString::Printf(TEXT("Saving scene %s"), *ScenePackageName));
		if (!SaveBatch({ ScenePackage }))
		{
			return MCRun::Fail(Stage, FString::Printf(TEXT("Could not save the scene asset %s"), *ScenePackageName));
		}
	}

	Task.EnterProgressFrame(1.f, LOCTEXT("SavingLevel", "Saving the level"));
	if (Request.bSaveLevel || bWorldInFolder)
	{
		if (MCModels::IsSavedMap(WorldPackageName))
		{
			MCRun::LogStep(FString::Printf(TEXT("Saving level %s"), *WorldPackageName));
			ULevelEditorSubsystem* LevelSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
			if (!LevelSubsystem || !LevelSubsystem->SaveCurrentLevel())
			{
				return MCRun::Fail(Stage, TEXT("Could not save the current level"));
			}
		}
		else if (Request.bSaveLevel)
		{
			MCPasses::AddWarning(Result.Warnings, TEXT("The open level is untitled and was not saved. Save it to keep the scene actor."));
		}
	}

	if (!Folder.StartsWith(TEXT("/Game/")))
	{
		MCPasses::CheckSelfContained(Folder, Result.Warnings);
	}
	Stage.Total = Content.Num();
	Stage.Detail = FString::Printf(TEXT("Saved %s: %d packages in %d batches"), *Folder, Content.Num(), Batches);
	return true;
}
}

#undef LOCTEXT_NAMESPACE
