// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexWater.h"
#include "CortexTypes.h"

#include "Player.h"
#include "Game.h"
#include "team/Team.h"
#include "building/Building.h"
#include "game/entities/BuildingType.h"
#include "map/Map.h"
#include "Ressource.h"

#include <vector>

namespace Cortex
{
	namespace
	{
		// Count tiles reachable from (cx, cy) by an 8-connected flood-fill, bounded
		// to a Chebyshev radius of CORTEX_SWIM_REACH_RADIUS around the anchor and to
		// tiles that pass the engine's ground-unit space predicate for the given
		// swim ability. isHardSpaceForGroundUnit ignores transient ground units (so
		// the count is stable cycle-to-cycle and depends only on terrain / buildings
		// / resources / forbidden), and its canSwim gate is exactly what makes water
		// an obstacle (false) or passable (true) — the one bit that differs between
		// the land and swim passes.
		//
		// The returned COUNT is independent of visitation order (it is the size of a
		// connected component), so a plain FIFO is deterministic without any tie-
		// breaking. Warp-safe via Map's coordinate normalization.
		int countReach(Map& map, const Team* team, int cx, int cy, bool canSwim)
		{
			const int w = map.getW();
			const int h = map.getH();
			const Uint32 me = team->me;
			const int R = CORTEX_SWIM_REACH_RADIUS;

			std::vector<char> visited(static_cast<size_t>(w) * h, 0);
			std::vector<int> frontier; // queue of flattened indices, drained by head.
			frontier.reserve(256);

			// Seed: the passable tiles in the 8-neighbourhood of the anchor. The
			// anchor itself is a building tile (occupied), so we never enqueue it.
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
				{
					if (dx == 0 && dy == 0)
						continue;
					const int nx = map.normalizeX(cx + dx);
					const int ny = map.normalizeY(cy + dy);
					const size_t idx = static_cast<size_t>(ny) * w + nx;
					if (visited[idx])
						continue;
					if (!map.isHardSpaceForGroundUnit(nx, ny, canSwim, me))
						continue;
					visited[idx] = 1;
					frontier.push_back(static_cast<int>(idx));
				}

			int count = 0;
			for (size_t head = 0; head < frontier.size(); head++)
			{
				const int cur = frontier[head];
				const int x = cur % w;
				const int y = cur / w;
				count++;

				for (int dy = -1; dy <= 1; dy++)
					for (int dx = -1; dx <= 1; dx++)
					{
						if (dx == 0 && dy == 0)
							continue;
						const int nx = map.normalizeX(x + dx);
						const int ny = map.normalizeY(y + dy);
						// Stay within the colony vicinity ("relative proximity"): a
						// far-off lake the colony will never work must not by itself
						// argue for a pool.
						if (map.warpDistMax(cx, cy, nx, ny) > R)
							continue;
						const size_t idx = static_cast<size_t>(ny) * w + nx;
						if (visited[idx])
							continue;
						if (!map.isHardSpaceForGroundUnit(nx, ny, canSwim, me))
							continue;
						visited[idx] = 1;
						frontier.push_back(static_cast<int>(idx));
					}
			}
			return count;
		}
	} // namespace

	SwimAssessment assessSwim(Player* player)
	{
		SwimAssessment out;
		out.algaeDiscovered = 0;
		out.landReach = 0;
		out.waterReach = 0;

		if (player == NULL || player->team == NULL)
			return out;
		Team* team = player->team;
		Game* game = team->game;
		if (game == NULL)
			return out;
		Map& map = game->map;
		const int w = map.getW();
		const int h = map.getH();

		// (1) Algae: any takeable ALGA tile we have legitimately discovered. Algae
		// grows on water and is harvestable only by swimmers, so a revealed algae
		// tile is a direct reason to train SWIM. FOW-gated (team->allies vision),
		// exactly like the placement / fruit scans — never unfogged truth.
		for (int x = 0; x < w && out.algaeDiscovered == 0; x++)
			for (int y = 0; y < h; y++)
				if (map.isRessourceTakeable(x, y, ALGA)
				 && map.isMapDiscovered(x, y, team->allies))
				{
					out.algaeDiscovered = 1;
					break;
				}

		// (2) Reach: anchor the flood-fill on our first live, real (non-virtual)
		// building in index order — deterministic, and for a clustered economy
		// colony it sits at the colony's heart. Without a real building there is
		// nothing to anchor on (and the reach signal is meaningless), so leave both
		// counts at 0 (the policy's reach-expansion gate then never fires).
		int anchorX = -1, anchorY = -1;
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building* b = team->myBuildings[i];
			if (b == NULL || b->buildingState == Building::DEAD)
				continue;
			if (b->type != NULL && b->type->isVirtual)
				continue; // flags occupy no ground; not a colony anchor.
			anchorX = b->posX;
			anchorY = b->posY;
			break;
		}
		if (anchorX < 0)
			return out;

		out.landReach  = countReach(map, team, anchorX, anchorY, /*canSwim=*/false);
		out.waterReach = countReach(map, team, anchorX, anchorY, /*canSwim=*/true);
		return out;
	}
}
