// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "StartingPositions.h"
#include "Distances.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GeneratorDefinition.h"
#include "GlobalContainer.h"
#include "HeightMap.h"
#include "Map.h"
#include "Regions.h"
#include "Resources.h"
#include "Terrain.h"
#include "Unit.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
using namespace MapGeneration;

namespace MapGeneration
{
bool divideUpPlayerLands(Game &game, GenerationContext &context, std::vector<int> &grid,
						 std::vector<int> &teamAreaNumbers, int &areaNumber,
						 const PlayerLandResources &resources)
{
	context.stage = "resources and starts";
	int typeNum = globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
	BuildingType *swarm = globalContainer->buildingsTypes.get(typeNum);

	// Compute the distances from water
	std::vector<MapGeneratorPoint> sources;
	std::vector<MapGeneratorPoint> obstacles;
	std::vector<int> distances;
	obstacles.clear();
	getAllPoints(game.map, grid, 0, sources);
	computeDistances(game.map, sources, obstacles, distances);

	// Create a new heightmap from noise and distance to water
	std::vector<int> heightmap(game.map.getW() * game.map.getH(), 50);
	adjustHeightmapFromPerlinNoise(game.map, context, heightmap, 5);
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			int d = distances[y * game.map.getW() + x];
			heightmap[y * game.map.getW() + x] -= d;
		}
	}

	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		// Initialize
		std::vector<int> areaWeights;
		std::vector<int> areaNumbers;
		for (int j = 0; j < 12; ++j)
		{
			areaWeights.push_back(1);
			areaNumbers.push_back(areaNumber);
			areaNumber += 1;
		}

		// Divide the area. Its possible the area will be so small it can't be used
		if (divideUpArea(game.map, context, grid, teamAreaNumbers[i], areaWeights, areaNumbers))
		{
			// Sort the list of areas based on how close they are to water
			std::vector<int> areaDistances(areaNumbers.size());
			std::vector<int> areaIndexes(areaNumbers.size());
			for (unsigned int j = 0; j < areaNumbers.size(); ++j)
			{
				areaDistances[j] =
					computeAverageDistance(game.map, grid, areaNumbers[j], distances);
				areaIndexes[j] = j;
			}
			ListComparator compare(areaDistances);
			std::sort(areaIndexes.begin(), areaIndexes.end(), compare);
			for (unsigned int j = 0; j < areaDistances.size(); ++j)
			{
				areaIndexes[j] = areaNumbers[areaIndexes[j]];
			}
			areaNumbers = areaIndexes;

			// Place wood. A field covers the zone's tiles whose raised height clears 50, so the
			// wood amount sets how far the raise reaches in from the coast.
			std::vector<MapGeneratorPoint> wheatWoodPoints;
			std::vector<MapGeneratorPoint> wheatPoints;
			getAllPoints(game.map, grid, areaNumbers[3], wheatWoodPoints);
			getAllPoints(game.map, grid, areaNumbers[4], wheatWoodPoints);
			getAllPoints(game.map, grid, areaNumbers[5], wheatWoodPoints);
			const int woodRise = int(scaledCount(10, resources.wood));
			adjustHeightmapFromPoints(game.map, wheatWoodPoints, heightmap, woodRise);
			for (unsigned int j = 0; j < wheatWoodPoints.size(); ++j)
			{
				int h = heightmap[wheatWoodPoints[j].y * game.map.getW() + wheatWoodPoints[j].x];
				if (h > 50 && resources.wood > 0)
				{
					game.map.setResource(wheatWoodPoints[j].x, wheatWoodPoints[j].y, WOOD, 1);
				}
			}
			wheatWoodPoints.clear();

			// Place wheat. The swarm goes beside the default wheat field, whatever amount is
			// actually placed, so the base layout doesn't move with the wheat amount.
			getAllPoints(game.map, grid, areaNumbers[0], wheatWoodPoints);
			getAllPoints(game.map, grid, areaNumbers[1], wheatWoodPoints);
			getAllPoints(game.map, grid, areaNumbers[2], wheatWoodPoints);
			const int wheatRise = int(scaledCount(10, resources.wheat));
			adjustHeightmapFromPoints(game.map, wheatWoodPoints, heightmap, wheatRise);
			for (unsigned int j = 0; j < wheatWoodPoints.size(); ++j)
			{
				int h = heightmap[wheatWoodPoints[j].y * game.map.getW() + wheatWoodPoints[j].x];
				if (h > 50 && resources.wheat > 0)
					game.map.setResource(wheatWoodPoints[j].x, wheatWoodPoints[j].y, CORN, 1);
				if (h - wheatRise + 10 > 50)
					wheatPoints.push_back(wheatWoodPoints[j]);
			}

			// These are all points in the base
			std::vector<MapGeneratorPoint> baseLocations;
			getAllPoints(game.map, grid, areaNumbers[6], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[7], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[8], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[9], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[10], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[11], baseLocations);

			// Place stone
			int numberOfStone = int(scaledCount(6, resources.stone));
			std::vector<MapGeneratorPoint> stoneLocations = baseLocations;
			chooseRandomPoints(game.map, context, stoneLocations, numberOfStone);
			for (unsigned int j = 0; j < stoneLocations.size(); ++j)
			{
				game.map.setResource(stoneLocations[j].x, stoneLocations[j].y, STONE, 1);
			}

			// Concerning starting locations, we also consider points inside the wheat and wood
			// areas
			getAllPoints(game.map, grid, areaNumbers[0], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[1], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[2], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[3], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[4], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[5], baseLocations);

			// Compute every points distance from the wheat
			std::vector<int> wheatDistance;
			computeDistances(game.map, wheatPoints, obstacles, wheatDistance);

			// Only consider points between 1 and 4 squares from wheat
			auto startsWithinOfWheat = [&](int window)
			{
				std::vector<MapGeneratorPoint> found;
				for (unsigned int j = 0; j < baseLocations.size(); ++j)
				{
					int minValue = 100000;
					for (int x = 0; x < 4; ++x)
					{
						for (int y = 0; y < 4; ++y)
						{
							int nx = game.map.normalizeX(baseLocations[j].x + x);
							int ny = game.map.normalizeY(baseLocations[j].y + y);
							minValue = std::min(wheatDistance[ny * game.map.getW() + nx], minValue);
						}
					}
					if (minValue >= 1 && minValue <= window)
					{
						found.push_back(baseLocations[j]);
					}
				}
				return found;
			};
			std::vector<MapGeneratorPoint> startingLocations = startsWithinOfWheat(2);

			// Place swarms
			chooseFreeForBuildingSquares(game, startingLocations, swarm, i);
			// A field grown well past its default size covers the building sites beside it, and the
			// window above only looks 1 to 2 tiles out from where the default-sized wheat field
			// would lie. Rather than fail, look further out from that same wheat for a site the
			// fields have left clear: the colony still starts beside its own farmland, just not
			// right up against it. Only a non-default amount can reach this.
			for (int window = 6;
				 startingLocations.empty() && window <= 24 &&
				 (resources.wheat != 100 || resources.wood != 100 || resources.stone != 100);
				 window += 6)
			{
				startingLocations = startsWithinOfWheat(window);
				chooseFreeForBuildingSquares(game, startingLocations, swarm, i);
			}
			if (startingLocations.size() == 0)
			{
				return false;
			}
			int chosen = context.stream("regions")() % startingLocations.size();
			Building *b =
				addBuilding(game, startingLocations[chosen].x, startingLocations[chosen].y, i,
							IntBuildingType::SWARM_BUILDING, 1, false);
			if (b == NULL)
			{
				return false;
			}

			// Set the initial viewport location
			game.teams[i]->startPosX = b->posX;
			game.teams[i]->startPosY = b->posY;
			game.teams[i]->startPosSet = Team::START_POS_FROM_SWARM;
			context.bootX[i] = b->posX;
			context.bootY[i] = b->posY;

			// Place units around the swarm
			std::vector<MapGeneratorPoint> unitLocations = baseLocations;
			chooseFreeForGroundUnits(game.map, unitLocations, i);
			chooseTouchingBuilding(game.map, unitLocations, b);
			chooseRandomPoints(game.map, context, unitLocations, context.request.nbWorkers);
			if (unitLocations.size() != static_cast<size_t>(context.request.nbWorkers))
			{
				context.detail = "Not enough worker positions";
				return false;
			}
			for (unsigned int n = 0; n < unitLocations.size(); ++n)
			{
				if (!game.addUnit(unitLocations[n].x, unitLocations[n].y, i, WORKER, 0, 0, 0, 0))
				{
					context.detail = "Worker placement failed";
					return false;
				}
			}
		}
		else
		{
			return false;
		}
	}
	return true;
}

