// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"

class FProperty;
class UMCSettings;

/**
 * Import templates: named sets of the settings that decide how a model is processed (import options,
 * Nanite, collision, textures, materials). The model rows of the Import page and the General settings
 * (folder, level, run and batch options) stay outside them.
 * Built-in templates live in code and cannot be changed. Project templates are text files in
 * Config/ModelConstructor/Templates, so they can be committed and shared.
 */
namespace MCTemplates
{
	struct FTemplate
	{
		FString Name;
		FString Description;
		bool bBuiltIn = false;
		/** Full path of the template file. Empty for built-in templates. */
		FString File;
		/** Content folder the template imports into. Empty keeps the current destination. */
		FString Destination;
		/** Every template setting as Name=Value lines, after the template is applied over the defaults. */
		FString Settings;
	};

	/** Called once from the settings class constructor, before saved config is loaded. */
	void CaptureFactoryDefaults(const UMCSettings& Defaults);

	/** Which part of the tool a saved setting belongs to. */
	enum class EGroup : uint8
	{
		/** Source, destination, name and chosen template: set on the Import page. */
		Model,
		/** How the model is processed: held by templates. */
		Template,
		/** Folder, level, run and batch options: the same for every import. */
		General,
		/** Not a setting, or not shown. */
		None
	};

	EGroup GroupOf(const FProperty& Property);

	/** True for settings a template holds. */
	bool IsTemplateProperty(const FProperty& Property);
	bool IsGeneralProperty(const FProperty& Property);

	/** The template settings of an object as sorted Name=Value lines, for saving and comparing. */
	FString ExportTemplateSettings(const UMCSettings& Settings);
	FString ExportGeneralSettings(const UMCSettings& Settings);

	/** Copies the General settings, and nothing else, from one object to another. */
	void CopyGeneral(const UMCSettings& From, UMCSettings& To);

	/** Sets every template setting to the value the class declares, before any saved config. */
	void ApplyFactoryDefaults(UMCSettings& Target);

	/** True when the template settings still have the values the class declares. */
	bool HasFactoryTemplateSettings(const UMCSettings& Settings);

	/**
	 * Applies the template part of the settings recorded at a model's last import, and its template name.
	 * Folder, level, run and batch options are left as they are, so an old record cannot bring back outdated values.
	 * False when the model has no record.
	 */
	bool ApplyRecorded(const FString& ScenePath, UMCSettings& Target);

	/** One plain line saying what a template does, for the Import page. */
	FString Summarise(const FTemplate& Template);

	/** Built-in templates first, then project templates by name. Reads the template folder each call. */
	TArray<FTemplate> List();
	/** The current name of a built-in template, given a name it had before. Other names come back unchanged. */
	FString CurrentName(const FString& Name);
	TOptional<FTemplate> Find(const FString& Name);

	/** Sets every template setting from the template, the destination when it has one, and the template name. */
	void Apply(const FTemplate& Template, UMCSettings& Target);

	/** True when the target's template settings, and destination if the template sets one, equal the template's. */
	bool Matches(const FTemplate& Template, const UMCSettings& Settings);

	/** "Name", "Name (changed)", or empty when no template was chosen or it no longer exists. */
	FString Describe(const UMCSettings& Settings);

	/** Why a name cannot be used for a new or renamed template, or empty. ExistingName is the template being renamed or overwritten. */
	FString CheckName(const FString& Name, const FString& ExistingName = FString());

	/** Saves the template settings of From as a project template. Overwrites a project template of the same name. */
	bool Save(const FString& Name, const FString& Description, bool bIncludeDestination, const UMCSettings& From, FString& OutError);
	bool Rename(const FString& OldName, const FString& NewName, FString& OutError);
	bool Delete(const FString& Name, FString& OutError);

	/** Config/ModelConstructor/Templates in the project, as a full path. */
	FString Folder();
}
