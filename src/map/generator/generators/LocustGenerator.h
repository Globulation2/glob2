// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct LocustOptions
{
	int homeSize, lakes, lakeSize, wheat, wood;
	explicit LocustOptions(const GenerationRequest &);
};
GeneratorDefinition locustDefinition();
