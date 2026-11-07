// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeightFieldLooks.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationWork.h"
#include "HeightMap.h"
#include "Map.h"
#include "Morphology.h"
#include "Rock.h"
#include "TerrainTypeTable.h"
#include "Topology.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace MapGeneration
{
TerrainSketch heightFieldSketch(const GenerationRequest &request, GenerationContext &context,
								const HeightFieldOptions &options, const HeightFieldBuilder &build,
								std::vector<int> *deposits)
{
	// The same steps, in the same order, as generateHeightField up to its terrain (Terrain.cpp).
	const int w = 1 << request.wDec, h = 1 << request.hDec;
	const float smoothingFactor = (float)(options.smoothing + 4) * 3;
	const HeightFieldTiling tiling = heightFieldTiling(w, h, options.repeat);
	HeightMap hm(tiling.w, tiling.h, context.stream("heightmap"));
	build(hm, tiling.w, tiling.h, smoothingFactor);
	const HeightFieldLevels levels = classifyHeightField(hm, tiling, options);
	Game game(nullptr);
	Map &scratch = game.map;
	scratch.setSize(request.wDec, request.hDec);
	scratch.setGame(&game);
	paintHeightFieldTerrain(scratch, hm, tiling, levels);
	if (deposits)
	{
		paintHeightFieldResources(scratch, hm, tiling, levels, options);
		deposits->assign(size_t(w) * h, NO_RES_TYPE);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				::MapGeneration::generationCheckpoint();
				(*deposits)[size_t(y) * w + x] = scratch.getResource(x, y).type;
			}
	}
	return mapSketch(scratch);
}

TerrainSketch mapSketch(const Map &map)
{
	const auto state = map.vertexTerrainState();
	TerrainSketch sketch(state.size());
	for (size_t i = 0; i < state.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		sketch[i] = (unsigned char)state[i];
	}
	return sketch;
}

std::vector<unsigned char> wetVertices(const TerrainSketch &sketch)
{
	std::vector<unsigned char> wet(sketch.size(), 0);
	for (size_t i = 0; i < sketch.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		const TerrainGroup group = terrainGroup(TerrainType(sketch[i]));
		wet[i] = group == TerrainGroup::Water || group == TerrainGroup::DeepWater;
	}
	return wet;
}

std::vector<int> stepsFromWet(const Torus &t, const TerrainSketch &sketch)
{
	const std::vector<unsigned char> wet = wetVertices(sketch);
	if (std::find(wet.begin(), wet.end(), 1) == wet.end())
		return std::vector<int>(sketch.size(), -1);
	return stepsFrom(t, wet);
}

std::vector<int> distanceFromWet(const Torus &t, const TerrainSketch &sketch)
{
	const std::vector<unsigned char> wet = wetVertices(sketch);
	if (std::find(wet.begin(), wet.end(), 1) == wet.end())
		return std::vector<int>(sketch.size(), -1);
	const std::vector<std::int64_t> d2 = distanceSquaredTo(t, wet);
	std::vector<int> distance(sketch.size(), 0);
	for (size_t i = 0; i < sketch.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		// Integer square root, then rounded: d2 - r^2 > r means sqrt(d2) is past r + 1/2.
		std::int64_t r = std::int64_t(std::sqrt(double(d2[i])));
		while ((r + 1) * (r + 1) <= d2[i])
			++r;
		while (r * r > d2[i])
			--r;
		distance[i] = int(d2[i] - r * r > r ? r + 1 : r);
	}
	return distance;
}

int deepenWater(TerrainSketch &sketch, const Torus &t, int steps, int least, TerrainType deep,
				const std::vector<unsigned char> *eligible)
{
	std::vector<unsigned char> water(sketch.size(), 0);
	for (size_t i = 0; i < sketch.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		water[i] = sketch[i] == WATER;
	}
	std::vector<unsigned char> core = deepCore(t, water, steps);
	if (eligible)
		for (size_t i = 0; i < core.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			core[i] = core[i] && eligible->at(i);
		}
	core = dropSmallRegions(t, core, std::max(1, least));
	int deepened = 0;
	for (size_t i = 0; i < core.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (core[i])
		{
			sketch[i] = (unsigned char)deep;
			++deepened;
		}
	}
	return deepened;
}

