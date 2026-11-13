// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/MapRouting.h"
#include "MapInternal.h"
#include "gradient/GradientRuntime.h"
#include "gradient/DirectFlightDirection.h"
#include "Game.h"
#include "Utilities.h"

void MapRouting::gradientMovementDemand(GradientFieldKey key)
{
    key.swim=accessClass(movementAccessFromLegacy(key.swim));
    if(map.game && !map.compute.insideJob())gradientRuntime->adaptive.queried(key,map.game->stepCounter);
}
void MapRouting::gradientMovementOutcome(const GradientQueryContext& context,const Uint16* cells,const GradientQueryResult& result)
{
    if(!map.game || map.compute.insideJob() || !cells)return;
    GradientMovementEvidence evidence;
    evidence.emptyGoal=result.atGoal && !result.goalValid;
    evidence.reachableNoMove=result.travelRequired && !result.moved && cells[map.coordToIndex(context.x,context.y)]>GRADIENT_UNREACHABLE && !result.atGoal;
    gradientRuntime->adaptive.enroll(context.key).recordMovement(evidence);
    if(auto& probe=gradientRuntime->qualityProbe) {
        probe->recordMovement(context.key,evidence);
        probe->observe(map,context,cells);
    }
}
void MapRouting::directGradientMovement(GradientFieldKey key,int x,int y,const EntityRandom& random,const Uint16* cells,bool moved,bool destinationValid,bool recordDemand)
{
    if(recordDemand)gradientMovementDemand(key);
    if(!cells)return;
    GradientQueryContext context{key,x,y,random,nullptr,GradientQueryContext::Mode::DirectFlight};
    GradientQueryResult result;
    result.atGoal=cells[map.coordToIndex(x,y)]==GRADIENT_AT_GOAL ||
        (key.family==GradientFieldFamily::Markets && cells[map.coordToIndex(x,y)]>GRADIENT_UNREACHABLE && gradientGoalValid(key,x,y));
    result.goalValid=result.atGoal&&gradientGoalValid(key,x,y);
    result.travelRequired=!result.goalValid;
    result.moved=moved;
    gradientMovementOutcome(context,cells,result);
    if(!destinationValid || (result.atGoal&&!result.goalValid)) {
        if(auto** slot=gradientFieldDestination(key))
            requestGradientField(slot,int(key.family),key.team,key.material==GradientFieldKey::NoMaterial?0:key.material,movementAccessFromLegacy(key.swim));
    }
}
GradientQueryResult MapRouting::selectGradientQuery(const GradientQueryContext& context,const Uint16* cells) const
{
    auto copy=context;return selectGradientQueryImpl(copy,cells);
}
GradientQueryResult MapRouting::selectGradientQueryImpl(GradientQueryContext& context,const Uint16* cells) const
{
    GradientQueryResult result;
    if(!cells)return result;
    const auto key=context.key;
    const int x=context.x,y=context.y,swim=key.swim;
    const auto index=map.coordToIndex(x,y);
    const auto here=cells[index];
    const Uint32 mask=Team::teamNumberToMask(key.team);
    if(key.family==GradientFieldFamily::Forbidden) {
        // Escaped units may still call this selector; preserve its historical
        // direction while excluding intentional arrival from stall evidence.
        result.goalValid=gradientGoalValid(key,x,y);
        result.atGoal=here==GRADIENT_AT_GOAL || result.goalValid;
        result.travelRequired=!result.goalValid;
        Uint16 bestBase=0,bestOption=0;int best=0;
        for(int di=0;di<8;++di) {
            const int nx=(x+tabClose[di][0])&map.wMask,ny=(y+tabClose[di][1])&map.hMask;
            if(swim==AIR_MOVEMENT_CLASS?!map.isFreeForAirUnit(nx,ny):
                ((swim==WATER_ONLY_CLASS&&!map.terrainPropertiesAt(nx,ny).swimmable)||!map.isFreeForGroundUnitNoForbidden(nx,ny,swim>0)))continue;
            const auto at=map.coordToIndex(nx,ny);
            const Uint16 base=cells[at],option=context.option?context.option[at]:0;
            if(base>bestBase || (base==bestBase && option>bestOption)) {bestBase=base;bestOption=option;best=di;}
        }
        result.moved=bestBase>GRADIENT_UNREACHABLE;
        if(result.moved){result.dx=tabClose[best][0];result.dy=tabClose[best][1];}
        return result;
    }
    result.atGoal=here==GRADIENT_AT_GOAL;
    if(result.atGoal) {
        result.goalValid=gradientGoalValid(key,x,y);
        result.travelRequired=!result.goalValid;
        return result;
    }
    if(here<=GRADIENT_UNREACHABLE) {
        // Own cell stale in the field (see MapRouting::staleCellStep).
        if(context.mode!=GradientQueryContext::Mode::DirectFlight && key.family!=GradientFieldFamily::Materials
            && key.family!=GradientFieldFamily::Markets && staleCellStep(cells,movementAccessFromLegacy(swim),mask,x,y,&result.dx,&result.dy)) {
            result.moved=true;
            result.travelRequired=true;
        }
        return result;
    }
    if(context.mode==GradientQueryContext::Mode::DirectFlight) {
        Sint32 tx=0,ty=0;
        const bool exactPeak=getGlobalGradientDestination(cells,x,y,&tx,&ty);
        // Material pickup accepts weighted supplier peaks; its live destination
        // reader uses ascent coordinates, not the exact65535 return flag.
        const bool material=key.family==GradientFieldFamily::Materials || key.family==GradientFieldFamily::Markets;
        result.moved=material || exactPeak;
        if(result.moved) {
            result.goalValid=gradientGoalValid(key,tx,ty);
            const auto step=direct_flight::select(x,y,tx,ty,map.getW(),map.getH(),
                [&](int nx,int ny){return map.isFreeForMovement(nx,ny,AIR_MOVEMENT_CLASS,mask);});
            result.dx=step.dx;result.dy=step.dy;
            result.moved=result.goalValid && (result.dx||result.dy);
            if(!result.moved && gradientGoalValid(key,x,y)){result.atGoal=result.goalValid=true;result.travelRequired=false;}
        }
        return result;
    }
    if(key.family==GradientFieldFamily::Guard && (map.areaCells[index].guard&mask) &&
        map.game->gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing)) {
        // This branch is intentional area balancing, never a failed required trip.
        result.travelRequired=false;result.goalValid=gradientGoalValid(key,x,y);
        if((context.random.nextU32()&((1<<GUARD_LEAVE_CHANCE_SHIFT)-1))==0)
            result.moved=directionByGradient(context.random,mask,movementAccessFromLegacy(swim),x,y,cells,&result.dx,&result.dy,true);
        else result.moved=directionByGradient(context.random,mask,movementAccessFromLegacy(swim),x,y,cells,&result.dx,&result.dy,true,mask);
        return result;
    }
    result.moved=directionByGradient(context.random,mask,movementAccessFromLegacy(swim),x,y,cells,&result.dx,&result.dy,true);
    if(!result.moved)result.moved=directionByGradient(context.random,mask,movementAccessFromLegacy(swim),x,y,cells,&result.dx,&result.dy,false);
    if(!result.moved && key.family==GradientFieldFamily::Markets && gradientGoalValid(key,x,y)) {
        result.atGoal=result.goalValid=true;result.travelRequired=false;
    }
    return result;
}

