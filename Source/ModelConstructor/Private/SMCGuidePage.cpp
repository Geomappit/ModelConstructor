// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "SMCGuidePage.h"
#include "MCLogos.h"
#include "MCSettings.h"
#include "MCTemplates.h"
#include "UObject/UnrealType.h"
#include "HAL/PlatformProcess.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

namespace
{
	const FString DeveloperUrl(TEXT("https://geomappit.com/"));
	const FString ScheduleSceneUrl(TEXT("https://graphl.tech/twin-sync-studio/"));

	constexpr float ContentMaxWidth = 860.f;
	constexpr float TermWidth = 230.f;

	FSlateFontInfo TitleFont() { return FCoreStyle::GetDefaultFontStyle("Bold", 18); }
	FSlateFontInfo HeadingFont() { return FCoreStyle::GetDefaultFontStyle("Bold", 14); }
	FSlateFontInfo SubHeadingFont() { return FCoreStyle::GetDefaultFontStyle("Bold", 11); }
	FSlateFontInfo BodyFont() { return FCoreStyle::GetDefaultFontStyle("Regular", 10); }
	FSlateFontInfo BodyBoldFont() { return FCoreStyle::GetDefaultFontStyle("Bold", 10); }

	TSharedRef<SWidget> Para(const FText& Text)
	{
		return SNew(STextBlock).Text(Text).Font(BodyFont()).AutoWrapText(true);
	}

	TSharedRef<SWidget> Subdued(const FText& Text)
	{
		return SNew(STextBlock).Text(Text).Font(BodyFont()).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground());
	}

	TSharedRef<SWidget> SubHeading(const FText& Text)
	{
		return SNew(STextBlock).Text(Text).Font(SubHeadingFont());
	}

	/** A list with a marker in front of each line: numbers for steps, a dot for points. */
	TSharedRef<SWidget> List(const TArray<FText>& Lines, bool bNumbered)
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			const FText Marker = bNumbered ? FText::Format(LOCTEXT("GuideStepNumber", "{0}."), FText::AsNumber(Index + 1)) : FText::FromString(TEXT("\u2022"));
			Box->AddSlot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SBox)
					.WidthOverride(22.f)
					[
						SNew(STextBlock).Text(Marker).Font(BodyBoldFont())
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					Para(Lines[Index])
				]
			];
		}
		return Box;
	}

	/** One row of a two-column list. */
	struct FTerm
	{
		FText Name;
		FText Body;
	};

	/** Two columns: a name on the left and what it means on the right. */
	TSharedRef<SWidget> Terms(const TArray<FTerm>& Rows)
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		for (const FTerm& Row : Rows)
		{
			Box->AddSlot().AutoHeight().Padding(8.f, 3.f, 0.f, 3.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
				[
					SNew(SBox)
					.WidthOverride(TermWidth)
					[
						SNew(STextBlock).Text(Row.Name).Font(BodyBoldFont()).AutoWrapText(true)
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					Para(Row.Body)
				]
			];
		}
		return Box;
	}

	/** A framed block for an example or a tip. */
	TSharedRef<SWidget> Card(const FText& Title, const FText& UseWith, const FText& Body)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(12.f, 10.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(Title).Font(SubHeadingFont()).AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 6.f)
				[
					SNew(STextBlock).Text(UseWith).Font(BodyFont()).ColorAndOpacity(FSlateColor::UseSubduedForeground()).AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					Para(Body)
				]
			];
	}
}

void SMCGuidePage::Construct(const FArguments& InArgs)
{
	SAssignNew(Content, SVerticalBox);
	SAssignNew(Contents, SVerticalBox);
	BuildContent();

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		+ SSplitter::Slot().Value(0.24f)
		[
			BuildSidebar()
		]
		+ SSplitter::Slot().Value(0.76f)
		[
			SAssignNew(ContentScroll, SScrollBox)
			+ SScrollBox::Slot().HAlign(HAlign_Left).Padding(FMargin(20.f, 12.f, 20.f, 24.f))
			[
				SNew(SBox)
				.MaxDesiredWidth(ContentMaxWidth)
				[
					Content.ToSharedRef()
				]
			]
		]
	];
}

