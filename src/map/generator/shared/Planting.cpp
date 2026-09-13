// SPDX-License-Identifier: GPL-3.0-or-later
#include "Planting.h"
#include "GenerationContext.h"
#include "Resources.h"
#include "Wedge.h"
#include <algorithm>
#include <cstdlib>
namespace MapGeneration
{
bool clearGround(const Map &map, int x, int y)
{
	return map.isGrass(x, y) && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID &&
		   map.getGroundUnit(x, y) == NOGUID;
}

std::vector<unsigned char> swarmSurroundings(const Torus &t, const GenerationContext &context,
											 int clearance)
{
	std::vector<unsigned char> reserved(size_t(t.w) * t.h, 0);
	for (int team = 0; team < context.request.nbTeams; ++team)
		for (int dy = -clearance; dy < 4 + clearance; ++dy)
			for (int dx = -clearance; dx < 4 + clearance; ++dx)
				reserved[t.at(context.bootX[team] + dx, context.bootY[team] + dy)] = 1;
	return reserved;
}

void clearAroundSwarms(Map &map, const GenerationContext &context, const Torus &t,
					   const std::vector<unsigned char> *keep)
{
	for (int team = 0; team < context.request.nbTeams; ++team)
		for (int dy = -kSwarmClearance; dy < 4 + kSwarmClearance; ++dy)
			for (int dx = -kSwarmClearance; dx < 4 + kSwarmClearance; ++dx)
			{
				const int x = t.x(context.bootX[team] + dx), y = t.y(context.bootY[team] + dy);
				if (map.isResource(x, y) && !(keep && (*keep)[y * t.w + x]))
					map.setNoResource(x, y, 1);
			}
}

namespace
{
// Map::growResources' algae test, evaluated exactly for chosen tiles. Each offset is the difference
// of two independent draws of 0 to 15: -15 to 15, weighted 16 - |d| out of 256.
class AlgaeGrowth
{
  public:
	AlgaeGrowth(const Map &map, const Torus &t) : t(t), water(t.size(), 0), sand(t.size(), 0)
	{
		for (int y = 0; y < t.h; ++y)
			for (int x = 0; x < t.w; ++x)
			{
				water[y * t.w + x] = map.isWater(x, y);
				sand[y * t.w + x] = map.isSand(x, y);
			}
		for (int d = -15; d <= 15; ++d)
			weight[d + 15] = (16 - std::abs(d)) / 256.0;
		// The sand the test looks for lies within 30 tiles; beyond that the chance is 0.
		toSand = stepsFrom(t, sand);
	}
	double at(int i) const
	{
		if (!water[i] || toSand[i] < 0 || toSand[i] > 30)
			return 0;
		// Map sizes are powers of two, so wrapping is a mask; this loop runs 961 times a tile.
		const int x = i % t.w, y = i / t.w, wm = t.w - 1, hm = t.h - 1;
		double sum = 0;
		for (int dy = -15; dy <= 15; ++dy)
		{
			const int waterRow = ((y + dy) & hm) * t.w, sandColumn = (x + 2 * dy) & wm;
			double row = 0;
			for (int dx = -15; dx <= 15; ++dx)
				if (water[waterRow + ((x + dx) & wm)] &&
					sand[((y + 2 * dx) & hm) * t.w + sandColumn])
					row += weight[dx + 15];
			sum += row * weight[dy + 15];
		}
		return sum;
	}

  private:
	const Torus &t;
	std::vector<unsigned char> water, sand;
	std::vector<int> toSand;
	double weight[31];
};
} // namespace

std::vector<double> algaeGrowthChance(const Map &map, const Torus &t)
{
	const AlgaeGrowth growth(map, t);
	std::vector<double> chance(t.size(), 0.0);
	for (int i = 0; i < t.size(); ++i)
		chance[i] = growth.at(i);
	return chance;
}

void seedAlgae(Map &map, GenerationContext &context, const Torus &t, const char *stream,
			   int algaePercent, const AlgaeBand &band, const WedgeFrame *wedges)
{
	const int n = t.w * t.h;
	std::vector<MapGeneratorPoint> water;
	if (band.nearestOffshore < 0)
	{
		for (int i = 0; i < n; ++i)
			if (map.isWater(i % t.w, i / t.w))
				water.emplace_back(i % t.w, i / t.w);
	}
	else
	{
		std::vector<unsigned char> wet(n), dry(n);
		for (int y = 0; y < t.h; ++y)
			for (int x = 0; x < t.w; ++x)
			{
				wet[y * t.w + x] = map.isWater(x, y);
				dry[y * t.w + x] = !map.isWater(x, y);
			}
		const std::vector<int> offshore = stepsFrom(t, dry, wet);
		for (int i = 0; i < n; ++i)
			if (offshore[i] >= band.nearestOffshore && offshore[i] <= band.farthestOffshore)
				water.emplace_back(i % t.w, i / t.w);
	}
	if (water.empty())
		return;
	// The count follows the whole band. With a best share, the clumps then go only on the water
	// in it where algae regrows most readily, so the same amount of algae sits where it lasts.
	const int clumps = scaledCount(int(water.size()) / band.tilesPerClump, algaePercent);
	if (band.bestShare <= 0 && !wedges)
	{
		for (int clump = 0; clump < clumps; ++clump)
			placeResourceClump(map, context, water[context.bounded(stream, water.size())], ALGA,
							   band.clumpRadius);
		return;
	}
	// Only the band's own tiles are measured: the test costs 961 lookups a tile.
	const AlgaeGrowth growth(map, t);
	const int groups = wedges ? wedges->teams : 1;
	std::vector<std::vector<std::pair<double, int>>> byGroup(groups);
	for (const MapGeneratorPoint &p : water)
	{
		const int i = p.y * t.w + p.x;
		byGroup[wedges ? wedges->cell(p.x, p.y).k : 0].push_back(
			{band.bestShare > 0 ? -growth.at(i) : 0.0, i});
	}
	for (auto &group : byGroup)
	{
		if (group.empty())
			continue;
		std::stable_sort(group.begin(), group.end());
		if (band.bestShare > 0)
			group.resize(std::max<size_t>(1, size_t(std::lround(group.size() * band.bestShare))));
		for (int clump = 0; clump < clumps / groups; ++clump)
		{
			const int i = group[context.bounded(stream, group.size())].second;
			placeResourceClump(map, context, {i % t.w, i / t.w}, ALGA, band.clumpRadius);
		}
	}
}

void stockIslands(Map &map, GenerationContext &context, const std::vector<Island> &islands,
				  const char *stream)
{
	const int width = map.getW();
	for (const Island &island : islands)
	{
		std::vector<MapGeneratorPoint> grass;
		for (int i : island.tiles)
			if (map.isGrass(i % width, i / width))
				grass.emplace_back(i % width, i / width);
		if (grass.empty())
			continue;
		MapGeneratorPoint centre(island.x, island.y);
		if (!map.isGrass(centre.x, centre.y))
			centre = grass[context.bounded(stream, grass.size())];
		switch (context.bounded(stream, 3))
		{
		case 0:
			placeResourceClump(map, context, centre, STONE, 2);
			break;
		case 1:
			placeResourceClump(map, context, centre, CHERRY + int(context.bounded(stream, 3)), 2);
			break;
		default:
			placeResourceClump(map, context, centre, CORN, 2);
			break;
		}
	}
}
} // namespace MapGeneration
