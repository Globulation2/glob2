// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct CombOptions
{
	int peninsulas, wheat, wood, stone, algae, fruit;
	explicit CombOptions(const GenerationRequest &);
};
GeneratorDefinition combDefinition();