void chooseFreeForBuildingSquares(Game &game, std::vector<MapGeneratorPoint> &points,
								  BuildingType *type, int team)
{
	std::vector<MapGeneratorPoint> newPoints;
	for (unsigned int n = 0; n < points.size(); ++n)
	{
		if (game.checkRoomForBuilding(points[n].x, points[n].y, type, team, false))
		{
			newPoints.push_back(MapGeneratorPoint(points[n].x, points[n].y));
		}
	}
	points = newPoints;
}

void chooseFreeForGroundUnits(Map &map, std::vector<MapGeneratorPoint> &points, int team)
{
	std::vector<MapGeneratorPoint> newPoints;
	for (unsigned int n = 0; n < points.size(); ++n)
	{
		if (map.isFreeForGroundUnit(points[n].x, points[n].y, false, 1 << team))
		{
			newPoints.push_back(MapGeneratorPoint(points[n].x, points[n].y));
		}
	}
	points = newPoints;
}

void chooseTouchingBuilding(Map &map, std::vector<MapGeneratorPoint> &points, Building *building)
{
	std::vector<MapGeneratorPoint> newPoints;
	for (unsigned int n = 0; n < points.size(); ++n)
	{
		if (map.doesPosTouchBuilding(points[n].x, points[n].y, building->gid))
		{
			newPoints.push_back(MapGeneratorPoint(points[n].x, points[n].y));
		}
	}
	points = newPoints;
}

