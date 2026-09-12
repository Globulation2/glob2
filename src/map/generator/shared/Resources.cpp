// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "Resources.h"
#include "Distances.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationResult.h"
#include "HeightMap.h"
#include "Map.h"
#include "Regions.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include "Unit.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <utility>
using namespace MapGeneration;

namespace MapGeneration
{
int placeResourceClump(Map &map, GenerationContext &context, MapGeneratorPoint center,
					   int resourceType, int radius)
{
	int placed = 0;
	for (int dy = -radius; dy <= radius; ++dy)
		for (int dx = -radius; dx <= radius; ++dx)
		{
			const int d2 = dx * dx + dy * dy;
			if (d2 > radius * radius ||
				(d2 > (radius - 1) * (radius - 1) && context.bounded("resources", 4) == 0))
				continue;
			const int x = map.normalizeX(center.x + dx), y = map.normalizeY(center.y + dy);
			const int existingType = map.getResource(x, y).type;
			if (map.isResourceAllowed(x, y, resourceType) &&
				(existingType == NO_RES_TYPE || existingType == resourceType))
			{
				map.setResource(x, y, resourceType, 1);
				++placed;
			}
		}
	return placed;
}

void setScaledResource(Map &map, int x, int y, int resourceType, int size, int percent)
{
	if (percent == 100)
	{
		map.setResource(x, y, resourceType, size);
		return;
	}
	const int half = size >> 1;
	const int target = int(scaledCount(std::int64_t(2 * half + 1) * (2 * half + 1), percent));
	if (target <= 0)
		return;
	int reach = 0;
	while ((2 * reach + 1) * (2 * reach + 1) < target)
		++reach;
	const int side = 2 * reach + 1;
	// Offset i is column i / side, row i % side, the order setResource visits its square in. Keep
	// the `target` offsets nearest the centre (by ring, then by distance round it).
	const auto rank = [side, reach](int i)
	{
		const int dx = std::abs(i / side - reach), dy = std::abs(i % side - reach);
		return std::make_pair(std::max(dx, dy), dx + dy);
	};
	std::vector<int> order(size_t(side) * side);
	for (size_t i = 0; i < order.size(); ++i)
		order[i] = int(i);
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return rank(a) < rank(b); });
	std::vector<unsigned char> chosen(order.size(), 0);
	for (int k = 0; k < target; ++k)
		chosen[size_t(order[size_t(k)])] = 1;
	for (int i = 0; i < int(chosen.size()); ++i)
		if (chosen[size_t(i)])
			map.setResource(map.normalizeX(x + i / side - reach),
							map.normalizeY(y + i % side - reach), resourceType, 1);
}

int placeResourceClumpInArea(Map &map, GenerationContext &context,
							 const std::vector<MapGeneratorPoint> &points, int resourceType,
							 int radius)
{
	if (points.empty())
		return 0;
	const size_t first = context.bounded("resources", points.size());
	for (size_t offset = 0; offset < points.size(); ++offset)
	{
		const MapGeneratorPoint &center = points[(first + offset) % points.size()];
		if (!map.isResourceAllowed(center.x, center.y, resourceType))
			continue;
		const int placed = placeResourceClump(map, context, center, resourceType, radius);
		if (placed)
			return placed;
	}
	return 0;
}

