// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "MCTemplates.h"
#include "MCModels.h"
#include "MCSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace MCTemplates
{
namespace
{
	const TCHAR* SettingsSection = TEXT("[Settings]");
	const TCHAR* Changed = TEXT(" (changed)");
	constexpr int32 MaxNameLength = 64;

	/** The template settings exactly as the class declares them, before any saved config. */
	FString& FactoryDefaults()
	{
		static FString Text;
		return Text;
	}

	struct FBuiltIn
	{
		const TCHAR* Name;
		/** An earlier name for this template, so names recorded on models and in saved settings still resolve. */
		const TCHAR* OldName;
		const TCHAR* Description;
		void (*Set)(UMCSettings&);
	};

	// Each built-in starts from the class defaults, so only the differences are listed here.
	const FBuiltIn BuiltIns[] = {
		{ TEXT("Datasmith Model"), nullptr,
			TEXT("Any Datasmith model. One box of collision per mesh, translucent materials kept, no schedule handling."),
			[](UMCSettings& S)
			{
				S.CollisionMode = EMCCollisionMode::SimpleBox;
				S.bAllowCpuAccess = false;
				S.bConvertTranslucentToMaskedWithDither = false;
				S.bScheduleAnimationSupport = false;
			} },
		{ TEXT("Datasmith Model with Collision"), nullptr,
			TEXT("As Datasmith Model, but with complex collision, so traces and characters hit the real surface. Builds slower and uses more memory than boxes."),
			[](UMCSettings& S)
			{
				S.CollisionMode = EMCCollisionMode::Complex;
				S.bAllowCpuAccess = false;
				S.bConvertTranslucentToMaskedWithDither = false;
				S.bScheduleAnimationSupport = false;
			} },
		{ TEXT("Twin Sync Model"), TEXT("Schedule Model"),
			TEXT("Models from Twin Sync Studio that play a construction schedule. Translucent materials become masked with a dither and schedule materials keep animating. One box of collision per mesh."),
			[](UMCSettings& S)
			{
				S.CollisionMode = EMCCollisionMode::SimpleBox;
				S.bAllowCpuAccess = false;
				S.bConvertTranslucentToMaskedWithDither = true;
				S.bScheduleAnimationSupport = true;
			} },
		{ TEXT("Twin Sync Model with Collision"), TEXT("Schedule Model with Collision"),
			TEXT("As Twin Sync Model, but with complex collision for traces against the real surface, and CPU access for runtime code that reads vertices. Uses more memory."),
			[](UMCSettings& S)
			{
				S.CollisionMode = EMCCollisionMode::Complex;
				S.bAllowCpuAccess = true;
				S.bConvertTranslucentToMaskedWithDither = true;
				S.bScheduleAnimationSupport = true;
			} },
	};

	/** Factory defaults, then the template's own lines, read back so the text is in one exact form. */
	FString Normalise(TFunctionRef<void(UMCSettings&)> Change)
	{
		// A short-lived object, so nothing is held past the call.
		TStrongObjectPtr<UMCSettings> S(NewObject<UMCSettings>(GetTransientPackage(), NAME_None, RF_Transient));
		MCModels::ImportSettings(FactoryDefaults(), *S);
		Change(*S);
		return ExportTemplateSettings(*S);
	}

	FString FileFor(const FString& Name)
	{
		return Folder() / (FPaths::MakeValidFileName(Name.TrimStartAndEnd(), TEXT('_')) + TEXT(".ini"));
	}

	bool IsBuiltInName(const FString& Name)
	{
		for (const FBuiltIn& BuiltIn : BuiltIns)
		{
			if (Name.Equals(BuiltIn.Name, ESearchCase::IgnoreCase) || (BuiltIn.OldName && Name.Equals(BuiltIn.OldName, ESearchCase::IgnoreCase)))
			{
				return true;
			}
		}
		return false;
	}

	TOptional<FTemplate> ReadFile(const FString& File)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			return {};
		}
		FTemplate Template;
		Template.File = File;

		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		FString Body;
		bool bInSettings = false;
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			if (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT(";")))
			{
				continue;
			}
			if (Trimmed == SettingsSection)
			{
				bInSettings = true;
				continue;
			}
			if (bInSettings)
			{
				Body += Trimmed + TEXT("\n");
				continue;
			}
			FString Key;
			FString Value;
			if (Trimmed.Split(TEXT("="), &Key, &Value))
			{
				if (Key == TEXT("Name"))
				{
					Template.Name = Value.TrimStartAndEnd();
				}
				else if (Key == TEXT("Description"))
				{
					Template.Description = Value.TrimStartAndEnd();
				}
				else if (Key == TEXT("Destination"))
				{
					Template.Destination = MCModels::NormaliseContentPath(Value);
				}
			}
		}
		if (Template.Name.IsEmpty())
		{
			Template.Name = FPaths::GetBaseFilename(File);
		}
		// Settings missing from the file, such as ones added in a later version, take the defaults.
		Template.Settings = Normalise([&Body](UMCSettings& S) { MCModels::ImportSettings(Body, S); });
		return Template;
	}

	bool WriteFile(const FTemplate& Template, FString& OutError)
	{
		TArray<FString> Lines;
		Lines.Add(TEXT("; Model Constructor import template. Settings not listed use the plugin defaults."));
		Lines.Add(TEXT("Name=") + Template.Name);
		if (!Template.Description.IsEmpty())
		{
			Lines.Add(TEXT("Description=") + Template.Description.Replace(TEXT("\n"), TEXT(" ")));
		}
		if (!Template.Destination.IsEmpty())
		{
			Lines.Add(TEXT("Destination=") + Template.Destination);
		}
		Lines.Add(SettingsSection);
		Lines.Add(Template.Settings);

		IFileManager::Get().MakeDirectory(*Folder(), /*Tree*/ true);
		if (!FFileHelper::SaveStringToFile(FString::Join(Lines, LINE_TERMINATOR) + LINE_TERMINATOR, *Template.File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Could not write %s. Check the file is not read-only or checked in."), *Template.File);
			return false;
		}
		return true;
	}
}

