// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexWater.h"
#include "CortexTypes.h"

#include "Player.h"
#include "Game.h"
#include "team/Team.h"
#include "building/Building.h"
#include "game/entities/BuildingType.h"
#include "IntBuildingType.h"
#include "map/Map.h"
#include "Ressource.h"

#include <vector>
#include <cstdlib>  // getenv (gated diagnostics)
#include <iostream> // std::cerr (gated diagnostics)

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
		//
		// If algaeAdjacent is non-null, it is set to true the moment any reachable tile
		// is found 8-adjacent to a DISCOVERED, takeable ALGA tile — i.e. a worker could
		// stand on reachable ground and harvest that algae from the shore. (Only
		// meaningful with canSwim=false: a non-swimmer's reachable region tells us
		// whether algae can be hauled to a building site WITHOUT a swimming pool.)
		int countReach(Map& map, const Team* team, int cx, int cy, bool canSwim,
		               bool* algaeAdjacent = NULL)
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
						// Shore-harvest probe: this neighbour of the reachable tile we just
						// popped is a discovered, takeable ALGA tile, so a worker standing on
						// that reachable tile could harvest it with no swimming pool. Tested
						// BEFORE the gates below: algae sits on water (never itself a reachable
						// tile, so the passability gate would reject it) and may lie one step
						// past the reach radius while still harvestable from inside it.
						if (algaeAdjacent != NULL && !*algaeAdjacent
						 && map.isRessourceTakeable(nx, ny, ALGA)
						 && map.isMapDiscovered(nx, ny, team->allies))
							*algaeAdjacent = true;
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

	SwimAssessment assessSwim(Player* player, bool wantSwimReach)
	{
		SwimAssessment out;
		out.algaeDiscovered = 0;
		out.landReach = 0;
		out.waterReach = 0;
		out.algaeReachable = 0;

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

		// Ground pass (always): the no-swim reachable region, which also tells us whether
		// any discovered algae is harvestable from shore (algaeReachable) — the signal the
		// school gate needs at every stage of the game.
		bool algaeAdjacent = false;
		out.landReach = countReach(map, team, anchorX, anchorY, /*canSwim=*/false,
		                           &algaeAdjacent);
		out.algaeReachable = algaeAdjacent ? 1 : 0;

		// Swim pass (only when asked): the land-vs-water reach gap feeds the one-shot
		// swimming-pool decision, which never re-fires once a pool exists — so the caller
		// skips this second fill in that case to bound observation cost.
		if (wantSwimReach)
			out.waterReach = countReach(map, team, anchorX, anchorY, /*canSwim=*/true);
		return out;
	}

	namespace
	{
		// Full-map BFS over tiles passable for a ground unit with the given canSwim
		// ability, seeded from the 8-neighbourhood of (seedX, seedY). The seed tile is
		// a building footprint (occupied), so it is never itself enqueued — the same
		// anchor trick countReach uses. Fills `dist` (size w*h, -1 == unreached) with
		// the hop distance from the seed ring. Plain FIFO: the hop-distance field of an
		// unweighted 8-connected grid is visitation-order independent, so no tie-break
		// is needed for determinism. Warp-safe via Map normalization; no floats / RNG /
		// std::set. Unbounded (whole map) — unlike countReach's radius-bounded fill.
		void bfsGroundField(Map& map, Uint32 me, int seedX, int seedY, bool canSwim,
		                    std::vector<int>& dist)
		{
			const int w = map.getW();
			const int h = map.getH();
			dist.assign(static_cast<size_t>(w) * h, -1);
			std::vector<int> frontier; // queue of flattened indices, drained by head.
			frontier.reserve(1024);

			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
				{
					if (dx == 0 && dy == 0)
						continue;
					const int nx = map.normalizeX(seedX + dx);
					const int ny = map.normalizeY(seedY + dy);
					const size_t idx = static_cast<size_t>(ny) * w + nx;
					if (dist[idx] >= 0)
						continue;
					if (!map.isHardSpaceForGroundUnit(nx, ny, canSwim, me))
						continue;
					dist[idx] = 1;
					frontier.push_back(static_cast<int>(idx));
				}

			for (size_t head = 0; head < frontier.size(); head++)
			{
				const int cur = frontier[head];
				const int x = cur % w;
				const int y = cur / w;
				const int nd = dist[cur] + 1;
				for (int dy = -1; dy <= 1; dy++)
					for (int dx = -1; dx <= 1; dx++)
					{
						if (dx == 0 && dy == 0)
							continue;
						const int nx = map.normalizeX(x + dx);
						const int ny = map.normalizeY(y + dy);
						const size_t idx = static_cast<size_t>(ny) * w + nx;
						if (dist[idx] >= 0)
							continue;
						if (!map.isHardSpaceForGroundUnit(nx, ny, canSwim, me))
							continue;
						dist[idx] = nd;
						frontier.push_back(static_cast<int>(idx));
					}
			}
		}

		// Smallest hop distance among the 8 tiles adjacent to (tx, ty) in `dist`. The
		// target tile is an enemy-building footprint (never passable, so never in the
		// field), so we read its 8-neighbourhood — the same "reached when BFS touches
		// any 8-adjacent tile" trick countReach uses for its anchor. -1 when none of
		// the 8 neighbours was reached (the target's land region is unreachable).
		int distToTarget(Map& map, const std::vector<int>& dist, int tx, int ty)
		{
			const int w = map.getW();
			int best = -1;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
				{
					if (dx == 0 && dy == 0)
						continue;
					const int nx = map.normalizeX(tx + dx);
					const int ny = map.normalizeY(ty + dy);
					const int d = dist[static_cast<size_t>(ny) * w + nx];
					if (d >= 0 && (best < 0 || d < best))
						best = d;
				}
			return best;
		}

		// The colony rally tile: its first (lowest-index) alive SWARM_BUILDING, else the
		// first alive building. Mirrors AICortex::computeRallyPoint (including its lack of
		// a virtual-building skip) so the landing-zone swim ranking measures from the same
		// muster origin the offense pipeline gathers at. Returns false when the team has
		// no building.
		bool rallyTile(const Team* team, int& rx, int& ry)
		{
			Building* fallback = NULL;
			for (int i = 0; i < Building::MAX_COUNT; i++)
			{
				Building* b = team->myBuildings[i];
				if (b == NULL || b->buildingState == Building::DEAD)
					continue;
				if (fallback == NULL)
					fallback = b;
				if (b->type != NULL
				 && b->type->shortTypeNum == IntBuildingType::SWARM_BUILDING)
				{
					rx = b->posX;
					ry = b->posY;
					return true;
				}
			}
			if (fallback != NULL)
			{
				rx = fallback->posX;
				ry = fallback->posY;
				return true;
			}
			return false;
		}
	} // namespace

	AmphibiousAssessment assessAmphibious(Player* player, int targetX, int targetY,
	                                      const Sint32* standoffX, const Sint32* standoffY,
	                                      int standoffCount, int landingStandoffTiles,
	                                      int forwardRallyPathDist)
	{
		AmphibiousAssessment out;
		out.amphibious   = 0;
		out.landDist     = -1;
		out.swimDist     = -1;
		out.landingValid = 0;
		out.landingX     = -1;
		out.landingY     = -1;
		out.forwardRallyValid = 0;
		out.forwardRallyX     = -1;
		out.forwardRallyY     = -1;

		if (player == NULL || player->team == NULL)
			return out;
		Team* team = player->team;
		Game* game = team->game;
		if (game == NULL)
			return out;
		Map& map = game->map;
		const Uint32 me = team->me;
		const int w = map.getW();
		const int h = map.getH();

		int rallyX = -1, rallyY = -1;
		if (!rallyTile(team, rallyX, rallyY))
			return out; // no colony anchor to march from.

		// Two full-map BFS from the rally: the LAND path (water blocks) and the SWIM
		// path (water passes). Each distance is measured to the target's 8-neighbourhood.
		std::vector<int> landField, swimField;
		bfsGroundField(map, me, rallyX, rallyY, /*canSwim=*/false, landField);
		bfsGroundField(map, me, rallyX, rallyY, /*canSwim=*/true,  swimField);
		out.landDist = distToTarget(map, landField, targetX, targetY);
		out.swimDist = distToTarget(map, swimField, targetX, targetY);

		// AMPHIBIOUS iff swimDist reachable AND (landDist unreachable OR swimDist <
		// landDist). A land-only path is also a valid swim path, so the swimmer's
		// distance can never exceed the walker's; swimDist < landDist therefore means
		// the true shortest path to the target's land region crosses water.
		const bool amphibious = out.swimDist >= 0
		    && (out.landDist < 0 || out.swimDist < out.landDist);
		// DIAGNOSTIC (gated): the classifier's raw inputs — per-field reached-tile
		// counts (and how much of the swim field is actually water) alongside the two
		// distances. Locked-equal distances with a large swimWater count means the
		// shortest route genuinely gains nothing from water (e.g. a resource-walled
		// approach), not that the swim toggle is broken. Pure read -> stderr.
		if (getenv("CORTEX_DUMP_AMPHIB"))
		{
			int landReached = 0, swimReached = 0, swimWater = 0;
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++)
				{
					const size_t idx = static_cast<size_t>(y) * w + x;
					if (landField[idx] >= 0)
						landReached++;
					if (swimField[idx] >= 0)
					{
						swimReached++;
						if (map.isWater(x, y))
							swimWater++;
					}
				}
			std::cerr << "CORTEX_AMPHIB rally=" << rallyX << "," << rallyY
			          << " tgt=" << targetX << "," << targetY
			          << " landDist=" << out.landDist << " swimDist=" << out.swimDist
			          << " landReached=" << landReached
			          << " swimReached=" << swimReached << " swimWater=" << swimWater
			          << " amphibious=" << (amphibious ? 1 : 0) << "\n";
		}
		if (!amphibious)
		{
			// LAND campaign. Short march: keep today's straight MUSTER -> ASSAULT.
			// LONG march (true land-path distance beyond the knob): pick a FORWARD
			// RALLY staging tile so the waves stage through CROSS anyway — the phase
			// machine's anchor just stops being water-specific.
			if (forwardRallyPathDist <= 0 || out.landDist < 0
			 || out.landDist <= forwardRallyPathDist)
				return out;

			// Third BFS (long-land branch only): the hop-distance field FROM the
			// target over land (canSwim=false) — each tile's true path distance to
			// the target, the "how far forward is this tile" metric the staging
			// selection minimizes.
			std::vector<int> targetLand;
			bfsGroundField(map, me, targetX, targetY, /*canSwim=*/false, targetLand);

			// Corridor scan. A staging tile must be reachable from the rally by LAND
			// (the marchers can't swim) and from the target's side (targetLand). Rank:
			// (1) LOWEST BFS distance to the target — as far forward as the standoff
			//     allows; the building-standoff filter below already guarantees that
			//     distance >= landingStandoffTiles, since the target IS one of the
			//     standoff buildings and BFS hops >= warp Chebyshev.
			// (2) tie -> LOWEST land-BFS distance from the rally: at fixed target
			//     distance, minimizing rally distance puts the tile ON a shortest
			//     rally->target path — the corridor the waves march anyway.
			// (3) tie -> lowest flattened index (the landing picker's tie-break).
			// Never-fail fallback (no tile clears the building standoff): same rank
			// over tiles with the explicit targetDist >= standoff bound instead.
			int bestIdx = -1, bestTgt = 0, bestRally = 0;
			int bestIdxAny = -1, bestTgtAny = 0, bestRallyAny = 0;
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++)
				{
					const size_t idx = static_cast<size_t>(y) * w + x;
					const int tgtD = targetLand[idx];
					const int rallyD = landField[idx];
					if (tgtD < 0 || rallyD < 0)
						continue; // not a corridor tile.

					if (tgtD >= landingStandoffTiles
					 && (bestIdxAny < 0 || tgtD < bestTgtAny
					     || (tgtD == bestTgtAny && (rallyD < bestRallyAny
					         || (rallyD == bestRallyAny
					             && static_cast<int>(idx) < bestIdxAny)))))
					{
						bestTgtAny = tgtD;
						bestRallyAny = rallyD;
						bestIdxAny = static_cast<int>(idx);
					}

					// Standoff: stage clear of every discovered enemy building, so the
					// wave masses out of shelling range before the final push (the same
					// bound the landing zone keeps).
					bool tooClose = false;
					for (int s = 0; s < standoffCount && !tooClose; s++)
						if (map.warpDistMax(x, y, standoffX[s], standoffY[s]) < landingStandoffTiles)
							tooClose = true;
					if (tooClose)
						continue;

					if (bestIdx < 0 || tgtD < bestTgt
					 || (tgtD == bestTgt && (rallyD < bestRally
					     || (rallyD == bestRally && static_cast<int>(idx) < bestIdx))))
					{
						bestTgt = tgtD;
						bestRally = rallyD;
						bestIdx = static_cast<int>(idx);
					}
				}

			const int chosen = (bestIdx >= 0) ? bestIdx : bestIdxAny;
			if (chosen >= 0)
			{
				out.forwardRallyValid = 1;
				out.forwardRallyX = chosen % w;
				out.forwardRallyY = chosen / w;
			}
			// DIAGNOSTIC (gated): the staging pick alongside the trigger inputs.
			if (getenv("CORTEX_DUMP_AMPHIB"))
			{
				std::cerr << "CORTEX_FWDRALLY tgt=" << targetX << "," << targetY
				          << " landDist=" << out.landDist
				          << " pathKnob=" << forwardRallyPathDist
				          << " rally=";
				if (out.forwardRallyValid)
					std::cerr << out.forwardRallyX << "," << out.forwardRallyY
					          << " tgtD=" << ((bestIdx >= 0) ? bestTgt : bestTgtAny)
					          << " rallyD=" << ((bestIdx >= 0) ? bestRally : bestRallyAny)
					          << " standoffOk=" << (bestIdx >= 0 ? 1 : 0);
				else
					std::cerr << "none";
				std::cerr << "\n";
			}
			return out;
		}
		out.amphibious = 1;

		// Third BFS (amphibious branch only): the TARGET's own land COMPONENT (canSwim=
		// false reachable set from the target). The landing zone must sit in it — that is
		// the land the swimmers climb out onto and then walk to the enemy.
		std::vector<int> targetLand;
		bfsGroundField(map, me, targetX, targetY, /*canSwim=*/false, targetLand);

		// Scan the component for shore tiles (8-adjacent to a water tile — where a
		// swimmer leaves the water). Track two bests: one that clears the standoff and,
		// as a never-fail fallback, the best ignoring standoff. "Best" == lowest swim-BFS
		// distance from the rally, tie-broken by lowest flattened index (deterministic).
		int bestIdx = -1, bestSwim = 0;
		int bestIdxAny = -1, bestSwimAny = 0;
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
			{
				const size_t idx = static_cast<size_t>(y) * w + x;
				if (targetLand[idx] < 0)
					continue; // not in the target's land component.
				const int swim = swimField[idx];
				if (swim < 0)
					continue; // the swimmers can't even reach this component tile.

				bool shore = false;
				for (int dy = -1; dy <= 1 && !shore; dy++)
					for (int dx = -1; dx <= 1 && !shore; dx++)
					{
						if (dx == 0 && dy == 0)
							continue;
						if (map.isWater(map.normalizeX(x + dx), map.normalizeY(y + dy)))
							shore = true;
					}
				if (!shore)
					continue;

				// Never-fail fallback: best shore tile regardless of standoff.
				if (bestIdxAny < 0 || swim < bestSwimAny
				 || (swim == bestSwimAny && static_cast<int>(idx) < bestIdxAny))
				{
					bestSwimAny = swim;
					bestIdxAny = static_cast<int>(idx);
				}

				// Standoff: keep clear of every discovered enemy building so the swimmers
				// form up out of shelling range before pushing inland.
				bool tooClose = false;
				for (int s = 0; s < standoffCount && !tooClose; s++)
					if (map.warpDistMax(x, y, standoffX[s], standoffY[s]) < landingStandoffTiles)
						tooClose = true;
				if (tooClose)
					continue;

				if (bestIdx < 0 || swim < bestSwim
				 || (swim == bestSwim && static_cast<int>(idx) < bestIdx))
				{
					bestSwim = swim;
					bestIdx = static_cast<int>(idx);
				}
			}

		const int chosen = (bestIdx >= 0) ? bestIdx : bestIdxAny;
		if (chosen < 0)
			return out; // no reachable shore tile: cannot amphibious-assault this target.
		out.landingValid = 1;
		out.landingX = chosen % w;
		out.landingY = chosen / w;
		return out;
	}
}
