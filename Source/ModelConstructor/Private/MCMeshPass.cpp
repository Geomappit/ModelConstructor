// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

// The mesh pass: collision, Nanite and build settings in one edit per mesh, then one build per batch.
// Setting the fields directly and building once replaces the two or three builds per mesh that
// PostEditChange caused when collision and Nanite were set separately.

#include "MCPasses.h"
#include "MCRun.h"
#include "MCSettings.h"
#include "Engine/StaticMesh.h"
#include "Misc/ScopedSlowTask.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshCompiler.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

namespace MCPasses
{
namespace
{
	struct FMeshPlan
	{
		bool bRemoveShapes = false;
		bool bSetBox = false;
		bool bSetFlag = false;
		ECollisionTraceFlag Flag = CTF_UseDefault;
		bool bCpuAccess = false;
		bool bNavigationOff = false;
		bool bNanite = false;
		bool bBuild = false;
		FMeshNaniteSettings Nanite;
		FMeshBuildSettings Build;
		FKBoxElem Box;

		bool HasWork() const { return bRemoveShapes || bSetBox || bSetFlag || bCpuAccess || bNavigationOff || bNanite || bBuild; }
	};

	FMeshNaniteSettings MakeNanite(const UStaticMesh* Mesh, const UMCSettings& S)
	{
		// Starts from the mesh's current settings, so fields this pass does not set keep their values.
		FMeshNaniteSettings Nanite = Mesh->NaniteSettings;
		Nanite.bEnabled = S.bEnableNanite;
		Nanite.bPreserveArea = S.bPreserveArea;
		Nanite.bExplicitTangents = false;
		Nanite.bLerpUVs = true;
		Nanite.PositionPrecision = S.PositionPrecision;
		Nanite.KeepPercentTriangles = S.KeepPercentTriangles;
		Nanite.FallbackTarget = ENaniteFallbackTarget::PercentTriangles;
		Nanite.FallbackPercentTriangles = S.FallbackPercentTriangles;
		Nanite.FallbackRelativeError = 0.f;
		Nanite.MaxEdgeLengthFactor = 2.f;
		return Nanite;
	}

	FMeshBuildSettings MakeBuild(const FMeshBuildSettings& Current, const UMCSettings& S)
	{
		FMeshBuildSettings Build = Current;
		Build.bUseMikkTSpace = S.bUseMikkTSpace;
		Build.bRecomputeNormals = S.bRecomputeNormals;
		Build.bRecomputeTangents = S.bRecomputeTangents;
		Build.bRemoveDegenerates = S.bRemoveDegenerates;
		Build.bGenerateLightmapUVs = S.bGenerateLightmapUVs;
		return Build;
	}

	/** Triangles the collision would use: the fallback mesh when Nanite is on, else the full mesh. */
	int64 CollisionTriangles(const UStaticMesh* Mesh, const UMCSettings& S)
	{
		const int64 Source = SourceTriangles(Mesh);
		return S.bEnableNanite ? static_cast<int64>(static_cast<double>(Source) * S.FallbackPercentTriangles) : Source;
	}

	int32 SimpleShapeCount(const UStaticMesh* Mesh)
	{
		const UBodySetup* Body = Mesh->GetBodySetup();
		return Body ? Body->AggGeom.GetElementCount() : 0;
	}

	/** A box around the mesh's bounds. Flat meshes get a 1 cm thickness so the box still blocks. */
	FKBoxElem MakeBox(const UStaticMesh* Mesh)
	{
		const FBox Bounds = Mesh->GetBoundingBox();
		const FVector Size = Bounds.GetSize().ComponentMax(FVector(1.0));
		FKBoxElem Box(static_cast<float>(Size.X), static_cast<float>(Size.Y), static_cast<float>(Size.Z));
		Box.Center = Bounds.GetCenter();
		return Box;
	}

	/** True when the mesh already has exactly this box and nothing else. */
	bool HasOnlyBox(const UBodySetup* Body, const FKBoxElem& Box)
	{
		if (!Body || Body->AggGeom.GetElementCount() != 1 || Body->AggGeom.BoxElems.Num() != 1)
		{
			return false;
		}
		const FKBoxElem& Current = Body->AggGeom.BoxElems[0];
		constexpr double Tolerance = 0.5;
		return Current.Center.Equals(Box.Center, Tolerance) && Current.Rotation.IsNearlyZero()
			&& FMath::IsNearlyEqual(static_cast<double>(Current.X), static_cast<double>(Box.X), Tolerance)
			&& FMath::IsNearlyEqual(static_cast<double>(Current.Y), static_cast<double>(Box.Y), Tolerance)
			&& FMath::IsNearlyEqual(static_cast<double>(Current.Z), static_cast<double>(Box.Z), Tolerance);
	}