void CaptureFactoryDefaults(const UMCSettings& Defaults)
{
	if (FactoryDefaults().IsEmpty())
	{
		FactoryDefaults() = ExportTemplateSettings(Defaults);
	}
}

EGroup GroupOf(const FProperty& Property)
{
	if (!Property.HasAnyPropertyFlags(CPF_Config))
	{
		return EGroup::None;
	}
	static const TSet<FName> Model = {
		GET_MEMBER_NAME_CHECKED(UMCSettings, TemplateName),
		GET_MEMBER_NAME_CHECKED(UMCSettings, SourceFile),
		GET_MEMBER_NAME_CHECKED(UMCSettings, DestinationFolder),
		GET_MEMBER_NAME_CHECKED(UMCSettings, bUseCustomName),
		GET_MEMBER_NAME_CHECKED(UMCSettings, ModelName) };
	// Where the model goes, the level and how the run behaves are chosen per machine, so templates leave them alone.
	static const TSet<FName> General = {
		GET_MEMBER_NAME_CHECKED(UMCSettings, DuplicateFolderAction),
		GET_MEMBER_NAME_CHECKED(UMCSettings, bCreateLevelBesideModel),
		GET_MEMBER_NAME_CHECKED(UMCSettings, AfterRun),
		GET_MEMBER_NAME_CHECKED(UMCSettings, bSaveAfterImport),
		GET_MEMBER_NAME_CHECKED(UMCSettings, MeshBatchSize),
		GET_MEMBER_NAME_CHECKED(UMCSettings, MeshBatchTriangleBudget),
		GET_MEMBER_NAME_CHECKED(UMCSettings, TextureBatchSize),
		GET_MEMBER_NAME_CHECKED(UMCSettings, MaterialBatchSize),
		GET_MEMBER_NAME_CHECKED(UMCSettings, SaveBatchSize),
		GET_MEMBER_NAME_CHECKED(UMCSettings, bPauseLevelDuringPasses),
		GET_MEMBER_NAME_CHECKED(UMCSettings, bPauseViewportsDuringRun) };

	const FName Name = Property.GetFName();
	if (Name == GET_MEMBER_NAME_CHECKED(UMCSettings, ConfigVersion))
	{
		return EGroup::None;
	}
	if (Model.Contains(Name))
	{
		return EGroup::Model;
	}
	return General.Contains(Name) ? EGroup::General : EGroup::Template;
}

bool IsTemplateProperty(const FProperty& Property)
{
	return GroupOf(Property) == EGroup::Template;
}

bool IsGeneralProperty(const FProperty& Property)
{
	return GroupOf(Property) == EGroup::General;
}

namespace
{
	FString ExportGroup(const UMCSettings& Settings, EGroup Group)
	{
		TArray<FString> Lines;
		for (TFieldIterator<FProperty> It(UMCSettings::StaticClass()); It; ++It)
		{
			if (GroupOf(**It) == Group)
			{
				FString Value;
				It->ExportTextItem_Direct(Value, It->ContainerPtrToValuePtr<void>(&Settings), nullptr, nullptr, PPF_Delimited);
				Lines.Add(It->GetName() + TEXT("=") + Value);
			}
		}
		Lines.Sort();
		return FString::Join(Lines, TEXT("\n"));
	}
}

FString ExportTemplateSettings(const UMCSettings& Settings)
{
	return ExportGroup(Settings, EGroup::Template);
}

