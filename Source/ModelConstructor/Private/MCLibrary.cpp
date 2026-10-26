// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "MCLibrary.h"
#include "MCModels.h"
#include "MCPasses.h"
#include "MCRemove.h"
#include "MCRun.h"
#include "MCSave.h"
#include "MCSettings.h"
#include "MCTemplates.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "DatasmithBlueprintLibrary.h"
#include "DatasmithImportOptions.h"
#include "DatasmithScene.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "LevelEditorSubsystem.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

namespace
{
	using namespace MCRun;
	using MCModels::CurrentMapPackage;
	using MCModels::IsSavedMap;
	using MCModels::NormaliseContentPath;
	using MCModels::Registry;
	using MCModels::RootOf;
	namespace Tags = MCModels::Tags;

	constexpr int32 MaxFolderNameLength = 48;

	const UMCSettings& ResolveSettings(const UMCSettings* Settings)
	{
		return Settings ? *Settings : *GetDefault<UMCSettings>();
	}

	/** Turns a model name into a folder name: symbols become underscores, repeated underscores merge, and the length is capped to keep Windows paths short. */
	FString FolderNameFor(const FString& Name)
	{
		FString Leaf = ObjectTools::SanitizeObjectName(Name.TrimStartAndEnd()).Replace(TEXT(" "), TEXT("_"));
		while (Leaf.ReplaceInline(TEXT("__"), TEXT("_")) > 0)
		{
		}
		Leaf = Leaf.Left(MaxFolderNameLength);
		while (Leaf.StartsWith(TEXT("_")))
		{
			Leaf.RightChopInline(1);
		}
		while (Leaf.EndsWith(TEXT("_")))
		{
			Leaf.LeftChopInline(1);
		}
		return Leaf;
	}

	FString ModelDisplayName(const UMCSettings& S)
	{
		const FString Custom = S.ModelName.TrimStartAndEnd();
		return S.bUseCustomName && !Custom.IsEmpty() ? Custom : FPaths::GetBaseFilename(S.SourceFile.FilePath.TrimStartAndEnd());
	}

	bool FolderExists(const FString& Folder)
	{
		IAssetRegistry& AssetRegistry = Registry();
		if (AssetRegistry.PathExists(FName(*Folder)) || AssetRegistry.HasAssets(FName(*Folder), /*bRecursive*/ true))
		{
			return true;
		}
		FString Directory;
		return FPackageName::TryConvertLongPackageNameToFilename(Folder + TEXT("/"), Directory) && IFileManager::Get().DirectoryExists(*Directory);
	}

	FString FindScenePath(const FString& Folder)
	{
		IAssetRegistry& AssetRegistry = Registry();
		AssetRegistry.ScanPathsSynchronous({ Folder });

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Folder));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UDatasmithScene::StaticClass()->GetClassPathName());
		TArray<FAssetData> Found;
		AssetRegistry.GetAssets(Filter, Found);
		if (Found.Num() == 0)
		{
			return FString();
		}
		Found.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
		return Found[0].GetSoftObjectPath().ToString();
	}

	/** A re-import keeps the model's folder, name and id; a new import makes them. */
	struct FJob
	{
		const UMCSettings& S;
		bool bReimport = false;
		FString ScenePath;
		FString Fingerprint;
		FString DisplayName;
		FString ImportId;
		FMCRemovalPlan Plan;
	};

	bool RunPassStages(FRunner& Runner, const MCPasses::FContext& Context, FMCImportResult& Result)
	{
		auto Pass = [&](int32 Stage, void (*Fn)(const MCPasses::FContext&, FMCStageResult&))
		{
			return Runner.Run(Stage, [&](FMCStageResult& Entry)
			{
				Fn(Context, Entry);
				return Entry.State != EMCStageState::Failed;
			});
		};

		// The model's actors are paused for the passes and come back once, before the report reads them.
		bool bOk = false;
		{
			FActorPause Pause;
			if (Context.Settings.bPauseLevelDuringPasses)
			{
				Result.Counters.Add(TEXT("PausedActors"), Pause.Begin(Context.Folder));
			}
			bOk = Pass(StageMeshes, &MCPasses::RunMeshes)
				&& Pass(StageTexture, &MCPasses::RunTexture)
				&& Pass(StageMaterial, &MCPasses::RunMaterial)
				&& Runner.Run(StageFinishPassBuilds, [](FMCStageResult& Entry) { MCPasses::FinishBuilds(Entry); return true; });
		}
		return bOk && Runner.Run(StageReport, [&](FMCStageResult& Entry) { MCPasses::RunReport(Context, Result, Entry); return true; });
	}

	bool CheckSource(const FString& Source, FMCStageResult& Stage)
	{
		if (Source.IsEmpty())
		{
			return Fail(Stage, TEXT("Choose a .udatasmith file."));
		}
		if (!FPaths::GetExtension(Source).Equals(TEXT("udatasmith"), ESearchCase::IgnoreCase))
		{
			return Fail(Stage, FString::Printf(TEXT("%s is not a .udatasmith file."), *Source));
		}
		if (!FPaths::FileExists(Source))
		{
			return Fail(Stage, FString::Printf(TEXT("File not found: %s"), *Source));
		}
		return true;
	}
}

