// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#include "MCLogos.h"
#include "MCLogoData.h"
#include "MCPasses.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

namespace
{
	constexpr int32 LogoCount = 2;
	TSharedPtr<FSlateDynamicImageBrush> Brushes[LogoCount];
	bool bTried[LogoCount] = {};

	TSharedPtr<FSlateDynamicImageBrush> Decode(const uint8* Png, int64 Size, const TCHAR* Name)
	{
		IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = Wrappers.CreateImageWrapper(EImageFormat::PNG);
		TArray<uint8> Pixels;
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Png, Size) || !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Pixels))
		{
			UE_LOG(LogModelConstructor, Warning, TEXT("Could not decode the %s logo."), Name);
			return nullptr;
		}
		// The texture name must be unique among dynamic Slate resources.
		const FVector2D PixelSize(static_cast<double>(Wrapper->GetWidth()), static_cast<double>(Wrapper->GetHeight()));
		return FSlateDynamicImageBrush::CreateWithImageData(FName(*FString::Printf(TEXT("ModelConstructor.Logo.%s"), Name)), PixelSize, Pixels);
	}
}

namespace MCLogos
{
	const FSlateBrush* Get(ELogo Logo)
	{
		const int32 Index = static_cast<int32>(Logo);
		if (Index < 0 || Index >= LogoCount || !FSlateApplication::IsInitialized())
		{
			return nullptr;
		}
		if (!bTried[Index])
		{
			bTried[Index] = true;
			Brushes[Index] = Logo == ELogo::Developer
				? Decode(MCLogoData::Developer, sizeof(MCLogoData::Developer), TEXT("Developer"))
				: Decode(MCLogoData::ScheduleScenes, sizeof(MCLogoData::ScheduleScenes), TEXT("ScheduleScenes"));
		}
		return Brushes[Index].Get();
	}

	void Release()
	{
		for (int32 Index = 0; Index < LogoCount; ++Index)
		{
			Brushes[Index].Reset();
			bTried[Index] = false;
		}
	}
}
