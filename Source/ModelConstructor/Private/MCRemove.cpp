// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "MCRemove.h"
#include "MCModels.h"
#include "MCPasses.h"
#include "MCRun.h"
#include "MCSettings.h"
#include "AssetCompilingManager.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "DatasmithScene.h"
#include "Editor.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace MCRemove
{
namespace
{
	using MCModels::Registry;
	namespace Tags = MCModels::Tags;

	const TCHAR* SidecarExtensions[] = { TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl"), TEXT("upayload") };

	UWorld* EditorWorld()
	{
		UUnrealEditorSubsystem* EditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>() : nullptr;
		return EditorSubsystem ? EditorSubsystem->GetEditorWorld() : nullptr;
	}

	bool UsesSourceControl()
	{
		ISourceControlModule& SourceControl = ISourceControlModule::Get();
		return SourceControl.IsEnabled() && SourceControl.GetProvider().IsAvailable();
	}

	/** True when the open level is in the set, or streams a level that is. */
	bool OpenWorldTouches(const TSet<FName>& Packages)
	{
		const UWorld* World = EditorWorld();
		if (!World)
		{
			return false;
		}
		if (Packages.Contains(World->GetOutermost()->GetFName()))
		{
			return true;
		}
		for (const ULevelStreaming* Streaming : World->GetStreamingLevels())
		{
			if (Streaming && Packages.Contains(Streaming->GetWorldAssetPackageFName()))
			{
				return true;
			}
		}
		return false;
	}

	TArray<UPackage*> LoadedPackages(const TArray<FString>& Packages)
	{
		TArray<UPackage*> Loaded;
		for (const FString& Name : Packages)
		{
			if (UPackage* Package = FindPackage(nullptr, *Name); IsValid(Package))
			{
				Loaded.Add(Package);
			}
		}
		return Loaded;
	}

	void CloseEditors(const TArray<UPackage*>& Packages)
	{
		UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		for (UPackage* Package : Packages)
		{
			ForEachObjectWithPackage(Package, [Editors](UObject* Object)
			{
				if (Object->IsAsset())
				{
					Editors->CloseAllEditorsForAsset(Object);
				}
				return true;
			}, /*bIncludeNestedObjects*/ false);
		}
	}

	/** Unloads every package of the model. Fails, with nothing deleted, when one stays in memory. */
	bool UnloadAll(const TArray<FString>& Packages, FString& OutError)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		TArray<UPackage*> Loaded = LoadedPackages(Packages);
		if (Loaded.Num() > 0)
		{
			CloseEditors(Loaded);
			MCRun::LogStep(FString::Printf(TEXT("Unloading %d packages of the model"), Loaded.Num()));
			FText Error;
			UPackageTools::UnloadPackages(Loaded, Error, /*bUnloadDirtyPackages*/ true);
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}

		TArray<FString> Stuck;
		for (UPackage* Package : LoadedPackages(Packages))
		{
			Stuck.Add(Package->GetName());
		}
		if (Stuck.Num() > 0)
		{
			OutError = FString::Printf(TEXT("%d packages of the model are still in use in the editor, so nothing was deleted. Close what uses them and try again:%s%s"),
				Stuck.Num(), LINE_TERMINATOR, *MCModels::JoinCapped(Stuck));
			return false;
		}
		return true;
	}

	/** Deletes package files straight from disk. Nothing is loaded. */
	bool DeleteFiles(const TArray<FString>& Packages, int32& OutDeleted, FString& OutError)
	{
		TArray<FString> MainFiles;
		TArray<FString> AllFiles;
		for (const FString& Package : Packages)
		{
			FString File;
			if (!FPackageName::DoesPackageExist(Package, &File))
			{
				continue;
			}
			File = FPaths::ConvertRelativePathToFull(File);
			MainFiles.Add(File);
			AllFiles.Add(File);
			const FString Base = FPaths::ChangeExtension(File, TEXT(""));
			for (const TCHAR* Extension : SidecarExtensions)
			{
				const FString Sidecar = Base + TEXT(".") + Extension;
				if (IFileManager::Get().FileExists(*Sidecar))
				{
					AllFiles.Add(Sidecar);
				}
			}
		}

		// Checked first, so a locked file stops the removal before anything is deleted.
		TArray<FString> ReadOnly;
		for (const FString& File : AllFiles)
		{
			if (IFileManager::Get().IsReadOnly(*File))
			{
				ReadOnly.Add(File);
			}
		}
		if (ReadOnly.Num() > 0)
		{
			OutError = FString::Printf(TEXT("%d files are read-only, so nothing was deleted:%s%s"), ReadOnly.Num(), LINE_TERMINATOR, *MCModels::JoinCapped(ReadOnly));
			return false;
		}

		MCRun::LogStep(FString::Printf(TEXT("Deleting %d files"), AllFiles.Num()));
		TArray<FString> Failed;
		for (const FString& File : AllFiles)
		{
			if (!IFileManager::Get().Delete(*File, /*bRequireExists*/ false, /*bEvenReadOnly*/ false, /*bQuiet*/ true))
			{
				Failed.Add(File);
			}
		}

		// Tells the registry the files are gone, now rather than when the folder watcher notices.
		Registry().ScanModifiedAssetFiles(MainFiles);
		OutDeleted = MainFiles.Num() - Failed.FilterByPredicate([&MainFiles](const FString& File) { return MainFiles.Contains(File); }).Num();

		if (Failed.Num() > 0)
		{
			OutError = FString::Printf(TEXT("%d files could not be deleted. Another program may have them open:%s%s"), Failed.Num(), LINE_TERMINATOR, *MCModels::JoinCapped(Failed));
			return false;
		}
		return true;
	}

	/** The engine delete, for source control. It loads each asset first. */
	bool EngineDelete(const TArray<FString>& Packages, int32& OutDeleted, FString& OutError)
	{
		TArray<FAssetData> Assets;
		for (const FString& Package : Packages)
		{
			TArray<FAssetData> InPackage;
			Registry().GetAssetsByPackageName(FName(*Package), InPackage);
			Assets.Append(InPackage);
		}
		CloseEditors(LoadedPackages(Packages));

		// Without confirmation the engine deletes only when nothing outside the set still references the assets.
		OutDeleted = ObjectTools::DeleteAssets(Assets, /*bShowConfirmation*/ false);
		if (OutDeleted < Assets.Num())
		{
			OutError = FString::Printf(TEXT("%d of %d assets were deleted. Something still uses the rest. See the Output Log."), OutDeleted, Assets.Num());
			return false;
		}
		return true;
	}

	/** Only empty directories are deleted. Files that are not packages stay, and so does their folder. */
	void DeleteEmptyDirectories(const FString& DiskFolder)
	{
		TArray<FString> Directories;
		IFileManager::Get().IterateDirectoryRecursively(*DiskFolder, [&Directories](const TCHAR* Path, bool bIsDirectory)
		{
			if (bIsDirectory)
			{
				Directories.Add(Path);
			}
			return true;
		});
		Directories.Add(DiskFolder);
		Directories.Sort([](const FString& A, const FString& B) { return A.Len() > B.Len(); });

		for (const FString& Directory : Directories)
		{
			bool bEmpty = true;
			IFileManager::Get().IterateDirectory(*Directory, [&bEmpty](const TCHAR*, bool)
			{
				bEmpty = false;
				return false;
			});
			if (bEmpty && IFileManager::Get().DeleteDirectory(*Directory, /*bRequireExists*/ false, /*bTree*/ false))
			{
				FString PackagePath;
				if (FPackageName::TryConvertFilenameToLongPackageName(Directory / TEXT(""), PackagePath))
				{
					Registry().RemovePath(MCModels::NormaliseContentPath(PackagePath));
				}
			}
		}
	}

	TArray<FString> RegistryLeftovers(const FString& Folder, const TArray<FString>& Packages)
	{
		const TSet<FString> Set(Packages);
		TArray<FAssetData> Assets;
		Registry().GetAssetsByPath(FName(*Folder), Assets, /*bRecursive*/ true);
		TArray<FString> Left;
		for (const FAssetData& Asset : Assets)
		{
			const FString Name = Asset.PackageName.ToString();
			if ((Packages.Num() == 0 || Set.Contains(Name)) && !Left.Contains(Name))
			{
				Left.Add(Name);
			}
		}
		return Left;
	}
}

FMCRemovalPlan PlanRemoval(const FString& ScenePath, bool bForReimport)
{
	FMCRemovalPlan Plan;
	Plan.ScenePath = ScenePath;
	Plan.bForReimport = bForReimport;
	Plan.bDeletesWithoutLoading = !UsesSourceControl();

	const FSoftObjectPath SceneObjectPath(ScenePath);
	const FString ScenePackage = SceneObjectPath.GetLongPackageName();
	UDatasmithScene* Scene = Cast<UDatasmithScene>(SceneObjectPath.TryLoad());
	if (!Scene)
	{
		Plan.Blockers.Add(FString::Printf(TEXT("Scene not found: %s"), *ScenePath));
		return Plan;
	}

	Plan.DisplayName = MCModels::GetTag(Scene, Tags::DisplayName);
	if (MCModels::GetTag(Scene, Tags::ImportId).IsEmpty())
	{
		Plan.Blockers.Add(TEXT("Only models imported by Model Constructor can be removed or re-imported here. Use the Content Browser for other scenes."));
		return Plan;
	}

	Plan.Folder = MCModels::OwnedModelFolder(Scene, ScenePackage);
	if (Plan.Folder.IsEmpty())
	{
		Plan.Blockers.Add(TEXT("The folder recorded at import does not match where the scene asset is now, so the model folder cannot be identified safely."));
		return Plan;
	}
	if (Plan.DisplayName.IsEmpty())
	{
		Plan.DisplayName = FPaths::GetCleanFilename(Plan.Folder);
	}

	const FString Level = MCModels::GetTag(Scene, Tags::Level);
	const bool bCreatedLevel = MCModels::GetTag(Scene, Tags::CreatedLevel) == TEXT("true") || Level == MCModels::LevelPathFor(Plan.Folder);
	Plan.LevelPath = bCreatedLevel && Level.StartsWith(Plan.Folder + TEXT("/")) ? Level : FString();
	if (bForReimport && Plan.LevelPath.IsEmpty())
	{
		Plan.Blockers.Add(TEXT("This model was imported into an existing level, so it cannot be re-imported here. Remove it, clear its actors from that level, and import it again."));
		return Plan;
	}

	// Checks on the folder path itself, before looking inside it.
	const FString Root = MCModels::RootOf(Plan.Folder);
	const FString Destination = MCModels::NormaliseContentPath(GetDefault<UMCSettings>()->DestinationFolder.Path);
	TArray<FString> Segments;
	Plan.Folder.ParseIntoArray(Segments, TEXT("/"));
	if (Segments.Num() < 2 || Root == TEXT("Engine") || Root == TEXT("Script") || Root == TEXT("Temp") || Root == TEXT("Memory"))
	{
		Plan.Blockers.Add(FString::Printf(TEXT("%s is a content root or a protected location."), *Plan.Folder));
		return Plan;
	}
	if (Plan.Folder.Equals(Destination, ESearchCase::IgnoreCase))
	{
		Plan.Blockers.Add(FString::Printf(TEXT("%s is the import destination folder, not a model folder."), *Plan.Folder));
		return Plan;
	}

	const MCModels::FFolderScan Scan = MCModels::ScanFolder(Plan.Folder);
	Plan.DiskFolder = Scan.DiskFolder;
	Plan.OtherFileCount = Scan.OtherFiles;
	if (Scan.DiskFolder.IsEmpty())
	{
		Plan.Blockers.Add(FString::Printf(TEXT("%s is not in a mounted content root."), *Plan.Folder));
		return Plan;
	}

	bool bHasManifest = false;
	const TSet<FName> Recorded = MCModels::ReadManifest(Scene, Plan.Folder, bHasManifest);
	const TSet<FName> Owned = bHasManifest ? Recorded : MCModels::StructuralOwnership(Scene, Plan.Folder, ScenePackage);

	TArray<FString> Unknown;
	TArray<FName> ToDelete;
	for (const FName& Package : Scan.Packages)
	{
		if (Owned.Contains(Package))
		{
			ToDelete.Add(Package);
		}
		else
		{
			Unknown.Add(Package.ToString());
		}
	}
	Unknown.Sort();
	if (Unknown.Num() > 0)
	{
		Plan.Blockers.Add(FString::Printf(TEXT("%d assets in the folder were not created by this import. Move them out of the folder first:%s%s"),
			Unknown.Num(), LINE_TERMINATOR, *MCModels::JoinCapped(Unknown)));
	}
	if (!bHasManifest)
	{
		UE_LOG(LogModelConstructor, Log, TEXT("%s has no list of imported assets; using the assets its Datasmith scene lists"), *Plan.Folder);
	}

	// A re-import puts the level back at the same path, so levels that stream it keep working.
	const TSet<FName> LevelPackages = Plan.LevelPath.IsEmpty() ? TSet<FName>()
		: TSet<FName>{ FName(*Plan.LevelPath), FName(*(Plan.LevelPath + TEXT("_BuiltData"))) };

	IAssetRegistry& AssetRegistry = Registry();
	const TSet<FName> DeleteSet(ToDelete);
	TArray<FString> External;
	TArray<FString> LevelUsers;
	for (const FName& Package : ToDelete)
	{
		TArray<FName> Referencers;
		AssetRegistry.GetReferencers(Package, Referencers, UE::AssetRegistry::EDependencyCategory::Package);
		for (const FName& Referencer : Referencers)
		{
			if (DeleteSet.Contains(Referencer))
			{
				continue;
			}
			if (bForReimport && LevelPackages.Contains(Package))
			{
				LevelUsers.AddUnique(Referencer.ToString());
			}
			else
			{
				External.Add(FString::Printf(TEXT("%s is used by %s"), *Package.ToString(), *Referencer.ToString()));
			}
		}
	}
	External.Sort();
	if (External.Num() > 0)
	{
		Plan.Blockers.Add(FString::Printf(TEXT("%d references from outside the model would break. Remove those uses first:%s%s"),
			External.Num(), LINE_TERMINATOR, *MCModels::JoinCapped(External)));
	}
	LevelUsers.Sort();
	if (LevelUsers.Num() > 0)
	{
		Plan.Notes.Add(FString::Printf(TEXT("%d levels use the model level. They keep working, because the level comes back at the same path:%s%s"),
			LevelUsers.Num(), LINE_TERMINATOR, *MCModels::JoinCapped(LevelUsers)));
	}

	ToDelete.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	for (const FName& Package : ToDelete)
	{
		Plan.Packages.Add(Package.ToString());
		const FString* Class = Scan.ClassByPackage.Find(Package);
		Plan.AssetsByClass.FindOrAdd(Class ? *Class : TEXT("Unknown"))++;
		if (const int64* Size = Scan.SizeByPackage.Find(Package))
		{
			++Plan.FileCount;
			Plan.Bytes += *Size;
		}
		if (const UPackage* Loaded = FindPackage(nullptr, *Package.ToString()))
		{
			Plan.UnsavedCount += Loaded->IsDirty() ? 1 : 0;
		}
	}

	if (OpenWorldTouches(DeleteSet))
	{
		Plan.OpenLevelToClose = MCModels::CurrentMapPackage();
	}
	if (!Plan.bDeletesWithoutLoading)
	{
		Plan.Notes.Add(TEXT("Source control is on, so the engine delete is used. It loads the model first, which is slower for large models."));
	}

	if (Plan.Packages.Num() == 0 && Plan.Blockers.Num() == 0)
	{
		Plan.Blockers.Add(TEXT("Nothing to remove."));
	}

	FString Hashed = Plan.Folder + (bForReimport ? TEXT("\nreimport") : TEXT(""));
	for (const FString& Package : Plan.Packages)
	{
		Hashed += TEXT("\n") + Package;
	}
	Plan.Fingerprint = FMD5::HashAnsiString(*Hashed);
	Plan.bCanRemove = Plan.Blockers.Num() == 0;
	return Plan;
}

FMCRemovalResult Remove(const FString& ScenePath, const FString& Fingerprint, bool bForReimport)
{
	const double Start = FPlatformTime::Seconds();
	FMCRemovalResult Result;
	if (!GEditor || GEditor->IsPlaySessionInProgress())
	{
		Result.Error = TEXT("Stop Play In Editor before removing a model.");
		return Result;
	}

	// The plan is built again here, so removal always works from the folder as it is now.
	const FMCRemovalPlan Plan = PlanRemoval(ScenePath, bForReimport);
	if (!Plan.bCanRemove)
	{
		Result.Error = Plan.Blockers.Num() > 0 ? Plan.Blockers[0] : TEXT("The model cannot be removed.");
		return Result;
	}
	if (Plan.Fingerprint != Fingerprint)
	{
		Result.Error = TEXT("The folder changed after it was reviewed. Review it again.");
		return Result;
	}
	MCRun::LogStep(FString::Printf(TEXT("Removing %s: %d packages"), *Plan.Folder, Plan.Packages.Num()));

	// 1. Nothing of the model may stay open or streamed in.
	const TSet<FString> DeleteSet(Plan.Packages);
	if (!Plan.OpenLevelToClose.IsEmpty())
	{
		const bool bOpenIsModel = DeleteSet.Contains(Plan.OpenLevelToClose);
		if (!bOpenIsModel && !FEditorFileUtils::SaveDirtyPackages(/*bPromptUserToSave*/ true, /*bSaveMapPackages*/ true, /*bSaveContentPackages*/ false))
		{
			Result.Error = TEXT("Cancelled at the save prompt. Nothing was deleted.");
			return Result;
		}
		MCRun::LogStep(FString::Printf(TEXT("Closing %s"), *Plan.OpenLevelToClose));
		UEditorLoadingAndSavingUtils::NewBlankMap(/*bSaveExistingMap*/ false);
	}

	// 2. Let builds and loads finish, so nothing is still reading the files.
	FAssetCompilingManager::Get().FinishAllCompilation();
	FlushAsyncLoading();

	// 3. Unload, then delete.
	bool bDeleted = false;
	if (Plan.bDeletesWithoutLoading)
	{
		bDeleted = UnloadAll(Plan.Packages, Result.Error) && DeleteFiles(Plan.Packages, Result.DeletedAssets, Result.Error);
	}
	else
	{
		bDeleted = EngineDelete(Plan.Packages, Result.DeletedAssets, Result.Error);
	}

	DeleteEmptyDirectories(Plan.DiskFolder);
	Result.bFolderRemoved = !IFileManager::Get().DirectoryExists(*Plan.DiskFolder);
	Result.Seconds = static_cast<float>(FPlatformTime::Seconds() - Start);
	if (!bDeleted)
	{
		UE_LOG(LogModelConstructor, Warning, TEXT("%s"), *Result.Error);
		return Result;
	}

	// 4. Check the result instead of assuming it.
	const TArray<FString> Left = RegistryLeftovers(Plan.Folder, Plan.Packages);
	if (Left.Num() > 0)
	{
		Result.Error = FString::Printf(TEXT("The files were deleted, but the asset registry still lists %d of them. Restart the editor before importing into %s:%s%s"),
			Left.Num(), *Plan.Folder, LINE_TERMINATOR, *MCModels::JoinCapped(Left));
		return Result;
	}

	Result.bSucceeded = true;
	MCRun::LogStep(FString::Printf(TEXT("Removed %s: %d packages in %.1f s"), *Plan.Folder, Result.DeletedAssets, Result.Seconds));
	return Result;
}

FString CheckEditorReady(const FString& Folder, const TArray<FString>& Packages)
{
	if (!GEditor)
	{
		return TEXT("The editor is not available.");
	}
	if (GEditor->IsPlaySessionInProgress())
	{
		return TEXT("A Play In Editor session started.");
	}

	FAssetCompilingManager& Compiling = FAssetCompilingManager::Get();
	if (Compiling.GetNumRemainingAssets() > 0)
	{
		Compiling.FinishAllCompilation();
	}
	FlushAsyncLoading();
	if (Compiling.GetNumRemainingAssets() > 0)
	{
		return FString::Printf(TEXT("%d assets are still building."), Compiling.GetNumRemainingAssets());
	}
	if (Registry().IsLoadingAssets())
	{
		return TEXT("The asset registry is still scanning. Wait for it to finish and try again.");
	}

	TArray<FString> Loaded;
	for (UPackage* Package : LoadedPackages(Packages))
	{
		Loaded.Add(Package->GetName());
	}
	if (Loaded.Num() > 0)
	{
		return FString::Printf(TEXT("%d packages of the old version are still in memory:%s%s"), Loaded.Num(), LINE_TERMINATOR, *MCModels::JoinCapped(Loaded));
	}

	const TArray<FString> Left = RegistryLeftovers(Folder, TArray<FString>());
	if (Left.Num() > 0)
	{
		return FString::Printf(TEXT("%d assets are still listed under %s:%s%s"), Left.Num(), *Folder, LINE_TERMINATOR, *MCModels::JoinCapped(Left));
	}
	return FString();
}
}