FString UMCLibrary::ResolveModelFolder(const UMCSettings* Settings, FString& OutError)
{
	OutError.Reset();
	const UMCSettings& S = ResolveSettings(Settings);

	const FString Source = S.SourceFile.FilePath.TrimStartAndEnd();
	if (Source.IsEmpty())
	{
		OutError = TEXT("Choose a .udatasmith file.");
		return FString();
	}

	const FString Destination = NormaliseContentPath(S.DestinationFolder.Path);
	if (!Destination.StartsWith(TEXT("/")) || Destination.Len() < 2)
	{
		OutError = TEXT("The destination must be a content path such as /Game/Models.");
		return FString();
	}

	const FString Root = RootOf(Destination);
	if (Root == TEXT("Engine") || Root == TEXT("Script") || Root == TEXT("Temp") || Root == TEXT("Memory"))
	{
		OutError = FString::Printf(TEXT("Models cannot be imported under /%s."), *Root);
		return FString();
	}

	FString Unused;
	if (!FPackageName::TryConvertLongPackageNameToFilename(Destination + TEXT("/"), Unused))
	{
		OutError = FString::Printf(TEXT("/%s is not a mounted content root."), *Root);
		return FString();
	}

	if (S.bUseCustomName && S.ModelName.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("Enter a model name, or untick Model name to use the file name.");
		return FString();
	}

	const FString Leaf = FolderNameFor(ModelDisplayName(S));
	if (Leaf.IsEmpty())
	{
		OutError = S.bUseCustomName ? TEXT("The model name has no characters a folder name can use.") : TEXT("The file name cannot be used as a folder name.");
		return FString();
	}

	const FString Base = Destination / Leaf;
	if (!FolderExists(Base))
	{
		return Base;
	}
	if (S.DuplicateFolderAction == EMCDuplicateFolderAction::Refuse)
	{
		OutError = FString::Printf(TEXT("%s already exists."), *Base);
		return FString();
	}
	for (int32 Suffix = 2; Suffix < 1000; ++Suffix)
	{
		const FString Candidate = FString::Printf(TEXT("%s_%d"), *Base, Suffix);
		if (!FolderExists(Candidate))
		{
			return Candidate;
		}
	}
	OutError = FString::Printf(TEXT("No free folder name for %s."), *Base);
	return FString();
}

