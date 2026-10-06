// SPDX-License-Identifier: GPL-3.0-or-later
#include "Material.h"
#include "EngineFixtures.h"
#include "BuildingType.h"
#include "Bullet.h"
#include "Sector.h"
#include "Version.h"
#include "Order.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <memory>

namespace
{
void productionCatalog(Game& game, bool instant = false)
{
    auto json = nlohmann::json::parse(game.buildingsTypes.snapshotJson());
    auto& b = json["variants"][1];
    auto& production = b["semantics"]["production"];
    production["scheduling"] = "weighted_committed_job";
    production["recipes"] = {
        {"worker", {{"enabled", true}, {"duration", instant ? 0 : 1}, {"cost", {{"food", 2}}}}},
        {"warrior", {{"enabled", true}, {"duration", 3}, {"cost", {{"wood", 3}}}}}};
    b["properties"]["maxMaterial"][WOOD] = 20;
    game.buildingsTypes.loadSnapshotJson(json.dump());
    game.configureBuildingCatalog();
}
std::string saveProductionGame(Game& game, bool text)
{
    auto* bytes = new GAGCore::MemoryStreamBackend;
    std::unique_ptr<GAGCore::OutputStream> out(text
        ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(bytes))
        : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(bytes)));
    game.save(out.get(), false, "Production capabilities");
    out->flush(); return bytes->takeContents();
}
bool loadProductionGame(Game& game, const std::string& bytes, bool text)
{
    auto* storage = new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size());
    storage->seekFromStart(0);
    std::unique_ptr<GAGCore::InputStream> input(text
        ? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(storage))
        : static_cast<GAGCore::InputStream*>(new GAGCore::BinaryInputStream(storage)));
    return game.load(input.get());
}
}

TEST_SUITE("BuildingProductionCombat")
{
TEST_CASE("committed recipes reserve distinct resources and ratio changes choose the next job")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    productionCatalog(world.game);
    Building* b = world.addBuilding("swarm", 8, 8);
    b->materials[WHEAT] = b->materials[WOOD] = 10;
    b->swarmStep();
    CHECK(b->productionUnit == WORKER);
    CHECK(b->productionTimeout == 0);
    CHECK(b->materials[WHEAT] == 10);
    CHECK(b->availableMaterial(WHEAT) == 8);
    b->ratio[WORKER] = 0; b->ratio[WARRIOR] = 1;
    b->swarmStep();
    CHECK(world.team->stats.measurements.births[WORKER] == 1);
    CHECK(b->materials[WHEAT] == 8);
    CHECK(b->productionUnit == -1);
    b->swarmStep();
    CHECK(b->productionUnit == WARRIOR);
    CHECK(b->productionTimeout == 2);
    CHECK(b->availableMaterial(WOOD) == 7);
    b->cancelProduction();
    CHECK(b->materials[WOOD] == 10);
    CHECK(b->availableMaterial(WOOD) == 10);
    CHECK(world.team->stats.measurements.births[EXPLORER] == 0);
}

TEST_CASE("completed blocked job retains its budget and zero duration remains a producer")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    productionCatalog(world.game, true);
    Building* b = world.addBuilding("swarm", 8, 8);
    b->materials[WHEAT] = 2;
    world.team->addToStaticAbilitiesLists(b);
    REQUIRE(std::find(world.team->swarms.begin(), world.team->swarms.end(), b) != world.team->swarms.end());
    for (int y=7; y<=12; ++y) for (int x=7; x<=12; ++x)
        if (x==7 || x==12 || y==7 || y==12) world.game.map.setResourceByIndex(x,y,STONE,1);
    for (int tick=0; tick<5; ++tick) b->swarmStep();
    CHECK(b->productionUnit == WORKER);
    CHECK(b->productionTimeout == -1);
    CHECK(b->materials[WHEAT] == 2);
    CHECK(b->availableMaterial(WHEAT) == 0);
    CHECK(world.team->stats.measurements.births[WORKER] == 0);
    b->launchDelete();
    CHECK(b->productionUnit == -1);
    CHECK(b->availableMaterial(WHEAT) == 2);
}

TEST_CASE("committed recipe survives binary and text continuation with its reservation")
{
    glob2test::HeadlessGlobals globals;
    for (bool text : {false, true})
    {
        glob2test::HeadlessGame world({.loadDefaultRace=true, .header=true});
        productionCatalog(world.game);
        Building* b = world.addBuilding("swarm",8,8);
        b->ratio[WORKER]=0; b->ratio[WARRIOR]=1;
        b->materials[WOOD]=10;
        b->swarmStep();
        const int gid=b->gid;
        glob2test::HeadlessGame restored({.loadDefaultRace=true, .header=true});
        REQUIRE(loadProductionGame(restored.game, saveProductionGame(world.game,text),text));
        // Loading replaces the fixture's original Team pointer.
        Building* copy=restored.game.teams[0]->myBuildings[Building::GIDtoID(gid)];
        REQUIRE(copy);
        CHECK(copy->productionUnit == WARRIOR);
        CHECK(copy->productionTimeout == b->productionTimeout);
        CHECK(copy->availableMaterial(WOOD) == b->availableMaterial(WOOD));
        for (int tick=0; tick<3; ++tick) { b->swarmStep(); copy->swarmStep(); }
        CHECK(copy->productionUnit == b->productionUnit);
        CHECK(copy->materials[WOOD] == 7);
        CHECK(copy->materials[WOOD] == b->materials[WOOD]);
        CHECK(copy->owner->stats.measurements.births[WARRIOR] == 1);
    }
}

