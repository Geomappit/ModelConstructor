// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "MCLogos.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBox;
class SVerticalBox;

/** Guide page: how to use each page, what the settings mean, example uses, and links. */
class SMCGuidePage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMCGuidePage) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** Adds a section heading to the content and a matching link to the contents list. */
	void AddSection(const FText& Title);

	void BuildContent();
	void AddStartSection();
	void AddManagerSection();
	void AddImportSection();
	void AddSettingsPageSection();
	void AddReimportSection();
	void AddRemoveSection();
	void AddSettingsSection();
	void AddExamplesSection();

	TSharedRef<SWidget> BuildSidebar();
	TSharedRef<SWidget> MakeLinkCard(const FText& Label, const FText& Description, const FString& Url, MCLogos::ELogo LogoId) const;

	TSharedPtr<SScrollBox> ContentScroll;
	TSharedPtr<SVerticalBox> Content;
	TSharedPtr<SVerticalBox> Contents;
};
