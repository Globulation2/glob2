// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GeneratorControls.h"
#include <vector>
class Game;
struct GenerationContext;
struct GeneratorDefinition
{
	const char *id;
	int legacyId;
	const char *nameKey;
	unsigned revision;
	bool editorOnly;
	std::vector<GeneratorControl> controls;
	bool (*generate)(Game &, GenerationContext &);
	bool hasStartingColonies = true;
	// Optional pure checks for relationships between controls and generator-specific topology.
	std::string (*validateRequest)(const GenerationRequest &) = nullptr;
	std::string (*validateWorld)(const Game &, const GenerationContext &) = nullptr;
};