namespace
{
// generateHeightField (Terrain.cpp) carves its own resource bands from the same noise field
// that carved the terrain: every tile's resource is a threshold on a value it already has, so
// stone and algae come out following the same contours the water and grass do, rather than
// looking like independent decisions. scatterResources runs after these generators' terrain is
// already fixed and irregular, so it cannot share that one field the way generateHeightField
// does, but it can still take the same threshold approach: fill the tightest (lowest-value)
// share of a noise field of its own that covers the requested tile count - the same
// histogrammed level search generateHeightField uses, just over an explicit candidate list
// instead of the whole grid. Compact circular clumps at independent random centers are a
// fundamentally different, and visibly clumpier, shape. Corn and wood go through
// scatterFarmland below instead: unlike stone and algae, they also need to prefer farmable
// ground, and that preference must only pick *where* farmland goes, not *which* of the two
// crops a given spot gets - see that function for why.
//
// A single global threshold across the whole map works for one connected landmass (Fjord),
// but a generator whose islands are separate landmasses (Isles, ConcreteIslands) gives
// each one its own, slightly different fertility (or noise) range - a global "take the best
// tiles first" pass can end up spending almost the entire band on whichever one or two islands
// happen to score highest, leaving the rest with none at all. That is exactly the "resources
// aren't balanced between players" complaint this whole scatter is meant to avoid, just at the
// scale of one island instead of one player. Grouping candidates by connected landmass and
// running the same histogram threshold independently within each group, sized to that group's
// own share of the candidate pool, keeps every landmass' band proportional to how much eligible
// ground it has - on a single connected map this is one group covering everything, identical to
// before.
//
// water chooses what the components are made of: false groups land into landmasses, true groups
// water into separate bodies (a lake apart from the sea). Algae only places on water, so an algae
// band has to be shared out between water bodies the same way a stone band is shared out between
// landmasses; drawn over land components it would never find a single candidate.
std::vector<int> computeComponents(const Map &map, bool water, int &numComponents)
{
	const int w = map.getW(), h = map.getH();
	std::vector<int> component(size_t(w) * h, -1);
	// Bounded by the component[np] < 0 guard below to at most w*h enqueues per component, and
	// every tile belongs to exactly one component - a flat preallocated FIFO reused across
	// components, the same fix as floodReach below and computeDistances in Distances.cpp.
	std::vector<int> queue(size_t(w) * h);
	int nextId = 0;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const int start = y * w + x;
			if (component[start] != -1 || map.isWater(x, y) != water)
				continue;
			size_t qHead = 0, qTail = 0;
			component[start] = nextId;
			queue[qTail++] = start;
			while (qHead < qTail)
			{
				const int p = queue[qHead++];
				const int px = p % w, py = p / w;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						if (!dx && !dy)
							continue;
						const int nx = map.normalizeX(px + dx), ny = map.normalizeY(py + dy);
						const int np = ny * w + nx;
						if (component[np] == -1 && map.isWater(nx, ny) == water)
						{
							component[np] = nextId;
							queue[qTail++] = np;
						}
					}
			}
			++nextId;
		}
	numComponents = nextId;
	return component;
}

void scatterBand(Map &map, HeightMap &noise, int resourceType, int targetTiles,
				 const std::vector<int> &component, int numComponents)
{
	if (targetTiles <= 0 || numComponents <= 0)
		return;
	const int width = map.getW(), height = map.getH();
	constexpr unsigned kBuckets = 2048;
	std::vector<std::vector<MapGeneratorPoint>> candidates(numComponents);
	std::vector<std::vector<unsigned>> level(numComponents);
	std::vector<std::vector<int>> histogram(numComponents, std::vector<int>(kBuckets, 0));
	int totalCandidates = 0;
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			if (map.getResource(x, y).type != NO_RES_TYPE)
				continue;
			if (!map.isResourceAllowed(x, y, resourceType))
				continue;
			const int comp = component[y * width + x];
			if (comp < 0)
				continue;
			const unsigned lvl = noise.uiLevel(x, y, kBuckets);
			candidates[comp].emplace_back(x, y);
			level[comp].push_back(lvl);
			++histogram[comp][lvl];
			++totalCandidates;
		}
	if (totalCandidates == 0)
		return;
	for (int c = 0; c < numComponents; ++c)
	{
		const auto &compCandidates = candidates[c];
		if (compCandidates.empty())
			continue;
		const int share = int((std::int64_t(targetTiles) * std::int64_t(compCandidates.size())) /
							  totalCandidates);
		const int wanted = std::min<int>(compCandidates.size(), std::max(1, share));
		unsigned threshold = kBuckets - 1;
		int accumulated = 0;
		for (unsigned b = 0; b < kBuckets; ++b)
		{
			accumulated += histogram[c][b];
			if (accumulated >= wanted)
			{
				threshold = b;
				break;
			}
		}
		for (size_t i = 0; i < compCandidates.size(); ++i)
			if (level[c][i] <= threshold)
				map.setResource(compCandidates[i].x, compCandidates[i].y, resourceType, 1);
	}
}

