// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct TidalFlatsOptions
{
	int homeIslandSize, extraIslands, sandbars, tidePools, lagoons, coastRoughness;
	bool centralIsland;
	// 1 neap, 2 mean, 3 spring: the request's choice, or the seed's when it asks for Random (0).
	int tide;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit TidalFlatsOptions(const GenerationRequest &r);
};
GeneratorDefinition tidalFlatsDefinition();
