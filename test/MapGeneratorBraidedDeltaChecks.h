// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BraidedDeltaGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include "GenerationValidation.h"
#include "Room.h"
#include "Grid.h"
#include "Growth.h"
#include "Pipeline.h"
#include <cassert>

// Exercise the production service and final-world validator, including deliberate damage.
// A successful seed alone would not show that the validator notices lost expansion ground.
inline void braidedDeltaChecks()
{
	const auto definition = braidedDeltaDefinition();
	GenerationRequest request;
	request.setMethodDefaults(definition.legacyId);
	request.wDec = request.hDec = 7;
	request.nbTeams = 4;
	request.options["braid-count"] = 2;
	request.options["rejoining-frequency"] = 3;
	for (unsigned seed : {7u, 101u, 22001u, 53006u, 711038u, 711131u, 711226u})
	{
		request = GenerationRequest();
		request.setMethodDefaults(definition.legacyId);
		request.wDec = request.hDec = 7;
		request.nbTeams = 4;
		request.options["braid-count"] = 2;
		request.options["rejoining-frequency"] = 3;
		request.seed = seed;
		// Bulk seed 711038 exhausts the wood kit's bank space when wheat and the
		// permanent resources are abundant. Emergency topups must not seed a town.
		if (seed == 711038u)
		{
			request.nbWorkers = 8;
			request.options["wheat-amount"] = 300;
			request.options["wood-amount"] = 0;
			request.options["stone-amount"] = 300;
			request.options["algae-amount"] = 0;
			request.options["fruit-amount"] = 300;
		}
		// The same bank-reservation failure also occurred on dense medium and large
		// islands. Keep one fixture of each size, including the opposite crop extreme.
		if (seed == 711131u || seed == 711226u)
		{
			const bool large = seed == 711226u;
			request.wDec = request.hDec = large ? 9 : 8;
			request.nbTeams = 12;
			request.nbWorkers = 8;
			request.options["braid-count"] = large ? 5 : 4;
			request.options["island-size"] = large ? 48 : 32;
			request.options["crossing-spacing"] = large ? 96 : 64;
			request.options["wheat-amount"] = large ? 0 : 300;
			request.options["wood-amount"] = large ? 300 : 0;
			request.options["stone-amount"] = large ? 300 : 0;
			request.options["algae-amount"] = large ? 300 : 0;
			request.options["fruit-amount"] = 300;
		}
		Game game(nullptr);
		const auto result = GenerationService().generate(game, request, true);
		assert(result);
		GenerationContext context(request);
		assert(definition.validateWorld(game, context).empty());
		// Saturate the eight-connected grass reached by existing wheat/wood. This is a
		// conservative growth envelope, not a growth-speed simulation: it deliberately ignores
		// fertility so an initially clear route cannot pass merely by waiting longer to clog.
		// Sand town rims keep their interiors out of the flood. Revision 1 fails here because
		// its fords have no permanent approaches through the bank crops.
		const MapGeneration::Torus torus(game.map);
		const auto overgrown = MapGeneration::cropSpreadEnvelope(game.map);
		for (int i : overgrown.visited)
			if (game.map.isResourceAllowed(i % torus.w, i / torus.w, WHEAT))
				game.map.setResource(i % torus.w, i / torus.w, WHEAT, 1);
		assert(MapGeneration::walkFromFirstColony(game.map, request.nbTeams, "the overgrown delta",
												  "over its ford approaches")
				   .error.empty());
		assert(definition.validateWorld(game, context).empty());
		// Fill all surviving grass with stone. Terrain and colony count still match, but the
		// usable-island contract must fail after furnishing, not just pass a terrain flood.
		for (int y = 0; y < game.map.getH(); ++y)
			for (int x = 0; x < game.map.getW(); ++x)
				if (game.map.isResourceAllowed(x, y, STONE))
					game.map.setResource(x, y, STONE, 1);
		assert(!definition.validateWorld(game, context).empty());
	}
	// Restore the smallest layout for request-boundary checks after the large fixture.
	request.wDec = request.hDec = 7;
	request.nbTeams = 4;
	request.options["island-size"] = 32;
	// Too many braids, too many colonies and undersized maps fail before mutating a Game.
	request.options["braid-count"] = 5;
	assert(!validateGenerationRequest(request, definition).empty());
	request.options["braid-count"] = 2;
	request.nbTeams = 5;
	assert(!validateGenerationRequest(request, definition).empty());
	request.nbTeams = 1;
	request.wDec = 6;
	assert(!validateGenerationRequest(request, definition).empty());
}
