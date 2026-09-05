// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexPlacementGeo.h"

#include "CortexTypes.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Ressource.h"
#include "building/Building.h"
#include "game/entities/BuildingType.h"
#include "map/Map.h"
#include "team/Team.h"

namespace Cortex
{
	namespace
	{
		// Warp-safe point-in-footprint test: is the (already normalized) tile
		// (nx, ny) covered by the footprint (cx, cy) top-left, cw x ch? Footprints
		// are small (<= a few tiles per side), so the direct scan is cheap and
		// avoids fragile rectangle-overlap math across the map seam.
		bool footprintContains(const Map& map, int cx, int cy, int cw, int ch,
		                       int nx, int ny)
		{
			for (int j = 0; j < ch; j++)
			{
				if (map.normalizeY(cy + j) != ny)
					continue;
				for (int i = 0; i < cw; i++)
					if (map.normalizeX(cx + i) == nx)
						return true;
			}
			return false;
		}

		// Side bitmask used by innOccupiedSides.
		static const int SIDE_LEFT   = 1;
		static const int SIDE_RIGHT  = 2;
		static const int SIDE_TOP    = 4;
		static const int SIDE_BOTTOM = 8;

		// Signed gap between two intervals on a circular axis of size `dim`.
		// Interval A = [a0, a0+alen-1], B = [b0, b0+blen-1], both mod dim.
		// Returns -1 if the intervals overlap (share a cell); otherwise the number of
		// empty cells in the smaller gap between them (0 == touching/adjacent).
		//
		// The map wraps (toroidal), so two boxes have a gap on each side of the seam;
		// we take the smaller. Working in d = b's start relative to a's start (mod dim)
		// lets the same integer arithmetic cover the wrap case without a branch on which
		// box is "first" — the wrap distance is just (dim - d). All-integer, no rand,
		// no float: safe for lockstep.
		static int axisSignedGap(int a0, int alen, int b0, int blen, int dim)
		{
			const int d = ((b0 - a0) % dim + dim) % dim; // b's start relative to a's start
			const int forwardGap  = d - alen;            // a's end -> b's start
			const int backwardGap = (dim - d) - blen;    // b's end -> a's start (wrap)
			if (forwardGap < 0 || backwardGap < 0)
				return -1;                               // overlap on this axis
			return forwardGap < backwardGap ? forwardGap : backwardGap;
		}
	} // namespace

