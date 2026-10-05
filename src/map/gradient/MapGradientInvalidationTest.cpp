// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include <algorithm>
#include <iostream>
#include "GlobalContainer.h"
#include "Game.h"
#include "Map.h"
#include "Building.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "Player.h"
#include "Order.h"
#include "Brush.h"
#include <PerformanceTelemetry.h>
#include <memory>
#include <vector>

namespace
{
// SDL compiler flags may rename main even when SDL_MAIN_HANDLED is set.
struct Fixture {
 Game game;
 explicit Fixture(int lastTeam): game(nullptr) {
  game.map.setSize(6,6,GRASS); game.map.setGame(&game);
  for(int i=0;i<=lastTeam;++i) { game.addTeam(); game.teams[i]->race.loadDefault(); }
 }
};
static auto propagations() {
 return PerformanceTelemetry::collector().calls[static_cast<unsigned>(PerformanceTelemetry::Id::Propagation)];
}
void edit(Fixture& f,int x,int y,bool add,bool mixed=false) {
 auto o=std::make_shared<OrderAlterForbidden>();o->sender=0;o->teamNumber=0;o->type=add?BrushTool::MODE_ADD:BrushTool::MODE_DEL;
 o->centerX=x;o->centerY=y;o->minX=o->minY=0;o->maxX=mixed?2:1;o->maxY=1;o->mask.resize(mixed?2:1,true);
 f.game.executeOrder(o,0);
}
void player(Fixture& f){f.game.players[0]=new Player();f.game.players[0]->setTeam(f.game.teams[0]);f.game.gameHeader.setNumberOfPlayers(1);}
}