FString ExportGeneralSettings(const UMCSettings& Settings)
{
	return ExportGroup(Settings, EGroup::General);
}

void CopyGeneral(const UMCSettings& From, UMCSettings& To)
{
	for (TFieldIterator<FProperty> It(UMCSettings::StaticClass()); It; ++It)
	{
		if (GroupOf(**It) == EGroup::General)
		{
			It->CopyCompleteValue_InContainer(&To, &From);
		}
	}
}

void ApplyFactoryDefaults(UMCSettings& Target)
{
	MCModels::ImportSettings(FactoryDefaults(), Target);
}

bool HasFactoryTemplateSettings(const UMCSettings& Settings)
{
	return ExportTemplateSettings(Settings) == Normalise([](UMCSettings&) {});
}

bool ApplyRecorded(const FString& ScenePath, UMCSettings& Target)
{
	TStrongObjectPtr<UMCSettings> Recorded(NewObject<UMCSettings>(GetTransientPackage(), NAME_None, RF_Transient));
	if (!MCModels::ReadRecordedSettings(ScenePath, *Recorded))
	{
		return false;
	}
	MCModels::ImportSettings(ExportTemplateSettings(*Recorded), Target);
	Target.TemplateName = CurrentName(Recorded->TemplateName);
	return true;
}

FString Summarise(const FTemplate& Template)
{
	TStrongObjectPtr<UMCSettings> S(NewObject<UMCSettings>(GetTransientPackage(), NAME_None, RF_Transient));
	MCModels::ImportSettings(Template.Settings, *S);

	TArray<FString> Parts;
	Parts.Add(S->bEnableNanite ? TEXT("Nanite on") : TEXT("Nanite off"));
	switch (S->CollisionMode)
	{
	case EMCCollisionMode::Complex:
		Parts.Add(TEXT("collision on the real surface"));
		break;
	case EMCCollisionMode::SimpleBox:
		Parts.Add(TEXT("one collision box per mesh"));
		break;
	case EMCCollisionMode::KeepImported:
		Parts.Add(TEXT("collision as imported"));
		break;
	default:
		Parts.Add(TEXT("no collision"));
		break;
	}
	if (S->bAllowCpuAccess)
	{
		Parts.Add(TEXT("CPU access on"));
	}
	Parts.Add(S->MaxTextureSize > 0 ? FString::Printf(TEXT("textures up to %d px"), S->MaxTextureSize) : FString(TEXT("textures not capped")));
	Parts.Add(S->bConvertTranslucentToMaskedWithDither ? TEXT("translucent materials dithered") : TEXT("translucent materials kept"));
	if (S->bScheduleAnimationSupport)
	{
		Parts.Add(TEXT("schedule materials kept animating"));
	}

	FString Line = FString::Join(Parts, TEXT(", ")) + TEXT(".");
	Line[0] = FChar::ToUpper(Line[0]);
	if (!Template.Destination.IsEmpty())
	{
		Line += FString::Printf(TEXT(" Imports into %s."), *Template.Destination);
	}
	return Line;
}

TArray<FTemplate> List()
{
	TArray<FTemplate> Result;
	for (const FBuiltIn& BuiltIn : BuiltIns)
	{
		FTemplate& Template = Result.AddDefaulted_GetRef();
		Template.Name = BuiltIn.Name;
		Template.Description = BuiltIn.Description;
		Template.bBuiltIn = true;
		Template.Settings = Normalise(BuiltIn.Set);
	}

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Folder() / TEXT("*.ini")), /*Files*/ true, /*Directories*/ false);
	TArray<FTemplate> Project;
	for (const FString& File : Files)
	{
		TOptional<FTemplate> Template = ReadFile(Folder() / File);
		// A built-in name in a file would hide which one is meant, so such files are skipped.
		if (Template.IsSet() && !IsBuiltInName(Template->Name)
			&& !Project.ContainsByPredicate([&Template](const FTemplate& Other) { return Other.Name.Equals(Template->Name, ESearchCase::IgnoreCase); }))
		{
			Project.Add(MoveTemp(Template.GetValue()));
		}
	}
	Project.Sort([](const FTemplate& A, const FTemplate& B) { return A.Name < B.Name; });
	Result.Append(MoveTemp(Project));
	return Result;
}

FString CurrentName(const FString& Name)
{
	for (const FBuiltIn& BuiltIn : BuiltIns)
	{
		if (BuiltIn.OldName && Name.Equals(BuiltIn.OldName, ESearchCase::IgnoreCase))
		{
			return BuiltIn.Name;
		}
	}
	return Name;
}

