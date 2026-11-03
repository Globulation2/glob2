// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientFixture.h"
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
   glob2test::awaitGradient(game,[&] { return map.getMaterialGradientSlot(0,resource,swim); });
   for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
    CAPTURE(resource);CAPTURE(swim);CAPTURE(x);CAPTURE(y);
    int snapshotX=-1,snapshotY=-1,snapshotDistance=-1,liveX=-1,liveY=-1,liveDistance=-1;
    const bool captured=query.resourceAvailableUpdate(0,resource,swim,x,y,&snapshotX,&snapshotY,&snapshotDistance);
    const bool live=map.materialAvailableUpdateSlot(0,resource,swim,x,y,&liveX,&liveY,&liveDistance);
    CHECK(captured==live);
    // Destination outputs are meaningful only on success. The live Pending /
    // Unreachable adapter leaves them untouched; the private oracle may write
    // its search origin on failure. Reachable routes must still match exactly.
    if(live) {
     CHECK(snapshotX==liveX);CHECK(snapshotY==liveY);
     CHECK(snapshotDistance==liveDistance);
    }
   }
  }
  CHECK(enrollments.size()==9);
 }
 TEST_CASE("published aged resource fields survive changed resources without fresh reconstruction")
 {
  glob2test::HeadlessGlobals globals;
  for (bool enabled : {false,true}) {
  CAPTURE(enabled);
  glob2test::HeadlessGame fixture{glob2test::GameOptions{
   .wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true}};
  auto& game=fixture.game;auto& map=game.map;
  map.setMaterialDestinationsEnabled(enabled);
  map.setResourceByIndex(5,5,WHEAT,1);
  int initialX,initialY,initialDistance;
  glob2test::awaitGradient(game,[&] { return map.getMaterialGradientSlot(0,WHEAT,0); });
  REQUIRE(map.materialAvailableUpdateSlot(0,materialIndex(MaterialId::Food),0,8,8,&initialX,&initialY,&initialDistance));
  map.setNoResource(5,5,0);map.setResourceByIndex(24,24,WHEAT,1);
  auto world=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
  NumbiObservation::ResourceInitializations cache;
  {
   NumbiObservation::Queries query(*world,0,cache);
   int x,y,distance;
   REQUIRE(query.resourceAvailableUpdate(0,materialIndex(MaterialId::Food),0,8,8,&x,&y,&distance));
   CHECK(x==initialX);CHECK(y==initialY);CHECK(distance==initialDistance);
   int liveX=-1,liveY=-1,liveDistance=-1;
   REQUIRE(map.materialAvailableUpdateSlot(0,materialIndex(MaterialId::Food),0,8,8,&liveX,&liveY,&liveDistance));
   CHECK(x==liveX);CHECK(y==liveY);CHECK(distance==liveDistance);
   if(map.materialDestinationsEnabled()) {
    Sint32 workerX=-1,workerY=-1;
    CHECK_FALSE(map.projectMaterialDestination(map.getMaterialGradientSlot(0,WHEAT,0),0,WHEAT,0,8,8,&workerX,&workerY));
   }
  }
  CHECK(cache.empty());
  std::weak_ptr<const AIEngine::AIWorldView> weak=world;world.reset();CHECK(weak.expired());
  }
 }
 TEST_CASE("cold private initialization retains legacy ascent before labeled publication")
 {
  glob2test::HeadlessGlobals globals;
  glob2test::HeadlessGame fixture{glob2test::GameOptions{
   .wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true}};
  auto& game=fixture.game;auto& map=game.map;
  map.setMaterialDestinationsEnabled(true);
  map.setResourceByIndex(8,8,WHEAT,1);map.setResourceByIndex(24,8,WHEAT,1);
  auto world=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
  REQUIRE(world->resourceGradient(0,WHEAT,0).empty());
  NumbiObservation::ResourceInitializations cache;NumbiObservation::Queries query(*world,0,cache);
  const auto gradient=query.resourceGradient(0,WHEAT,0);
  for(int y=0;y<32;++y)for(int x=0;x<32;++x) {
   int px=-1,py=-1,distance=-1;Sint32 legacyX=-1,legacyY=-1;
   REQUIRE(query.resourceAvailableUpdate(0,WHEAT,0,x,y,&px,&py,&distance));
   REQUIRE(query.getGlobalGradientDestination(gradient.data(),x,y,&legacyX,&legacyY));
   CHECK(px==legacyX);CHECK(py==legacyY);
  }
  REQUIRE(cache.size()==1);
 }
 TEST_CASE("published material XY tie choices match live and snapshot planning")
 {
  glob2test::HeadlessGlobals globals;
  glob2test::HeadlessGame fixture{glob2test::GameOptions{
   .wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.loadDefaultRace=true}};
  auto& game=fixture.game;auto& map=game.map;
  map.setMaterialDestinationsEnabled(true);
  map.setResourceByIndex(8,8,WHEAT,1);map.setResourceByIndex(24,8,WHEAT,1);
  map.paintCell(16,12,WATER);map.paintCell(16,13,ICE);map.paintCell(16,14,TRAIL);
  for(int swim:{0,3,6}) {
   const auto* values=glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,swim);});
   const auto destinations=map.materialGradientDestinations(values);REQUIRE(destinations);
   auto world=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
   NumbiObservation::ResourceInitializations cache;NumbiObservation::Queries query(*world,0,cache);
   for(int y=0;y<32;++y)for(int x=0;x<32;++x) {
    int sx=-1,sy=-1,sd=-1,lx=-1,ly=-1,ld=-1;
    const bool captured=query.resourceAvailableUpdate(0,WHEAT,swim,x,y,&sx,&sy,&sd);
    const bool live=map.materialAvailableUpdateSlot(0,WHEAT,swim,x,y,&lx,&ly,&ld);
    CHECK(captured==live);
    if(live) {
     CHECK(sx==lx);CHECK(sy==ly);CHECK(sd==ld);
     CHECK(sx==destinations->x(map.coordToIndex(x,y)));
     CHECK(sy==destinations->y(map.coordToIndex(x,y)));
    }
   }
   CHECK(cache.empty());
  }
 }

}
