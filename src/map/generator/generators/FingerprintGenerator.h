// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct FingerprintOptions
{
	int wavelength, pattern, barrier, grain, homeSize;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit FingerprintOptions(const GenerationRequest &r);
};
GeneratorDefinition fingerprintDefinition();