namespace
{
	void RunImportStages(FRunner& Runner, FJob& Job, FMCImportResult& Result, FString& PreviousMap)
	{
		const UMCSettings& S = Job.S;
		const FString Source = FPaths::ConvertRelativePathToFull(S.SourceFile.FilePath.TrimStartAndEnd());

		const bool bPrepared = Runner.Run(StagePrepare, [&](FMCStageResult& Stage)
		{
			if (!GEditor)
			{
				return Fail(Stage, TEXT("The editor is not available."));
			}
			if (GEditor->IsPlaySessionInProgress())
			{
				return Fail(Stage, TEXT("Stop Play In Editor before importing."));
			}
			if (!CheckSource(S.SourceFile.FilePath.TrimStartAndEnd(), Stage))
			{
				return false;
			}

			FString Error;
			if (!MCSave::Preflight(Source, Result.Warnings, Error))
			{
				return Fail(Stage, Error);
			}

			if (Job.bReimport)
			{
				Job.Plan = MCRemove::PlanRemoval(Job.ScenePath, /*bForReimport*/ true);
				if (!Job.Plan.bCanRemove)
				{
					return Fail(Stage, Job.Plan.Blockers.Num() > 0 ? Job.Plan.Blockers[0] : FString(TEXT("The model cannot be re-imported.")));
				}
				if (Job.Plan.Fingerprint != Job.Fingerprint)
				{
					return Fail(Stage, TEXT("The model folder changed after it was reviewed. Review the re-import again."));
				}
				Result.Folder = Job.Plan.Folder;
			}
			else
			{
				Result.Folder = UMCLibrary::ResolveModelFolder(&S, Error);
				if (Result.Folder.IsEmpty())
				{
					return Fail(Stage, Error);
				}
			}

			// A new level replaces the open one, so unsaved map changes are offered for saving first,
			// before the progress dialog opens.
			if (S.bCreateLevelBesideModel
				&& !FEditorFileUtils::SaveDirtyPackages(/*bPromptUserToSave*/ true, /*bSaveMapPackages*/ true, /*bSaveContentPackages*/ false))
			{
				return Fail(Stage, TEXT("Cancelled at the save prompt."));
			}

			PreviousMap = CurrentMapPackage();
			Stage.Detail = Result.Folder;
			return true;
		});
		if (!bPrepared)
		{
			return;
		}
		Runner.ShowDialog();

		if (Job.bReimport)
		{
			const bool bRemoved = Runner.Run(StageRemoveOld, [&](FMCStageResult& Stage)
			{
				const FMCRemovalResult Removal = MCRemove::Remove(Job.ScenePath, Job.Fingerprint, /*bForReimport*/ true);
				Stage.Changed = Removal.DeletedAssets;
				Stage.Total = Job.Plan.Packages.Num();
				if (!Removal.bSucceeded)
				{
					return Fail(Stage, Removal.Error);
				}
				Stage.Detail = FString::Printf(TEXT("Deleted %d packages in %.1f s"), Removal.DeletedAssets, Removal.Seconds);
				return true;
			});
			// The import only starts once the old version is gone from disk, memory and the asset registry.
			const bool bReady = bRemoved && Runner.Run(StageEditorReady, [&](FMCStageResult& Stage)
			{
				const FString Reason = MCRemove::CheckEditorReady(Result.Folder, Job.Plan.Packages);
				if (!Reason.IsEmpty())
				{
					return Fail(Stage, FString::Printf(TEXT("The editor is not ready for the import: %s"), *Reason));
				}
				if (FPackageName::DoesPackageExist(Job.Plan.LevelPath))
				{
					return Fail(Stage, FString::Printf(TEXT("The old level %s is still on disk."), *Job.Plan.LevelPath));
				}
				Stage.Detail = TEXT("Old version removed; nothing of it is loaded or listed");
				return true;
			});
			if (!bReady)
			{
				return;
			}
		}

		const bool bLevelOk = Runner.Run(StageLevel, [&](FMCStageResult& Stage)
		{
			if (!S.bCreateLevelBesideModel)
			{
				Result.LevelPath = IsSavedMap(CurrentMapPackage()) ? CurrentMapPackage() : FString();
				Stage.Detail = Result.LevelPath.IsEmpty() ? FString(TEXT("Using the open untitled level")) : Result.LevelPath;
				return true;
			}

			ULevelEditorSubsystem* LevelSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
			const FString LevelPath = Job.bReimport ? Job.Plan.LevelPath : MCModels::LevelPathFor(Result.Folder);
			if (!LevelSubsystem || !LevelSubsystem->NewLevel(LevelPath, /*bIsPartitionedWorld*/ false))
			{
				return Fail(Stage, FString::Printf(TEXT("Could not create level %s"), *LevelPath));
			}
			// The level package must exist on disk before the import places the scene actor in it.
			if (!LevelSubsystem->SaveCurrentLevel())
			{
				return Fail(Stage, FString::Printf(TEXT("Could not save level %s"), *LevelPath));
			}
			Result.LevelPath = LevelPath;
			Stage.Detail = LevelPath;
			return true;
		});
		if (!bLevelOk)
		{
			return;
		}

		const bool bImported = Runner.Run(StageImport, [&](FMCStageResult& Stage)
		{
			TStrongObjectPtr<UDatasmithSceneElement> Scene(UDatasmithSceneElement::ConstructDatasmithSceneFromFile(Source));
			if (!Scene.IsValid())
			{
				return Fail(Stage, FString::Printf(TEXT("Datasmith could not read %s"), *Source));
			}

			if (UDatasmithImportOptions* Options = Cast<UDatasmithImportOptions>(Scene->GetOptions(UDatasmithImportOptions::StaticClass())))
			{
				FDatasmithImportBaseOptions& Base = Options->BaseOptions;
				Base.SceneHandling = EDatasmithImportScene::CurrentLevel;
				Base.bIncludeGeometry = true;
				Base.bIncludeMaterial = true;
				Base.bIncludeLight = S.bImportLights;
				Base.bIncludeCamera = S.bImportCameras;
				Base.bIncludeAnimation = S.bImportAnimations;
				Base.StaticMeshOptions.bGenerateLightmapUVs = S.bGenerateLightmapUVs;
				Base.StaticMeshOptions.bRemoveDegenerates = S.bRemoveDegenerates;
			}
			else
			{
				MCPasses::AddWarning(Result.Warnings, TEXT("Datasmith import options were not available; engine defaults were used"));
			}

			const FDatasmithImportFactoryCreateFileResult ImportResult = Scene->ImportScene(Result.Folder);
			Scene->DestroyScene();
			if (!ImportResult.bImportSucceed)
			{
				return Fail(Stage, TEXT("The Datasmith import failed. See the Output Log for the reason."));
			}

			Result.ScenePath = FindScenePath(Result.Folder);
			if (Result.ScenePath.IsEmpty())
			{
				return Fail(Stage, FString::Printf(TEXT("No Datasmith scene asset found under %s after import"), *Result.Folder));
			}

			// Package metadata only persists on save, so it is written only when the run saves.
			UObject* SceneAsset = FSoftObjectPath(Result.ScenePath).TryLoad();
			if (S.bSaveAfterImport && SceneAsset)
			{
				const FString Id = Job.ImportId.IsEmpty() ? FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6).ToLower() : Job.ImportId;
				MCModels::SetTag(SceneAsset, Tags::ImportedAt, FDateTime::UtcNow().ToIso8601());
				MCModels::SetTag(SceneAsset, Tags::ImportId, Id);
				MCModels::SetTag(SceneAsset, Tags::Level, Result.LevelPath);
				MCModels::SetTag(SceneAsset, Tags::CreatedLevel, S.bCreateLevelBesideModel ? TEXT("true") : TEXT("false"));
				MCModels::SetTag(SceneAsset, Tags::Folder, Result.Folder);
				MCModels::SetTag(SceneAsset, Tags::DisplayName, Job.DisplayName.IsEmpty() ? ModelDisplayName(S) : Job.DisplayName);
				MCModels::SetTag(SceneAsset, Tags::SourceFile, Source);
			}

			Stage.Detail = Result.ScenePath;
			return true;
		});
		if (!bImported)
		{
			return;
		}

