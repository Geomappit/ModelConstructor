// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "SMCFields.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

namespace
{
	constexpr float ValueNameWidth = 130.f;
	constexpr float DescriptionMaxWidth = 900.f;
}

FSlateFontInfo MCFields::TitleFont() { return FCoreStyle::GetDefaultFontStyle("Bold", 18); }
FSlateFontInfo MCFields::HeadingFont() { return FCoreStyle::GetDefaultFontStyle("Bold", 14); }
FSlateFontInfo MCFields::SectionFont() { return FCoreStyle::GetDefaultFontStyle("Bold", 11); }

TSharedRef<SWidget> MCFields::Label(const FText& Text)
{
	return SNew(SBox)
		.WidthOverride(LabelWidth)
		[
			SNew(STextBlock).Text(Text)
		];
}

TSharedRef<SWidget> MCFields::Group(const FText& Title, const TSharedRef<SWidget>& Content)
{
	return Panel(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(STextBlock).Text(Title).Font(SectionFont())
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Content
		],
		FMargin(8.f, 6.f, 8.f, 8.f));
}

TSharedRef<SWidget> MCFields::Panel(const TSharedRef<SWidget>& Content, FMargin Padding)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(Padding)
		[
			Content
		];
}

TSharedRef<SWidget> MCFields::PageHeader(const FText& Title, const FText& Description)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Text(Title).Font(TitleFont())
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			SNew(SBox)
			.MaxDesiredWidth(DescriptionMaxWidth)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(Description)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
}

TSharedRef<SWidget> MCFields::Section(const FText& Title)
{
	return SNew(STextBlock).Text(Title).Font(SectionFont());
}

TSharedRef<SWidget> MCFields::ValueRow(const FText& Name, TAttribute<FText> Value, TAttribute<FSlateColor> Color)
{
	return SNew(SHorizontalBox)
		.Visibility_Lambda([Value]() { return Value.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 2.f, 8.f, 2.f)
		[
			SNew(SBox)
			.WidthOverride(ValueNameWidth)
			[
				SNew(STextBlock)
				.Text(Name)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 2.f)
		[
			SNew(STextBlock)
			.Text(Value)
			.ToolTipText(Value)
			.ColorAndOpacity(Color)
			.AutoWrapText(true)
		];
}

TSharedRef<SWidget> MCFields::IconButton(FName Icon, const FText& LabelText, const FText& ToolTip, TFunction<void()> OnClick, TAttribute<bool> IsEnabled, bool bPrimary, float Width)
{
	return SNew(SBox)
		.WidthOverride(Width > 0.f ? FOptionalSize(Width) : FOptionalSize())
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get(), bPrimary ? "PrimaryButton" : "Button")
			.ContentPadding(Width > 0.f ? FMargin(8.f, 4.f) : FMargin(4.f, 2.f))
			.ToolTipText(ToolTip)
			.IsEnabled(IsEnabled)
			.OnClicked_Lambda([OnClick]()
			{
				OnClick();
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(SImage).Image(FAppStyle::GetBrush(Icon)).ColorAndOpacity(FSlateColor::UseForeground())
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(LabelText)
				]
			]
		];
}

TSharedRef<SWidget> MCFields::PathRow(const FText& LabelText, TAttribute<FText> Value, TFunction<void(const FString&)> OnCommitted, TFunction<void()> OnBrowse, TAttribute<bool> IsEnabled)
{
	return SNew(SHorizontalBox)
		.IsEnabled(IsEnabled)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			Label(LabelText)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SEditableTextBox)
			.Text(Value)
			.OnTextCommitted_Lambda([OnCommitted](const FText& Text, ETextCommit::Type) { OnCommitted(Text.ToString()); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("BrowseButton", "..."))
			.ToolTipText(LOCTEXT("BrowseButtonTip", "Browse"))
			.OnClicked_Lambda([OnBrowse]()
			{
				OnBrowse();
				return FReply::Handled();
			})
		];
}

#undef LOCTEXT_NAMESPACE