bool MapRouting::pathfindForbidden(const Uint16* option,int team,MovementAccess movementAccessValue,int x,int y,int* dx,int* dy)
{
    int swim=accessClass(movementAccessValue);

    PERF_SCOPE_TIME(PathArea);
    const GradientFieldKey key{GradientFieldFamily::Forbidden,Uint8(team),Uint8(swim)};
    gradientMovementDemand(key);
    const auto* cells=getForbiddenGradient(team,movementAccessValue);
    if(!cells)return false;
    GradientQueryContext context{key,x,y,EntityRandom{},option,GradientQueryContext::Mode::ForbiddenEscape};
    const auto result=selectGradientQuery(context,cells);
    gradientMovementOutcome(context,cells,result);
    if(result.atGoal&&!result.goalValid)updateForbiddenGradient(team,movementAccessValue);
    if(result.moved){*dx=result.dx;*dy=result.dy;return true;}
    if(result.travelRequired && cells[map.coordToIndex(x,y)]>GRADIENT_UNREACHABLE)updateForbiddenGradient(team,movementAccessValue);
    return false;
}
bool MapRouting::pathfindArea(EntityRandom& random,AreaKind kind,int team,MovementAccess movementAccessValue,int x,int y,int* dx,int* dy)
{
    int swim=accessClass(movementAccessValue);

    PERF_SCOPE_TIME(PathArea);
    if (kind==AreaKind::Guard ? !map.paintedAreaTileCount(GuardArea,team) : !map.hasClearingArea(team)) return false;
    const GradientFieldKey key{kind==AreaKind::Guard?GradientFieldFamily::Guard:GradientFieldFamily::Clear,Uint8(team),Uint8(swim)};
    gradientMovementDemand(key);
    const auto* cells=kind==AreaKind::Guard?getGuardAreasGradient(team,movementAccessValue):getClearAreasGradient(team,movementAccessValue);
    if(!cells)return false;
    const GradientQueryContext before{key,x,y,random};
    auto context=before;
    const auto result=selectGradientQueryImpl(context,cells);
    random=context.random;
    gradientMovementOutcome(before,cells,result);
    if(result.moved){*dx=result.dx;*dy=result.dy;return true;}
    if((result.atGoal&&!result.goalValid)||(result.travelRequired&&cells[map.coordToIndex(x,y)]>GRADIENT_UNREACHABLE&&!result.atGoal)) {
        if(kind==AreaKind::Guard)updateGuardAreasGradient(team,movementAccessValue);else updateClearAreasGradient(team,movementAccessValue);
    }
    return false;
}
StepResult MapRouting::queryForbiddenStep(const Uint16* option,int team,MovementAccess movementAccessValue,int x,int y,int* dx,int* dy)
{
    int swim=accessClass(movementAccessValue);

    if(!getForbiddenGradient(team,movementAccessValue)) {
        gradientMovementDemand({GradientFieldFamily::Forbidden,Uint8(team),Uint8(swim)});
        return StepResult::Pending;
    }
    return pathfindForbidden(option,team,movementAccessValue,x,y,dx,dy)?StepResult::Step:StepResult::Blocked;
}
StepResult MapRouting::queryAreaStep(EntityRandom& random,AreaKind kind,int team,MovementAccess movementAccessValue,int x,int y,int* dx,int* dy)
{
    int swim=accessClass(movementAccessValue);

    if (kind==AreaKind::Guard ? !map.paintedAreaTileCount(GuardArea,team) : !map.hasClearingArea(team)) return StepResult::Blocked;
    const auto* cells=kind==AreaKind::Guard?getGuardAreasGradient(team,movementAccessValue):getClearAreasGradient(team,movementAccessValue);
    if(!cells) {
        gradientMovementDemand({kind==AreaKind::Guard?GradientFieldFamily::Guard:GradientFieldFamily::Clear,Uint8(team),Uint8(swim)});
        return StepResult::Pending;
    }
    const bool arrival=cells[map.coordToIndex(x,y)]==GRADIENT_AT_GOAL;
    const bool moved=pathfindArea(random,kind,team,movementAccessValue,x,y,dx,dy);
    return arrival?StepResult::AtGoal:moved?StepResult::Step:StepResult::Blocked;
}