	void ApplyPlan(UStaticMesh* Mesh, const FMeshPlan& Plan)
	{
		Mesh->Modify();
		if (Plan.bRemoveShapes || Plan.bSetBox || Plan.bSetFlag)
		{
			Mesh->CreateBodySetup();
			UBodySetup* Body = Mesh->GetBodySetup();
			Body->Modify();
			if (Plan.bRemoveShapes || Plan.bSetBox)
			{
				Body->RemoveSimpleCollision();
			}
			if (Plan.bSetBox)
			{
				Body->AggGeom.BoxElems.Add(Plan.Box);
			}
			Body->CollisionTraceFlag = Plan.Flag;
			// The cooked collision no longer matches the shapes; it is cooked again when needed.
			Body->InvalidatePhysicsData();
		}
		if (Plan.bCpuAccess)
		{
			Mesh->bAllowCPUAccess = true;
		}
		if (Plan.bNavigationOff)
		{
			Mesh->MarkAsNotHavingNavigationData();
		}
		if (Plan.bNanite)
		{
			Mesh->NaniteSettings = Plan.Nanite;
		}
		if (Plan.bBuild)
		{
			Mesh->GetSourceModel(0).BuildSettings = Plan.Build;
		}
	}

	/** Builds the batch and waits for it, so only one batch of build data is in memory at a time. */
	void BuildBatch(TArray<UStaticMesh*>& Batch, int32& BatchNumber)
	{
		if (Batch.Num() == 0)
		{
			return;
		}
		++BatchNumber;
		MCRun::LogStep(FString::Printf(TEXT("Building mesh batch %d (%d meshes)"), BatchNumber, Batch.Num()));

		UStaticMesh::FBuildParameters Parameters;
		Parameters.bInSilent = true;
		Parameters.bInRebuildUVChannelData = true;
		Parameters.bInEnforceLightmapRestrictions = true;
		UStaticMesh::BatchBuild(Batch, Parameters);
		FStaticMeshCompilingManager::Get().FinishCompilation(Batch);

		// The build is done, so the unpacked source meshes are not needed in memory. They stay in the
		// package and load again on demand.
		for (UStaticMesh* Mesh : Batch)
		{
			Mesh->ClearMeshDescriptions();
		}
		Batch.Reset();
	}
}

int64 SourceTriangles(const UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return 0;
	}
	if (Mesh->IsNaniteEnabled() && Mesh->HasValidNaniteData())
	{
		return Mesh->GetNumNaniteTriangles();
	}
	return Mesh->GetNumLODs() > 0 ? Mesh->GetNumTriangles(0) : 0;
}

