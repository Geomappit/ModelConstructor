// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

using UnrealBuildTool;

public class ModelConstructor : ModuleRules
{
	public ModelConstructor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			// UMCSettings in a public header derives from UDeveloperSettings.
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"EditorSubsystem",
			"LevelEditor",
			"ApplicationCore",
			"Slate",
			"SlateCore",
			"InputCore",
			"ToolMenus",
			"WorkspaceMenuStructure",
			"PropertyEditor",
			"Projects",
			"AssetRegistry",
			"ContentBrowser",
			"DesktopPlatform",
			"DatasmithContent",
			"DatasmithImporter",
			"MaterialEditor",
			"RenderCore",
			// FMaterialUpdateContext reads the editor's shader platform (GMaxRHIShaderPlatform) from RHI.
			"RHI",
			"ImageWrapper",
			// Removal checks whether source control is on and marks files for delete through it when it is.
			"SourceControl"
		});
	}
}
