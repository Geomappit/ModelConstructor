// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "MCSettings.h"
#include "MCTemplates.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class IDetailsView;

/**
 * Settings page: the template list on the left, the selected template's settings on the right.
 * Built-in templates are shown read-only; project templates and the General settings are edited on a
 * copy and written only when the user saves.
 */
class SMCSettingsPage : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnTemplateNamed, const FString& /*Name*/);

	SLATE_BEGIN_ARGS(SMCSettingsPage) {}
		/** Fires when the user asks to import with a template. */
		SLATE_EVENT(FOnTemplateNamed, OnUseTemplate)
		/** Fires after a template is saved, added, renamed or deleted. */
		SLATE_EVENT(FSimpleDelegate, OnTemplatesChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Shows a template, asking first when the current one has unsaved changes. */
	void SelectTemplate(const FString& Name);

	/** Reads the template folder again and keeps the selection when it still exists. */
	void RefreshTemplates();

	bool HasUnsavedChanges() const { return bDirty; }

	/** Offers to save or discard unsaved changes. False when the user chose to keep editing. */
	bool ConfirmLeave();

private:
	struct FItem
	{
		bool bGeneral = false;
		MCTemplates::FTemplate Template;
	};
	using FItemPtr = TSharedPtr<FItem>;

	TSharedRef<SWidget> BuildList();
	TSharedRef<SWidget> BuildEditor();
	TSharedRef<SWidget> BuildProjectRows();
	TSharedRef<SWidget> MakeMoreMenu();
	TSharedRef<ITableRow> OnGenerateRow(FItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnSelectionChanged(FItemPtr Item, ESelectInfo::Type SelectInfo);

	void Load(const FItemPtr& Item);
	void Select(const FItemPtr& Item);
	FItemPtr Find(const FString& Name) const;
	FString State() const;
	void UpdateDirty();
	EActiveTimerReturnType OnTimer(double InCurrentTime, float InDeltaTime);

	bool IsGeneral() const { return Current.IsValid() && Current->bGeneral; }
	bool IsProject() const { return Current.IsValid() && !Current->bGeneral && !Current->Template.bBuiltIn; }
	bool IsBuiltIn() const { return Current.IsValid() && !Current->bGeneral && Current->Template.bBuiltIn; }
	bool IsEditable() const { return IsGeneral() || IsProject(); }

	FText GetTitleText() const;
	FText GetInfoText() const;

	bool Save();
	void OnNew();
	void OnDuplicate();
	void OnRename();
	void OnDelete();

	FOnTemplateNamed OnUseTemplate;
	FSimpleDelegate OnTemplatesChanged;

	TArray<FItemPtr> Items;
	FItemPtr Current;
	TSharedPtr<SListView<FItemPtr>> List;
	TSharedPtr<IDetailsView> DetailsView;

	/** The copy being edited. Never the Import page settings, so nothing changes until Save. */
	TStrongObjectPtr<UMCSettings> Working;
	FString WorkingDescription;
	FString WorkingDestination;
	bool bWorkingSetsDestination = false;

	/** State() when the item was loaded or last saved. */
	FString SavedState;
	bool bDirty = false;
	/** Set while the list selection is changed in code, so the change is not taken as the user's. */
	bool bSelectingInCode = false;
};
