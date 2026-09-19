// Raw worker diagnostics must classify time once and never change simulation state.
#include "GlobalContainer.h"
#include "Game.h"
#include "team/Team.h"
#include "unit/Unit.h"
#include "building/Building.h"
#include "building/IntBuildingType.h"
#include "TeamStat.h"
#include <cassert>
#include <cstdlib>
#include <iostream>

GlobalContainer* globalContainer=nullptr;
int main(int argc,char** argv)
{
    const bool enabled=argc<2 || std::string(argv[1])!="--disabled";
    if(enabled) SDL_setenv("GLOB2_TEAM_TIMELINE","1",1);
    GlobalContainer globals;globalContainer=&globals;globals.runNoX=true;
    globals.buildingsTypes.init();IntBuildingType::init();
    Game game(nullptr);game.map.setSize(6,6,GRASS);game.map.setGame(&game);
    game.addTeam();game.teams[0]->race.loadDefault();
    auto* worker=game.addUnit(12,12,0,WORKER,0,0,0,0);
    auto* swarm=game.addBuilding(8,8,globals.buildingsTypes.getTypeNum("swarm",0,false),0);
    assert(worker && swarm);
    auto& stats=game.teams[0]->stats;
    const auto observe=[&](int bucket) {
        const Uint32 before=game.checkSum();
        stats.observeLabour(worker);
        assert(game.checkSum()==before);
        assert(stats.labourTicks[bucket]==(enabled ? 1u : 0u));
    };
    worker->medical=Unit::MED_FREE;worker->activity=Unit::ACT_RANDOM;
    observe(0);
    worker->medical=Unit::MED_HUNGRY;worker->targetBuilding=nullptr;
    worker->displacement=Unit::DIS_GOING_TO_BUILDING;observe(3);
    worker->targetBuilding=swarm;observe(1);
    worker->displacement=Unit::DIS_INSIDE;observe(2);
    worker->medical=Unit::MED_DAMAGED;worker->targetBuilding=nullptr;
    worker->displacement=Unit::DIS_GOING_TO_BUILDING;observe(6);
    worker->medical=Unit::MED_FREE;worker->activity=Unit::ACT_UPGRADING;
    worker->destinationPurpose=WALK;observe(7);
    worker->displacement=Unit::DIS_INSIDE;observe(8);
    worker->activity=Unit::ACT_FILLING;worker->attachedBuilding=swarm;
    worker->displacement=Unit::DIS_HARVESTING;observe(10);
    Uint64 sum=0;for(int i=0;i<27;++i)sum+=stats.labourTicks[i];
    assert(sum==stats.labourTicks[27] && sum==(enabled ? 8u : 0u));
    const Uint32 before=game.checkSum();
    stats.recordCombatDeath(worker);
    assert(game.checkSum()==before);
    assert(stats.combatDeathPlace[WORKER][0]==(enabled ? 1u : 0u));
    assert(stats.combatDeathJob[WORKER][4]==(enabled ? 1u : 0u));
    // No actual engine assignment was made by this classification fixture.
    worker->attachedBuilding=nullptr;worker->targetBuilding=nullptr;
    std::cout<<"Worker telemetry "<<(enabled ? "enabled" : "disabled")
        <<": buckets, totals, combat attribution and unchanged checksums PASS\n";
}
