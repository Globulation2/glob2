// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "BitArray.h"
#include "Ressource.h"

#include <SDL3/SDL_stdinc.h>

#include <cstddef>
#include <vector>

class Map;

//! Immutable copy of the per-tile map layers the renderer reads: terrain,
//! resources, discovery and fog of war for every team, and the locally displayed
//! team's forbidden/guard/clear areas. Extracted from the simulation's Map at a
//! tick boundary; afterwards it is only read, so the renderer can draw it while
//! the simulation advances. The query functions match Map's exactly.
class SceneMap
{
public:
	//! Copy the layers from map. Runs where the map may be read (the simulation side).
	//! displayW/H: the drawn map area in pixels (a client value, see ClientRequests).
	void extract(const Map &map, int displayW, int displayH);
	//! Single-threaded callers (tests, tools): take the drawn area from the map.
	void extract(const Map &map);

	int getW() const { return w; }
	int getH() const { return h; }
	int getMaskW() const { return wMask; }
	int getMaskH() const { return hMask; }
	//! Identity of the source map (Map::identity()); renewed when the map is replaced.
	Uint64 identity() const { return sourceIdentity; }
	//! Stable key for caches of drawn geometry: the same for every extraction of one map.
	const void *cacheKey() const { return sourceKey; }

	size_t coordToIndex(int x, int y) const { return (size_t(y & hMask) << wDec) + (x & wMask); }
	Uint16 getTerrain(int x, int y) const { return terrain[coordToIndex(x, y)]; }
	const Resource &getResource(int x, int y) const { return resources[coordToIndex(x, y)]; }
	const Resource &getResource(size_t pos) const { return resources[pos]; }
	bool isMapDiscovered(int x, int y, Uint32 visionMask) const
	{
		return (discovered[coordToIndex(x, y)] & visionMask) != 0;
	}
	bool isMapPartiallyDiscovered(int x1, int y1, int x2, int y2, Uint32 visionMask) const;
	bool isFOWDiscovered(int x, int y, int visionMask) const
	{
		return (fogOfWar[coordToIndex(x, y)] & visionMask) != 0;
	}
	bool isForbiddenInDisplayedView(int x, int y) const { return forbiddenView.get(coordToIndex(x, y)); }
	bool isGuardAreaInDisplayedView(int x, int y) const { return guardAreaView.get(coordToIndex(x, y)); }
	bool isClearAreaInDisplayedView(int x, int y) const { return clearAreaView.get(coordToIndex(x, y)); }
	bool canResourcesGrow(int x, int y) const { return resourcesGrow[coordToIndex(x, y)]; }
	Uint16 getGroundUnit(int x, int y) const { return groundUnits[coordToIndex(x, y)]; }
	Uint16 getAirUnit(int x, int y) const { return airUnits[coordToIndex(x, y)]; }
	Uint16 getBuilding(int x, int y) const { return buildings[coordToIndex(x, y)]; }
	//! Undermap terrain type (Map::getUMTerrain), as an int.
	int getUMTerrain(int x, int y) const { return undermap[coordToIndex(x, y)]; }
	//! Map::isHardSpaceForBuilding: every tile of the rectangle is grass, without a
	//! resource or a building.
	bool isHardSpaceForBuilding(int x, int y, int w, int h) const
	{
		for (int yi = y; yi < y + h; yi++)
			for (int xi = x; xi < x + w; xi++)
			{
				const size_t i = coordToIndex(xi, yi);
				if (resources[i].type != NO_RES_TYPE || buildings[i] != 0xFFFF || terrain[i] >= 16)
					return false;
			}
		return true;
	}
	void mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX, int viewportY) const;
	void mapCaseToDisplayableVector(int mx, int my, int *px, int *py, int viewportX, int viewportY, int screenW, int screenH) const;

private:
	int w = 0, h = 0, wMask = 0, hMask = 0, wDec = 0;
	Uint64 sourceIdentity = 0;
	const void *sourceKey = nullptr;
	int displayViewportW = 0, displayViewportH = 0;
	std::vector<Uint16> terrain, groundUnits, airUnits, buildings;
	std::vector<Resource> resources;
	std::vector<Uint8> resourcesGrow, undermap;
	std::vector<Uint32> discovered, fogOfWar;
	Utilities::BitArray forbiddenView, guardAreaView, clearAreaView;
};
