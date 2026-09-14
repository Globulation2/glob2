// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct AmphitheatreOptions
{
	int rings, rampWidth, pitSize, terraceWidth, baySize, borderWall, towers, towerCount;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit AmphitheatreOptions(const GenerationRequest &r);
};
GeneratorDefinition amphitheatreDefinition();
