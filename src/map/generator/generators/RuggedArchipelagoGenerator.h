// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct RuggedArchipelagoOptions
{
	int island_size, beach_size;
	explicit RuggedArchipelagoOptions(const GenerationRequest &r)
		: island_size(r.option("island-size")), beach_size(r.option("beach-size"))
	{
	}
};
GeneratorDefinition ruggedArchipelagoDefinition();
