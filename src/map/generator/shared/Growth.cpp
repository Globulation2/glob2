#include "GenerationWork.h"
#include "GenerationFertilityWork.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PowerOfTwo.h"
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
    const auto& properties=map.resourcePropertiesByIndex(type);
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
	{
		::MapGeneration::generationCheckpoint();
		if (region.at(i))
		{
			const int type = map.getResource(t.remainderX(i), i / t.w).type;
			seeds += spreadingCrop(map, type);
		}
	}
	return seeds;
}

Flood cropSpreadEnvelope(const Map &map, const Fertility::Field *fertility)
{
	const Torus t(map);
	std::vector<unsigned char> seeds(t.size(), 0), grass(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		const int x = t.remainderX(i), y = i / t.w;
		const int type = map.getResource(x, y).type;
		seeds.at(i) = (spreadingCrop(map, type)) && (!fertility || fertility->at(x, y) > 0);
		// A tile whose growth flag is off never takes a crop, so the envelope stops at it as
		// the engine does. No generated map sets the flag; loaded scenario maps may.
		grass.at(i) =
			map.terrainSupportsMaterialAt(x, y, MaterialId::Food) && map.canResourcesGrow(x, y);
	}
	// Reuse the same toroidal eight-neighbour topology as the other region operations.
	auto result = floodFrom(t, seeds, grass);
	if (fertility)
		for (int i = 0; i < t.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			const int type = map.getResource(t.remainderX(i), i / t.w).type;
			if ((spreadingCrop(map, type)) && result.steps.at(i) < 0)
			{
				result.steps.at(i) = 0;
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
		::MapGeneration::generationCheckpoint();
		const int type = map.getResource(t.remainderX(i), i / t.w).type;
		if (spreadingCrop(map, type))
		{
			reached.at(i) = 1;
			if (fertility.at(t.remainderX(i), i / t.w) > 0) queue.push_back(i);
		}
	}
	for (size_t head = 0; head < queue.size(); ++head)
	{
		::MapGeneration::generationCheckpoint();
		const int i = queue.at(head);
		for (int dy = -1; dy <= 1; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -1; dx <= 1; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				const int q = t.at(t.remainderX(i) + dx, i / t.w + dy);
				if (reached.at(q) ||
					!map.terrainSupportsMaterialAt(t.remainderX(q), q / t.w, MaterialId::Food) ||
					!map.canResourcesGrow(t.remainderX(q), q / t.w))
					continue;
				reached.at(q) = 1;
				if (fertility.at(t.remainderX(q), q / t.w) > 0) queue.push_back(q);
			}
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
		::MapGeneration::generationCheckpoint();
		const auto& properties=terrainProperties(static_cast<TerrainType>(id));
		const auto pure=pureTiles(sketch,t,static_cast<TerrainType>(id));
		for (int i = 0; i < t.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (pure.at(i))
			{
				contribution.at(i) = properties.fertilitySource ? properties.fertilityQ8 : 0;
				inhibition.at(i) = properties.inhibitionQ8;
				growth.at(i) = properties.growthQ8;
			}
		}
	}
	Fertility::Field field;
	generationFertilityRebuildWork(t.w, t.h);
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
	{
		::MapGeneration::generationCheckpoint();
		if (region.at(i))
		{
			++tiles;
			watered += field.at(int(dimensionRemainder(i, w)), int(i / w)) >= minimum;
		}
	}
	return tiles ? double(watered) / double(tiles) : 0.0;
}

int wetTiles(const Fertility::Field &field, const std::vector<unsigned char> &region)
{
	const int w = field.getW();
	int wet = 0;
	for (size_t i = 0; i < region.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		wet += region.at(i) && field.at(int(dimensionRemainder(i, w)), int(i / w)) > 0;
	}
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
	{
		::MapGeneration::generationCheckpoint();
		if (zone.at(i) && sketch.at(i) == WATER)
		{
			sketch.at(i) = GRASS;
			++drained;
		}
	}
	return drained;
}

} // namespace MapGeneration

namespace MapGeneration
{
std::uint32_t meanFertilityAround(const Fertility::Field &field, const Torus &t, int site,
								  int radius)
{
	const int sx = t.remainderX(site), sy = site / t.w;
	std::uint64_t sum = 0;
	for (int dy = -radius; dy <= radius; ++dy)
	{
		::MapGeneration::generationCheckpoint();
		for (int dx = -radius; dx <= radius; ++dx)
		{
			::MapGeneration::generationCheckpoint();
			sum += field.at(t.x(sx + dx), t.y(sy + dy));
		}
	}
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
		::MapGeneration::generationCheckpoint();
		std::uint64_t sum = 0;
		for (int dx = -radius; dx <= radius; ++dx)
		{
			::MapGeneration::generationCheckpoint();
			sum += field.at(t.x(dx), y);
		}
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			rows.at(size_t(y * t.w + x)) = sum;
			sum += field.at(t.x(x + radius + 1), y);
			sum -= field.at(t.x(x - radius), y);
		}
	}
	std::vector<std::uint32_t> means(size_t(n), 0);
	const std::uint64_t tiles = std::uint64_t(span) * span;
	for (int x = 0; x < t.w; ++x)
	{
		::MapGeneration::generationCheckpoint();
		std::uint64_t sum = 0;
		for (int dy = -radius; dy <= radius; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			sum += rows.at(size_t(t.y(dy) * t.w + x));
		}
		for (int y = 0; y < t.h; ++y)
		{
			::MapGeneration::generationCheckpoint();
			means.at(size_t(y * t.w + x)) = std::uint32_t(sum / tiles);
			sum += rows.at(size_t(t.y(y + radius + 1) * t.w + x));
			sum -= rows.at(size_t(t.y(y - radius) * t.w + x));
		}
	}
	return means;
}
} // namespace MapGeneration
