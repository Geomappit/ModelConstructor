// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "MCModels.h"
#include "MCTemplates.h"
#include "MCPasses.h"
#include "MCSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "DatasmithAssetImportData.h"
#include "DatasmithScene.h"
#include "Editor.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Base64.h"
#include "Misc/Compression.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "UObject/Package.h"

namespace MCModels
{
namespace
{
	constexpr int32 MaxListedPerBlocker = 15;

	/** Tags small enough for the asset registry. The manifest and the settings record stay out of it. */
	const TCHAR* RegistryTags[] = {
		Tags::ImportId, Tags::DisplayName, Tags::Folder, Tags::Level, Tags::CreatedLevel,
		Tags::ImportedAt, Tags::SourceFile, Tags::HasSettings, Tags::Template };

	UEditorAssetSubsystem* AssetSubsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
	}

	bool IsPackageFile(const FString& File)
	{
		const FString Extension = FPaths::GetExtension(File, /*bIncludeDot*/ true);
		return Extension == FPackageName::GetAssetPackageExtension() || Extension == FPackageName::GetMapPackageExtension();
	}

	/** Sidecar files the engine can write next to a package. They belong to the package, not to the user. */
	bool IsPackageSidecar(const FString& File)
	{
		const FString Extension = FPaths::GetExtension(File);
		return Extension == TEXT("uexp") || Extension == TEXT("ubulk") || Extension == TEXT("uptnl") || Extension == TEXT("upayload");
	}

	/** Same check as OwnedModelFolder, from tag values, so it also works on registry data. */
	FString OwnedFolderFromTags(const FString& ImportId, const FString& FolderTag, const FString& LevelTag, const FString& ScenePackage)
	{
		if (ImportId.IsEmpty())
		{
			return FString();
		}
		FString Folder = NormaliseContentPath(FolderTag);
		if (Folder.IsEmpty())
		{
			// Imports from before the folder tag: accept the scene's grandparent folder only when the level
			// the import created sits there under its expected name.
			const FString Candidate = FPaths::GetPath(FPaths::GetPath(ScenePackage));
			if (LevelTag == LevelPathFor(Candidate))
			{
				Folder = Candidate;
			}
		}
		if (Folder.IsEmpty() || !ScenePackage.StartsWith(Folder + TEXT("/")))
		{
			return FString();
		}
		return Folder;
	}

	FDateTime FileTime(const FString& File)
	{
		return File.IsEmpty() ? FDateTime::MinValue() : IFileManager::Get().GetTimeStamp(*File);
	}

	FString PackageFile(const FString& Package)
	{
		FString File;
		FPackageName::DoesPackageExist(Package, &File);
		return File;
	}

	/** Fills an entry from tag values. Get returns the value of one tag. */
	void FillEntry(FMCSceneEntry& Entry, const FString& Package, const FString& PackagePath, TFunctionRef<FString(const TCHAR*)> Get)
	{
		Entry.ImportId = Get(Tags::ImportId);
		const FString OwnedFolder = OwnedFolderFromTags(Entry.ImportId, Get(Tags::Folder), Get(Tags::Level), Package);
		Entry.Folder = OwnedFolder.IsEmpty() ? PackagePath : OwnedFolder;

		Entry.DisplayName = Get(Tags::DisplayName);
		if (Entry.DisplayName.IsEmpty())
		{
			Entry.DisplayName = FPaths::GetCleanFilename(Entry.Folder);
		}
		Entry.SourceFile = Get(Tags::SourceFile);
		Entry.LevelPath = Get(Tags::Level);
		Entry.bCreatedLevel = Get(Tags::CreatedLevel) == TEXT("true");
		Entry.bHasSettings = Get(Tags::HasSettings) == TEXT("true");
		Entry.TemplateName = MCTemplates::CurrentName(Get(Tags::Template));

		FDateTime ImportedAt;
		if (FDateTime::ParseIso8601(*Get(Tags::ImportedAt), ImportedAt))
		{
			Entry.Time = ImportedAt;
			Entry.bTimeIsImport = true;
		}
		else
		{
			Entry.Time = FileTime(PackageFile(Package));
		}
	}

	struct FCachedScene
	{
		FDateTime Stamp;
		FMCSceneEntry Entry;
	};

	/** Scenes without registry tags are loaded once and cached until their file changes. */
	TMap<FName, FCachedScene>& LoadedSceneCache()
	{
		static TMap<FName, FCachedScene> Cache;
		return Cache;
	}
}

void RegisterRegistryTags()
{
	TSet<FName>& Set = UObject::GetMetaDataTagsForAssetRegistry();
	for (const TCHAR* Tag : RegistryTags)
	{
		Set.Add(FName(Tag));
	}
}

