// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "SMCTool.h"
#include "SMCGuidePage.h"
#include "SMCImportPage.h"
#include "SMCModelManager.h"
#include "SMCSettingsPage.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

const FName SMCTool::TabName(TEXT("ModelConstructor"));

void SMCTool::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 8.f, 8.f, 4.f).HAlign(HAlign_Left)
		[
			SNew(SSegmentedControl<int32>)
			.Value_Lambda([this]() { return ActivePage; })
			.OnValueChanged_Lambda([this](int32 Page) { ShowPage(Page); })
			+ SSegmentedControl<int32>::Slot(PageManager).Text(LOCTEXT("ManagerPage", "Model Manager"))
			+ SSegmentedControl<int32>::Slot(PageImport).Text(LOCTEXT("ImportPage", "Import"))
			+ SSegmentedControl<int32>::Slot(PageSettings).Text(LOCTEXT("SettingsPage", "Settings"))
			+ SSegmentedControl<int32>::Slot(PageGuide).Text(LOCTEXT("GuidePage", "Guide"))
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([this]() { return ActivePage; })
			+ SWidgetSwitcher::Slot()
			[
				SAssignNew(ManagerPage, SMCModelManager)
				.Icon(InArgs._LargeIcon)
				.OnImportNew_Lambda([this]() { ShowPage(PageImport); })
				.OnOpenSettings(this, &SMCTool::OpenSettings)
				.OnOpenGuide_Lambda([this]() { ShowPage(PageGuide); })
				.OnTemplatesChanged(this, &SMCTool::OnTemplatesChanged)
			]
			+ SWidgetSwitcher::Slot()
			[
				SAssignNew(ImportPage, SMCImportPage)
				.OnImported_Lambda([this]() { ManagerPage->MarkStale(); })
				.OnEditTemplate(this, &SMCTool::OpenSettings)
				.OnTemplatesChanged(this, &SMCTool::OnTemplatesChanged)
			]
			+ SWidgetSwitcher::Slot()
			[
				SAssignNew(SettingsPage, SMCSettingsPage)
				.OnUseTemplate_Lambda([this](const FString& Name)
				{
					ImportPage->UseTemplate(Name);
					ShowPage(PageImport);
				})
				.OnTemplatesChanged(this, &SMCTool::OnTemplatesChanged)
			]
			+ SWidgetSwitcher::Slot()
			[
				SNew(SMCGuidePage)
			]
		]
	];
	ManagerPage->Refresh();
}

void SMCTool::ShowPage(int32 Page)
{
	if (Page == ActivePage)
	{
		return;
	}
	if (ActivePage == PageSettings && !SettingsPage->ConfirmLeave())
	{
		return;
	}
	ActivePage = Page;
	// Templates and models can change on disk or through scripts, so each page reads them when shown.
	if (Page == PageManager)
	{
		ManagerPage->RefreshIfStale();
		ManagerPage->RefreshTemplates();
	}
	else if (Page == PageImport)
	{
		ImportPage->RefreshTemplates();
	}
	else if (Page == PageSettings)
	{
		SettingsPage->RefreshTemplates();
	}
}

void SMCTool::OpenSettings(const FString& TemplateName)
{
	ShowPage(PageSettings);
	if (ActivePage == PageSettings && !TemplateName.IsEmpty())
	{
		SettingsPage->SelectTemplate(TemplateName);
	}
}

void SMCTool::OnTemplatesChanged()
{
	ImportPage->RefreshTemplates();
	ManagerPage->RefreshTemplates();
	if (ActivePage != PageSettings)
	{
		SettingsPage->RefreshTemplates();
	}
}

bool SMCTool::ConfirmClose()
{
	return !SettingsPage.IsValid() || SettingsPage->ConfirmLeave();
}

#undef LOCTEXT_NAMESPACE
