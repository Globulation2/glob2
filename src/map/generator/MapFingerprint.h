// SPDX-License-Identifier: GPL-3.0-or-later
// The fingerprint sim-diff and repeatability checks compare generated maps by. Test-only,
// and free of doctest so the standalone MapGeneratorGoldenTest harness can use it too.
#pragma once
#include "Game.h"
#include "Map.h"
#include "Team.h"
#include <cstdint>

/// Everything a generator decides that a player can see: terrain, resources and where each
/// colony starts. Two maps with the same fingerprint are the same map for fingerprint comparisons.
inline std::uint64_t mapFingerprint(const Game &game)
{
	std::uint64_t hash = 14695981039346656037ull;
	auto add = [&](unsigned v)
	{
		hash ^= v;
		hash *= 1099511628211ull;
	};
	add(game.map.getW());
	add(game.map.getH());
	for (int y = 0; y < game.map.getH(); ++y)
		for (int x = 0; x < game.map.getW(); ++x)
		{
			add(game.map.vertexTerrainAt(x, y));
			// Keep the historical empty marker in geometry fingerprints; storage
			// widening alone must not invalidate established generator worlds.
			const auto resource=game.map.getResource(x,y).type;
			add(resource==NO_RES_TYPE ? 255u : resource);
			add(game.map.getResource(x, y).amount);
		}
	add(game.teamsCount());
	for (int i = 0; i < game.teamsCount(); ++i)
	{
		add(game.teams[i]->startPosX);
		add(game.teams[i]->startPosY);
	}
	return hash;
}