TEST_SUITE("MapGradientInvalidation")
{
	TEST_CASE("forbidden edits invalidate exactly the affected fields and preserve the rest")
	{
		glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings = true});
	 int checks=0;
	 for(int swim=0;swim<SWIM_CLASS_COUNT;++swim)for(int resource:{WHEAT,WOOD,NO_RES_TYPE})for(bool selected:{false,true})for(bool mixed:{false,true})for(bool add:{false,true}) {
	  Fixture f(1);player(f);auto& m=f.game.map;int n=m.getW()*m.getH();std::vector<Building*> bs;
	  for(int team=0;team<2;++team)for(const char* kind:{"inn","warflag","explorationflag","clearingflag"}){
	   int x=team?42:8,y=8+int(bs.size()%4)*10;auto* b=f.game.addBuilding(x,y,globals->buildingsTypes.getTypeNum(kind,0,false),team);REQUIRE(b);b->unitStayRange=32;b->clearingResources[WHEAT]=b->clearingResources[WOOD]=true;bs.push_back(b);
	  }
	  if(resource!=NO_RES_TYPE)m.setResource(20,20,resource,1);
	  m.setResource(25,24,WHEAT,1);m.addGuardArea(26,26,0);if(selected)m.addClearArea(20,20,0);
	  if(!add){m.addForbidden(20,20,0);if(mixed)m.addForbidden(21,20,0);}
	  for(auto* b:bs){m.buildingGradient(b,swim);m.updateGlobalGradient(b,swim);m.finishBuildingGradient(b,swim);}
	  m.getForbiddenGradient(0,swim);m.getGuardAreasGradient(0,swim);m.getClearAreasGradient(0,swim);
	  edit(f,20,20,add,mixed);
	  for(auto* b:bs){auto* g=m.buildingGradient(b,swim);m.finishBuildingGradient(b,swim);std::vector<Uint16> before(g,g+n);m.updateGlobalGradient(b,swim);m.finishBuildingGradient(b,swim);REQUIRE(std::equal(before.begin(),before.end(),b->globalGradient[swim]));++checks;}
	  auto area=[&](auto get,auto update){const Uint16* g=(m.*get)(0,swim);std::vector<Uint16> before(g,g+n);(m.*update)(0,swim);REQUIRE(std::equal(before.begin(),before.end(),(m.*get)(0,swim)));++checks;};
	  area(&Map::getForbiddenGradient,static_cast<void(Map::*)(int,int)>(&Map::updateForbiddenGradient));
	  area(&Map::getGuardAreasGradient,static_cast<void(Map::*)(int,int)>(&Map::updateGuardAreasGradient));
	  area(&Map::getClearAreasGradient,static_cast<void(Map::*)(int,int)>(&Map::updateClearAreasGradient));
	 }
	 // Already-stale walking fields must not be blessed current by an unrelated order.
	 {Fixture f(1);player(f);auto& m=f.game.map;auto* b=f.game.addBuilding(8,8,globals->buildingsTypes.getTypeNum("inn",0,false),0);m.setResource(20,20,WHEAT,1);m.buildingGradient(b,0);auto old=b->gradientGeneration[0];m.addForbidden(30,30,1);REQUIRE(old!=m.topologyGeneration);edit(f,20,20,true);REQUIRE(b->gradientGeneration[0]==old);}
	 // Irrelevant orders preserve a paused search, its frozen field and dirty status.
	 {Fixture f(1);player(f);auto& m=f.game.map;
	  auto* b=f.game.addBuilding(8,8,globals->buildingsTypes.getTypeNum("inn",0,false),0);
	  m.setResource(20,20,WHEAT,1);m.buildingGradient(b,0);m.updateGlobalGradient(b,0);
	  auto* search=b->globalGradientSearch[0].get();REQUIRE(search);
	  auto* field=b->globalGradient[0];std::vector<Uint16> snapshot(field,field+4096);
	  const bool dirty=b->dirtyGradient[0];edit(f,20,20,true);
	  REQUIRE((b->globalGradientSearch[0].get()==search && b->globalGradient[0]==field));
	  REQUIRE((b->dirtyGradient[0]==dirty && std::equal(snapshot.begin(),snapshot.end(),field)));
	  const auto generation=m.topologyGeneration;edit(f,20,20,true);
	  REQUIRE((m.topologyGeneration==generation && b->globalGradientSearch[0].get()==search));
	 }
	 // Record a pre-existing stale escape field that an incidental forbidden edit used to refresh.
	 {Fixture f(1);player(f);auto& m=f.game.map;f.game.gameHeader.setResourceGrowthDisabled(true);m.setResource(20,20,WHEAT,1);m.setResource(21,20,WHEAT,1);m.addForbidden(20,20,0);m.addForbidden(21,20,0);m.getForbiddenGradient(0,0);m.replaceResource(21,20, Resource{});edit(f,20,20,false);auto* g=m.getForbiddenGradient(0,0);auto stale=g[m.coordToIndex(21,20)];const int bound=8*f.game.mapHeader.getNumberOfTeams()*SWIM_CLASS_COUNT; for(int i=0;i<=bound;++i)m.syncStep(i); auto fresh=g[m.coordToIndex(21,20)];std::cout<<"ESCAPE_STALENESS stale="<<stale<<" fresh="<<fresh<<"\n";REQUIRE(stale!=fresh);}
	 // A worker in the middle of a ring cannot use a freshly depleted exit until refresh.
	 {Fixture f(1);player(f);auto& m=f.game.map;m.addForbidden(20,20,0);
	 for(int y=19;y<=21;++y)for(int x=19;x<=21;++x)if(x!=20||y!=20)m.setResource(x,y,WHEAT,1);
	 m.setResource(25,25,WHEAT,1);m.getForbiddenGradient(0,0);m.replaceResource(21,20, Resource{});m.setResourcesGrow(21,20, false);edit(f,25,25,true);
	 int dx=0,dy=0;bool before=m.pathfindForbidden(nullptr,0,0,20,20,&dx,&dy);REQUIRE(!before);
	 const int bound=8*f.game.mapHeader.getNumberOfTeams()*SWIM_CLASS_COUNT;
	 for(int tick=1;tick<=bound;++tick)m.syncStep(tick);
	 bool after=m.pathfindForbidden(nullptr,0,0,20,20,&dx,&dy);REQUIRE(after);REQUIRE((dx==1&&dy==0));
	 std::cout<<"ESCAPE_RECOVERY before="<<before<<" after="<<after<<" within_ticks="<<bound<<" direction="<<dx<<","<<dy<<" PASS\n";
	 }
	 // A scheduled pass must rebuild exactly when land seed classification changes.
	 int transitions=0;
	 for(int mutation=0;mutation<12;++mutation)for(int swim=0;swim<SWIM_CLASS_COUNT;++swim){
	  Fixture f(1);player(f);f.game.gameHeader.setResourceGrowthDisabled(true);auto& m=f.game.map;
	  for(int y=16;y<25;++y)for(int x=16;x<25;++x)m.addForbidden(x,y,0);
	  m.setResource(20,20,WHEAT,1);m.setResourceAmount(m.coordToIndex(20,20), 3);
	  if(mutation==4)m.markImmobileUnit(21,20,0);
	  if(mutation==6)m.setBuilding(21,20,1,1,42);
	  if(mutation==8)m.setTerrain(21,20,256);
	  auto* g=m.getForbiddenGradient(0,swim);std::vector<Uint16> old(g,g+4096);
	  switch(mutation){
	   case 0:m.setResourceAmount(m.coordToIndex(20,20), 1);m.decResource(20,20);break;
	   case 1:m.incResource(21,20,WHEAT,0);break;
	   case 2:m.decResource(20,20);break; // quantity3->2 leaves blocking unchanged
	   case 3:m.markImmobileUnit(21,20,0);break;
	   case 4:m.clearImmobileUnit(21,20);break;
	   case 5:m.setBuilding(21,20,1,1,42);break;
	   case 6:m.setBuilding(21,20,1,1,NOGBID);break;
	   case 7:m.setTerrain(21,20,256);break;
	   case 8:m.setTerrain(21,20,0);break;
	   case 9:edit(f,20,20,true);break; // no-op forbidden brush
	   case 10:edit(f,20,20,false);break; // resource-only mask edit
	   case 11:{auto resource=m.getResource(20,20);resource.type=WOOD;m.replaceResource(20,20,resource);break;}
	  }
	  auto rebuilds=propagations();
	  m.syncStep(swim*8); // exact scheduled team0 slot
	  auto rebuildCount=propagations()-rebuilds;
	  std::vector<Uint16> checked(g,g+4096);m.updateForbiddenGradient(0,swim);
	  REQUIRE(std::equal(checked.begin(),checked.end(),g));
	  bool unchanged=mutation==2||mutation==7||mutation==8||mutation==9||mutation==10||mutation==11
	   || (swim==Map::SWIM_CLASS_EVEN && (mutation==7||mutation==8));
	  bool uniform=swim==0||swim==Map::SWIM_CLASS_EVEN;
	  REQUIRE(rebuildCount==(!uniform||!unchanged?1:0));
	  ++transitions;
	 }
	 std::cout<<"CHECKED_TRANSITIONS "<<transitions<<" PASS (quantity/no-op/resource-type changes skip land rebuilds)\n";
	 {Fixture f(3);player(f);f.game.gameHeader.setResourceGrowthDisabled(true);auto& m=f.game.map;
	  for(int y=17;y<=23;++y)for(int x=17;x<=23;++x){
	   for(int team=0;team<4;++team)m.addForbidden(x,y,team);
	   if(x==17||x==23||y==17||y==23)m.setResource(x,y,WHEAT,1);
	  }
	  for(int team=0;team<4;++team)for(int swim=0;swim<SWIM_CLASS_COUNT;++swim)m.getForbiddenGradient(team,swim);
	  auto before=propagations();int lookups=0;
	  for(int iteration=0;iteration<512;++iteration)for(int team=0;team<4;++team)for(int swim=0;swim<SWIM_CLASS_COUNT;++swim){int dx=0,dy=0;REQUIRE(!m.pathfindForbidden(nullptr,team,swim,20,20,&dx,&dy));++lookups;}
	  REQUIRE(propagations()==before);
	  std::cout<<"UNREACHABLE_POCKET lookups="<<lookups<<" propagation_rebuilds="<<propagations()-before<<" PASS\n";
	 }
	 {Fixture f(3);player(f);f.game.gameHeader.setResourceGrowthDisabled(true);auto& m=f.game.map;
	  const int slots=4*SWIM_CLASS_COUNT,cycle=8*slots;std::vector<Uint16*> fields;
	  for(int team=0;team<4;++team)for(int swim=0;swim<SWIM_CLASS_COUNT;++swim)fields.push_back(m.getForbiddenGradient(team,swim));
	  // Deliberately dirty one marker after each visit to make every slot observable.
	  // The square is genuinely empty/non-forbidden, so the refreshed value must be a goal.
	  int pos=m.coordToIndex(10,10);for(auto* field:fields)field[pos]=0;
	  const unsigned long long start=(1ULL<<32)-2*cycle;
	  std::vector<long long> last(slots,-1);std::vector<int> visits(slots,0);int maxGap=0,totalBuilds=0;
	  for(int offset=0;offset<4*cycle;++offset){
	   auto before=propagations();m.syncStep(Uint32(start+offset));int changed=0;
	   for(int slot=0;slot<slots;++slot)if(fields[slot][pos]==65535){
	    ++changed;++visits[slot];if(last[slot]>=0){int gap=offset-last[slot];maxGap=std::max(maxGap,gap);REQUIRE(gap<2*cycle);}last[slot]=offset;fields[slot][pos]=0;
	   }
	   REQUIRE(changed<=1);REQUIRE(propagations()-before==static_cast<unsigned>(changed));
	   REQUIRE(changed==((Uint32(start+offset)&7)==0?1:0));totalBuilds+=changed;
	  }
	  for(int n:visits)REQUIRE(n>=3);
	  REQUIRE(maxGap>=cycle);
	  std::cout<<"FOUR_TEAM_WRAP slots="<<slots<<" cycle="<<cycle<<" tested_ticks="<<4*cycle<<" maximum_gap="<<maxGap<<" strict_bound="<<2*cycle<<" builds="<<totalBuilds<<" PASS\n";
	 }
	}
}
