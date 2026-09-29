// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct UniformOptions
{
	explicit UniformOptions(const GenerationRequest &r) {}
};
GeneratorDefinition uniformDefinition();
