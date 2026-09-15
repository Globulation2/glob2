// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GeneratorControls.h"
#include "StartQuality.h"
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
	// How the lobby ranks this generator's candidate seeds (StartQuality.h). The defaults suit most
	// maps; a map that is cramped, crowded or far-flung on purpose (building room scarce by design,
	// colonies meant to touch) says so here, so the ranking rewards what the map is meant to be rather
	// than marking every candidate down for it.
	MapGeneration::StartQualityWeights qualityWeights{};
	MapGeneration::StartQualityScale qualityScale{};
	// How many WORKER units every colony starts with, when that is not the lobby's shared "Starting
	// workers" control (1 to 8). A premade-base landscape (shared/Bases.h) owns its count through a
	// control of its own and ignores the lobby's value; the structural check after generation
	// (validateGeneratedWorld) compares against this instead. nullptr means request.nbWorkers, as
	// every other landscape. Declared last so every existing positional initialiser stays valid.
	int (*startingWorkers)(const GenerationRequest &) = nullptr;
};
