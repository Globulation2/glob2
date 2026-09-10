// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationContext.h"
#include "GenerationService.h"
#include "LegacyGenerationDescriptor.h"
#include <optional>
class MapGenerator
{
  public:
	bool generateMap(Game &game, const MapGenerationDescriptor &d,
					 std::optional<Uint32> seed = std::nullopt)
	{
		return bool(GenerationService().generate(
			game, fromLegacyDescriptor(d, seed ? *seed : GenerationContext::randomSeed())));
	}
	bool generateMap(Game &game, const GenerationRequest &r)
	{
		return bool(GenerationService().generate(game, r));
	}
};
