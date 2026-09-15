// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"

// Geometric controls describe lava, not colony slots. Resource values are percentages;
// the structural rock and the guaranteed starter crops are intentionally unscaled.
struct LavaShieldOptions
{
	int tongues, longTongues, branching, rimWidth;
	bool islets; // a couple of swimming-prize islets out in the ocean where the torus wraps
	int wheat, wood, stone, algae, fruit;
	explicit LavaShieldOptions(const GenerationRequest &);
};
GeneratorDefinition lavaShieldDefinition();
