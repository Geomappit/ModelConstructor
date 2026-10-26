// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "MCTypes.h"
#include "MCSettings.generated.h"

/**
 * Import options for Model Constructor, saved per user, per project. Edited on the tool's Settings page,
 * not in Editor Preferences, so template settings always come from a template.
 */
UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "Model Constructor"))
class MODELCONSTRUCTOR_API UMCSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMCSettings();

	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual bool SupportsAutoRegistration() const override { return false; }
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Raised when a default changes between versions. UpgradeConfig applies the new defaults once. */
	static constexpr int32 CurrentConfigVersion = 5;

	/** The template chosen on the Import page, or empty. */
	UPROPERTY(Config, BlueprintReadWrite, Category = "Model")
	FString TemplateName;

	/** Applies defaults added since the saved settings were written, and saves them. Returns true when anything changed. */
	bool UpgradeConfig();

	/** Copies every saved setting from another settings object. */
	void CopySettingsFrom(const UMCSettings& Other);

	/** Version of the saved settings. Not shown in the editor. */
	UPROPERTY(Config)
	int32 ConfigVersion = 0;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Model", meta = (FilePathFilter = "udatasmith",
		ToolTip = "The .udatasmith file to import."))
	FFilePath SourceFile;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Model", meta = (LongPackageName,
		ToolTip = "Content folder that receives the model. Each import creates its own subfolder, named after the file or after Model name when set."))
	FDirectoryPath DestinationFolder;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Model", meta = (InlineEditConditionToggle))
	bool bUseCustomName = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Model", meta = (EditCondition = "bUseCustomName",
		ToolTip = "Name for the model instead of the file name. Used for the folder, with spaces and symbols replaced, and shown as written in Model Manager."))
	FString ModelName;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Import", meta = (
		ShortHelp = "Bring in the lights from the export.",
		ToolTip = "Import the lights in the Datasmith file. Off by default, because exported lights rarely suit a Lumen scene."))
	bool bImportLights = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Import", meta = (
		ShortHelp = "Bring in the cameras from the export.",
		ToolTip = "Import the cameras in the Datasmith file."))
	bool bImportCameras = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Import", meta = (
		ShortHelp = "Bring in animation from the export.",
		ToolTip = "Import the animation in the Datasmith file as level sequences."))
	bool bImportAnimations = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Import", AdvancedDisplay, meta = (
		ShortHelp = "Drop zero-area triangles when meshes are built.",
		ToolTip = "Drop zero-area triangles when building meshes."))
	bool bRemoveDegenerates = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Import", AdvancedDisplay, meta = (DisplayName = "Generate Lightmap UVs",
		ShortHelp = "Only for baked lighting. Slows the import.",
		ToolTip = "Only needed for baked lighting. Nanite and dynamic lighting do not use them, and they slow the import."))
	bool bGenerateLightmapUVs = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Import", AdvancedDisplay, meta = (DisplayName = "Use MikkTSpace",
		ShortHelp = "Make missing tangents with MikkTSpace.",
		ToolTip = "Tangents the build has to make use MikkTSpace instead of the engine's own method. Nanite works out its own tangents, so this mostly affects the fallback mesh."))
	bool bUseMikkTSpace = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Import", AdvancedDisplay, meta = (
		ShortHelp = "Rebuild normals instead of using the exported ones.",
		ToolTip = "Rebuild the normals instead of using the ones in the export. Off keeps the hard and soft edges the design tool exported. Missing normals are made either way."))
	bool bRecomputeNormals = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Import", AdvancedDisplay, meta = (
		ShortHelp = "Rebuild tangents instead of using the exported ones.",
		ToolTip = "Rebuild the tangents instead of using the ones in the export. Missing tangents are made either way."))
	bool bRecomputeTangents = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nanite", meta = (
		ShortHelp = "Turn Nanite on for every mesh.",
		ToolTip = "Turn Nanite on for every mesh in the model, so the engine can draw very detailed geometry quickly."))
	bool bEnableNanite = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nanite", AdvancedDisplay, meta = (ClampMin = "-8", ClampMax = "16",
		ShortHelp = "How finely vertex positions are stored. 5 is about 0.3 mm.",
		ToolTip = "Vertex position step of 2^-N cm. 5 is about 0.3 mm, which suits most building models."))
	int32 PositionPrecision = 5;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nanite", AdvancedDisplay, meta = (
		ShortHelp = "Keep thin parts from vanishing at a distance.",
		ToolTip = "Nanite keeps the area of thin parts as it simplifies them, so they do not disappear at a distance."))
	bool bPreserveArea = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nanite", AdvancedDisplay, meta = (ClampMin = "0", ClampMax = "1",
		ShortHelp = "Share of triangles Nanite keeps. 1 keeps them all.",
		ToolTip = "Share of the source triangles Nanite keeps. 1 keeps them all."))
	float KeepPercentTriangles = 1.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nanite", AdvancedDisplay, meta = (ClampMin = "0", ClampMax = "1",
		ShortHelp = "Detail of the simple mesh. Also sets Complex collision detail.",
		ToolTip = "Share of triangles kept in the fallback mesh. The fallback is used for ray tracing, for platforms without Nanite, and with Complex collision for collision too."))
	float FallbackPercentTriangles = 0.2f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (
		ShortHelp = "Complex hits the real surface. Simple Box is one box per mesh.",
		ToolTip = "Complex: queries use the render mesh. With Nanite on that is the fallback mesh, so Fallback Percent Triangles also sets collision detail. Simple Box: one box per mesh, the lightest option."))
	EMCCollisionMode CollisionMode = EMCCollisionMode::SimpleBox;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Collision", AdvancedDisplay, meta = (ClampMin = "0", UIMin = "0",
		ShortHelp = "Bigger meshes get no collision and are listed. 0 is no limit.",
		ToolTip = "Meshes whose collision would have more triangles than this get no collision and are listed in the warnings. Very large collision meshes are slow to build and save. 0 turns the limit off."))
	int32 ComplexCollisionTriangleLimit = 1500000;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Collision", AdvancedDisplay, meta = (ClampMin = "0", UIMin = "0",
		ShortHelp = "Bigger meshes are left out of navigation. 0 is no limit.",
		ToolTip = "Meshes with more collision triangles than this are left out of navigation, so navigation does not export their full geometry. 0 keeps every mesh in navigation."))
	int32 NavigationTriangleLimit = 150000;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Collision", AdvancedDisplay, meta = (DisplayName = "Allow CPU Access",
		ShortHelp = "For runtime code that reads vertices. Adds memory to every mesh.",
		ToolTip = "Keep a CPU copy of each mesh for runtime tools that read vertices. Traces do not need it. Costs memory on every mesh."))
	bool bAllowCpuAccess = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Textures", meta = (ClampMin = "0", UIMin = "0",
		ShortHelp = "Largest texture size in pixels. 0 is no cap.",
		ToolTip = "Largest texture size in pixels. Only textures whose source is larger are capped. 0 turns the cap off."))
	int32 MaxTextureSize = 2048;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Textures", AdvancedDisplay, meta = (
		ShortHelp = "Stream textures as virtual textures.",
		ToolTip = "Stream textures as virtual textures. Samplers in the model's own materials are switched to match. Textures that a material outside the model folder also uses are left as they are and listed. Needs virtual textures turned on in the project."))
	bool bVirtualTextureStreaming = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Materials", meta = (DisplayName = "Convert Translucent To Masked With Dither",
		ShortHelp = "Nanite can't draw translucent materials, so glass becomes dithered.",
		ToolTip = "Nanite does not draw translucent materials. Converts them to masked and drives the opacity mask through a temporal dither."))
	bool bConvertTranslucentToMaskedWithDither = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Materials", meta = (DisplayName = "Schedule Animation Support",
		ShortHelp = "Keep schedule materials showing and hiding elements.",
		ToolTip = "Finds materials that call the schedule animation function and keeps them showing and hiding elements as the schedule plays. Only those materials are affected, so models without schedule animation import as normal. Turn off to treat every material the same way."))
	bool bScheduleAnimationSupport = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Materials", AdvancedDisplay, meta = (EditCondition = "bScheduleAnimationSupport",
		ShortHelp = "Text in the name of the schedule material function.",
		ToolTip = "Materials that call a function with this text in its name take the dither opacity from that function's Opacity output."))
	FString ScheduleFunctionNameContains = TEXT("ScheduleAnimation");

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Materials", AdvancedDisplay, meta = (EditCondition = "bScheduleAnimationSupport",
		ShortHelp = "Class of the actor that plays the schedule. Checked in the report.",
		ToolTip = "Class name of the actor that drives the schedule. When schedule materials are found, the report warns if the import did not bring this actor into the level."))
	FString ScheduleActorClassName = TEXT("ScheduleManager");

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Materials", AdvancedDisplay, meta = (EditCondition = "bScheduleAnimationSupport",
		ShortHelp = "Actor tags counted in the report.",
		ToolTip = "Actor tags counted in the report."))
	TArray<FName> ScheduleActorTags;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Materials", AdvancedDisplay, meta = (DisplayName = "Set Used With Nanite",
		ShortHelp = "So packaged builds include the Nanite shaders.",
		ToolTip = "Set the Nanite usage flag on every parent material so packaged builds have the shaders."))
	bool bSetUsedWithNanite = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Folder and level", meta = (
		ShortHelp = "When the model folder already exists: add _2, _3, or stop.",
		ToolTip = "What to do when the model's folder already exists."))
	EMCDuplicateFolderAction DuplicateFolderAction = EMCDuplicateFolderAction::Suffix;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Folder and level", meta = (
		ShortHelp = "Give each model its own level. Needed for re-import.",
		ToolTip = "Create L_<Folder> in the model folder and import into it. Off: import into the open level. Only models in their own level can be re-imported."))
	bool bCreateLevelBesideModel = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Folder and level", meta = (DisplayName = "When The Run Finishes", EditCondition = "bCreateLevelBesideModel",
		ShortHelp = "Go back to the level you had open, or stay in the model level.",
		ToolTip = "Return to previous level: reopen the level that was open before, or a blank level if it was untitled, which frees the model's memory. Open the model level: leave it open in the editor."))
	EMCAfterRun AfterRun = EMCAfterRun::ReturnToPrevious;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Run", meta = (
		ShortHelp = "Save the model folder and its level at the end.",
		ToolTip = "Save the model folder and its level when the run finishes. Nothing else in the project is saved. Remove and re-import need a saved import."))
	bool bSaveAfterImport = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Run", AdvancedDisplay, meta = (
		ShortHelp = "Faster passes on large models. Leave on.",
		ToolTip = "Takes the model's actors out of the renderer, physics and navigation while the mesh, texture and material passes run, then puts them back once. Without this, every material change refreshes every actor, which is slow on large models."))
	bool bPauseLevelDuringPasses = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Run", AdvancedDisplay, meta = (
		ShortHelp = "Stop viewports redrawing while the run works.",
		ToolTip = "Stops the viewports redrawing in real time while the run works."))
	bool bPauseViewportsDuringRun = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Batches", meta = (ClampMin = "1", UIMin = "1", UIMax = "256",
		ShortHelp = "Meshes rebuilt at a time. Lower it if memory runs short.",
		ToolTip = "Meshes rebuilt together. The run waits for each batch before starting the next, so smaller batches use less memory."))
	int32 MeshBatchSize = 128;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Batches", AdvancedDisplay, meta = (ClampMin = "100000", UIMin = "100000",
		ShortHelp = "Triangle total that closes a mesh batch early.",
		ToolTip = "A mesh batch also closes when its meshes add up to this many triangles, so very large meshes build on their own."))
	int32 MeshBatchTriangleBudget = 20000000;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Batches", meta = (ClampMin = "1", UIMin = "1", UIMax = "256",
		ShortHelp = "Textures changed before the run waits for them.",
		ToolTip = "Textures changed together before the run waits for them to finish building."))
	int32 TextureBatchSize = 32;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Batches", meta = (ClampMin = "1", UIMin = "1", UIMax = "256",
		ShortHelp = "Materials changed in one update.",
		ToolTip = "Materials changed together in one update."))
	int32 MaterialBatchSize = 32;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Batches", AdvancedDisplay, meta = (ClampMin = "1", UIMin = "1", UIMax = "2000",
		ShortHelp = "Packages written per save step. Each step is logged.",
		ToolTip = "Packages written per save step. Each step is logged, so the Output Log shows where a save stopped."))
	int32 SaveBatchSize = 200;
};
