// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct MarchesOptions
{
	int homeSize, marchWidth, borderRoughness;
	bool orchards;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit MarchesOptions(const GenerationRequest &r);
};
GeneratorDefinition marchesDefinition();
