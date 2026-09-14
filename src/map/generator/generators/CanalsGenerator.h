// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct CanalsOptions
{
	int blockShape, blockSize, canalWidth, warp, extraBridges, towers, towerCount;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit CanalsOptions(const GenerationRequest &r);
};
GeneratorDefinition canalsDefinition();
