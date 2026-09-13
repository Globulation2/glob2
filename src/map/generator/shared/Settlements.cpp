// SPDX-License-Identifier: GPL-3.0-or-later
#include "Settlements.h"
#include "Building.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include <limits>
namespace MapGeneration
{
bool placeSettlement(Game &game, GenerationContext &context, int team,
					 const std::vector<unsigned char> &home, MapGeneratorPoint anchor,
					 const std::string &stream)
{
	context.stage = "settlement";
	auto fail = [&](const std::string &detail)
	{
		context.detail = "Colony " + std::to_string(team) + ": " + detail;
		return false;
	};
	const int w = game.map.getW(), h = game.map.getH();
	if (team < 0 || team >= game.teamsCount() || !game.teams[team] ||
		home.size() != size_t(w) * h || context.request.nbWorkers < 1)
		return fail("invalid home region or colony");
	int type = globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
	const auto *buildingType = globalContainer->buildingsTypes.get(type);
	if (!buildingType)
		return fail("missing swarm type");
	auto inside = [&](int x, int y)
	{ return home[game.map.normalizeY(y) * w + game.map.normalizeX(x)] != 0; };
	std::vector<MapGeneratorPoint> candidates;
	std::vector<std::pair<int, MapGeneratorPoint>> eligible;
	int nearest = std::numeric_limits<int>::max();
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			if (!inside(x, y) || !game.checkRoomForBuilding(x, y, buildingType, team, false))
				continue;
			bool fits = true;
			for (int dy = 0; dy < buildingType->height && fits; ++dy)
				for (int dx = 0; dx < buildingType->width; ++dx)
					if (!inside(x + dx, y + dy))
					{
						fits = false;
						break;
					}
			if (!fits)
				continue;
			int distance = game.map.warpDistSquare(x, y, game.map.normalizeX(anchor.x),
												   game.map.normalizeY(anchor.y));
			eligible.emplace_back(distance, MapGeneratorPoint(x, y));
			if (distance < nearest)
			{
				nearest = distance;
				candidates.clear();
			}
			if (distance <= nearest)
				candidates.emplace_back(x, y);
		}
	if (candidates.empty())
		return fail("no swarm footprint fits inside the home region");
	// The workers need room as well as the swarm: a worker tile is a free tile inside the home
	// region touching the swarm, and Map::doesPosTouchBuilding counts diagonals, so those are the
	// ring around its footprint. A colony whose own kit fills the ground right against the swarm
	// can leave too few of them, which used to fail the whole map. Measure that room before the
	// building goes down, so a site further from the anchor can be taken instead. The first pick is
	// the same random one as ever, drawn the same way from the same nearest-tie list, so any colony
	// that already had the room stays exactly where it was.
	auto workerRoom = [&](const MapGeneratorPoint &at)
	{
		int room = 0;
		for (int y = at.y - 1; y <= at.y + buildingType->height; ++y)
			for (int x = at.x - 1; x <= at.x + buildingType->width; ++x)
			{
				if (x >= at.x && x < at.x + buildingType->width && y >= at.y &&
					y < at.y + buildingType->height)
					continue;
				if (inside(x, y) &&
					game.map.isFreeForGroundUnit(game.map.normalizeX(x), game.map.normalizeY(y),
												 false, 1u << team))
					++room;
			}
		return room;
	};
	MapGeneratorPoint p = candidates[context.bounded(stream, candidates.size())];
	if (workerRoom(p) < context.request.nbWorkers)
	{
		const MapGeneratorPoint *roomier = nullptr;
		int roomiestDistance = std::numeric_limits<int>::max();
		for (const auto &[distance, at] : eligible)
			if (distance < roomiestDistance && workerRoom(at) >= context.request.nbWorkers)
			{
				roomiestDistance = distance;
				roomier = &at;
			}
		if (roomier)
			p = *roomier;
	}
	Building *building = game.addBuilding(p.x, p.y, type, team, 1, 0);
	if (!building)
		return fail("swarm placement failed");
	std::vector<MapGeneratorPoint> workers;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			if (inside(x, y) && game.map.isFreeForGroundUnit(x, y, false, 1u << team) &&
				game.map.doesPosTouchBuilding(x, y, building->gid))
				workers.emplace_back(x, y);
	if (workers.size() < size_t(context.request.nbWorkers))
		return fail("need " + std::to_string(context.request.nbWorkers) + " worker tiles; found " +
					std::to_string(workers.size()));
	for (int i = 0; i < context.request.nbWorkers; ++i)
	{
		size_t selected = i + context.bounded(stream, workers.size() - i);
		std::swap(workers[i], workers[selected]);
		if (!game.addUnit(workers[i].x, workers[i].y, team, WORKER, 0, 0, 0, 0))
			return fail("worker placement failed");
	}
	game.teams[team]->startPosX = building->posX;
	game.teams[team]->startPosY = building->posY;
	game.teams[team]->startPosSet = Team::START_POS_FROM_SWARM;
	context.bootX[team] = building->posX;
	context.bootY[team] = building->posY;
	game.teams[team]->createLists();
	return true;
}
} // namespace MapGeneration
