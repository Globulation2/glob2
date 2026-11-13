// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/MapRouting.h"
#include "Utilities.h"
#include "Unit.h"
#include "Game.h"
#include "MapInternal.h"
#include "gradient/GradientRuntime.h"



// Material pathfinding and idle ground movement.

std::optional<GradientFieldKey> MapRouting::materialGradientFieldKey(int team,int material,MovementAccess movementAccessValue,const Uint16* cells) const
{
    int swim=accessClass(movementAccessValue);

    if(!cells)return std::nullopt;
    if(cells==materialGradients[team][material][swim])return GradientFieldKey{GradientFieldFamily::Materials,Uint8(team),Uint8(swim),Uint8(material)};
    if(cells==marketMaterialGradients[team][material][swim])return GradientFieldKey{GradientFieldFamily::Markets,Uint8(team),Uint8(swim),Uint8(material)};
    return std::nullopt;
}

bool MapRouting::pathfindMaterial(EntityRandom& random, int teamNumber, Uint8 resourceType, MovementAccess movementAccessValue, int x, int y, int *dx, int *dy, bool *stopWork, Building *target, bool withMarkets)
{
    int swimClass=accessClass(movementAccessValue);

	PERF_SCOPE_TIME(PathResource);
	assert(resourceType<MaterialCount);
    std::optional<GradientFieldKey> key;
	const Uint16 *gradient=getMaterialGradientSlot(teamNumber, resourceType, movementAccessFromLegacy(swimClass), withMarkets, target,&key);
	if (!gradient) {if(key)gradientMovementDemand(*key);*stopWork=false;return false;}
    const size_t hereIndex=map.coordToIndex(x,y);
    const Uint16 here=gradient[hereIndex];
    if(gradient==materialGradients[teamNumber][resourceType][swimClass])
        key=GradientFieldKey{GradientFieldFamily::Materials,Uint8(teamNumber),Uint8(swimClass),resourceType};
    else if(gradient==marketMaterialGradients[teamNumber][resourceType][swimClass])
        key=GradientFieldKey{GradientFieldFamily::Markets,Uint8(teamNumber),Uint8(swimClass),resourceType};
    // Reading stale zero still demands this field: a removed blocker may now
    // admit a route. Actual escape outcome/quality stays with Forbidden only.
    if(key)gradientMovementDemand(*key);
    std::optional<GradientQueryContext> context;
    if(key && here!=GRADIENT_FORBIDDEN)context=GradientQueryContext{*key,x,y,random};
    GradientQueryResult outcome;
    const auto move=[&]() {

    // Resolve the already-selected destination instead of repeating supplier
    // key rules. Stable cache entries remain pinned while their ticket exists.
    const auto refresh=[&] {
        using Kind=gradient_preparation::Kind;
        if (map.compute.insideJob()) return;
        if (gradient==materialGradients[teamNumber][resourceType][swimClass])
            requestGradientField(&materialGradients[teamNumber][resourceType][swimClass],int(Kind::Materials),teamNumber,resourceType,movementAccessFromLegacy(swimClass));
        else if (gradient==marketMaterialGradients[teamNumber][resourceType][swimClass])
            requestGradientField(&marketMaterialGradients[teamNumber][resourceType][swimClass],int(Kind::Markets),teamNumber,resourceType,movementAccessFromLegacy(swimClass));
        else for (auto& [key,field]:gradientRuntime->materialFields)
            if (field.published==gradient) {
                requestGradientField(&field.published,int(Kind::Markets),field.team,field.resource,movementAccessFromLegacy(field.swim),field.consumer,field.modes);
                break;
            }
    };
	Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	// Own cell stale in the field (see MapRouting::staleCellStep).
	if (staleCellStep(gradient,movementAccessFromLegacy(swimClass),teamMask,x,y,dx,dy)) {
		*stopWork=false;
		return true;
	}
	if (here==GRADIENT_FORBIDDEN)
	{
		*stopWork=true;
		return pathfindForbidden(gradient, teamNumber, movementAccessFromLegacy(swimClass), x, y, dx, dy);
	}
	if (here==GRADIENT_UNREACHABLE)
	{
		*stopWork=true;
		return false;
	}
	*stopWork=false;
    if(here==GRADIENT_AT_GOAL) {
        outcome.atGoal=true;
        outcome.goalValid=key?gradientGoalValid(*key,x,y):map.materialAmountAtSlot(hereIndex,resourceType)>0;
        outcome.travelRequired=!outcome.goalValid;
        if(!outcome.goalValid)refresh();
        return false;
    }
    if(directionByGradient(random,teamMask,movementAccessFromLegacy(swimClass),x,y,gradient,dx,dy,true))return true;
    if(directionByGradient(random,teamMask,movementAccessFromLegacy(swimClass),x,y,gradient,dx,dy,false))return true;
    if(key && key->family==GradientFieldFamily::Markets && gradientGoalValid(*key,x,y)) {
        outcome.atGoal=outcome.goalValid=true;outcome.travelRequired=false;return false;
    }
    refresh();
    return false;
    };
    const bool moved=move();
    if(context) {
        outcome.moved=moved;
        if(moved){outcome.dx=*dx;outcome.dy=*dy;}
        gradientMovementOutcome(*context,gradient,outcome);
    }
    return moved;
}

