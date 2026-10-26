// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "MCPasses.h"
#include "MCModels.h"
#include "MCRun.h"
#include "MCSettings.h"
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/StaticMeshComponent.h"
#include "DatasmithScene.h"
#include "DatasmithSceneActor.h"
#include "DistanceFieldAtlas.h"
#include "Editor.h"
#include "EditorSupportDelegates.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialShared.h"
// 5.6 declares the card build queue in MeshCardBuild.h, earlier versions in MeshCardRepresentation.h.
#if __has_include("MeshCardBuild.h")
#include "MeshCardBuild.h"
#else
#include "MeshCardRepresentation.h"
#endif
#include "Misc/ScopedSlowTask.h"
#include "PhysicsEngine/BodySetup.h"
#include "ShaderCompiler.h"
#include "StaticMeshCompiler.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "TextureCompiler.h"
#include "UObject/SoftObjectPath.h"

#define LOCTEXT_NAMESPACE "ModelConstructor"

namespace MCPasses
{
namespace
{
	const TCHAR* DitherFunctionPath = TEXT("/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA.DitherTemporalAA");
	const TCHAR* DitherFunctionName = TEXT("DitherTemporalAA");
	constexpr int32 MaxNamesPerWarning = 10;

	/** Report thresholds. Above them a model is likely to be slow to save or to hit engine size limits. */
	constexpr int32 LargeLevelActorCount = 100000;
	constexpr int64 LargeMeshTriangles = 20000000;
	constexpr double LargeMeshExtentCm = 1.0e7;

	FString JoinNames(const TArray<FString>& Names)
	{
		if (Names.Num() <= MaxNamesPerWarning)
		{
			return FString::Join(Names, TEXT(", "));
		}
		TArray<FString> Head(Names.GetData(), MaxNamesPerWarning);
		return FString::Printf(TEXT("%s and %d more"), *FString::Join(Head, TEXT(", ")), Names.Num() - MaxNamesPerWarning);
	}

	/**
	 * Each pass picks assets, checks them (Needs) and fixes them (Apply). Needs must not change the
	 * asset, so a second run over the same folder reports everything as already OK.
	 */
	template <typename T>
	void RunAssetPass(FMCStageResult& Stage, const TArray<T*>& Assets, TFunctionRef<bool(T*)> Needs, TFunctionRef<void(T*)> Apply)
	{
		Stage.Total += Assets.Num();

		FScopedSlowTask Task(static_cast<float>(Assets.Num()), FText::FromString(Stage.Name));
		for (int32 Index = 0; Index < Assets.Num(); ++Index)
		{
			Task.EnterProgressFrame(1.f, FText::FromString(FString::Printf(TEXT("%s %d/%d"), *Stage.Name, Index + 1, Assets.Num())));
			if (Task.ShouldCancel())
			{
				Stage.State = EMCStageState::Cancelled;
				return;
			}

			T* Asset = Assets[Index];
			if (Needs(Asset))
			{
				Apply(Asset);
				++Stage.Changed;
			}
			else
			{
				++Stage.AlreadyOk;
			}
		}
	}

	bool CallsFunction(UMaterial* Material, const FString& NameContains)
	{
		if (NameContains.IsEmpty())
		{
			return false;
		}
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			const UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
			if (Call && Call->MaterialFunction && Call->MaterialFunction->GetName().Contains(NameContains))
			{
				return true;
			}
		}
		return false;
	}

	bool IsDither(const UMaterialExpression* Expression)
	{
		const UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
		return Call && Call->MaterialFunction && Call->MaterialFunction->GetName().Contains(DitherFunctionName);
	}

	int32 FindAlphaThresholdInput(const UMaterialExpressionMaterialFunctionCall* Call)
	{
		for (int32 Index = 0; Index < Call->FunctionInputs.Num(); ++Index)
		{
			const UMaterialExpressionFunctionInput* Input = Call->FunctionInputs[Index].ExpressionInput;
			if (Input && Input->InputName.ToString().Replace(TEXT(" "), TEXT("")).Equals(TEXT("AlphaThreshold"), ESearchCase::IgnoreCase))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	struct FMaterialPlan
	{
		TArray<TPair<UMaterialExpressionTextureBase*, EMaterialSamplerType>> Samplers;
		UMaterialExpressionMaterialFunctionCall* ExistingDither = nullptr;
		bool bWireDither = false;
		bool bSetMasked = false;
		bool bSetNanite = false;

		bool HasWork() const { return Samplers.Num() > 0 || bWireDither || bSetMasked || bSetNanite; }
	};

	struct FOpacitySource
	{
		UMaterialExpression* Expression = nullptr;
		FString Output;
	};

	/**
	 * Where the dither gets its opacity, first match wins: the schedule function's Opacity output (only
	 * with schedule support on), then a scalar parameter named like opacity or alpha, then whatever is
	 * wired into Opacity.
	 */
	FOpacitySource FindOpacitySource(UMaterial* Material, const UMCSettings& Settings)
	{
		const FString FunctionName = Settings.ScheduleFunctionNameContains.TrimStartAndEnd();
		if (Settings.bScheduleAnimationSupport && !FunctionName.IsEmpty())
		{
			for (UMaterialExpression* Expression : Material->GetExpressions())
			{
				const UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
				if (Call && Call->MaterialFunction && Call->MaterialFunction->GetName().Contains(FunctionName))
				{
					return { Expression, TEXT("Opacity") };
				}
			}
		}

		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (const UMaterialExpressionScalarParameter* Param = Cast<UMaterialExpressionScalarParameter>(Expression))
			{
				const FString Name = Param->ParameterName.ToString();
				if (Name.Contains(TEXT("opacity")) || Name.Contains(TEXT("alpha")))
				{
					return { Expression, FString() };
				}
			}
		}

		if (UMaterialExpression* Driver = UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_Opacity))
		{
			return { Driver, UMaterialEditingLibrary::GetMaterialPropertyInputNodeOutputName(Material, MP_Opacity) };
		}
		return {};
	}

