// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AINumbi.h"
#include "NumbiQueries.h"
#include "ai/engine/AIDecision.h"
#include "AI.h"
#include "Player.h"

TEST_SUITE("NumbiObservation")
{
 TEST_CASE("private initial resource fields match live seeds costs and destination ties")
 {
  glob2test::HeadlessGlobals globals;
  glob2test::HeadlessGame fixture{glob2test::GameOptions{
   .wDec=5,.hDec=5,.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true}};
  auto& game=fixture.game;auto& map=game.map;
  map.setResourceByIndex(5,5,WHEAT,1);map.setResourceByIndex(24,20,WOOD,1);map.setResourceByIndex(10,24,CHERRY,1);
  map.paintCell(16,16,WATER);map.paintCell(17,16,ICE);map.paintCell(18,16,TRAIL);
  map.addForbidden(4,5,0);map.markImmobileUnit(6,5,0);
  const auto world=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
  NumbiObservation::ResourceInitializations cache;
  std::vector<AIEngine::ResourceEnrollmentRequest> enrollments;
  NumbiObservation::Queries query(*world,0,cache,&enrollments);
  for(int resource:{WHEAT,WOOD,CHERRY}) for(int swim:{0,3,6}) {
   for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
    CAPTURE(resource);CAPTURE(swim);CAPTURE(x);CAPTURE(y);
    int snapshotX=-1,snapshotY=-1,snapshotDistance=-1,liveX=-1,liveY=-1,liveDistance=-1;
    const bool captured=query.resourceAvailableUpdate(0,resource,swim,x,y,&snapshotX,&snapshotY,&snapshotDistance);
    const bool live=map.materialAvailableUpdateSlot(0,resource,swim,x,y,&liveX,&liveY,&liveDistance);
    CHECK(captured==live);CHECK(snapshotX==liveX);CHECK(snapshotY==liveY);
    if(live) CHECK(snapshotDistance==liveDistance);
   }
  }
  CHECK(enrollments.size()==9);
 }
 TEST_CASE("published aged resource fields survive changed resources without fresh reconstruction")
 {
  glob2test::HeadlessGlobals globals;
  glob2test::HeadlessGame fixture{glob2test::GameOptions{
   .wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true}};
  auto& game=fixture.game;auto& map=game.map;
  map.setResourceByIndex(5,5,WHEAT,1);
  int initialX,initialY,initialDistance;
  REQUIRE(map.materialAvailableUpdateSlot(0,materialIndex(MaterialId::Food),0,8,8,&initialX,&initialY,&initialDistance));
  map.setNoResource(5,5,0);map.setResourceByIndex(24,24,WHEAT,1);
  auto world=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
  NumbiObservation::ResourceInitializations cache;
  {
   NumbiObservation::Queries query(*world,0,cache);
   int x,y,distance;
   REQUIRE(query.resourceAvailableUpdate(0,materialIndex(MaterialId::Food),0,8,8,&x,&y,&distance));
   CHECK(x==initialX);CHECK(y==initialY);CHECK(distance==initialDistance);
  }
  CHECK(cache.empty());
  std::weak_ptr<const AIEngine::AIWorldView> weak=world;world.reset();CHECK(weak.expired());
 }
}