TEST_CASE("rectangular hybrid shoots class-specific snapshots and regenerates without production")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.teams=2, .loadDefaultRace=true});
    auto json = nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& inn=json["variants"][3];
    inn["properties"]["width"]=3; inn["properties"]["height"]=1;
    inn["properties"]["shootingRange"]=5; inn["properties"]["shootSpeed"]=1024;
    inn["properties"]["shootRhythm"]=65535; inn["properties"]["maxBullets"]=10;
    inn["properties"]["multiplierStoneToBullets"]=2;
    inn["semantics"]["projectileDamage"]={0,9,17};
    inn["semantics"]["projectileBuildingDamage"]=0;
    inn["semantics"]["ammunitionMaterial"]=WOOD;
    inn["semantics"]["ammunitionCost"]=2;
    inn["semantics"]["regenerationPerTick"]=3;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("inn",8,8);
    b->hp-=10; const int hp=b->hp; b->regenerationStep(); CHECK(b->hp==hp+3);
    b->materials[WOOD]=2;
    Unit* enemy=world.addUnit(EXPLORER,14,9,1);
    enemy->speed=1; enemy->delta=0;
    b->turretStep(0); b->turretStep(1); b->turretStep(2);
    CHECK(b->materials[WOOD]==0); CHECK(b->bullets==1);
    Sector* sector=world.game.map.getSector(b->getMidX(),b->getMidY());
    REQUIRE(!sector->bullets.empty());
    Bullet* shot=sector->bullets.front();
    CHECK(shot->unitDamage[WORKER]==0); CHECK(shot->unitDamage[EXPLORER]==9); CHECK(shot->unitDamage[WARRIOR]==17);
    CHECK(shot->shootDamage==0);
    auto* storage=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(storage); shot->save(&output);
    auto* duplicate=new GAGCore::MemoryStreamBackend(*storage); duplicate->seekFromStart(0);
    GAGCore::BinaryInputStream input(duplicate); Bullet loaded(&input,VERSION_MINOR);
    CHECK(loaded.unitDamage==shot->unitDamage);
    const int before=enemy->hp;
    const int damage=std::max(1,9-enemy->getRealArmor(false));
    b->kill(); shot->ticksLeft=0; sector->step();
    CHECK(enemy->hp==before-damage);
}
TEST_CASE("hybrid attraction and service routes remain independent across save continuation")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true, .header=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& inn=json["variants"][3];
    inn["properties"]["zonable"]={1,1,1};
    inn["properties"]["defaultUnitStayRange"]=6;
    inn["properties"]["maxUnitStayRange"]=10;
    inn["semantics"]["assignmentLimit"]=3;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("inn",8,8);
    b->maxUnitWorking=10;
    for (int resource=0; resource<MaterialCount; ++resource) b->materials[resource]=b->type->maxMaterial[resource];
    b->update();
    world.game.map.setResourceByIndex(12,8,WOOD,1);
    Unit* worker=world.addUnit(WORKER,6,8);
    Unit* explorer=world.addUnit(EXPLORER,6,9);
    Unit* warrior=world.addUnit(WARRIOR,6,10);
    for (int tick=0; tick<33; ++tick) b->subscribeWorkStep();
    CHECK(b->desiredMaxUnitWorking==3);
    REQUIRE(b->unitsWorking.size()==3);
    CHECK(worker->activity==Unit::ACT_FLAG);
    CHECK(explorer->activity==Unit::ACT_FLAG);
    CHECK(warrior->activity==Unit::ACT_FLAG);
    auto& map=world.game.map;
    const Uint16* services=map.buildingGradient(b,0,BuildingRoute::Footprint);
    const Uint16* clearing=map.buildingGradient(b,0,BuildingRoute::Clearing);
    const Uint16* combat=map.buildingGradient(b,0,BuildingRoute::Combat);
    REQUIRE(services); REQUIRE(clearing); REQUIRE(combat);
    CHECK(services!=clearing); CHECK(clearing!=combat);
    CHECK(services[map.coordToIndex(12,8)]==GRADIENT_FORBIDDEN);
    CHECK(clearing[map.coordToIndex(12,8)]==GRADIENT_AT_GOAL);
    CHECK(combat[map.coordToIndex(11,8)]==GRADIENT_AT_GOAL);
    const int gid=b->gid;
    for (bool text : {false,true})
    {
        glob2test::HeadlessGame restored({.loadDefaultRace=true, .header=true});
        REQUIRE(loadProductionGame(restored.game,saveProductionGame(world.game,text),text));
        Building* copy=restored.game.teams[0]->myBuildings[Building::GIDtoID(gid)];
        REQUIRE(copy);
        for (BuildingRoute route : {BuildingRoute::Footprint,BuildingRoute::Clearing,BuildingRoute::Combat})
        {
            const int slot=b->routeSlot(0,route);
            REQUIRE(copy->globalGradient[slot]);
            CHECK(std::equal(b->globalGradient[slot],b->globalGradient[slot]+map.getW()*map.getH(),copy->globalGradient[slot]));
            CHECK(copy->lastGlobalGradientUpdateStepCounter[slot]==b->lastGlobalGradientUpdateStepCounter[slot]);
        }
    }
}

TEST_CASE("stock suppliers route independently of fruit exchange with configured pickup cost")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& market=json["variants"][3]["semantics"]["market"];
    market["suppliesStock"]=true; market["suppliesStockExperiment"]="";
    market["interTeamFruitExchange"]=false; market["pickupPenalty"]=11;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("inn",8,8);
    b->materials[WHEAT]=5;
    CHECK_FALSE(b->type->canExchange);
    REQUIRE(world.team->stockSuppliers.size()==1);
    const Uint16* field=world.game.map.getMaterialGradientSlot(0,WHEAT,0,true);
    CHECK(field[world.game.map.coordToIndex(8,8)]==GRADIENT_AT_GOAL-11*GRADIENT_STEP);
}

TEST_CASE("overlay services and suppliers share a footprint without occupying ground")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& semantics=json["variants"][3]["semantics"];
    semantics["occupiesGround"]=false;
    semantics["market"]["suppliesStock"]=true;
    semantics["market"]["suppliesStockExperiment"]="";
    semantics["market"]["pickupPenalty"]=7;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.game.addBuilding(8,8,world.game.buildingsTypes.getFinishedTypeNum("inn"),0);
    REQUIRE(b);
    b->materials[WHEAT]=5;
    CHECK(world.game.map.getBuilding(8,8)==NOGBID);
    CHECK(world.game.map.doesPosTouchBuilding(7,8,b->gid).has_value());
    CHECK_FALSE(world.game.map.doesPosTouchBuilding(3,3,b->gid).has_value());
    const Uint16* field=world.game.map.getMaterialGradientSlot(0,WHEAT,0,true);
    CHECK(field[world.game.map.coordToIndex(8,8)]==GRADIENT_AT_GOAL-7*GRADIENT_STEP);
    Unit* worker=world.addUnit(WORKER,7,8);
    CHECK(world.game.map.touchedStockedMarketSlot(worker,WHEAT)==b);
}

TEST_CASE("combined attraction profiles independently require warrior level and explorer bombing")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    json["variants"][3]["properties"]["zonable"]={1,1,1};
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("inn",8,8);
    Unit* explorer=world.addUnit(EXPLORER,6,8);
    Unit* warrior=world.addUnit(WARRIOR,6,9);
    Unit* worker=world.addUnit(WORKER,6,10);
    b->minLevelToFlag=2;
    CHECK(b->canUnitWorkHere(explorer,true));
    CHECK_FALSE(b->canUnitWorkHere(warrior,true));
    CHECK(b->canUnitWorkHere(worker,true));
    b->minWorkerLevelToFlag=1;
    CHECK_FALSE(b->canUnitWorkHere(worker,true));
    worker->constructionLevel=1;
    CHECK(b->canUnitWorkHere(worker,true));
    b->explorersRequireBombing=true;
    CHECK_FALSE(b->canUnitWorkHere(explorer,true));
    explorer->level[MAGIC_ATTACK_GROUND]=1;
    CHECK(b->canUnitWorkHere(explorer,true));
}