void UnregisterRegistryTags()
{
	TSet<FName>& Set = UObject::GetMetaDataTagsForAssetRegistry();
	for (const TCHAR* Tag : RegistryTags)
	{
		Set.Remove(FName(Tag));
	}
}

IAssetRegistry& Registry()
{
	return FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
}

FString GetTag(UObject* Object, const TCHAR* Tag)
{
	UEditorAssetSubsystem* Subsystem = AssetSubsystem();
	return Object && Subsystem ? Subsystem->GetMetadataTag(Object, Tag) : FString();
}

void SetTag(UObject* Object, const TCHAR* Tag, const FString& Value)
{
	if (UEditorAssetSubsystem* Subsystem = AssetSubsystem(); Object && Subsystem)
	{
		Subsystem->SetMetadataTag(Object, Tag, Value);
	}
}

FString NormaliseContentPath(FString Path)
{
	Path.TrimStartAndEndInline();
	Path.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (Path.Len() > 1 && Path.EndsWith(TEXT("/")))
	{
		Path.LeftChopInline(1);
	}
	return Path;
}

FString RootOf(const FString& Path)
{
	FString Root;
	Path.RightChop(1).Split(TEXT("/"), &Root, nullptr);
	return Root.IsEmpty() ? Path.RightChop(1) : Root;
}

bool IsSavedMap(const FString& PackageName)
{
	return !PackageName.IsEmpty() && !PackageName.StartsWith(TEXT("/Temp/"));
}

FString CurrentMapPackage()
{
	UUnrealEditorSubsystem* EditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>() : nullptr;
	const UWorld* World = EditorSubsystem ? EditorSubsystem->GetEditorWorld() : nullptr;
	return World ? World->GetOutermost()->GetName() : FString();
}

FString LevelPathFor(const FString& Folder)
{
	return Folder / (TEXT("L_") + FPaths::GetCleanFilename(Folder));
}

FFolderScan ScanFolder(const FString& Folder)
{
	FFolderScan Scan;
	IAssetRegistry& AssetRegistry = Registry();
	AssetRegistry.ScanPathsSynchronous({ Folder }, /*bForceRescan*/ true);

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByPath(FName(*Folder), Assets, /*bRecursive*/ true);
	for (const FAssetData& Asset : Assets)
	{
		Scan.Packages.Add(Asset.PackageName);
		Scan.ClassByPackage.FindOrAdd(Asset.PackageName, Asset.AssetClassPath.GetAssetName().ToString());
	}

	if (FPackageName::TryConvertLongPackageNameToFilename(Folder + TEXT("/"), Scan.DiskFolder))
	{
		Scan.DiskFolder = FPaths::ConvertRelativePathToFull(Scan.DiskFolder);
		IFileManager::Get().IterateDirectoryRecursively(*Scan.DiskFolder, [&Scan](const TCHAR* Path, bool bIsDirectory)
		{
			if (bIsDirectory)
			{
				Scan.Directories.Add(Path);
				return true;
			}
			FString PackageName;
			if (IsPackageFile(Path) && FPackageName::TryConvertFilenameToLongPackageName(Path, PackageName))
			{
				const FName Name(*PackageName);
				Scan.Packages.Add(Name);
				Scan.SizeByPackage.Add(Name, IFileManager::Get().FileSize(Path));
				Scan.FileByPackage.Add(Name, Path);
			}
			else if (!IsPackageSidecar(Path))
			{
				++Scan.OtherFiles;
			}
			return true;
		});
	}
	return Scan;
}

FString OwnedModelFolder(UObject* Scene, const FString& ScenePackage)
{
	return OwnedFolderFromTags(GetTag(Scene, Tags::ImportId), GetTag(Scene, Tags::Folder), GetTag(Scene, Tags::Level), ScenePackage);
}

FString JoinCapped(const TArray<FString>& Lines)
{
	TArray<FString> Head;
	for (int32 Index = 0; Index < Lines.Num() && Index < MaxListedPerBlocker; ++Index)
	{
		Head.Add(TEXT("    ") + Lines[Index]);
	}
	if (Lines.Num() > MaxListedPerBlocker)
	{
		Head.Add(FString::Printf(TEXT("    and %d more"), Lines.Num() - MaxListedPerBlocker));
	}
	return FString::Join(Head, LINE_TERMINATOR);
}

TSet<FName> ReadManifest(UObject* Scene, const FString& Folder, bool& bOutHasManifest)
{
	TSet<FName> Packages;
	bOutHasManifest = false;

	const FString Encoded = GetTag(Scene, Tags::Manifest);
	const int32 RawSize = FCString::Atoi(*GetTag(Scene, Tags::ManifestSize));
	TArray<uint8> Compressed;
	if (Encoded.IsEmpty() || RawSize <= 0 || !FBase64::Decode(Encoded, Compressed))
	{
		return Packages;
	}

	TArray<uint8> Raw;
	Raw.SetNumUninitialized(RawSize);
	if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), RawSize, Compressed.GetData(), Compressed.Num()))
	{
		return Packages;
	}

	const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Raw.GetData()), Raw.Num());
	const FString Text(Converted.Length(), Converted.Get());
	TArray<FString> Lines;
	Text.ParseIntoArray(Lines, TEXT("\n"));
	for (const FString& Relative : Lines)
	{
		Packages.Add(FName(*(Folder / Relative)));
	}
	bOutHasManifest = true;
	return Packages;
}

