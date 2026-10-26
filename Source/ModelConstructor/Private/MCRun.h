// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopedSlowTask.h"
#include "MCTypes.h"

class AActor;

/** Shared pieces of a run: the stage list, the stage runner, the editor pauses and the run journal. */
namespace MCRun
{
	enum EStage : int32
	{
		StagePrepare,
		StageRemoveOld,
		StageEditorReady,
		StageLevel,
		StageImport,
		StageFinishImportBuilds,
		StageMeshes,
		StageTexture,
		StageMaterial,
		StageFinishPassBuilds,
		StageReport,
		StageSave,
		StageCount
	};

	const TCHAR* StageName(int32 Stage);

	/** Sets the stage detail and returns false, for one-line failures. */
	bool Fail(FMCStageResult& Stage, const FString& Message);

	/** Writes a line to the log and flushes it, so the log is complete even if the editor stops right after. */
	void LogStep(const FString& Message);

	/** Runs stages in order inside one slow task. A failed or cancelled stage stops the run. */
	class FRunner
	{
	public:
		FRunner(FMCImportResult& InResult, const TArray<int32>& InStages, const FString& Kind);
		~FRunner();

		void Finish();

		/** Opened after the first stage, so a save prompt from that stage is not hidden behind the progress dialog. */
		void ShowDialog();

		bool Run(int32 Stage, TFunctionRef<bool(FMCStageResult&)> Body);

	private:
		void Stop(int32 FromIndex, EMCStageState State, const FString& Error);

		FMCImportResult& Result;
		TArray<int32> Stages;
		FScopedSlowTask Task;
		FString Kind;
		double StartTime = 0.0;
		bool bDialog = false;
		bool bFinished = false;
	};

	/** Stops the viewports redrawing in real time until destroyed. */
	class FViewportPause
	{
	public:
		explicit FViewportPause(bool bEnable);
		~FViewportPause();

	private:
		bool bActive = false;
	};

	/**
	 * Takes the actors that use meshes from a folder out of the renderer, physics and navigation, and puts them
	 * back once on Resume or when destroyed. Material and mesh changes then do not refresh every actor each time.
	 */
	class FActorPause
	{
	public:
		FActorPause() = default;
		~FActorPause();

		/** Returns the number of actors paused. */
		int32 Begin(const FString& Folder);
		void Resume();

	private:
		TArray<TWeakObjectPtr<AActor>> Actors;
	};

	/** A small file in Saved/ModelConstructor that says which run is going and which stage it is on. */
	namespace Journal
	{
		void Begin(const FString& Kind);
		void Stage(const FString& StageName, const FString& Folder);
		void End(bool bSucceeded);

		/** A description of a run that started and never ended, or empty. */
		FString ReadUnfinished();
		void Clear();
	}
}