TEST_CASE("physical combat attraction invalidates on enemy changes and mixed relocation resets routes")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& inn=json["variants"][3];
    inn["properties"]["zonable"]={1,0,1};
    inn["properties"]["zonableForbidden"]=1;
    inn["properties"]["defaultUnitStayRange"]=4;
    inn["properties"]["maxUnitStayRange"]=8;
    inn["semantics"]["relocatable"]=true;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("inn",8,8);
    world.team->addToStaticAbilitiesLists(b);
    REQUIRE(world.game.map.buildingGradient(b,0,BuildingRoute::Combat));
    world.team->dirtyWarFlagGradient();
    CHECK(b->globalGradient[b->routeSlot(0,BuildingRoute::Combat)]==nullptr);
    REQUIRE(world.game.map.buildingGradient(b,0,BuildingRoute::Footprint));
    auto move=std::make_shared<OrderMoveFlag>(b->gid,16,16,false);
    move->sender=0; world.game.executeOrder(move,0);
    CHECK(b->posX==16);
    CHECK(world.game.map.getBuilding(8,8)==NOGBID);
    CHECK(world.game.map.getBuilding(16,16)==b->gid);
    CHECK(b->globalGradient[b->routeSlot(0,BuildingRoute::Footprint)]==nullptr);
    CHECK(world.team->integrity());
}

TEST_CASE("direct stock withdrawal is independent of food and fruit exchange")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& source=json["variants"][3];
    auto& sink=json["variants"][9];
    source["semantics"]["market"]["suppliesDirectStock"]=true;
    source["properties"]["maxMaterial"][WOOD]=10;
    source["semantics"]["market"]["suppliesDirectStockMaterials"]={"wood"};
    sink["semantics"]["market"]["fetchesDirectStock"]=true;
    sink["semantics"]["market"]["fetchesStock"]=false;
    sink["properties"]["maxMaterial"][WOOD]=10;
    sink["semantics"]["replenishMaterials"]={"wood"};
    sink["semantics"]["market"]["fetchesDirectStockMaterials"]={"wood"};
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* supplier=world.addBuilding("inn",8,8);
    Building* consumer=world.addBuilding("hospital",16,16);
    supplier->materials[WOOD]=5;
    CHECK_FALSE(supplier->type->canExchange);
    CHECK_FALSE(consumer->type->canFeedUnit);
    CHECK(supplier->type->runtimeSuppliesDirectStock);
    CHECK(consumer->type->runtimeFetchesDirectStock);
    Unit* worker=world.addUnit(WORKER,7,8);
    worker->attachedBuilding=consumer;
    worker->ownExchangeBuilding=supplier;
    worker->setTargetBuilding(supplier);
    worker->activity=Unit::ACT_FILLING;
    worker->displacement=Unit::DIS_FILLING_BUILDING;
    worker->destinationPurpose=WOOD;
    worker->delta=UNIT_DELTA_MAX;
    worker->action=STOP_WALK; worker->speed=1;
    worker->syncStep();
    CHECK(worker->carriedMaterial==WOOD);
    CHECK(supplier->materials[WOOD]==4);
    CHECK(worker->targetBuilding==consumer);
    worker->ownExchangeBuilding=nullptr;
    worker->attachedBuilding=nullptr;
    worker->setTargetBuilding(nullptr);
}

TEST_CASE("construction reserves independent costs beyond storage and survives continuation")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& site=json["variants"][2];
    site["properties"]["maxMaterial"][WOOD]=0;
    site["semantics"]["replenishMaterials"]=nlohmann::json::array();
    site["semantics"]["constructionCost"]={{"wood",3}};
    site["semantics"]["market"]["sharedStock"]=true;
    json["variants"][3]["semantics"]["market"]["sharedStock"]=true;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.game.addBuilding(8,8,2,0,3,1);
    REQUIRE(b);
    b->addMaterialIntoBuilding(WOOD);
    CHECK(b->constructionReserved[WOOD]==1);
    CHECK(b->materials[WOOD]==1);
    CHECK(b->availableMaterial(WOOD)==0);
    CHECK(b->materialDeliveryNeed(WOOD)==2);
    BuildingMaterialCost competing{}; competing[WOOD]=1;
    CHECK_FALSE(b->reserveMaterials(competing));
    const auto binary=saveProductionGame(world.game,false);
    glob2test::HeadlessGame continued({.loadDefaultRace=true});
    REQUIRE(loadProductionGame(continued.game,binary,false));
    Building* loaded=continued.game.teams[0]->myBuildings[Building::GIDtoID(b->gid)];
    REQUIRE(loaded);
    CHECK(loaded->constructionReserved==b->constructionReserved);
    CHECK(loaded->availableMaterial(WOOD)==0);
    for (Building* job : {b,loaded})
    {
        job->addMaterialIntoBuilding(WOOD);
        job->addMaterialIntoBuilding(WOOD);
        CHECK_FALSE(job->type->isBuildingSite);
        CHECK(job->materials[WOOD]==0);
        CHECK(job->reservedMaterials[WOOD]==0);
    }
}

TEST_CASE("converging upgrades cancel to the instance origin and return real materials")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    json["variants"][9]["next"]="inn.1.site";
    json["variants"][4]["semantics"]["constructionCost"]={{"wood",4}};
    json["variants"][4]["semantics"]["production"]["scheduling"]="weighted_committed_job";
    json["variants"][4]["semantics"]["production"]["recipes"]={{"worker",{{"enabled",true},{"duration",10},{"cost",{{"food",1}}}}}};
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("hospital",8,8);
    const int origin=b->typeNum;
    b->launchConstruction(1,1);
    REQUIRE(b->getConstructionOriginTypeNum()==origin);
    REQUIRE(b->tryToBuildingSiteRoom());
    REQUIRE(b->type->key=="inn.1.site");
    b->addMaterialIntoBuilding(WOOD);
    CHECK(b->constructionReserved[WOOD]==1);
    b->materials[WHEAT]=2;
    b->swarmStep();
    REQUIRE(b->productionUnit==WORKER);
    REQUIRE(b->reservedMaterials[WHEAT]==1);
    b->cancelConstruction(0);
    CHECK(b->productionUnit==-1);
    CHECK(b->materials[WHEAT]==2);
    CHECK(b->reservedMaterials[WHEAT]==0);
    CHECK(b->typeNum==origin);
    CHECK(b->materials[WOOD]==1);
    CHECK(b->reservedMaterials[WOOD]==0);
    CHECK(b->getConstructionOriginTypeNum()==-1);
}