TSet<FName> StructuralOwnership(UDatasmithScene* Scene, const FString& Folder, const FString& ScenePackage)
{
	TSet<FName> Owned;
	Owned.Add(FName(*ScenePackage));

#if WITH_EDITORONLY_DATA
	auto AddMap = [&Owned](const auto& Map)
	{
		for (const auto& Pair : Map)
		{
			Owned.Add(FName(*Pair.Value.ToSoftObjectPath().GetLongPackageName()));
		}
	};
	AddMap(Scene->StaticMeshes);
	AddMap(Scene->Textures);
	AddMap(Scene->MaterialFunctions);
	AddMap(Scene->Materials);
	AddMap(Scene->LevelSequences);
	AddMap(Scene->LevelVariantSets);
#endif

	const FString Level = GetTag(Scene, Tags::Level);
	const bool bCreated = GetTag(Scene, Tags::CreatedLevel) == TEXT("true") || Level == LevelPathFor(Folder);
	if (bCreated && Level.StartsWith(Folder + TEXT("/")))
	{
		Owned.Add(FName(*Level));
		Owned.Add(FName(*(Level + TEXT("_BuiltData"))));
	}
	return Owned;
}

TArray<FMCSceneEntry> FindScenes(bool bIncludePlugins)
{
	IAssetRegistry& AssetRegistry = Registry();

	TMap<FString, FString> PluginRoots;
	if (bIncludePlugins)
	{
		for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
		{
			if (Plugin->GetLoadedFrom() != EPluginLoadedFrom::Engine)
			{
				PluginRoots.Add(Plugin->GetMountedAssetPath(), Plugin->GetFriendlyName());
			}
		}
	}

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByClass(UDatasmithScene::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses*/ true);

	TArray<FMCSceneEntry> Entries;
	for (const FAssetData& Data : Assets)
	{
		const FString Package = Data.PackageName.ToString();
		FString Root;
		if (Package.StartsWith(TEXT("/Game/")))
		{
			Root = TEXT("Project");
		}
		else
		{
			for (const TPair<FString, FString>& Pair : PluginRoots)
			{
				if (Package.StartsWith(Pair.Key))
				{
					Root = Pair.Value;
					break;
				}
			}
		}
		if (Root.IsEmpty())
		{
			continue;
		}

		FMCSceneEntry Entry;
		FString ImportId;
		if (Data.GetTagValue(FName(Tags::ImportId), ImportId) && !ImportId.IsEmpty())
		{
			// Saved by this version: everything needed is in the registry, so nothing loads.
			FillEntry(Entry, Package, Data.PackagePath.ToString(), [&Data](const TCHAR* Tag)
			{
				FString Value;
				Data.GetTagValue(FName(Tag), Value);
				return Value;
			});
		}
		else
		{
			// Older imports and scenes imported another way: load once, then reuse until the file changes.
			const FDateTime Stamp = FileTime(PackageFile(Package));
			FCachedScene* Cached = LoadedSceneCache().Find(Data.PackageName);
			if (Cached && Cached->Stamp == Stamp)
			{
				Entry = Cached->Entry;
			}
			else
			{
				UDatasmithScene* Scene = Cast<UDatasmithScene>(Data.GetAsset());
				if (!Scene)
				{
					continue;
				}
				FillEntry(Entry, Package, Data.PackagePath.ToString(), [Scene](const TCHAR* Tag) { return GetTag(Scene, Tag); });
				Entry.bHasSettings = !GetTag(Scene, Tags::Settings).IsEmpty();
#if WITH_EDITORONLY_DATA
				if (const UAssetImportData* ImportData = Scene->AssetImportData; Entry.SourceFile.IsEmpty() && ImportData)
				{
					Entry.SourceFile = ImportData->GetFirstFilename();
				}
#endif
				LoadedSceneCache().Add(Data.PackageName, { Stamp, Entry });
			}
		}

		Entry.AssetName = Data.AssetName.ToString();
		Entry.ScenePath = Data.GetSoftObjectPath().ToString();
		Entry.Root = Root;
		if (!Entry.SourceFile.IsEmpty() && FPaths::FileExists(Entry.SourceFile))
		{
			Entry.SourceModified = FileTime(Entry.SourceFile);
		}

		Entry.Levels.Reset();
		TArray<FName> Referencers;
		AssetRegistry.GetReferencers(Data.PackageName, Referencers, UE::AssetRegistry::EDependencyCategory::Package);
		for (const FName& Referencer : Referencers)
		{
			TArray<FAssetData> InPackage;
			AssetRegistry.GetAssetsByPackageName(Referencer, InPackage);
			if (InPackage.ContainsByPredicate([](const FAssetData& Asset) { return Asset.AssetClassPath == UWorld::StaticClass()->GetClassPathName(); }))
			{
				Entry.Levels.Add(Referencer.ToString());
			}
		}
		Entry.Levels.Sort();
		Entries.Add(MoveTemp(Entry));
	}

	Entries.Sort([](const FMCSceneEntry& A, const FMCSceneEntry& B)
	{
		return A.DisplayName.Compare(B.DisplayName, ESearchCase::IgnoreCase) < 0;
	});
	return Entries;
}

