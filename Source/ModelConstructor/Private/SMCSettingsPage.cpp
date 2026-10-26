// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "SMCSettingsPage.h"
#include "MCModels.h"
#include "MCSettingsLayout.h"
#include "SMCDialogs.h"
#include "SMCFields.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IDetailsView.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "UObject/Package.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

namespace
{
	UMCSettings& ImportPageSettings()
	{
		return *GetMutableDefault<UMCSettings>();
	}
}

void SMCSettingsPage::Construct(const FArguments& InArgs)
{
	OnUseTemplate = InArgs._OnUseTemplate;
	OnTemplatesChanged = InArgs._OnTemplatesChanged;
	Working.Reset(NewObject<UMCSettings>(GetTransientPackage(), NAME_None, RF_Transient));

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs Args;
	Args.bAllowSearch = true;
	Args.bHideSelectionTip = true;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	Args.ColumnWidth = 0.3f;
	DetailsView = PropertyEditor.CreateDetailView(Args);
	DetailsView->RegisterInstancedCustomPropertyLayout(UMCSettings::StaticClass(), FOnGetDetailCustomizationInstance::CreateLambda([this]()
	{
		FMCSettingsLayout::FArgs Layout;
		Layout.Object = Working.Get();
		Layout.GetGroup = [this]() { return IsGeneral() ? MCTemplates::EGroup::General : MCTemplates::EGroup::Template; };
		Layout.IsEditable = [this]() { return IsEditable(); };
		return FMCSettingsLayout::Make(MoveTemp(Layout));
	}));
	DetailsView->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateLambda([this]() { return IsEditable(); }));
	DetailsView->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&) { UpdateDirty(); });
	// The view edits one working copy for the whole session; Load fills it and refreshes the rows.
	DetailsView->SetObject(Working.Get());

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(12.f, 8.f, 12.f, 10.f)
		[
			MCFields::PageHeader(LOCTEXT("SettingsTitle", "Import settings"),
				LOCTEXT("SettingsIntro", "A template sets how a model is processed: Nanite, collision, textures and materials. You choose one on the Import page for each import. Built-in templates can't be changed, so duplicate one to make your own. Project templates are saved in Config/ModelConstructor/Templates, so you can commit them and share them with your team. General holds the folder, level and run options that apply to every import."))
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(8.f, 0.f, 8.f, 8.f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			+ SSplitter::Slot().Value(0.28f)
			[
				BuildList()
			]
			+ SSplitter::Slot().Value(0.72f)
			[
				BuildEditor()
			]
		]
	];

	RefreshTemplates();
	if (!Current.IsValid() && Items.Num() > 0)
	{
		const FItemPtr First = Find(ImportPageSettings().TemplateName);
		Select(First.IsValid() ? First : Items[0]);
	}
	RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SMCSettingsPage::OnTimer));
}