// Map::growResources only regrows a wheat or wood tile near water (the same triangular kernel
// Fertility::Field evaluates exactly) - a corn or wood tile with none nearby is a one-time find
// that can never come back, not a farm. But fertility answers only where farmland can be: it
// says nothing about whether a given fertile spot should become corn or wood, and thresholding
// corn and wood on the same field back to back (corn takes the highest band, wood the
// next-highest) turned that lopsided into two concentric rings sorted by distance from water,
// with wood pushed entirely behind corn instead of the two sitting side by side the way real
// farmland does. So the two questions are answered separately here: fertility selects the
// region - widened well past the requested tile count so it reaches into lower-but-still-
// farmable ground instead of a razor-thin ring at the very highest values - and then an
// unrelated noise field splits that region into corn and wood, so which crop lands where varies
// along the coast rather than with distance from it.
void scatterFarmland(Map &map, const Fertility::Field &fertility, HeightMap &splitNoise,
					 int cornTarget, int woodTarget, const std::vector<int> &landComponent,
					 int numComponents)
{
	const int totalTarget = cornTarget + woodTarget;
	if (totalTarget <= 0 || numComponents <= 0)
		return;
	const int width = map.getW(), height = map.getH();
	constexpr unsigned kBuckets = 2048;
	constexpr int kWiden = 3; // the fertile region reaches roughly 3x past the requested tile count
	std::vector<std::vector<MapGeneratorPoint>> candidates(numComponents);
	std::vector<std::vector<unsigned>> fertLevel(numComponents);
	std::vector<std::vector<int>> fertHistogram(numComponents, std::vector<int>(kBuckets, 0));
	int totalCandidates = 0;
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			if (map.getResource(x, y).type != NO_RES_TYPE)
				continue;
			// Wood and corn share the same terrain requirement (grass), so either stands in for
			// eligibility here - which of the two a tile ends up with is decided below, by the split.
			if (!map.isResourceAllowed(x, y, CORN))
				continue;
			const std::uint32_t f = fertility.at(x, y);
			if (f == 0)
				continue;
			const int comp = landComponent[y * width + x];
			if (comp < 0)
				continue;
			const unsigned lvl =
				kBuckets - 1 -
				std::min<unsigned>(kBuckets - 1, unsigned((f * kBuckets) / Fertility::kScale));
			candidates[comp].emplace_back(x, y);
			fertLevel[comp].push_back(lvl);
			++fertHistogram[comp][lvl];
			++totalCandidates;
		}
	if (totalCandidates == 0)
		return;
	for (int c = 0; c < numComponents; ++c)
	{
		const auto &compCandidates = candidates[c];
		if (compCandidates.empty())
			continue;
		const int share = int((std::int64_t(totalTarget) * std::int64_t(compCandidates.size())) /
							  totalCandidates);
		// Widening is proportionally huge on a landmass with little eligible ground to begin with -
		// a small island can have its whole candidate pool absorbed by kWiden's multiplier even
		// though the unwidened share alone would have left most of it free. Capping the region
		// at 2/3 of the component's own pool guarantees slack to route around it regardless of
		// component size, instead of only checking against the (unrelated) map-wide total above.
		const int widenedShare = std::max(1, share) * kWiden;
		const int slackCap = std::max<int>(1, int(compCandidates.size()) * 2 / 3);
		const int regionWanted =
			std::min<int>(compCandidates.size(), std::min(widenedShare, slackCap));
		unsigned threshold = kBuckets - 1;
		int accumulated = 0;
		for (unsigned b = 0; b < kBuckets; ++b)
		{
			accumulated += fertHistogram[c][b];
			if (accumulated >= regionWanted)
			{
				threshold = b;
				break;
			}
		}
		std::vector<MapGeneratorPoint> region;
		for (size_t i = 0; i < compCandidates.size(); ++i)
			if (fertLevel[c][i] <= threshold)
				region.push_back(compCandidates[i]);
		if (region.empty())
			continue;
		// Split the region by an independent noise field instead of fertility - sort it into that
		// field's order and slice off the lowest share for corn, the next share for wood, so the
		// two form separate patches following the noise field's own organic contours rather than
		// corn's band always sitting closer to the water than wood's.
		std::vector<size_t> order(region.size());
		for (size_t i = 0; i < order.size(); ++i)
			order[i] = i;
		std::sort(order.begin(), order.end(),
				  [&](size_t a, size_t b)
				  {
					  return splitNoise.uiLevel(region[a].x, region[a].y, kBuckets) <
							 splitNoise.uiLevel(region[b].x, region[b].y, kBuckets);
				  });
		const int cornWanted =
			std::min<int>(region.size(),
						  std::max(0, int((std::int64_t(cornTarget) * std::int64_t(region.size())) /
										  totalTarget)));
		const int woodWanted =
			std::min<int>(int(region.size()) - cornWanted,
						  std::max(0, int((std::int64_t(woodTarget) * std::int64_t(region.size())) /
										  totalTarget)));
		for (int i = 0; i < cornWanted; ++i)
			map.setResource(region[order[i]].x, region[order[i]].y, CORN, 1);
		for (int i = cornWanted; i < cornWanted + woodWanted; ++i)
			map.setResource(region[order[i]].x, region[order[i]].y, WOOD, 1);
	}
}
} // namespace

