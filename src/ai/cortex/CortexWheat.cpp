// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexWheat.h"

#include "map/Map.h"
#include "building/BuildingUtils.h"
#include "building/Building.h"
#include "BuildingType.h"
#include "Player.h"
#include "team/Team.h"
#include "Game.h"
#include "Ressource.h"

#include <climits>
#include <deque>

// See CortexWheat.h for the design rationale. This is the geometry + reconcile
// core only; it builds tile sets but emits no Orders.

namespace Cortex
{
	namespace
	{
		// 4-neighbourhood in a fixed (deterministic) order: N, W, E, S.
		const int NB_DX[4] = { 0, -1, 1, 0 };
		const int NB_DY[4] = { -1, 0, 0, 1 };

		bool isCorn(Map& map, int x, int y)
		{
			return map.getRessource(x, y).type == CORN;
		}
	} // namespace

	WheatScanResult scanWheatForbidden(
		Map& map, Uint32 teamMask, int teamNumber,
		const std::vector<int>& consumerSeeds,
		int boxMinX, int boxMinY, int boxMaxX, int boxMaxY,
		int openMargin, bool ignoreFOW, bool wantDebug, bool liftAll)
	{
		WheatScanResult res;

		// The open margin is disabled: every reachable wheat row is checkerboarded,
		// so `openMargin` no longer gates classification. The parameter is retained
		// only to keep the observation/action layout and call sites unchanged.
		(void)openMargin;

		const int w = map.getW();
		const int h = map.getH();
		if (w <= 0 || h <= 0)
			return res;

		// Clamp the territory region to the map (no wrap across the box edge: the
		// colony region is local, so a plain clamped rectangle is sufficient).
		if (boxMinX < 0) boxMinX = 0;
		if (boxMinY < 0) boxMinY = 0;
		if (boxMaxX > w - 1) boxMaxX = w - 1;
		if (boxMaxY > h - 1) boxMaxY = h - 1;
		if (boxMinX > boxMaxX || boxMinY > boxMaxY)
			return res;

		auto inBox = [&](int x, int y) {
			return x >= boxMinX && x <= boxMaxX && y >= boxMinY && y <= boxMaxY;
		};
		// A CORN tile counts as field only if the team can see it (unless the
		// debug caller bypasses fog on a freshly-loaded, fully-fogged map).
		auto cornVisible = [&](int x, int y) {
			return ignoreFOW || map.isFOWDiscovered(x, y, teamMask);
		};
		// A tile belongs to our field if it is discovered wheat inside the
		// territory box; a tile is "land" if a ground unit can stand on it (the
		// BFS floods over land to reach the field, but land never counts toward
		// depth). Forbidden status is ignored on purpose so our own paint never
		// changes the measured depth (keeps the reconcile stable).
		auto isField = [&](int x, int y) {
			return inBox(x, y) && isCorn(map, x, y) && cornVisible(x, y);
		};
		auto isLand = [&](int x, int y) {
			return inBox(x, y) && map.isFreeForGroundUnitNoForbidden(x, y, false);
		};

		// --- Gather the field tiles. ---
		std::vector<int> fieldTiles;
		for (int y = boxMinY; y <= boxMaxY; y++)
			for (int x = boxMinX; x <= boxMaxX; x++)
				if (isField(x, y))
					fieldTiles.push_back(static_cast<int>(map.coordToIndex(x, y)));
		res.fieldTileCount = static_cast<Sint32>(fieldTiles.size());
		if (wantDebug)
		{
			res.classOf.assign(static_cast<size_t>(w) * h, WC_NONE);
			res.depthOf.assign(static_cast<size_t>(w) * h, -1);
		}
		if (fieldTiles.empty())
			return res;

		// --- Land+wheat BFS from the consumer's walkable exit ring. ---
		// Plain breadth-first over walkable terrain, so each tile is first reached
		// along a shortest WALKING path from the inn. The stored value is not the
		// walking distance but the number of wheat tiles crossed along that path:
		// land steps advance the path without bumping the count, and the first wheat
		// tile entered is depth 1. So depth == "wheat tiles deep from where the inn's
		// nearest approach enters the field" — the inn<->field land gap and intra-
		// field land never count toward N, yet the gradient still recedes away from
		// the inn (a far edge reached only by ploughing through the field stays deep
		// and protected, while the inn-facing edge stays shallow and open).
		//
		// The consumer usually sits on its own building footprint, which is NOT
		// walkable, so seeding the centre tile alone would trap the search. Seed every
		// walkable tile within a small exit radius instead — the inn's exit ring.
		const int SEED_EXIT_RADIUS = 3;
		std::vector<int> depth(static_cast<size_t>(w) * h, INT_MAX);
		std::deque<int> q; // FIFO: all steps cost one walking move, so plain BFS order.
		for (int seed : consumerSeeds)
		{
			if (seed < 0 || seed >= w * h)
				continue;
			const int seedX = seed % w;
			const int seedY = seed / w;
			for (int dy = -SEED_EXIT_RADIUS; dy <= SEED_EXIT_RADIUS; dy++)
				for (int dx = -SEED_EXIT_RADIUS; dx <= SEED_EXIT_RADIUS; dx++)
				{
					const int x = seedX + dx;
					const int y = seedY + dy;
					if (!isLand(x, y))
						continue;
					const int idx = static_cast<int>(map.coordToIndex(x, y));
					if (depth[idx] == INT_MAX)
					{
						depth[idx] = 0; // land exit ring: zero wheat crossed so far.
						q.push_back(idx);
					}
				}
		}

		while (!q.empty())
		{
			const int cur = q.front();
			q.pop_front();
			const int cx = cur % w;
			const int cy = cur / w;
			const int d = depth[cur];
			for (int k = 0; k < 4; k++)
			{
				const int nx = cx + NB_DX[k];
				const int ny = cy + NB_DY[k];
				const bool wheat = isField(nx, ny);
				if (!wheat && !isLand(nx, ny))
					continue;
				const int ni = static_cast<int>(map.coordToIndex(nx, ny));
				if (depth[ni] != INT_MAX)
					continue; // first BFS visit == shortest walking path; keep it.
				depth[ni] = d + (wheat ? 1 : 0);
				q.push_back(ni);
			}
		}

		// --- Classify field wheat and collect the desired forbidden set. ---
		for (int idx : fieldTiles)
		{
			const int x = idx % w;
			const int y = idx / w;
			const int d = depth[idx];
			if (d == INT_MAX)
				continue; // wheat the consumer cannot reach over land: not harvested.
			if (wantDebug)
				res.depthOf[idx] = static_cast<Sint16>(d < 32767 ? d : 32767);

			// Open margin removed: EVERY reachable row of wheat is checkerboarded,
			// with no exempt rows nearest the harvest source. Classification is purely
			// by parity — half the field (the WHEAT_PARITY half) is protected, the
			// other half harvest-open, all the way in to depth 1. (`openMargin` is no
			// longer consulted; it is retained only for the observation/action layout.)
			// WHEAT-BLITZ liftAll: classify what WOULD be the protected half as
			// WC_CHECKER_OPEN instead of WC_FORBIDDEN so `desired` stays empty — the
			// reconcile then un-forbids the WHOLE field. Iteration order, BFS, depths,
			// and the add/del diff are untouched, so determinism is preserved.
			Uint8 cls;
			if (!liftAll && ((x + y) & 1) == WHEAT_PARITY)
			{
				cls = WC_FORBIDDEN;
				res.desired.push_back(idx);
			}
			else
			{
				cls = WC_CHECKER_OPEN;
			}
			if (wantDebug)
				res.classOf[idx] = cls;
		}
		res.forbiddenCount = static_cast<Sint32>(res.desired.size());

		// --- Connected components among reachable field wheat (informational). ---
		{
			std::vector<bool> seen(static_cast<size_t>(w) * h, false);
			for (int y = boxMinY; y <= boxMaxY; y++)
				for (int x = boxMinX; x <= boxMaxX; x++)
				{
					const int idx = static_cast<int>(map.coordToIndex(x, y));
					if (seen[idx] || !isCorn(map, x, y) || !cornVisible(x, y)
					    || depth[idx] == INT_MAX)
						continue;
					res.componentCount++;
					std::deque<int> stack;
					stack.push_back(idx);
					seen[idx] = true;
					while (!stack.empty())
					{
						const int c = stack.back();
						stack.pop_back();
						const int ccx = c % w;
						const int ccy = c / w;
						for (int kk = 0; kk < 4; kk++)
						{
							const int nx = ccx + NB_DX[kk];
							const int ny = ccy + NB_DY[kk];
							if (!inBox(nx, ny) || !isCorn(map, nx, ny) || !cornVisible(nx, ny))
								continue;
							const int ni = static_cast<int>(map.coordToIndex(nx, ny));
							if (seen[ni] || depth[ni] == INT_MAX)
								continue;
							seen[ni] = true;
							stack.push_back(ni);
						}
					}
				}
		}

		// --- Reconcile against the team's CURRENT forbidden paint. ---
		// Current = forbidden tiles in the box that are OURS to manage, i.e. minus
		// our own building footprints (auto-forbidden by the engine,
		// Game_orders.cpp; tearing those down would foul our own colony).
		std::vector<bool> currentBit(static_cast<size_t>(w) * h, false);
		std::vector<int> current;
		for (int y = boxMinY; y <= boxMaxY; y++)
			for (int x = boxMinX; x <= boxMaxX; x++)
			{
				if (!map.isForbidden(x, y, teamMask))
					continue;
				const Uint16 gid = map.getBuilding(x, y);
				if (gid != NOGBID && BuildingUtils::GIDtoTeam(gid) == teamNumber)
					continue; // our footprint, not wheat paint.
				const int idx = static_cast<int>(map.coordToIndex(x, y));
				currentBit[idx] = true;
				current.push_back(idx);
			}

		// ADD = desired - current; DEL = current - desired. Index order preserved.
		for (int idx : res.desired)
			if (!currentBit[idx])
				res.add.push_back(idx);
		// A forbidden tile is retired ONLY when we can currently SEE it (no fog of
		// war) AND the wheat under it is gone. This is deliberately INDEPENDENT of the
		// desired checkerboard: the desired pattern drives where we ADD paint, never
		// where we remove it. Stripping paint from a tile that still has wheat — just
		// because it fell in the harvest half, the open margin, or briefly went
		// unreachable — tears protection off field we are trying to maintain and lets
		// workers harvest the reseed half, which is exactly what breaks the field.
		//   - fogged tile          -> keep paint (we cannot confirm depletion);
		//   - visible, still wheat -> keep paint (reachable or not, it is still field);
		//   - visible, wheat gone  -> retire paint.
		// The debug/static path (ignoreFOW) treats every tile as visible.
		//
		// WHEAT-BLITZ liftAll: the steady-state depletion guards above are SKIPPED —
		// we retire ALL current paint (DEL = current, ADD empty since `desired` is
		// empty), un-forbidding the whole field for a one-time harvest burst even
		// though the wheat is still standing. This is the deliberate famine override,
		// distinct from the steady-state "retire only when depleted" invariant; normal
		// protection re-paints the checkerboard once the famine clears. Iterating
		// `current` in index order keeps the DEL list deterministic.
		for (int idx : current)
		{
			if (!liftAll)
			{
				const int x = idx % w;
				const int y = idx / w;
				if (!ignoreFOW && !map.isFOWDiscovered(x, y, teamMask))
					continue; // in fog: confirmation pending, leave the paint.
				if (isCorn(map, x, y))
					continue; // still field wheat: keep protecting it.
			}
			res.del.push_back(idx);
		}
		res.addCount = static_cast<Sint32>(res.add.size());
		res.delCount = static_cast<Sint32>(res.del.size());

		return res;
	}