		const bool bSettled = Runner.Run(StageFinishImportBuilds, [&](FMCStageResult& Stage)
		{
			MCPasses::FinishBuilds(Stage);
			// The import's undo records and temporary objects are not needed any more. Undo history is only
			// cleared when the run made the level, so nothing of the user's own work is lost.
			if (S.bCreateLevelBesideModel)
			{
				GEditor->ResetTransaction(LOCTEXT("ImportUndo", "Model Constructor import"));
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			return true;
		});
		const MCPasses::FContext Context{ Result.Folder, S, Result.Warnings, Result.Counters };
		if (!bSettled || !RunPassStages(Runner, Context, Result))
		{
			return;
		}

		Runner.Run(StageSave, [&](FMCStageResult& Stage)
		{
			if (!S.bSaveAfterImport)
			{
				Stage.State = EMCStageState::Skipped;
				Stage.Detail = TEXT("Saving is off; the model is not saved");
				return true;
			}
			MCSave::FRequest Request;
			Request.Folder = Result.Folder;
			Request.ScenePath = Result.ScenePath;
			Request.bSaveLevel = true;
			Request.RecordSettings = &S;
			Request.bRecordManifest = true;
			return MCSave::SaveModel(Request, Result, Stage);
		});
	}

	void RunFolderStages(FRunner& Runner, const UMCSettings& S, FMCImportResult& Result)
	{
		const bool bPrepared = Runner.Run(StagePrepare, [&](FMCStageResult& Stage)
		{
			if (!GEditor)
			{
				return Fail(Stage, TEXT("The editor is not available."));
			}
			if (GEditor->IsPlaySessionInProgress())
			{
				return Fail(Stage, TEXT("Stop Play In Editor before running passes."));
			}
			if (!FolderExists(Result.Folder))
			{
				return Fail(Stage, FString::Printf(TEXT("Folder not found: %s"), *Result.Folder));
			}
			Result.ScenePath = FindScenePath(Result.Folder);
			Result.LevelPath = IsSavedMap(CurrentMapPackage()) ? CurrentMapPackage() : FString();
			Stage.Detail = Result.Folder;
			return true;
		});
		if (!bPrepared)
		{
			return;
		}
		Runner.ShowDialog();

		const MCPasses::FContext Context{ Result.Folder, S, Result.Warnings, Result.Counters };
		if (!RunPassStages(Runner, Context, Result))
		{
			return;
		}

		Runner.Run(StageSave, [&](FMCStageResult& Stage)
		{
			if (!S.bSaveAfterImport)
			{
				Stage.State = EMCStageState::Skipped;
				Stage.Detail = TEXT("Saving is off; changes are not saved");
				return true;
			}
			MCSave::FRequest Request;
			Request.Folder = Result.Folder;
			Request.ScenePath = Result.ScenePath;
			return MCSave::SaveModel(Request, Result, Stage);
		});
	}