	float ConstantOpacity(UMaterial* Material)
	{
		if (const UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData())
		{
			if (EditorData->Opacity.UseConstant)
			{
				return EditorData->Opacity.Constant;
			}
		}
		return 1.f;
	}

	bool IsInFolder(const UObject* Object, const FString& Folder)
	{
		if (!Object)
		{
			return false;
		}
		const FString Package = Object->GetPackage()->GetName();
		return Package == Folder || Package.StartsWith(Folder + TEXT("/"));
	}

	bool ProjectUsesVirtualTextures()
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VirtualTextures"));
		return Variable && Variable->GetInt() != 0;
	}

	/** The sampler type that matches a texture's streaming mode. False when the type has no counterpart. */
	bool MatchingSamplerType(EMaterialSamplerType Type, bool bVirtual, EMaterialSamplerType& Out)
	{
		static const EMaterialSamplerType Pairs[][2] = {
			{ SAMPLERTYPE_Color, SAMPLERTYPE_VirtualColor },
			{ SAMPLERTYPE_Grayscale, SAMPLERTYPE_VirtualGrayscale },
			{ SAMPLERTYPE_Alpha, SAMPLERTYPE_VirtualAlpha },
			{ SAMPLERTYPE_Normal, SAMPLERTYPE_VirtualNormal },
			{ SAMPLERTYPE_Masks, SAMPLERTYPE_VirtualMasks },
			{ SAMPLERTYPE_LinearColor, SAMPLERTYPE_VirtualLinearColor },
			{ SAMPLERTYPE_LinearGrayscale, SAMPLERTYPE_VirtualLinearGrayscale },
		};
		for (const auto& Pair : Pairs)
		{
			if (Type == Pair[0] || Type == Pair[1])
			{
				Out = Pair[bVirtual ? 1 : 0];
				return true;
			}
		}
		return false;
	}

	/** Textures and samplers that can be switched to the requested streaming mode. */
	struct FVirtualTexturePlan
	{
		TSet<UTexture*> Textures;
		TMap<UMaterialExpressionTextureBase*, EMaterialSamplerType> Samplers;
		TArray<FString> Locked;
	};

	/**
	 * A virtual texture needs a virtual sampler and a regular texture a regular one, in every material
	 * that samples it. Textures and the samplers they reach are grouped, and a group switches only when
	 * all of it is in the model folder and every sampler type has a counterpart. Anything else is left as
	 * it is, because switching half a group gives the materials sampler errors.
	 */
	FVirtualTexturePlan PlanVirtualTextures(const FString& Folder, bool bVirtual, const TArray<UMaterial*>& Materials,
		const TArray<UMaterialInstanceConstant*>& Instances, const TArray<UTexture2D*>& Textures)
	{
		TArray<int32> Parent;
		TArray<bool> Locked;
		TMap<UObject*, int32> Ids;

		auto Node = [&](UObject* Key) -> int32
		{
			if (const int32* Found = Ids.Find(Key))
			{
				return *Found;
			}
			const int32 Id = Parent.Num();
			Parent.Add(Id);
			Locked.Add(false);
			Ids.Add(Key, Id);
			return Id;
		};
		auto Root = [&](int32 Id) -> int32
		{
			while (Parent[Id] != Id)
			{
				Parent[Id] = Parent[Parent[Id]];
				Id = Parent[Id];
			}
			return Id;
		};
		auto Join = [&](int32 A, int32 B)
		{
			A = Root(A);
			B = Root(B);
			if (A != B)
			{
				Parent[B] = A;
				Locked[A] = Locked[A] || Locked[B];
			}
		};
		auto Lock = [&](int32 Id) { Locked[Root(Id)] = true; };
		auto AddTexture = [&](UTexture* Texture) -> int32
		{
			const int32 Id = Node(Texture);
			if (!Texture->IsA<UTexture2D>() || !IsInFolder(Texture, Folder))
			{
				Lock(Id);
			}
			return Id;
		};

		for (UTexture2D* Texture : Textures)
		{
			AddTexture(Texture);
		}

		TMultiMap<TPair<UMaterial*, FName>, UMaterialExpressionTextureBase*> ByParameter;
		for (UMaterial* Material : Materials)
		{
			for (UMaterialExpression* Expression : Material->GetExpressions())
			{
				UMaterialExpressionTextureBase* Sampler = Cast<UMaterialExpressionTextureBase>(Expression);
				if (!Sampler)
				{
					continue;
				}
				const int32 Id = Node(Sampler);
				EMaterialSamplerType Unused;
				if (!MatchingSamplerType(Sampler->SamplerType, bVirtual, Unused))
				{
					Lock(Id);
				}
				if (UTexture* Texture = Sampler->Texture.Get())
				{
					Join(Id, AddTexture(Texture));
				}
				else
				{
					// An empty sampler draws the engine default texture, which is never virtual.
					Lock(Id);
				}
				if (const UMaterialExpressionTextureSampleParameter* Param = Cast<UMaterialExpressionTextureSampleParameter>(Sampler))
				{
					ByParameter.Add(TPair<UMaterial*, FName>(Material, Param->ParameterName), Sampler);
				}
			}
		}

		for (UMaterialInstanceConstant* Instance : Instances)
		{
			UMaterial* Base = Instance->GetMaterial();
			const bool bLocalBase = IsInFolder(Base, Folder);
			for (const FTextureParameterValue& Value : Instance->TextureParameterValues)
			{
				UTexture* Texture = Value.ParameterValue.Get();
				if (!Texture)
				{
					continue;
				}
				const int32 TextureId = AddTexture(Texture);
				TArray<UMaterialExpressionTextureBase*> Targets;
				if (bLocalBase && Value.ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter)
				{
					ByParameter.MultiFind(TPair<UMaterial*, FName>(Base, Value.ParameterInfo.Name), Targets);
				}
				if (Targets.Num() == 0)
				{
					// The sampler is in a parent outside the folder, or in a layer or function this pass does not edit.
					Lock(TextureId);
					continue;
				}
				for (UMaterialExpressionTextureBase* Sampler : Targets)
				{
					Join(TextureId, Node(Sampler));
				}
			}
		}

		FVirtualTexturePlan Plan;
		for (const TPair<UObject*, int32>& Pair : Ids)
		{
			UTexture* Texture = Cast<UTexture>(Pair.Key);
			if (Locked[Root(Pair.Value)])
			{
				if (Texture && IsInFolder(Texture, Folder) && static_cast<bool>(Texture->VirtualTextureStreaming) != bVirtual)
				{
					Plan.Locked.Add(Texture->GetName());
				}
				continue;
			}
			if (Texture)
			{
				Plan.Textures.Add(Texture);
			}
			else if (UMaterialExpressionTextureBase* Sampler = Cast<UMaterialExpressionTextureBase>(Pair.Key))
			{
				EMaterialSamplerType Target;
				if (MatchingSamplerType(Sampler->SamplerType, bVirtual, Target) && Target != Sampler->SamplerType)
				{
					Plan.Samplers.Add(Sampler, Target);
				}
			}
		}
		Plan.Locked.Sort();
		return Plan;
	}

	/** Builds the plan for a pass, or an empty one when virtual textures are asked for but the project has them off. */
	FVirtualTexturePlan PlanVirtualTexturesFor(const FContext& Context, const TArray<UMaterial*>& Materials,
		const TArray<UMaterialInstanceConstant*>& Instances, const TArray<UTexture2D*>& Textures)
	{
		const bool bVirtual = Context.Settings.bVirtualTextureStreaming;
		if (bVirtual && !ProjectUsesVirtualTextures())
		{
			return {};
		}
		return PlanVirtualTextures(Context.Folder, bVirtual, Materials, Instances, Textures);
	}
}

