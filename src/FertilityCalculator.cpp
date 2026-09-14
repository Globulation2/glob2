// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#include "FertilityCalculator.h"

#include "Map.h"
#include "map/FertilityField.h"

#include <algorithm>
#include <limits>

namespace FertilityCalculator
{
	void compute(Map& map, const ProgressCallback& progress)
	{
		if (progress)
			progress(0.0f);

		const Fertility::Field field = Fertility::forMap(map);

		if (progress)
			progress(0.8f);

		// Fertility::kScale is one past what Tile::fertility holds. Only a tile whose
		// entire neighbourhood is qualifying water reaches it, which no grass tile can.
		constexpr std::uint32_t kCeiling = std::numeric_limits<Uint16>::max();
		Uint16 fertilityMax = 0;
		for (int x = 0; x < map.getW(); ++x)
			for (int y = 0; y < map.getH(); ++y)
			{
				const Uint16 value =
					static_cast<Uint16>(std::min(field.at(x, y), kCeiling));
				map.getTile(x, y).fertility = value;
				fertilityMax = std::max(fertilityMax, value);
			}
		map.fertilityMaximum = fertilityMax;

		if (progress)
			progress(1.0f);
	}
}
