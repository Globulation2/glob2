// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct ContestedCommonsOptions
{
	int homeSize, commonsSize, moatWidth, bridgeCount;
	explicit ContestedCommonsOptions(const GenerationRequest &r);
};
GeneratorDefinition contestedCommonsDefinition();
