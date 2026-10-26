// Copyright 2026 GeoMappit. Licensed under the Apache License, Version 2.0. See LICENSE in the plugin root.

#pragma once

#include "CoreMinimal.h"

struct FSlateBrush;

/** Logos drawn on the Guide page, decoded from data built into the module. */
namespace MCLogos
{
	enum class ELogo : uint8
	{
		Developer,
		ScheduleScenes,
	};

	/** Decodes the logo on first use. Null if it could not be decoded or Slate is not running. */
	const FSlateBrush* Get(ELogo Logo);

	/** Frees the decoded logos. Call before Slate shuts down. */
	void Release();
}
