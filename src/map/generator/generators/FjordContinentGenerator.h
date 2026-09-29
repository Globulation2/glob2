// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct FjordContinentOptions
{
	int continentSize, roughness, fjordWidth, resourceIslands, lakeSize, lakeConnected;
	bool sandyLakeShore, bankDeposits;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit FjordContinentOptions(const GenerationRequest &r);
};
GeneratorDefinition fjordContinentDefinition();
