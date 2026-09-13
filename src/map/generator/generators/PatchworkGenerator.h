// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct PatchworkOptions
{
	int homeSize, borderRoughness, smoothing;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit PatchworkOptions(const GenerationRequest &r);
};
GeneratorDefinition patchworkDefinition();