int deepStepsForShare(const TerrainSketch &sketch, const Torus &t, int least, int permille)
{
	std::vector<unsigned char> land(sketch.size(), 0);
	long long water = 0;
	for (size_t i = 0; i < sketch.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		land[i] = sketch[i] != WATER;
		water += sketch[i] == WATER;
	}
	const int floor = std::max(2, least);
	if (water == 0 || water == (long long)sketch.size())
		return floor;
	const std::vector<int> fromLand = stepsFrom(t, land);
	std::vector<long long> atLeast;
	for (size_t i = 0; i < sketch.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (!land[i])
		{
			if (int(atLeast.size()) <= fromLand[i])
				atLeast.resize(size_t(fromLand[i]) + 1, 0);
			++atLeast[size_t(fromLand[i])];
		}
	}
	// Running from the deepest step down, the count at or beyond each step.
	for (int s = int(atLeast.size()) - 2; s >= 0; --s)
		atLeast[size_t(s)] += atLeast[size_t(s) + 1];
	int steps = floor;
	while (steps < int(atLeast.size()) && atLeast[size_t(steps)] * 1000 > water * permille)
		++steps;
	return steps;
}

std::vector<int> wetShare(const Torus &t, const std::vector<unsigned char> &wet, int radius)
{
	const std::vector<int> count = windowCount(t, wet, radius);
	const long long window = (2LL * radius + 1) * (2LL * radius + 1);
	std::vector<int> share(count.size(), 0);
	for (size_t i = 0; i < count.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		share[i] = int(1000LL * count[i] / window);
	}
	return share;
}

std::vector<unsigned char> majorWater(const Torus &t, const std::vector<unsigned char> &wet, int share)
{
	const std::vector<int> label = connectedRegions(wet, t.w, t.h, true, GridNeighbors::Eight);
	int bodies = 0;
	for (int l : label)
		bodies = std::max(bodies, l + 1);
	std::vector<long long> size(size_t(bodies), 0);
	for (int l : label)
	{
		::MapGeneration::generationCheckpoint();
		if (l >= 0)
			++size[size_t(l)];
	}
	const long long largest = bodies ? *std::max_element(size.begin(), size.end()) : 0;
	std::vector<unsigned char> major(wet.size(), 0);
	for (size_t i = 0; i < wet.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		major[i] = label[i] >= 0 && size[size_t(label[i])] * 1000 >= largest * share;
	}
	return major;
}

int layLevee(TerrainSketch &sketch, const std::vector<int> &fromWet, const std::vector<unsigned char> &eligible,
			 int nearest, int farthest, const std::vector<int> &widen, int cut, TerrainType from,
			 TerrainType fertile)
{
	int laid = 0;
	nearest = std::max(1, nearest);
	for (size_t i = 0; i < sketch.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (sketch[i] != from || !eligible.at(i) || fromWet[i] < nearest)
			continue;
		const int reach = farthest + (!widen.empty() && widen[i] > cut ? 1 : 0);
		if (fromWet[i] > reach)
			continue;
		sketch[i] = (unsigned char)fertile;
		++laid;
	}
	return laid;
}

int paveBeyondWater(TerrainSketch &sketch, const std::vector<int> &fromWet, const std::vector<unsigned char> &eligible,
					const std::vector<int> &jitter, int reach, int fray, TerrainType from, TerrainType to)
{
	int paved = 0;
	for (size_t i = 0; i < sketch.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (sketch[i] != from || !eligible.at(i) || fromWet[i] < 0)
			continue;
		const int extra = jitter.empty() ? 0 : fray * (65535 - std::clamp(jitter[i], 0, 65535)) / 65536;
		if (fromWet[i] >= reach + std::max(0, extra))
		{
			sketch[i] = (unsigned char)to;
			++paved;
		}
	}
	return paved;
}

