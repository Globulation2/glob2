// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "MapInternal.h"
#include "Game.h"
#include "Utilities.h"



// Area pathfinding (forbidden, guard, clear, point-to-point)

bool Map::pathfindForbidden(const Uint16 *optionGradient, int teamNumber, int swimClass, int x, int y, int *dx, int *dy)
{
	PERF_SCOPE_TIME(PathArea);
	const Uint16 *gradient=getForbiddenGradient(teamNumber, swimClass);
	if (!gradient) return false;
	bool canSwim=swimClass>0;

	// Pick the neighbor with the highest (base, option) lexicographically. The base gradient
	// dominates; the option gradient is used as a tiebreaker. Reject results where the chosen
	// base is unreachable (i.e. require base > GRADIENT_UNREACHABLE).
	Uint16 bestBase = 0;
	Uint16 bestOption = 0;
	int maxd = 0;
	for (int di=0; di<8; di++)
	{
		int rx=tabClose[di][0];
		int ry=tabClose[di][1];
		int xg=(x+rx)&wMask;
		int yg=(y+ry)&hMask;
		if (swimClass==AIR_MOVEMENT_CLASS ? !isFreeForAirUnit(xg,yg) : ((swimClass==WATER_ONLY_CLASS && !terrainPropertiesAt(xg,yg).swimmable) || !isFreeForGroundUnitNoForbidden(xg, yg, canSwim)))
			continue;
		size_t addr=xg+(yg<<wDec);
		Uint16 base=gradient[addr];
		Uint16 option = (optionGradient!=NULL) ? optionGradient[addr] : 0;
		if (base > bestBase || (base == bestBase && option > bestOption))
		{
			bestBase = base;
			bestOption = option;
			maxd = di;
		}
	}
	if (bestBase > GRADIENT_UNREACHABLE)
	{
		*dx=tabClose[maxd][0];
		*dy=tabClose[maxd][1];
        return true;
    }
    // A complete unreachable pocket can wait for scheduled maintenance. Local
    // blockage of an otherwise usable route requests an earlier delayed field.
    if (gradient[coordToIndex(x,y)]>GRADIENT_UNREACHABLE)
        updateForbiddenGradient(teamNumber,swimClass);
    return false;
}

bool Map::pathfindArea(EntityRandom& random, AreaKind kind, int teamNumber, int swimClass, int x, int y, int *dx, int *dy)
{
	PERF_SCOPE_TIME(PathArea);
	const Uint16 *gradient = (kind == AreaKind::Guard)
		? getGuardAreasGradient(teamNumber, swimClass)
		: getClearAreasGradient(teamNumber, swimClass);
	if (!gradient) return false;
	const Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	const size_t index = coordToIndex(x, y);
	const Uint16 here = gradient[index];
	if (here <= GRADIENT_UNREACHABLE)
		return false; // any existing area is too far away.
	if (kind == AreaKind::Guard && (areaCells[index].guard & teamMask)
		&& game->gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing))
	{
		// Guard-area balancing: crowded seeds sit below the goal, so another
		// area's field can lead out of an over-full one. Take that way out one
		// action in 2^GUARD_LEAVE_CHANCE_SHIFT so the area trickles rather than
		// empties; otherwise step to a higher painted neighbour, or report no
		// move and let the caller's in-area wander take over.
		if (here == GRADIENT_AT_GOAL)
			return false;
		if ((random.nextU32() & ((1 << GUARD_LEAVE_CHANCE_SHIFT) - 1)) == 0)
			return directionByGradient(random, teamMask, swimClass, x, y, gradient, dx, dy, true);
		return directionByGradient(random, teamMask, swimClass, x, y, gradient, dx, dy, true, teamMask);
	}
	if (here == GRADIENT_AT_GOAL)
		return false; // we already are in an area.

	if (directionByGradient(random, teamMask, swimClass, x, y, gradient, dx, dy, true))
		return true;
	if (directionByGradient(random, teamMask, swimClass, x, y, gradient, dx, dy, false))
		return true;

	// Local blockage enrolls a delayed refresh and keeps the published field.
	switch (kind)
	{
		case AreaKind::Guard: updateGuardAreasGradient(teamNumber, swimClass); break;
		case AreaKind::Clear: updateClearAreasGradient(teamNumber, swimClass); break;
	}
	return false;
}

StepResult Map::queryForbiddenStep(const Uint16* option, int team, int swim, int x, int y, int* dx, int* dy)
{
    // Check readiness before any movement decision; absence is not blockage.
    if (!getForbiddenGradient(team,swim)) return StepResult::Pending;
    return pathfindForbidden(option,team,swim,x,y,dx,dy) ? StepResult::Step : StepResult::Blocked;
}
StepResult Map::queryAreaStep(EntityRandom& random, AreaKind kind, int team, int swim, int x, int y, int* dx, int* dy)
{
    const auto* field=kind==AreaKind::Guard ? getGuardAreasGradient(team,swim) : getClearAreasGradient(team,swim);
    if (!field) return StepResult::Pending;
    if (field[coordToIndex(x,y)]==GRADIENT_AT_GOAL) return StepResult::AtGoal;
    return pathfindArea(random,kind,team,swim,x,y,dx,dy) ? StepResult::Step : StepResult::Blocked;
}
