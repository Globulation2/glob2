// Link with the game objects (excluding Glob2.cpp) to exercise the real runtime.
#include "/Users/bradley/glob2/src/GlobalContainer.h"
#include "/Users/bradley/glob2/src/Game.h"
#include "/Users/bradley/glob2/src/team/Team.h"
#include "/Users/bradley/glob2/src/ai/AIImplementation.h"
#include "/Users/bradley/glob2/src/map/Map.h"
#include "/Users/bradley/glob2/src/Order.h"
#include "/Users/bradley/glob2/src/Player.h"
#include "/Users/bradley/glob2/src/Version.h"
#include "/Users/bradley/glob2/src/TeamStat.h"
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
#include "/Users/bradley/glob2/src/AIMaximaRuntime.h"
#include "/Users/bradley/glob2/src/AIMaxima.h"
#undef private
#include "/Users/bradley/glob2/src/building/Building.h"
#include "/Users/bradley/glob2/src/game/entities/BuildingType.h"
#include "/Users/bradley/glob2/src/building/IntBuildingType.h"
#include "/Users/bradley/glob2/src/unit/Unit.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <cassert>
#include <iostream>

GlobalContainer* globalContainer = NULL;
using namespace AIMaximaRuntime;


using namespace AIMaximaPlacement;


#include "Engine.h"
#include "AIMaximaFoodSupply.h"
int main(int argc,char** argv) {
 GlobalContainer globals("glob2-maxima-inspect");globalContainer=&globals;globals.runNoX=true;globals.load();
 GameGUI gui;GAGCore::BinaryInputStream input(new GAGCore::FileStreamBackend(fopen(argv[1],"rb")));
 assert(gui.load(&input));auto& game=gui.game;
 std::cout<<"INSPECT tick="<<game.stepCounter<<" teams="<<game.mapHeader.getNumberOfTeams()<<" size="<<game.map.getW()<<"x"<<game.map.getH()<<"\n";
 for(int p=0;p<game.gameHeader.getNumberOfPlayers();++p) {
  auto* ai=game.players[p]->ai?dynamic_cast<AIMaxima::Maxima*>(game.players[p]->ai->aiImplementation):nullptr;if(!ai)continue;
  std::cout<<"INSPECT player="<<p<<" timer="<<ai->timer<<" population="<<ai->snapshot.population<<" workers="<<ai->snapshot.workers<<" accessible_corn="<<ai->environment.accessible_corn<<" birth_budget="<<ai->budget.swarm_workers<<" wheat_radius="<<ai->budget.swarm_supply_radius<<"\n";
  ai->plan_offense(ai->context);
  auto flag=ai->tactical_mission.flagId>=0 ? ai->context.buildings.get_building(ai->tactical_mission.flagId) : nullptr;
  int eligible=0,trainable=0;
  for(int id=0;id<Unit::MAX_COUNT;++id) {
    auto u=game.players[p]->team->myUnits[id];
    if(!u || u->typeNum!=WARRIOR || u->isDead || u->medical!=Unit::MED_FREE
      || std::min(u->level[ATTACK_SPEED],u->level[ATTACK_STRENGTH])<ai->strategy.tactics.flag_minimum_level-1)continue;
    if(!(flag && u->attachedBuilding==flag) && (u->attachedBuilding || u->activity!=Unit::ACT_RANDOM || u->movement==Unit::MOV_ATTACKING_TARGET))continue;
    ++eligible;bool learns=false;
    for(int bid=0;bid<Building::MAX_COUNT;++bid) {
      auto b=game.players[p]->team->myBuildings[bid];
      if(!b || b->type->shortTypeNum!=IntBuildingType::ATTACK_BUILDING || b->type->isBuildingSite || b->maxUnitInside<=int(b->unitsInside.size()))continue;
      for(int ability=WALK;ability<ARMOR;++ability)
        if(u->canLearn[ability] && b->type->upgrade[ability] && u->level[ability]<=b->type->level)learns=true;
    }
    if(learns)++trainable;
  }
  std::cout<<"COMBAT player="<<p<<" eligible="<<eligible<<" can_use_open_barracks="<<trainable
   <<" subtracted_slots="<<ai->offense_diagnostics.openTrainingSlots<<" requested="<<ai->budget.tactical_requested_force
   <<" gate="<<ai->offense_diagnostics.gate<<"\n";
  if(eligible>=4 && trainable==0 && ai->offense_diagnostics.openTrainingSlots>0) {
   std::vector<std::pair<Building*,int>> capacities;
   for(int i=0;i<Building::MAX_COUNT;++i) {
    auto b=game.players[p]->team->myBuildings[i];
    if(b && b->type->shortTypeNum==IntBuildingType::ATTACK_BUILDING) {
      capacities.push_back({b,b->maxUnitInside});b->maxUnitInside=b->unitsInside.size();
    }
   }
   ai->plan_offense(ai->context);
   std::cout<<"COUNTERFACTUAL_NO_UNUSABLE_SLOTS player="<<p<<" requested="<<ai->budget.tactical_requested_force
    <<" kind="<<ai->budget.tactical_kind<<" gate="<<ai->offense_diagnostics.gate<<"\n";
   for(auto entry:capacities)entry.first->maxUnitInside=entry.second;
  }
  for(int i=0;i<Building::MAX_COUNT;++i) {
   auto* b=game.players[p]->team->myBuildings[i];if(!b||(b->type->shortTypeNum!=IntBuildingType::SWARM_BUILDING && b->type->shortTypeNum!=IntBuildingType::FOOD_BUILDING))continue;
   std::cout<<"SWARM player="<<p<<" type="<<b->type->shortTypeNum<<" gid="<<b->gid<<" pos="<<b->posX<<","<<b->posY<<" site="<<b->type->isBuildingSite<<" wheat="<<b->resources[WHEAT]<<" production_timeout="<<b->productionTimeout<<" ratios="<<b->ratio[0]<<","<<b->ratio[1]<<","<<b->ratio[2]<<"\n";
   for(int radius:{12,24,48})std::cout<<"CAPACITY player="<<p<<" radius="<<radius<<" raw="<<AIMaxima::reachableFoodCapacity(&game.map,b,game.players[p]->team->me,ai->budget.can_swim,radius,ai->fertility_cache,&ai->applied_farm_protection_mask,nullptr)<<"\n";
  }
 }
}