bool SetDisplayName(const FString& ScenePath, const FString& Name, FString& OutError)
{
	UEditorAssetSubsystem* Subsystem = AssetSubsystem();
	UObject* Scene = FSoftObjectPath(ScenePath).TryLoad();
	if (!Subsystem || !Cast<UDatasmithScene>(Scene))
	{
		OutError = FString::Printf(TEXT("Scene not found: %s"), *ScenePath);
		return false;
	}

	const FString Trimmed = Name.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		Subsystem->RemoveMetadataTag(Scene, Tags::DisplayName);
	}
	else
	{
		Subsystem->SetMetadataTag(Scene, Tags::DisplayName, Trimmed);
	}
	Scene->MarkPackageDirty();

	if (!Subsystem->SaveLoadedAsset(Scene, /*bOnlyIfIsDirty*/ false))
	{
		OutError = TEXT("The name was set but the scene asset could not be saved.");
		return false;
	}
	return true;
}

int32 WriteManifest(UObject* Scene, const FString& Folder)
{
	const FFolderScan Scan = ScanFolder(Folder);

	TArray<FString> Relative;
	for (const FName& Package : Scan.Packages)
	{
		const FString Name = Package.ToString();
		if (Name.StartsWith(Folder + TEXT("/")))
		{
			Relative.Add(Name.RightChop(Folder.Len() + 1));
		}
	}
	Relative.Sort();

	const FTCHARToUTF8 Utf8(*FString::Join(Relative, TEXT("\n")));
	const int32 RawSize = Utf8.Length();
	int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, RawSize);
	TArray<uint8> Compressed;
	Compressed.SetNumUninitialized(CompressedSize);
	if (RawSize == 0 || !FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedSize, Utf8.Get(), RawSize))
	{
		return 0;
	}
	Compressed.SetNum(CompressedSize);

	SetTag(Scene, Tags::Manifest, FBase64::Encode(Compressed));
	SetTag(Scene, Tags::ManifestSize, FString::FromInt(RawSize));
	Scene->MarkPackageDirty();
	return Relative.Num();
}

FString ExportSettings(const UMCSettings& Settings)
{
	TArray<FString> Lines;
	for (TFieldIterator<FProperty> It(UMCSettings::StaticClass()); It; ++It)
	{
		if (!It->HasAnyPropertyFlags(CPF_Config) || It->GetFName() == GET_MEMBER_NAME_CHECKED(UMCSettings, ConfigVersion))
		{
			continue;
		}
		FString Value;
		It->ExportTextItem_Direct(Value, It->ContainerPtrToValuePtr<void>(&Settings), nullptr, nullptr, PPF_Delimited);
		Lines.Add(It->GetName() + TEXT("=") + Value);
	}
	return FString::Join(Lines, TEXT("\n"));
}

bool ImportSettings(const FString& Text, UMCSettings& Target)
{
	TArray<FString> Lines;
	Text.ParseIntoArray(Lines, TEXT("\n"));
	int32 Read = 0;
	for (const FString& Line : Lines)
	{
		FString Name;
		FString Value;
		if (!Line.Split(TEXT("="), &Name, &Value))
		{
			continue;
		}
		FProperty* Property = FindFProperty<FProperty>(UMCSettings::StaticClass(), *Name);
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Config) || Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMCSettings, ConfigVersion))
		{
			continue;
		}
		if (Property->ImportText_Direct(*Value, Property->ContainerPtrToValuePtr<void>(&Target), &Target, PPF_Delimited))
		{
			++Read;
		}
	}
	return Read > 0;
}

bool ReadRecordedSettings(const FString& ScenePath, UMCSettings& Target)
{
	UObject* Scene = FSoftObjectPath(ScenePath).TryLoad();
	const FString Text = GetTag(Scene, Tags::Settings);
	return !Text.IsEmpty() && ImportSettings(Text, Target);
}
}
