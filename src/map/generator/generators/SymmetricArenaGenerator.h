// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct SymmetricArenaOptions
{
	int centreSize, moatWidth, causewayWidth, causeways, lakes, richness;
	bool moat, orchardStone;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit SymmetricArenaOptions(const GenerationRequest &r);
};
GeneratorDefinition symmetricArenaDefinition();