TEST_CASE("repair budgets use independent fruit costs and cancellation creates no material credits")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    json["variants"][3]["semantics"]["repairCost"]={{"cherries",4}};
    json["variants"][2]["properties"]["materialMultiplier"][CHERRY]=1;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("inn",8,8);
    b->hp=b->getEffectiveMaxHp()/2;
    int needed[MaterialCount]{};
    b->getMaterialCountToRepair(needed);
    CHECK(needed[CHERRY]==2);
    b->launchConstruction(1,1);
    REQUIRE(b->tryToBuildingSiteRoom());
    CHECK(b->constructionBudget[CHERRY]==2);
    CHECK(b->materials[WOOD]==0);
    b->addMaterialIntoBuilding(CHERRY);
    CHECK(b->constructionReserved[CHERRY]==1);
    b->cancelConstruction(1);
    CHECK(b->type->key=="inn.0.finished");
    CHECK(b->materials[CHERRY]==0);
    CHECK(b->hp==150);
    CHECK(b->materials[WOOD]==0);
    CHECK(b->reservedMaterials[CHERRY]==0);
}

TEST_CASE("zero cost roots and upgrade stages complete without a delivery event")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    json["variants"][2]["semantics"]["constructionCost"]=nlohmann::json::object();
    json["variants"][4]["semantics"]["constructionCost"]=nlohmann::json::object();
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.game.addBuilding(8,8,2,0,1,1);
    REQUIRE(b);
    b->step();
    REQUIRE(b->type->key=="inn.0.finished");
    CHECK(b->hp==b->getEffectiveInitHp());
    b->launchConstruction(1,1);
    REQUIRE(b->tryToBuildingSiteRoom());
    b->step();
    CHECK(b->type->key=="inn.1.finished");
    CHECK(b->hp==b->getEffectiveInitHp());
}

TEST_CASE("version135 partial repair imports remaining work without material credits")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    const auto bytes=glob2test::readFile(glob2test::inflated("building-catalog/partial-repair135.game.gz"));
    REQUIRE(loadProductionGame(world.game,bytes,false));
    Building* b=world.game.teams[0]->myBuildings[0];
    REQUIRE(b);
    CHECK(b->type->key=="inn.0.site"); CHECK(b->hp==166);
    CHECK(b->constructionBudget[WOOD]==1); CHECK(b->constructionReserved[WOOD]==0);
    CHECK(b->materials[WOOD]==0); CHECK(b->materialDeliveryNeed(WOOD)==1);
    glob2test::HeadlessGame resumed({.loadDefaultRace=true});
    REQUIRE(loadProductionGame(resumed.game,saveProductionGame(world.game,false),false));
    Building* copy=resumed.game.teams[0]->myBuildings[0];
    REQUIRE(copy);
    b->addMaterialIntoBuilding(WOOD); copy->addMaterialIntoBuilding(WOOD);
    CHECK(b->type->key=="inn.0.finished"); CHECK(copy->type->key==b->type->key);
    CHECK(b->hp==200); CHECK(copy->hp==b->hp); CHECK(copy->materials[WOOD]==0);
}

TEST_CASE("partial supplier packets preserve raw equivalents and save continuation")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    json["variants"][3]["properties"]["maxMaterial"][WOOD]=20;
    json["variants"][3]["properties"]["materialMultiplier"][WOOD]=4;
    json["variants"][9]["properties"]["maxMaterial"][WOOD]=20;
    json["variants"][9]["properties"]["materialMultiplier"][WOOD]=3;
    json["variants"][9]["semantics"]["replenishMaterials"]={"wood"};
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* source=world.addBuilding("inn",8,8);
    Building* sink=world.addBuilding("hospital",16,16);
    source->materials[WOOD]=2;
    const MaterialPacket packet=source->withdrawMaterialPacket(WOOD);
    CHECK(packet.numerator==1); CHECK(packet.denominator==2); CHECK(source->materials[WOOD]==0);
    Unit* courier=world.addUnit(WORKER,7,7); courier->receiveCarriedMaterial(WOOD,packet);
    glob2test::HeadlessGame copy({.loadDefaultRace=true});
    REQUIRE(loadProductionGame(copy.game,saveProductionGame(world.game,false),false));
    Unit* loaded=copy.game.teams[0]->myUnits[Unit::GIDtoID(courier->gid)];
    REQUIRE(loaded); CHECK(loaded->carriedPacket.numerator==1); CHECK(loaded->carriedPacket.denominator==2);
    Building* copiedSink=copy.game.teams[0]->myBuildings[Building::GIDtoID(sink->gid)];
    for (auto [receiver,cargo] : {std::pair{sink,packet},std::pair{copiedSink,loaded->carriedPacket}})
    {
        const auto delivered=receiver->deliverMaterialPacket(WOOD,cargo);
        CHECK(delivered.acceptedStock==1); CHECK(receiver->materials[WOOD]==1);
        CHECK(delivered.discardedNumerator==1); CHECK(delivered.discardedDenominator==6);
        // 1/2 raw = 1 stock / 3 stock-per-raw + 1/6 deliberately discarded.
        CHECK(Uint64(cargo.numerator)*3*delivered.discardedDenominator
            == Uint64(cargo.denominator)*(Uint64(delivered.acceptedStock)*delivered.discardedDenominator+3*delivered.discardedNumerator));
        CHECK(receiver->owner->stats.measurements.materialSpillageEvents==1);
        const auto fractional=receiver->deliverMaterialPacket(WOOD,{1,10});
        CHECK(fractional.acceptedStock==0); CHECK(fractional.discardedNumerator==1); CHECK(fractional.discardedDenominator==10);
    }
}

TEST_CASE("repair earns only original damage and paid materials cannot be refunded after healing")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    json["variants"][3]["semantics"]["repairCost"]={{"wood",4}};
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("inn",8,8); b->hp=100;
    b->launchConstruction(1,1); REQUIRE(b->tryToBuildingSiteRoom());
    b->addMaterialIntoBuilding(WOOD); CHECK(b->hp==150);
    b->hp-=30;
    b->addMaterialIntoBuilding(WOOD);
    CHECK(b->type->key=="inn.0.finished"); CHECK(b->hp==170);
    CHECK(b->materials[WOOD]==0); CHECK(b->reservedMaterials[WOOD]==0);
    b->hp=100;
    b->launchConstruction(1,1); REQUIRE(b->tryToBuildingSiteRoom());
    b->addMaterialIntoBuilding(WOOD); CHECK(b->hp==150);
    b->cancelConstruction(1);
    CHECK(b->hp==150); CHECK(b->materials[WOOD]==0);
    CHECK(b->reservedMaterials[WOOD]==0);
}