Building *addBuilding(Game &game, int x, int y, int team, int type, int level,
					  bool underConstruction)
{
	std::string name = IntBuildingType::typeFromShortNumber(type);
	int typeNum = globalContainer->buildingsTypes.getTypeNum(name, level - 1, underConstruction);
	BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
	if (bt == NULL)
	{
		return NULL;
	}

	if (game.checkRoomForBuilding(x, y, bt, team, false))
	{
		if (bt->maxUnitWorking)
			return game.addBuilding(x, y, typeNum, team, 1, 0);
		else
			return game.addBuilding(x, y, typeNum, team, 0, 0);
	}
	return NULL;
}
} // namespace MapGeneration

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
	const int w = map.getW(), h = map.getH();
	std::vector<std::uint8_t> hard(size_t(w) * h);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			hard[size_t(y) * w + x] = map.isHardSpaceForGroundUnit(x, y, false, 0) ? 1 : 0;
	return hard;
}

// Walking distance from every tile to the nearest deposit of one resource. Workers stand
// beside a deposit rather than on it (a resource tile is not walkable), so the sources are the
// walkable tiles touching one. One flood answers the question for every tile on the map, which
// is what makes scoring a few hundred candidate sites cheap enough to do exhaustively. Distances
// on any map this engine supports fit comfortably in 16 bits, halving the footprint of an array
// this flood (and every scoreAsBuilt call after it) touches over and over.
std::vector<std::int16_t> distanceToResource(Map &map, const std::vector<std::uint8_t> &hard,
											 int resourceType)
{
	const int w = map.getW(), h = map.getH();
	std::vector<std::int16_t> dist(size_t(w) * h, -1);
	// Every cell is enqueued at most once (the dist[np] < 0 guard below), so this never needs
	// more than w*h slots - a flat preallocated FIFO instead of std::queue<int>'s
	// std::deque-backed, block-by-block growth (see computeDistances for the same fix).
	std::vector<int> q(size_t(w) * h);
	size_t qHead = 0, qTail = 0;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			if (map.getResource(x, y).type != resourceType)
				continue;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
					int np = ny * w + nx;
					if (dist[np] < 0 && hard[np])
					{
						dist[np] = 0;
						q[qTail++] = np;
					}
				}
		}
	while (qHead < qTail)
	{
		int p = q[qHead++];
		int x = p % w, y = p / w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (dx == 0 && dy == 0)
					continue;
				int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
				int np = ny * w + nx;
				if (dist[np] < 0 && hard[np])
				{
					dist[np] = dist[p] + 1;
					q[qTail++] = np;
				}
			}
	}
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