TSharedRef<SWidget> SMCGuidePage::BuildSidebar()
{
	return SNew(SScrollBox)
		+ SScrollBox::Slot().Padding(FMargin(12.f, 12.f, 12.f, 4.f))
		[
			SNew(STextBlock).Text(LOCTEXT("GuideContents", "Contents")).Font(SubHeadingFont())
		]
		+ SScrollBox::Slot().Padding(FMargin(4.f, 0.f, 12.f, 8.f))
		[
			Contents.ToSharedRef()
		]
		+ SScrollBox::Slot().Padding(FMargin(12.f, 4.f))
		[
			SNew(SSeparator)
		]
		+ SScrollBox::Slot().Padding(FMargin(12.f, 8.f, 12.f, 8.f))
		[
			SNew(STextBlock).Text(LOCTEXT("GuideLinks", "Links")).Font(SubHeadingFont())
		]
		+ SScrollBox::Slot().Padding(FMargin(12.f, 0.f, 12.f, 12.f))
		[
			MakeLinkCard(LOCTEXT("LinkDeveloper", "GeoMappit"),
				LOCTEXT("LinkDeveloperBody", "Developer of Model Constructor. See other Unreal plugins and digital twin work."),
				DeveloperUrl, MCLogos::ELogo::Developer)
		]
		+ SScrollBox::Slot().Padding(FMargin(12.f, 0.f, 12.f, 12.f))
		[
			MakeLinkCard(LOCTEXT("LinkScheduleScenes", "Twin Sync Studio"),
				LOCTEXT("LinkScheduleScenesBody", "Brings Synchro and iTwin scenes into Unreal. Model Constructor supports its models through the Twin Sync Model templates."),
				ScheduleSceneUrl, MCLogos::ELogo::ScheduleScenes)
		];
}

TSharedRef<SWidget> SMCGuidePage::MakeLinkCard(const FText& Label, const FText& Description, const FString& Url, MCLogos::ELogo LogoId) const
{
	const FText Tip = FText::Format(LOCTEXT("LinkTip", "Opens {0} in your web browser."), FText::FromString(Url));
	auto Open = [Url]()
	{
		FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
		return FReply::Handled();
	};
	const FSlateBrush* Logo = MCLogos::Get(LogoId);

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(10.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(SButton)
					.Visibility(Logo ? EVisibility::Visible : EVisibility::Collapsed)
					.ButtonStyle(&FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(0.f))
					.ToolTipText(Tip)
					.OnClicked_Lambda(Open)
					[
						// The data is twice the drawn size, so it stays sharp at high DPI.
						SNew(SBox)
						.WidthOverride(40.f)
						.HeightOverride(40.f)
						[
							SNew(SImage).Image(Logo)
						]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&FAppStyle::Get(), "PrimaryButton")
					.HAlign(HAlign_Center)
					.ContentPadding(FMargin(12.f, 6.f))
					.ToolTipText(Tip)
					.OnClicked_Lambda(Open)
					[
						SNew(STextBlock).Text(Label).Font(SubHeadingFont())
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 4.f)
			[
				Para(Description)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Url))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
}

void SMCGuidePage::AddSection(const FText& Title)
{
	TSharedRef<SWidget> Heading = SNew(STextBlock).Text(Title).Font(HeadingFont());
	Content->AddSlot().AutoHeight().Padding(0.f, Content->NumSlots() == 0 ? 0.f : 26.f, 0.f, 4.f)
	[
		Heading
	];
	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
	[
		SNew(SSeparator)
	];

	const TWeakPtr<SWidget> WeakHeading = Heading;
	Contents->AddSlot().AutoHeight()
	[
		SNew(SButton)
		.ButtonStyle(&FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(8.f, 3.f))
		.OnClicked_Lambda([this, WeakHeading]()
		{
			if (ContentScroll.IsValid() && WeakHeading.IsValid())
			{
				ContentScroll->ScrollDescendantIntoView(WeakHeading.Pin(), true, EDescendantScrollDestination::TopOrLeft);
			}
			return FReply::Handled();
		})
		[
			SNew(STextBlock).Text(Title).Font(BodyFont())
		]
	];
}

void SMCGuidePage::BuildContent()
{
	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
	[
		SNew(STextBlock).Text(LOCTEXT("GuideTitle", "Model Constructor guide")).Font(TitleFont())
	];
	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 18.f)
	[
		Subdued(LOCTEXT("GuideIntro", "Model Constructor imports a Datasmith file into its own folder and level, prepares it for Nanite, and lets you re-import or remove it later. Imported models don't need this plugin, so you can share and package them without it."))
	];

	AddStartSection();
	AddManagerSection();
	AddImportSection();
	AddSettingsPageSection();
	AddReimportSection();
	AddRemoveSection();
	AddSettingsSection();
	AddExamplesSection();
}

