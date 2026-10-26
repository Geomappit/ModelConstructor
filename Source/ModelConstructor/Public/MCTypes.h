// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "MCTypes.generated.h"

UENUM(BlueprintType)
enum class EMCCollisionMode : uint8
{
	Complex UMETA(ToolTip = "Remove simple shapes and use the render mesh for collision queries. Traces hit the real surface."),
	SimpleBox UMETA(DisplayName = "Simple Box", ToolTip = "One box around each mesh, used for every query. Cheap to build, save and run. Traces hit the box, not the surface."),
	KeepImported UMETA(DisplayName = "Keep Imported", ToolTip = "Leave collision as the importer created it."),
	NoCollision UMETA(DisplayName = "None", ToolTip = "Remove simple shapes and leave the mesh with no collision geometry.")
};

UENUM(BlueprintType)
enum class EMCDuplicateFolderAction : uint8
{
	Suffix UMETA(ToolTip = "Import into <Name>_2, <Name>_3 and so on."),
	Refuse UMETA(ToolTip = "Stop with an error.")
};

UENUM(BlueprintType)
enum class EMCAfterRun : uint8
{
	ReturnToPrevious UMETA(DisplayName = "Return to previous level", ToolTip = "Reopen the level that was open before the run, or a blank level if it was untitled. The model level is closed, which frees its memory."),
	OpenModelLevel UMETA(DisplayName = "Open the model level", ToolTip = "Leave the model's level open in the editor.")
};

UENUM(BlueprintType)
enum class EMCStageState : uint8
{
	Pending,
	Running,
	Done,
	Skipped,
	Failed,
	Cancelled
};

USTRUCT(BlueprintType)
struct MODELCONSTRUCTOR_API FMCStageResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	EMCStageState State = EMCStageState::Pending;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	int32 Changed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	int32 AlreadyOk = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	int32 Total = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	float Seconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString Detail;
};

USTRUCT(BlueprintType)
struct MODELCONSTRUCTOR_API FMCImportResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	bool bSucceeded = false;

	/** Why the run stopped. Empty when it succeeded. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString Error;

	/** Content folder that holds the model. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString Folder;

	/** Object path of the Datasmith scene asset. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString ScenePath;

	/** Package path of the level holding the scene actor. Empty when that level is untitled. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString LevelPath;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	TArray<FMCStageResult> Stages;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	TArray<FString> Warnings;

	/** Asset and actor counts from the report stage. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	TMap<FString, int32> Counters;

	/** World bounds of the model's mesh actors. Invalid when the scene actor is not in the open level. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FBox Bounds = FBox(ForceInit);

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	float TotalSeconds = 0.f;

	/** True when the run replaced an earlier import of the same model. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	bool bReimport = false;
};

/** One Datasmith scene asset found in the project or in a content plugin. */
USTRUCT(BlueprintType)
struct MODELCONSTRUCTOR_API FMCSceneEntry
{
	GENERATED_BODY()

	/** Name given at import, else the folder name. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString AssetName;

	/** The model folder: the folder Model Constructor created, or the scene asset's own folder for other imports. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString Folder;

	/** Object path of the scene asset. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString ScenePath;

	/** "Project", or the name of the content plugin that holds the scene. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString Root;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString SourceFile;

	/** Import time when Model Constructor recorded it, else the time the scene asset was last saved. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FDateTime Time;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	bool bTimeIsImport = false;

	/** Last-modified time of the source file on disk now. Not set when the file is missing. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FDateTime SourceModified;

	/** Six-character id written by Model Constructor. Empty for scenes imported another way. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString ImportId;

	/** Saved levels that reference the scene asset. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	TArray<FString> Levels;

	/** The level the import placed the model in, as recorded at import. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString LevelPath;

	/** True when Model Constructor created that level for the model. Re-import needs this. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	bool bCreatedLevel = false;

	/** True when the settings used for the import were recorded on the scene asset. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	bool bHasSettings = false;

	/** The template the last import started from, or empty. Ends in " (changed)" when settings were edited. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString TemplateName;
};

/** What RemoveModel would delete, and why it might refuse. Nothing is changed by building a plan. */
USTRUCT(BlueprintType)
struct MODELCONSTRUCTOR_API FMCRemovalPlan
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	bool bCanRemove = false;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString ScenePath;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString Folder;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString DiskFolder;

	/** Packages that would be deleted, sorted. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	TArray<FString> Packages;

	/** Asset count per class name. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	TMap<FString, int32> AssetsByClass;

	/** Package files on disk that would be deleted. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	int32 FileCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	int64 Bytes = 0;

	/** Files in the folder that are not packages. They are left in place, so the folder stays. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	int32 OtherFileCount = 0;

	/** Loaded packages with unsaved changes that would be deleted too. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	int32 UnsavedCount = 0;

	/** The open level, when it is part of the model or streams part of it, and would be closed first. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString OpenLevelToClose;

	/** True for the plan of a re-import: the model level may be used by other levels, because it comes back at the same path. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	bool bForReimport = false;

	/** The model's level, when the import created one. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString LevelPath;

	/** True when the files are deleted straight from disk without loading the model. False when source control is on and the engine delete is used, which loads the model first. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	bool bDeletesWithoutLoading = true;

	/** Information that does not stop the removal, for example levels that stream the model level. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	TArray<FString> Notes;

	/** Reasons removal is refused. Empty when bCanRemove is true. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	TArray<FString> Blockers;

	/** Pass this to RemoveModel. It changes when the package list changes, which stops a removal based on an old review. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString Fingerprint;
};

USTRUCT(BlueprintType)
struct MODELCONSTRUCTOR_API FMCRemovalResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	bool bSucceeded = false;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	FString Error;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	int32 DeletedAssets = 0;

	/** False when files that are not packages kept the folder on disk. */
	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	bool bFolderRemoved = false;

	UPROPERTY(BlueprintReadOnly, Category = "Model Constructor")
	float Seconds = 0.f;
};
