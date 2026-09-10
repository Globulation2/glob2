// SPDX-License-Identifier: GPL-3.0-or-later
#include "UniformGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include <algorithm>
using namespace MapGeneration;
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