TEST_CASE("zero cost repair preserves damage received after the repair started")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    json["variants"][3]["semantics"]["repairCost"]=nlohmann::json::object();
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("inn",8,8); b->hp=100;
    b->launchConstruction(1,1); REQUIRE(b->tryToBuildingSiteRoom());
    b->hp-=20; b->step();
    CHECK(b->type->key=="inn.0.finished"); CHECK(b->hp==180);
}

TEST_CASE("shared repair sites restore each recorded origin and staffing cap")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    const int inn=world.game.buildingsTypes.findByKey("inn.0.finished");
    const int hospital=world.game.buildingsTypes.findByKey("hospital.0.finished");
    const int site=world.game.buildingsTypes.findByKey("inn.0.site");
    for (int origin : {inn,hospital})
    {
        auto& variant=json["variants"][origin];
        variant["previous"]="inn.0.site";
        variant["semantics"]["repairCost"]={{"wood",4}};
        variant["semantics"]["assignmentLimit"]=origin==inn ? 1 : 7;
        variant["presentation"]["defaultAssigned"]=1;
        variant["properties"]["hpMax"]=origin==inn ? 200 : 400;
        variant["properties"]["hpInit"]=origin==inn ? 200 : 400;
    }
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* a=world.addBuilding("inn",8,8);
    Building* b=world.addBuilding("hospital",20,20);
    for (Building* building : {a,b})
    {
        const int origin=building->typeNum;
        const int maxHp=building->getEffectiveMaxHp();
        const int staffing=building->type->semantics.assignmentLimit;
        building->hp=maxHp/2;
        building->launchConstruction(1,staffing);
        REQUIRE(building->tryToBuildingSiteRoom());
        REQUIRE(building->typeNum==site);
        CHECK(building->getEffectiveMaxHp()==maxHp);
        CHECK(building->getConstructionCompletionTypeNum()==origin);
        building->addMaterialIntoBuilding(WOOD);
        CHECK(building->hp==maxHp*3/4);
    }
    glob2test::HeadlessGame resumed({.loadDefaultRace=true});
    REQUIRE(loadProductionGame(resumed.game,saveProductionGame(world.game,false),false));
    for (Building* building : {a,b})
    {
        const int origin=building->getConstructionOriginTypeNum();
        Building* copy=resumed.game.teams[0]->myBuildings[Building::GIDtoID(building->gid)];
        REQUIRE(copy);
        building->addMaterialIntoBuilding(WOOD); copy->addMaterialIntoBuilding(WOOD);
        CHECK(building->typeNum==origin); CHECK(copy->typeNum==origin);
        CHECK(building->hp==building->getEffectiveMaxHp()); CHECK(copy->hp==building->hp);
        CHECK(building->maxUnitWorking==building->type->semantics.assignmentLimit);
        CHECK(copy->maxUnitWorking==building->maxUnitWorking);
    }
}

TEST_CASE("unfunded new-site cooldown uses job state and counts fruit funding")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    const int site=world.game.buildingsTypes.findByKey("inn.0.site");
    json["variants"][site]["properties"]["level"]=3;
    json["variants"][site]["semantics"]["constructionCost"]={{"cherries",3}};
    json["variants"][site]["properties"]["materialMultiplier"][CHERRY]=1;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* funded=world.game.addBuilding(8,8,site,0,1,1);
    REQUIRE(funded);
    funded->addMaterialIntoBuilding(CHERRY);
    funded->kill();
    CHECK(world.team->noMoreBuildingSitesCountdown==0);
    Building* empty=world.game.addBuilding(20,20,site,0,1,1);
    REQUIRE(empty);
    empty->kill();
    CHECK(world.team->noMoreBuildingSitesCountdown==int(Team::noMoreBuildingSitesCountdownMax));
}

TEST_CASE("resource permissions separate storage replenishment and direct withdrawal")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& source=json["variants"][3]; auto& sink=json["variants"][9];
    for (auto* variant : {&source,&sink}) {
        (*variant)["properties"]["maxMaterial"][WOOD]=20;
        (*variant)["properties"]["maxMaterial"][WHEAT]=20;
        (*variant)["semantics"]["replenishMaterials"]={"wood"};
    }
    source["semantics"]["market"]["suppliesDirectStock"]=true;
    source["semantics"]["market"]["suppliesDirectStockMaterials"]={"wood"};
    sink["semantics"]["market"]["fetchesDirectStock"]=true;
    sink["semantics"]["market"]["fetchesDirectStockMaterials"]={"wood","food"};
    sink["semantics"]["market"]["fetchesStock"]=false;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* supplier=world.addBuilding("inn",8,8);
    Building* receiver=world.addBuilding("hospital",20,20);
    supplier->materials[WOOD]=30; supplier->materials[WHEAT]=10;
    CHECK(receiver->materialDeliveryNeed(WHEAT)==0);
    CHECK(receiver->deliverMaterialPacket(WHEAT,{}).acceptedStock==0);
    CHECK(receiver->materialDeliveryNeed(WOOD)==20);
    int distance=0;
    CHECK(world.game.map.materialAvailableSlot(0,WOOD,0,19,20,&distance,false,receiver));
    CHECK_FALSE(world.game.map.materialAvailableSlot(0,WHEAT,0,19,20,&distance,false,receiver));
    Unit* worker=world.addUnit(WORKER,7,8);
    receiver->maxUnitWorking=receiver->desiredMaxUnitWorking=1;
    REQUIRE(receiver->subscribeToBringMaterialsStep());
    CHECK(worker->attachedBuilding==receiver);
    CHECK(world.game.map.touchedStockedMarketSlot(worker,WOOD)==supplier);
    CHECK(world.game.map.touchedStockedMarketSlot(worker,WHEAT)==nullptr);
    worker->standardRandomActivity();
}

