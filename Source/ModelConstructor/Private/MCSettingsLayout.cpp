// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "MCSettingsLayout.h"
#include "MCSettings.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** The bool a property's EditCondition names, when the condition is a single bool. */
	const FBoolProperty* EditConditionOf(const FProperty& Property)
	{
		const FString Condition = Property.GetMetaData(TEXT("EditCondition")).TrimStartAndEnd();
		return Condition.IsEmpty() ? nullptr : FindFProperty<FBoolProperty>(UMCSettings::StaticClass(), *Condition);
	}
}

TSharedRef<IDetailCustomization> FMCSettingsLayout::Make(FArgs InArgs)
{
	return MakeShareable(new FMCSettingsLayout(MoveTemp(InArgs)));
}

void FMCSettingsLayout::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	const MCTemplates::EGroup Shown = Args.GetGroup ? Args.GetGroup() : MCTemplates::EGroup::Template;
	const TFunction<bool()> IsEditable = Args.IsEditable;
	const TWeakObjectPtr<UMCSettings> Object = Args.Object;

	// Declaration order, so the groups and rows read the same as the settings class.
	for (TFieldIterator<FProperty> It(UMCSettings::StaticClass()); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}
		const TSharedRef<IPropertyHandle> Handle = DetailBuilder.GetProperty(Property->GetFName(), UMCSettings::StaticClass());
		if (!Handle->IsValidHandle())
		{
			continue;
		}
		if (MCTemplates::GroupOf(*Property) != Shown)
		{
			DetailBuilder.HideProperty(Handle);
			continue;
		}

		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(FName(*Property->GetMetaData(TEXT("Category"))));
		IDetailPropertyRow& Row = Category.AddProperty(Handle);
		Row.OverrideResetToDefault(FResetToDefaultOverride::Hide());

		TSharedPtr<SWidget> NameWidget;
		TSharedPtr<SWidget> ValueWidget;
		Row.GetDefaultWidgets(NameWidget, ValueWidget);

		const FBoolProperty* Condition = EditConditionOf(*Property);
		auto IsEnabled = [IsEditable, Object, Condition]()
		{
			if (IsEditable && !IsEditable())
			{
				return false;
			}
			const UMCSettings* Settings = Object.Get();
			return !Condition || !Settings || Condition->GetPropertyValue_InContainer(Settings);
		};

		// Arrays keep their element rows under the header.
		const bool bShowChildren = Property->IsA<FArrayProperty>();
		Row.CustomWidget(bShowChildren)
			.NameContent()
			[
				NameWidget.ToSharedRef()
			]
			.ValueContent()
			.MinDesiredWidth(ValueWidth + 260.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(ValueWidth)
					.IsEnabled_Lambda(IsEnabled)
					[
						ValueWidget.ToSharedRef()
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(16.f, 2.f, 4.f, 2.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Property->GetMetaData(TEXT("ShortHelp"))))
					.ToolTipText(Property->GetToolTipText())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.AutoWrapText(true)
				]
			];
	}
}
