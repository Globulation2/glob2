// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct OrchardCommonsOptions
{
	int spacing;
	int wheat, wood, stone, algae, fruit;
	explicit OrchardCommonsOptions(const GenerationRequest &);
};
GeneratorDefinition orchardCommonsDefinition();
