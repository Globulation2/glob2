// SPDX-License-Identifier: GPL-3.0-or-later
#include "BalancedStarts.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GlobalContainer.h"
#include "Grid.h"
#include "Map.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>
namespace MapGeneration
{
namespace
{
// isHardSpaceForGroundUnit(x, y, false, 0) is a pure function of terrain/resource state for
// every call in this file: canSwim and the team mask are always the same two constants, no
// forbidden-area bit survives a `& 0`, and nothing places a building before chooseBalancedStarts
// runs (placeStarts(), the only thing that does, runs after boot tiles are already chosen). So
// the answer never changes across the two distanceToResource() floods and the up-to-900 calls
// to scoreAsBuilt() below; computing it once into a flat byte per tile turns what used to be a
// branchy, multi-array accessor call at every visited tile into a single sequential array read.
std::vector<std::uint8_t> buildHardSpaceGrid(Map &map)
{
	return groundUnitTiles(map);
}

// Walking distance from every tile to the nearest deposit of one resource. Workers stand
// beside a deposit rather than on it (a resource tile is not walkable), so the sources are the
// walkable tiles touching one. One flood answers the question for every tile on the map, which
// is what makes scoring a few hundred candidate sites cheap enough to do exhaustively. Distances
// on any map this engine supports fit comfortably in 16 bits, halving the footprint of an array
// every scoreAsBuilt call after it touches over and over.
std::vector<std::int16_t> distanceToResource(Map &map, const std::vector<std::uint8_t> &hard,
											 int resourceType)
{
	const Torus t(map);
	std::vector<unsigned char> beside(size_t(t.size()), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			if (map.getResource(x, y).type != resourceType)
				continue;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int np = t.at(x + dx, y + dy);
					if (hard[np])
						beside[np] = 1;
				}
		}
	const std::vector<int> steps = stepsFrom(t, beside, hard);
	std::vector<std::int16_t> dist(steps.size());
	for (size_t i = 0; i < steps.size(); ++i)
		dist[i] = std::int16_t(steps[i]);
	return dist;
}
} // namespace

