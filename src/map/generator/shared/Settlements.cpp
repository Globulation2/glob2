// SPDX-License-Identifier: GPL-3.0-or-later
#include "Settlements.h"
#include "Building.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GlobalContainer.h"
#include "Grid.h"
#include "Unit.h"
#include <cmath>
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
		context.telemetry.choice("settlement.failure", detail, team);
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
	context.telemetry.measure("settlement.eligible_sites", int(eligible.size()), team);
	context.telemetry.measure("settlement.nearest_ties", int(candidates.size()), team);
	if (!candidates.empty())
		context.telemetry.measure("settlement.nearest_distance_squared", nearest, team);
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
	const int initialWorkerRoom = workerRoom(p);
	context.telemetry.measure("settlement.requested_workers", context.request.nbWorkers, team);
	context.telemetry.measure("settlement.initial_worker_room", initialWorkerRoom, team);
	if (initialWorkerRoom < context.request.nbWorkers)
	{
		const MapGeneratorPoint *roomier = nullptr;
		int roomiestDistance = std::numeric_limits<int>::max();
		for (const auto &[distance, at] : eligible)
			if (distance < roomiestDistance && workerRoom(at) >= context.request.nbWorkers)
			{
				roomiestDistance = distance;
				roomier = &at;
			}
		context.telemetry.fallback("settlement.worker_room_search", "nearest site too cramped",
								   team);
		context.telemetry.measure("settlement.roomier_site_found", roomier != nullptr, team);
		if (roomier)
		{
			p = *roomier;
			context.telemetry.measure("settlement.relocated_distance_squared", roomiestDistance,
									  team);
		}
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
	context.telemetry.measure("settlement.worker_tiles", int(workers.size()), team);
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
	context.telemetry.measure("settlement.placed_workers", context.request.nbWorkers, team);
	return true;
}

namespace
{
// Shared nearest-footprint search for optional starting buildings. Keep the
// historic tower traversal and tie order: existing maps must not change merely
// because an inn now uses the same search. Coverage is an optional tower filter.
int startingBuildingSite(Game &game, int team, const BuildingType *buildingType, double x, double y,
						 int within, const std::vector<unsigned char> &allowed,
						 const std::vector<MapGeneratorPoint> &cover = {})
{
	if (!buildingType || team < 0 || team >= game.teamsCount() || !game.teams[team] ||
		allowed.size() != size_t(game.map.getW()) * game.map.getH())
}

int placeBuilding(Game &game, int team, const char *typeName, int level, double x, double y,
				  int within, const std::vector<unsigned char> &allowed)
{
	const int type = globalContainer->buildingsTypes.getTypeNum(typeName, level, false);
	const BuildingType *tower = globalContainer->buildingsTypes.get(type);
	if (!tower || team < 0 || team >= game.teamsCount() || !game.teams[team])
		return -1;
	const Map &map = game.map;
	const Torus torus(map);
	const int w = map.getW(), h = map.getH();
	const int cx = int(std::lround(x)), cy = int(std::lround(y));
	int best = -1;
	double nearest = std::numeric_limits<double>::max();
	for (int dy = -within; dy <= within; ++dy)
		for (int dx = -within; dx <= within; ++dx)
		{
			const int px = map.normalizeX(cx + dx), py = map.normalizeY(cy + dy);
			bool fits = true;
			for (int fy = 0; fy < buildingType->height && fits; ++fy)
				for (int fx = 0; fx < buildingType->width && fits; ++fx)
					fits = allowed[map.normalizeY(py + fy) * w + map.normalizeX(px + fx)] != 0;
			// Scan the real footprint and range, not a centre-distance approximation.
			// The optional constraint costs only candidate footprint × cover points;
			// existing callers pass no points and keep their exact selection order.
			for (const MapGeneratorPoint &point : cover)
			{
				int reach = std::numeric_limits<int>::max();
				for (int fy = 0; fy < buildingType->height; ++fy)
					for (int fx = 0; fx < buildingType->width; ++fx)
						reach =
							std::min(reach, torus.chebyshev(px + fx, py + fy, point.x, point.y));
				fits = fits && reach <= buildingType->shootingRange;
			}
			if (!fits || !game.checkRoomForBuilding(px, py, buildingType, team, false))
				continue;
			const double mx = cx + dx + buildingType->width / 2.0 - x,
						 my = cy + dy + buildingType->height / 2.0 - y;
			const double d = mx * mx + my * my;
			if (d < nearest)
			{
				nearest = d;
				best = py * w + px;
			}
		}
	(void)h;
	if (best < 0)
		return -1;
	return best;
}
} // namespace

