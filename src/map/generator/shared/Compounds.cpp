// SPDX-License-Identifier: GPL-3.0-or-later
#include "Compounds.h"
#include "Map.h"
#include "Ressource.h"
#include <cstdlib>
namespace MapGeneration
{
void stampCompound(const Torus &t, const BaseSite &site, int radius, int gates, int gateWidth,
				   int label, CompoundMasks &masks)
{
	for (int v = -radius; v <= radius; ++v)
		for (int u = -radius; u <= radius; ++u)
		{
			const int i = baseTile(t, site, u, v);
			const bool onWall = std::abs(u) == radius || std::abs(v) == radius;
			if (!onWall)
			{
				if (masks.interiorOf[i] < 0)
					masks.interiorOf[i] = label;
				continue;
			}
			// A gate is the middle `gateWidth` tiles of its side; a side's corners never open.
			const int half = gateWidth / 2;
			const CompoundSide sides[4] = {CompoundSide::Front, CompoundSide::Back,
										  CompoundSide::Right, CompoundSide::Left};
			bool open = false;
			for (int g = 0; g < gates && g < 4 && !open; ++g)
				switch (sides[g])
				{
				case CompoundSide::Front:
					open = u == radius && std::abs(v) <= half;
					break;
				case CompoundSide::Back:
					open = u == -radius && std::abs(v) <= half;
					break;
				case CompoundSide::Right:
					open = v == radius && std::abs(u) <= half;
					break;
				case CompoundSide::Left:
					open = v == -radius && std::abs(u) <= half;
					break;
				}
			if (open)
				masks.gate[i] = 1;
			else
				masks.wall[i] = 1;
		}
}

bool compoundsApart(const Torus &t, const BaseSite &a, const BaseSite &b, int radius, int gap)
{
	return t.chebyshev(a.x, a.y, b.x, b.y) >= 2 * radius + 1 + gap;
}

std::string wallStanding(const Map &map, const Torus &t, const std::vector<unsigned char> &wall,
						 const std::vector<unsigned char> &doors, const char *what)
{
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = i % t.w, y = i / t.w;
		const bool stone = map.isResource(x, y) && map.getResource(x, y).type == STONE;
		if (wall[i] && !stone)
			return std::string("A ") + what + " has lost its stone at (" + std::to_string(x) + ", " +
				   std::to_string(y) + ").";
		if (doors[i] && stone)
			return std::string("A ") + what + "'s gate is walled up at (" + std::to_string(x) +
				   ", " + std::to_string(y) + ").";
	}
	return "";
}
} // namespace MapGeneration
