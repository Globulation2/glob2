// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct FjordContinentOptions
{
	int continentSize, roughness, fjordWidth, resourceIslands;
	explicit FjordContinentOptions(const GenerationRequest &r);
};
GeneratorDefinition fjordContinentDefinition();