void SMCGuidePage::AddStartSection()
{
	AddSection(LOCTEXT("GuideStart", "Quick start"));
	Content->AddSlot().AutoHeight()
	[
		List({
			LOCTEXT("Start1", "Open the Import page and choose a template that matches your model. If you're not sure, start with Datasmith Model. The line under it says what the template does."),
			LOCTEXT("Start2", "Pick the .udatasmith file under Source file, and the content folder for your models under Destination."),
			LOCTEXT("Start3", "Check the line above the Import button. It shows the folder the model will go into, or why it can't import yet."),
			LOCTEXT("Start4", "Press Import. A progress window shows each stage. When it's done, the table lists what each stage did, with any warnings under it."),
			LOCTEXT("Start5", "Find the model later in Model Manager, where you can open its level, re-import it or remove it."),
		}, true)
	];
}

void SMCGuidePage::AddManagerSection()
{
	AddSection(LOCTEXT("GuideManager", "Model Manager"));
	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
	[
		Para(LOCTEXT("ManagerIntroGuide", "Lists every Datasmith scene in the project. Select one to see its folder, level, source file and the template it was imported with. Orange notes say when the source file has changed or moved since the import. Tick Include plugins to list scenes in content plugins too. Drag the bar between the two sides to resize them."))
	];
	Content->AddSlot().AutoHeight()
	[
		Terms({
			{ LOCTEXT("MgReimport", "Re-import"), LOCTEXT("MgReimportBody", "Opens the re-import section under the details. See Re-import below.") },
			{ LOCTEXT("MgShow", "Show in Content Browser"), LOCTEXT("MgShowBody", "Opens the model folder.") },
			{ LOCTEXT("MgRemove", "Remove"), LOCTEXT("MgRemoveBody", "Deletes the model. See Remove below.") },
			{ LOCTEXT("MgCopy", "More: Copy scene path, Copy source path"), LOCTEXT("MgCopyBody", "Copies the scene asset path, or the path of the file the model came from.") },
			{ LOCTEXT("MgRename", "More: Rename"), LOCTEXT("MgRenameBody", "Changes the name shown for the model. No asset or folder is renamed.") },
			{ LOCTEXT("MgSaveTemplate", "More: Save import settings as template"), LOCTEXT("MgSaveTemplateBody", "Makes a project template from the settings the model was imported with, and opens it on the Settings page. Use it to change one setting for a re-import.") },
			{ LOCTEXT("MgOpen", "Open level"), LOCTEXT("MgOpenBody", "Opens the selected level that uses the model. Double-click a level to open it.") },
		})
	];
}

void SMCGuidePage::AddImportSection()
{
	AddSection(LOCTEXT("GuideImport", "Import page"));
	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
	[
		Para(LOCTEXT("ImportIntro", "The Model box at the top says what to import, where, and with which template. The run and its results are below it."))
	];
	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
	[
		Terms({
			{ LOCTEXT("ImpTemplate", "Template"), LOCTEXT("ImpTemplateBody", "Sets how the model is processed. The line under it sums up what it does. Edit opens it on the Settings page. Every import uses the template as it is saved there.") },
			{ LOCTEXT("ImpSource", "Source file"), LOCTEXT("ImpSourceBody", "The .udatasmith file exported from your design tool.") },
			{ LOCTEXT("ImpDestination", "Destination"), LOCTEXT("ImpDestinationBody", "The content folder that holds your models, for example /Game/Models. Each model gets its own folder inside it. Some templates set it for you.") },
			{ LOCTEXT("ImpName", "Model name"), LOCTEXT("ImpNameBody", "Tick it to use your own name instead of the file name. Spaces and symbols become underscores in the folder name. Model Manager shows the name as you typed it.") },
			{ LOCTEXT("ImpRun", "Import"), LOCTEXT("ImpRunBody", "The line above it shows the folder the model goes into, or why it can't import yet. A progress window shows each stage and has a Cancel button.") },
			{ LOCTEXT("ImpResult", "Results"), LOCTEXT("ImpResultBody", "One row per stage: what changed, what was already correct, and how long it took. Warnings list anything worth checking, such as meshes over the collision limit.") },
		})
	];
	Content->AddSlot().AutoHeight()
	[
		Card(LOCTEXT("ImportTipTitle", "Tip"), LOCTEXT("ImportTipTag", "Keep the Output Log open"),
			LOCTEXT("ImportTipBody", "Every stage and save batch is written to the Output Log. If a large import stops, the last lines show where. If the editor closes during a run, the Import page tells you which model and stage it was on the next time you open it."))
	];
}

