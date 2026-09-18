// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct TugOptions
{
	int prizes;    // how many prizes are strung through the march
	int march;     // how wide the no-man's land between territories is, in tiles
	int levelling; // how hard the search equalises the walk to each prize, 0 to 100
	int lakes;     // the share of the country under water, percent
	int wheat, wood, stone, fruit; // percentages of the default amounts
	explicit TugOptions(const GenerationRequest &r);
};
GeneratorDefinition tugDefinition();
