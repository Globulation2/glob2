// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GenerationResult.h"
#include "GeneratorRegistry.h"
class Game;
class GenerationService
{
	const GeneratorRegistry &registry;

  public:
	explicit GenerationService(const GeneratorRegistry &registry = GeneratorRegistry::builtins())
		: registry(registry)
	{
	}
	GenerationResult generate(Game &freshGame, const GenerationRequest &request) const;
};
