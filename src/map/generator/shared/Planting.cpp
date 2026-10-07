// SPDX-License-Identifier: GPL-3.0-or-later
#include "Planting.h"
#include "Morphology.h"
#include "GenerationContext.h"
#include "GenerationResult.h"
#include "Resources.h"
#include "Wedge.h"
#include <algorithm>
#include <cstdlib>
namespace MapGeneration
{
ResourceStock capResourceStock(Map &map, int type, int maximumAmount)
{
	if (type < 0 || type >= MaterialSlotCount || maximumAmount <= 0)
		throw GenerationFailure("Resource stock cap requires a valid type and a positive amount");
	ResourceStock stock;
	for (int i = 0; i < map.getW() * map.getH(); ++i)
	{
		const auto &resource = map.getResource(i);
		if (resource.type != type)
			continue;
		map.setResourceAmount(i, std::min<int>(resource.amount, maximumAmount));
		++stock.tiles;
		stock.amount += resource.amount;
	}
	return stock;
}

bool clearGround(const Map &map, int x, int y)
{
	const auto& terrain=map.terrainPropertiesAt(x,y);
	return terrain.walkable && (terrain.resourcesGrow || terrain.nonGrowingResources) &&
		!map.isResource(x,y) && map.getBuilding(x,y)==NOGBID &&
		   map.getGroundUnit(x, y) == NOGUID;
}

int plantFieldInteriors(Map &map, const Torus &t, const std::vector<int> &tiles, int type,
						int wanted)
{
	if (wanted <= 0)
		return 0;
	std::vector<unsigned char> mask(t.size(), 0);
	std::vector<int> candidates;
	for (int i : tiles)
		if (!mask[i] && clearGround(map, i % t.w, i / t.w) &&
			map.isResourceAllowed(i % t.w, i / t.w, type))
		{
			mask[i] = 1;
			candidates.push_back(i);
		}
	const auto depth = clearance(t, mask);
	std::sort(candidates.begin(), candidates.end(),
			  [&](int a, int b) { return depth[a] != depth[b] ? depth[a] > depth[b] : a < b; });
	const int count = std::min(wanted, int(candidates.size()));
	for (int k = 0; k < count; ++k)
		map.setResourceByIndex(candidates[k] % t.w, candidates[k] / t.w, type, 1);
	return count;
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

int clearDeposits(Map &map, const Torus &t, const std::vector<unsigned char> &region,
				  const std::vector<unsigned char> *keep)
{
	int cleared = 0;
	for (int i = 0; i < t.size(); ++i)
	{
		const int x = i % t.w, y = i / t.w;
		if (region[i] && map.isResource(x, y) && map.terrainPropertiesAt(x,y).walkable && !(keep && (*keep)[i]))
		{
			map.setNoResource(x, y, 1);
			++cleared;
		}
	}
	return cleared;
}

namespace
{
// Generation and actual growth consume the same cached ecology. The view is
// stable while a planting pass changes resources without changing terrain.
class AlgaeGrowth
{
public:
	AlgaeGrowth(const Map& map, const Torus&) : map(map), field(map.resourceGrowthField()) {}
	double at(int i) const
	{
		if (!map.terrainSupportsResourceAtByIndex(i % map.getW(),i / map.getW(),ALGA)) return 0;
		return double(field.rate(i,ALGA))/Fertility::kRateScale;
	}
private:
	const Map& map;
	const Fertility::GrowthCache& field;
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

namespace
{
// seedAlgae's work, with the clumps shared out between `groups` groups of water (one group when
// `groupAt` is null); a tile whose group is negative takes no clump.
template <typename GroupAt>
void seedAlgaeIn(Map &map, GenerationContext &context, const Torus &t, const char *stream,
				 int algaePercent, const AlgaeBand &band, int groups, const GroupAt *groupAt)
{
	const int n = t.w * t.h;
	std::vector<MapGeneratorPoint> water;
	if (band.nearestOffshore < 0)
	{
		for (int i = 0; i < n; ++i)
			if (map.isResourceAllowed(i % t.w, i / t.w, ALGA))
				water.emplace_back(i % t.w, i / t.w);
	}
	else
	{
		std::vector<unsigned char> wet(n), dry(n);
		for (int y = 0; y < t.h; ++y)
			for (int x = 0; x < t.w; ++x)
			{
				wet[y * t.w + x] = map.isResourceAllowed(x,y,ALGA);
				dry[y * t.w + x] = !map.isResourceAllowed(x,y,ALGA);
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
	if (band.bestShare <= 0 && !groupAt)
	{
		for (int clump = 0; clump < clumps; ++clump)
			placeResourceClump(map, context, water[context.bounded(stream, water.size())], ALGA,
							   band.clumpRadius);
		return;
	}
	// Rank the band against the same cached growth field used by simulation.
	const AlgaeGrowth growth(map, t);
	std::vector<std::vector<std::pair<double, int>>> byGroup(groups);
	for (const MapGeneratorPoint &p : water)
	{
		const int i = p.y * t.w + p.x;
		const int group = groupAt ? (*groupAt)(p.x, p.y) : 0;
		if (group >= 0 && group < groups)
			byGroup[group].push_back({band.bestShare > 0 ? -growth.at(i) : 0.0, i});
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
} // namespace

void seedAlgae(Map &map, GenerationContext &context, const Torus &t, const char *stream,
			   int algaePercent, const AlgaeBand &band, const WedgeFrame *wedges)
{
	if (!wedges)
	{
		const auto none = [](int, int) { return 0; };
		seedAlgaeIn(map, context, t, stream, algaePercent, band, 1,
					band.bestShare > 0 ? &none : nullptr);
		return;
	}
	const auto wedgeOf = [&](int x, int y) { return wedges->cell(x, y).k; };
	seedAlgaeIn(map, context, t, stream, algaePercent, band, wedges->teams, &wedgeOf);
}

void seedAlgae(Map &map, GenerationContext &context, const Torus &t, const char *stream,
			   int algaePercent, const AlgaeBand &band, const std::vector<int> &groupOf, int groups)
{
	const auto groupAt = [&](int x, int y) { return groupOf[y * t.w + x]; };
	seedAlgaeIn(map, context, t, stream, algaePercent, band, std::max(1, groups), &groupAt);
}

void stockIslands(Map &map, GenerationContext &context, const std::vector<Island> &islands,
				  const char *stream)
{
	const int width = map.getW();
	// The stock lottery may choose any of these deposits after the center.
	constexpr int stockTypes[]={STONE,WHEAT,CHERRY,ORANGE,PRUNE};
	const auto acceptsStock=[&](int x,int y) {
		for (int resource : stockTypes) if (!map.terrainSupportsResourceAtByIndex(x,y,resource)) return false;
        return true;
	};
	for (const Island &island : islands)
	{
		std::vector<MapGeneratorPoint> grass;
		for (int i : island.tiles)
			if (acceptsStock(i % width, i / width))
				grass.emplace_back(i % width, i / width);
		if (grass.empty())
			continue;
		MapGeneratorPoint centre(island.x, island.y);
		if (!acceptsStock(centre.x, centre.y))
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
			placeResourceClump(map, context, centre, WHEAT, 2);
			break;
		}
	}
}
} // namespace MapGeneration
