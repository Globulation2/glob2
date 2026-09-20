// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct LastTreelineOptions
{
	int depth;
	int wheat, wood, stone, algae, fruit;
	explicit LastTreelineOptions(const GenerationRequest &);
};
GeneratorDefinition lastTreelineDefinition();
