// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "MCRun.h"
#include "MCPasses.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "Subsystems/UnrealEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

namespace MCRun
{
namespace
{
	FText PauseLabel()
	{
		return LOCTEXT("PauseLabel", "Model Constructor");
	}

	UWorld* EditorWorld()
	{
		UUnrealEditorSubsystem* EditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>() : nullptr;
		return EditorSubsystem ? EditorSubsystem->GetEditorWorld() : nullptr;
	}

	FString JournalFile()
	{
		return FPaths::ProjectSavedDir() / TEXT("ModelConstructor") / TEXT("LastRun.ini");
	}

	TMap<FString, FString>& JournalValues()
	{
		static TMap<FString, FString> Values;
		return Values;
	}

	void WriteJournal()
	{
		TArray<FString> Lines;
		for (const TPair<FString, FString>& Pair : JournalValues())
		{
			Lines.Add(Pair.Key + TEXT("=") + Pair.Value);
		}
		FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *JournalFile());
	}
}

const TCHAR* StageName(int32 Stage)
{
	static const TCHAR* Names[StageCount] = {
		TEXT("Prepare"), TEXT("Remove old version"), TEXT("Check editor ready"), TEXT("Level"), TEXT("Import"),
		TEXT("Finish import builds"), TEXT("Meshes"), TEXT("Texture"), TEXT("Material"), TEXT("Finish pass builds"),
		TEXT("Report"), TEXT("Save") };
	return Names[Stage];
}

bool Fail(FMCStageResult& Stage, const FString& Message)
{
	Stage.Detail = Message;
	return false;
}

void LogStep(const FString& Message)
{
	UE_LOG(LogModelConstructor, Display, TEXT("%s"), *Message);
	GLog->Flush();
}

FRunner::FRunner(FMCImportResult& InResult, const TArray<int32>& InStages, const FString& InKind)
	: Result(InResult)
	, Stages(InStages)
	, Task(static_cast<float>(InStages.Num()), LOCTEXT("RunTitle", "Model Constructor"))
	, Kind(InKind)
	, StartTime(FPlatformTime::Seconds())
{
	for (const int32 Stage : Stages)
	{
		FMCStageResult& Entry = Result.Stages.AddDefaulted_GetRef();
		Entry.Name = StageName(Stage);
	}
	Journal::Begin(Kind);
	LogStep(FString::Printf(TEXT("%s started"), *Kind));
}

FRunner::~FRunner()
{
	Finish();
}

void FRunner::Finish()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	Result.TotalSeconds = static_cast<float>(FPlatformTime::Seconds() - StartTime);
	Result.bSucceeded = Result.Error.IsEmpty();
	Journal::End(Result.bSucceeded);
	LogStep(FString::Printf(TEXT("%s %s: %s in %.1f s"), *Kind, Result.bSucceeded ? TEXT("finished") : TEXT("stopped"),
		*Result.Folder, Result.TotalSeconds));
}

void FRunner::ShowDialog()
{
	if (!bDialog)
	{
		Task.MakeDialog(/*bShowCancelButton*/ true);
		bDialog = true;
	}
}

bool FRunner::Run(int32 Stage, TFunctionRef<bool(FMCStageResult&)> Body)
{
	const int32 Index = Stages.IndexOfByKey(Stage);
	check(Index != INDEX_NONE);
	FMCStageResult& Entry = Result.Stages[Index];

	Task.EnterProgressFrame(1.f, FText::FromString(Entry.Name));
	if (bDialog && Task.ShouldCancel())
	{
		Stop(Index, EMCStageState::Cancelled, FString::Printf(TEXT("Cancelled before %s. Changes made so far are not saved."), *Entry.Name));
		return false;
	}

	Journal::Stage(Entry.Name, Result.Folder);
	LogStep(FString::Printf(TEXT("Stage %s"), *Entry.Name));

	Entry.State = EMCStageState::Running;
	const double Start = FPlatformTime::Seconds();
	const bool bOk = Body(Entry);
	Entry.Seconds = static_cast<float>(FPlatformTime::Seconds() - Start);

	if (Entry.State == EMCStageState::Cancelled)
	{
		Stop(Index + 1, EMCStageState::Cancelled, FString::Printf(TEXT("Cancelled during %s. Changes made so far are not saved."), *Entry.Name));
		return false;
	}
	if (!bOk || Entry.State == EMCStageState::Failed)
	{
		Entry.State = EMCStageState::Failed;
		if (Result.Error.IsEmpty())
		{
			Result.Error = Entry.Detail.IsEmpty() ? FString::Printf(TEXT("%s failed"), *Entry.Name) : Entry.Detail;
		}
		Stop(Index + 1, EMCStageState::Skipped, Result.Error);
		return false;
	}
	if (Entry.State == EMCStageState::Running)
	{
		Entry.State = EMCStageState::Done;
	}
	return true;
}

