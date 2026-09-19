// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct PortageLakesOptions
{
	int elongation, depth, trails;
	int wheat, wood, stone, algae, fruit;
	explicit PortageLakesOptions(const GenerationRequest &);
};
GeneratorDefinition portageLakesDefinition();
