// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GeneratorControls.h"
#include "StartQuality.h"
#include <string>
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
	// Catalog facets for browsing (the landscape picker's tag filters), each "category:value"
	// (e.g. "terrain:natural", "feature:river", "style:wide-open"): what a player scanning a
	// shelf of landscapes would use to narrow the list. Set by each generator's own
	// ...Definition() alongside its other registration fields (controls, revision, ...) rather
	// than in a separate lookup table, so tagging a generator stays colocated with the rest of
	// its registration and there is nothing extra to keep in sync when adding one. Required for
	// every non-editor-only generator (GeneratorRegistry's constructor checks it).
	std::vector<std::string> tags;
};