TSharedRef<SWidget> SMCSettingsPage::BuildList()
{
	return MCFields::Panel(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			MCFields::Section(LOCTEXT("TemplatesHeading", "Templates"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MCFields::IconButton("Icons.Plus", LOCTEXT("NewTemplate", "New"), LOCTEXT("NewTemplateTip", "Make a project template that starts from the plugin defaults."),
					[this]() { OnNew(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
			[
				MCFields::IconButton("Icons.Duplicate", LOCTEXT("DuplicateTemplate", "Duplicate"), LOCTEXT("DuplicateTemplateTip", "Make a project template that starts as a copy of the selected one."),
					[this]() { OnDuplicate(); }, TAttribute<bool>::CreateLambda([this]() { return Current.IsValid() && !Current->bGeneral; }))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SComboButton)
				.HasDownArrow(true)
				.ToolTipText(LOCTEXT("TemplateMoreTip", "Rename, delete or find project templates."))
				.ButtonContent()
				[
					SNew(STextBlock).Text(LOCTEXT("TemplateMore", "More"))
				]
				.OnGetMenuContent(this, &SMCSettingsPage::MakeMoreMenu)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SAssignNew(List, SListView<FItemPtr>)
			.ListItemsSource(&Items)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SMCSettingsPage::OnGenerateRow)
			.OnSelectionChanged(this, &SMCSettingsPage::OnSelectionChanged)
		],
		FMargin(8.f));
}

TSharedRef<SWidget> SMCSettingsPage::BuildEditor()
{
	const TSharedRef<SWidget> Header = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(this, &SMCSettingsPage::GetTitleText).Font(MCFields::HeadingFont())
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this]() { return bDirty ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text(LOCTEXT("UnsavedChanges", "Unsaved changes"))
				.ColorAndOpacity(MCDialogs::WarningColor())
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
			[
				// Two buttons rather than one restyled button: Slate button styles are not attributes.
				SNew(SBox)
				.MinDesiredWidth(110.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SButton)
						.Visibility_Lambda([this]() { return bDirty ? EVisibility::Visible : EVisibility::Collapsed; })
						.ButtonStyle(&FAppStyle::Get(), "PrimaryButton")
						.HAlign(HAlign_Center)
						.Text(LOCTEXT("SaveChanges", "Save changes"))
						.ToolTipText(LOCTEXT("SaveChangesTip", "Write the changes to the template, or to your General settings."))
						.OnClicked_Lambda([this]() { Save(); return FReply::Handled(); })
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SButton)
						.Visibility_Lambda([this]() { return bDirty ? EVisibility::Collapsed : EVisibility::Visible; })
						.IsEnabled(false)
						.HAlign(HAlign_Center)
						.Text_Lambda([this]() { return IsBuiltIn() ? LOCTEXT("ReadOnly", "Read-only") : LOCTEXT("Saved", "Saved"); })
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Revert", "Revert"))
				.ToolTipText(LOCTEXT("RevertTip", "Throw away the changes since the last save."))
				.IsEnabled_Lambda([this]() { return bDirty; })
				.OnClicked_Lambda([this]() { Load(Current); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Visibility_Lambda([this]() { return IsGeneral() ? EVisibility::Collapsed : EVisibility::Visible; })
				.Text(LOCTEXT("UseTemplate", "Use for import"))
				.ToolTipText(LOCTEXT("UseTemplateTip", "Choose this template on the Import page and go there."))
				.OnClicked_Lambda([this]()
				{
					if (Current.IsValid() && !Current->bGeneral && ConfirmLeave())
					{
						OnUseTemplate.ExecuteIfBound(Current->Template.Name);
					}
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SMCSettingsPage::GetInfoText)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Visibility_Lambda([this]() { return IsBuiltIn() ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text(LOCTEXT("DuplicateToEdit", "Duplicate to edit..."))
				.OnClicked_Lambda([this]() { OnDuplicate(); return FReply::Handled(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			BuildProjectRows()
		];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(MCFields::Gap, 0.f, 0.f, MCFields::Gap)
		[
			MCFields::Panel(Header, FMargin(12.f, 10.f))
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(MCFields::Gap, 0.f, 0.f, 0.f)
		[
			DetailsView.ToSharedRef()
		];
}

TSharedRef<SWidget> SMCSettingsPage::BuildProjectRows()
{
	return SNew(SVerticalBox)
		.Visibility_Lambda([this]() { return IsProject() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				MCFields::Label(LOCTEXT("DescriptionLabel", "Description"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(WorkingDescription); })
				.HintText(LOCTEXT("DescriptionHint", "What kind of model this template is for. Shown on the Import page."))
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					WorkingDescription = Text.ToString();
					UpdateDirty();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(MCFields::LabelWidth)
				[
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("SetsDestinationTip", "Choosing this template also sets the destination on the Import page."))
					.IsChecked_Lambda([this]() { return bWorkingSetsDestination ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
					{
						bWorkingSetsDestination = State == ECheckBoxState::Checked;
						if (bWorkingSetsDestination && WorkingDestination.IsEmpty())
						{
							WorkingDestination = ImportPageSettings().DestinationFolder.Path;
						}
						UpdateDirty();
					})
					[
						SNew(STextBlock).Text(LOCTEXT("DestinationLabel", "Destination"))
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.IsEnabled_Lambda([this]() { return bWorkingSetsDestination; })
				.Text_Lambda([this]() { return FText::FromString(WorkingDestination); })
				.HintText(LOCTEXT("DestinationHint", "Off: the template keeps the destination set on the Import page"))
				.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
				{
					WorkingDestination = MCModels::NormaliseContentPath(Text.ToString());
					UpdateDirty();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.IsEnabled_Lambda([this]() { return bWorkingSetsDestination; })
				.Text(LOCTEXT("BrowseButton", "..."))
				.OnClicked_Lambda([this]()
				{
					const FString Picked = MCDialogs::PickContentFolder(WorkingDestination);
					if (!Picked.IsEmpty())
					{
						WorkingDestination = Picked;
						UpdateDirty();
					}
					return FReply::Handled();
				})
			]
		];
}

TSharedRef<SWidget> SMCSettingsPage::MakeMoreMenu()
{
	FMenuBuilder Menu(/*bInShouldCloseWindowAfterMenuSelection*/ true, nullptr);
	auto IfProject = FCanExecuteAction::CreateLambda([this]() { return IsProject(); });
	Menu.AddMenuEntry(LOCTEXT("RenameTemplate", "Rename..."), LOCTEXT("RenameTemplateTip", "Rename this project template."), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SMCSettingsPage::OnRename), IfProject));
	Menu.AddMenuEntry(LOCTEXT("DeleteTemplate", "Delete..."), LOCTEXT("DeleteTemplateTip", "Delete this project template. Models imported with it keep their settings."), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SMCSettingsPage::OnDelete), IfProject));
	Menu.AddSeparator();
	Menu.AddMenuEntry(LOCTEXT("OpenTemplateFolder", "Show templates folder"), LOCTEXT("OpenTemplateFolderTip", "Open Config/ModelConstructor/Templates. Copy files here to share templates between projects."), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			const FString Folder = MCTemplates::Folder();
			IFileManager::Get().MakeDirectory(*Folder, /*Tree*/ true);
			FPlatformProcess::ExploreFolder(*Folder);
		})));
	Menu.AddMenuEntry(LOCTEXT("ReloadTemplates", "Reload templates"), LOCTEXT("ReloadTemplatesTip", "Read the templates folder again, for files added outside the editor."), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			RefreshTemplates();
			OnTemplatesChanged.ExecuteIfBound();
		})));
	return Menu.MakeWidget();
}

TSharedRef<ITableRow> SMCSettingsPage::OnGenerateRow(FItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	FText KindText;
	if (Item->bGeneral)
	{
		KindText = LOCTEXT("GeneralTag", "every import");
	}
	else
	{
		KindText = Item->Template.bBuiltIn ? LOCTEXT("BuiltInTag", "built-in") : LOCTEXT("ProjectTag", "project");
	}
	const FText Name = Item->bGeneral ? LOCTEXT("GeneralName", "General") : FText::FromString(Item->Template.Name);
	const FString TemplateName = Item->bGeneral ? FString() : Item->Template.Name;

	return SNew(STableRow<FItemPtr>, OwnerTable)
		.Padding(FMargin(6.f, 4.f))
		.ToolTipText(Item->bGeneral ? LOCTEXT("GeneralTip", "Folder, level and run options used for every import. Saved for you in this project.") : FText::FromString(Item->Template.Description))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Name)
				.Font(Item->bGeneral ? MCFields::SectionFont() : FAppStyle::GetFontStyle("NormalFont"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Visibility_Lambda([TemplateName]()
				{
					return !TemplateName.IsEmpty() && ImportPageSettings().TemplateName.Equals(TemplateName, ESearchCase::IgnoreCase) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.Text(LOCTEXT("InUseTag", "on Import page"))
				.ColorAndOpacity(FStyleColors::AccentBlue)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(KindText).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
}

void SMCSettingsPage::OnSelectionChanged(FItemPtr Item, ESelectInfo::Type SelectInfo)
{
	if (bSelectingInCode)
	{
		return;
	}
	if (!Item.IsValid() || Item == Current)
	{
		// Keep something selected, so the right side always describes the list.
		if (Current.IsValid())
		{
			TGuardValue<bool> Guard(bSelectingInCode, true);
			List->SetSelection(Current, ESelectInfo::Direct);
		}
		return;
	}
	const bool bGeneral = Item->bGeneral;
	const FString Name = Item->Template.Name;
	if (!ConfirmLeave())
	{
		TGuardValue<bool> Guard(bSelectingInCode, true);
		List->SetSelection(Current, ESelectInfo::Direct);
		return;
	}
	// Saving reads the templates again, which replaces the items, so the choice is found again by name.
	const FItemPtr Fresh = bGeneral ? Items[0] : Find(Name);
	if (Fresh.IsValid())
	{
		Select(Fresh);
	}
}

void SMCSettingsPage::Select(const FItemPtr& Item)
{
	Current = Item;
	if (List.IsValid())
	{
		TGuardValue<bool> Guard(bSelectingInCode, true);
		List->SetSelection(Item, ESelectInfo::Direct);
		List->RequestScrollIntoView(Item);
	}
	Load(Current);
}

void SMCSettingsPage::SelectTemplate(const FString& Name)
{
	RefreshTemplates();
	const FItemPtr Target = Find(Name);
	if (!Target.IsValid() || Target == Current || !ConfirmLeave())
	{
		return;
	}
	if (const FItemPtr Item = Find(Name))
	{
		Select(Item);
	}
}

SMCSettingsPage::FItemPtr SMCSettingsPage::Find(const FString& Name) const
{
	const FItemPtr* Found = Items.FindByPredicate([&Name](const FItemPtr& Item)
	{
		return !Item->bGeneral && Item->Template.Name.Equals(Name, ESearchCase::IgnoreCase);
	});
	return Found ? *Found : FItemPtr();
}

void SMCSettingsPage::RefreshTemplates()
{
	const bool bWasGeneral = IsGeneral();
	const FString CurrentName = Current.IsValid() && !Current->bGeneral ? Current->Template.Name : FString();

	Items.Reset();
	const FItemPtr General = MakeShared<FItem>();
	General->bGeneral = true;
	Items.Add(General);
	for (MCTemplates::FTemplate& Template : MCTemplates::List())
	{
		const FItemPtr Item = MakeShared<FItem>();
		Item->Template = MoveTemp(Template);
		Items.Add(Item);
	}
	if (List.IsValid())
	{
		List->RequestListRefresh();
	}

	// The new items replace the old ones; the edited copy stays as it is, so unsaved changes survive a reload.
	FItemPtr Match = bWasGeneral ? General : Find(CurrentName);
	if (Current.IsValid() && Match.IsValid())
	{
		Current = Match;
		TGuardValue<bool> Guard(bSelectingInCode, true);
		List->SetSelection(Current, ESelectInfo::Direct);
	}
	else if (Current.IsValid())
	{
		bDirty = false;
		Select(General);
	}
}

void SMCSettingsPage::Load(const FItemPtr& Item)
{
	if (!Item.IsValid())
	{
		return;
	}
	Working->CopySettingsFrom(ImportPageSettings());
	WorkingDescription.Reset();
	WorkingDestination.Reset();
	bWorkingSetsDestination = false;
	if (!Item->bGeneral)
	{
		MCModels::ImportSettings(Item->Template.Settings, *Working);
		WorkingDescription = Item->Template.Description;
		WorkingDestination = Item->Template.Destination;
		bWorkingSetsDestination = !WorkingDestination.IsEmpty();
	}
	SavedState = State();
	bDirty = false;
	DetailsView->ForceRefresh();
}

FString SMCSettingsPage::State() const
{
	if (!Current.IsValid())
	{
		return FString();
	}
	if (Current->bGeneral)
	{
		return MCTemplates::ExportGeneralSettings(*Working);
	}
	return MCTemplates::ExportTemplateSettings(*Working)
		+ TEXT("\nDescription=") + WorkingDescription.TrimStartAndEnd()
		+ TEXT("\nDestination=") + (bWorkingSetsDestination ? WorkingDestination : FString());
}

void SMCSettingsPage::UpdateDirty()
{
	bDirty = IsEditable() && State() != SavedState;
}

EActiveTimerReturnType SMCSettingsPage::OnTimer(double InCurrentTime, float InDeltaTime)
{
	UpdateDirty();
	return EActiveTimerReturnType::Continue;
}

FText SMCSettingsPage::GetTitleText() const
{
	if (!Current.IsValid())
	{
		return FText::GetEmpty();
	}
	return Current->bGeneral ? LOCTEXT("GeneralTitle", "General") : FText::FromString(Current->Template.Name);
}

FText SMCSettingsPage::GetInfoText() const
{
	if (IsGeneral())
	{
		return LOCTEXT("GeneralInfo", "Used for every import and re-import, whichever template is chosen. Saved for you in this project, so other people on the team keep their own.");
	}
	if (IsBuiltIn())
	{
		const FString& Description = Current->Template.Description;
		return FText::Format(LOCTEXT("BuiltInInfo", "{0} Built-in templates can't be changed."), FText::FromString(Description));
	}
	if (IsProject())
	{
		return FText::Format(LOCTEXT("ProjectInfo", "Project template, saved in {0}"), FText::FromString(FPaths::ConvertRelativePathToFull(Current->Template.File)));
	}
	return FText::GetEmpty();
}

bool SMCSettingsPage::ConfirmLeave()
{
	UpdateDirty();
	if (!bDirty)
	{
		return true;
	}
	const FText Message = FText::Format(LOCTEXT("SaveBeforeLeaving", "Save the changes to {0}?\n\nYes saves them, No throws them away, Cancel goes back to editing."), GetTitleText());
	switch (FMessageDialog::Open(EAppMsgType::YesNoCancel, Message))
	{
	case EAppReturnType::Yes:
		return Save();
	case EAppReturnType::No:
		Load(Current);
		return true;
	default:
		return false;
	}
}

bool SMCSettingsPage::Save()
{
	if (!IsEditable())
	{
		return false;
	}
	UMCSettings& Target = ImportPageSettings();
	if (Current->bGeneral)
	{
		MCTemplates::CopyGeneral(*Working, Target);
		Target.SaveConfig();
		SavedState = State();
		bDirty = false;
		return true;
	}

	const FString Name = Current->Template.Name;
	// The Import page follows the template only while it still matches it, so older unsaved settings are not overwritten.
	const bool bImportPageFollows = Target.TemplateName.Equals(Name, ESearchCase::IgnoreCase)
		&& MCTemplates::ExportTemplateSettings(Target) == Current->Template.Settings;

	Working->DestinationFolder.Path = WorkingDestination;
	FString Error;
	if (!MCTemplates::Save(Name, WorkingDescription, bWorkingSetsDestination && !WorkingDestination.IsEmpty(), *Working, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
		return false;
	}
	if (bImportPageFollows)
	{
		MCModels::ImportSettings(MCTemplates::ExportTemplateSettings(*Working), Target);
		Target.SaveConfig();
	}

	RefreshTemplates();
	SavedState = State();
	bDirty = false;
	OnTemplatesChanged.ExecuteIfBound();
	return true;
}

void SMCSettingsPage::OnNew()
{
	if (!ConfirmLeave())
	{
		return;
	}
	FString Description;
	const FString Name = MCDialogs::AskTemplateName(LOCTEXT("NewTitle", "New template"), LOCTEXT("NewAccept", "Create"), FString(), FString(), &Description);
	if (Name.IsEmpty())
	{
		return;
	}
	TStrongObjectPtr<UMCSettings> Defaults(NewObject<UMCSettings>(GetTransientPackage(), NAME_None, RF_Transient));
	MCTemplates::ApplyFactoryDefaults(*Defaults);
	FString Error;
	if (!MCTemplates::Save(Name, Description, /*bIncludeDestination*/ false, *Defaults, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
		return;
	}
	RefreshTemplates();
	Select(Find(Name));
	OnTemplatesChanged.ExecuteIfBound();
}

void SMCSettingsPage::OnDuplicate()
{
	if (!Current.IsValid() || Current->bGeneral || !ConfirmLeave())
	{
		return;
	}
	const MCTemplates::FTemplate Source = Current->Template;
	FString Description = Source.Description;
	const FString Name = MCDialogs::AskTemplateName(LOCTEXT("DuplicateTitle", "Duplicate template"), LOCTEXT("DuplicateAccept", "Duplicate"),
		Source.Name + TEXT(" copy"), FString(), &Description);
	if (Name.IsEmpty())
	{
		return;
	}
	TStrongObjectPtr<UMCSettings> Copy(NewObject<UMCSettings>(GetTransientPackage(), NAME_None, RF_Transient));
	MCModels::ImportSettings(Source.Settings, *Copy);
	Copy->DestinationFolder.Path = Source.Destination;
	FString Error;
	if (!MCTemplates::Save(Name, Description, !Source.Destination.IsEmpty(), *Copy, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
		return;
	}
	RefreshTemplates();
	Select(Find(Name));
	OnTemplatesChanged.ExecuteIfBound();
}

void SMCSettingsPage::OnRename()
{
	if (!IsProject() || !ConfirmLeave())
	{
		return;
	}
	const FString OldName = Current->Template.Name;
	const FString Name = MCDialogs::AskTemplateName(LOCTEXT("RenameTemplateTitle", "Rename template"), LOCTEXT("RenameTemplateAccept", "Rename"), OldName, OldName, nullptr);
	if (Name.IsEmpty() || Name == OldName)
	{
		return;
	}
	FString Error;
	if (!MCTemplates::Rename(OldName, Name, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
		return;
	}
	UMCSettings& Target = ImportPageSettings();
	if (Target.TemplateName.Equals(OldName, ESearchCase::IgnoreCase))
	{
		Target.TemplateName = Name;
		Target.SaveConfig();
	}
	RefreshTemplates();
	Select(Find(Name));
	OnTemplatesChanged.ExecuteIfBound();
}

void SMCSettingsPage::OnDelete()
{
	if (!IsProject())
	{
		return;
	}
	const FString Name = Current->Template.Name;
	const FText Message = FText::Format(LOCTEXT("DeleteTemplateConfirm", "Delete the template {0}? Models imported with it keep their settings."), FText::FromString(Name));
	if (FMessageDialog::Open(EAppMsgType::OkCancel, Message) != EAppReturnType::Ok)
	{
		return;
	}
	FString Error;
	if (!MCTemplates::Delete(Name, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
		return;
	}
	UMCSettings& Target = ImportPageSettings();
	if (Target.TemplateName.Equals(Name, ESearchCase::IgnoreCase))
	{
		Target.TemplateName.Reset();
		Target.SaveConfig();
	}
	bDirty = false;
	RefreshTemplates();
	OnTemplatesChanged.ExecuteIfBound();
}

#undef LOCTEXT_NAMESPACE