bool chooseBalancedStarts(Game &game, GenerationContext &context, int minDistSquare)
{
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();
	const int nbTeams = context.request.nbTeams;
	if (nbTeams <= 0 || minDistSquare <= 0)
		return false;
	const int typeNum = globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
	const BuildingType *swarm = globalContainer->buildingsTypes.get(typeNum);
	if (!swarm)
		return false;

	const std::vector<std::uint8_t> hard = buildHardSpaceGrid(map);
	const std::vector<std::int16_t> woodDist = distanceToResource(map, hard, WOOD);
	const std::vector<std::int16_t> wheatDist = distanceToResource(map, hard, CORN);

	// A site is worth exactly what its *worse* resource costs to reach: a colony next to wood
	// but a long walk from wheat is not a good start, however good the wood is.
	// A colony changes its own surroundings the moment it is built: placeStarts() clears a
	// five by seven box of resources to make room, the swarm itself becomes four by four tiles
	// of obstacle, and the workers appear on the row above it rather than on the boot tile. A
	// site scored against the bare map is therefore scored on deposits it is about to destroy
	// and paths it is about to block, which is how four sites picked as exactly equal can
	// finish unequal. Score what the colony will actually live with instead. The offsets below
	// mirror placeStarts(); they are what it does, not a guess at it.
	std::vector<std::uint16_t> visited(size_t(w) * h, 0);
	std::uint16_t visitStamp = 0;
	// scoreAsBuilt runs its own small flood per candidate site - up to 900 times per call to
	// this function. A fresh std::queue per call means a fresh std::deque allocation (and its
	// block-by-block growth) 900 times over; every one of those floods visits each tile at most
	// once (the visitStamp guard below), so one pair of w*h-sized buffers, reused across every
	// call and just reset to empty (an O(1) index reset, not a reallocation), is always enough.
	std::vector<int> queueTile(size_t(w) * h), queueDist(size_t(w) * h);
	auto scoreAsBuilt = [&](int bx, int by, int limit) -> int
	{
		auto cleared = [&](int tx, int ty)
		{
			int ox = map.normalizeX(tx - bx), oy = map.normalizeY(ty - by);
			if (ox >= w / 2)
				ox -= w;
			if (oy >= h / 2)
				oy -= h;
			return ox >= 0 && ox <= 4 && oy >= -2 && oy <= 4;
		};
		auto blocked = [&](int tx, int ty)
		{
			int ox = map.normalizeX(tx - bx), oy = map.normalizeY(ty - by);
			if (ox >= w / 2)
				ox -= w;
			if (oy >= h / 2)
				oy -= h;
			return ox >= 0 && ox < swarm->width && oy >= 0 && oy < swarm->height;
		};
		++visitStamp;
		size_t qHead = 0, qTail = 0;
		auto push = [&](int tx, int ty, int d)
		{
			int nx = map.normalizeX(tx), ny = map.normalizeY(ty);
			int np = ny * w + nx;
			if (visited[np] == visitStamp)
				return;
			visited[np] = visitStamp;
			queueTile[qTail] = np;
			queueDist[qTail] = d;
			++qTail;
		};
		// Workers spawn on the row above the swarm, so that is where a gathering trip starts.
		for (int i = 0; i < std::max(1, context.request.nbWorkers); ++i)
			push(bx + (i % 4), by - 1 - (i / 4), 0);
		int wood = -1, wheat = -1;
		while (qHead < qTail && (wood < 0 || wheat < 0))
		{
			int p = queueTile[qHead];
			int d = queueDist[qHead];
			++qHead;
			if (d >= limit)
				continue;
			int x = p % w, y = p / w;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					if (dx == 0 && dy == 0)
						continue;
					int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
					int type = map.getResource(nx, ny).type;
					if (!cleared(nx, ny))
					{
						if (type == WOOD && wood < 0)
							wood = d + 1;
						if (type == CORN && wheat < 0)
							wheat = d + 1;
					}
					if (!blocked(nx, ny) && hard[ny * w + nx])
						push(nx, ny, d + 1);
				}
		}
		if (wood < 0 || wheat < 0)
			return -1;
		return std::max(wood, wheat);
	};

	std::vector<std::pair<int, int>> sites; // (score, tile index)
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			if (!map.isFreeForBuilding(x, y, swarm->width, swarm->height))
				continue;
			const int p = y * w + x;
			if (woodDist[p] < 0 || wheatDist[p] < 0)
				continue;
			// The bare-map distance can only understate what the built colony will walk, so it
			// is a sound cheap filter: it shortlists sites worth the exact simulation above.
			sites.push_back({std::max(woodDist[p], wheatDist[p]), p});
		}
	if ((int)sites.size() < nbTeams)
		return false;
	std::sort(sites.begin(), sites.end());
	// Large maps can offer tens of thousands of legal sites. Thinning by stride keeps the
	// sample spread across the whole score range instead of crowding one end of it, and keeps
	// the search below a bounded cost regardless of map size.
	const size_t cap = 900;
	if (sites.size() > cap)
	{
		std::vector<std::pair<int, int>> thinned;
		thinned.reserve(cap);
		for (size_t i = 0; i < cap; ++i)
			thinned.push_back(sites[i * sites.size() / cap]);
		sites.swap(thinned);
	}

	// Re-score the shortlist as it will actually be built, and re-sort on the honest number.
	{
		std::vector<std::pair<int, int>> exact;
		exact.reserve(sites.size());
		for (const auto &s : sites)
		{
			// 32 steps: the wood range of the start guarantee, the farthest a start's resources are
			// allowed to be.
			const int built = scoreAsBuilt(s.second % w, s.second / w, 32);
			if (built >= 0)
				exact.push_back({built, s.second});
		}
		if ((int)exact.size() < nbTeams)
			return false;
		std::sort(exact.begin(), exact.end());
		sites.swap(exact);
	}

	// Sites are sorted by score, so any set of colonies drawn from a short window of this list
	// is a set whose colonies are closely matched. Find the narrowest window that still holds
	// nbTeams mutually distant sites; scanning windows from the low-score end means ties are
	// settled in favour of the set that is not just equal but good.
	std::vector<int> best;
	int bestSpread = -1;
	for (size_t i = 0; i < sites.size(); ++i)
	{
		if (bestSpread == 0)
			break;
		std::vector<int> picked;
		for (size_t j = i; j < sites.size(); ++j)
		{
			if (bestSpread >= 0 && sites[j].first - sites[i].first >= bestSpread)
				break; // this window is already no better than what we hold
			const int px = sites[j].second % w, py = sites[j].second / w;
			bool farEnough = true;
			for (int q : picked)
				if (map.warpDistSquare(px, py, q % w, q / w) < minDistSquare)
				{
					farEnough = false;
					break;
				}
			if (!farEnough)
				continue;
			picked.push_back(sites[j].second);
			if ((int)picked.size() == nbTeams)
			{
				bestSpread = sites[j].first - sites[i].first;
				best = picked;
				break;
			}
		}
	}
	if ((int)best.size() != nbTeams)
		return false;
	for (int team = 0; team < nbTeams; ++team)
	{
		context.bootX[team] = best[team] % w;
		context.bootY[team] = best[team] / w;
	}
	return true;
}
} // namespace MapGeneration
