// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "SMCModelManager.h"
#include "SMCDialogs.h"
#include "SMCFields.h"
#include "SMCReimportPanel.h"
#include "MCLibrary.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

namespace
{
	const FName ColName(TEXT("Name"));
	const FName ColFolder(TEXT("Folder"));
	const FName ColRoot(TEXT("Root"));
	const FName ColId(TEXT("Id"));

	constexpr float ActionWidth = 240.f;
	constexpr float LevelsHeight = 96.f;

	FString FormatTime(const FDateTime& Time)
	{
		return Time.GetTicks() > 0 ? Time.ToString(TEXT("%Y-%m-%d %H:%M UTC")) : FString(TEXT("unknown"));
	}

	bool SourceChanged(const FMCSceneEntry& Entry)
	{
		return Entry.bTimeIsImport && Entry.SourceModified.GetTicks() > 0 && Entry.SourceModified > Entry.Time;
	}

	bool SourceMissing(const FMCSceneEntry& Entry)
	{
		return !Entry.SourceFile.IsEmpty() && Entry.SourceModified.GetTicks() == 0;
	}

	class SMCSceneRow : public SMultiColumnTableRow<TSharedPtr<FMCSceneEntry>>
	{
	public:
		SLATE_BEGIN_ARGS(SMCSceneRow) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedPtr<FMCSceneEntry> InItem)
		{
			Item = InItem;
			SMultiColumnTableRow<TSharedPtr<FMCSceneEntry>>::Construct(FSuperRowType::FArguments().Padding(FMargin(0.f, 2.f)), OwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
		{
			FString Text;
			FSlateColor Color = FSlateColor::UseSubduedForeground();
			if (Column == ColName)
			{
				Text = Item->DisplayName;
				Color = FSlateColor::UseForeground();
			}
			else if (Column == ColFolder)
			{
				Text = Item->Folder;
			}
			else if (Column == ColRoot)
			{
				Text = Item->Root;
			}
			else if (Column == ColId)
			{
				Text = Item->ImportId;
			}
			return SNew(SBox)
				.Padding(FMargin(4.f, 1.f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Text))
					.ColorAndOpacity(Color)
					.ToolTipText(FText::FromString(Text))
				];
		}

	private:
		TSharedPtr<FMCSceneEntry> Item;
	};
}

void SMCModelManager::Construct(const FArguments& InArgs)
{
	Icon = InArgs._Icon;
	OnImportNew = InArgs._OnImportNew;
	OnOpenSettings = InArgs._OnOpenSettings;
	OnOpenGuide = InArgs._OnOpenGuide;
	OnTemplatesChanged = InArgs._OnTemplatesChanged;

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		+ SSplitter::Slot().Value(0.36f)
		[
			BuildSidebar()
		]
		+ SSplitter::Slot().Value(0.64f)
		[
			BuildDetails()
		]
	];
}

TSharedRef<SWidget> SMCModelManager::BuildSidebar()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(MCFields::Gap, MCFields::Gap, 0.f, 0.f)
		[
			BuildTitleCard()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(MCFields::Gap, MCFields::Gap, 0.f, 0.f)
		[
			BuildActions()
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(MCFields::Gap, MCFields::Gap, 0.f, MCFields::Gap)
		[
			BuildModelList()
		];
}

TSharedRef<SWidget> SMCModelManager::BuildTitleCard()
{
	return MCFields::Panel(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top)
		[
			MCFields::PageHeader(LOCTEXT("ManagerTitle", "Model Constructor"),
				LOCTEXT("ManagerIntro", "Imports Datasmith files into their own folder and level, ready for Nanite. Pick a model below to see where it came from, then re-import or remove it. Imported models don't need this plugin, so you can share and package them without it."))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(12.f, 2.f, 0.f, 0.f)
		[
			SNew(SImage)
			.Visibility(Icon ? EVisibility::Visible : EVisibility::Collapsed)
			.Image(Icon)
		],
		FMargin(12.f, 10.f));
}

TSharedRef<SWidget> SMCModelManager::BuildActions()
{
	return MCFields::Panel(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			MCFields::Section(LOCTEXT("ActionsHeading", "Actions"))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 2.f)
		[
			MCFields::IconButton("Icons.Plus", LOCTEXT("ActionImport", "Import a new model..."), LOCTEXT("ActionImportTip", "Go to the Import page."),
				[this]() { OnImportNew.ExecuteIfBound(); }, true, /*bPrimary*/ true, ActionWidth)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 2.f)
		[
			MCFields::IconButton("Icons.Settings", LOCTEXT("ActionSettings", "Edit import settings..."), LOCTEXT("ActionSettingsTip", "Go to the Settings page to view and edit templates."),
				[this]() { OnOpenSettings.ExecuteIfBound(FString()); }, true, /*bPrimary*/ false, ActionWidth)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 2.f)
		[
			MCFields::IconButton("Icons.Help", LOCTEXT("ActionGuide", "Read the guide"), LOCTEXT("ActionGuideTip", "How each page works, what the settings mean, and examples."),
				[this]() { OnOpenGuide.ExecuteIfBound(); }, true, /*bPrimary*/ false, ActionWidth)
		],
		FMargin(12.f, 8.f));
}

