// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct VulturesOptions
{
	int homeSize, lakes, lakeSize, wheat, wood;
	explicit VulturesOptions(const GenerationRequest &);
};
GeneratorDefinition vulturesDefinition();
