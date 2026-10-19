// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientRuntime.h"
#include "BuildingType.h"
#include <nlohmann/json.hpp>

namespace {
void marketCatalog(Game& game,bool overlay=false,bool shared=false)
{
    auto catalog=nlohmann::json::parse(game.buildingsTypes.snapshotJson());
    auto& variant=catalog["variants"][game.buildingsTypes.getFinishedTypeNum("inn")];
    auto& semantics=variant["semantics"];
    semantics["occupiesGround"]=!overlay;
    semantics["market"]["suppliesStock"]=true;
    semantics["market"]["suppliesStockExperiment"]="";
    semantics["market"]["suppliesStockMaterials"]={"food"};
    semantics["market"]["sharedStock"]=shared;
    variant["properties"]["maxMaterial"][WHEAT]=20;
    game.buildingsTypes.loadSnapshotJson(catalog.dump());game.configureBuildingCatalog();
}
GradientFieldKey marketKey() {return {GradientFieldFamily::Markets,0,0,WHEAT};}
std::uint64_t footprint(const Building& building) {return building.type->width*building.type->height;}
void initialize(Map& map) {map.gradientRuntime->adaptive.enroll(marketKey());map.ensureGradientMarketTargets(0,WHEAT);}
auto counts(Map& map) {return map.gradientRuntime->adaptive.currentTargets(marketKey());}
}
TEST_SUITE("GradientMarketTurnover") {
TEST_CASE("real delivery updates zero to positive membership after stock mutation") {
    glob2test::HeadlessGlobals globals;glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
    marketCatalog(world.game);auto* supplier=world.addBuilding("inn",8,8);supplier->setMaterial(WHEAT,0);initialize(world.game.map);
    CHECK(counts(world.game.map).added==0);const auto delivery=supplier->deliverMaterialPacket(WHEAT,{});(void)delivery;
    REQUIRE(supplier->availableMaterial(WHEAT)>0);CHECK(counts(world.game.map).added==footprint(*supplier));
    world.game.map.dirtyMarketGradientsSlot(0,WHEAT);CHECK(counts(world.game.map).added==footprint(*supplier));
    supplier->launchDelete();CHECK(counts(world.game.map).removed==footprint(*supplier));
    supplier->cancelDelete();CHECK(counts(world.game.map).added==2*footprint(*supplier));
}
TEST_CASE("overlapping overlay sources count union rather than supplier transitions") {
    glob2test::HeadlessGlobals globals;glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
    marketCatalog(world.game,true);auto* first=world.addBuilding("inn",8,8);auto* second=world.addBuilding("inn",8,8);
    first->setMaterial(WHEAT,5);second->setMaterial(WHEAT,0);initialize(world.game.map);
    second->deliverMaterialPacket(WHEAT,{});REQUIRE(second->availableMaterial(WHEAT)>0);CHECK(counts(world.game.map).added==0);
    first->launchDelete();CHECK(counts(world.game.map).removed==0);
    second->launchDelete();CHECK(counts(world.game.map).removed==footprint(*second));
}
TEST_CASE("shared pool transition updates every eligible footprint and stock registration is observable") {
    glob2test::HeadlessGlobals globals;glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true});
    marketCatalog(world.game,false,true);auto* first=world.addBuilding("inn",4,4);auto* second=world.addBuilding("inn",16,16);
    first->setMaterial(WHEAT,0);initialize(world.game.map);first->deliverMaterialPacket(WHEAT,{});
    REQUIRE(second->availableMaterial(WHEAT)>0);const auto both=footprint(*first)+footprint(*second);CHECK(counts(world.game.map).added==both);
    // Construction completion registers an already stocked ALIVE provider via
    // this same capability-list operation, after earlier pointer/type changes.
    world.team->removeFromAbilitiesLists(second);CHECK(counts(world.game.map).removed==footprint(*second));
    world.team->addToStaticAbilitiesLists(second);CHECK(counts(world.game.map).added==both+footprint(*second));
    world.game.map.dirtyMarketGradientsSlot(0,WHEAT);CHECK(counts(world.game.map).added==both+footprint(*second));
}
}