void FRunner::Stop(int32 FromIndex, EMCStageState State, const FString& Error)
{
	for (int32 Index = FromIndex; Index < Result.Stages.Num(); ++Index)
	{
		Result.Stages[Index].State = State;
	}
	if (Result.Error.IsEmpty())
	{
		Result.Error = Error;
	}
	UE_LOG(LogModelConstructor, Warning, TEXT("%s"), *Result.Error);
}

FViewportPause::FViewportPause(bool bEnable)
{
	if (!bEnable || !GEditor)
	{
		return;
	}
	for (FEditorViewportClient* Client : GEditor->GetAllViewportClients())
	{
		if (Client)
		{
			Client->AddRealtimeOverride(/*bShouldBeRealtime*/ false, PauseLabel());
		}
	}
	bActive = true;
}

FViewportPause::~FViewportPause()
{
	if (!bActive || !GEditor)
	{
		return;
	}
	for (FEditorViewportClient* Client : GEditor->GetAllViewportClients())
	{
		if (Client)
		{
			Client->RemoveRealtimeOverride(PauseLabel(), /*bCheckMissingOverride*/ false);
		}
	}
}

FActorPause::~FActorPause()
{
	Resume();
}

int32 FActorPause::Begin(const FString& Folder)
{
	Resume();
	UWorld* World = EditorWorld();
	if (!World)
	{
		return 0;
	}

	const FString Prefix = Folder + TEXT("/");
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
		const bool bUsesFolder = Components.ContainsByPredicate([&Prefix](const UStaticMeshComponent* Component)
		{
			const UStaticMesh* Mesh = Component->GetStaticMesh();
			return Mesh && Mesh->GetPathName().StartsWith(Prefix);
		});
		if (bUsesFolder)
		{
			Actors.Add(Actor);
		}
	}

	FScopedSlowTask Task(static_cast<float>(Actors.Num()), LOCTEXT("PauseActors", "Pausing model actors"));
	for (const TWeakObjectPtr<AActor>& Actor : Actors)
	{
		Task.EnterProgressFrame(1.f);
		Actor->UnregisterAllComponents();
	}
	if (Actors.Num() > 0)
	{
		LogStep(FString::Printf(TEXT("Paused %d actors during the passes"), Actors.Num()));
	}
	return Actors.Num();
}

void FActorPause::Resume()
{
	if (Actors.Num() == 0)
	{
		return;
	}
	LogStep(FString::Printf(TEXT("Restoring %d actors"), Actors.Num()));
	FScopedSlowTask Task(static_cast<float>(Actors.Num()), LOCTEXT("ResumeActors", "Restoring model actors"));
	for (const TWeakObjectPtr<AActor>& Actor : Actors)
	{
		Task.EnterProgressFrame(1.f);
		if (AActor* Live = Actor.Get())
		{
			Live->RegisterAllComponents();
		}
	}
	Actors.Reset();
}

namespace Journal
{
void Begin(const FString& Kind)
{
	TMap<FString, FString>& Values = JournalValues();
	Values.Reset();
	Values.Add(TEXT("State"), TEXT("Running"));
	Values.Add(TEXT("Kind"), Kind);
	Values.Add(TEXT("Started"), FDateTime::Now().ToString());
	WriteJournal();
}

void Stage(const FString& StageName, const FString& Folder)
{
	TMap<FString, FString>& Values = JournalValues();
	Values.Add(TEXT("Stage"), StageName);
	Values.Add(TEXT("Folder"), Folder);
	WriteJournal();
}

void End(bool bSucceeded)
{
	JournalValues().Add(TEXT("State"), bSucceeded ? TEXT("Finished") : TEXT("Stopped"));
	WriteJournal();
}

FString ReadUnfinished()
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *JournalFile()))
	{
		return FString();
	}
	TMap<FString, FString> Values;
	for (const FString& Line : Lines)
	{
		FString Key;
		FString Value;
		if (Line.Split(TEXT("="), &Key, &Value))
		{
			Values.Add(Key, Value);
		}
	}
	if (Values.FindRef(TEXT("State")) != TEXT("Running"))
	{
		return FString();
	}
	const FString Folder = Values.FindRef(TEXT("Folder"));
	return FString::Printf(TEXT("The last %s (started %s) did not finish. It stopped during %s%s. The editor may have closed or crashed. Check the folder in Model Manager, and remove or re-import it."),
		*Values.FindRef(TEXT("Kind")).ToLower(), *Values.FindRef(TEXT("Started")), *Values.FindRef(TEXT("Stage")),
		Folder.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" of %s"), *Folder));
}

void Clear()
{
	JournalValues().Reset();
	IFileManager::Get().Delete(*JournalFile(), /*bRequireExists*/ false);
}
}
}

#undef LOCTEXT_NAMESPACE
