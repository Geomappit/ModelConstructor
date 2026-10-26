// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "MCTemplates.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UMCSettings;

/**
 * Details layout for the Settings page: shows one group of settings, each row as name, value and a short
 * description taken from the ShortHelp metadata. Reset-to-default is hidden, because the archetype of an
 * edited copy is the Import page settings, not the template's own defaults.
 */
class FMCSettingsLayout : public IDetailCustomization
{
public:
	struct FArgs
	{
		/** The settings object the view edits. Read for edit conditions. */
		TWeakObjectPtr<UMCSettings> Object;
		/** The group to show. Read each time the view refreshes. */
		TFunction<MCTemplates::EGroup()> GetGroup;
		/** False while a built-in template is shown. */
		TFunction<bool()> IsEditable;
	};

	static TSharedRef<IDetailCustomization> Make(FArgs InArgs);

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/** Width of the value column, so the descriptions line up. */
	static constexpr float ValueWidth = 240.f;

private:
	explicit FMCSettingsLayout(FArgs InArgs) : Args(MoveTemp(InArgs)) {}

	FArgs Args;
};
