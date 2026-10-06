// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AIWarrush.h"
#include "ai/observation/WorldQueries.h"
#include "Player.h"
#include "Order.h"
#include <algorithm>

TEST_SUITE("WarrushObservation")
{
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
  CHECK(swarm->maxUnitWorking==0);CHECK(ai.observation==nullptr);CHECK(ai.observedTeams.empty());
  context.pollSequence=2;
  CHECK(ai.getOrder(context)->getOrderType()==ORDER_NULL);
  AIEngine::ExecutionReceipt rejection;rejection.request.observedTick=world->tick;rejection.request.pollSequence=1;
  rejection.status=AIEngine::ExecutionStatus::Rejected;receipts.push_back(rejection);
  context.pollSequence=3;
  auto retry=std::dynamic_pointer_cast<OrderModifyBuilding>(ai.getOrder(context));REQUIRE(retry);
  CHECK(retry->gid==first->gid);CHECK(retry->numberRequested==first->numberRequested);
 }
}