void RunMeshes(const FContext& Context, FMCStageResult& Stage)
{
	const UMCSettings& S = Context.Settings;
	const EMCCollisionMode Mode = S.CollisionMode;
	const bool bShapes = Mode != EMCCollisionMode::KeepImported;
	const int32 BatchSize = FMath::Max(1, S.MeshBatchSize);
	const int64 TriangleBudget = FMath::Max<int64>(100000, S.MeshBatchTriangleBudget);

	TArray<FString> NoLod;
	TArray<FString> OverCollisionLimit;
	TArray<FString> NoCollision;
	TArray<FString> NavigationOff;

	TArray<UStaticMesh*> Meshes;
	for (UStaticMesh* Mesh : LoadAssets<UStaticMesh>(Context.Folder))
	{
		if (Mesh->GetNumSourceModels() > 0)
		{
			Meshes.Add(Mesh);
		}
		else
		{
			NoLod.Add(Mesh->GetName());
		}
	}
	Stage.Total = Meshes.Num() + NoLod.Num();

	TArray<UStaticMesh*> Batch;
	int64 BatchTriangles = 0;
	int32 BatchNumber = 0;

	FScopedSlowTask Task(static_cast<float>(Meshes.Num()), FText::FromString(Stage.Name));
	for (int32 Index = 0; Index < Meshes.Num(); ++Index)
	{
		Task.EnterProgressFrame(1.f, FText::FromString(FString::Printf(TEXT("Meshes %d/%d"), Index + 1, Meshes.Num())));
		if (Task.ShouldCancel())
		{
			// Meshes already changed in this batch are built, so none is left half done.
			BuildBatch(Batch, BatchNumber);
			Stage.State = EMCStageState::Cancelled;
			return;
		}

		UStaticMesh* Mesh = Meshes[Index];
		const int64 Triangles = CollisionTriangles(Mesh, S);

		FMeshPlan Plan;
		if (Mode == EMCCollisionMode::SimpleBox)
		{
			// The box answers simple and complex queries, so no triangle collision is cooked at all.
			Plan.Flag = CTF_UseSimpleAsComplex;
			Plan.Box = MakeBox(Mesh);
			const UBodySetup* Body = Mesh->GetBodySetup();
			Plan.bSetBox = !HasOnlyBox(Body, Plan.Box);
			Plan.bSetFlag = !Body || Body->CollisionTraceFlag != Plan.Flag;
		}
		else if (bShapes)
		{
			const bool bOverLimit = Mode == EMCCollisionMode::Complex && S.ComplexCollisionTriangleLimit > 0 && Triangles > S.ComplexCollisionTriangleLimit;
			const bool bNone = Mode == EMCCollisionMode::NoCollision || bOverLimit;
			Plan.Flag = bNone ? CTF_UseSimpleAsComplex : CTF_UseComplexAsSimple;
			Plan.bRemoveShapes = SimpleShapeCount(Mesh) > 0;
			const UBodySetup* Body = Mesh->GetBodySetup();
			Plan.bSetFlag = !Body || Body->CollisionTraceFlag != Plan.Flag;
			if (bOverLimit)
			{
				OverCollisionLimit.Add(FString::Printf(TEXT("%s (%lld)"), *Mesh->GetName(), Triangles));
			}
			else if (bNone && (Plan.bRemoveShapes || Plan.bSetFlag))
			{
				NoCollision.Add(Mesh->GetName());
			}
		}
		Plan.bCpuAccess = S.bAllowCpuAccess && !Mesh->bAllowCPUAccess;
		// A box is cheap for navigation, so only triangle collision is left out.
		Plan.bNavigationOff = Mode != EMCCollisionMode::SimpleBox && S.NavigationTriangleLimit > 0 && Triangles > S.NavigationTriangleLimit && Mesh->bHasNavigationData;
		if (Plan.bNavigationOff)
		{
			NavigationOff.Add(Mesh->GetName());
		}
		Plan.Nanite = MakeNanite(Mesh, S);
		Plan.bNanite = !(Mesh->NaniteSettings == Plan.Nanite);
		const FMeshBuildSettings& CurrentBuild = Mesh->GetSourceModel(0).BuildSettings;
		Plan.Build = MakeBuild(CurrentBuild, S);
		Plan.bBuild = !(CurrentBuild == Plan.Build);

		if (!Plan.HasWork())
		{
			++Stage.AlreadyOk;
			continue;
		}

		ApplyPlan(Mesh, Plan);
		++Stage.Changed;

		// A very large mesh closes the batch before it, so it builds on its own.
		const int64 BuildTriangles = FMath::Max<int64>(SourceTriangles(Mesh), 1);
		if (Batch.Num() > 0 && BatchTriangles + BuildTriangles > TriangleBudget)
		{
			BuildBatch(Batch, BatchNumber);
			BatchTriangles = 0;
		}
		Batch.Add(Mesh);
		BatchTriangles += BuildTriangles;
		if (Batch.Num() >= BatchSize)
		{
			BuildBatch(Batch, BatchNumber);
			BatchTriangles = 0;
		}
	}
	BuildBatch(Batch, BatchNumber);

	AddListWarning(Context.Warnings, TEXT("meshes skipped because they have no LOD 0"), NoLod);
	AddListWarning(Context.Warnings, FString::Printf(TEXT("meshes have no collision because their collision would be over %d triangles (Complex Collision Triangle Limit)"),
		S.ComplexCollisionTriangleLimit), OverCollisionLimit);
	// Collision Mode None is a choice, not a problem, so it is logged rather than listed as a warning.
	if (NoCollision.Num() > 0)
	{
		UE_LOG(LogModelConstructor, Log, TEXT("%d meshes set to no collision (Collision Mode None)"), NoCollision.Num());
	}
	if (NavigationOff.Num() > 0)
	{
		UE_LOG(LogModelConstructor, Log, TEXT("%d meshes left out of navigation (over %d triangles)"), NavigationOff.Num(), S.NavigationTriangleLimit);
	}
	Context.Counters.Add(TEXT("MeshesOverCollisionLimit"), OverCollisionLimit.Num());
	Context.Counters.Add(TEXT("MeshesOutOfNavigation"), NavigationOff.Num());
	Context.Counters.Add(TEXT("MeshBuildBatches"), BatchNumber);

	Stage.Detail = FString::Printf(TEXT("%d changed in %d batches, %d already correct"), Stage.Changed, BatchNumber, Stage.AlreadyOk);
}
}

#undef LOCTEXT_NAMESPACE
