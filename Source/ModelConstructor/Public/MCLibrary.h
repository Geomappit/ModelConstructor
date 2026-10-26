// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCTypes.h"
#include "MCLibrary.generated.h"

class UMCSettings;

UCLASS()
class MODELCONSTRUCTOR_API UMCLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Imports the source file into a new folder under the destination and prepares it for Nanite. Uses the saved settings when Settings is null. */
	UFUNCTION(BlueprintCallable, Category = "Model Constructor")
	static FMCImportResult ImportModel(const UMCSettings* Settings);

	/** Runs the mesh, texture and material passes and the report over an existing content folder. */
	UFUNCTION(BlueprintCallable, Category = "Model Constructor")
	static FMCImportResult RunPasses(const FString& Folder, const UMCSettings* Settings);

	/** The folder the next import would create. Empty, with OutError set, when the settings cannot be used. */
	UFUNCTION(BlueprintCallable, Category = "Model Constructor")
	static FString ResolveModelFolder(const UMCSettings* Settings, FString& OutError);

	/**
	 * Replaces a model with a fresh import of its source: removes the old version, checks the editor is ready,
	 * then imports into the same folder under the same name. Fingerprint must come from PlanReimportModel.
	 * Uses Settings when given, else the settings recorded at the last import.
	 */
	UFUNCTION(BlueprintCallable, Category = "Model Constructor")
	static FMCImportResult ReimportModel(const FString& ScenePath, const FString& Fingerprint, const UMCSettings* Settings);

	/** Names of the import templates: built-in ones first, then project templates. */
	UFUNCTION(BlueprintCallable, Category = "Model Constructor")
	static TArray<FString> GetTemplateNames();

	/** Applies a template to Target, or to the Import page settings when Target is null. False when the template is not found. */
	UFUNCTION(BlueprintCallable, Category = "Model Constructor")
	static bool ApplyTemplate(const FString& Name, UMCSettings* Target);

	/** Saves the template settings of From (the Import page settings when null) as a project template. */
	UFUNCTION(BlueprintCallable, Category = "Model Constructor")
	static bool SaveTemplate(const FString& Name, const FString& Description, bool bIncludeDestination, const UMCSettings* From, FString& OutError);

	/** Lists what a re-import would delete first, or why it cannot run. Changes nothing. */
	UFUNCTION(BlueprintCallable, Category = "Model Constructor")
	static FMCRemovalPlan PlanReimportModel(const FString& ScenePath);

	/** Loads the settings recorded at a model's last import into Target. False when none were recorded. */
	UFUNCTION(BlueprintCallable, Category = "Model Constructor")
	static bool GetRecordedSettings(const FString& ScenePath, UMCSettings* Target);

	/** Every Datasmith scene asset in the project, and in non-engine content plugins when asked. Reads the asset registry; only scenes saved by older versions are loaded. */
	UFUNCTION(BlueprintCallable, Category = "Model Constructor")
	static TArray<FMCSceneEntry> FindScenes(bool bIncludePlugins);

	/** Opens a saved level, offering to save unsaved level changes first. */
	UFUNCTION(BlueprintCallable, Category = "Model Constructor")
	static bool OpenLevel(const FString& LevelPath);

	/** Sets the name shown for a model. Changes a label on the scene asset only; no asset or folder is renamed. An empty name clears it. */
	UFUNCTION(BlueprintCallable, Category = "Model Constructor")
	static bool SetModelDisplayName(const FString& ScenePath, const FString& Name, FString& OutError);

	/** Lists what removing a model would delete, or why it cannot. Changes nothing. */
	UFUNCTION(BlueprintCallable, Category = "Model Constructor")
	static FMCRemovalPlan PlanRemoveModel(const FString& ScenePath);

	/**
	 * Deletes a model imported by Model Constructor: its folder and only the assets the import created.
	 * Fingerprint must come from PlanRemoveModel; the call refuses if the folder changed since.
	 */
	UFUNCTION(BlueprintCallable, Category = "Model Constructor")
	static FMCRemovalResult RemoveModel(const FString& ScenePath, const FString& Fingerprint);
};