void AddWarning(TArray<FString>& Warnings, const FString& Message)
{
	UE_LOG(LogModelConstructor, Warning, TEXT("%s"), *Message);
	Warnings.Add(Message);
}

void AddListWarning(TArray<FString>& Warnings, const FString& What, const TArray<FString>& Names)
{
	if (Names.Num() > 0)
	{
		AddWarning(Warnings, FString::Printf(TEXT("%d %s: %s"), Names.Num(), *What, *JoinNames(Names)));
	}
}

void SetCountDetail(FMCStageResult& Stage)
{
	if (Stage.Detail.IsEmpty())
	{
		Stage.Detail = FString::Printf(TEXT("%d changed, %d already correct"), Stage.Changed, Stage.AlreadyOk);
	}
}

TArray<FAssetData> FindAssets(const FString& Folder, const UClass* Class)
{
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Folder));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(Class->GetClassPathName());
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> Found;
	MCModels::Registry().GetAssets(Filter, Found);
	Found.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
	return Found;
}

void RunTexture(const FContext& Context, FMCStageResult& Stage)
{
	const int32 Cap = Context.Settings.MaxTextureSize;
	const bool bVirtual = Context.Settings.bVirtualTextureStreaming;
	const int32 BatchSize = FMath::Max(1, Context.Settings.TextureBatchSize);
	FProperty* VirtualProperty = FindFProperty<FProperty>(UTexture::StaticClass(), TEXT("VirtualTextureStreaming"));
	FProperty* SizeProperty = FindFProperty<FProperty>(UTexture::StaticClass(), TEXT("MaxTextureSize"));

	// Uses the source size, because a texture that is still compiling reports a placeholder size.
	auto NeedsCap = [Cap](const UTexture2D* Texture)
	{
		if (Cap <= 0 || !Texture->Source.IsValid())
		{
			return false;
		}
		const int64 Largest = FMath::Max<int64>(Texture->Source.GetSizeX(), Texture->Source.GetSizeY());
		const bool bLimitMissing = Texture->MaxTextureSize <= 0 || Texture->MaxTextureSize > Cap;
		return Largest > Cap && bLimitMissing;
	};

	// Each batch is waited for before the next starts, so only a batch of texture builds is in memory at once.
	TArray<UTexture*> Batch;
	auto FinishBatch = [&Batch]()
	{
		if (Batch.Num() > 0)
		{
			FTextureCompilingManager::Get().FinishCompilation(Batch);
			Batch.Reset();
		}
	};

	const TArray<UTexture2D*> Textures = LoadAssets<UTexture2D>(Context.Folder);
	const FVirtualTexturePlan Plan = PlanVirtualTexturesFor(Context, LoadAssets<UMaterial>(Context.Folder),
		LoadAssets<UMaterialInstanceConstant>(Context.Folder), Textures);
	if (bVirtual && !ProjectUsesVirtualTextures())
	{
		AddWarning(Context.Warnings, TEXT("Virtual Texture Streaming is on in the template, but virtual textures are off in this project, so no texture was switched. Turn on Enable virtual texture support in Project Settings and run again"));
	}
	AddListWarning(Context.Warnings, TEXT("textures kept their virtual texture setting because a material outside the model folder, or a sampler with no matching type, also uses them"), Plan.Locked);

	auto NeedsVirtual = [&Plan, bVirtual](UTexture2D* Texture)
	{
		return Plan.Textures.Contains(Texture) && static_cast<bool>(Texture->VirtualTextureStreaming) != bVirtual;
	};

	RunAssetPass<UTexture2D>(Stage, Textures,
		[&](UTexture2D* Texture)
		{
			return NeedsCap(Texture) || NeedsVirtual(Texture);
		},
		[&](UTexture2D* Texture)
		{
			const bool bVirtualChanges = NeedsVirtual(Texture);
			Texture->Modify();
			if (NeedsCap(Texture))
			{
				Texture->MaxTextureSize = Cap;
			}
			if (bVirtualChanges)
			{
				Texture->VirtualTextureStreaming = bVirtual;
			}

			// Naming the changed property keeps the update small: only a streaming change updates the materials using the texture.
			FPropertyChangedEvent Event(bVirtualChanges ? VirtualProperty : SizeProperty);
			Texture->PostEditChangeProperty(Event);

			Batch.Add(Texture);
			if (Batch.Num() >= BatchSize)
			{
				FinishBatch();
			}
		});
	FinishBatch();

	SetCountDetail(Stage);
}