TOptional<FTemplate> Find(const FString& Name)
{
	const FString Wanted = CurrentName(Name);
	for (FTemplate& Template : List())
	{
		if (Template.Name.Equals(Wanted, ESearchCase::IgnoreCase))
		{
			return MoveTemp(Template);
		}
	}
	return {};
}

void Apply(const FTemplate& Template, UMCSettings& Target)
{
	MCModels::ImportSettings(Template.Settings, Target);
	if (!Template.Destination.IsEmpty())
	{
		Target.DestinationFolder.Path = Template.Destination;
	}
	Target.TemplateName = Template.Name;
}

bool Matches(const FTemplate& Template, const UMCSettings& Settings)
{
	return ExportTemplateSettings(Settings) == Template.Settings
		&& (Template.Destination.IsEmpty() || MCModels::NormaliseContentPath(Settings.DestinationFolder.Path) == Template.Destination);
}

FString Describe(const UMCSettings& Settings)
{
	if (Settings.TemplateName.IsEmpty())
	{
		return FString();
	}
	const TOptional<FTemplate> Template = Find(Settings.TemplateName);
	if (!Template.IsSet())
	{
		return FString();
	}
	return Matches(*Template, Settings) ? Template->Name : Template->Name + Changed;
}

FString CheckName(const FString& Name, const FString& ExistingName)
{
	const FString Trimmed = Name.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return TEXT("Type a name.");
	}
	if (Trimmed.Len() > MaxNameLength)
	{
		return FString::Printf(TEXT("Use %d characters or fewer."), MaxNameLength);
	}
	if (Trimmed.EndsWith(Changed, ESearchCase::IgnoreCase))
	{
		return TEXT("Choose a name that does not end in (changed).");
	}
	if (IsBuiltInName(Trimmed))
	{
		return TEXT("A built-in template has this name.");
	}
	const bool bSameAsExisting = !ExistingName.IsEmpty() && Trimmed.Equals(ExistingName, ESearchCase::IgnoreCase);
	if (!bSameAsExisting)
	{
		if (Find(Trimmed).IsSet())
		{
			return TEXT("A template with this name already exists.");
		}
		if (FPaths::FileExists(FileFor(Trimmed)))
		{
			return TEXT("A template file with a similar name already exists.");
		}
	}
	return FString();
}

bool Save(const FString& Name, const FString& Description, bool bIncludeDestination, const UMCSettings& From, FString& OutError)
{
	OutError.Reset();
	const FString Trimmed = Name.TrimStartAndEnd();
	if (IsBuiltInName(Trimmed) || Trimmed.IsEmpty())
	{
		OutError = Trimmed.IsEmpty() ? FString(TEXT("Type a name.")) : FString(TEXT("Built-in templates cannot be changed. Save as a new template instead."));
		return false;
	}
	const TOptional<FTemplate> Existing = Find(Trimmed);

	FTemplate Template;
	Template.Name = Trimmed;
	Template.Description = Description.TrimStartAndEnd();
	Template.File = Existing.IsSet() ? Existing->File : FileFor(Trimmed);
	Template.Destination = bIncludeDestination ? MCModels::NormaliseContentPath(From.DestinationFolder.Path) : FString();
	Template.Settings = ExportTemplateSettings(From);
	return WriteFile(Template, OutError);
}

bool Rename(const FString& OldName, const FString& NewName, FString& OutError)
{
	OutError.Reset();
	TOptional<FTemplate> Template = Find(OldName);
	if (!Template.IsSet() || Template->bBuiltIn)
	{
		OutError = TEXT("Only project templates can be renamed.");
		return false;
	}
	OutError = CheckName(NewName, OldName);
	if (!OutError.IsEmpty())
	{
		return false;
	}
	const FString OldFile = Template->File;
	Template->Name = NewName.TrimStartAndEnd();
	Template->File = FileFor(Template->Name);
	if (!WriteFile(*Template, OutError))
	{
		return false;
	}
	if (!FPaths::IsSamePath(OldFile, Template->File))
	{
		IFileManager::Get().Delete(*OldFile, /*RequireExists*/ false, /*EvenReadOnly*/ false, /*Quiet*/ true);
	}
	return true;
}

bool Delete(const FString& Name, FString& OutError)
{
	OutError.Reset();
	const TOptional<FTemplate> Template = Find(Name);
	if (!Template.IsSet() || Template->bBuiltIn)
	{
		OutError = TEXT("Only project templates can be deleted.");
		return false;
	}
	if (!IFileManager::Get().Delete(*Template->File, /*RequireExists*/ false, /*EvenReadOnly*/ false, /*Quiet*/ true))
	{
		OutError = FString::Printf(TEXT("Could not delete %s. Check the file is not read-only or checked in."), *Template->File);
		return false;
	}
	return true;
}

FString Folder()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / TEXT("ModelConstructor") / TEXT("Templates"));
}
}
