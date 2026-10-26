// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Misc/Attribute.h"
#include "Styling/SlateColor.h"
#include "Widgets/SWidget.h"

/** Small layout pieces shared by the pages, so they look the same. */
namespace MCFields
{
	/** Width of the label column in the Model boxes. */
	constexpr float LabelWidth = 110.f;

	/** Space between the boxes on a page. */
	constexpr float Gap = 8.f;

	FSlateFontInfo TitleFont();
	FSlateFontInfo HeadingFont();
	FSlateFontInfo SectionFont();

	/** A label sized to the label column. */
	TSharedRef<SWidget> Label(const FText& Text);

	/** A framed box with a heading. */
	TSharedRef<SWidget> Group(const FText& Title, const TSharedRef<SWidget>& Content);

	/** A framed box without a heading. */
	TSharedRef<SWidget> Panel(const TSharedRef<SWidget>& Content, FMargin Padding = FMargin(10.f, 8.f));

	/** A page title with a short description under it, wrapped to a readable width. */
	TSharedRef<SWidget> PageHeader(const FText& Title, const FText& Description);

	/** A bold heading for a part of a page. */
	TSharedRef<SWidget> Section(const FText& Title);

	/** One read-only row of a details list: a subdued name, then the value. Collapsed while the value is empty. */
	TSharedRef<SWidget> ValueRow(const FText& Name, TAttribute<FText> Value, TAttribute<FSlateColor> Color = FSlateColor::UseForeground());

	/** A button with an icon in front of the text. Width 0 sizes it to its content. */
	TSharedRef<SWidget> IconButton(FName Icon, const FText& Label, const FText& ToolTip, TFunction<void()> OnClick,
		TAttribute<bool> IsEnabled = true, bool bPrimary = false, float Width = 0.f);

	/** A row with a label, a text box and a browse button. */
	TSharedRef<SWidget> PathRow(const FText& Label, TAttribute<FText> Value, TFunction<void(const FString&)> OnCommitted, TFunction<void()> OnBrowse, TAttribute<bool> IsEnabled = true);
}