void RunMaterial(const FContext& Context, FMCStageResult& Stage)
{
	const UMCSettings& S = Context.Settings;
	const bool bConvert = S.bConvertTranslucentToMaskedWithDither;
	const bool bNanite = S.bSetUsedWithNanite;

	const TArray<UMaterial*> Materials = LoadAssets<UMaterial>(Context.Folder);
	const TArray<UMaterialInstanceConstant*> Instances = LoadAssets<UMaterialInstanceConstant>(Context.Folder);
	const FVirtualTexturePlan VirtualPlan = PlanVirtualTexturesFor(Context, Materials, Instances, LoadAssets<UTexture2D>(Context.Folder));
	const bool bSamplers = VirtualPlan.Samplers.Num() > 0;

	if (!bConvert && !bSamplers && !bNanite)
	{
		Stage.State = EMCStageState::Skipped;
		Stage.Detail = TEXT("No material option is on");
		return;
	}

	UMaterialFunctionInterface* DitherFunction = bConvert ? LoadObject<UMaterialFunctionInterface>(nullptr, DitherFunctionPath) : nullptr;
	if (bConvert && !DitherFunction)
	{
		AddWarning(Context.Warnings, FString::Printf(TEXT("Engine material function %s not found; translucent materials were not converted"), DitherFunctionPath));
	}
	const bool bCanConvert = bConvert && DitherFunction;

	const int32 BatchSize = FMath::Max(1, S.MaterialBatchSize);
	TMap<UMaterial*, FMaterialPlan> Plans;
	TArray<FString> Blocked;
	TArray<FString> ConstantFallback;
	TArray<FString> Incomplete;

	auto MakePlan = [&](UMaterial* Material) -> FMaterialPlan&
	{
		if (FMaterialPlan* Existing = Plans.Find(Material))
		{
			return *Existing;
		}
		FMaterialPlan& Plan = Plans.Add(Material);

		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (bSamplers)
			{
				UMaterialExpressionTextureBase* Sampler = Cast<UMaterialExpressionTextureBase>(Expression);
				if (const EMaterialSamplerType* Target = Sampler ? VirtualPlan.Samplers.Find(Sampler) : nullptr)
				{
					Plan.Samplers.Emplace(Sampler, *Target);
				}
			}
			if (!Plan.ExistingDither && IsDither(Expression))
			{
				Plan.ExistingDither = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
			}
		}

		const bool bTranslucent = Material->BlendMode == BLEND_Translucent;
		const bool bMasked = Material->BlendMode == BLEND_Masked;
		const bool bSelected = bTranslucent || (bMasked && Plan.ExistingDither);
		if (bCanConvert && bSelected)
		{
			UMaterialExpression* MaskDriver = UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_OpacityMask);
			if (MaskDriver && !IsDither(MaskDriver))
			{
				Blocked.Add(Material->GetName());
			}
			else
			{
				Plan.bWireDither = MaskDriver == nullptr;
				Plan.bSetMasked = bTranslucent;
			}
		}

		Plan.bSetNanite = bNanite && !UMaterialEditingLibrary::HasMaterialUsage(Material, MATUSAGE_Nanite);
		return Plan;
	};

	auto WireDither = [&](UMaterial* Material, FMaterialPlan& Plan)
	{
		UMaterialExpressionMaterialFunctionCall* Dither = Plan.ExistingDither;
		const bool bNewNode = Dither == nullptr;
		int32 InputIndex = bNewNode ? INDEX_NONE : FindAlphaThresholdInput(Dither);
		const bool bInputFree = bNewNode || (InputIndex != INDEX_NONE && Dither->FunctionInputs[InputIndex].Input.Expression == nullptr);

		FOpacitySource Source;
		if (bInputFree)
		{
			Source = FindOpacitySource(Material, S);
			if (!Source.Expression)
			{
				UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(
					UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), -640, 320));
				Constant->R = ConstantOpacity(Material);
				Source.Expression = Constant;
				ConstantFallback.Add(Material->GetName());
			}
		}

		if (bNewNode)
		{
			int32 X = 0;
			int32 Y = 0;
			UMaterialEditingLibrary::GetMaterialExpressionNodePosition(Source.Expression, X, Y);
			Dither = Cast<UMaterialExpressionMaterialFunctionCall>(
				UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMaterialFunctionCall::StaticClass(), X + 320, Y));
			Dither->SetMaterialFunction(DitherFunction);
			InputIndex = FindAlphaThresholdInput(Dither);
		}

		bool bOk = InputIndex != INDEX_NONE;
		if (bOk && bInputFree)
		{
			const FString InputName = Dither->FunctionInputs[InputIndex].ExpressionInput->InputName.ToString();
			bOk = UMaterialEditingLibrary::ConnectMaterialExpressions(Source.Expression, Source.Output, Dither, InputName);
		}
		bOk = UMaterialEditingLibrary::ConnectMaterialProperty(Dither, FString(), MP_OpacityMask) && bOk;
		if (!bOk)
		{
			Incomplete.Add(Material->GetName());
		}
	};

	// Changed materials are updated together: one update per batch refreshes their instances and the
	// actors using them once, instead of once per material.
	TArray<UMaterial*> Batch;
	int32 BatchNumber = 0;
	auto UpdateBatch = [&]()
	{
		if (Batch.Num() == 0)
		{
			return;
		}
		++BatchNumber;
		MCRun::LogStep(FString::Printf(TEXT("Updating material batch %d (%d materials)"), BatchNumber, Batch.Num()));
		{
			FMaterialUpdateContext UpdateContext;
			const TSet<UMaterial*> InBatch(Batch);
			for (UMaterial* Material : Batch)
			{
				UpdateContext.AddMaterial(Material);
				Material->PreEditChange(nullptr);
				Material->PostEditChange();
				Material->MarkPackageDirty();
			}
			for (UMaterialInstanceConstant* Instance : Instances)
			{
				if (InBatch.Contains(Instance->GetMaterial()))
				{
					UpdateContext.AddMaterialInstance(Instance);
				}
			}
		}
		Batch.Reset();
	};

	Stage.Total += Materials.Num();
	{
		FScopedSlowTask Task(static_cast<float>(Materials.Num()), FText::FromString(Stage.Name));
		for (int32 Index = 0; Index < Materials.Num(); ++Index)
		{
			Task.EnterProgressFrame(1.f, FText::FromString(FString::Printf(TEXT("Material %d/%d"), Index + 1, Materials.Num())));
			if (Task.ShouldCancel())
			{
				UpdateBatch();
				Stage.State = EMCStageState::Cancelled;
				return;
			}

			UMaterial* Material = Materials[Index];
			FMaterialPlan& Plan = MakePlan(Material);
			if (!Plan.HasWork())
			{
				++Stage.AlreadyOk;
				continue;
			}

			Material->Modify();
			for (const TPair<UMaterialExpressionTextureBase*, EMaterialSamplerType>& Sampler : Plan.Samplers)
			{
				Sampler.Key->Modify();
				Sampler.Key->SamplerType = Sampler.Value;
			}
			if (Plan.bWireDither)
			{
				WireDither(Material, Plan);
			}
			if (Plan.bSetMasked)
			{
				Material->BlendMode = BLEND_Masked;
				Material->bHasPixelAnimation = false;
			}
			if (Plan.bSetNanite)
			{
				// Set directly: the usage setter compiles shaders straight away, and the batch update compiles again.
				Material->bUsedWithNanite = true;
			}
			++Stage.Changed;

			Batch.Add(Material);
			if (Batch.Num() >= BatchSize)
			{
				UpdateBatch();
			}
		}
		UpdateBatch();
	}

	if (Stage.State != EMCStageState::Cancelled && bCanConvert)
	{
		TArray<FString> Overridden;
		RunAssetPass<UMaterialInstanceConstant>(Stage, Instances,
			[](UMaterialInstanceConstant* Instance)
			{
				return Instance->BasePropertyOverrides.bOverride_BlendMode && Instance->BasePropertyOverrides.BlendMode == BLEND_Translucent;
			},
			[&](UMaterialInstanceConstant* Instance)
			{
				Instance->Modify();
				Instance->BasePropertyOverrides.BlendMode = BLEND_Masked;
				UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
				Overridden.Add(Instance->GetName());
			});
		AddListWarning(Context.Warnings, TEXT("material instances overridden from translucent to masked; their parent decides the opacity mask"), Overridden);
	}

	if (Stage.Changed > 0)
	{
		FEditorDelegates::RefreshEditor.Broadcast();
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	}

	AddListWarning(Context.Warnings, TEXT("materials left translucent because their opacity mask is already driven by something else"), Blocked);
	AddListWarning(Context.Warnings, TEXT("materials had no opacity source, so a constant drives the dither"), ConstantFallback);
	AddListWarning(Context.Warnings, TEXT("materials where the dither was only partly connected"), Incomplete);
	SetCountDetail(Stage);
}

