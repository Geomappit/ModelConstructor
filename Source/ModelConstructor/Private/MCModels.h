// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "MCTypes.h"

class UObject;
class UDatasmithScene;
class UMCSettings;
class IAssetRegistry;

/** Imported models after the fact: tags, listing, display names, the package record and the settings record. */
namespace MCModels
{
	/** Metadata tags stored on the scene asset. They are editor-only, so the model does not need this plugin to load. */
	namespace Tags
	{
		inline const TCHAR* ImportedAt = TEXT("ModelConstructor.ImportedAt");
		inline const TCHAR* ImportId = TEXT("ModelConstructor.ImportId");
		inline const TCHAR* Level = TEXT("ModelConstructor.Level");
		inline const TCHAR* CreatedLevel = TEXT("ModelConstructor.CreatedLevel");
		inline const TCHAR* Folder = TEXT("ModelConstructor.Folder");
		inline const TCHAR* DisplayName = TEXT("ModelConstructor.DisplayName");
		inline const TCHAR* SourceFile = TEXT("ModelConstructor.SourceFile");
		/** "true" when Settings is set. Small, so it can go in the asset registry. */
		inline const TCHAR* HasSettings = TEXT("ModelConstructor.HasSettings");
		/** The template the import started from, with " (changed)" when settings were edited after choosing it. */
		inline const TCHAR* Template = TEXT("ModelConstructor.Template");
		/** The settings used for the last import, one Name=Value line each. */
		inline const TCHAR* Settings = TEXT("ModelConstructor.Settings");
		/** The packages the import created, relative to the folder, compressed and base64 encoded. */
		inline const TCHAR* Manifest = TEXT("ModelConstructor.Manifest");
		inline const TCHAR* ManifestSize = TEXT("ModelConstructor.ManifestSize");
	}

	/** Adds the small tags to the asset registry, so the model list is read without loading scene assets. */
	void RegisterRegistryTags();
	void UnregisterRegistryTags();

	IAssetRegistry& Registry();

	FString GetTag(UObject* Object, const TCHAR* Tag);
	void SetTag(UObject* Object, const TCHAR* Tag, const FString& Value);

	/** Turns backslashes into slashes and trims spaces and trailing slashes. */
	FString NormaliseContentPath(FString Path);

	/** First path segment without slashes: "Game" for /Game/Models. */
	FString RootOf(const FString& Path);

	/** Untitled maps live under /Temp/ and cannot be reopened or saved without a Save As dialog. */
	bool IsSavedMap(const FString& PackageName);

	FString CurrentMapPackage();

	/** The level a model import creates for a folder: <Folder>/L_<FolderName>. */
	FString LevelPathFor(const FString& Folder);

	TArray<FMCSceneEntry> FindScenes(bool bIncludePlugins);

	/** Records every package now in the folder on the scene asset. Does not save. Returns the package count. */
	int32 WriteManifest(UObject* Scene, const FString& Folder);

	bool SetDisplayName(const FString& ScenePath, const FString& Name, FString& OutError);

	/** Settings as Name=Value lines, one per saved setting. */
	FString ExportSettings(const UMCSettings& Settings);

	/** Reads lines written by ExportSettings. Unknown names are skipped, so records from other versions still load. */
	bool ImportSettings(const FString& Text, UMCSettings& Target);

	/** Loads the settings recorded on a scene into Target. False when none were recorded. */
	bool ReadRecordedSettings(const FString& ScenePath, UMCSettings& Target);

	/** Every package under a content folder. Reads the asset registry and the disk, so a file the registry has not picked up still counts. */
	struct FFolderScan
	{
		FString DiskFolder;
		TSet<FName> Packages;
		TMap<FName, FString> ClassByPackage;
		TMap<FName, int64> SizeByPackage;
		/** Package name to the main file on disk. */
		TMap<FName, FString> FileByPackage;
		TArray<FString> Directories;
		int32 OtherFiles = 0;
	};

	FFolderScan ScanFolder(const FString& Folder);

	/** The folder Model Constructor created for this scene, or empty when the record does not match where the scene is. */
	FString OwnedModelFolder(UObject* Scene, const FString& ScenePackage);

	/** The recorded package list. bOutHasManifest is false when the import did not record one. */
	TSet<FName> ReadManifest(UObject* Scene, const FString& Folder, bool& bOutHasManifest);

	/** Packages Datasmith lists for the scene, plus the scene and the level the import created. */
	TSet<FName> StructuralOwnership(UDatasmithScene* Scene, const FString& Folder, const FString& ScenePackage);

	/** Indented lines, capped so a long list stays readable. */
	FString JoinCapped(const TArray<FString>& Lines);
}