void scatterResources(Game &game, GenerationContext &context, const ResourceDensities &density)
{
	Map &map = game.map;
	const int width = map.getW(), height = map.getH(), area = width * height;
	const Fertility::Field fertility = Fertility::forMap(map, false);
	HeightMap noise(width, height, context.stream("scatter-noise"));
	noise.makePlain(24);
	// A much finer scale than the terrain-shaping noise above: this one only decides which of two
	// *adjacent* crops a farmland tile becomes, not where the coastline itself bends, so its
	// features need to be sized like a player's local working area (tens of tiles), not like a
	// continent (hundreds). Reusing 24 here first produced patches wide enough that a zoomed-in
	// view could sit entirely inside one - corn and wood alternated across the whole map, but not
	// within reach of any one colony, so a colony's own farmland still read as a solid wall of one
	// crop with the other hidden behind it.
	HeightMap splitNoise(width, height, context.stream("scatter-split"));
	splitNoise.makePlain(6);
	int numComponents = 0;
	const std::vector<int> landComponent = computeComponents(map, false, numComponents);

	scatterFarmland(map, fertility, splitNoise, density.corn * area / 1600,
					density.wood * area / 1600, landComponent, numComponents);
	scatterBand(map, noise, STONE, density.stone * area / 3000, landComponent, numComponents);
	if (density.algae > 0)
	{
		int numWaterBodies = 0;
		const std::vector<int> waterBody = computeComponents(map, true, numWaterBodies);
		scatterBand(map, noise, ALGA, density.algae * area / 800, waterBody, numWaterBodies);
	}

	// Fruit stays a rare, discrete find rather than a background band - "a distinct little
	// prize", the same role it already plays elsewhere in these generators - so it keeps the
	// original single-clump-at-a-random-point search instead of joining the noise bands above.
	for (int i = 0; i < density.fruit; ++i)
	{
		const int type = CHERRY + context.bounded("resources", 3);
		MapGeneratorPoint center(0, 0);
		bool found = false;
		for (int attempt = 0; attempt < 100 && !found; ++attempt)
		{
			center = {int(context.bounded("resources", width)),
					  int(context.bounded("resources", height))};
			found = map.isResourceAllowed(center.x, center.y, type);
		}
		if (found)
			placeResourceClump(map, context, center, type, 1);
	}
}

