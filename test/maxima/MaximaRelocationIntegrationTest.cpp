// Link with the game objects (excluding Glob2.cpp) to exercise the real runtime.
#include "../../src/GlobalContainer.h"
#include "../../src/Game.h"
#include "../../src/team/Team.h"
#include "../../src/ai/AIImplementation.h"
#include "../../src/map/Map.h"
#include "../../src/Order.h"
#include "../../src/Player.h"
#include "../../src/Version.h"
#include "../../src/TeamStat.h"
#include <memory>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <vector>
// Access the scheduler boundary without adding a production testing API.
#define private public
#include "../../src/ai/maxima/AIMaximaRuntime.h"
#include "../../src/ai/maxima/AIMaxima.h"
#undef private
#include "../../src/building/Building.h"
#include "../../src/game/entities/BuildingType.h"
#include "../../src/building/IntBuildingType.h"
#include "../../src/unit/Unit.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <cassert>
#include <iostream>

GlobalContainer* globalContainer = NULL;
using namespace AIMaximaRuntime;


using namespace AIMaximaPlacement;

namespace
{
struct Fixture
{
    Game game{nullptr};
    std::unique_ptr<AIMaxima::Maxima> ai;
    std::vector<Building*> buildings;
    Fixture(int count=2, const char* kind="swarm")
    {
        game.map.setSize(6,6,GRASS); game.map.setGame(&game);
        game.addTeam(); game.teams[0]->race.loadDefault();
        game.players[0]=new Player; game.players[0]->setTeam(game.teams[0]);
        game.gameHeader.setNumberOfPlayers(1);
        const int type=globalContainer->buildingsTypes.getTypeNum(kind,0,false);
        for(int i=0;i<count;++i)
        {
            buildings.push_back(game.addBuilding(10+i*16,10+i*16,type,0));
            assert(buildings.back());
        }
        ai=std::make_unique<AIMaxima::Maxima>(game.players[0]);
        ai->ensure_strategy();
        auto& c=ai->context; c.initialize();
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)
            game.map.setMapDiscovered(x,y,game.teams[0]->me);
        ai->initialize_farming_cache(c); ai->configure_development_planner();
        ai->budget.food_ledger_enabled=true;
        ai->budget.food_relocation_enabled=true;
        ai->budget.food_retirement_enabled=true;
        ai->budget.recovery_active=false;
        ai->snapshot.critical_food=ai->snapshot.own_buildings_under_attack=0;
        ai->snapshot.own_units_under_attack=ai->snapshot.population=0;
        ai->timer=7000; ai->relocation_target_building=0;
        ai->relocation_target_since=3000;
        for(int i=0;i<count;++i)ai->food_burden_since[i]=1000;
        DevelopmentAction action; action.id=77; action.type=BuildStandalone;
        action.purpose=Relocation; action.replacesBuildingId=0; action.buildingId=1;
        action.buildingType=buildings[0]->type->shortTypeNum;
        action.issuedTick=3000; action.state=Completed;
        ai->development_planner.actionMap[action.id]=action;
    }
    WorldState world() { return ai->collect_development_world(ai->context); }
    std::set<int> deletions() const
    {
        std::set<int> result;
        for(const auto& order:ai->context.managementOrders)
            if(auto* d=dynamic_cast<Management::DestroyBuilding*>(order.get()))result.insert(d->id);
        return result;
    }
    void retireAndRelocate()
    {
        auto w=world();
        ai->update_food_relocation(ai->context,w);
        ai->update_food_retirement(ai->context,w);
    }
    bool offersReplacement()
    {
        for(const auto& intent:ai->collect_development_intents(world()))
            if(intent.purpose==Relocation && intent.replacesBuildingId==0)return true;
        return false;
    }
    void roundTrip()
    {
        auto* memory=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(memory);
        ai->save(&output);
        const std::string bytes(memory->getBuffer(),memory->getPosition());
        auto* saved=new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size());
        saved->seekFromStart(0);
        GAGCore::BinaryInputStream input(saved);
        GAGCore::BinaryInputStream::CheckedReads checked(&input);
        auto restored=std::make_unique<AIMaxima::Maxima>(&input,game.players[0],VERSION_MINOR);
        ai=std::move(restored);
    }
};

