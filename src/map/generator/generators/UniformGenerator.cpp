// SPDX-License-Identifier: GPL-3.0-or-later
#include "UniformGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include <algorithm>
using namespace MapGeneration;
// Uniform terrain (id 0): the map editor's blank canvas, not a game map. It fills the whole map
// with the one terrain picked on the new-map screen (water, sand or grass) and adds a single team
// with no swarm, so a designer starts from nothing. It is editor-only (the definition's true below)
// and has no starting colonies (the final false), so it is offered only in the editor, and
// validation and the golden test expect exactly one team from it instead of one per colony.
static bool generate(Game &game, GenerationContext &context)
{
	game.map.makeHomogenMap(context.request.terrainType);
	game.addTeam();
	return true;
}

GeneratorDefinition uniformDefinition()
{
	return {"uniform",
			0,
			"uniform terrain",
			1,
			true,
			{

			},
			generate,
			false};
}
