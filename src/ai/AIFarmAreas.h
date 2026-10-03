// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Shared rule for AIs that farm wheat with a farm area (the farm-areas
// experiment, docs/features/farm-areas.md) instead of forbidden-zone patterns.
// Without the experiment the AIs keep their original forbidden-zone farming;
// they check Map::farmAreasEnabled() before using anything here.

#include "Map.h"
#include "Ressource.h"

namespace AIFarmAreas
{
	//! Whether an AI farming wheat should keep (x,y) inside its farm area: the
	//! ground can grow wheat (Map::canPaintFarmArea) and the tile holds wheat or
	//! touches it, so the farm covers the field and the ring it grows into. Each
	//! AI adds its own reach-from-water and discovery conditions on top.
	inline bool wantsFarm(const Map& map, int x, int y)
	{
		if (!map.canPaintFarmArea(x, y))
			return false;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (map.getResource(x + dx, y + dy).type == WHEAT)
					return true;
		return false;
	}
}
