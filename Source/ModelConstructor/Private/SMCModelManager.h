// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "MCTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SMCReimportPanel;
class SScrollBox;
struct FSlateBrush;

/**
 * Model Manager page: every Datasmith scene in the project on the left; the selected model's details,
 * levels and actions on the right, with a re-import section that opens under the details.
 */
class SMCModelManager : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnTemplateNamed, const FString& /*Name*/);

	SLATE_BEGIN_ARGS(SMCModelManager) : _Icon(nullptr) {}
		/** Shown in the title card. */
		SLATE_ARGUMENT(const FSlateBrush*, Icon)
		SLATE_EVENT(FSimpleDelegate, OnImportNew)
		/** Opens the Settings page, on a template when the name is not empty. */
		SLATE_EVENT(FOnTemplateNamed, OnOpenSettings)
		SLATE_EVENT(FSimpleDelegate, OnOpenGuide)
		/** Fires after this page adds a template. */
		SLATE_EVENT(FSimpleDelegate, OnTemplatesChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void Refresh();
	void RefreshIfStale();
	void MarkStale() { bStale = true; }
	void RefreshTemplates();

private:
	using FEntry = TSharedPtr<FMCSceneEntry>;

	TSharedRef<SWidget> BuildSidebar();
	TSharedRef<SWidget> BuildTitleCard();
	TSharedRef<SWidget> BuildActions();
	TSharedRef<SWidget> BuildModelList();
	TSharedRef<SWidget> BuildDetails();
	TSharedRef<SWidget> BuildDetailRows();
	TSharedRef<SWidget> BuildButtonRow();
	TSharedRef<SWidget> BuildLevels();
	TSharedRef<SWidget> MakeMoreMenu();

	void ApplyFilter();
	void OnSelectionChanged(FEntry Item, ESelectInfo::Type SelectInfo);
	FEntry Selected() const;
	void SelectScene(const FString& ScenePath);
	FText GetCountText() const;
	FText GetStatusText() const;
	bool CanReimport() const;
	bool CanRemove() const;

	void OpenReimport();
	void OnShowInContentBrowser();
	void OnOpenLevel();
	void OnRename();
	void OnRemove();
	void OnSaveAsTemplate();

	FSimpleDelegate OnImportNew;
	FOnTemplateNamed OnOpenSettings;
	FSimpleDelegate OnOpenGuide;
	FSimpleDelegate OnTemplatesChanged;
	const FSlateBrush* Icon = nullptr;

	bool bIncludePlugins = false;
	bool bStale = true;
	bool bReimportOpen = false;
	FString FilterText;

	TArray<FEntry> AllItems;
	TArray<FEntry> FilteredItems;
	TArray<TSharedPtr<FString>> LevelItems;

	TSharedPtr<SListView<FEntry>> List;
	TSharedPtr<SListView<TSharedPtr<FString>>> LevelList;
	TSharedPtr<SMCReimportPanel> ReimportPanel;
	TSharedPtr<SScrollBox> DetailsScroll;
};