void SMCGuidePage::AddSettingsPageSection()
{
	AddSection(LOCTEXT("GuideSettingsPage", "Settings page and templates"));
	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
	[
		Para(LOCTEXT("SettingsPageIntro", "A template holds the import, Nanite, collision, texture and material settings, and can also set the destination. The Settings page lists the templates on the left and shows the selected one on the right, with a short note beside each setting. Hover over a setting's name for more detail."))
	];
	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
	[
		Terms({
			{ LOCTEXT("TplDatasmith", "Datasmith Model"), LOCTEXT("TplDatasmithBody", "Any Datasmith model. One collision box per mesh, translucent materials kept as they are, no schedule handling.") },
			{ LOCTEXT("TplDatasmithCollision", "Datasmith Model with Collision"), LOCTEXT("TplDatasmithCollisionBody", "As Datasmith Model, but collision on the real surface instead of boxes, so traces and characters hit walls and floors. Builds slower and uses more memory.") },
			{ LOCTEXT("TplSchedule", "Twin Sync Model"), LOCTEXT("TplScheduleBody", "Models from Twin Sync Studio that play a construction schedule. Translucent materials become masked with a dither so Nanite can draw them, and schedule materials keep showing and hiding elements. One collision box per mesh.") },
			{ LOCTEXT("TplScheduleCollision", "Twin Sync Model with Collision"), LOCTEXT("TplScheduleCollisionBody", "As Twin Sync Model, but collision on the real surface instead of boxes, and CPU access for runtime code that reads vertices. Uses more memory.") },
			{ LOCTEXT("TplGeneral", "General"), LOCTEXT("TplGeneralBody", "Not a template. The folder, level, run and batch options used for every import, whichever template you choose. Saved for you only.") },
		})
	];
	Content->AddSlot().AutoHeight()
	[
		List({
			LOCTEXT("Tpl1", "Built-in templates can't be changed. Select one and press Duplicate to make your own copy."),
			LOCTEXT("Tpl2", "New starts a template from the plugin defaults. More has Rename, Delete, Show templates folder and Reload templates."),
			LOCTEXT("Tpl3", "Changes are kept on a copy until you press Save changes. Revert goes back to the saved version. If you leave with unsaved changes, you're asked first."),
			LOCTEXT("Tpl4", "Tick Destination to make a template import into a set folder. Leave it off to use the folder on the Import page."),
			LOCTEXT("Tpl5", "Use for import chooses the template on the Import page and takes you there."),
			LOCTEXT("Tpl6", "Project templates are text files in Config/ModelConstructor/Templates. Commit that folder so everyone on the project imports the same way, or copy files to another project and press Reload templates."),
		}, false)
	];
}

void SMCGuidePage::AddReimportSection()
{
	AddSection(LOCTEXT("GuideReimport", "Re-import"));
	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
	[
		Para(LOCTEXT("ReimportIntroGuide", "Replaces a model with a fresh import of its source, in the same folder, with the same name and the level at the same path. Use it when the design has changed."))
	];
	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
	[
		List({
			LOCTEXT("Re1", "Select the model in Model Manager and press Re-import. The re-import section opens under the details."),
			LOCTEXT("Re2", "Source file uses the recorded file. Pick another if the file has moved."),
			LOCTEXT("Re3", "Settings uses the ones recorded at the last import. You can choose a template instead. The line under it says what they do."),
			LOCTEXT("Re4", "Press Re-import. A window lists everything that will be replaced. Press Re-import in that window to go ahead, or Cancel. The results appear under the button."),
		}, true)
	];
	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
	[
		SubHeading(LOCTEXT("ReKeptTitle", "What is kept and what is lost"))
	];
	Content->AddSlot().AutoHeight()
	[
		List({
			LOCTEXT("ReKept1", "Kept: the folder, the model name and id, and the level path. Levels that stream the model level keep working."),
			LOCTEXT("ReKept2", "Lost: actors you added to the model level yourself. Put your own actors in a separate level that streams the model level."),
			LOCTEXT("ReKept3", "The old version is fully removed, and the editor is checked, before the new import starts."),
			LOCTEXT("ReKept4", "Only models that Model Constructor imported into their own level can be re-imported."),
		}, false)
	];
}

void SMCGuidePage::AddRemoveSection()
{
	AddSection(LOCTEXT("GuideRemove", "Remove"));
	Content->AddSlot().AutoHeight()
	[
		List({
			LOCTEXT("Rm1", "Deletes the model's folder and only the assets its import made. It only works on models imported with Model Constructor."),
			LOCTEXT("Rm2", "It refuses if anything else is in the folder, or if anything outside the folder uses the model, and lists what is in the way."),
			LOCTEXT("Rm3", "A window lists every package that will be deleted. Nothing is deleted until you press Remove in that window."),
			LOCTEXT("Rm4", "Files are deleted straight from disk, which is fast on large models. With source control on, the engine delete is used instead, which is slower. Deleted files can't be restored from the editor."),
		}, false)
	];
}

