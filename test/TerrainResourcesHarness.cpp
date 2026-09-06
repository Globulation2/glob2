// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise real terrain regeneration and resource clearing without a window.
#include "GlobalContainer.h"
#include "Map.h"

#include <cassert>
#include <cstdio>
#include <vector>

GlobalContainer* globalContainer = nullptr;

int main()
{
	GlobalContainer globals;
	globalContainer = &globals;
	globals.runNoX = true;
	const int positions[][2] = {{8, 8}, {0, 0}, {15, 0}, {0, 15}, {15, 15}};
	int strokes = 0;
	for (int type = 0; type < MAX_RESOURCES; ++type)
		for (TerrainType paint : {GRASS, SAND, WATER})
			for (const auto& position : positions)
			{
				Map map;
				map.setSize(4, 4, static_cast<TerrainType>(globals.resourcesTypes.get(type)->terrain));
				for (int y = 0; y < 16; ++y)
					for (int x = 0; x < 16; ++x)
					{
						auto& resource = map.getResource(x, y);
						resource.type = type;
						resource.amount = 3;
					}

				// An adjacent second stroke also checks overlapping brush footprints.
				for (int offset : {0, 1})
				{
					const int px = position[0] + offset, py = position[1];
					std::vector<Resource> before;
					for (int y = 0; y < 16; ++y)
						for (int x = 0; x < 16; ++x)
							before.push_back(map.getResource(x, y));

					// The map operations used by MapEdit::handleTerrainClick.
					map.setUMatPos(px, py, paint, 1);
					map.removeUnallowedResources(px - 2, py - 2, 4, 4);
					if (paint == GRASS)
						for (int y = py - 1; y <= py; ++y)
							for (int x = px - 1; x <= px; ++x)
								map.getResource(x, y).clear();

					// Independent whole-map oracle: retain every compatible resource,
					// except the four tiles explicitly cleared by the grass brush.
					for (int y = 0; y < 16; ++y)
						for (int x = 0; x < 16; ++x)
						{
							Resource expected = before[y * 16 + x];
							const bool bareGrass = paint == GRASS &&
								(x == (px & 15) || x == ((px - 1) & 15)) &&
								(y == (py & 15) || y == ((py - 1) & 15));
							if (bareGrass || (expected.type != NO_RES_TYPE &&
								map.getTerrainType(x, y) != globals.resourcesTypes.get(expected.type)->terrain))
								expected.clear();
							assert(map.getResource(x, y).getUint32() == expected.getUint32());
						}
					++strokes;
				}
			}
	std::printf("Terrain resource regressions passed: %d strokes, all 8 resources, 3 terrains, interior and four wrapped corners\n", strokes);
}