void FinishBuilds(FMCStageResult& Stage)
{
	FAssetCompilingManager& Manager = FAssetCompilingManager::Get();
	const int32 Remaining = Manager.GetNumRemainingAssets();
	Stage.Total = Remaining;
	Stage.Detail = FString::Printf(TEXT("%d assets were still building"), Remaining);
	if (Remaining <= 0)
	{
		return;
	}

	// Distance fields and mesh cards are queued after each mesh build finishes, so a finished
	// mesh batch can still leave them pending. Waiting on each queue in turn shows which one the
	// time went to.
	TArray<FString> Parts;
	auto Wait = [&Parts](const TCHAR* Label, int32 Count, TFunctionRef<void()> Finish)
	{
		if (Count <= 0)
		{
			return;
		}
		const double Start = FPlatformTime::Seconds();
		Finish();
		Parts.Add(FString::Printf(TEXT("%s %d (%.1f s)"), Label, Count, FPlatformTime::Seconds() - Start));
	};

	FStaticMeshCompilingManager& MeshCompiler = FStaticMeshCompilingManager::Get();
	Wait(TEXT("meshes"), MeshCompiler.GetNumRemainingMeshes(), [&MeshCompiler]() { MeshCompiler.FinishAllCompilation(); });

	FTextureCompilingManager& TextureCompiler = FTextureCompilingManager::Get();
	Wait(TEXT("textures"), TextureCompiler.GetNumRemainingTextures(), [&TextureCompiler]() { TextureCompiler.FinishAllCompilation(); });

	if (GDistanceFieldAsyncQueue)
	{
		Wait(TEXT("distance fields"), GDistanceFieldAsyncQueue->GetNumOutstandingTasks(), []() { GDistanceFieldAsyncQueue->BlockUntilAllBuildsComplete(); });
	}
	if (GCardRepresentationAsyncQueue)
	{
		Wait(TEXT("mesh cards"), GCardRepresentationAsyncQueue->GetNumOutstandingTasks(), []() { GCardRepresentationAsyncQueue->BlockUntilAllBuildsComplete(); });
	}
	if (GShaderCompilingManager)
	{
		Wait(TEXT("shader jobs"), GShaderCompilingManager->GetNumRemainingJobs(), []() { GShaderCompilingManager->FinishAllCompilation(); });
	}
	Wait(TEXT("other"), Manager.GetNumRemainingAssets(), [&Manager]() { Manager.FinishAllCompilation(); });

	if (Parts.Num() > 0)
	{
		Stage.Detail += TEXT(": ") + FString::Join(Parts, TEXT(", "));
	}
	UE_LOG(LogModelConstructor, Log, TEXT("%s"), *Stage.Detail);
}