void fillInResource(Map &map, GenerationContext &context, std::vector<MapGeneratorPoint> &points,
					int resourceType, int maxFillSize)
{
	if (maxFillSize < 1 || maxFillSize >= std::min(map.getW(), map.getH()))
		throw GenerationFailure("Invalid resource patch size");
	for (unsigned int n = 0; n < points.size(); ++n)
	{
		map.setResource(points[n].x, points[n].y, resourceType,
						1 + context.stream("regions")() % maxFillSize);
	}
}

namespace
{
// Ground units can't walk onto a tile carrying a resource (Map::isHardSpaceForGroundUnit
// excludes them), so a solid, unbroken band from the noise-band resource painting can wall a
// team's boot tile off from the rest of an otherwise perfectly connected landmass. floodReach
// walks that real, resource-respecting space; dist is kept (not just aggregated) so a caller
// can compare it against a wall-blind flood and find exactly where such a wall runs.
struct ReachResult
{
	int wheatDist = -1, woodDist = -1;
	std::vector<MapGeneratorPoint> closeGrass, farGrass;
	std::vector<int> dist;
};
ReachResult floodReach(Map &map, int bootX, int bootY, int exploreLimit, int closeRange,
					   int clearRadius)
{
	const int w = map.getW(), h = map.getH();
	ReachResult r;
	r.dist.assign(size_t(w) * h, -1);
	// Bounded by the r.dist[np] < 0 guard below to at most w*h enqueues - a flat preallocated
	// FIFO instead of std::queue<int>'s std::deque, which grows by separately heap-allocated
	// blocks (same fix as computeDistances in Distances.cpp).
	std::vector<int> q(size_t(w) * h);
	size_t qHead = 0, qTail = 0;
	int start = bootY * w + bootX;
	r.dist[start] = 0;
	q[qTail++] = start;
	while (qHead < qTail)
	{
		int p = q[qHead++];
		int x = p % w, y = p / w;
		if (map.getUMTerrain(x, y) == GRASS && r.dist[p] >= clearRadius &&
			r.dist[p] <= exploreLimit)
			(r.dist[p] <= closeRange ? r.closeGrass : r.farGrass)
				.push_back(MapGeneratorPoint(x, y));
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (dx == 0 && dy == 0)
					continue;
				int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
				int np = ny * w + nx;
				int resType = map.getResource(nx, ny).type;
				if (resType == CORN && r.wheatDist < 0)
					r.wheatDist = r.dist[p] + 1;
				if (resType == WOOD && r.woodDist < 0)
					r.woodDist = r.dist[p] + 1;
				if (r.dist[np] < 0 && r.dist[p] < exploreLimit &&
					map.isHardSpaceForGroundUnit(nx, ny, false, 0))
				{
					r.dist[np] = r.dist[p] + 1;
					q[qTail++] = np;
				}
			}
	}
	return r;
}

// The same flood, but blocked only by water — as if resources didn't exist. Diffing this
// against floodReach's result isolates exactly which tiles a resource wall blocks: reachable
// here, not reachable there, adjacent to what is. Clearing only those tiles (rather than an
// entire neighborhood) opens the way while disturbing nothing else nearby. Protected walls are
// part of the terrain, so they block this flood as well.
std::vector<int> terrainOnlyReach(Map &map, int bootX, int bootY, int limit,
								  const std::vector<unsigned char> *protectedWalls)
{
	const int w = map.getW(), h = map.getH();
	std::vector<int> dist(size_t(w) * h, -1);
	std::vector<int> q(size_t(w) * h);
	size_t qHead = 0, qTail = 0;
	int start = bootY * w + bootX;
	dist[start] = 0;
	q[qTail++] = start;
	while (qHead < qTail)
	{
		int p = q[qHead++];
		if (dist[p] >= limit)
			continue;
		int x = p % w, y = p / w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (dx == 0 && dy == 0)
					continue;
				int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
				int np = ny * w + nx;
				if (dist[np] < 0 && !map.isWater(nx, ny) &&
					!(protectedWalls && (*protectedWalls)[np]))
				{
					dist[np] = dist[p] + 1;
					q[qTail++] = np;
				}
			}
	}
	return dist;
}

