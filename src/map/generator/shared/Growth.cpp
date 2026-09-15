// SPDX-License-Identifier: GPL-3.0-or-later
#include "Growth.h"
#include "Morphology.h"
#include "Map.h"
#include <stdexcept>
#include "TerrainType.h"
namespace MapGeneration
{
int cropSeedsIn(const Map &map, const std::vector<unsigned char> &region)
{
	const Torus t(map);
	if (region.size() != size_t(t.size()))
		throw std::invalid_argument("cropSeedsIn requires one mask entry per map tile");
	int seeds = 0;
	for (int i = 0; i < t.size(); ++i)
		if (region[i])
		{
			const int type = map.getResource(i % t.w, i / t.w).type;
			seeds += type == WHEAT || type == WOOD;
		}
	return seeds;
}

Flood cropSpreadEnvelope(const Map &map)
{
	const Torus t(map);
	std::vector<unsigned char> seeds(t.size(), 0), grass(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = i % t.w, y = i / t.w;
		const int type = map.getResource(x, y).type;
		seeds[i] = type == WHEAT || type == WOOD;
		grass[i] = map.isGrass(x, y);
	}
	// Reuse the same toroidal eight-neighbour topology as the other region operations.
	return floodFrom(t, seeds, grass);
}

Fertility::Field cropGrowthField(const TerrainSketch &sketch, const Torus &t)
{
	const std::vector<unsigned char> water = pureTiles(sketch, t, WATER);
	const std::vector<unsigned char> sand = pureTiles(sketch, t, SAND);
	Fertility::Field field;
	field.rebuild(t.w, t.h, std::vector<std::uint8_t>(water.begin(), water.end()),
				  std::vector<std::uint8_t>(sand.begin(), sand.end()));
	return field;
}

double wateredShare(const Fertility::Field &field, const std::vector<unsigned char> &region,
					std::uint32_t minimum)
{
	const int w = field.getW();
	long long tiles = 0, watered = 0;
	for (size_t i = 0; i < region.size(); ++i)
		if (region[i])
		{
			++tiles;
			watered += field.at(int(i % w), int(i / w)) >= minimum;
		}
	return tiles ? double(watered) / double(tiles) : 0.0;
}

int wetTiles(const Fertility::Field &field, const std::vector<unsigned char> &region)
{
	const int w = field.getW();
	int wet = 0;
	for (size_t i = 0; i < region.size(); ++i)
		wet += region[i] && field.at(int(i % w), int(i / w)) > 0;
	return wet;
}

std::vector<unsigned char> dryZone(const Torus &t, const std::vector<unsigned char> &region)
{
	return dilate(t, region, kCropProbeReach);
}

int drainWithin(TerrainSketch &sketch, const std::vector<unsigned char> &zone)
{
	int drained = 0;
	for (size_t i = 0; i < sketch.size(); ++i)
		if (zone[i] && sketch[i] == WATER)
		{
			sketch[i] = GRASS;
			++drained;
		}
	return drained;
}
} // namespace MapGeneration