void RunReport(const FContext& Context, FMCImportResult& Result, FMCStageResult& Stage)
{
	const UMCSettings& S = Context.Settings;
	const TArray<UStaticMesh*> Meshes = LoadAssets<UStaticMesh>(Context.Folder);
	const TArray<UMaterial*> Materials = LoadAssets<UMaterial>(Context.Folder);
	const TArray<UTexture2D*> Textures = LoadAssets<UTexture2D>(Context.Folder);
	const int32 InstanceCount = FindAssets(Context.Folder, UMaterialInstanceConstant::StaticClass()).Num();

	TArray<FString> NaniteOff;
	TArray<FString> SimpleLeft;
	TArray<FString> HugeMeshes;
	TArray<FString> FarMeshes;
	for (UStaticMesh* Mesh : Meshes)
	{
		if (S.bEnableNanite && !Mesh->NaniteSettings.bEnabled)
		{
			NaniteOff.Add(Mesh->GetName());
		}
		const UBodySetup* Body = Mesh->GetBodySetup();
		const int32 ExpectedShapes = S.CollisionMode == EMCCollisionMode::SimpleBox ? 1 : 0;
		if (S.CollisionMode != EMCCollisionMode::KeepImported && Body && Body->AggGeom.GetElementCount() > ExpectedShapes)
		{
			SimpleLeft.Add(Mesh->GetName());
		}
		const int64 Triangles = SourceTriangles(Mesh);
		if (Triangles > LargeMeshTriangles)
		{
			HugeMeshes.Add(FString::Printf(TEXT("%s (%lld)"), *Mesh->GetName(), Triangles));
		}
		if (Mesh->GetBounds().BoxExtent.GetMax() > LargeMeshExtentCm)
		{
			FarMeshes.Add(Mesh->GetName());
		}
	}

	TArray<FString> Translucent;
	int32 ScheduleMaterials = 0;
	const FString ScheduleFunction = S.ScheduleFunctionNameContains.TrimStartAndEnd();
	for (UMaterial* Material : Materials)
	{
		if (Material->BlendMode == BLEND_Translucent)
		{
			Translucent.Add(Material->GetName());
		}
		ScheduleMaterials += CallsFunction(Material, ScheduleFunction) ? 1 : 0;
	}

	// Instances whose parent lives outside the folder are not edited by the material pass, so their
	// parent can still be translucent or missing the Nanite usage.
	TArray<FString> ExternalTranslucent;
	TArray<FString> ExternalNoNanite;
	{
		TSet<UMaterial*> CheckedParents;
		for (UMaterialInstanceConstant* Instance : LoadAssets<UMaterialInstanceConstant>(Context.Folder))
		{
			UMaterial* Base = Instance->GetMaterial();
			if (!Base || IsInFolder(Base, Context.Folder))
			{
				continue;
			}
			if (Instance->GetBlendMode() == BLEND_Translucent)
			{
				ExternalTranslucent.Add(Instance->GetName());
			}
			if (S.bEnableNanite && S.bSetUsedWithNanite && !CheckedParents.Contains(Base))
			{
				CheckedParents.Add(Base);
				if (!UMaterialEditingLibrary::HasMaterialUsage(Base, MATUSAGE_Nanite))
				{
					ExternalNoNanite.Add(Base->GetPathName());
				}
			}
		}
	}

	TArray<FString> OverCap;
	if (S.MaxTextureSize > 0)
	{
		for (UTexture2D* Texture : Textures)
		{
			if (!Texture->Source.IsValid())
			{
				continue;
			}
			const int64 Largest = FMath::Max<int64>(Texture->Source.GetSizeX(), Texture->Source.GetSizeY());
			if (Largest > S.MaxTextureSize && (Texture->MaxTextureSize <= 0 || Texture->MaxTextureSize > S.MaxTextureSize))
			{
				OverCap.Add(Texture->GetName());
			}
		}
	}

	int32 LevelActors = 0;
	int32 SceneActors = 0;
	int32 OwnSceneActors = 0;
	int32 MeshActors = 0;
	FBox Bounds(ForceInit);
	const FSoftObjectPath ScenePath(Result.ScenePath);

	UUnrealEditorSubsystem* EditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>() : nullptr;
	UWorld* World = EditorSubsystem ? EditorSubsystem->GetEditorWorld() : nullptr;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			++LevelActors;
		}
		for (TActorIterator<ADatasmithSceneActor> It(World); It; ++It)
		{
			++SceneActors;
			if (!It->Scene || FSoftObjectPath(It->Scene.Get()) != ScenePath)
			{
				continue;
			}
			++OwnSceneActors;
			for (const TPair<FName, TSoftObjectPtr<AActor>>& Pair : It->RelatedActors)
			{
				const AActor* Actor = Pair.Value.Get();
				if (Actor && Actor->FindComponentByClass<UStaticMeshComponent>())
				{
					++MeshActors;
					Bounds += Actor->GetComponentsBoundingBox(/*bNonColliding*/ true);
				}
			}
		}
	}

	int32 ScheduleActors = 0;
	int32 TaggedActors = 0;
	const FString ScheduleClass = S.ScheduleActorClassName.TrimStartAndEnd();
	if (World && !ScheduleClass.IsEmpty())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetClass()->GetName() == ScheduleClass)
			{
				++ScheduleActors;
			}
			if (S.bScheduleAnimationSupport && S.ScheduleActorTags.ContainsByPredicate([&It](const FName& Tag) { return It->ActorHasTag(Tag); }))
			{
				++TaggedActors;
			}
		}
	}
	// Schedule checks only apply to models that use schedule materials, so other models get no schedule warnings.
	Result.Counters.Add(TEXT("ScheduleMaterials"), ScheduleMaterials);
	if (S.bScheduleAnimationSupport)
	{
		Result.Counters.Add(TEXT("ScheduleActors"), ScheduleActors);
		Result.Counters.Add(TEXT("ScheduleTaggedActors"), TaggedActors);
		if (ScheduleMaterials > 0 && OwnSceneActors > 0 && ScheduleActors == 0)
		{
			AddWarning(Context.Warnings, FString::Printf(TEXT("%d materials use schedule animation, but there is no %s actor in the level. The importer drops it when the schedule plugin is not enabled in this project; enable that plugin and import again"),
				ScheduleMaterials, *ScheduleClass));
		}
	}
	else if (ScheduleActors > 0 || ScheduleMaterials > 0)
	{
		AddWarning(Context.Warnings, TEXT("This model uses schedule animation, but Schedule Animation Support is off. Turn it on for the next import if the schedule should still play"));
	}

	if (LevelActors > LargeLevelActorCount)
	{
		AddWarning(Context.Warnings, FString::Printf(TEXT("The level has %d actors. Levels this large are slow to save and load and can reach engine size limits; consider exporting the model in parts"), LevelActors));
	}

	if (OwnSceneActors == 0)
	{
		AddWarning(Context.Warnings, TEXT("The open level has no Datasmith scene actor for this model; level checks were skipped"));
	}
	else if (OwnSceneActors > 1)
	{
		AddWarning(Context.Warnings, FString::Printf(TEXT("The open level has %d scene actors for this model; expected 1"), OwnSceneActors));
	}

	AddListWarning(Context.Warnings, TEXT("meshes with Nanite off"), NaniteOff);
	AddListWarning(Context.Warnings, TEXT("meshes still with simple collision shapes"), SimpleLeft);
	AddListWarning(Context.Warnings, TEXT("materials still translucent"), Translucent);
	AddListWarning(Context.Warnings, TEXT("material instances are translucent through a parent material outside the model folder, which is not converted"), ExternalTranslucent);
	AddListWarning(Context.Warnings, TEXT("parent materials outside the model folder are not set Used With Nanite. The editor sets it the first time they draw, but a packaged build can show the default material until the parent is saved with it"), ExternalNoNanite);
	AddListWarning(Context.Warnings, TEXT("textures still over the size cap"), OverCap);
	AddListWarning(Context.Warnings, FString::Printf(TEXT("meshes have over %lld triangles; very large meshes are slow to build and save"), LargeMeshTriangles), HugeMeshes);
	AddListWarning(Context.Warnings, TEXT("meshes are over 100 km across, which usually means stray geometry far from the model"), FarMeshes);

	Result.Counters.Add(TEXT("LevelActors"), LevelActors);
	Result.Counters.Add(TEXT("SceneActors"), SceneActors);
	Result.Counters.Add(TEXT("ModelMeshActors"), MeshActors);
	Result.Counters.Add(TEXT("Meshes"), Meshes.Num());
	Result.Counters.Add(TEXT("Materials"), Materials.Num());
	Result.Counters.Add(TEXT("MaterialInstances"), InstanceCount);
	Result.Counters.Add(TEXT("Textures"), Textures.Num());
	Result.Counters.Add(TEXT("NaniteOff"), NaniteOff.Num());
	Result.Counters.Add(TEXT("SimpleCollisionLeft"), SimpleLeft.Num());
	Result.Counters.Add(TEXT("Translucent"), Translucent.Num());
	Result.Counters.Add(TEXT("TexturesOverCap"), OverCap.Num());
	Result.Counters.Add(TEXT("ExternalTranslucentInstances"), ExternalTranslucent.Num());
	Result.Counters.Add(TEXT("ExternalParentsWithoutNanite"), ExternalNoNanite.Num());
	Result.Bounds = Bounds;

	Stage.Total = Meshes.Num() + Materials.Num() + InstanceCount + Textures.Num();
	Stage.Detail = FString::Printf(TEXT("%d meshes, %d materials, %d instances, %d textures, %d mesh actors"),
		Meshes.Num(), Materials.Num(), InstanceCount, Textures.Num(), MeshActors);
	if (Bounds.IsValid)
	{
		const FVector Size = Bounds.GetSize() / 100.0;
		Stage.Detail += FString::Printf(TEXT(", %.1f x %.1f x %.1f m"), Size.X, Size.Y, Size.Z);
	}
}

void CheckSelfContained(const FString& Folder, TArray<FString>& Warnings)
{
	IAssetRegistry& AssetRegistry = MCModels::Registry();
	AssetRegistry.ScanPathsSynchronous({ Folder }, /*bForceRescan*/ true);

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Folder));
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	TSet<FName> Seen;
	TArray<FString> Offenders;
	for (const FAssetData& Asset : Assets)
	{
		if (Seen.Contains(Asset.PackageName))
		{
			continue;
		}
		Seen.Add(Asset.PackageName);

		TArray<FName> Dependencies;
		AssetRegistry.GetDependencies(Asset.PackageName, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
		for (const FName& Dependency : Dependencies)
		{
			if (Dependency.ToString().StartsWith(TEXT("/Game/")))
			{
				Offenders.Add(FString::Printf(TEXT("%s -> %s"), *Asset.PackageName.ToString(), *Dependency.ToString()));
			}
		}
	}
	AddListWarning(Warnings, TEXT("references to project content, so the folder cannot be copied on its own"), Offenders);
}
}

#undef LOCTEXT_NAMESPACE