bool placeArchipelagoStarts(Game &game, GenerationContext &context, int islandSize)
{
	for (int s = 0; s < context.request.nbTeams; s++)
	{
		// Legacy layout searches may choose a footprint across a torus seam.
		// Buildings wrap those coordinates, and start metadata must wrap too.
		context.bootX[s] = game.map.normalizeX(context.bootX[s]);
		context.bootY[s] = game.map.normalizeY(context.bootY[s]);
		if (game.mapHeader.getNumberOfTeams() <= s)
			game.addTeam();
		int squareSize = 5 + islandSize / 10;
		game.map.setUMatPos(context.bootX[s] + 2, context.bootY[s] + 0, GRASS, squareSize);
		game.map.setUMatPos(context.bootX[s] + 2, context.bootY[s] + 2, GRASS, squareSize);

		Sint32 typeNum = globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
		if (!game.checkRoomForBuilding(context.bootX[s], context.bootY[s],
									   globalContainer->buildingsTypes.get(typeNum), s, false))
		{
			context.detail = "No room for a colony's swarm";
			return false;
		}
		game.teams[s]->startPosX = context.bootX[s];
		game.teams[s]->startPosY = context.bootY[s];
		Building *b = game.addBuilding(context.bootX[s], context.bootY[s], typeNum, s);
		if (!b)
			return false;
		for (int i = 0; i < context.request.nbWorkers; i++)
			if (game.addUnit(context.bootX[s] + (i % 4), context.bootY[s] - 1 - (i / 4), s, WORKER,
							 0, 0, 0, 0) == NULL)
			{
				context.detail = "No room for a colony's starting workers";
				return false;
			}
		game.teams[s]->createLists();
	}
	game.map.smoothResources(islandSize / 10);
	return true;
}

bool placeStarts(Game &game, GenerationContext &context)
{
	for (int s = 0; s < context.request.nbTeams; s++)
	{
		// Legacy layout searches may choose a footprint across a torus seam.
		// Buildings wrap those coordinates, and start metadata must wrap too.
		context.bootX[s] = game.map.normalizeX(context.bootX[s]);
		context.bootY[s] = game.map.normalizeY(context.bootY[s]);
		assert(game.mapHeader.getNumberOfTeams() == s);
		if (game.mapHeader.getNumberOfTeams() <= s)
			game.addTeam();

		game.map.setUMatPos(context.bootX[s] + 2, context.bootY[s] + 0, GRASS, 5);
		game.map.setUMatPos(context.bootX[s] + 2, context.bootY[s] + 2, GRASS, 5);
		game.map.setNoResource(context.bootX[s] + 2, context.bootY[s] + 0, 5);
		game.map.setNoResource(context.bootX[s] + 2, context.bootY[s] + 2, 5);

		Sint32 typeNum = globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
		if (!game.checkRoomForBuilding(context.bootX[s], context.bootY[s],
									   globalContainer->buildingsTypes.get(typeNum), s, false))
		{
			context.detail = "No room for a colony's swarm";
			return false;
		}
		game.teams[s]->startPosX = context.bootX[s];
		game.teams[s]->startPosY = context.bootY[s];
		Building *b = game.addBuilding(context.bootX[s], context.bootY[s], typeNum, s);
		if (!b)
			return false;
		for (int i = 0; i < context.request.nbWorkers; i++)
			if (game.addUnit(context.bootX[s] + (i % 4), context.bootY[s] - 1 - (i / 4), s, WORKER,
							 0, 0, 0, 0) == NULL)
			{
				context.detail = "No room for a colony's starting workers";
				return false;
			}
		game.teams[s]->createLists();
	}
	return true;
}

} // namespace MapGeneration
