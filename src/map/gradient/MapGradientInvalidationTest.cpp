// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include <nlohmann/json.hpp>
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <algorithm>
#include <iostream>
#include "GlobalContainer.h"
#include "Game.h"
#include "Map.h"
#include "MapInternal.h"
#include "Building.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "UnitCatalog.h"
#include "Player.h"
#include "Order.h"
#include "Brush.h"
#include "BuildingGradientSearch.h"
#include <sstream>
#include <PerformanceTelemetry.h>
#include <memory>
#include <vector>
#include "gradient/ResourceGradientProbe.h"
#include <fstream>
#include "gradient/ResourceExperiment.h"
#include "ai/observation/AIWorldView.h"

namespace
{
// SDL compiler flags may rename main even when SDL_MAIN_HANDLED is set.
struct ScopedResourceEnv {
 std::string key,old;bool existed;
 ScopedResourceEnv(const char* k,const char* v):key(k),old(std::getenv(k)?std::getenv(k):""),existed(std::getenv(k)!=nullptr){setenv(k,v,1);}
 ~ScopedResourceEnv(){if(existed)setenv(key.c_str(),old.c_str(),1);else unsetenv(key.c_str());}
};
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

// A water-only caller must reach the same test building over water. Existing
// walking/swimming variants keep their original grass setup and assertions.
static void prepareMovementTerrain(Map& map,int swim)
{
 if(swim==SWIM_CLASS_COUNT-1)
  for(int y=0;y<map.getH();++y)for(int x=0;x<map.getW();++x)
   if(map.getBuilding(x,y)==NOGBID)map.paintCell(x,y,WATER);
}

TEST_SUITE("MapGradientInvalidation")
{
 TEST_CASE("experimental lazy resource fields resolve exact scalar and movement answers")
 {
  glob2test::HeadlessGlobals globals;
  for(int swim=0;swim<SWIM_CLASS_COUNT;++swim) {
   Fixture full(0);
   std::unique_ptr<Fixture> lazy;
   {ScopedResourceEnv mode("GLOB2_RESOURCE_LAZY_DEPTH","0");lazy=std::make_unique<Fixture>(0);}
   for(auto* f:{&full,lazy.get()}) {
    if(swim==SWIM_CLASS_COUNT-1)for(int y=0;y<64;++y)for(int x=0;x<64;++x)f->game.map.paintCell(x,y,WATER);
    f->game.map.setResourceByIndex(20,20,WOOD,1);
    f->game.map.setResourceByIndex(45,45,WOOD,1);
   }
   auto& a=full.game.map;auto& b=lazy->game.map;
   const auto* expected=a.getMaterialGradientSlot(0,WOOD,swim);
   const Uint16* partial;
   {ResourceExperiment::ReadScope read(ResourceExperiment::Reader::Movement);partial=b.getMaterialGradientSlot(0,WOOD,swim);}
   if(swim==0) {
    REQUIRE(expected[0]>GRADIENT_UNREACHABLE);
    CHECK(partial[0]==GRADIENT_UNREACHABLE);
    REQUIRE(ResourceSnapshotExperiment::resolveOwner(partial,b.coordToIndex(20,20)));
    CHECK(partial[0]==GRADIENT_UNREACHABLE); // checking a seeded goal must not finish the map
    CHECK(b.isGradientPeak(partial,20,20)==a.isGradientPeak(expected,20,20));
   }
   for(const auto p:std::array<std::pair<int,int>,5>{{{0,0},{19,20},{33,33},{63,63},{20,20}}}) {
    CHECK(b.getGradient(0,WOOD,swim,p.first,p.second)==expected[b.coordToIndex(p.first,p.second)]);
    EntityRandom ra;ra.initialize(7,EntityRandom::Kind::Unit,0,1);auto rb=ra;
    int ax=0,ay=0,bx=0,by=0;bool as=false,bs=false;
    const bool am=a.pathfindMaterial(ra,0,WOOD,swim,p.first,p.second,&ax,&ay,&as,nullptr,false);
    const bool bm=b.pathfindMaterial(rb,0,WOOD,swim,p.first,p.second,&bx,&by,&bs,nullptr,false);
    CHECK(am==bm);CHECK(as==bs);CHECK(ra==rb);if(am){CHECK(ax==bx);CHECK(ay==by);}
   }
   b.finishResourceGradients();
   CHECK(std::equal(expected,expected+4096,partial));
   const Uint16 capturedGoal=expected[a.coordToIndex(20,20)];
   for(auto* f:{&full,lazy.get()}) {
    auto& m=f->game.map;m.configureGradientPipeline(0,2);m.advanceGradientPipeline();
    m.stagePeriodicGradientPreparation();m.preparePendingGradient();
    m.replaceResource(20,20,Resource{});
    m.advanceGradientPipeline();m.advanceGradientPipeline();m.finishResourceGradients();
   }
   const auto* scheduled=a.getMaterialGradientSlot(0,WOOD,swim);
   const auto* resumed=b.getMaterialGradientSlot(0,WOOD,swim);
   CHECK(std::equal(scheduled,scheduled+4096,resumed));
   CHECK(resumed[b.coordToIndex(20,20)]==capturedGoal);
  }
 }

 TEST_CASE("experimental lazy resource snapshots retain an exact independent frontier")
 {
  glob2test::HeadlessGlobals globals;
  for(int swim:{0,6,7}) {
   Fixture full(0);
   std::unique_ptr<Fixture> lazy;
   {ScopedResourceEnv mode("GLOB2_RESOURCE_LAZY_DEPTH","0");lazy=std::make_unique<Fixture>(0);}
   for(auto* f:{&full,lazy.get()}) {
    if(swim==7)for(int y=0;y<64;++y)for(int x=0;x<64;++x)f->game.map.paintCell(x,y,WATER);
    f->game.map.setResourceByIndex(20,20,WOOD,1);f->game.map.setResourceByIndex(45,45,WOOD,1);
   }
   const auto* expected=full.game.map.getMaterialGradientSlot(0,WOOD,swim);
   {ResourceExperiment::ReadScope read(ResourceExperiment::Reader::Movement);lazy->game.map.getMaterialGradientSlot(0,WOOD,swim);}
   const auto world=AIEngine::AIWorldView::capture(lazy->game,AIEngine::AIWorldView::captureCatalog(lazy->game));
   const auto& captured=world->components();
   if(!captured.resourceFields || !captured.resourceFields->find(MapState::planeKey(0,WOOD,swim,false))) {
    CHECK(std::all_of(expected,expected+4096,[](auto v){return v<=GRADIENT_UNREACHABLE;}));continue;
   }
   lazy->game.map.replaceResource(20,20,Resource{});lazy->game.map.updateMaterialGradient(0,WOOD,swim);
   for(const auto p:std::array<std::pair<int,int>,3>{{{0,0},{19,20},{63,63}}}) {
    std::unique_lock<std::mutex> guard;
    const auto values=world->resourceGradientAt(0,WOOD,swim,lazy->game.map.coordToIndex(p.first,p.second),guard);
    REQUIRE(values.size()==4096);CHECK(values[lazy->game.map.coordToIndex(p.first,p.second)]==expected[lazy->game.map.coordToIndex(p.first,p.second)]);
   }
   const auto all=world->resourceGradient(0,WOOD,swim);
   CHECK(std::equal(expected,expected+4096,all.begin()));
  }
 }
 TEST_CASE("resource quality probe detects a depleted goal without changing simulation state")
 {
  glob2test::HeadlessGlobals globals;
  glob2test::HeadlessGame fixture({.wDec=6,.hDec=6,.teams=1,.discovered=true,.clearImmobile=true,.header=true});
  auto& game=fixture.game; auto& map=game.map;
  map.setResourceByIndex(20,20,WOOD,1);
  map.setResourceByIndex(30,20,WOOD,1);
  const auto* live=map.getMaterialGradientSlot(0,WOOD,0);
  const auto path=glob2test::artifactDir()/"resource-quality.csv";
  ResourceGradientProbe probe(path.string().c_str());
  probe.window=0; probe.captured[live]=0;
  EntityRandom random; random.initialize(1,EntityRandom::Kind::Unit,0,1);
  const auto saved=random;
  probe.observe(map,0,WOOD,0,20,20,live,random,false,0,0);
  map.replaceResource(20,20,Resource{}); game.stepCounter=1;
  const auto before=map.checkSum(true);
  probe.observe(map,0,WOOD,0,20,20,live,random,false,0,0);
  CHECK(map.checkSum(true)==before); CHECK(random==saved);
  map.replaceResource(30,20,Resource{}); game.stepCounter=2;
  const auto depleted=map.checkSum(true);
  probe.observe(map,0,WOOD,0,20,20,live,random,false,0,0);
  CHECK(map.checkSum(true)==depleted); CHECK(random==saved);
  game.stepCounter=9;
  probe.observe(map,0,WOOD,0,20,20,live,random,false,0,0);
  game.stepCounter=10;
  probe.observe(map,0,WOOD,0,20,20,live,random,false,0,0);
  probe.output.flush();
  std::ifstream input(path); std::string line;
  std::vector<std::string> names;
  REQUIRE(bool(std::getline(input,line)));
  { std::istringstream cells(line); std::string cell; while(std::getline(cells,cell,',')) names.push_back(cell); }
  int rows=0,allConsumed=0,delayedPublications=0;
  while(std::getline(input,line)) {
   std::map<std::string,int> row; std::istringstream cells(line); std::string cell; size_t i=0;
   while(std::getline(cells,cell,',')) { REQUIRE(i<names.size()); row[names[i++]]=std::stoi(cell); }
   if(row["policy"]==1001 && (row["query"]==4 || row["query"]==5)) {
    CHECK(row["age"]==8);
    CHECK(row["false_reachable"]==(row["query"]==4));
    ++delayedPublications;
   }
   if(row["query"]==3 && row["policy"]==0) {
    CHECK(row["false_reachable"]==1); CHECK(row["missed_move"]==0); ++allConsumed;
   }
   if(row["query"]!=2) continue;
   ++rows;
   if(row["policy"]==0 || row["policy"]==256) {
    CHECK(row["depleted_goal"]==1); CHECK(row["missed_move"]==1); CHECK(row["age"]==1);
   }
   if(row["policy"]==256) { CHECK(row["goals_removed"]==1); CHECK(row["sources_removed"]==1); }
   if(row["policy"]==1) {
    CHECK(row["depleted_goal"]==0); CHECK(row["missed_move"]==0); CHECK(row["extra_cost"]==0);
   }
  }
  CHECK(rows==19);
  CHECK(allConsumed==1);
  CHECK(delayedPublications==2);
 }

 TEST_CASE("unused movement classes suspend shared refreshes and queries rebuild retained fields")
 {
  glob2test::HeadlessGlobals globals;
  Fixture f(1); auto& map=f.game.map;
  map.setResourceByIndex(20,20,WHEAT,10);
  map.addGuardArea(24,24,0); map.addClearArea(20,20,0);
  for(int team=0;team<2;++team) {
   for(int c:{0,Map::SWIM_CLASS_EVEN}) {
    map.getMaterialGradientSlot(team,WHEAT,c);
    map.getGuardAreasGradient(team,c);
    map.getClearAreasGradient(team,c);
    map.getForbiddenGradient(team,c);
   }
   f.game.teams[team]->stats.editMeasurements().movementClassesKnown=true;
  }
  // Team 0 is eliminated but its remaining ground unit still needs its fields.
  f.game.teams[0]->isAlive=false;
  f.game.teams[0]->stats.editMeasurements().groundUnitsByMovementClass[0]=1;
  const auto before=propagations();
  for(unsigned tick=0;tick<32;++tick) map.syncStep(tick);
  CHECK(propagations()>before);
  f.game.teams[0]->stats.editMeasurements().groundUnitsByMovementClass[0]=0;
  const auto idle=propagations();
  for(unsigned tick=32;tick<96;++tick) map.syncStep(tick);
  CHECK(propagations()==idle);
  map.setResourceByIndex(21,20,WHEAT,10);
  const auto goal=map.coordToIndex(21,20);
  CHECK(map.getMaterialGradientSlot(1,WHEAT,0)[goal]==GRADIENT_AT_GOAL);
  CHECK(propagations()==idle+1);
  map.getGuardAreasGradient(1,0); map.getClearAreasGradient(1,0); map.getForbiddenGradient(1,0);
  CHECK(propagations()==idle+4);
  // Published demand may lag the first new unit; cold queries still work.
  REQUIRE(map.getMaterialGradientSlot(1,WHEAT,6));
  f.game.teams[1]->stats.editMeasurements().groundUnitsByMovementClass[0]=1;
  map.stagePeriodicGradientPreparation();
  CHECK(propagations()>idle+4);
 }

 TEST_CASE("unused fields leave the rotation while the one-field-per-tick budget stays unchanged")
 {
  glob2test::HeadlessGlobals globals;
  Fixture f(0); auto& map=f.game.map;
  map.setResourceByIndex(20,20,WHEAT,10);
  auto* active=map.getMaterialGradientSlot(0,WHEAT,0);
  map.getMaterialGradientSlot(0,WHEAT,Map::SWIM_CLASS_EVEN);
  auto& counts=f.game.teams[0]->stats.editMeasurements();
  counts.movementClassesKnown=true; counts.groundUnitsByMovementClass[0]=1;
  map.setResourceByIndex(21,20,WHEAT,10);
  const auto before=propagations();
  for(int i=0;i<20;++i) map.stagePeriodicGradientPreparation();
  CHECK(propagations()==before+20);
  CHECK(active[map.coordToIndex(21,20)]==GRADIENT_AT_GOAL);
  map.getMaterialGradientSlot(0,WHEAT,0);
  CHECK(propagations()==before+20);
  map.getMaterialGradientSlot(0,WHEAT,Map::SWIM_CLASS_EVEN);
  CHECK(propagations()==before+21);
 }

 TEST_CASE("unused movement classes suspend market stock refreshes too")
 {
  glob2test::HeadlessGlobals globals;
  glob2test::GameOptions options{.discovered=true,.clearImmobile=true,.header=true};
  options.experiments.set(ExperimentId::MarketsV2);
  glob2test::HeadlessGame world(options);
  auto& map=world.game.map;
  auto* market=world.addBuilding("market",8,8,0);
  REQUIRE(market);
  market->setMaterial(WHEAT,100);
  map.getMaterialGradientSlot(0,WHEAT,0,true);
  map.configureGradientPipeline(0,2);
  world.team->stats.editMeasurements().movementClassesKnown=true;
  const auto before=propagations();
  for(int i=0;i<8;++i) { map.stagePeriodicGradientPreparation(); map.advanceGradientPipeline(); }
  CHECK(map.gradientPipelineStatus().jobs==0);
  CHECK(propagations()==before);
  market->setMaterial(WHEAT,0);
  CHECK(map.getMaterialGradientSlot(0,WHEAT,0,true)[map.coordToIndex(8,8)]!=GRADIENT_AT_GOAL);
  CHECK(propagations()==before+1);
  world.team->stats.editMeasurements().groundUnitsByMovementClass[0]=1;
  map.stagePeriodicGradientPreparation();
  CHECK(map.gradientPipelineStatus().jobs==1);
 }

 TEST_CASE("suspended freshness and published movement counts survive binary and text saves")
 {
  glob2test::HeadlessGlobals globals;
  for(bool text:{false,true}) {
   glob2test::HeadlessGame original(glob2test::GameOptions{.wDec=6,.hDec=6,.loadDefaultRace=true,.header=true}); auto& map=original.game.map;
   map.setResourceByIndex(20,20,WHEAT,10);
   map.getMaterialGradientSlot(0,WHEAT,0);
   original.game.teams[0]->stats.editMeasurements().movementClassesKnown=true;
   map.stagePeriodicGradientPreparation();
   map.setResourceByIndex(21,20,WHEAT,10);
   auto* memory=new GAGCore::MemoryStreamBackend;
   std::unique_ptr<GAGCore::OutputStream> out(text
    ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(memory))
    : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(memory)));
   original.game.save(out.get(),false,"suspended gradients"); out->flush();
   Game restored(nullptr);
   auto backend=std::make_unique<GAGCore::MemoryStreamBackend>(memory->getBuffer(),memory->getPosition());
   backend->seekFromStart(0);
   std::unique_ptr<GAGCore::InputStream> in(text
    ? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(backend.get()))
    : static_cast<GAGCore::InputStream*>(new GAGCore::BinaryInputStream(backend.release())));
   REQUIRE(restored.load(in.get()));
   CHECK(restored.teams[0]->stats.editMeasurements().movementClassesKnown);
   CHECK_FALSE(restored.teams[0]->stats.usesGroundMovementClass(0));
   CHECK(map.checkSum(true)==restored.map.checkSum(true));
   const auto before=propagations();
   CHECK(restored.map.getMaterialGradientSlot(0,WHEAT,0)[map.coordToIndex(21,20)]==GRADIENT_AT_GOAL);
   CHECK(propagations()==before+1);
  }
 }

 TEST_CASE("suspending a movement class cancels pending periodic publications")
 {
  glob2test::HeadlessGlobals globals;
  Fixture f(0); auto& map=f.game.map;
  map.setResourceByIndex(20,20,WHEAT,10);
  map.getMaterialGradientSlot(0,WHEAT,0);
  map.configureGradientPipeline(0,2);
  map.advanceGradientPipeline();
  map.stagePeriodicGradientPreparation(); map.preparePendingGradient();
  REQUIRE(map.gradientPipelineStatus().pending>0);
  f.game.teams[0]->stats.editMeasurements().movementClassesKnown=true;
  map.stagePeriodicGradientPreparation(); map.preparePendingGradient();
  for(int i=0;i<4;++i) map.advanceGradientPipeline();
  CHECK(map.gradientPipelineStatus().published==0);
  CHECK(map.gradientPipelineStatus().discarded>0);
  const auto before=propagations();
  map.getMaterialGradientSlot(0,WHEAT,0);
  CHECK(propagations()==before+1);
 }

 TEST_CASE("forbidden painting refreshes routes across passable resources")
 {
  glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings = true});
  for(int swim=0;swim<SWIM_CLASS_COUNT;++swim) {
   Fixture f(0); player(f); auto& map=f.game.map;
   auto* building=f.game.addBuilding(8,8,globals->buildingsTypes.getTypeNum("inn",0,false),0);
   REQUIRE(building);
   prepareMovementTerrain(map,swim);
   using Json=nlohmann::json;
   const auto cottonId=*map.resourceRegistry().find("cotton");
   auto definition=Json::parse(map.resourceRegistry().serialize())["resources"][resourceIndex(cottonId)];
   definition["key"]="test-passable-fabric";
   definition["properties"]["blocksGround"]=false;
   definition["properties"]["clearable"]=true;
   map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({definition})}}.dump());
   const auto cotton=*map.resourceRegistry().find("test-passable-fabric");
   map.setResource(20,20,cotton,1);
   REQUIRE(!map.resourceBlocksGround(map.coordToIndex(20,20)));
   map.buildingGradient(building,swim); map.finishBuildingGradient(building,swim);
   edit(f,20,20,true);
   const auto* gradient=map.buildingGradient(building,swim); map.finishBuildingGradient(building,swim);
   REQUIRE(gradient!=nullptr);
   REQUIRE(gradient[map.coordToIndex(20,20)]==GRADIENT_FORBIDDEN);
   const std::vector<Uint16> actual(gradient,gradient+map.getW()*map.getH());
   map.updateGlobalGradient(building,swim); map.finishBuildingGradient(building,swim);
   REQUIRE(std::equal(actual.begin(),actual.end(),building->globalGradient[building->routeSlot(swim,BuildingRoute::Automatic)]));
  }
 }

	TEST_CASE("forbidden edits invalidate exactly the affected fields and preserve the rest")
	{
		glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings = true});
	 int checks=0;
	 for(int swim=0;swim<SWIM_CLASS_COUNT;++swim)for(int resource:{WHEAT,WOOD,NO_RES_TYPE})for(bool selected:{false,true})for(bool mixed:{false,true})for(bool add:{false,true}) {
	  Fixture f(1);player(f);auto& m=f.game.map;int n=m.getW()*m.getH();std::vector<Building*> bs;
	  for(int team=0;team<2;++team)for(const char* kind:{"inn","warflag","explorationflag","clearingflag"}){
	   int x=team?42:8,y=8+int(bs.size()%4)*10;auto* b=f.game.addBuilding(x,y,globals->buildingsTypes.getTypeNum(kind,0,false),team);REQUIRE(b);b->unitStayRange=32;b->setClearsMaterial(WHEAT,true); b->setClearsMaterial(WOOD,true);bs.push_back(b);
	  }
	  prepareMovementTerrain(m,swim);
	  if(resource!=NO_RES_TYPE)m.setResourceByIndex(20,20,resource,1);
	  m.setResourceByIndex(25,24,WHEAT,1);m.addGuardArea(26,26,0);if(selected)m.addClearArea(20,20,0);
	  if(!add){m.addForbidden(20,20,0);if(mixed)m.addForbidden(21,20,0);}
	  for(auto* b:bs){m.buildingGradient(b,swim);m.updateGlobalGradient(b,swim);m.finishBuildingGradient(b,swim);}
	  m.getForbiddenGradient(0,swim);m.getGuardAreasGradient(0,swim);m.getClearAreasGradient(0,swim);
	  edit(f,20,20,add,mixed);
	  for(auto* b:bs){auto* g=m.buildingGradient(b,swim);m.finishBuildingGradient(b,swim);REQUIRE(g!=nullptr);std::vector<Uint16> before(g,g+n);m.updateGlobalGradient(b,swim);m.finishBuildingGradient(b,swim);REQUIRE(std::equal(before.begin(),before.end(),b->globalGradient[b->routeSlot(swim, BuildingRoute::Automatic)]));++checks;}
	  auto area=[&](auto get,auto update){const Uint16* g=(m.*get)(0,swim);std::vector<Uint16> before(g,g+n);(m.*update)(0,swim);REQUIRE(std::equal(before.begin(),before.end(),(m.*get)(0,swim)));++checks;};
	  area(&Map::getForbiddenGradient,static_cast<void(Map::*)(int,int)>(&Map::updateForbiddenGradient));
	  area(&Map::getGuardAreasGradient,static_cast<void(Map::*)(int,int)>(&Map::updateGuardAreasGradient));
	  area(&Map::getClearAreasGradient,static_cast<void(Map::*)(int,int)>(&Map::updateClearAreasGradient));
	 }
	 // Already-stale walking fields must not be blessed current by an unrelated order.
	 {Fixture f(1);player(f);auto& m=f.game.map;auto* b=f.game.addBuilding(8,8,globals->buildingsTypes.getTypeNum("inn",0,false),0);m.setResourceByIndex(20,20,WHEAT,1);m.buildingGradient(b,0);auto old=b->gradientGeneration[0];m.addForbidden(30,30,1);REQUIRE(old!=m.topologyGeneration);edit(f,20,20,true);REQUIRE(b->gradientGeneration[0]==old);}
	 // Irrelevant orders preserve a paused search, its frozen field and dirty status.
	 {Fixture f(1);player(f);auto& m=f.game.map;
	  auto* b=f.game.addBuilding(8,8,globals->buildingsTypes.getTypeNum("inn",0,false),0);
	  m.setResourceByIndex(20,20,WHEAT,1);m.buildingGradient(b,0);m.updateGlobalGradient(b,0);
	  auto* search=b->globalGradientSearch[0].get();REQUIRE(search);
	  auto* field=b->globalGradient[0];std::vector<Uint16> snapshot(field,field+4096);
	  const bool dirty=b->dirtyGradient[0];edit(f,20,20,true);
	  REQUIRE((b->globalGradientSearch[0].get()==search && b->globalGradient[0]==field));
	  REQUIRE((b->dirtyGradient[0]==dirty && std::equal(snapshot.begin(),snapshot.end(),field)));
	  const auto generation=m.topologyGeneration;edit(f,20,20,true);
	  REQUIRE((m.topologyGeneration==generation && b->globalGradientSearch[0].get()==search));
	 }
	 // Record a pre-existing stale escape field that an incidental forbidden edit used to refresh.
	 {Fixture f(1);player(f);auto& m=f.game.map;f.game.gameHeader.setResourceGrowthDisabled(true);m.setResourceByIndex(20,20,WHEAT,1);m.setResourceByIndex(21,20,WHEAT,1);m.addForbidden(20,20,0);m.addForbidden(21,20,0);m.getForbiddenGradient(0,0);m.replaceResource(21,20, Resource{});edit(f,20,20,false);auto* g=m.getForbiddenGradient(0,0);auto stale=g[m.coordToIndex(21,20)];const int bound=8*f.game.mapHeader.getNumberOfTeams()*SWIM_CLASS_COUNT; for(int i=0;i<=bound;++i)m.syncStep(i); auto fresh=g[m.coordToIndex(21,20)];std::cout<<"ESCAPE_STALENESS stale="<<stale<<" fresh="<<fresh<<"\n";REQUIRE(stale!=fresh);}
	 // A worker in the middle of a ring cannot use a freshly depleted exit until refresh.
	 {Fixture f(1);player(f);auto& m=f.game.map;m.addForbidden(20,20,0);
	 for(int y=19;y<=21;++y)for(int x=19;x<=21;++x)if(x!=20||y!=20)m.setResourceByIndex(x,y,WHEAT,1);
	 m.setResourceByIndex(25,25,WHEAT,1);m.getForbiddenGradient(0,0);m.replaceResource(21,20, Resource{});m.setResourcesGrow(21,20, false);edit(f,25,25,true);
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
      CAPTURE(swim); CAPTURE(mutation);
      if(swim==WATER_ONLY_CLASS) {
          // The extra escape-field schedule is enabled by actual catalog
          // capabilities. A manually requested field alone keeps stock timing.
          auto definitions=nlohmann::json::parse(f.game.unitCatalog().serialize());
          auto swimmer=definitions["units"][WORKER]; swimmer["key"]="fixture:water-only";
          swimmer["behaviors"]["walk"]=false; swimmer["behaviors"]["fly"]=false;
          swimmer["behaviors"]["swim"]=true;
          for(auto& level:swimmer["levels"]) level["performance"][SWIM]=20;
          definitions["units"].push_back(swimmer);
          f.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
          f.game.configureBuildingCatalog();
          REQUIRE(f.game.hasWaterOnlyUnits());
          prepareMovementTerrain(m,swim);
      } else REQUIRE_FALSE(f.game.hasWaterOnlyUnits());
	  for(int y=16;y<25;++y)for(int x=16;x<25;++x)m.addForbidden(x,y,0);
	  m.setResourceByIndex(20,20,WHEAT,1);m.setResourceAmount(m.coordToIndex(20,20), 3);
	  if(mutation==4)m.markImmobileUnit(21,20,0);
	  if(mutation==6)m.setBuilding(21,20,1,1,42);
	  if(mutation==8)m.paintCell(21, 20, WATER);
	  auto* g=m.getForbiddenGradient(0,swim);std::vector<Uint16> old(g,g+4096);
	  switch(mutation){
	   case 0:m.setResourceAmount(m.coordToIndex(20,20), 1);m.decResource(20,20);break;
	   case 1:m.incResourceByIndex(21,20,WHEAT,0);break;
	   case 2:m.decResource(20,20);break; // quantity3->2 leaves blocking unchanged
	   case 3:m.markImmobileUnit(21,20,0);break;
	   case 4:m.clearImmobileUnit(21,20);break;
	   case 5:m.setBuilding(21,20,1,1,42);break;
	   case 6:m.setBuilding(21,20,1,1,NOGBID);break;
	   case 7:m.paintCell(21, 20, WATER);break;
	   case 8:m.paintCell(21, 20, GRASS);break;
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
	   if(x==17||x==23||y==17||y==23)m.setResourceByIndex(x,y,WHEAT,1);
	  }
	  for(int team=0;team<4;++team)for(int swim=0;swim<SWIM_CLASS_COUNT;++swim)m.getForbiddenGradient(team,swim);
	  auto before=propagations();int lookups=0;
	  for(int iteration=0;iteration<512;++iteration)for(int team=0;team<4;++team)for(int swim=0;swim<SWIM_CLASS_COUNT;++swim){int dx=0,dy=0;REQUIRE(!m.pathfindForbidden(nullptr,team,swim,20,20,&dx,&dy));++lookups;}
	  REQUIRE(propagations()==before);
	  std::cout<<"UNREACHABLE_POCKET lookups="<<lookups<<" propagation_rebuilds="<<propagations()-before<<" PASS\n";
	 }
	 {Fixture f(3);player(f);f.game.gameHeader.setResourceGrowthDisabled(true);auto& m=f.game.map;
	  const int slots=4*LEGACY_SWIM_CLASS_COUNT,cycle=8*slots;std::vector<Uint16*> fields;
	  for(int team=0;team<4;++team)for(int swim=0;swim<LEGACY_SWIM_CLASS_COUNT;++swim)fields.push_back(m.getForbiddenGradient(team,swim));
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
