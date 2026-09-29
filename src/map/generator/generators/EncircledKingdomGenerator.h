// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct EncircledKingdomOptions
{
	int plan, gateWidth, farmland;
	int wheat, wood, stone, algae, fruit;
	explicit EncircledKingdomOptions(const GenerationRequest &);
};
GeneratorDefinition encircledKingdomDefinition();