TEST_CASE("bounded supply cache excludes the recipient and its shared inventory and resumes eviction")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    for (int id : {1,3,9}) {
        auto& variant=json["variants"][id];
        variant["semantics"]["market"]["suppliesDirectStock"]=true;
        variant["semantics"]["market"]["suppliesDirectStockMaterials"]={"wood","food"};
        variant["semantics"]["market"]["fetchesDirectStock"]=true;
        variant["semantics"]["market"]["fetchesDirectStockMaterials"]={"wood","food"};
        variant["semantics"]["market"]["fetchesStock"]=false;
        variant["semantics"]["market"]["sharedStock"]=id!=1;
    }
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* receiver=world.addBuilding("inn",8,8);
    Building* shared=world.addBuilding("hospital",20,20);
    receiver->materials[WOOD]=receiver->materials[WHEAT]=10;
    int distance=0;
    CHECK_FALSE(world.game.map.materialAvailableSlot(0,WOOD,0,7,8,&distance,false,receiver));
    CHECK(receiver->materials==shared->materials);
    Building* local=world.addBuilding("swarm",30,30);
    local->materials[WOOD]=local->materials[WHEAT]=10;
    world.game.map.dirtyMarketGradientsSlot(0,WOOD); world.game.map.dirtyMarketGradientsSlot(0,WHEAT);
    const Uint64 oneField=Uint64(world.game.map.getW())*world.game.map.getH()*sizeof(Uint16);
    world.game.map.setMaterialRoutingCacheBudget(oneField);
    CHECK(world.game.map.materialAvailableSlot(0,WOOD,0,7,8,&distance,false,receiver));
    CHECK(world.game.map.materialAvailableSlot(0,WHEAT,0,7,8,&distance,false,receiver));
    CHECK(world.game.map.materialRoutingCacheBytes()==oneField);
    glob2test::HeadlessGame copy({.loadDefaultRace=true});
    REQUIRE(loadProductionGame(copy.game,saveProductionGame(world.game,false),false));
    Building* copied=copy.game.teams[0]->myBuildings[Building::GIDtoID(receiver->gid)];
    REQUIRE(copied);
    CHECK(copy.game.map.materialRoutingCacheBytes()==oneField);
    for (int resource : {WOOD,WHEAT,WOOD}) {
        const Uint16* expected=world.game.map.getMaterialGradientSlot(0,resource,0,false,receiver);
        const Uint16* actual=copy.game.map.getMaterialGradientSlot(0,resource,0,false,copied);
        CHECK(std::equal(expected,expected+world.game.map.getW()*world.game.map.getH(),actual));
        CHECK(world.game.map.checkSum(true)==copy.game.map.checkSum(true));
        CHECK(copy.game.map.materialRoutingCacheBytes()==oneField);
    }
}

TEST_CASE("combined stock routes include both provider modes and track direct overlay relocation")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& routed=json["variants"][3]["semantics"]["market"];
    routed["suppliesStock"]=true; routed["suppliesStockExperiment"]=""; routed["suppliesStockMaterials"]={"wood"};
    routed["suppliesDirectStock"]=false;
    auto& direct=json["variants"][9];
    direct["semantics"]["occupiesGround"]=false; direct["semantics"]["relocatable"]=true;
    direct["semantics"]["market"]["suppliesDirectStock"]=true;
    direct["semantics"]["market"]["suppliesDirectStockMaterials"]={"wood"};
    auto& recipient=json["variants"][1]["semantics"]["market"];
    recipient["fetchesStock"]=true; recipient["fetchesStockExperiment"]=""; recipient["fetchesStockMaterials"]={"wood"};
    recipient["fetchesDirectStock"]=true; recipient["fetchesDirectStockMaterials"]={"wood"};
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* a=world.addBuilding("inn",8,8); Building* b=world.addBuilding("hospital",20,20);
    Building* sink=world.addBuilding("swarm",30,30); a->materials[WOOD]=b->materials[WOOD]=5;
    auto& map=world.game.map;
    CHECK(map.materialSupplyModesSlot(sink,WOOD)==3);
    const Uint16* field=map.getMaterialGradientSlot(0,WOOD,0,false,sink);
    const Uint16 aGoal=GRADIENT_AT_GOAL-a->type->semantics.market.pickupPenalty*GRADIENT_STEP;
    const Uint16 bGoal=GRADIENT_AT_GOAL-b->type->semantics.market.pickupPenalty*GRADIENT_STEP;
    CHECK(field[map.coordToIndex(8,8)]==aGoal); CHECK(field[map.coordToIndex(20,20)]==bGoal);
    Unit* worker=world.addUnit(WORKER,19,20); worker->attachedBuilding=sink;
    CHECK(map.touchedStockedMarketSlot(worker,WOOD)==b); // warms sparse overlay index
    auto move=std::make_shared<OrderMoveFlag>(b->gid,24,24,false); move->sender=0;
    world.game.executeOrder(move,0);
    CHECK(b->posX==24); CHECK(map.touchedStockedMarketSlot(worker,WOOD)==nullptr);
    field=map.getMaterialGradientSlot(0,WOOD,0,false,sink);
    CHECK(field[map.coordToIndex(24,24)]==bGoal); CHECK(field[map.coordToIndex(20,20)]<bGoal);
    worker->attachedBuilding=nullptr;
}

TEST_CASE("shared ammunition consumption invalidates stock advertised by another building")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& supplier=json["variants"][3]["semantics"]["market"];
    supplier["sharedStock"]=true; supplier["suppliesDirectStock"]=true; supplier["suppliesDirectStockMaterials"]={"stone"};
    auto& turret=json["variants"][9]; turret["semantics"]["market"]["sharedStock"]=true;
    turret["properties"]["shootingRange"]=4; turret["properties"]["shootSpeed"]=1024;
    turret["properties"]["shootRhythm"]=65535; turret["properties"]["maxBullets"]=2;
    turret["properties"]["multiplierStoneToBullets"]=1; turret["semantics"]["projectileDamage"]={1,1,1};
    auto& sink=json["variants"][1]["semantics"]["market"];
    sink["fetchesDirectStock"]=true; sink["fetchesDirectStockMaterials"]={"stone"};
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* source=world.addBuilding("inn",8,8); Building* gun=world.addBuilding("hospital",20,20);
    Building* receiver=world.addBuilding("swarm",30,30); source->materials[STONE]=1;
    int distance=0;
    REQUIRE(world.game.map.materialAvailableSlot(0,STONE,0,29,30,&distance,false,receiver));
    gun->turretStep(0);
    CHECK(source->materials[STONE]==0);
    CHECK_FALSE(world.game.map.materialAvailableSlot(0,STONE,0,29,30,&distance,false,receiver));
}

