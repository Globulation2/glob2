// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AIWarrush.h"
#include "ai/observation/WorldQueries.h"
#include "Player.h"
#include "Order.h"
#include <algorithm>

TEST_SUITE("WarrushObservation")
{
 TEST_CASE("declared inputs omit published fields without changing decisions")
 {
  glob2test::HeadlessGlobals globals;
  glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5,.hDec=5,.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true}};
  auto& game=fixture.game;
  REQUIRE(fixture.addBuilding("swarm",4,4));
  game.map.setResourceByIndex(18,18,WHEAT,1);
  Sint32 x=0,y=0,distance=0;
  game.map.materialAvailableUpdateSlot(0,materialIndex(MaterialId::Food),0,4,4,&x,&y,&distance);
  AIWarrush full(game.players[0]),projected(game.players[0]);
  MersenneTwister fullRandom(713),projectedRandom(713);
  full.setRandomEngine(fullRandom);projected.setRandomEngine(projectedRandom);
  const auto requirements=projected.observationRequirements();
  CHECK_FALSE(SimulationSnapshot::needs(requirements,SimulationSnapshot::Component::ResourceFields));
  CHECK(SimulationSnapshot::needs(requirements,SimulationSnapshot::Component::Growth));
  const auto world=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
  REQUIRE_FALSE(world->resourceGradient(0,WHEAT,0).empty());
  const AIEngine::AIWorldView input(world->components().project(requirements));
  CHECK_FALSE(input.components().resourceFields);
  std::vector<AIEngine::ExecutionReceipt> receipts;
  std::vector<AIEngine::ResourceEnrollmentRequest> enrollments;
  for(Uint64 poll=0;poll<128;++poll) {
   AIEngine::DecisionContext before{*world,0,0,receipts},after{input,0,0,receipts};
   before.pollSequence=after.pollSequence=poll;after.resourceEnrollments=&enrollments;
   auto a=full.getOrder(before),b=projected.getOrder(after);
   REQUIRE(a);REQUIRE(b);REQUIRE(a->getOrderType()==b->getOrderType());
   REQUIRE(a->getDataLength()==b->getDataLength());
   if(a->getDataLength()) CHECK(std::equal(a->getData(),a->getData()+a->getDataLength(),b->getData()));
   CHECK(enrollments.empty());
  }
 }

 TEST_CASE("snapshot influence and uphill destination preserve live query semantics")
 {
  glob2test::HeadlessGlobals globals;
  glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5,.hDec=5,.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true}};
  auto& game=fixture.game;
  const auto world=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
  AIEngine::ResourceInitializations cache;AIEngine::WorldQueries query(*world,0,cache);
  std::vector<Uint8> live(32*32,1),captured;
  for(int y=2;y<30;++y) if(y!=9 && y!=24) live[y*32+14]=0;
  live[3*32+1]=255;live[24*32+30]=210;live[16*32+16]=64;
  captured=live;game.map.updateGlobalGradient(live.data());query.updateGlobalGradient(captured.data());
  REQUIRE(live==captured);
  for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
   Sint32 liveX,liveY,viewX,viewY;
   CHECK(game.map.getGlobalGradientDestination(live.data(),x,y,&liveX,&liveY)==query.getGlobalGradientDestination(captured.data(),x,y,&viewX,&viewY));
   CHECK(liveX==viewX);CHECK(liveY==viewY);
  }
 }
 TEST_CASE("pending assignments settle through immutable rejection receipts")
 {
  glob2test::HeadlessGlobals globals;
  glob2test::HeadlessGame fixture{glob2test::GameOptions{.wDec=5,.hDec=5,.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true}};
  auto& game=fixture.game;
  const int type=game.buildingsTypes.getTypeNum("swarm",0,false);
  auto* swarm=game.addBuilding(4,4,type,0,0,0);REQUIRE(swarm);
  game.stepCounter=50000;
  AIWarrush ai(game.players[0]);ai.buildingDelay=100;ai.areaUpdatingDelay=100;
  const auto world=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
  std::vector<AIEngine::ExecutionReceipt> receipts;
  AIEngine::DecisionContext context{*world,0,0,receipts};context.pollSequence=1;
  auto first=std::dynamic_pointer_cast<OrderModifyBuilding>(ai.getOrder(context));REQUIRE(first);
  CHECK(swarm->maxUnitWorking==0);CHECK(ai.observation==nullptr);CHECK(ai.observedTeam==nullptr);
  context.pollSequence=2;
  CHECK(ai.getOrder(context)->getOrderType()==ORDER_NULL);
  AIEngine::ExecutionReceipt rejection;rejection.request.observedTick=world->tick;rejection.request.pollSequence=1;
  rejection.status=AIEngine::ExecutionStatus::Rejected;receipts.push_back(rejection);
  context.pollSequence=3;
  auto retry=std::dynamic_pointer_cast<OrderModifyBuilding>(ai.getOrder(context));REQUIRE(retry);
  CHECK(retry->gid==first->gid);CHECK(retry->numberRequested==first->numberRequested);
 }
}
