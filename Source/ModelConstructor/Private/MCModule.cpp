// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "MCLogos.h"
#include "MCModels.h"
#include "MCPasses.h"
#include "MCSettings.h"
#include "MCTemplates.h"
#include "SMCTool.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

DEFINE_LOG_CATEGORY(LogModelConstructor);

UMCSettings::UMCSettings()
{
	DestinationFolder.Path = TEXT("/Game/Models");
	ScheduleActorTags = { TEXT("ScheduleAnimatedCluster"), TEXT("ScheduleAnimatedUnclustered") };
	// Saved config is loaded after the constructor, so this is the one place the declared defaults can be read.
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		MCTemplates::CaptureFactoryDefaults(*this);
	}
}

#if WITH_EDITOR
FText UMCSettings::GetSectionText() const
{
	return LOCTEXT("SettingsSection", "Model Constructor");
}

void UMCSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// The pages edit temporary copies; only the shared settings are written to the config file.
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		SaveConfig();
	}
}
#endif

bool UMCSettings::UpgradeConfig()
{
	if (ConfigVersion >= CurrentConfigVersion)
	{
		return false;
	}
	// No saved section means a fresh install. ConfigVersion is 0 only because nothing was written,
	// so the declared defaults are already current and must not be migrated.
	if (!GConfig->DoesSectionExist(*GetClass()->GetPathName(), GetClass()->GetConfigName()))
	{
		ConfigVersion = CurrentConfigVersion;
		return false;
	}
	if (ConfigVersion < 2)
	{
		bEnableNanite = true;
		bScheduleAnimationSupport = true;
		AfterRun = EMCAfterRun::ReturnToPrevious;
	}
	else if (ConfigVersion < 3)
	{
		// Two old flags became AfterRun. Their keys are no longer properties, so they are read from the file directly.
		bool bRestorePrevious = true;
		bool bOpenModelLevel = false;
		const FString Section = GetClass()->GetPathName();
		const FString& File = GetClass()->GetConfigName();
		GConfig->GetBool(*Section, TEXT("bRestorePreviousLevel"), bRestorePrevious, File);
		GConfig->GetBool(*Section, TEXT("bOpenLevelAfterImport"), bOpenModelLevel, File);
		AfterRun = bOpenModelLevel || !bRestorePrevious ? EMCAfterRun::OpenModelLevel : EMCAfterRun::ReturnToPrevious;
	}
	if (ConfigVersion < 5)
	{
		// Only values still at the old default move, so a user's own choice survives.
		if (ComplexCollisionTriangleLimit == 1000000) { ComplexCollisionTriangleLimit = 1500000; }
		if (NavigationTriangleLimit == 100000) { NavigationTriangleLimit = 150000; }
		if (MeshBatchSize == 32) { MeshBatchSize = 128; }
	}
	// Built-in templates were renamed; keep the Import page on the same one.
	TemplateName = MCTemplates::CurrentName(TemplateName);
	ConfigVersion = CurrentConfigVersion;
	SaveConfig();
	return true;
}

void UMCSettings::CopySettingsFrom(const UMCSettings& Other)
{
	for (TFieldIterator<FProperty> It(UMCSettings::StaticClass()); It; ++It)
	{
		if (It->HasAnyPropertyFlags(CPF_Config) && It->GetFName() != GET_MEMBER_NAME_CHECKED(UMCSettings, ConfigVersion))
		{
			It->CopyCompleteValue_InContainer(this, &Other);
		}
	}
}

namespace
{
	const FName StyleName(TEXT("ModelConstructorStyle"));
	const FName IconName(TEXT("ModelConstructor.Icon"));
	const FName LargeIconName(TEXT("ModelConstructor.Icon.Large"));
}

class FMCModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		MCModels::RegisterRegistryTags();
		if (GetMutableDefault<UMCSettings>()->UpgradeConfig())
		{
			UE_LOG(LogModelConstructor, Log, TEXT("Model Constructor settings updated to the defaults of this version"));
		}

		// Commandlets load editor modules without Slate, so there is no UI to register.
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}

		RegisterStyle();

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(SMCTool::TabName, FOnSpawnTab::CreateStatic(&FMCModule::SpawnTab))
			.SetDisplayName(LOCTEXT("TabTitle", "Model Constructor"))
			.SetIcon(FSlateIcon(StyleName, IconName))
			.SetTooltipText(LOCTEXT("TabTip", "Import Datasmith models, prepare them for Nanite, and re-import or remove them."))
			.SetMenuType(ETabSpawnerMenuType::Hidden);
		bTabRegistered = true;

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMCModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		MCLogos::Release();
		MCModels::UnregisterRegistryTags();
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

		if (bTabRegistered && FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SMCTool::TabName);
		}
		bTabRegistered = false;

		if (Style.IsValid())
		{
			FSlateStyleRegistry::UnRegisterSlateStyle(*Style);
			Style.Reset();
		}
	}

private:
	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args)
	{
		const TSharedRef<SMCTool> Tool = SNew(SMCTool).LargeIcon(FSlateIcon(StyleName, LargeIconName).GetIcon());
		const TWeakPtr<SMCTool> WeakTool = Tool;
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			.OnCanCloseTab_Lambda([WeakTool]()
			{
				const TSharedPtr<SMCTool> Pinned = WeakTool.Pin();
				return !Pinned.IsValid() || Pinned->ConfirmClose();
			})
			[
				Tool
			];
	}

	void RegisterStyle()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ModelConstructor"));
		if (!Plugin.IsValid())
		{
			return;
		}
		Style = MakeShared<FSlateStyleSet>(StyleName);
		const FString Icon = Plugin->GetBaseDir() / TEXT("Resources") / TEXT("Icon128.png");
		Style->Set(IconName, new FSlateImageBrush(Icon, FVector2D(20.f, 20.f)));
		Style->Set(FName(TEXT("ModelConstructor.Icon.Small")), new FSlateImageBrush(Icon, FVector2D(16.f, 16.f)));
		Style->Set(LargeIconName, new FSlateImageBrush(Icon, FVector2D(48.f, 48.f)));
		FSlateStyleRegistry::RegisterSlateStyle(*Style);
	}

	static void OpenTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(FTabId(SMCTool::TabName));
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("ModelConstructor"), LOCTEXT("MenuSection", "Model Constructor"));
		Section.AddMenuEntry(
			TEXT("ModelConstructorImporter"),
			LOCTEXT("MenuEntry", "Model Constructor"),
			LOCTEXT("MenuEntryTip", "Import Datasmith models, prepare them for Nanite, and re-import or remove them."),
			FSlateIcon(StyleName, IconName),
			FUIAction(FExecuteAction::CreateStatic(&FMCModule::OpenTab)));

		UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.User"));
		FToolMenuSection& ToolbarSection = Toolbar->FindOrAddSection(TEXT("ModelConstructor"));
		ToolbarSection.AddEntry(FToolMenuEntry::InitToolBarButton(
			TEXT("ModelConstructorToolbar"),
			FUIAction(FExecuteAction::CreateStatic(&FMCModule::OpenTab)),
			LOCTEXT("ToolbarLabel", "Model Constructor"),
			LOCTEXT("MenuEntryTip", "Import Datasmith models, prepare them for Nanite, and re-import or remove them."),
			FSlateIcon(StyleName, IconName)));
	}

	bool bTabRegistered = false;
	TSharedPtr<FSlateStyleSet> Style;
};

IMPLEMENT_MODULE(FMCModule, ModelConstructor)

#undef LOCTEXT_NAMESPACE