// Clears the resource tiles directly responsible for a team's cramped pocket: exactly the
// tiles reachable without resources blocking the way, not reachable with them, and touching a
// tile that is — the wall's inner face — plus, on a wall thick enough to have an outer face
// too, that face as well. A real dead end (no meaningfully larger landmass once resources are
// ignored) clears nothing, since there is no better tile on the other side to find. Iterated a
// few times by the caller in case a wall is thicker still. A protected wall tile is never
// cleared.
bool clearResourceWall(Map &map, const std::vector<int> &boxedDist,
					   const std::vector<int> &openDist, int minGain,
					   const std::vector<unsigned char> *protectedWalls)
{
	const int w = map.getW(), h = map.getH();
	int cleared = 0;
	std::vector<std::pair<int, int>> toClear;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			int p = y * w + x;
			if (boxedDist[p] >= 0 || openDist[p] < 0 || !map.isResource(x, y) ||
				(protectedWalls && (*protectedWalls)[p]))
				continue;
			bool touchesBoxed = false;
			for (int dy = -1; dy <= 1 && !touchesBoxed; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					if (dx == 0 && dy == 0)
						continue;
					int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
					if (boxedDist[ny * w + nx] >= 0)
					{
						touchesBoxed = true;
						break;
					}
				}
			if (touchesBoxed)
				toClear.push_back({x, y});
		}
	if ((int)toClear.size() < minGain)
		return false;
	for (auto [x, y] : toClear)
	{
		map.setNoResource(x, y, 1);
		++cleared;
	}
	return cleared > 0;
}
} // namespace

void guaranteeStartingResources(Game &game, GenerationContext &context, int wheatRange,
								int woodRange, int clearRadius,
								const std::vector<unsigned char> *protectedWalls)
{
	Map &map = game.map;
	if (protectedWalls && protectedWalls->size() != size_t(map.getW()) * map.getH())
		throw GenerationFailure("Protected wall mask does not match the map size");
	const int exploreLimit = std::max(wheatRange, woodRange) * 5 / 2;
	const int closeRange = wheatRange / 2;
	// Below this many reached tiles a team is badly boxed in, but that has two very different
	// causes: a resource wall sealing off an otherwise fine landmass (swamp and river both paint
	// resources with no regard for what they might enclose), or the boot search simply landing on
	// a genuinely small spot — a real little island on a water-heavy map, say, where there's
	// nothing bigger on the other side to reach at all. Only the first is fixable, and only its
	// exact wall tiles are cleared, so a genuinely small spot is correctly left untouched.
	const int minPocketTiles = 60;
	for (int team = 0; team < context.request.nbTeams; ++team)
	{
		// A caller can hand over a boot tile it has not wrapped onto the map yet: the height-field
		// generators' fallback site search does, before placeStarts normalizes it. The floods below
		// index by it directly.
		int bootX = map.normalizeX(context.bootX[team]),
			bootY = map.normalizeY(context.bootY[team]);
		ReachResult reach = floodReach(map, bootX, bootY, exploreLimit, closeRange, clearRadius);
		auto pocketSize = [&]
		{
			int n = 0;
			for (int v : reach.dist)
				if (v >= 0)
					++n;
			return n;
		};
		bool underServed = reach.wheatDist < 0 || reach.wheatDist > wheatRange ||
						   reach.woodDist < 0 || reach.woodDist > woodRange;
		for (int attempt = 0; underServed && pocketSize() < minPocketTiles && attempt < 6;
			 ++attempt)
		{
			std::vector<int> open =
				terrainOnlyReach(map, bootX, bootY, exploreLimit, protectedWalls);
			if (!clearResourceWall(map, reach.dist, open, /*minGain=*/1, protectedWalls))
				break;
			reach = floodReach(map, bootX, bootY, exploreLimit, closeRange, clearRadius);
			underServed = reach.wheatDist < 0 || reach.wheatDist > wheatRange ||
						  reach.woodDist < 0 || reach.woodDist > woodRange;
		}
		auto placeReachable = [&](int resourceType)
		{
			if (!reach.closeGrass.empty() &&
				placeResourceClumpInArea(map, context, reach.closeGrass, resourceType, 2))
				return;
			if (!reach.farGrass.empty())
				placeResourceClumpInArea(map, context, reach.farGrass, resourceType, 2);
		};
		if (reach.wheatDist < 0 || reach.wheatDist > wheatRange)
			placeReachable(CORN);
		if (reach.woodDist < 0 || reach.woodDist > woodRange)
			placeReachable(WOOD);
	}
}