void SMCGuidePage::AddSettingsSection()
{
	AddSection(LOCTEXT("GuideSettings", "What the settings mean"));
	Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
	[
		Para(LOCTEXT("SettingsIntro", "The same groups as on the Settings page. Settings marked Advanced there are folded away because you rarely need them."))
	];

	// Read from the settings class, so the guide always matches the Settings page.
	auto AddGroup = [this](MCTemplates::EGroup Group, const FText& Title)
	{
		Content->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 2.f)
		[
			SNew(STextBlock).Text(Title).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
		];
		FString Category;
		TArray<FTerm> Rows;
		auto Flush = [this, &Category, &Rows]()
		{
			if (Rows.Num() > 0)
			{
				Content->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)
				[
					SubHeading(FText::FromString(Category))
				];
				Content->AddSlot().AutoHeight()
				[
					Terms(Rows)
				];
			}
			Rows.Reset();
		};
		for (TFieldIterator<FProperty> It(UMCSettings::StaticClass()); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Edit) || MCTemplates::GroupOf(**It) != Group)
			{
				continue;
			}
			const FString ThisCategory = It->GetMetaData(TEXT("Category"));
			if (ThisCategory != Category)
			{
				Flush();
				Category = ThisCategory;
			}
			Rows.Add({ It->GetDisplayNameText(), It->GetToolTipText() });
		}
		Flush();
	};
	AddGroup(MCTemplates::EGroup::Template, LOCTEXT("SetTemplateGroup", "Template settings"));
	AddGroup(MCTemplates::EGroup::General, LOCTEXT("SetGeneralGroup", "General settings"));
}

void SMCGuidePage::AddExamplesSection()
{
	AddSection(LOCTEXT("GuideExamples", "Examples"));

	auto AddExample = [this](const FText& Title, const FText& UseWith, const FText& Body)
	{
		Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			Card(Title, UseWith, Body)
		];
	};

	AddExample(LOCTEXT("ExViewTitle", "Design review or flythrough"),
		LOCTEXT("ExViewTag", "Datasmith Model"),
		LOCTEXT("ExViewBody", "You fly around the model, take stills or click objects to see their details. Simple Box collision is enough for clicking, and it keeps the import quick and the files small."));
	AddExample(LOCTEXT("ExScheduleTitle", "Construction schedule playback"),
		LOCTEXT("ExScheduleTag", "Twin Sync Model"),
		LOCTEXT("ExScheduleBody", "The model shows how a project is built over time. Schedule materials keep showing and hiding elements as the schedule plays. A box per mesh is enough to click elements and see their details, and keeps the import light."));
	AddExample(LOCTEXT("ExWalkTitle", "Walkthrough or measuring"),
		LOCTEXT("ExWalkTag", "Datasmith Model with Collision, or Twin Sync Model with Collision for schedule models"),
		LOCTEXT("ExWalkBody", "Someone walks through the building, or a tool measures against the real surface. Complex collision makes traces hit walls and floors, not boxes. If traces need more detail, raise Fallback Percent Triangles in your copy."));
	AddExample(LOCTEXT("ExLargeTitle", "Very large site or building"),
		LOCTEXT("ExLargeTag", "Any template, with lower batch sizes under General"),
		LOCTEXT("ExLargeBody", "Export the model in parts, for example one file per building or floor. Each part gets its own folder and level, and a persistent level can stream them together. If memory runs short, lower Mesh Batch Size under General and Max Texture Size in your template."));
	AddExample(LOCTEXT("ExTeamTitle", "Same kind of model every week"),
		LOCTEXT("ExTeamTag", "Your own project template"),
		LOCTEXT("ExTeamBody", "Duplicate a built-in template, change what you need, and tick Destination so each new export lands in the right folder. Commit the template file so your team uses it too."));
	AddExample(LOCTEXT("ExOneOffTitle", "Re-import with one setting changed"),
		LOCTEXT("ExOneOffTag", "Save import settings as template"),
		LOCTEXT("ExOneOffBody", "In Model Manager, choose More, then Save import settings as template. Change the setting on the Settings page and save. Then open Re-import and choose that template under Settings."));
}

#undef LOCTEXT_NAMESPACE