void MapRouting::pathfindRandom(Unit *unit)
{
	int x=unit->posX;
	int y=unit->posY;
	if (map.terrainPropertiesAt(x,y).groundHealthQ8 < 0)
	{
		pathfindTerrainSafety(unit);
		return;
	}
	if ((map.areaCells[x+(y<<map.wDec)].forbidden)&unit->owner->me)
	{
		if (unit->findDisplacement([&](int* dx, int* dy) { return pathfindForbidden(NULL, unit->owner->teamNumber, unit->movementAccess(), x, y, dx, dy); })
			&& map.terrainPropertiesAt(x+unit->dx,y+unit->dy).groundHealthQ8 >= 0)
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
			&& (map.areaCells[map.coordToIndex(x, y)].guard & unit->owner->me);
		bool da[8];
		int count=0;
		for (int pass = keepInGuardArea ? 0 : 1; pass < 2 && count == 0; pass++)
		{
			for (int di=0; di<8; di++)
			{
				int tx=(x+tabClose[di][0])&map.wMask;
				int ty=(y+tabClose[di][1])&map.hMask;
				if (pass == 0 && !(map.areaCells[map.coordToIndex(tx, ty)].guard & unit->owner->me))
					da[di]=false;
				else if ((unit->performance[WALK] || map.terrainPropertiesAt(tx,ty).swimmable) && map.terrainPropertiesAt(tx,ty).groundHealthQ8 >= 0
					&& map.isFreeForGroundUnit(tx, ty, (unit->performance[SWIM]>0), unit->owner->me))
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

StepResult MapRouting::queryMaterialStep(EntityRandom& random, int team, Uint8 material, MovementAccess movementAccessValue, int x, int y, int* dx, int* dy, bool* stopWork, Building* target, bool markets, Sint32* projectedX, Sint32* projectedY, bool* projectedValid)
{
    int swim=accessClass(movementAccessValue);
    if (projectedValid) *projectedValid=false;
    std::optional<GradientFieldKey> key;
    const auto* field=getMaterialGradientSlot(team,material,movementAccessFromLegacy(swim),markets,target,&key);
    if (!field) {if(key)gradientMovementDemand(*key);*stopWork=false;return StepResult::Pending;}
    if (field[map.coordToIndex(x,y)]==GRADIENT_FORBIDDEN && !getForbiddenGradient(team,movementAccessFromLegacy(swim))) { *stopWork=false; return StepResult::Pending; }
    const bool atGoal=field[map.coordToIndex(x,y)]==GRADIENT_AT_GOAL;
    const bool stepped=pathfindMaterial(random,team,material,movementAccessFromLegacy(swim),x,y,dx,dy,stopWork,target,markets);
    if (projectedX && projectedY && projectedValid)
        *projectedValid=projectMaterialDestination(field,team,material,movementAccessValue,
            x+(stepped?*dx:0),y+(stepped?*dy:0),projectedX,projectedY,markets,target);
    if (atGoal) return StepResult::AtGoal;
    return stepped ? StepResult::Step : StepResult::Blocked;
}

void Map::claimPursuitGradientSignal(Unit& unit,bool recordOutcome,bool moved)
{
    if(!activeClaimsEnabled() || !unit.activeMaterialClaim || !game || !cellCount())return;
    const int team=unit.owner->teamNumber;const auto material=unit.activeMaterialClaim->material;
    const int swim=accessClass(unit.movementAccess());
    std::optional<GradientFieldKey> key;
    const auto* field=routing().getMaterialGradientSlot(team,material,unit.movementAccess(),false,nullptr,&key);
    if(!key)return;
    routing().gradientMovementDemand(*key);
    if(!recordOutcome || !field)return;
    const int x=unit.posX,y=unit.posY;
    const Uint16 here=field[coordToIndex(x,y)];
    // Same classification as pathfindMaterial: no evidence on forbidden cells,
    // an invalid goal is empty-goal evidence and requests the field refresh.
    if(here==GRADIENT_FORBIDDEN)return;
    GradientQueryResult outcome;outcome.moved=moved;
    if(here==GRADIENT_AT_GOAL) {
        outcome.atGoal=true;outcome.goalValid=routing().gradientGoalValid(*key,x,y);outcome.travelRequired=!outcome.goalValid;
        if(!outcome.goalValid && !compute.insideJob() && field==routing().materialGradients[team][material][swim])
            routing().requestGradientField(&routing().materialGradients[team][material][swim],int(gradient_preparation::Kind::Materials),team,material,movementAccessFromLegacy(swim));
    }
    routing().gradientMovementOutcome(GradientQueryContext{*key,x,y,unit.entityRandom},field,outcome);
}