void deletionSafety()
{
    // The replacement is burdened (e.g. wheat was lost during construction).
    // Both real policies run against exactly the same pre-order snapshot.
    for(bool restored:{false,true})for(int count:{2,3})
    {
        Fixture f(count);
        auto w=f.world();
        f.ai->update_food_relocation(f.ai->context,w);
        if(restored)f.roundTrip();
        f.ai->update_food_retirement(f.ai->context,w);
        const std::set<int> expected=count==2?std::set<int>{0}:std::set<int>{0,2};
        if(f.deletions()!=expected)
        {
            std::cerr<<"restored="<<restored<<" count="<<count<<" deletes=";
            for(int id:f.deletions())std::cerr<<id<<",";
            std::cerr<<" target="<<f.ai->relocation_target_building
                <<" burden="<<f.ai->food_burden_since.size()<<"\n";
        }
        assert(f.deletions()==expected);
        f.ai->context.update_management_orders();
        for(auto& order:f.ai->context.orders)
            if(order->getOrderType()==ORDER_DELETE)
            { order->sender=0; f.game.executeOrder(order,0); }
        assert(f.buildings[0]->buildingState==Building::WAITING_FOR_DESTRUCTION);
        assert(f.buildings[1]->buildingState==Building::ALIVE);
        // Still present in the register during destruction: it is not spare capacity.
        f.ai->context.managementOrders.clear();
        f.ai->timer+=f.ai->budget.food_retirement_cooldown_ticks;
        f.retireAndRelocate(); assert(f.deletions().empty());
    }
    // Retirement already claimed the replacement, or a deletion is in the engine.
    for(bool issued:{false,true})
    {
        Fixture f;
        if(issued)f.ai->food_retirement_issued.insert(1);
        else f.buildings[1]->launchDelete();
        f.retireAndRelocate();
        assert(f.deletions().empty());
        assert(f.ai->relocation_target_building==-1);
        assert(f.buildings[0]->buildingState==Building::ALIVE);
    }
    // Inn-seat safety must also subtract other pending deletions.
    Fixture inns(3,"inn"); inns.ai->relocation_target_building=-1;
    inns.ai->snapshot.population=30; inns.buildings[2]->launchDelete();
    inns.ai->update_food_retirement(inns.ai->context,inns.world());
    assert(inns.deletions().empty());
}

void lostReplacement()
{
    for(bool restored:{false,true})
    {
        Fixture f;
        // Completion was observed while hunger deferred removal of the old swarm.
        f.ai->snapshot.critical_food=1;
        f.ai->update_food_relocation(f.ai->context,f.world());
        assert(f.ai->relocation_completed_tick==7000);
        if(restored)f.roundTrip();
        f.buildings[1]->kill(); f.game.teams[0]->syncStep(); f.ai->context.buildings.tick();
        f.ai->snapshot.critical_food=0; f.ai->timer=8000;
        auto w=f.world(); f.ai->development_planner.observe(w);
        f.ai->update_food_relocation(f.ai->context,w);
        assert(f.ai->relocation_target_building==-1);
        assert(f.ai->last_food_relocation_tick==8000);
        assert(f.deletions().empty());
        if(restored)f.roundTrip();
        // The completed historical action must not suppress a fresh offer, or make
        // its next observation immediately fail again.
        f.ai->timer=14000; f.ai->update_food_relocation(f.ai->context,f.world());
        assert(f.ai->relocation_target_building==0 && f.offersReplacement());
        f.ai->timer=14001; f.ai->update_food_relocation(f.ai->context,f.world());
        assert(f.ai->relocation_target_building==0 && f.offersReplacement());
    }
}

void deferredRetry()
{
    Fixture f; f.ai->snapshot.critical_food=1;
    f.ai->update_food_relocation(f.ai->context,f.world());
    f.roundTrip();
    f.ai->timer=13000; f.ai->update_food_relocation(f.ai->context,f.world());
    assert(f.ai->relocation_target_building==-1 && f.deletions().empty());
    f.roundTrip();
    f.ai->snapshot.critical_food=0; f.ai->timer=19000;
    f.ai->update_food_relocation(f.ai->context,f.world());
    assert(f.ai->relocation_target_building==0 && f.offersReplacement());
    f.ai->update_food_relocation(f.ai->context,f.world());
    assert(f.deletions().empty());
}

void savedRelationships()
{
    for(auto state:{ParcelReserved,CreateIssued,SiteObserved,Completed,
                   InvalidatedBeforeIssue,CreateTimedOut,DestroyedDuringConstruction})
    {
        Fixture f;
        auto& action=f.ai->development_planner.actionMap.at(77);
        f.ai->environment.accessible_corn_fraction=17699;
        action.state=state;
        if(state==ParcelReserved||state==InvalidatedBeforeIssue)action.issuedTick=-1;
        // Detached history must remain detached even when it lacks an issue time.
        auto old=action;old.id=76;old.replacesBuildingId=-1;
        f.ai->development_planner.actionMap[76]=old;
        f.roundTrip();
        assert(f.ai->environment.accessible_corn_fraction==17699);
        assert(f.ai->development_planner.actionMap.at(77).replacesBuildingId==0);
        assert(f.ai->development_planner.actionMap.at(76).replacesBuildingId==-1);
        assert(f.ai->current_food_relocation()->id==77);
    }

}
}

int main(int argc,char** argv)
{
    GlobalContainer container; globalContainer=&container;
    container.runNoX=true; container.buildingsTypes.init(); IntBuildingType::init();
    const std::string selected=argc>1?argv[1]:"all";
    if(selected=="all"||selected=="deletion")deletionSafety();
    if(selected=="all"||selected=="recovery")lostReplacement();
    if(selected=="all"||selected=="retry")deferredRetry();
    if(selected=="all"||selected=="save")savedRelationships();
    std::cout << "Maxima relocation " << selected << " regressions passed\n";
}
