// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeadlessGlobals.h"
#include "gradient/MapRouting.h"
#include "HeadlessGame.h"
#include "GameFixtures.h"
#include "ComputeExecutor.h"
#include "ActiveMaterialClaims.h"
#include "Version.h"
#include "ClaimPolicy.h"
#include "GradientFixture.h"
#include "ClaimRoute.h"
#include "ResourceClaimRouteView.h"
#include <fstream>
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <memory>
#include "Unit.h"
#include "Building.h"

namespace
{
std::string saveRoutes(Map& map, bool text)
{
    auto* backend = new GAGCore::MemoryStreamBackend;
    std::unique_ptr<GAGCore::OutputStream> output(text
        ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend))
        : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(backend)));
    map.saveActiveClaimRoutes(output.get());
    output->flush();
    return backend->takeContents();
}
void restoreRoutes(Map& map, const std::string& bytes, bool text, int version = VERSION_MINOR)
{
    auto backend = std::make_unique<GAGCore::MemoryStreamBackend>();
    backend->write(bytes.data(), bytes.size()); backend->seekFromStart(0);
    if (text) { GAGCore::TextInputStream input(backend.get()); map.loadActiveClaimRoutes(&input, version); }
    else { GAGCore::BinaryInputStream input(backend.release()); map.loadActiveClaimRoutes(&input, version); }
}
// Claims held on a tile for one material, as the ledger counts them.
unsigned claimsAt(Map& map, Uint32 cell, int material = WHEAT)
{
    return map.materialClaimLedger().materialCount(cell, unsigned(material));
}
struct Assignment
{
    Unit& unit;
    ~Assignment() { unit.releaseActiveMaterialClaim(); unit.attachedBuilding = nullptr; }
};
void assign(Unit& unit, Building& building, Map& map, int x, int y)
{
    unit.releaseActiveMaterialClaim();
    unit.attachedBuilding = &building; unit.activity = Unit::ACT_FILLING;
    unit.displacement = Unit::DIS_GOING_TO_RESOURCE; unit.destinationPurpose = WHEAT; unit.medical = Unit::MED_FREE;
    const auto target = Uint32(map.coordToIndex(x, y));
    map.setResourceByIndex(x, y, WHEAT, 1);
    map.setMaterialAmountSlot(target, WHEAT, 3);
    REQUIRE(unit.acquireMaterialClaimAt(target));
    REQUIRE(unit.activeMaterialClaimValid());
}
}