TEST_CASE("production transitions initialize new recipes and restore canceled preferences")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    for (int id : {3,5}) {
        auto& production=json["variants"][id]["semantics"]["production"];
        production["scheduling"]="weighted_committed_job";
        production["initialRatios"]={0,0,2};
        production["recipes"]={{"warrior",{{"enabled",true},{"duration",0},{"cost",nlohmann::json::object()}}}};
        if (id==5) { production["recipes"]["worker"]={{"enabled",true},{"duration",0},{"cost",nlohmann::json::object()}}; production["initialRatios"]={3,0,2}; }
    }
    json["variants"][2]["semantics"]["constructionCost"]=nlohmann::json::object();
    json["variants"][4]["semantics"]["constructionCost"]={{"wood",1}};
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.game.addBuilding(8,8,2,0,1,1); REQUIRE(b); b->step();
    REQUIRE(b->typeNum==3); CHECK(b->runtime==world.game.buildingsTypes.getRuntime(3)); CHECK(b->ratio[WARRIOR]==2); CHECK(b->ratio[WORKER]==0);
    CHECK(b->canAffordProduction(WARRIOR));
    b->siteCompletionPending=true; CHECK_FALSE(b->canAffordProduction(WARRIOR)); b->siteCompletionPending=false;
    b->ratio[WARRIOR]=7;
    b->launchConstruction(1,1); REQUIRE(b->tryToBuildingSiteRoom());
    glob2test::HeadlessGame copy({.loadDefaultRace=true});
    REQUIRE(loadProductionGame(copy.game,saveProductionGame(world.game,false),false));
    Building* resumed=copy.game.teams[0]->myBuildings[Building::GIDtoID(b->gid)]; REQUIRE(resumed);
    CHECK(resumed->runtime==copy.game.buildingsTypes.getRuntime(resumed->typeNum));
    resumed->cancelConstruction(1); CHECK(resumed->ratio[WARRIOR]==7);
    CHECK(resumed->runtime==copy.game.buildingsTypes.getRuntime(3));
    b->addMaterialIntoBuilding(WOOD);
    CHECK(b->typeNum==5); CHECK(b->runtime==world.game.buildingsTypes.getRuntime(5)); CHECK(b->ratio[WARRIOR]==7); CHECK(b->ratio[WORKER]==3);
}

TEST_CASE("overlay transition and cancellation discard cached footprint routes")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    for (int id : {3,4}) json["variants"][id]["semantics"]["occupiesGround"]=false;
    json["variants"][4]["properties"]["width"]=4;
    json["variants"][4]["properties"]["height"]=1;
    json["variants"][4]["properties"]["decLeft"]=2;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("inn",8,8); auto& map=world.game.map;
    REQUIRE(map.buildingGradient(b,0,BuildingRoute::Footprint));
    b->launchConstruction(1,1); REQUIRE(b->tryToBuildingSiteRoom());
    CHECK(b->globalGradient[b->routeSlot(0,BuildingRoute::Footprint)]==nullptr);
    CHECK(b->runtime->width==4);
    const Uint16* site=map.buildingGradient(b,0,BuildingRoute::Footprint); REQUIRE(site);
    CHECK(site[map.coordToIndex(b->posX+3,b->posY)]==GRADIENT_AT_GOAL);
    b->cancelConstruction(1);
    CHECK(b->globalGradient[b->routeSlot(0,BuildingRoute::Footprint)]==nullptr);
    const Uint16* restored=map.buildingGradient(b,0,BuildingRoute::Footprint); REQUIRE(restored);
    CHECK(b->runtime->width==2);
    CHECK(b->posX==8); CHECK(restored[map.coordToIndex(8,8)]==GRADIENT_AT_GOAL);
    CHECK(restored[map.coordToIndex(14,8)]<GRADIENT_AT_GOAL);
}

TEST_CASE("mixed attraction borrows quota from an unavailable unit class")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& inn=json["variants"][3]; inn["properties"]["zonable"]={0,1,1};
    inn["properties"]["defaultUnitStayRange"]=5; inn["properties"]["maxUnitStayRange"]=5;
    inn["semantics"]["assignmentLimit"]=3; inn["semantics"]["replenishMaterials"]=nlohmann::json::array();
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("inn",8,8); b->maxUnitWorking=3; b->update();
    for (int i=0;i<3;++i) world.addUnit(WARRIOR,6,7+i);
    for (int tick=0;tick<33;++tick) b->subscribeWorkStep();
    CHECK(b->unitsWorking.size()==3);
    for (const Unit* unit : b->unitsWorking) CHECK(unit->typeNum==WARRIOR);
}

TEST_CASE("late choice advances the common timer and selects ratios only at completion")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& production=json["variants"][1]["semantics"]["production"];
    production["scheduling"]="weighted_late_choice";
    for (auto& recipe : production["recipes"]) { recipe["duration"]=5; recipe["cost"]={{"food",1}}; }
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("swarm",8,8); b->materials[WHEAT]=1;
    b->swarmStep(); b->swarmStep(); CHECK(b->productionTimeout==3);
    b->materials[WHEAT]=0; b->ratio[WORKER]=0; b->ratio[WARRIOR]=1;
    b->swarmStep(); CHECK(b->productionTimeout==3);
    b->materials[WHEAT]=1;
    for (int y=7;y<=12;++y) for (int x=7;x<=12;++x)
        if (x==7 || x==12 || y==7 || y==12) world.game.map.setResourceByIndex(x,y,STONE,1);
    for (int tick=0;tick<4;++tick) b->swarmStep();
    CHECK(b->productionTimeout<0); CHECK(world.team->stats.measurements.births[WARRIOR]==0);
    b->ratio[WARRIOR]=0; b->swarmStep();
    CHECK(world.team->stats.measurements.births[WORKER]==0); // fallback is also ground-blocked
    b->ratio[EXPLORER]=1; b->swarmStep();
    CHECK(world.team->stats.measurements.births[EXPLORER]==1);
    CHECK(b->materials[WHEAT]==0); CHECK(b->productionTimeout==5);
}