void gradeGrassLooks(TerrainSketch &looks, const TerrainSketch &mechanics, const std::vector<int> &fromWet,
					 const std::vector<unsigned char> &plain, const std::vector<int> &jitter, int lush, int green,
					 int shift)
{
	for (size_t i = 0; i < mechanics.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (mechanics[i] != GRASS || plain.at(i) || fromWet[i] < 0)
			continue;
		const double move = jitter.empty() ? 0.0 : shift * (jitter[i] - 32768) / 32768.0;
		if (fromWet[i] <= lush + move)
			looks[i] = LUSH_GRASS;
		else if (fromWet[i] > green + move)
			looks[i] = DRY_GRASS;
	}
}

std::vector<unsigned char> homeGardens(const Torus &t, const std::vector<int> &bootX, const std::vector<int> &bootY,
									   int teams, double radius, const std::vector<int> &jitter, double fray)
{
	std::vector<unsigned char> garden(size_t(t.size()), 0);
	const int reach = int(std::ceil(radius + std::abs(fray))) + 1;
	for (int k = 0; k < teams; ++k)
		for (int dy = -reach; dy <= reach; ++dy)
			for (int dx = -reach; dx <= reach; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				const int i = t.at(bootX.at(size_t(k)) + 2 + dx, bootY.at(size_t(k)) + 2 + dy);
				const double r = radius + (jitter.empty() ? 0.0 : fray * (jitter[i] - 32768) / 32768.0);
				if (dx * dx + dy * dy <= r * r)
					garden[i] = 1;
			}
	return garden;
}

DisplacedResources writeLayeredTerrain(Map &map, const TerrainSketch &mechanics, const TerrainSketch &looks)
{
	writeVertices(map, composeLooks(mechanics, looks));
	DisplacedResources lost;
	const int w = map.getW(), h = map.getH();
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const int type = map.getResource(x, y).type;
			if (type == NO_RES_TYPE || map.terrainSupportsResourceAtByIndex(x, y, type))
				continue;
			switch (type)
			{
			case WHEAT:
				++lost.wheat;
				break;
			case WOOD:
				++lost.wood;
				break;
			case STONE:
				++lost.stone;
				break;
			case ALGA:
				++lost.algae;
				lost.algaeTiles.push_back(y * w + x);
				break;
			case CHERRY:
			case ORANGE:
			case PRUNE:
				++lost.fruit;
				break;
			default:
				++lost.other;
				break;
			}
			map.replaceResource(x, y, Resource{});
		}
	return lost;
}

int partingBarrier(const Map &map)
{
	const Torus t(map);
	const int n = t.size();
	std::vector<unsigned char> open(size_t(n), 0), blocked(size_t(n), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const auto &p = map.terrainPropertiesAt(x, y);
			open[size_t(t.at(x, y))] = p.walkable;
			blocked[size_t(t.at(x, y))] = !p.walkable && !p.swimmable;
		}
	const std::vector<int> component = connectedRegions(open, t.w, t.h, true, GridNeighbors::Eight);
	const std::vector<int> label = connectedRegions(blocked, t.w, t.h, true, GridNeighbors::Eight);
	int groups = 0;
	for (int l : label)
		groups = std::max(groups, l + 1);
	std::vector<int> seen(size_t(groups), -1);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (label[i] < 0)
			continue;
		const int x = t.remainderX(i), y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = t.at(x + dx, y + dy);
				if (!open[size_t(j)])
					continue;
				int &first = seen[size_t(label[i])];
				if (first < 0)
					first = component[size_t(j)];
				else if (first != component[size_t(j)])
					return i;
			}
	}
	return -1;
}
} // namespace MapGeneration