namespace
{
// Where a team's own workers can walk within `range` — resources, water and buildings all block a
// unit — and how many of the tiles reached start a 4x4 building. Seeded from the workers rather
// than the boot tile, which is the swarm's own tile, so this counts the room a colony actually has.
struct WorkerReach
{
	std::vector<int> dist;
	int sites = 0;
};
WorkerReach reachFromWorkers(Map &map, int team, int range)
{
	const int w = map.getW(), h = map.getH();
	WorkerReach r;
	r.dist.assign(size_t(w) * h, -1);
	std::vector<int> q(size_t(w) * h);
	size_t qHead = 0, qTail = 0;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const Uint16 gid = map.getGroundUnit(x, y);
			if (gid != NOGUID && Unit::GIDtoTeam(gid) == team)
			{
				r.dist[size_t(y) * w + x] = 0;
				q[qTail++] = y * w + x;
			}
		}
	while (qHead < qTail)
	{
		const int p = q[qHead++];
		const int x = p % w, y = p / w;
		if (map.isFreeForBuilding(x, y, 4, 4))
			++r.sites;
		if (r.dist[p] >= range)
			continue;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (dx == 0 && dy == 0)
					continue;
				const int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy),
						  np = ny * w + nx;
				if (r.dist[np] < 0 && map.isHardSpaceForGroundUnit(nx, ny, false, 0))
				{
					r.dist[np] = r.dist[p] + 1;
					q[qTail++] = np;
				}
			}
	}
	return r;
}
} // namespace

void openCrampedStarts(Game &game, GenerationContext &context, int sites, int range,
					   const std::vector<unsigned char> *protectedWalls)
{
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();
	if (protectedWalls && protectedWalls->size() != size_t(w) * h)
		throw GenerationFailure("Protected wall mask does not match the map size");
	for (int team = 0; team < context.request.nbTeams; ++team)
	{
		WorkerReach reach = reachFromWorkers(map, team, range);
		if (reach.sites >= sites)
			continue;
		// Rings measured with resources ignored, so they walk out through the wall itself instead of
		// stopping at its inner face the way the colony's own flood does.
		const int bootX = map.normalizeX(context.bootX[team]),
				  bootY = map.normalizeY(context.bootY[team]);
		const std::vector<int> open = terrainOnlyReach(map, bootX, bootY, range, protectedWalls);
		std::vector<std::vector<int>> rings(size_t(range) + 1);
		for (int p = 0; p < w * h; ++p)
			if (open[p] >= 1 && open[p] <= range && map.isResource(p % w, p / w) &&
				!(protectedWalls && (*protectedWalls)[p]))
				rings[open[p]].push_back(p);
		// One ring at a time, nearest first, re-measuring after each: the ring that finally gives the
		// colony its room is the last one cleared, so nothing further out is touched. A colony on a
		// genuinely small spot — a real islet, with nothing to open up — just runs out of rings.
		for (int ring = 1; ring <= range && reach.sites < sites; ++ring)
		{
			if (rings[ring].empty())
				continue;
			for (const int p : rings[ring])
				map.setNoResource(p % w, p / w, 1);
			reach = reachFromWorkers(map, team, range);
		}
	}
}
} // namespace MapGeneration