TEST_CASE("sole supplier demolition and cancellation preserve routing across save continuation")
{
    glob2test::HeadlessGlobals globals;
    for (bool direct : {false,true}) {
        CAPTURE(direct);
        glob2test::HeadlessGame world({.loadDefaultRace=true});
        auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        auto& source=json["variants"][3]["semantics"]["market"];
        source["suppliesStock"]=!direct; source["suppliesStockExperiment"]="";
        source["suppliesStockMaterials"]={"wood"};
        source["suppliesDirectStock"]=direct; source["suppliesDirectStockMaterials"]={"wood"};
        auto& sink=json["variants"][9]["semantics"]["market"];
        sink["fetchesStock"]=!direct; sink["fetchesStockExperiment"]="";
        sink["fetchesStockMaterials"]={"wood"};
        sink["fetchesDirectStock"]=direct; sink["fetchesDirectStockMaterials"]={"wood"};
        world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
        Building* supplier=world.addBuilding("inn",8,8);
        Building* receiver=world.addBuilding("hospital",20,20);
        supplier->materials[WOOD]=10;
        const unsigned mode=direct ? 2 : 1;
        CHECK(world.game.map.materialSupplyModesSlot(receiver,WOOD)==mode);
        REQUIRE(world.game.map.materialAvailableSlot(0,WOOD,0,19,20,false,receiver));
        supplier->launchDelete();
        REQUIRE(supplier->buildingState==Building::WAITING_FOR_DESTRUCTION);
        CHECK(world.team->stockSuppliers.empty()); CHECK(world.team->directStockSuppliers.empty());
        CHECK(world.game.map.materialSupplyModesSlot(receiver,WOOD)==0);
        CHECK_FALSE(world.game.map.materialAvailableSlot(0,WOOD,0,19,20,false,receiver));
        glob2test::HeadlessGame restored({.loadDefaultRace=true});
        REQUIRE(loadProductionGame(restored.game,saveProductionGame(world.game,false),false));
        auto* copiedSupplier=restored.game.teams[0]->myBuildings[Building::GIDtoID(supplier->gid)];
        auto* copiedReceiver=restored.game.teams[0]->myBuildings[Building::GIDtoID(receiver->gid)];
        REQUIRE(copiedSupplier); REQUIRE(copiedReceiver);
        CHECK(restored.game.teams[0]->stockSuppliers.empty()); CHECK(restored.game.teams[0]->directStockSuppliers.empty());
        CHECK(restored.game.map.materialSupplyModesSlot(copiedReceiver,WOOD)==0);
        CHECK_FALSE(restored.game.map.materialAvailableSlot(0,WOOD,0,19,20,false,copiedReceiver));
        CHECK(restored.game.map.checkSum(true)==world.game.map.checkSum(true));
        supplier->cancelDelete(); copiedSupplier->cancelDelete();
        CHECK(world.game.map.materialSupplyModesSlot(receiver,WOOD)==mode);
        CHECK(restored.game.map.materialSupplyModesSlot(copiedReceiver,WOOD)==mode);
        CHECK(world.game.map.materialAvailableSlot(0,WOOD,0,19,20,false,receiver));
        CHECK(restored.game.map.materialAvailableSlot(0,WOOD,0,19,20,false,copiedReceiver));
        CHECK(restored.game.map.checkSum(true)==world.game.map.checkSum(true));
    }
}

TEST_CASE("current saves reject construction origins outside the explicit job graph")
{
    glob2test::HeadlessGlobals globals;
    for (bool repair : {false,true}) for (int invalidKind : {0,1,2}) {
        CAPTURE(repair); CAPTURE(invalidKind);
        glob2test::HeadlessGame world({.loadDefaultRace=true});
        Building* building=world.addBuilding("inn",8,8);
        if (repair) building->hp-=10;
        building->launchConstruction(1,1);
        REQUIRE(building->tryToBuildingSiteRoom());
        REQUIRE(building->type->isBuildingSite);
        // A site cannot be its own origin, an unrelated completed building
        // cannot become the result, and an existing-building job needs an origin.
        building->constructionOriginTypeNum=invalidKind==0 ? building->typeNum : invalidKind==1 ? 9 : -1;
        const auto bytes=saveProductionGame(world.game,false);
        glob2test::HeadlessGame restored({.loadDefaultRace=true});
        CHECK_THROWS_AS(loadProductionGame(restored.game,bytes,false),std::runtime_error);
    }
    for (bool repair : {false,true}) {
        glob2test::HeadlessGame world({.loadDefaultRace=true});
        Building* building=world.addBuilding("inn",8,8);
        // This malformed pending job still names a completed type, but records
        // another building as its cancellation origin.
        building->buildingState=Building::WAITING_FOR_CONSTRUCTION;
        building->constructionResultState=repair ? Building::REPAIR : Building::UPGRADE;
        building->constructionOriginTypeNum=9;
        const auto bytes=saveProductionGame(world.game,false);
        glob2test::HeadlessGame restored({.loadDefaultRace=true});
        CHECK_THROWS_AS(loadProductionGame(restored.game,bytes,false),std::runtime_error);
    }
}


TEST_CASE("demolition settles shared partial construction only when removal becomes final")
{
    glob2test::HeadlessGlobals globals;
    for (bool repair : {false,true}) {
        CAPTURE(repair);
        glob2test::HeadlessGame world({.loadDefaultRace=true});
        auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
        for (int type : {2,3}) json["variants"][type]["semantics"]["market"]["sharedStock"]=true;
        json["variants"][2]["properties"]["materialMultiplier"][WOOD]=1;
        json["variants"][2]["semantics"]["constructionCost"]={{"wood",4}};
        json["variants"][3]["semantics"]["repairCost"]={{"wood",8}};
        world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
        Building* b=world.game.addBuilding(8,8,repair ? 3 : 2,0,0,0); REQUIRE(b);
        if (repair) {
            b->hp=b->getEffectiveMaxHp()/2;
            b->launchConstruction(0,0); REQUIRE(b->tryToBuildingSiteRoom());
        }
        if (repair) b->addMaterialIntoBuilding(WOOD);
        else { b->materials[WOOD]=1; b->fundConstructionFromInventory(); }
        REQUIRE(b->constructionReserved[WOOD]==1);
        REQUIRE(world.team->reservedTeamMaterials[WOOD]==1);
        b->launchDelete(); CHECK(world.team->reservedTeamMaterials[WOOD]==1);
        b->cancelDelete(); CHECK(world.team->reservedTeamMaterials[WOOD]==1);
        CHECK(b->materials[WOOD]==1);
        b->launchDelete();
        const int id=Building::GIDtoID(b->gid);
        glob2test::HeadlessGame copy({.loadDefaultRace=true});
        REQUIRE(loadProductionGame(copy.game,saveProductionGame(world.game,false),false));
        auto* copiedTeam=copy.game.teams[0];
        REQUIRE(copiedTeam->reservedTeamMaterials[WOOD]==1);
        world.team->syncStep(); copiedTeam->syncStep();
        CHECK(world.team->myBuildings[id]==nullptr); CHECK(copiedTeam->myBuildings[id]==nullptr);
        CHECK(world.team->reservedTeamMaterials[WOOD]==0); CHECK(copiedTeam->reservedTeamMaterials[WOOD]==0);
        CHECK(world.team->teamMaterials[WOOD]==(repair ? 0 : 1));
        CHECK(copiedTeam->teamMaterials[WOOD]==world.team->teamMaterials[WOOD]);
    }
}

TEST_CASE("ground buildings with attraction radii remain melee targets")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& variant=json["variants"][3];
    variant["properties"]["zonable"]={1,1,1};
    variant["properties"]["defaultUnitStayRange"]=4;
    variant["properties"]["maxUnitStayRange"]=8;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* target=world.game.addBuilding(8,8,3,1); REQUIRE(target);
    Unit* attacker=world.addUnit(WARRIOR,7,8);
    world.team->enemies=world.game.teams[1]->me;
    const auto touched=world.game.map.doesUnitTouchEnemy(attacker);
    REQUIRE(touched);
    CHECK(world.game.map.getBuilding(attacker->posX+touched->dx,attacker->posY+touched->dy)==target->gid);
}

}