TEST_SUITE("ActiveClaimRoutes")
{
    TEST_CASE("adjacent pickup exchange swaps a distant claim for free adjacent stock and never steals another claim")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& map=fixture.game.map;map.enableResourceClaims();
        auto* consumer=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
        auto* holder=fixture.addUnit(WORKER,9,9);Assignment cleanup{*unit},cleanupHolder{*holder};
        assign(*unit,*consumer,map,14,8);assign(*holder,*consumer,map,9,8);
        map.setMaterialAmountSlot(map.coordToIndex(9,8),WHEAT,1);
        const auto original=*unit->activeMaterialClaim;const auto task=unit->materialClaimTask;
        const auto random=unit->entityRandom;const auto routes=saveRoutes(map,false);
        // The adjacent stock is reserved by another worker: no exchange.
        CHECK_FALSE(unit->prepareActiveMaterialPickup(9,8));
        CHECK(unit->activeMaterialClaim==original);CHECK(unit->materialClaimTask==task);
        CHECK(saveRoutes(map,false)==routes);
        // Once it is free, the distant claim is exchanged for it transactionally.
        holder->releaseActiveMaterialClaim();
        CHECK(unit->prepareActiveMaterialPickup(9,8));
        CHECK(unit->entityRandom==random);
        CHECK(unit->activeMaterialClaim->access==map.coordToIndex(9,8));
        CHECK(unit->materialClaimTask==task+1);
        CHECK(claimsAt(map,original.source)==0);
        CHECK(claimsAt(map,map.coordToIndex(9,8))==1);
        CHECK(map.materialAmountAtSlot(original.source,WHEAT)==3);
        CHECK(map.materialAmountAtSlot(map.coordToIndex(9,8),WHEAT)==1);
    }
    TEST_CASE("adjacent pickup exchange prefers the held reachable claim and rejects distant pickup calls")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& map=fixture.game.map;map.enableResourceClaims();
        auto* consumer=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);Assignment cleanup{*unit};
        assign(*unit,*consumer,map,9,8);const auto task=unit->materialClaimTask;
        map.setResourceByIndex(7,8,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(7,8),WHEAT,1);
        CHECK_FALSE(unit->prepareActiveMaterialPickup(7,8));CHECK(unit->materialClaimTask==task);
        CHECK(unit->prepareActiveMaterialPickup(9,8));
        assign(*unit,*consumer,map,14,8);const auto farClaim=*unit->activeMaterialClaim;
        map.setResourceByIndex(15,8,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(15,8),WHEAT,1);
        CHECK_FALSE(unit->prepareActiveMaterialPickup(15,8));CHECK(unit->activeMaterialClaim==farClaim);
    }
    TEST_CASE("denied adjacent pickup exchange preserves the outstanding route and its publication deadline")
    {
        glob2test::HeadlessGlobals globals;
        for(unsigned participants:{1u,3u}) {
            CAPTURE(participants);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
            auto& game=fixture.game;auto& map=game.map;map.configureCompute(participants);
            map.enableResourceClaims();
            auto* consumer=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            // The holder stands clear of the unit's equally cheap detours around the wheat.
            auto* holder=fixture.addUnit(WORKER,8,12);Assignment cleanup{*unit},holderCleanup{*holder};
            assign(*unit,*consumer,map,14,8);assign(*holder,*consumer,map,9,8);
            const auto adjacent=Uint32(map.coordToIndex(9,8));map.setMaterialAmountSlot(adjacent,WHEAT,1);
            game.observeCompletedWorld();int dx=0,dy=0;
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
            map.flushClaimRoutes();
            const auto admittedTick=game.stepCounter;
            const auto task=unit->materialClaimTask;
            const auto original=*unit->activeMaterialClaim,other=*holder->activeMaterialClaim;
            const auto random=unit->entityRandom;
            // Both serializations include the request identity, due tick and
            // privately completed output, while owner publication stays pending.
            const auto binary=saveRoutes(map,false),text=saveRoutes(map,true);
            const auto checksum=map.activeClaimRouteChecksum(true);
            REQUIRE_FALSE(unit->prepareActiveMaterialPickup(9,8));
            CHECK(unit->activeMaterialClaim==original);CHECK(unit->materialClaimTask==task);
            CHECK(holder->activeMaterialClaim==other);CHECK(unit->entityRandom==random);
            CHECK(claimsAt(map,original.source)==1);CHECK(claimsAt(map,adjacent)==1);
            CHECK(saveRoutes(map,false)==binary);CHECK(saveRoutes(map,true)==text);
            CHECK(map.activeClaimRouteChecksum(true)==checksum);
            map.publishClaimRoutes();
            CHECK(game.stepCounter==admittedTick);
            CHECK(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
            CHECK(saveRoutes(map,false)==binary);
            for(unsigned tick=1;tick<ResourceClaims::Policy::RoutePublicationDelay;++tick) {
                ++game.stepCounter;map.publishClaimRoutes();
                CHECK(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
            }
            ++game.stepCounter;map.publishClaimRoutes();
            CHECK(game.stepCounter==admittedTick+ResourceClaims::Policy::RoutePublicationDelay);
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
            CHECK(dx==1);CHECK(std::abs(dy)<=1);
            CHECK(unit->activeMaterialClaim==original);CHECK(unit->materialClaimTask==task);
            CHECK(map.materialAmountAtSlot(original.source,WHEAT)==3);
            CHECK(map.materialAmountAtSlot(adjacent,WHEAT)==1);
        }
    }
    TEST_CASE("adjacent pickup exchange uses toroidal distance at map edges")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& map=fixture.game.map;map.enableResourceClaims();
        auto* consumer=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,0,0);Assignment cleanup{*unit};
        assign(*unit,*consumer,map,14,8);const auto old=unit->activeMaterialClaim->source;
        map.setResourceByIndex(map.getW()-1,map.getH()-1,WHEAT,1);
        map.setMaterialAmountSlot(map.coordToIndex(-1,-1),WHEAT,1);
        REQUIRE(unit->prepareActiveMaterialPickup(-1,-1));
        CHECK(unit->targetX==map.getW()-1);CHECK(unit->targetY==map.getH()-1);
        CHECK(claimsAt(map,old)==0);
        CHECK(claimsAt(map,map.coordToIndex(-1,-1))==1);
    }
    TEST_CASE("adjacent farm pickup can keep its remote reserved stock while changing access")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::GameOptions options{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true};
        options.experiments.set(ExperimentId::FarmAreas,true);glob2test::HeadlessGame fixture(options);
        auto& map=fixture.game.map;map.enableResourceClaims();
        auto* consumer=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,7,12);Assignment cleanup{*unit};
        for(int x=8;x<=14;++x) {
            map.setResourceByIndex(x,12,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(x,12),WHEAT,x==14?2:1);map.addFarmArea(x,12,0);
        }
        unit->attachedBuilding=consumer;unit->activity=Unit::ACT_FILLING;unit->displacement=Unit::DIS_GOING_TO_RESOURCE;
        unit->destinationPurpose=WHEAT;unit->medical=Unit::MED_FREE;
        REQUIRE(unit->acquireMaterialClaimAt(map.coordToIndex(14,12)));
        const auto source=unit->activeMaterialClaim->source;const auto task=unit->materialClaimTask;
        REQUIRE(source==map.coordToIndex(14,12));REQUIRE(unit->activeMaterialClaim->reserve==1);
        REQUIRE(unit->prepareActiveMaterialPickup(8,12));
        CHECK(unit->activeMaterialClaim->source==source);CHECK(unit->activeMaterialClaim->access==map.coordToIndex(8,12));
        CHECK(unit->materialClaimTask==task+1);CHECK(claimsAt(map,source)==1);
        CHECK(claimsAt(map,map.coordToIndex(8,12))==0);
        CHECK(map.materialAmountAtSlot(source,WHEAT)==2);
    }
    TEST_CASE("pending and published routes preserve publication and bytes across binary and text saves")
    {
        glob2test::HeadlessGlobals globals;
        for (unsigned threads : {1u, 3u}) for (bool text : {false, true}) {
            CAPTURE(threads); CAPTURE(text);
            glob2test::HeadlessGame fixture({.discovered=true, .clearImmobile=true, .loadDefaultRace=true, .header=true});
            auto& game = fixture.game; auto& map = game.map;
            map.computeExecutor().configure(threads); map.enableResourceClaims();
            auto* building = fixture.addBuilding("inn", 2, 2);
            auto* unit = fixture.addUnit(WORKER, 8, 8); Assignment cleanup{*unit}; assign(*unit, *building, map, 12, 8);
            game.observeCompletedWorld();
            int dx = 99, dy = 99;
            CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
            map.flushClaimRoutes();
            const auto before = map.activeClaimRouteChecksum(true);
            const auto pending = saveRoutes(map, text);
            restoreRoutes(map, pending, text);
            CHECK(map.activeClaimRouteChecksum(true) == before);
            CHECK(saveRoutes(map, text) == pending);
            map.publishClaimRoutes();
            CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
            for (unsigned tick = 1; tick < ResourceClaims::Policy::RoutePublicationDelay; ++tick) {
                ++game.stepCounter; map.publishClaimRoutes();
                CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
            }
            ++game.stepCounter; map.publishClaimRoutes();
            CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Ready);
            CHECK(dx == 1); CHECK(std::abs(dy) <= 1);
            const auto published = saveRoutes(map, text);
            const auto publishedHash = map.activeClaimRouteChecksum(true);
            restoreRoutes(map, published, text);
            CHECK(map.activeClaimRouteChecksum(true) == publishedHash);
            CHECK(saveRoutes(map, text) == published);
            // Merely querying an unexecuted direction must not advance cursor.
            CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Ready);
            CHECK(saveRoutes(map, text) == published);
        }
    }
    TEST_CASE("changed tasks and blocked cached steps cannot reuse an old route")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true, .clearImmobile=true, .loadDefaultRace=true, .header=true});
        auto& game = fixture.game; auto& map = game.map; map.enableResourceClaims();
        map.setClaimRouteDelay(1); // Routes publish on the next tick.
        auto* building = fixture.addBuilding("inn", 2, 2);
        auto* unit = fixture.addUnit(WORKER, 8, 8); Assignment cleanup{*unit}; assign(*unit, *building, map, 12, 8);
        game.observeCompletedWorld();
        int dx = 0, dy = 0;
        REQUIRE(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
        map.flushClaimRoutes(); ++game.stepCounter; map.publishClaimRoutes();
        game.invalidateObservation(); game.observeCompletedWorld();
        REQUIRE(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Ready);
        const auto beforeTraffic = saveRoutes(map, false);
        // A unit standing on the next route cell is traffic: the worker
        // sidesteps it and the cached route itself is unchanged.
        const int blockedX = unit->posX + dx, blockedY = unit->posY + dy;
        fixture.addUnit(WORKER, blockedX, blockedY);
        map.markImmobileUnit(blockedX, blockedY, unit->owner->teamNumber);
        REQUIRE(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Ready);
        CHECK_FALSE((unit->posX + dx == blockedX && unit->posY + dy == blockedY));
        CHECK(unit->activeMaterialClaim.has_value());
        CHECK(saveRoutes(map, false) == beforeTraffic);
        // A save seals this tick's route submissions; continue on the next tick.
        const auto boundary = [&] { map.flushClaimRoutes(); ++game.stepCounter; map.publishClaimRoutes(); game.invalidateObservation(); game.observeCompletedWorld(); };
        boundary();
        // Forbidden area on the next route cell changes static geometry: the
        // cached route is dropped and replanned, never followed.
        map.addForbidden(blockedX, blockedY, unit->owner->teamNumber);
        game.invalidateObservation(); game.observeCompletedWorld();
        CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) != ResourceClaims::StepResult::Failed);
        CHECK(map.claimRouteComputing(*unit));
        CHECK(unit->activeMaterialClaim.has_value());
        CHECK(saveRoutes(map, false) != beforeTraffic);
        boundary();
        // A new task never reuses the old route, even after it is canceled.
        assign(*unit, *building, map, 8, 12);
        CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) != ResourceClaims::StepResult::Failed);
        CHECK(map.claimRouteComputing(*unit));
        map.cancelClaimRoute(ResourceClaims::unitOwner(unit->gid, unit->scriptIdentity));
        map.flushClaimRoutes(); ++game.stepCounter; map.publishClaimRoutes();
        game.invalidateObservation(); game.observeCompletedWorld();
        CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) != ResourceClaims::StepResult::Failed);
        CHECK(map.claimRouteComputing(*unit));
    }
    TEST_CASE("disabled and historical route state has no implicit active policy")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true, .header=true});
        auto& map = fixture.game.map;
        const auto before = map.activeClaimRouteChecksum(true);
        const auto disabled = saveRoutes(map, false); restoreRoutes(map, disabled, false);
        CHECK_FALSE(map.activeClaimsEnabled()); CHECK(map.activeClaimRouteChecksum(true) == before);
        map.enableResourceClaims();
        restoreRoutes(map, {}, false, 194);
        CHECK_FALSE(map.activeClaimsEnabled());
    }
    TEST_CASE("publication outside bounded prefix replans without releasing reservation")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
        auto& game=fixture.game;auto& map=game.map;map.enableResourceClaims();
        map.setClaimRouteDelay(1); // Routes publish on the next tick.
        auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
        Assignment cleanup{*unit};assign(*unit,*building,map,14,8);game.observeCompletedWorld();
        const auto claim=unit->activeMaterialClaim;const auto task=unit->materialClaimTask;
        int dx=0,dy=0;REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
        map.flushClaimRoutes();map.setGroundUnit(8,8,NOGUID);unit->posX=4;unit->posY=20;map.setGroundUnit(4,20,unit->gid);
        ++game.stepCounter;map.publishClaimRoutes();game.invalidateObservation();game.observeCompletedWorld();
        CHECK(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
        CHECK(unit->activeMaterialClaim==claim);CHECK(unit->materialClaimTask==task);
        const auto saved=saveRoutes(map,false);const auto hash=map.activeClaimRouteChecksum(true);
        restoreRoutes(map,saved,false);CHECK(map.activeClaimRouteChecksum(true)==hash);CHECK(saveRoutes(map,false)==saved);
        ++game.stepCounter;map.publishClaimRoutes();
        CHECK(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
    }

    TEST_CASE("saved routes reject inconsistent pending and progress metadata")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
        auto& game=fixture.game;auto& map=game.map;map.enableResourceClaims();
        auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
        Assignment cleanup{*unit};assign(*unit,*building,map,14,8);game.observeCompletedWorld();
        int dx=0,dy=0;REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
        const auto saved=saveRoutes(map,true);const auto hash=map.activeClaimRouteChecksum(true);
        for(const auto& replacements:std::vector<std::pair<std::string,std::string>>{
            {"enabled = 1;","enabled = 2;"},
            {"pending = 1;","pending = 2;"},
            {"pending = 1;","pending = 0;"},
            {"target = "+std::to_string(map.coordToIndex(14,8))+";","target = 0;"},
            {"awaitingAdmission = 0;","awaitingAdmission = 1;"},
            {"trafficWaits = 0;","trafficWaits = "+std::to_string(ResourceClaims::Policy::TrafficWaitLimit)+";"}}) {
            CAPTURE(replacements.first);CAPTURE(replacements.second);
            auto corrupt=saved;const auto at=corrupt.find(replacements.first);REQUIRE(at!=std::string::npos);
            corrupt.replace(at,replacements.first.size(),replacements.second);
            CHECK_THROWS_AS(restoreRoutes(map,corrupt,true),std::runtime_error);
            restoreRoutes(map,saved,true);CHECK(map.activeClaimRouteChecksum(true)==hash);
        }
    }

    TEST_CASE("terrain component prefilter selects reachable stock across the water divide")
    {
        glob2test::HeadlessGlobals globals;
        {
        glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
        auto& game=fixture.game;auto& map=game.map;
        game.gameHeader.setResourceGrowthDisabled(true);
        { Map::TerrainEditBatch edit(map);
            for(int y=0;y<32;++y)for(int x:{7,8,16,17})map.setVertexTerrain(x,y,WATER);
        }
        // Terrain-only components: two land strips, joined across the torus
        // seam on the far side; water separates them for ground movement.
        const auto labels=map.claimTerrainComponents(accessClass(MovementAccess::Land));
        const auto label=[&](int x,int y){return labels->at(Uint32(map.coordToIndex(x,y)));};
        CHECK(label(7,8)==ResourceClaims::TerrainComponentLabels::None);
        CHECK(label(16,8)==ResourceClaims::TerrainComponentLabels::None);
        CHECK(label(8,8)!=ResourceClaims::TerrainComponentLabels::None);
        CHECK(label(8,8)==label(14,18));CHECK(label(8,0)==label(15,31));
        CHECK(label(5,8)==label(20,8));CHECK(label(0,8)==label(31,8));CHECK(label(5,8)!=label(8,8));
        CHECK_FALSE(ResourceClaims::terrainComponentMayReach(*labels,8,8,Uint32(map.coordToIndex(5,8))));
        CHECK(ResourceClaims::terrainComponentMayReach(*labels,8,8,Uint32(map.coordToIndex(14,18))));
        // The water cell next to the bank is reachable through its stance.
        CHECK(ResourceClaims::terrainComponentMayReach(*labels,8,8,Uint32(map.coordToIndex(16,8))));
        const auto amphibious=map.claimTerrainComponents(accessClass(MovementAccess::Amphibious));
        CHECK(amphibious->at(Uint32(map.coordToIndex(8,8)))==amphibious->at(Uint32(map.coordToIndex(5,8))));
        const auto rebuilds=map.claimTerrainComponentRebuilds();
        CHECK(map.claimTerrainComponents(accessClass(MovementAccess::Land))==labels);
        CHECK(map.claimTerrainComponentRebuilds()==rebuilds);
        map.enableResourceClaims();
        map.setClaimRouteDelay(1); // Routes publish on the next tick.
        auto* building=fixture.addBuilding("inn",9,22);
        auto* unit=fixture.addUnit(WORKER,8,8);auto* holder=fixture.addUnit(WORKER,11,10);
        Assignment cleanup{*unit},holderCleanup{*holder};
        unit->setPerformance(SWIM,0);holder->setPerformance(SWIM,0);
        building->maxUnitWorking=2;building->setMaterial(WHEAT,0);building->updateCallLists();
        for(auto* worker:{unit,holder}) {worker->medical=Unit::MED_FREE;worker->activity=Unit::ACT_RANDOM;worker->hungriness=0;}
        const auto nearest=Uint32(map.coordToIndex(12,8));
        const auto unreachable=Uint32(map.coordToIndex(5,8));
        const auto reachable=Uint32(map.coordToIndex(14,18));
        for(const auto at:{nearest,unreachable,reachable}) {
            REQUIRE(map.incResourceByIndex(int(at%32),int(at/32),WHEAT,0));map.setMaterialAmountSlot(at,WHEAT,1);
        }
        glob2test::publishCurrentHiringSuggestions(game);
        REQUIRE(building->subscribeToBringMaterialsStep());
        REQUIRE(building->subscribeToBringMaterialsStep());
        glob2test::discardPrematureHiringSuggestions(game);
        for(auto* worker:{unit,holder}) {REQUIRE(worker->attachedBuilding==building);REQUIRE(worker->movementAccess()==MovementAccess::Land);}
        REQUIRE(holder->acquireMaterialClaimAt(nearest));
        REQUIRE(glob2test::awaitGradient(game,[&]{return map.routing().getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
        game.invalidateObservation();game.observeCompletedWorld();
        // Resource placement changes no terrain: the cached labelling stays.
        CHECK(map.claimTerrainComponents(accessClass(MovementAccess::Land))==labels);
        unit->handleActiveMaterialMovement();REQUIRE(unit->activeMaterialClaim);
        CHECK(unit->activeMaterialClaim->access==reachable);
        const auto start=map.warpDistMax(unit->posX,unit->posY,int(reachable%32),int(reachable/32));
        int dx=0,dy=0;
        map.flushClaimRoutes();++game.stepCounter;game.observeCompletedWorld();
        REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
        // The first step moves toward the reachable stock.
        CHECK(map.warpDistMax(unit->posX+dx,unit->posY+dy,int(reachable%32),int(reachable/32))<start);
        CHECK(map.materialAmountAtSlot(unreachable,WHEAT)==1);
        // Terrain edits invalidate the derived labels and rebuild them exactly.
        { Map::TerrainEditBatch edit(map); for(int x:{7,8})map.setVertexTerrain(x,8,GRASS); }
        const auto rebuilt=map.claimTerrainComponents(accessClass(MovementAccess::Land));
        CHECK(rebuilt!=labels);CHECK(map.claimTerrainComponentRebuilds()>rebuilds);
        CHECK(*rebuilt==*ResourceClaims::labelTerrainComponents(map,accessClass(MovementAccess::Land)));
        }
    }
    TEST_CASE("destination refresh reselects at the fixed cadence and switches to closer free stock")
    {
        glob2test::HeadlessGlobals globals;
        using ResourceClaims::Policy::DestinationRefreshDecisions;
        for(bool due:{false,true}) {
            CAPTURE(due);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);map.enableResourceClaims();
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            Assignment cleanup{*unit};assign(*unit,*building,map,14,8);
            const auto held=Uint32(map.coordToIndex(14,8)),closer=Uint32(map.coordToIndex(11,8));
            map.setResourceByIndex(11,8,WHEAT,1);map.setMaterialAmountSlot(closer,WHEAT,3);
            REQUIRE(glob2test::awaitGradient(game,[&]{return map.routing().getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
            map.routing().updateMaterialGradient(0,WHEAT,MovementAccess::Land,false);
            const auto relabeled=[&]{const auto* cost=map.routing().getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);const auto labels=map.routing().materialGradientDestinations(cost);return labels&&labels->x[map.coordToIndex(8,8)]==11;};
            for(unsigned boundary=0;!relabeled() && boundary<4096;++boundary){++game.stepCounter;game.observeCompletedWorld();}
            REQUIRE(relabeled());
            game.invalidateObservation();game.observeCompletedWorld();
            REQUIRE(unit->activeMaterialClaimValid());
            const auto task=unit->materialClaimTask;
            // The next decision is the DestinationRefreshDecisions-th since the
            // last selection only in the due arm.
            const Uint64 last=100;
            unit->materialClaimPolicyState.lastGoalAttempt=last;
            unit->materialClaimPolicyState.decisionOrdinal=last+DestinationRefreshDecisions-(due?1:2);
            unit->handleActiveMaterialMovement();
            REQUIRE(unit->activeMaterialClaim);
            if(due) {
                // Selection walks the pattern afresh and takes the closer stock.
                CHECK(unit->materialClaimPolicyState.lastGoalAttempt==last+DestinationRefreshDecisions);
                CHECK(unit->activeMaterialClaim->access==closer);CHECK(unit->materialClaimTask!=task);
                CHECK(claimsAt(map,held)==0);CHECK(claimsAt(map,closer)==1);
            } else {
                CHECK(unit->materialClaimPolicyState.lastGoalAttempt==last);
                CHECK(unit->activeMaterialClaim->access==held);CHECK(unit->materialClaimTask==task);
            }
        }
    }
    TEST_CASE("claims admit one holder per unit of stock and depleted holders release cleanly")
    {
        glob2test::HeadlessGlobals globals;
        {
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);map.enableResourceClaims();
            auto* building=fixture.addBuilding("inn",2,2);
            const int tx=14,ty=8;const auto target=Uint32(map.coordToIndex(tx,ty));
            map.setResourceByIndex(tx,ty,WHEAT,1);map.setMaterialAmountSlot(target,WHEAT,2);
            std::vector<Unit*> units;
            struct Cleanup { std::vector<Unit*>& units; ~Cleanup(){for(auto* u:units){u->releaseActiveMaterialClaim();u->attachedBuilding=nullptr;}} } cleanup{units};
            for(int i=0;i<5;++i) {
                auto* unit=fixture.addUnit(WORKER,8,6+i);REQUIRE(unit);units.push_back(unit);
                unit->attachedBuilding=building;unit->activity=Unit::ACT_FILLING;
                unit->displacement=Unit::DIS_GOING_TO_RESOURCE;unit->destinationPurpose=WHEAT;unit->medical=Unit::MED_FREE;
            }
            unsigned admitted=0;
            for(auto* unit:units) {
                const bool acquired=unit->acquireMaterialClaimAt(target);
                CHECK(acquired==(admitted<2));
                admitted+=acquired;
                if(acquired)CHECK(unit->activeMaterialClaimValid());else CHECK_FALSE(unit->activeMaterialClaim);
            }
            CHECK(admitted==2);
            CHECK(claimsAt(map,target)==2);
            // Selection's availability check agrees with acquisition.
            const auto held=*units.front()->activeMaterialClaim;
            const auto lastOwner=ResourceClaims::unitOwner(units.back()->gid,units.back()->scriptIdentity);
            CHECK_FALSE(map.materialClaimAvailable(lastOwner,held,2));
            // Both holders harvest; the stock is gone and the claim cannot be taken again.
            for(auto* unit:{units[0],units[1]}) {
                const auto claim=*unit->activeMaterialClaim;
                unit->releaseActiveMaterialClaim();
                REQUIRE(map.takeClaimedMaterial(claim,unit->owner->me));
            }
            CHECK(map.materialAmountAtSlot(target,WHEAT)==0);
            CHECK_FALSE(map.takeClaimedMaterial(held,units[0]->owner->me));
            CHECK(claimsAt(map,target)==0);
            // A holder of now-depleted stock is no longer valid and releases.
            map.setResourceByIndex(tx,ty,WHEAT,1);map.setMaterialAmountSlot(target,WHEAT,1);
            REQUIRE(units[2]->acquireMaterialClaimAt(target));
            map.setMaterialAmountSlot(target,WHEAT,0);
            CHECK_FALSE(units[2]->activeMaterialClaimValid());
            units[2]->releaseInvalidActiveMaterialClaim();CHECK_FALSE(units[2]->activeMaterialClaim);
            CHECK(claimsAt(map,target)==0);
        }
        // A stock decrease releases only the holders beyond the remaining
        // stock, retaining the lowest owner IDs.
        {
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);map.enableResourceClaims();
            auto* building=fixture.addBuilding("inn",2,2);
            const int tx=14,ty=8;const auto target=Uint32(map.coordToIndex(tx,ty));
            map.setResourceByIndex(tx,ty,WHEAT,1);map.setMaterialAmountSlot(target,WHEAT,4);
            std::vector<Unit*> units;
            struct Cleanup { std::vector<Unit*>& units; ~Cleanup(){for(auto* u:units){u->releaseActiveMaterialClaim();u->attachedBuilding=nullptr;}} } cleanup{units};
            std::vector<ResourceClaims::Owner> owners;
            for(int i=0;i<8;++i) {
                auto* unit=fixture.addUnit(WORKER,8,4+i);REQUIRE(unit);units.push_back(unit);
                unit->attachedBuilding=building;unit->activity=Unit::ACT_FILLING;
                unit->displacement=Unit::DIS_GOING_TO_RESOURCE;unit->destinationPurpose=WHEAT;unit->medical=Unit::MED_FREE;
                if(unit->acquireMaterialClaimAt(target))owners.push_back(ResourceClaims::unitOwner(unit->gid,unit->scriptIdentity));
            }
            REQUIRE(owners.size()==4);
            map.setMaterialAmountSlot(target,WHEAT,3);
            map.revalidateMaterialClaimOwners(owners);
            CHECK(claimsAt(map,target)==3);
            std::sort(owners.begin(),owners.end());
            for(std::size_t i=0;i<owners.size();++i)CHECK((map.materialClaimLedger().find(owners[i])!=nullptr)==(i<3));
        }
    }
    TEST_CASE("pending gradient step moves the worker while its route computes and stays deterministic")
    {
        glob2test::HeadlessGlobals globals;
        std::vector<std::vector<int>> traces;
        for(unsigned threads:{1u,2u}) for(bool text:{false,true}) {
            CAPTURE(threads);CAPTURE(text);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;
            map.computeExecutor().configure(threads);map.enableResourceClaims();
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            Assignment cleanup{*unit};assign(*unit,*building,map,14,8);
            REQUIRE(glob2test::awaitGradient(game,[&]{return map.routing().getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
            unit->materialClaimPolicyState.decisionOrdinal=1;game.invalidateObservation();game.observeCompletedWorld();
            const auto target=unit->activeMaterialClaim->access;
            const auto distance=[&](int x,int y){return map.warpDistMax(x,y,int(target%map.getW()),int(target/map.getW()));};
            const auto move=[&](int dx,int dy){map.setGroundUnit(unit->posX,unit->posY,NOGUID);unit->posX=(unit->posX+dx+map.getW())%map.getW();unit->posY=(unit->posY+dy+map.getH())%map.getH();map.setGroundUnit(unit->posX,unit->posY,unit->gid);};
            const auto roundtrip=[&]{const auto before=map.activeClaimRouteChecksum(true);const auto bytes=saveRoutes(map,text);restoreRoutes(map,bytes,text);CHECK(saveRoutes(map,text)==bytes);CHECK(map.activeClaimRouteChecksum(true)==before);};
            int dx=0,dy=0;
            // The route is still computing; the worker takes the gradient step.
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
            CHECK(map.claimRouteComputing(*unit));
            CHECK(distance(unit->posX+dx,unit->posY+dy)<distance(unit->posX,unit->posY));
            std::vector<int> trace{dx,dy};
            move(dx,dy);roundtrip();
            for(unsigned decision=0;decision<4 && distance(unit->posX,unit->posY)>1;++decision) {
                map.flushClaimRoutes();++game.stepCounter;map.publishClaimRoutes();game.invalidateObservation();game.observeCompletedWorld();
                ++unit->materialClaimPolicyState.decisionOrdinal;
                const auto result=map.nextClaimRouteStep(*unit,&dx,&dy);
                REQUIRE(result==ResourceClaims::StepResult::Ready);
                CHECK(distance(unit->posX+dx,unit->posY+dy)<distance(unit->posX,unit->posY));
                trace.insert(trace.end(),{dx,dy});move(dx,dy);roundtrip();
            }
            REQUIRE(unit->activeMaterialClaim);CHECK(unit->activeMaterialClaim->access==target);
            trace.push_back(int(map.activeClaimRouteChecksum(true)));
            traces.push_back(trace);
        }
        REQUIRE(traces.size()==4);
        for(const auto& trace:traces)CHECK(trace==traces.front());
    }
    TEST_CASE("claim distance cap rejects candidates far beyond the nearest destination")
    {
        glob2test::HeadlessGlobals globals;
        using namespace ResourceClaims::Policy;
        // Nearest destination (12,8) is held by another worker at distance 4,
        // so the cap is 4*MaxClaimDistanceFactorPermille/1000+ClaimDistanceSlackTiles
        // = 14 tiles. The only free stock lies straight south of the worker.
        static_assert(MaxClaimDistanceFactorPermille==2500 && ClaimDistanceSlackTiles==4);
        for(const auto& [freeY,admitted]:std::vector<std::pair<int,bool>>{{18,true},{22,true},{23,false},{24,false}}) {
            CAPTURE(freeY);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);map.enableResourceClaims();
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);auto* holder=fixture.addUnit(WORKER,11,11);
            Assignment cleanup{*unit},holderCleanup{*holder};
            assign(*holder,*building,map,12,8);map.setMaterialAmountSlot(Uint32(map.coordToIndex(12,8)),WHEAT,1);
            const auto free=Uint32(map.coordToIndex(8,freeY));
            assign(*unit,*building,map,8,freeY);map.setMaterialAmountSlot(free,WHEAT,1);
            unit->releaseActiveMaterialClaim();
            REQUIRE(holder->activeMaterialClaimValid());
            REQUIRE(glob2test::awaitGradient(game,[&]{return map.routing().getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
            game.invalidateObservation();game.observeCompletedWorld();
            const auto* field=map.routing().getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);REQUIRE(field);
            const auto destinations=map.routing().materialGradientDestinations(field);REQUIRE(destinations);
            REQUIRE(destinations->x[map.coordToIndex(8,8)]==12);REQUIRE(destinations->y[map.coordToIndex(8,8)]==8);
            const bool selected=unit->selectActiveMaterialClaim(field);
            CHECK(selected==admitted);
            if(selected)CHECK(unit->activeMaterialClaim->access==free);
            else CHECK_FALSE(unit->activeMaterialClaim);
            // The cap applies to selection only: the stock itself is claimable.
            if(!selected)CHECK(unit->acquireMaterialClaimAt(free));
        }
    }
    TEST_CASE("traffic sidestep and wait limit move past units blocking the next route cell")
    {
        glob2test::HeadlessGlobals globals;
        // walled: block the next route cell and both cells a sidestep could
        // use, so the worker waits TrafficWaitLimit decisions and then replans
        // around the units it sees.
        for(bool walled:{false,true}) for(bool text:{false,true}) {
            CAPTURE(walled);CAPTURE(text);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;map.enableResourceClaims();
            // Routes publish on the next tick, so a replan costs one decision.
            map.setClaimRouteDelay(1);
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            Assignment cleanup{*unit};assign(*unit,*building,map,14,8);
            unit->materialClaimPolicyState.decisionOrdinal=1;game.observeCompletedWorld();
            const auto target=unit->activeMaterialClaim->access;
            const auto distance=[&](int x,int y){return map.warpDistMax(x,y,int(target%map.getW()),int(target/map.getW()));};
            const auto move=[&](int dx,int dy){map.setGroundUnit(unit->posX,unit->posY,NOGUID);unit->posX=(unit->posX+dx+map.getW())%map.getW();unit->posY=(unit->posY+dy+map.getH())%map.getH();map.setGroundUnit(unit->posX,unit->posY,unit->gid);};
            const auto roundtrip=[&]{const auto before=map.activeClaimRouteChecksum(true);const auto bytes=saveRoutes(map,text);restoreRoutes(map,bytes,text);CHECK(saveRoutes(map,text)==bytes);CHECK(map.activeClaimRouteChecksum(true)==before);};
            const auto boundary=[&]{map.flushClaimRoutes();++game.stepCounter;map.publishClaimRoutes();game.invalidateObservation();game.observeCompletedWorld();};
            int dx=0,dy=0;
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
            boundary();
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
            REQUIRE(dx==1);
            const int blockedX=unit->posX+dx,blockedY=unit->posY+dy;
            // Ordinary (mobile) units: static routes ignore them, traffic does not.
            REQUIRE(fixture.addUnit(WORKER,blockedX,blockedY));
            if(walled)for(int y:{blockedY-1,blockedY+1})REQUIRE(fixture.addUnit(WORKER,blockedX,y));
            const auto start=distance(unit->posX,unit->posY);
            unsigned ready=0,waits=0;
            for(unsigned decision=0;decision<32 && distance(unit->posX,unit->posY)>1;++decision) {
                boundary();++unit->materialClaimPolicyState.decisionOrdinal;
                const auto result=map.nextClaimRouteStep(*unit,&dx,&dy);
                REQUIRE(result!=ResourceClaims::StepResult::Failed);
                REQUIRE(unit->activeMaterialClaim);
                if(result==ResourceClaims::StepResult::Ready) {
                    CHECK_FALSE((unit->posX+dx==blockedX && unit->posY+dy==blockedY));
                    move(dx,dy);++ready;
                } else {++waits;roundtrip();}
            }
            CHECK(distance(unit->posX,unit->posY)<=1);CHECK(distance(unit->posX,unit->posY)<start);
            if(!walled)CHECK(waits==0);
            // The last wait requests the replan; it may take a further
            // decision to publish.
            else {CHECK(waits>=ResourceClaims::Policy::TrafficWaitLimit);CHECK(waits<=ResourceClaims::Policy::TrafficWaitLimit+1);}
        }
    }

    TEST_CASE("a claim worker whose selection finds nothing gives up its job")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
        auto& game=fixture.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);map.enableResourceClaims();
        auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);auto* holder=fixture.addUnit(WORKER,11,11);
        Assignment cleanup{*unit},holderCleanup{*holder};
        // The only stock is held by another worker: selection finds nothing.
        assign(*holder,*building,map,12,8);map.setMaterialAmountSlot(Uint32(map.coordToIndex(12,8)),WHEAT,1);
        unit->attachedBuilding=building;unit->activity=Unit::ACT_FILLING;unit->displacement=Unit::DIS_GOING_TO_RESOURCE;
        unit->destinationPurpose=WHEAT;unit->medical=Unit::MED_FREE;
        REQUIRE(glob2test::awaitGradient(game,[&]{return map.routing().getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
        game.invalidateObservation();game.observeCompletedWorld();
        unit->handleActiveMaterialMovement();
        // Rather than wait on stock others hold, the worker is freed for hiring.
        CHECK_FALSE(unit->activeMaterialClaim);
        CHECK(unit->attachedBuilding==nullptr);CHECK(unit->activity==Unit::ACT_RANDOM);
        CHECK(unit->dx==0);CHECK(unit->dy==0);
        // The holder keeps its reservation.
        CHECK(holder->activeMaterialClaimValid());CHECK(claimsAt(map,Uint32(map.coordToIndex(12,8)))==1);
    }
}
