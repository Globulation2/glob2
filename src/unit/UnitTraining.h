// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingCatalog.h"
#include "UnitCatalog.h"

// Cold service planning shared by live units and captured AI observations.
// Capture the whole parallel grant set before any qualification/level changes.
namespace UnitTraining
{
template<class State> bool needed(const State& unit,const BuildingTrainingSpec& training,int ability)
{
    return training.enabled && unit.canLearn[ability] && training.units.matches(unit.typeNum,training.unitMask)
        && (unit.level[ability]<training.targetLevel
            || ((unit.capabilityFlags&UnitRuntimeTraits::LearnConstruction) && unit.constructionLevel<training.constructionLevel));
}
template<class State> Uint32 courses(const State& unit,const BuildingSemantics& spec,int purpose)
{
    Uint32 result=0;
    if(spec.trainingInParallel) {
        for(int ability=WALK;ability<NB_ABILITY;++ability)
            if(needed(unit,spec.training[ability],ability))result|=1u<<ability;
    } else if(needed(unit,spec.training[purpose],purpose))result=1u<<purpose;
    return result;
}
// Admission only needs locomotion grants; full bundle capture is deferred to
// completion. Ordinary single-ability services avoid a parallel course scan.
template<class State> Uint32 movementCourses(const State& unit,const BuildingSemantics& spec,int purpose)
{
    Uint32 result=0;
    if(spec.trainingInParallel) {
        for(int ability=WALK;ability<=FLY;++ability)
            if(needed(unit,spec.training[ability],ability))result|=1u<<ability;
    } else if(purpose>=WALK && purpose<=FLY && needed(unit,spec.training[purpose],purpose))result=1u<<purpose;
    return result;
}
template<class State> unsigned movementModes(const State& unit)
{
    return (unit.performance[WALK]>0?1u:0u)|(unit.performance[SWIM]>0?2u:0u)|(unit.performance[FLY]>0?4u:0u);
}
template<class State,class Levels,class Training> unsigned movementAfter(
    const State& unit,Uint32 mask,const Training& training,const Levels& levels)
{
    unsigned result=0;
    constexpr UnitRuntimeTraits::Flag gates[]={UnitRuntimeTraits::Walk,UnitRuntimeTraits::Swim,UnitRuntimeTraits::Fly};
    for(int index=0;index<3;++index) {
        const int ability=WALK+index;
        const int target=(mask&(1u<<ability)) && unit.level[ability]<training[ability].targetLevel
            ? levels[training[ability].targetLevel].performance[ability] : unit.performance[ability];
        if((unit.capabilityFlags&gates[index]) && target>0)result|=1u<<index;
    }
    return result;
}
// Occupancy, resources and forbidden areas can clear while an eater/trainer
// exits. Only immutable terrain compatibility can reject a prospective course.
template<class Terrain> bool exitTerrainSafe(unsigned before,unsigned after,
    int x,int y,int width,int height,Terrain terrain)
{
    if(!after)return false;
    // Flying units remain airborne; their exit is inside the footprint. Losing
    // a dormant ground mode cannot make that existing air exit less usable.
    if((before&4u) && (after&4u))return true;
    if((after&before)==before)return true;
    for(int dy=-1;dy<=height;++dy)for(int dx=-1;dx<=width;++dx) {
        if(dx!=-1 && dx!=width && dy!=-1 && dy!=height)continue;
        const auto& rules=terrain(x+dx,y+dy);
        if(after&4u ? rules.flyable : ((after&1u) && rules.walkable) || ((after&2u) && rules.swimmable))return true;
    }
    return false;
}
}