TSharedRef<SWidget> SMCModelManager::BuildModelList()
{
	return MCFields::Panel(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				MCFields::Section(LOCTEXT("ModelsHeading", "Models"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SMCModelManager::GetCountText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Search by name, folder or id"))
			.OnTextChanged_Lambda([this](const FText& Text)
			{
				FilterText = Text.ToString();
				ApplyFilter();
			})
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SAssignNew(List, SListView<FEntry>)
			.ListItemsSource(&FilteredItems)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow_Lambda([](FEntry Item, const TSharedRef<STableViewBase>& OwnerTable) -> TSharedRef<ITableRow>
			{
				return SNew(SMCSceneRow, OwnerTable, Item);
			})
			.OnSelectionChanged(this, &SMCModelManager::OnSelectionChanged)
			.HeaderRow
			(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(ColName).DefaultLabel(LOCTEXT("ColName", "Name")).FillWidth(0.34f)
				+ SHeaderRow::Column(ColFolder).DefaultLabel(LOCTEXT("ColFolder", "Folder")).FillWidth(0.42f)
				+ SHeaderRow::Column(ColRoot).DefaultLabel(LOCTEXT("ColRoot", "Root")).FillWidth(0.12f)
				+ SHeaderRow::Column(ColId).DefaultLabel(LOCTEXT("ColId", "Id")).FillWidth(0.12f)
			)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("IncludePluginsTip", "Also list scenes in project and marketplace content plugins. Engine plugins are never listed."))
				.IsChecked_Lambda([this]() { return bIncludePlugins ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					bIncludePlugins = State == ECheckBoxState::Checked;
					Refresh();
				})
				[
					SNew(STextBlock).Text(LOCTEXT("IncludePlugins", "Include plugins"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "Refresh"))
				.OnClicked_Lambda([this]()
				{
					Refresh();
					return FReply::Handled();
				})
			]
		],
		FMargin(8.f));
}