	// Chebyshev distance from the footprint's top-left corner to the nearest
	// live building owned by `team`. Returns -1 when the team has no
	// buildings yet (first placement: distance is meaningless).
	int distanceToNearestBuilding(Game* game, Team* team, int x, int y)
	{
		int best = -1;
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building* b = team->myBuildings[i];
			if (b == NULL || b->buildingState == Building::DEAD)
				continue;
			int d = game->map.warpDistMax(x, y, b->posX, b->posY);
			if (best < 0 || d < best)
				best = d;
		}
		return best;
	}

	// Translate a Chebyshev distance-to-colony into a placement score.
	// Closer is better; we want a compact colony but not literally stacked,
	// so distance 0 (impossible for a real footprint anyway) and tiny
	// distances are fine, and the score decays linearly with distance.
	// A team with no buildings gets a flat base so the very first building
	// is placed on the first legal, discovered tile in scan order.
	int scoreFromDistance(int distToColony)
	{
		static const int FIRST_BUILDING_BASE = 1000;
		static const int NEAR_COLONY_BASE = 10000;

		if (distToColony < 0)
			return FIRST_BUILDING_BASE;
		// Subtract distance so nearer spots rank higher; clamp at 0 so very
		// far candidates do not go negative and disorder the ranking.
		int score = NEAR_COLONY_BASE - distToColony;
		if (score < 0)
			score = 0;
		return score;
	}

	int rectEdgeChebyshev(int ax, int aw, int ay, int ah,
	                      int bx, int bw, int by, int bh, int mapW, int mapH)
	{
		// Chebyshev (max-axis) edge gap between two warp-wrapped boxes. Per-axis
		// overlap (gap < 0) clamps to 0 so a box that overlaps on one axis but is
		// separated on the other still reports the real separation along that axis;
		// boxes that overlap on both axes report 0 (touching/inside).
		int gx = axisSignedGap(ax, aw, bx, bw, mapW); if (gx < 0) gx = 0;
		int gy = axisSignedGap(ay, ah, by, bh, mapH); if (gy < 0) gy = 0;
		return gx > gy ? gx : gy;
	}

	bool rectsOverlap(int ax, int aw, int ay, int ah,
	                  int bx, int bw, int by, int bh, int mapW, int mapH)
	{
		// Two boxes share at least one tile iff they overlap on BOTH axes
		// (axisSignedGap returns -1 on an overlapping axis).
		return axisSignedGap(ax, aw, bx, bw, mapW) < 0
		    && axisSignedGap(ay, ah, by, bh, mapH) < 0;
	}

	int innOccupiedSides(const Map& map, int innX, int innY, int innW, int innH,
	                     int candX, int candY, int candW, int candH)
	{
		const bool haveCand = (candW > 0 && candH > 0);
		const int clear = CORTEX_INN_SIDE_CLEARANCE;

		int sides = 0;

		// Walk the clearance frame: the ring of thickness `clear` around the inn
		// footprint, corners included. dx/dy are offsets from the footprint's
		// top-left corner. Skip the interior — only the surrounding band matters.
		for (int dy = -clear; dy < innH + clear; dy++)
		{
			for (int dx = -clear; dx < innW + clear; dx++)
			{
				const bool insideX = (dx >= 0 && dx < innW);
				const bool insideY = (dy >= 0 && dy < innH);
				if (insideX && insideY)
					continue;

				const int nx = map.normalizeX(innX + dx);
				const int ny = map.normalizeY(innY + dy);

				bool occupied = (map.getBuilding(nx, ny) != NOGBID);
				if (!occupied && haveCand)
					occupied = footprintContains(map, candX, candY, candW, candH, nx, ny);
				if (!occupied)
					continue;

				// Classify by direction. A frame tile outside the footprint on both
				// axes (a diagonal corner) sets both adjacent side bits, so a corner
				// building counts as occupying two sides.
				if (dx < 0)          sides |= SIDE_LEFT;
				else if (dx >= innW) sides |= SIDE_RIGHT;
				if (dy < 0)          sides |= SIDE_TOP;
				else if (dy >= innH) sides |= SIDE_BOTTOM;
			}
		}

		int count = 0;
		for (int m = sides; m != 0; m >>= 1)
			count += (m & 1);
		return count;
	}

	void grownFootprint(const BuildingType* bt, int& w, int& h)
	{
		w = (bt != NULL) ? bt->width : 0;
		h = (bt != NULL) ? bt->height : 0;

		// Walk the upgrade chain, taking the max footprint along the way. nextLevel
		// is -1 (BUILDING_LEVEL_NONE) at the top of the chain.
		const BuildingType* cur = bt;
		while (cur != NULL && cur->nextLevel >= 0)
		{
			cur = globalContainer->buildingsTypes.get(cur->nextLevel);
			if (cur == NULL)
				break;
			if (cur->width > w)
				w = cur->width;
			if (cur->height > h)
				h = cur->height;
		}
	}

	void grownFootprintBox(const BuildingType* bt, int& ox, int& oy, int& w, int& h)
	{
		ox = 0;
		oy = 0;
		w = (bt != NULL) ? bt->width : 0;
		h = (bt != NULL) ? bt->height : 0;
		if (bt == NULL)
			return;

		// Union of every chain level's footprint, expressed relative to the placed
		// (level-0) top-left corner. The engine keeps the footprint CENTERED on
		// upgrade, and that re-centering composes to a constant center across the
		// whole chain, so a level's top-left relative to the base is exactly
		// (decLeft - baseDecLeft, decTop - baseDecTop). The base level is the (0, 0)
		// origin; min/max over all levels gives the reservation box.
		const int baseDecLeft = bt->decLeft;
		const int baseDecTop  = bt->decTop;
		int minX = 0, minY = 0;
		int maxX = bt->width, maxY = bt->height;

		const BuildingType* cur = bt;
		while (cur != NULL && cur->nextLevel >= 0)
		{
			cur = globalContainer->buildingsTypes.get(cur->nextLevel);
			if (cur == NULL)
				break;
			const int relX = cur->decLeft - baseDecLeft;
			const int relY = cur->decTop  - baseDecTop;
			if (relX < minX)               minX = relX;
			if (relY < minY)               minY = relY;
			if (relX + cur->width  > maxX) maxX = relX + cur->width;
			if (relY + cur->height > maxY) maxY = relY + cur->height;
		}

		ox = minX;
		oy = minY;
		w  = maxX - minX;
		h  = maxY - minY;
	}

	bool anyCornWithin(const Map& map, int x, int y, int w, int h, int dist)
	{
		// The footprint expanded by `dist` in Chebyshev distance is exactly the
		// rectangle [x-dist, x+w+dist) x [y-dist, y+h+dist). The footprint interior
		// cannot hold CORN (it passed isHardSpaceForBuilding), so scanning it too is
		// harmless. Early-out on the first wheat tile.
		for (int dy = -dist; dy < h + dist; dy++)
			for (int dx = -dist; dx < w + dist; dx++)
			{
				const int nx = map.normalizeX(x + dx);
				const int ny = map.normalizeY(y + dy);
				if (map.getRessource(nx, ny).type == CORN)
					return true;
			}
		return false;
	}

	int countCornWithin(const Map& map, int x, int y, int w, int h, int dist)
	{
		// Forbidden-BLIND companion to countHarvestableCornWithin: counts every CORN
		// tile in the expanded footprint regardless of the forbidden mask. The gap
		// between this and the harvestable count is exactly the forbidden-but-present
		// corn — the discriminator between checkerboard-forbidding and field depletion.
		int count = 0;
		for (int dy = -dist; dy < h + dist; dy++)
			for (int dx = -dist; dx < w + dist; dx++)
			{
				const int nx = map.normalizeX(x + dx);
				const int ny = map.normalizeY(y + dy);
				if (map.getRessource(nx, ny).type == CORN)
					count++;
			}
		return count;
	}

	int countSurvivingCornWithin(const Map& map, int x, int y, int w, int h, int dist)
	{
		// Parity-aware count of the CORN tiles that SURVIVE Cortex's wheat-protection
		// checkerboard — the open half the paint leaves harvestable: CORN tiles whose
		// (x+y) parity is NOT the protected WHEAT_PARITY half (CortexWheat.cpp:179).
		//
		// Why not countHarvestableCornWithin (CORN AND !forbidden)? That reads the LIVE
		// forbidden mask, so it answers "harvestable RIGHT NOW" — which swings with the
		// paint's drain/repaint timing and reads ~zero on a freshly-revealed field the
		// checkerboard reconcile has not yet covered. This counts the SUSTAINED set: the
		// tiles that remain open once protection settles, independent of paint timing.
		// That is the durable signal placement and feedCapacity want — "will this field
		// keep an inn fed", not "is every open tile painted this exact tick". Depleted
		// tiles are no longer CORN, so genuine field exhaustion still zeroes it; only our
		// own (recoverable) checkerboard no longer does.
		int count = 0;
		for (int dy = -dist; dy < h + dist; dy++)
			for (int dx = -dist; dx < w + dist; dx++)
			{
				const int nx = map.normalizeX(x + dx);
				const int ny = map.normalizeY(y + dy);
				if (map.getRessource(nx, ny).type != CORN)
					continue;
				if (((nx + ny) & 1) == WHEAT_PARITY)
					continue; // the checkerboard-forbidden half: not sustained.
				count++;
			}
		return count;
	}

	int countHarvestableCornWithin(const Map& map, Uint32 teamMask,
	                               int x, int y, int w, int h, int dist)
	{
		// Same expanded-footprint scan box as anyCornWithin ([x-dist, x+w+dist) x
		// [y-dist, y+h+dist)), but COUNTS the CORN tiles this team may actually
		// harvest: a tile counts only when it is CORN AND not forbidden for teamMask.
		// Depleted field tiles are no longer CORN, and the checkerboard wheat-
		// protection paint sets `forbidden` on the protected half (which blocks
		// harvest but not regrowth), so both are excluded — leaving the live,
		// harvestable wheat the caller's MIN_TILES threshold is measured against.
		int count = 0;
		for (int dy = -dist; dy < h + dist; dy++)
			for (int dx = -dist; dx < w + dist; dx++)
			{
				const int nx = map.normalizeX(x + dx);
				const int ny = map.normalizeY(y + dy);
				if (map.getRessource(nx, ny).type != CORN)
					continue;
				if (map.isForbidden(nx, ny, teamMask))
					continue;
				count++;
			}
		return count;
	}

	bool candidateCrowdsInn(Game* game, Team* team, const Map& map,
	                        int x, int y, int w, int h)
	{
		if (game == NULL || team == NULL)
			return false;

		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building* b = team->myBuildings[i];
			if (b == NULL || b->buildingState == Building::DEAD)
				continue;
			if (b->type == NULL ||
			    b->type->shortTypeNum != IntBuildingType::FOOD_BUILDING)
				continue;

			// Reserve clearance around the footprint the inn can grow INTO (3x3),
			// not just its current size, so a non-inn building does not get placed
			// in the tiles a 2x2 inn would expand into when it upgrades. Growth is
			// anchored at the inn's (posX, posY), so the grown footprint shares it.
			int iw, ih;
			grownFootprint(b->type, iw, ih);

			// Compare the inn's occupied-side count with and without the candidate.
			// Reject only when the candidate pushes it past the limit AND actually
			// makes it worse, so a pre-existing >1 inn (e.g. grandfathered from
			// before this rule) does not block every placement around it.
			const int withCand = innOccupiedSides(map, b->posX, b->posY, iw, ih,
			                                       x, y, w, h);
			if (withCand <= CORTEX_INN_MAX_TOUCH_SIDES)
				continue;
			const int baseline = innOccupiedSides(map, b->posX, b->posY, iw, ih,
			                                       -1, -1, 0, 0);
			if (withCand > baseline)
				return true;
		}
		return false;
	}

	bool candidateOverlapsReservedExpansion(Game* game, Team* team, const Map& map,
	                                        int cgx, int cgy, int cew, int ceh)
	{
		if (game == NULL || team == NULL)
			return false;

		const int mapW = map.getW();
		const int mapH = map.getH();

		// Today only inns are protected by candidateCrowdsInn (side-clearance). But
		// racetracks (WALKSPEED) and pools (SWIMSPEED) also reserve a grown box at
		// placement, yet once built only their level-0 footprint occupies the map —
		// so a later building can land in the ring they will expand into and block the
		// upgrade forever. Generalize the inn rule to all three growing types: a new
		// candidate's grown box must not overlap the still-reserved grown box of any
		// existing inn / racetrack / pool. grownFootprintBox on the building's CURRENT
		// type returns the box covering its current footprint plus all remaining upgrade
		// levels, anchored relative to the current corner (posX, posY), so this works
		// whether the existing building is level 0 or already partly upgraded.
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building* b = team->myBuildings[i];
			if (b == NULL || b->buildingState == Building::DEAD)
				continue;
			if (b->type == NULL)
				continue;
			const int t = b->type->shortTypeNum;
			if (t != IntBuildingType::FOOD_BUILDING
			 && t != IntBuildingType::WALKSPEED_BUILDING
			 && t != IntBuildingType::SWIMSPEED_BUILDING)
				continue;

			int bgox, bgoy, bew, beh;
			grownFootprintBox(b->type, bgox, bgoy, bew, beh);
			const int bx = b->posX + bgox;
			const int by = b->posY + bgoy;
			if (rectsOverlap(cgx, cew, cgy, ceh, bx, bew, by, beh, mapW, mapH))
				return true;
		}
		return false;
	}
}
