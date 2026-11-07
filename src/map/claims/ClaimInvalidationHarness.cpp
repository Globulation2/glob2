// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeadlessGlobals.h"
#include "HeadlessGame.h"
#include "ActiveMaterialClaims.h"
#include "Building.h"
#include "Unit.h"
#include <nlohmann/json.hpp>

namespace {
void assign(Unit& unit,Building& building,Map& map,int sx,int sy,int ax,int ay) {
    unit.attachedBuilding=&building;unit.activity=Unit::ACT_FILLING;
    unit.displacement=Unit::DIS_GOING_TO_RESOURCE;unit.destinationPurpose=WHEAT;unit.medical=Unit::MED_FREE;
    const Uint16 reserve=map.farmAreasEnabled() && map.isFarmArea(ax,ay,unit.owner->me)
        && !unit.owner->game->gameHeader.isResourceGrowthDisabled() ? map.resourceRegistry().yields(ResourceId(WHEAT))[WHEAT].seedReserve : 0;
    unit.activeMaterialClaim=ResourceClaims::Claim{Uint32(map.coordToIndex(sx,sy)),Uint32(map.coordToIndex(ax,ay)),WHEAT,WHEAT,false,reserve};
    ++unit.materialClaimTask;
    REQUIRE(map.acquireMaterialClaim(ResourceClaims::unitOwner(unit.gid,unit.scriptIdentity),*unit.activeMaterialClaim,100));
    REQUIRE(unit.activeMaterialClaimValid());
}
struct Detach {
    std::vector<Unit*> units;
    ~Detach(){for(auto* unit:units){unit->releaseActiveMaterialClaim();unit->attachedBuilding=nullptr;}}
};
}
TEST_SUITE("ClaimInvalidation") {
TEST_CASE("stock decreases retain valid reservations and evict only deterministic excess") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.wDec=5,.hDec=5,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& map=world.game.map;map.enableResourceClaims();
    auto* building=world.addBuilding("inn",2,2);
    map.setResourceByIndex(12,12,WHEAT,1);map.setMaterialAmount(map.coordToIndex(12,12),MaterialId::Food,5);
    Detach cleanup;
    for(int i=0;i<3;++i){auto* unit=world.addUnit(WORKER,4+i,8);cleanup.units.push_back(unit);assign(*unit,*building,map,12,12,12,12);}
    map.setMaterialAmount(map.coordToIndex(12,12),MaterialId::Food,4);
    for(auto* unit:cleanup.units)CHECK(unit->activeMaterialClaim.has_value());
    map.setMaterialAmount(map.coordToIndex(12,12),MaterialId::Food,2);
    const auto ordered=map.materialClaimLedger().ownersAtSource(Uint32(map.coordToIndex(12,12)));
    REQUIRE(ordered.size()==2);
    auto owners=std::vector<ResourceClaims::Owner>{};
    for(auto* unit:cleanup.units)owners.push_back(ResourceClaims::unitOwner(unit->gid,unit->scriptIdentity));
    std::sort(owners.begin(),owners.end());
    CHECK(ordered==std::vector<ResourceClaims::Owner>{owners[0],owners[1]});
    map.markImmobileUnit(12,12,0); // Temporary traffic never cancels pursuit.
    CHECK(map.materialClaimLedger().size()==2);
}
TEST_CASE("removing a farm bridge releases remote disconnected claims but preserves connected claims") {
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options{.wDec=5,.hDec=5,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true};
    options.experiments.set(ExperimentId::FarmAreas,true);
    glob2test::HeadlessGame world(options);auto& map=world.game.map;map.enableResourceClaims();
    auto* building=world.addBuilding("inn",2,2);
    for(int x=8;x<=12;++x){map.setResourceByIndex(x,12,WHEAT,1);map.setMaterialAmount(map.coordToIndex(x,12),MaterialId::Food,5);map.addFarmArea(x,12,0);}
    auto* remote=world.addUnit(WORKER,5,8);auto* local=world.addUnit(WORKER,6,8);Detach cleanup{{remote,local}};
    assign(*remote,*building,map,12,12,8,12);assign(*local,*building,map,12,12,12,12);
    REQUIRE(map.activeClaimFarmConnected(*remote->activeMaterialClaim,1));
    map.setMaterialAmount(map.coordToIndex(10,12),MaterialId::Food,0);
    CHECK_FALSE(remote->activeMaterialClaim);
    CHECK(local->activeMaterialClaim.has_value());
    CHECK(map.materialClaimLedger().size()==1);
}
TEST_CASE("pooled physical source paint preserves ownership while forbidden access cancels") {
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options{.wDec=5,.hDec=5,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true};
    options.experiments.set(ExperimentId::FarmAreas,true);
    glob2test::HeadlessGame world(options);auto& map=world.game.map;map.enableResourceClaims();
    auto* building=world.addBuilding("inn",2,2);
    for(int x=8;x<=12;++x){map.setResourceByIndex(x,12,WHEAT,1);map.setMaterialAmount(map.coordToIndex(x,12),MaterialId::Food,5);map.addFarmArea(x,12,0);}
    auto* unit=world.addUnit(WORKER,5,8);Detach cleanup{{unit}};
    assign(*unit,*building,map,12,12,8,12);
    const auto claim=unit->activeMaterialClaim;const auto task=unit->materialClaimTask;
    map.addForbidden(12,12,0);
    CHECK(unit->activeMaterialClaim==claim);CHECK(unit->materialClaimTask==task);
    REQUIRE(unit->activeMaterialClaimValid());
    CHECK(map.activeClaimFarmConnected(*unit->activeMaterialClaim,1));
    map.addForbidden(10,12,0); // Paint does not split the pooled stock component.
    CHECK(unit->activeMaterialClaim==claim);CHECK(unit->materialClaimTask==task);
    map.addForbidden(8,12,0);
    CHECK_FALSE(unit->activeMaterialClaim);CHECK(map.materialClaimLedger().size()==0);
}
TEST_CASE("pooled source fog loss preserves ownership until access visibility expires") {
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options{.wDec=5,.hDec=5,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true};
    options.experiments.set(ExperimentId::FarmAreas,true);
    glob2test::HeadlessGame world(options);auto& map=world.game.map;
    auto definitions=nlohmann::json::parse(map.resourceRegistry().serialize());
    definitions["resources"][WHEAT]["properties"]["visibleToHarvest"]=true;
    map.installResourceDefinitions(definitions.dump());map.enableResourceClaims();
    REQUIRE(map.resourcePropertiesByIndex(WHEAT).visibleToHarvest);
    auto* building=world.addBuilding("inn",2,2);
    for(int x=8;x<=10;++x){
        map.setResourceByIndex(x,12,WHEAT,1);map.setMaterialAmount(map.coordToIndex(x,12),MaterialId::Food,5);
        map.addFarmArea(x,12,0);map.setMapDiscovered(x,12,1);
    }
    auto* unit=world.addUnit(WORKER,5,8);Detach cleanup{{unit}};
    assign(*unit,*building,map,10,12,8,12);
    const auto claim=unit->activeMaterialClaim;const auto task=unit->materialClaimTask;
    map.switchFogOfWar(); // Retire one initially visible plane.
    map.setMapDiscovered(8,12,1);map.switchFogOfWar();
    CHECK_FALSE(map.isFOWDiscovered(10,12,1));
    CHECK(map.isFOWDiscovered(8,12,1));
    CHECK(unit->activeMaterialClaim==claim);CHECK(unit->materialClaimTask==task);
    map.switchFogOfWar();
    CHECK_FALSE(map.isFOWDiscovered(8,12,1));
    CHECK_FALSE(unit->activeMaterialClaim);CHECK(map.materialClaimLedger().size()==0);
}
TEST_CASE("forbidden endpoints and changed global harvest rules invalidate claims without touching replacement owners") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.wDec=5,.hDec=5,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& map=world.game.map;map.enableResourceClaims();auto* building=world.addBuilding("inn",2,2);
    map.setResourceByIndex(12,12,WHEAT,1);map.setMaterialAmount(map.coordToIndex(12,12),MaterialId::Food,5);
    auto* unit=world.addUnit(WORKER,5,8);Detach cleanup{{unit}};
    assign(*unit,*building,map,12,12,12,12);
    const auto stale=ResourceClaims::unitOwner(unit->gid,unit->scriptIdentity+1);
    map.recordMaterialClaim(stale,*unit->activeMaterialClaim);
    map.invalidateMaterialClaimOwners({stale});
    CHECK(unit->activeMaterialClaim.has_value());
    map.addForbidden(12,12,0);CHECK_FALSE(unit->activeMaterialClaim);
    map.removeForbidden(12,12,0);assign(*unit,*building,map,12,12,12,12);
    world.game.gameHeader.setResourceGrowthDisabled(true);map.checkActiveClaimRules();
    CHECK_FALSE(unit->activeMaterialClaim);CHECK(map.materialClaimLedger().size()==0);
}
}