TSharedRef<SWidget> SMCModelManager::BuildDetails()
{
	auto HasSelection = [this]() { return Selected().IsValid() ? EVisibility::Visible : EVisibility::Collapsed; };

	const TSharedRef<SWidget> Header = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Font(MCFields::HeadingFont())
			.Text_Lambda([this]() { const FEntry Item = Selected(); return Item.IsValid() ? FText::FromString(Item->DisplayName) : FText::GetEmpty(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text_Lambda([this]() { const FEntry Item = Selected(); return Item.IsValid() ? FText::FromString(Item->Folder) : FText::GetEmpty(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(MCDialogs::WarningColor())
			.Text(this, &SMCModelManager::GetStatusText)
			.Visibility_Lambda([this]() { return GetStatusText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			BuildButtonRow()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
		[
			BuildDetailRows()
		];

	// The column scrolls as a whole, so the re-import section and its results are never squeezed off the bottom.
	return SAssignNew(DetailsScroll, SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(MCFields::Gap, MCFields::Gap, MCFields::Gap, 0.f)
			[
				MCFields::Panel(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Visibility_Lambda([this]() { return Selected().IsValid() ? EVisibility::Collapsed : EVisibility::Visible; })
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text_Lambda([this]()
						{
							return AllItems.Num() > 0 ? LOCTEXT("NoSelection", "Select a model in the list to see its details.")
								: LOCTEXT("NoModels", "No models yet. Use Import a new model to bring in a Datasmith file.");
						})
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox)
						.Visibility_Lambda(HasSelection)
						[
							Header
						]
					],
					FMargin(14.f, 12.f))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(MCFields::Gap, MCFields::Gap, MCFields::Gap, 0.f)
			[
				SNew(SBox)
				.Visibility_Lambda(HasSelection)
				[
					BuildLevels()
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(MCFields::Gap)
			[
				SAssignNew(ReimportPanel, SMCReimportPanel)
				.Visibility_Lambda([this]() { return bReimportOpen && Selected().IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
				.OnClose_Lambda([this]() { bReimportOpen = false; })
				.OnReimported_Lambda([this]()
				{
					const FEntry Item = Selected();
					const FString ScenePath = Item.IsValid() ? Item->ScenePath : FString();
					Refresh();
					SelectScene(ScenePath);
					if (DetailsScroll.IsValid())
					{
						DetailsScroll->ScrollToEnd();
					}
				})
			]
		];
}

TSharedRef<SWidget> SMCModelManager::BuildButtonRow()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get(), "PrimaryButton")
			.ContentPadding(FMargin(16.f, 4.f))
			.Text(LOCTEXT("ReimportOpen", "Re-import..."))
			.ToolTipText(LOCTEXT("ReimportOpenTip", "Open the re-import section below: choose the source and settings, then run it."))
			.IsEnabled_Lambda([this]() { return CanReimport(); })
			.OnClicked_Lambda([this]() { OpenReimport(); return FReply::Handled(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ShowButton", "Show in Content Browser"))
			.OnClicked_Lambda([this]() { OnShowInContentBrowser(); return FReply::Handled(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("RemoveButton", "Remove..."))
			.ToolTipText(LOCTEXT("RemoveTip", "Delete this model's folder and the assets its import created. Shows what will be deleted first. Only for models imported with this tool."))
			.IsEnabled_Lambda([this]() { return CanRemove(); })
			.OnClicked_Lambda([this]() { OnRemove(); return FReply::Handled(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.HasDownArrow(true)
			.ToolTipText(LOCTEXT("ManagerMoreTip", "Copy paths, rename the model, or save its settings as a template."))
			.ButtonContent()
			[
				SNew(STextBlock).Text(LOCTEXT("ManagerMore", "More"))
			]
			.OnGetMenuContent(this, &SMCModelManager::MakeMoreMenu)
		];
}

TSharedRef<SWidget> SMCModelManager::BuildDetailRows()
{
	auto Value = [this](TFunction<FString(const FMCSceneEntry&)> Get)
	{
		return TAttribute<FText>::CreateLambda([this, Get]() { const FEntry Item = Selected(); return Item.IsValid() ? FText::FromString(Get(*Item)) : FText::GetEmpty(); });
	};
	auto Orange = [this](TFunction<bool(const FMCSceneEntry&)> Test)
	{
		return TAttribute<FSlateColor>::CreateLambda([this, Test]()
		{
			const FEntry Item = Selected();
			return Item.IsValid() && Test(*Item) ? MCDialogs::WarningColor() : FSlateColor::UseForeground();
		});
	};

	const TSharedRef<SWidget> ModelRows = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[
			MCFields::Section(LOCTEXT("ModelRowsHeading", "Model"))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MCFields::ValueRow(LOCTEXT("InfoLevel", "Level"), Value([](const FMCSceneEntry& E)
			{
				if (E.LevelPath.IsEmpty())
				{
					return FString();
				}
				return E.bCreatedLevel ? E.LevelPath : E.LevelPath + TEXT("   (an existing level)");
			}))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MCFields::ValueRow(LOCTEXT("InfoScene", "Scene asset"), Value([](const FMCSceneEntry& E) { return E.ScenePath; }))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MCFields::ValueRow(LOCTEXT("InfoTemplate", "Template"), Value([](const FMCSceneEntry& E) { return E.TemplateName; }))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MCFields::ValueRow(LOCTEXT("InfoRoot", "Root"), Value([](const FMCSceneEntry& E) { return E.Root; }))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MCFields::ValueRow(LOCTEXT("InfoId", "Import id"), Value([](const FMCSceneEntry& E) { return E.ImportId; }))
		];

	const TSharedRef<SWidget> SourceRows = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[
			MCFields::Section(LOCTEXT("SourceRowsHeading", "Source"))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MCFields::ValueRow(LOCTEXT("InfoSource", "File"),
				Value([](const FMCSceneEntry& E) { return E.SourceFile.IsEmpty() ? FString(TEXT("not recorded")) : E.SourceFile; }),
				Orange([](const FMCSceneEntry& E) { return SourceMissing(E); }))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MCFields::ValueRow(LOCTEXT("InfoImported", "Imported"),
				Value([](const FMCSceneEntry& E) { return E.bTimeIsImport ? FormatTime(E.Time) : FString(); }))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MCFields::ValueRow(LOCTEXT("InfoSaved", "Last saved"),
				Value([](const FMCSceneEntry& E) { return E.bTimeIsImport ? FString() : FormatTime(E.Time); }))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MCFields::ValueRow(LOCTEXT("InfoModified", "File modified"),
				Value([](const FMCSceneEntry& E) { return E.SourceModified.GetTicks() > 0 ? FormatTime(E.SourceModified) : FString(); }),
				Orange([](const FMCSceneEntry& E) { return SourceChanged(E); }))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MCFields::ValueRow(LOCTEXT("InfoSettings", "Settings"),
				Value([](const FMCSceneEntry& E) { return E.ImportId.IsEmpty() || E.bHasSettings ? FString() : FString(TEXT("not recorded (imported with an older version)")); }))
		];

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 16.f, 0.f)
		[
			ModelRows
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SourceRows
		];
}

TSharedRef<SWidget> SMCModelManager::BuildLevels()
{
	return MCFields::Panel(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				MCFields::Section(LOCTEXT("LevelsHeading", "Levels that use this model"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenLevel", "Open level"))
				.ToolTipText(LOCTEXT("OpenLevelTip", "Open the selected level, or the only one listed."))
				.IsEnabled_Lambda([this]() { return LevelItems.Num() == 1 || (LevelList.IsValid() && LevelList->GetNumItemsSelected() > 0); })
				.OnClicked_Lambda([this]() { OnOpenLevel(); return FReply::Handled(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Visibility_Lambda([this]() { return LevelItems.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("NoLevels", "No saved level uses this model."))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return LevelItems.Num() == 0 ? EVisibility::Collapsed : EVisibility::Visible; })
			.MaxDesiredHeight(LevelsHeight)
			[
				SAssignNew(LevelList, SListView<TSharedPtr<FString>>)
				.ListItemsSource(&LevelItems)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow_Lambda([](TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable) -> TSharedRef<ITableRow>
				{
					return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
						.Padding(FMargin(4.f, 1.f))
						[
							SNew(STextBlock).Text(FText::FromString(*Item))
						];
				})
				.OnMouseButtonDoubleClick_Lambda([this](TSharedPtr<FString>) { OnOpenLevel(); })
			]
		],
		FMargin(10.f, 8.f));
}

void SMCModelManager::Refresh()
{
	bStale = false;
	const FEntry Previous = Selected();
	const FString PreviousPath = Previous.IsValid() ? Previous->ScenePath : FString();

	AllItems.Reset();
	for (const FMCSceneEntry& Entry : UMCLibrary::FindScenes(bIncludePlugins))
	{
		AllItems.Add(MakeShared<FMCSceneEntry>(Entry));
	}
	AllItems.Sort([](const FEntry& A, const FEntry& B) { return A->DisplayName < B->DisplayName; });
	ApplyFilter();
	SelectScene(PreviousPath);
}

void SMCModelManager::RefreshIfStale()
{
	if (bStale)
	{
		Refresh();
	}
}

void SMCModelManager::RefreshTemplates()
{
	if (ReimportPanel.IsValid())
	{
		ReimportPanel->RefreshTemplates();
	}
}

void SMCModelManager::SelectScene(const FString& ScenePath)
{
	const FEntry* Match = ScenePath.IsEmpty() ? nullptr
		: FilteredItems.FindByPredicate([&ScenePath](const FEntry& Item) { return Item->ScenePath == ScenePath; });
	if (Match)
	{
		List->SetSelection(*Match);
	}
	else
	{
		List->ClearSelection();
		OnSelectionChanged(nullptr, ESelectInfo::Direct);
	}
}

void SMCModelManager::ApplyFilter()
{
	const FString Needle = FilterText.TrimStartAndEnd();
	FilteredItems.Reset();
	for (const FEntry& Item : AllItems)
	{
		if (Needle.IsEmpty() || Item->DisplayName.Contains(Needle) || Item->Folder.Contains(Needle)
			|| Item->AssetName.Contains(Needle) || Item->ImportId.Contains(Needle))
		{
			FilteredItems.Add(Item);
		}
	}
	if (List.IsValid())
	{
		List->RequestListRefresh();
	}
}

void SMCModelManager::OnSelectionChanged(FEntry Item, ESelectInfo::Type SelectInfo)
{
	LevelItems.Reset();
	if (Item.IsValid())
	{
		for (const FString& Level : Item->Levels)
		{
			LevelItems.Add(MakeShared<FString>(Level));
		}
	}
	if (LevelList.IsValid())
	{
		LevelList->RequestListRefresh();
	}
	// The section only follows the selection while open, because loading the settings record reads the scene asset.
	if (bReimportOpen && ReimportPanel.IsValid())
	{
		ReimportPanel->SetModel(Item);
	}
}

SMCModelManager::FEntry SMCModelManager::Selected() const
{
	if (!List.IsValid())
	{
		return nullptr;
	}
	const TArray<FEntry> Items = List->GetSelectedItems();
	return Items.Num() > 0 ? Items[0] : nullptr;
}

FText SMCModelManager::GetCountText() const
{
	if (FilteredItems.Num() == AllItems.Num())
	{
		return FText::Format(LOCTEXT("CountAll", "{0} scenes"), FText::AsNumber(AllItems.Num()));
	}
	return FText::Format(LOCTEXT("CountFiltered", "{0} of {1} scenes"), FText::AsNumber(FilteredItems.Num()), FText::AsNumber(AllItems.Num()));
}

FText SMCModelManager::GetStatusText() const
{
	const FEntry Item = Selected();
	if (!Item.IsValid())
	{
		return FText::GetEmpty();
	}
	if (Item->ImportId.IsEmpty())
	{
		return LOCTEXT("StatusNotOurs", "Not imported with Model Constructor, so it can't be re-imported or removed here.");
	}
	TArray<FText> Lines;
	if (!Item->bCreatedLevel)
	{
		Lines.Add(LOCTEXT("StatusNoOwnLevel", "Imported into an existing level, so it can't be re-imported. Remove it and import it again instead."));
	}
	if (SourceMissing(*Item))
	{
		Lines.Add(LOCTEXT("StatusSourceMissing", "The source file is not where it was. Choose its new place when you re-import."));
	}
	else if (SourceChanged(*Item))
	{
		Lines.Add(LOCTEXT("StatusSourceChanged", "The source file has changed since this import."));
	}
	return FText::Join(FText::FromString(TEXT("\n")), Lines);
}

bool SMCModelManager::CanReimport() const
{
	const FEntry Item = Selected();
	return Item.IsValid() && !Item->ImportId.IsEmpty() && Item->bCreatedLevel;
}

bool SMCModelManager::CanRemove() const
{
	const FEntry Item = Selected();
	return Item.IsValid() && !Item->ImportId.IsEmpty();
}

void SMCModelManager::OpenReimport()
{
	bReimportOpen = true;
	ReimportPanel->RefreshTemplates();
	ReimportPanel->SetModel(Selected());
	if (DetailsScroll.IsValid())
	{
		DetailsScroll->ScrollDescendantIntoView(ReimportPanel, true, EDescendantScrollDestination::TopOrLeft);
	}
}

#undef LOCTEXT_NAMESPACE