	WheatReconcile reconcileWheatForbidden(Player* player, int openMargin, bool buildMasks,
	                                       bool liftAll)
	{
		WheatReconcile out;
		if (player == NULL || player->team == NULL || player->team->game == NULL)
			return out;

		Team* team = player->team;
		Game* game = team->game;
		Map& map = game->map;
		const int w = map.getW();
		const int h = map.getH();
		if (w <= 0 || h <= 0)
			return out;

		const Uint32 teamMask = team->me;
		const int teamNumber = team->teamNumber;

		// Consumer seeds = feeding-building (inn) centre tiles; scanWheatForbidden
		// expands each to its walkable exit ring. The colony bounding box grows over
		// our REAL buildings only — virtual buildings (war flags) can sit at the
		// enemy base and would balloon the region, so they are excluded here (the
		// -dump-wheat tool didn't need this: a freshly-loaded map has no flags).
		// Iterate by array index, never a std::set, for lockstep determinism.
		std::vector<int> seeds;
		int bbMinX = w, bbMinY = h, bbMaxX = -1, bbMaxY = -1;
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building* b = team->myBuildings[i];
			if (b == NULL || b->buildingState == Building::DEAD)
				continue;
			if (b->type && b->type->isVirtual)
				continue; // war/exploration flag: not part of the colony footprint.
			if (b->posX < bbMinX) bbMinX = b->posX;
			if (b->posX > bbMaxX) bbMaxX = b->posX;
			if (b->posY < bbMinY) bbMinY = b->posY;
			if (b->posY > bbMaxY) bbMaxY = b->posY;
			if (b->type && b->type->canFeedUnit)
				seeds.push_back(static_cast<int>(map.coordToIndex(b->posX, b->posY)));
		}

