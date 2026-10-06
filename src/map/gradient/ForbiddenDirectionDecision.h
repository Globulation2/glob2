// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Map.h"
#include "MapInternal.h"

// Pure scoring over published inputs. Escape distance dominates the optional
// destination preference; reads must not refresh either field during auditing.
inline int evaluateForbiddenDirection(const Map &map, const Uint16 *base,
									 const Uint16 *option, int swim, int x, int y)
{
	if (!base)
		return -1;
	Uint16 bestBase = 0, bestOption = 0;
	int best = -1;
	for (int d = 0; d < 8; ++d)
	{
		const int nx = x + tabClose[d][0], ny = y + tabClose[d][1];
		if (!map.isFreeForGroundUnitNoForbidden(nx, ny, swim > 0))
			continue;
		const auto cell = map.coordToIndex(nx, ny);
		const auto value = base[cell], preference = option ? option[cell] : Uint16(0);
		if (value > bestBase || (value == bestBase && preference > bestOption))
		{
			bestBase = value;
			bestOption = preference;
			best = d;
		}
	}
	return bestBase > GRADIENT_UNREACHABLE ? best : -1;
}
