// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "Utilities.h"
#include "Unit.h"
#include "Game.h"
#include "MapInternal.h"
#include "gradient/GradientRuntime.h"



// Material pathfinding and idle ground movement.

bool Map::pathfindMaterial(EntityRandom& random, int teamNumber, Uint8 resourceType, int swimClass, int x, int y, int *dx, int *dy, bool *stopWork, Building *target, bool withMarkets)
{
	PERF_SCOPE_TIME(PathResource);
	assert(resourceType<MaterialCount);
	const Uint16 *gradient=getMaterialGradientSlot(teamNumber, resourceType, swimClass, withMarkets, target);
	if (!gradient) { *stopWork=false; return false; }
    // Only owner queries of ground natural fields enter the adaptive model.
    // Cold reads may be null; air and consumer fields retain their own policies.
    const bool naturalGround=game && !compute.insideJob() && swimClass<SWIM_CLASS_COUNT
        && gradient==materialGradients[teamNumber][resourceType][swimClass];
    std::optional<EntityRandom> before;
    if (naturalGround && gradientRuntime->qualityProbe) before.emplace(random);
    if (naturalGround) {
        gradientRuntime->initializeResourcePolicy(*this);
        auto& field=gradientRuntime->resources.fields[(teamNumber*MaterialCount+resourceType)*SWIM_CLASS_COUNT+swimClass];
        field.queried(game->stepCounter,gradient[coordToIndex(x,y)]!=GRADIENT_FORBIDDEN);
    }
    const auto move=[&]() {

	size_t hereIndex=coordToIndex(x, y);
    Uint16 here=gradient[hereIndex];
    // Resolve the already-selected destination instead of repeating supplier
    // key rules. Stable cache entries remain pinned while their ticket exists.
    const auto refresh=[&] {
        using Kind=gradient_preparation::Kind;
        if (compute.insideJob()) return;
        if (gradient==materialGradients[teamNumber][resourceType][swimClass])
            requestGradientField(&materialGradients[teamNumber][resourceType][swimClass],int(Kind::Materials),teamNumber,resourceType,swimClass);
        else if (gradient==marketMaterialGradients[teamNumber][resourceType][swimClass])
            requestGradientField(&marketMaterialGradients[teamNumber][resourceType][swimClass],int(Kind::Markets),teamNumber,resourceType,swimClass);
        else for (auto& [key,field]:gradientRuntime->materialFields)
            if (field.published==gradient) {
                requestGradientField(&field.published,int(Kind::Markets),field.team,field.resource,field.swim,field.consumer,field.modes);
                break;
            }
    };
	Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	if (here==GRADIENT_FORBIDDEN)
	{
		*stopWork=true;
		return pathfindForbidden(gradient, teamNumber, swimClass, x, y, dx, dy);
	}
	if (here==GRADIENT_UNREACHABLE)
	{
		*stopWork=true;
		return false;
	}
	*stopWork=false;
    if (here==GRADIENT_AT_GOAL) {
        if (!materialAmountAtSlot(hereIndex,resourceType)) refresh();
        return false;
    }
	if (directionByGradient(random, teamMask, swimClass, x, y, gradient, dx, dy, true))
		return true;
    if (directionByGradient(random, teamMask, swimClass, x, y, gradient, dx, dy, false)) return true;
    refresh();
    return false;
    };
    const bool moved=move();
    if (naturalGround && gradientRuntime->qualityProbe && gradient[coordToIndex(x,y)]!=GRADIENT_FORBIDDEN)
        gradientRuntime->qualityProbe->observe(*this,teamNumber,resourceType,swimClass,x,y,gradient,*before);
    return moved;
}

void Map::pathfindRandom(Unit *unit)
{
	int x=unit->posX;
	int y=unit->posY;
	if (terrainPropertiesAt(x,y).groundHealthQ8 < 0)
	{
		pathfindTerrainSafety(unit);
		return;
	}
	if ((areaCells[x+(y<<wDec)].forbidden)&unit->owner->me)
	{
		if (unit->findDisplacement([&](int* dx, int* dy) { return pathfindForbidden(NULL, unit->owner->teamNumber, unit->swimClass(), x, y, dx, dy); })
			&& terrainPropertiesAt(x+unit->dx,y+unit->dy).groundHealthQ8 >= 0)
		{
			unit->directionFromDxDy();
		}
		else
		{
			unit->dx=0;
			unit->dy=0;
			unit->direction=8;
		}
	}
	else
	{
		// Guard-area balancing: a warrior on the paint steps onto a free painted
		// neighbour first, so a full area does not empty all at once, and takes
		// the ordinary step when none is free rather than standing still.
		const bool keepInGuardArea = unit->hasCapability(UnitRuntimeTraits::GuardIdle)
			&& unit->owner->game->gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing)
			&& (areaCells[coordToIndex(x, y)].guard & unit->owner->me);
		bool da[8];
		int count=0;
		for (int pass = keepInGuardArea ? 0 : 1; pass < 2 && count == 0; pass++)
		{
			for (int di=0; di<8; di++)
			{
				int tx=(x+tabClose[di][0])&wMask;
				int ty=(y+tabClose[di][1])&hMask;
				if (pass == 0 && !(areaCells[coordToIndex(tx, ty)].guard & unit->owner->me))
					da[di]=false;
				else if ((unit->performance[WALK] || terrainPropertiesAt(tx,ty).swimmable) && terrainPropertiesAt(tx,ty).groundHealthQ8 >= 0
					&& isFreeForGroundUnit(tx, ty, (unit->performance[SWIM]>0), unit->owner->me))
				{
					da[di]=true;
					count++;
				}
				else
					da[di]=false;
			}
		}
		if (count==0)
		{
			unit->dx=0;
			unit->dy=0;
			unit->direction=8;
			return;
		}
		int dir=unit->entityRandom.nextU32()%count;
		for (int di=0; di<8; di++)
			if (da[di] && dir--==0)
			{
				unit->dx=tabClose[di][0];
				unit->dy=tabClose[di][1];
				unit->direction=di;
				return;
			}
		assert(false);
	}
}

StepResult Map::queryMaterialStep(EntityRandom& random, int team, Uint8 material, int swim, int x, int y, int* dx, int* dy, bool* stopWork, Building* target, bool markets)
{
    const auto* field=getMaterialGradientSlot(team,material,swim,markets,target);
    if (!field) { *stopWork=false; return StepResult::Pending; }
    if (field[coordToIndex(x,y)]==GRADIENT_FORBIDDEN && !getForbiddenGradient(team,swim)) { *stopWork=false; return StepResult::Pending; }
    const bool atGoal=field[coordToIndex(x,y)]==GRADIENT_AT_GOAL;
    const bool stepped=pathfindMaterial(random,team,material,swim,x,y,dx,dy,stopWork,target,markets);
    if (atGoal) return StepResult::AtGoal;
    return stepped ? StepResult::Step : StepResult::Blocked;
}
