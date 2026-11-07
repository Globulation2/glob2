#include "GenerationWork.h"
#include "GenerationNumeric.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PowerOfTwo.h"
#include "Settlements.h"
#include "StartingLayout.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "Grid.h"
#include <cmath>
#include <limits>
namespace MapGeneration
{
bool placeSettlement(GenerationWorld &game, GenerationContext &context, int team,
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
	if (!game.hasTeam(team) || home.size() != size_t(w) * h || context.request.nbWorkers < 1)
		return fail("invalid home region or colony");
	const GenerationBuildingType startingType = game.startingBuildingType();
	const int type = startingType.typeNum;
	const auto *buildingType = startingType ? &startingType : nullptr;
	if (!buildingType || !buildingType->runtimeAvailable)
		return fail("catalog has no available starting building");
	auto inside = [&](int x, int y)
	{ return home.at(game.map.normalizeY(y) * w + game.map.normalizeX(x)) != 0; };
	std::vector<MapGeneratorPoint> candidates;
	std::vector<std::pair<int, MapGeneratorPoint>> eligible;
	int nearest = std::numeric_limits<int>::max();
	for (int y = 0; y < h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			if (!inside(x, y) || !game.checkRoomForBuilding(x, y, *buildingType, team))
				continue;
			bool fits = true;
			for (int dy = 0; dy < buildingType->height && fits; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = 0; dx < buildingType->width; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					if (!inside(x + dx, y + dy))
					{
						fits = false;
						break;
					}
				}
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
		{
			::MapGeneration::generationCheckpoint();
			for (int x = at.x - 1; x <= at.x + buildingType->width; ++x)
			{
				::MapGeneration::generationCheckpoint();
				if (x >= at.x && x < at.x + buildingType->width && y >= at.y &&
					y < at.y + buildingType->height)
					continue;
				if (inside(x, y) &&
					game.map.isFreeForGroundUnit(game.map.normalizeX(x), game.map.normalizeY(y),
												 false, 1u << team))
					++room;
			}
		}
		return room;
	};
	MapGeneratorPoint p = candidates.at(context.bounded(stream, candidates.size()));
	const int initialWorkerRoom = workerRoom(p);
	context.telemetry.measure("settlement.requested_workers", context.request.nbWorkers, team);
	context.telemetry.measure("settlement.initial_worker_room", initialWorkerRoom, team);
	if (initialWorkerRoom < context.request.nbWorkers)
	{
		const MapGeneratorPoint *roomier = nullptr;
		int roomiestDistance = std::numeric_limits<int>::max();
		for (const auto &[distance, at] : eligible)
		{
			::MapGeneration::generationCheckpoint();
			if (distance < roomiestDistance && workerRoom(at) >= context.request.nbWorkers)
			{
				roomiestDistance = distance;
				roomier = &at;
			}
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
	const GenerationBuilding building =
		game.addBuilding(p.x, p.y, type, team, std::min(1, buildingType->assignmentLimit), 0);
	if (!building)
		return fail("swarm placement failed");
	std::vector<MapGeneratorPoint> workers;
	for (int y = 0; y < h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			if (inside(x, y) && game.map.isFreeForGroundUnit(x, y, false, 1u << team) &&
				touchesStartingFootprint(x, y, building.posX, building.posY, buildingType->width,
										 buildingType->height, game.map.getMaskW(),
										 game.map.getMaskH()))
				workers.emplace_back(x, y);
		}
	}
	context.telemetry.measure("settlement.worker_tiles", int(workers.size()), team);
	if (workers.size() < size_t(context.request.nbWorkers))
		return fail("need " + std::to_string(context.request.nbWorkers) + " worker tiles; found " +
					std::to_string(workers.size()));
	for (int i = 0; i < context.request.nbWorkers; ++i)
	{
		::MapGeneration::generationCheckpoint();
		size_t selected = i + context.bounded(stream, workers.size() - i);
		std::swap(workers.at(i), workers.at(selected));
		if (!game.addWorker(workers.at(i).x, workers.at(i).y, team))
			return fail("worker placement failed");
	}
	game.setTeamStart(team, building.posX, building.posY, GenerationTeamStart::FromSwarm);
	context.bootX[team] = building.posX;
	context.bootY[team] = building.posY;
	game.finishTeam(team);
	context.telemetry.measure("settlement.placed_workers", context.request.nbWorkers, team);
	return true;
}

namespace
{
// Shared nearest-footprint search for optional starting buildings. Keep the
// historic tower traversal and tie order: existing maps must not change merely
// because an inn now uses the same search. Coverage is an optional tower filter.
int startingBuildingSite(GenerationWorld &game, int team,
						 const GenerationBuildingType *buildingType, double x, double y, int within,
						 const std::vector<unsigned char> &allowed,
						 const std::vector<MapGeneratorPoint> &cover = {})
{
	if (!buildingType || !game.hasTeam(team) ||
		allowed.size() != size_t(game.map.getW()) * game.map.getH())
		return -1;
	const GenerationMap &map = game.map;
	const Torus torus(map);
	const int w = map.getW(), h = map.getH();
	const int cx = int(::MapGeneration::Numeric::lround(x)),
			  cy = int(::MapGeneration::Numeric::lround(y));
	int best = -1;
	double nearest = std::numeric_limits<double>::max();
	for (int dy = -within; dy <= within; ++dy)
	{
		::MapGeneration::generationCheckpoint();
		for (int dx = -within; dx <= within; ++dx)
		{
			::MapGeneration::generationCheckpoint();
			const int px = map.normalizeX(cx + dx), py = map.normalizeY(cy + dy);
			bool fits = true;
			for (int fy = 0; fy < buildingType->height && fits; ++fy)
			{
				::MapGeneration::generationCheckpoint();
				for (int fx = 0; fx < buildingType->width && fits; ++fx)
				{
					::MapGeneration::generationCheckpoint();
					fits = allowed.at(map.normalizeY(py + fy) * w + map.normalizeX(px + fx)) != 0;
				}
			}
			// Scan the real footprint and range, not a centre-distance approximation.
			// The optional constraint costs only candidate footprint × cover points;
			// existing callers pass no points and keep their exact selection order.
			for (const MapGeneratorPoint &point : cover)
			{
				::MapGeneration::generationCheckpoint();
				int reach = std::numeric_limits<int>::max();
				for (int fy = 0; fy < buildingType->height; ++fy)
				{
					::MapGeneration::generationCheckpoint();
					for (int fx = 0; fx < buildingType->width; ++fx)
					{
						::MapGeneration::generationCheckpoint();
						reach =
							std::min(reach, torus.chebyshev(px + fx, py + fy, point.x, point.y));
					}
				}
				fits = fits && reach <= buildingType->shootingRange;
			}
			if (!fits || !game.checkRoomForBuilding(px, py, *buildingType, team))
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
	}
	(void)h;
	if (best < 0)
		return -1;
	return best;
}
} // namespace

int placeBuilding(GenerationWorld &game, int team, const char *typeName, int level, double x,
				  double y, int within, const std::vector<unsigned char> &allowed)
{
	const GenerationBuildingType found = game.buildingType(typeName, level, false);
	const int type = found.typeNum;
	const GenerationBuildingType *buildingType = found ? &found : nullptr;
	const int best = startingBuildingSite(game, team, buildingType, x, y, within, allowed);
	if (best < 0)
		return -1;
	const int w = game.map.getW();
	const GenerationBuilding building =
		game.addBuilding(powerOfTwoRemainder(best, w), best / w, type, team,
						 std::min(1, game.buildingType(type).assignmentLimit), 0);
	if (!building)
		return -1;
	game.registerBuilding(team, building);
	return best;
}

int countBuildings(const GenerationWorld &game, int team, const char *typeName)
{
	if (!game.hasTeam(team))
		return 0;
	int count = 0;
	for (const GenerationBuilding &b : game.teamBuildings(team))
	{
		::MapGeneration::generationCheckpoint();
		if (b.type && b.type.type == typeName && !b.type.isBuildingSite)
			++count;
	}
	return count;
}

int placeTower(GenerationWorld &game, int team, int level, double x, double y, int within,
			   const std::vector<unsigned char> &allowed, bool stocked,
			   const std::vector<MapGeneratorPoint> &cover, bool supplyStone)
{
	// Authored map kits explicitly request the historical tower definition.
	const GenerationBuildingType found = game.buildingType("defencetower", level, false);
	const int type = found.typeNum;
	const GenerationBuildingType *tower = found ? &found : nullptr;
	const int best = startingBuildingSite(game, team, tower, x, y, within, allowed, cover);
	if (best < 0)
		return -1;
	const int w = game.map.getW();
	const GenerationBuilding building =
		game.addBuilding(powerOfTwoRemainder(best, w), best / w, type, team,
						 std::min(1, game.buildingType(type).assignmentLimit), 0);
	if (!building)
		return -1;
	game.setBuildingBullets(building, stocked ? tower->maxBullets : 0);
	if (supplyStone)
	{
		// Reserve stone and loaded bullets are separate stores. Supplying both
		// avoids an immediate delivery job, but combat consumes them normally and
		// later replenishment still needs miners. Refresh the call lists now so
		// the initial save does not retain the empty store's worker request.
		game.setBuildingMaterial(building, materialIndex(MaterialId::Stone),
								 tower->maxMaterial[materialIndex(MaterialId::Stone)]);
		game.updateBuildingCallLists(building);
	}
	game.registerBuilding(team, building);
	return best;
}
int placeStartingBuilding(GenerationWorld &game, int team, const char *name, int level, double x,
						  double y, int within, const std::vector<unsigned char> &allowed,
						  const std::vector<int> &supplies)
{
	const GenerationBuildingType found = game.buildingType(name, level, false);
	const int type = found.typeNum;
	const GenerationBuildingType *buildingType = found ? &found : nullptr;
	// Validate supplies before mutation. Callers choose resource kinds explicitly:
	// filling an inn's whole table would silently give away the contested fruit.
	for (int resource : supplies)
	{
		::MapGeneration::generationCheckpoint();
		if (resource < 0 || resource >= MaterialCount)
			return -1;
	}
	const int site = startingBuildingSite(game, team, buildingType, x, y, within, allowed);
	if (site < 0)
		return -1;
	const int w = game.map.getW();
	const GenerationBuilding building =
		game.addBuilding(powerOfTwoRemainder(site, w), site / w, type, team,
						 std::min(1, buildingType->assignmentLimit), 0);
	if (!building)
		return -1;
	for (int resource : supplies)
	{
		::MapGeneration::generationCheckpoint();
		game.setBuildingMaterial(building, resource, buildingType->maxMaterial[resource]);
	}
	game.registerBuilding(team, building);
	// Register this building's feeding/work services, leaving existing task
	// order alone. Normal tick logic resumes deliveries as supplies run out.
	game.updateBuilding(building);
	return site;
}
} // namespace MapGeneration
