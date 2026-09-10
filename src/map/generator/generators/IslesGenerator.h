// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct IslesOptions
{
	int island_size, bridge_width;
	explicit IslesOptions(const GenerationRequest &r)
		: island_size(r.option("island-size")), bridge_width(r.option("bridge-width"))
	{
	}
};
GeneratorDefinition islesDefinition();