	/** After a saved run: leave the model level open, go back to the previous level, or open a blank one. */
	void FinishLevel(const UMCSettings& S, const FMCImportResult& Result, const FString& PreviousMap)
	{
		if (!Result.bSucceeded || !S.bSaveAfterImport || !S.bCreateLevelBesideModel || S.AfterRun == EMCAfterRun::OpenModelLevel)
		{
			return;
		}
		if (PreviousMap == Result.LevelPath)
		{
			// The run started in the model's own level, so that level is the previous one.
			return;
		}
		ULevelEditorSubsystem* LevelSubsystem = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
		if (IsSavedMap(PreviousMap) && FPackageName::DoesPackageExist(PreviousMap) && LevelSubsystem)
		{
			LogStep(FString::Printf(TEXT("Returning to %s"), *PreviousMap));
			LevelSubsystem->LoadLevel(PreviousMap);
		}
		else
		{
			LogStep(TEXT("Opening a blank level"));
			UEditorLoadingAndSavingUtils::NewBlankMap(/*bSaveExistingMap*/ false);
		}
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	TArray<int32> AllStages(bool bReimport)
	{
		TArray<int32> Order;
		for (int32 Stage = 0; Stage < StageCount; ++Stage)
		{
			if (bReimport || (Stage != StageRemoveOld && Stage != StageEditorReady))
			{
				Order.Add(Stage);
			}
		}
		return Order;
	}
}

FMCImportResult UMCLibrary::ImportModel(const UMCSettings* Settings)
{
	const UMCSettings& S = ResolveSettings(Settings);
	FMCImportResult Result;
	FString PreviousMap;
	{
		MCRun::FViewportPause Viewports(S.bPauseViewportsDuringRun);
		FJob Job{ S };
		MCRun::FRunner Runner(Result, AllStages(false), TEXT("Import"));
		RunImportStages(Runner, Job, Result, PreviousMap);
		Runner.Finish();
	}
	FinishLevel(S, Result, PreviousMap);
	return Result;
}

FMCImportResult UMCLibrary::ReimportModel(const FString& ScenePath, const FString& Fingerprint, const UMCSettings* Settings)
{
	FMCImportResult Result;
	Result.bReimport = true;

	// Read what must survive the removal before anything is deleted.
	FString DisplayName;
	FString ImportId;
	FString RecordedSource;
	{
		UObject* Scene = FSoftObjectPath(ScenePath).TryLoad();
		DisplayName = MCModels::GetTag(Scene, Tags::DisplayName);
		ImportId = MCModels::GetTag(Scene, Tags::ImportId);
		RecordedSource = MCModels::GetTag(Scene, Tags::SourceFile);
	}

	TStrongObjectPtr<UMCSettings> JobSettings(NewObject<UMCSettings>(GetTransientPackage(), NAME_None, RF_Transient));
	JobSettings->CopySettingsFrom(*GetDefault<UMCSettings>());
	if (Settings)
	{
		JobSettings->CopySettingsFrom(*Settings);
	}
	else
	{
		MCTemplates::ApplyRecorded(ScenePath, *JobSettings);
		JobSettings->SourceFile.FilePath = RecordedSource;
	}
	if (JobSettings->SourceFile.FilePath.TrimStartAndEnd().IsEmpty())
	{
		JobSettings->SourceFile.FilePath = RecordedSource;
	}
	// A re-import always rebuilds the model in its own level, at the path it had.
	JobSettings->bCreateLevelBesideModel = true;
	JobSettings->bSaveAfterImport = true;

	FString PreviousMap;
	{
		MCRun::FViewportPause Viewports(JobSettings->bPauseViewportsDuringRun);
		FJob Job{ *JobSettings, /*bReimport*/ true, ScenePath, Fingerprint, DisplayName, ImportId };
		MCRun::FRunner Runner(Result, AllStages(true), TEXT("Re-import"));
		RunImportStages(Runner, Job, Result, PreviousMap);
		Runner.Finish();
	}
	FinishLevel(*JobSettings, Result, PreviousMap);
	return Result;
}

TArray<FString> UMCLibrary::GetTemplateNames()
{
	TArray<FString> Names;
	for (const MCTemplates::FTemplate& Template : MCTemplates::List())
	{
		Names.Add(Template.Name);
	}
	return Names;
}

bool UMCLibrary::ApplyTemplate(const FString& Name, UMCSettings* Target)
{
	const TOptional<MCTemplates::FTemplate> Template = MCTemplates::Find(Name);
	if (!Template.IsSet())
	{
		return false;
	}
	UMCSettings* Settings = Target ? Target : GetMutableDefault<UMCSettings>();
	MCTemplates::Apply(*Template, *Settings);
	if (!Target)
	{
		Settings->SaveConfig();
	}
	return true;
}

bool UMCLibrary::SaveTemplate(const FString& Name, const FString& Description, bool bIncludeDestination, const UMCSettings* From, FString& OutError)
{
	OutError = MCTemplates::CheckName(Name, Name);
	return OutError.IsEmpty() && MCTemplates::Save(Name, Description, bIncludeDestination, ResolveSettings(From), OutError);
}

FMCRemovalPlan UMCLibrary::PlanReimportModel(const FString& ScenePath)
{
	return MCRemove::PlanRemoval(ScenePath, /*bForReimport*/ true);
}

bool UMCLibrary::GetRecordedSettings(const FString& ScenePath, UMCSettings* Target)
{
	return Target && MCModels::ReadRecordedSettings(ScenePath, *Target);
}

FMCImportResult UMCLibrary::RunPasses(const FString& Folder, const UMCSettings* Settings)
{
	const UMCSettings& S = ResolveSettings(Settings);
	FMCImportResult Result;
	Result.Folder = NormaliseContentPath(Folder);
	{
		MCRun::FViewportPause Viewports(S.bPauseViewportsDuringRun);
		MCRun::FRunner Runner(Result, { MCRun::StagePrepare, MCRun::StageMeshes, MCRun::StageTexture, MCRun::StageMaterial, MCRun::StageFinishPassBuilds, MCRun::StageReport, MCRun::StageSave }, TEXT("Passes"));
		RunFolderStages(Runner, S, Result);
		Runner.Finish();
	}
	return Result;
}

TArray<FMCSceneEntry> UMCLibrary::FindScenes(bool bIncludePlugins)
{
	return MCModels::FindScenes(bIncludePlugins);
}

bool UMCLibrary::OpenLevel(const FString& LevelPath)
{
	if (!GEditor || GEditor->IsPlaySessionInProgress() || !FPackageName::DoesPackageExist(LevelPath))
	{
		return false;
	}
	if (!FEditorFileUtils::SaveDirtyPackages(/*bPromptUserToSave*/ true, /*bSaveMapPackages*/ true, /*bSaveContentPackages*/ false))
	{
		return false;
	}
	return UEditorLoadingAndSavingUtils::LoadMap(LevelPath) != nullptr;
}

bool UMCLibrary::SetModelDisplayName(const FString& ScenePath, const FString& Name, FString& OutError)
{
	OutError.Reset();
	return MCModels::SetDisplayName(ScenePath, Name, OutError);
}

FMCRemovalPlan UMCLibrary::PlanRemoveModel(const FString& ScenePath)
{
	return MCRemove::PlanRemoval(ScenePath, /*bForReimport*/ false);
}

FMCRemovalResult UMCLibrary::RemoveModel(const FString& ScenePath, const FString& Fingerprint)
{
	return MCRemove::Remove(ScenePath, Fingerprint, /*bForReimport*/ false);
}

#undef LOCTEXT_NAMESPACE
