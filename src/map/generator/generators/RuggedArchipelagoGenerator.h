// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct RuggedArchipelagoOptions
{
	int island_size, beach_size;
	bool extra_deposit;
	int wheat, wood, stone, algae; // percentages of the default amounts
	explicit RuggedArchipelagoOptions(const GenerationRequest &r)
		: island_size(r.option("island-size")), beach_size(r.option("beach-size")),
		  extra_deposit(r.option("extra-deposit") != 0), wheat(r.option("wheat-amount")),
		  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
		  algae(r.option("algae-amount"))
	{
	}
};
GeneratorDefinition ruggedArchipelagoDefinition();