int placeTower(Game &game, int team, int level, double x, double y, int within,
			   const std::vector<unsigned char> &allowed, bool stocked,
			   const std::vector<MapGeneratorPoint> &cover, bool supplyStone)
{
	const int type = globalContainer->buildingsTypes.getTypeNum("defencetower", level, false);
	const BuildingType *tower = globalContainer->buildingsTypes.get(type);
	const int best = startingBuildingSite(game, team, tower, x, y, within, allowed, cover);
	if (best < 0)
		return -1;
	const int w = game.map.getW();
	Building *building = game.addBuilding(best % w, best / w, type, team, 1, 0);
	if (!building)
		return -1;
	building->bullets = stocked ? tower->maxBullets : 0;
	if (supplyStone)
	{
		// Reserve stone and loaded bullets are separate stores. Supplying both
		// avoids an immediate delivery job, but combat consumes them normally and
		// later replenishment still needs miners. Refresh the call lists now so
		// the initial save does not retain the empty store's worker request.
		building->resources[STONE] = tower->maxResource[STONE];
		building->updateCallLists();
	}
	// The colony's lists were built when its swarm went down; the tower joins its turrets the way
	// Team::createLists would have taken it in.
	game.teams[team]->turrets.push_back(building);
	return best;
}
int placeStartingBuilding(Game &game, int team, const char *name, int level, double x, double y,
						  int within, const std::vector<unsigned char> &allowed,
						  const std::vector<int> &supplies)
{
	const int type = globalContainer->buildingsTypes.getTypeNum(name, level, false);
	const BuildingType *buildingType = globalContainer->buildingsTypes.get(type);
	// Validate supplies before mutation. Callers choose resource kinds explicitly:
	// filling an inn's whole table would silently give away the contested fruit.
	for (int resource : supplies)
		if (resource < 0 || resource >= MAX_NB_RESOURCES)
			return -1;
	const int site = startingBuildingSite(game, team, buildingType, x, y, within, allowed);
	if (site < 0)
		return -1;
	const int w = game.map.getW();
	Building *building = game.addBuilding(site % w, site / w, type, team, 1, 0);
	if (!building)
		return -1;
	for (int resource : supplies)
		building->resources[resource] = buildingType->maxResource[resource];
	// Game::addBuilding already registers markets and virtual buildings. Add
	// only the static lists it leaves to the setup caller; createLists cannot
	// be called again on a colony that already owns a swarm or towers.
	if (buildingType->unitProductionTime)
		game.teams[team]->swarms.push_back(building);
	if (buildingType->shootingRange)
		game.teams[team]->turrets.push_back(building);
	if (buildingType->zonable[WORKER])
		game.teams[team]->clearingFlags.push_back(building);
	// Register this building's feeding/work services, leaving existing task
	// order alone. Normal tick logic resumes deliveries as supplies run out.
	building->update();
	return site;
	// The colony's lists were built when its swarm went down (Team::createLists); a building placed
	// since joins the lists that function fills from the type, the way it would have taken it in.
	if (tower->shootingRange)
		game.teams[team]->turrets.push_back(building);
	if (tower->unitProductionTime)
		game.teams[team]->swarms.push_back(building);
	return best;
}

int countBuildings(const Game &game, int team, const char *typeName)
{
	if (team < 0 || team >= game.teamsCount() || !game.teams[team])
		return 0;
	int count = 0;
	for (int i = 0; i < Building::MAX_COUNT; ++i)
		if (const Building *b = game.teams[team]->myBuildings[i])
			if (b->type && b->type->type == typeName && !b->type->isBuildingSite)
				++count;
	return count;
}

int placeTower(Game &game, int team, int level, double x, double y, int within,
			   const std::vector<unsigned char> &allowed, bool stocked)
{
	const int at = placeBuilding(game, team, "defencetower", level, x, y, within, allowed);
	if (at < 0)
		return -1;
	const int w = game.map.getW();
	Building *building =
		game.teams[team]->myBuildings[Building::GIDtoID(game.map.getBuilding(at % w, at / w))];
	if (!building)
		return -1;
	building->bullets = stocked ? building->type->maxBullets : 0;
	return at;
}
} // namespace MapGeneration
