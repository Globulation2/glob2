// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct RainShadowOptions
{
	int ridges, slant, ridgeThickness, passSpacing, passWidth, leeWidth, homeSize;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit RainShadowOptions(const GenerationRequest &r);
};
GeneratorDefinition rainShadowDefinition();
