// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SMCImportPage;
class SMCModelManager;
class SMCSettingsPage;
struct FSlateBrush;

/** The Model Constructor tab: switches between the Model Manager, Import, Settings and Guide pages. */
class SMCTool : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMCTool) : _LargeIcon(nullptr) {}
		/** The plugin icon at title size, shown in Model Manager. */
		SLATE_ARGUMENT(const FSlateBrush*, LargeIcon)
	SLATE_END_ARGS()

	static const FName TabName;

	void Construct(const FArguments& InArgs);

	/** Asks about unsaved settings before the tab closes. False keeps the tab open. */
	bool ConfirmClose();

private:
	enum EPage : int32 { PageManager, PageImport, PageSettings, PageGuide };

	void ShowPage(int32 Page);
	void OpenSettings(const FString& TemplateName);
	void OnTemplatesChanged();

	int32 ActivePage = PageManager;
	TSharedPtr<SMCModelManager> ManagerPage;
	TSharedPtr<SMCImportPage> ImportPage;
	TSharedPtr<SMCSettingsPage> SettingsPage;
};
