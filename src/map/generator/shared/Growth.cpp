// SPDX-License-Identifier: GPL-3.0-or-later
#include "Growth.h"
#include "Map.h"
#include "Morphology.h"
#include "Map.h"
#include <stdexcept>
#include "TerrainType.h"
#include "TerrainProperties.h"
namespace MapGeneration
{
namespace
{
bool spreadingCrop(const Map& map, int type)
{
    if (type==NO_RES_TYPE) return false;
    const auto& properties=map.resourceProperties(type);
    return properties.spreadRate && (properties.materialMask &
        (materialBit(MaterialId::Food)|materialBit(MaterialId::Wood)));
}
}

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
			seeds += spreadingCrop(map, type);
		}
	return seeds;
}

Flood cropSpreadEnvelope(const Map &map, const Fertility::Field *fertility)
{
	const Torus t(map);
	std::vector<unsigned char> seeds(t.size(), 0), grass(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = i % t.w, y = i / t.w;
		const int type = map.getResource(x, y).type;
		seeds[i] = (spreadingCrop(map, type)) && (!fertility || fertility->at(x, y) > 0);
		// A tile whose growth flag is off never takes a crop, so the envelope stops at it as
		// the engine does. No generated map sets the flag; loaded scenario maps may.
		grass[i] = map.terrainSupportsMaterialAt(x,y,MaterialId::Food) && map.canResourcesGrow(x, y);
	}
	// Reuse the same toroidal eight-neighbour topology as the other region operations.
	auto result = floodFrom(t, seeds, grass);
	if (fertility)
		for (int i = 0; i < t.size(); ++i)
		{
			const int type = map.getResource(i % t.w, i / t.w).type;
			if ((spreadingCrop(map, type)) && result.steps[i] < 0)
			{
				result.steps[i] = 0;
				result.visited.push_back(i);
			}
		}
	return result;
}

std::vector<unsigned char> fertileCropEnvelope(const Map &map, const Fertility::Field &fertility)
{
	const Torus t(map);
	std::vector<unsigned char> reached(t.size(), 0);
	std::vector<int> queue;
	for (int i = 0; i < t.size(); ++i)
	{
		const int type = map.getResource(i % t.w, i / t.w).type;
		if (spreadingCrop(map, type))
		{
			reached[i] = 1;
			if (fertility.at(i % t.w, i / t.w) > 0) queue.push_back(i);
		}
	}
	for (size_t head = 0; head < queue.size(); ++head)
	{
		const int i = queue[head];
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int q = t.at(i % t.w + dx, i / t.w + dy);
				if (reached[q] || !map.terrainSupportsMaterialAt(q % t.w,q / t.w,MaterialId::Food) ||
					!map.canResourcesGrow(q % t.w, q / t.w)) continue;
				reached[q] = 1;
				if (fertility.at(q % t.w, q / t.w) > 0) queue.push_back(q);
			}
	}
	return reached;
}

Fertility::Field cropGrowthField(const TerrainSketch &sketch, const Torus &t)
{
	std::vector<std::int16_t> contribution(t.size(),0);
	std::vector<std::uint16_t> inhibition(t.size(),0), growth(t.size(),256);
	for (unsigned id=0; id<TERRAIN_COUNT; ++id)
	{
		const auto& properties=terrainProperties(static_cast<TerrainType>(id));
		const auto pure=pureTiles(sketch,t,static_cast<TerrainType>(id));
		for (int i=0; i<t.size(); ++i) if(pure[i])
		{
			contribution[i]=properties.fertilitySource ? properties.fertilityQ8 : 0;
			inhibition[i]=properties.inhibitionQ8;
			growth[i]=properties.growthQ8;
		}
	}
	Fertility::Field field;
	field.rebuildWeighted(t.w,t.h,contribution,inhibition);
	// The public sketch field measures potential before habitat gating, just
	// like the ungated map field; local modifiers are applied for crop habitats.
	field.multiplyLocal(growth);
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

namespace MapGeneration
{
std::uint32_t meanFertilityAround(const Fertility::Field &field, const Torus &t, int site,
								  int radius)
{
	const int sx = site % t.w, sy = site / t.w;
	std::uint64_t sum = 0;
	for (int dy = -radius; dy <= radius; ++dy)
		for (int dx = -radius; dx <= radius; ++dx)
			sum += field.at(t.x(sx + dx), t.y(sy + dy));
	const std::uint64_t tiles = std::uint64_t(2 * radius + 1) * (2 * radius + 1);
	return std::uint32_t(sum / tiles);
}

std::vector<std::uint32_t> meanFertilityField(const Fertility::Field &field, const Torus &t,
											  int radius)
{
	const int n = t.size();
	const int span = 2 * radius + 1;
	// Sums along each row over the window, then along each column over those sums.
	std::vector<std::uint64_t> rows(size_t(n), 0);
	for (int y = 0; y < t.h; ++y)
	{
		std::uint64_t sum = 0;
		for (int dx = -radius; dx <= radius; ++dx)
			sum += field.at(t.x(dx), y);
		for (int x = 0; x < t.w; ++x)
		{
			rows[size_t(y * t.w + x)] = sum;
			sum += field.at(t.x(x + radius + 1), y);
			sum -= field.at(t.x(x - radius), y);
		}
	}
	std::vector<std::uint32_t> means(size_t(n), 0);
	const std::uint64_t tiles = std::uint64_t(span) * span;
	for (int x = 0; x < t.w; ++x)
	{
		std::uint64_t sum = 0;
		for (int dy = -radius; dy <= radius; ++dy)
			sum += rows[size_t(t.y(dy) * t.w + x)];
		for (int y = 0; y < t.h; ++y)
		{
			means[size_t(y * t.w + x)] = std::uint32_t(sum / tiles);
			sum += rows[size_t(t.y(y + radius + 1) * t.w + x)];
			sum -= rows[size_t(t.y(y - radius) * t.w + x)];
		}
	}
	return means;
}
} // namespace MapGeneration
