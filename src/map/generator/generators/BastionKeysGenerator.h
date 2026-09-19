// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct BastionKeysOptions
{
	int homeSize, plantationSize, outerIslands;
	int wheat, wood, stone, algae, fruit;
	explicit BastionKeysOptions(const GenerationRequest &);
};
GeneratorDefinition bastionKeysDefinition();
