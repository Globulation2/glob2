// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct WhoAteTheMapOptions
{
	int appetite;
	int wheat, wood, stone, algae, fruit;
	explicit WhoAteTheMapOptions(const GenerationRequest &);
};
GeneratorDefinition whoAteTheMapDefinition();
