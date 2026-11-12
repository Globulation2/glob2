// SPDX-License-Identifier: GPL-3.0-or-later
// Point-to-point movement searches: the unified A* (field/astar/AStar.h) over the live map.
#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/MapRouting.h"
#include "Game.h"
#include "gradient/GradientRuntime.h"
#include <bit>
#include "MapInternal.h"
#include "Utilities.h"
#include "field/TerrainMovementCosts.h"
#include "MapAStarDomain.h"
#include "field/astar/AStarCapture.h"
#include <cassert>

astar::Workspace &MapRouting::pointSearchWorkspace()
{
	if (!pointSearch)
		pointSearch = std::make_unique<astar::Workspace>();
	return *pointSearch;
}

int MapRouting::pointSearchClass(MovementAccess movementAccessValue, Uint32 teamMask) const
{
	const int physical = accessClass(movementAccessValue);
	const int readerTeam = map.game && std::has_single_bit(teamMask) ? int(std::countr_zero(teamMask)) : -1;
	return readerTeam >= 0 ? preparationCostClass(readerTeam, physical) : physical;
}

bool MapRouting::pathfindPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy, MovementAccess movementAccessValue, Uint32 teamMask, int maximumLength)
{
	const int swimClass = pointSearchClass(movementAccessValue, teamMask);
	PERF_SCOPE_TIME(PathPoint);
	*dx = *dy = 0;
	if (maximumLength < 0)
		return false;
	targetX = (targetX + map.w) & map.wMask;
	targetY = (targetY + map.h) & map.hMask;
	const bool air = swimClass == AIR_MOVEMENT_CLASS;
	astar::Query query;
	query.width = map.w;
	query.height = map.h;
	query.start = std::uint32_t(map.coordToIndex(x, y));
	query.target = std::uint32_t(map.coordToIndex(targetX, targetY));
	// Step costs are in gradient units (see MapInternal.h).
	query.maximumCost = std::uint32_t(std::min<std::int64_t>(Map::GRADIENT_COST_LIMIT, std::int64_t(maximumLength) * GRADIENT_STEP));
	query.floor = MapAStar::floorOf(map.routeStepFloor(swimClass));
	const auto &view = map.liveView();
	astar::Result result;
	if (air)
	{
		// A flyer cannot enter a blocked target; it stops beside it instead.
		const auto i = query.target;
		const bool blocked = !map.terrainPropertiesAt(i).flyable || map.resourceBlocksAir(i) ||
							 map.getAirUnit(targetX, targetY) != NOGUID;
		query.goalRadius = blocked ? 1 : 0;
		const MapAStar::UnitDomain<true> domain{view, swimClass, teamMask};
		result = pointSearchWorkspace().search(domain, query);
		if (astar::capture::enabled()) [[unlikely]]
			astar::capture::point(domain, query, pointSearchWorkspace().windowSide());
	}
	else
	{
		const MapAStar::UnitDomain<false> domain{view, swimClass, teamMask};
		result = pointSearchWorkspace().search(domain, query);
		if (astar::capture::enabled()) [[unlikely]]
			astar::capture::point(domain, query, pointSearchWorkspace().windowSide());
	}
	if (result.status != astar::Status::Found)
		return false;
	*dx = result.firstDx;
	*dy = result.firstDy;
	return true;
}

void MapRouting::pathfindPointTargets(int x, int y, std::span<const PointTarget> targets, MovementAccess movementAccessValue,
									  Uint32 teamMask, int maximumLength, std::vector<PointTargetResult> &results)
{
	const int swimClass = pointSearchClass(movementAccessValue, teamMask);
	assert(swimClass != AIR_MOVEMENT_CLASS);
	PERF_SCOPE_TIME(PathPoint);
	results.assign(targets.size(), {});
	if (maximumLength < 0 || targets.empty())
		return;
	std::vector<std::uint32_t> &goals = pointTargetCells;
	goals.clear();
	for (const auto &t : targets)
		goals.push_back(std::uint32_t(map.coordToIndex(t.x, t.y)));
	astar::GoalQuery query;
	query.width = map.w;
	query.height = map.h;
	query.start = std::uint32_t(map.coordToIndex(x, y));
	query.maximumCost = std::uint32_t(std::min<std::int64_t>(Map::GRADIENT_COST_LIMIT, std::int64_t(maximumLength) * GRADIENT_STEP));
	query.sizing = MapAStar::floorOf(map.routeStepFloor(swimClass));
	pointTargetResults.resize(goals.size());
	const MapAStar::UnitDomain<false> domain{map.liveView(), swimClass, teamMask};
	pointSearchWorkspace().searchGoals(domain, query, goals, pointTargetResults);
	if (astar::capture::enabled()) [[unlikely]]
		astar::capture::goals(domain, query, goals, pointSearchWorkspace().windowSide());
	for (std::size_t i = 0; i < goals.size(); ++i)
		results[i] = {pointTargetResults[i].reached, Sint8(pointTargetResults[i].firstDx), Sint8(pointTargetResults[i].firstDy)};
}

// Compatibility entry point for existing callers. All movement uses the same A*.
bool MapRouting::pathfindAirPointToPoint(int x,int y,int targetX,int targetY,int* dx,int* dy)
{
    return pathfindPointToPoint(x,y,targetX,targetY,dx,dy,MovementAccess::Air,0,Map::GRADIENT_COST_LIMIT/GRADIENT_STEP);
}
