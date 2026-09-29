// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct FortsOptions
{
	int homeSize, gateWidth, riverWidth, lakes, villageSize;
	int wheat, wood, stone, algae, fruit;
	explicit FortsOptions(const GenerationRequest &r);
};
GeneratorDefinition fortsDefinition();
