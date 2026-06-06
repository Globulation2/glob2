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
	} // namespace

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
}