		// Always fold the team start into the bbox so the region is valid even
		// before the first building, and seed the start as a fallback consumer when
		// no inn exists yet (matches the -dump-wheat derivation).
		const int startX = team->startPosX;
		const int startY = team->startPosY;
		if (startX < bbMinX) bbMinX = startX;
		if (startX > bbMaxX) bbMaxX = startX;
		if (startY < bbMinY) bbMinY = startY;
		if (startY > bbMaxY) bbMaxY = startY;
		if (bbMaxX < bbMinX || bbMaxY < bbMinY)
			return out; // no buildings and an unset start: nothing to scan.
		if (seeds.empty())
			seeds.push_back(static_cast<int>(map.coordToIndex(startX, startY)));

		// Colony region = bbox padded by WHEAT_REGION_MARGIN (scanWheatForbidden
		// clamps to the map, but clamp here too so the values are sane).
		int boxMinX = bbMinX - WHEAT_REGION_MARGIN;
		int boxMinY = bbMinY - WHEAT_REGION_MARGIN;
		int boxMaxX = bbMaxX + WHEAT_REGION_MARGIN;
		int boxMaxY = bbMaxY + WHEAT_REGION_MARGIN;
		if (boxMinX < 0) boxMinX = 0;
		if (boxMinY < 0) boxMinY = 0;
		if (boxMaxX > w - 1) boxMaxX = w - 1;
		if (boxMaxY > h - 1) boxMaxY = h - 1;

		// Live path: real fog-of-war (only paint wheat we can currently see), and
		// no debug overlays. buildMasks decides whether we also paint the brushes.
		WheatScanResult r = scanWheatForbidden(
			map, teamMask, teamNumber, seeds,
			boxMinX, boxMinY, boxMaxX, boxMaxY,
			openMargin, /*ignoreFOW=*/false, /*wantDebug=*/false, liftAll);

		out.addCount = r.addCount;
		out.delCount = r.delCount;
		if (buildMasks)
		{
			// Accumulate the ADD/DEL tile lists (already in index order) into the
			// two BrushAccumulators, one 1x1 brush per tile (figure 0), exactly as
			// AIWarrush paints its forbidden checkerboard (AIWarrush.cpp:551-583).
			for (int idx : r.add)
				out.add.applyBrush(BrushApplication(idx % w, idx / w, 0), &map);
			for (int idx : r.del)
				out.del.applyBrush(BrushApplication(idx % w, idx / w, 0), &map);
		}
		return out;
	}
}
