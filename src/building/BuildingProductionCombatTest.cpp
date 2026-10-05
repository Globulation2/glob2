// SPDX-License-Identifier: GPL-3.0-or-later
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
        {"worker", {{"enabled", true}, {"duration", instant ? 0 : 1}, {"cost", {{"wheat", 2}}}}},
        {"warrior", {{"enabled", true}, {"duration", 3}, {"cost", {{"wood", 3}}}}}};
    b["properties"]["maxResource"][WOOD] = 20;
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
    b->resources[WHEAT] = b->resources[WOOD] = 10;
    b->swarmStep();
    CHECK(b->productionUnit == WORKER);
    CHECK(b->productionTimeout == 0);
    CHECK(b->resources[WHEAT] == 10);
    CHECK(b->availableResource(WHEAT) == 8);
    b->ratio[WORKER] = 0; b->ratio[WARRIOR] = 1;
    b->swarmStep();
    CHECK(world.team->stats.measurements.births[WORKER] == 1);
    CHECK(b->resources[WHEAT] == 8);
    CHECK(b->productionUnit == -1);
    b->swarmStep();
    CHECK(b->productionUnit == WARRIOR);
    CHECK(b->productionTimeout == 2);
    CHECK(b->availableResource(WOOD) == 7);
    b->cancelProduction();
    CHECK(b->resources[WOOD] == 10);
    CHECK(b->availableResource(WOOD) == 10);
    CHECK(world.team->stats.measurements.births[EXPLORER] == 0);
}

TEST_CASE("completed blocked job retains its budget and zero duration remains a producer")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    productionCatalog(world.game, true);
    Building* b = world.addBuilding("swarm", 8, 8);
    b->resources[WHEAT] = 2;
    world.team->addToStaticAbilitiesLists(b);
    REQUIRE(std::find(world.team->swarms.begin(), world.team->swarms.end(), b) != world.team->swarms.end());
    for (int y=7; y<=12; ++y) for (int x=7; x<=12; ++x)
        if (x==7 || x==12 || y==7 || y==12) world.game.map.setResource(x,y,STONE,1);
    for (int tick=0; tick<5; ++tick) b->swarmStep();
    CHECK(b->productionUnit == WORKER);
    CHECK(b->productionTimeout == -1);
    CHECK(b->resources[WHEAT] == 2);
    CHECK(b->availableResource(WHEAT) == 0);
    CHECK(world.team->stats.measurements.births[WORKER] == 0);
    b->launchDelete();
    CHECK(b->productionUnit == -1);
    CHECK(b->availableResource(WHEAT) == 2);
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
        b->resources[WOOD]=10;
        b->swarmStep();
        const int gid=b->gid;
        glob2test::HeadlessGame restored({.loadDefaultRace=true, .header=true});
        REQUIRE(loadProductionGame(restored.game, saveProductionGame(world.game,text),text));
        // Loading replaces the fixture's original Team pointer.
        Building* copy=restored.game.teams[0]->myBuildings[Building::GIDtoID(gid)];
        REQUIRE(copy);
        CHECK(copy->productionUnit == WARRIOR);
        CHECK(copy->productionTimeout == b->productionTimeout);
        CHECK(copy->availableResource(WOOD) == b->availableResource(WOOD));
        for (int tick=0; tick<3; ++tick) { b->swarmStep(); copy->swarmStep(); }
        CHECK(copy->productionUnit == b->productionUnit);
        CHECK(copy->resources[WOOD] == 7);
        CHECK(copy->resources[WOOD] == b->resources[WOOD]);
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
    inn["semantics"]["ammunitionResource"]=WOOD;
    inn["semantics"]["ammunitionCost"]=2;
    inn["semantics"]["regenerationPerTick"]=3;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("inn",8,8);
    b->hp-=10; const int hp=b->hp; b->regenerationStep(); CHECK(b->hp==hp+3);
    b->resources[WOOD]=2;
    Unit* enemy=world.addUnit(EXPLORER,14,9,1);
    enemy->speed=1; enemy->delta=0;
    b->turretStep(0); b->turretStep(1); b->turretStep(2);
    CHECK(b->resources[WOOD]==0); CHECK(b->bullets==1);
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
    for (int resource=0; resource<MAX_RESOURCES; ++resource) b->resources[resource]=b->type->maxResource[resource];
    b->update();
    world.game.map.setResource(12,8,WOOD,1);
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
    b->resources[WHEAT]=5;
    CHECK_FALSE(b->type->canExchange);
    REQUIRE(world.team->stockSuppliers.size()==1);
    const Uint16* field=world.game.map.getResourceGradient(0,WHEAT,0,true);
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
    b->resources[WHEAT]=5;
    CHECK(world.game.map.getBuilding(8,8)==NOGBID);
    CHECK(world.game.map.doesPosTouchBuilding(7,8,b->gid).has_value());
    CHECK_FALSE(world.game.map.doesPosTouchBuilding(3,3,b->gid).has_value());
    const Uint16* field=world.game.map.getResourceGradient(0,WHEAT,0,true);
    CHECK(field[world.game.map.coordToIndex(8,8)]==GRADIENT_AT_GOAL-7*GRADIENT_STEP);
    Unit* worker=world.addUnit(WORKER,7,8);
    CHECK(world.game.map.touchedStockedMarket(worker,WHEAT)==b);
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
    source["properties"]["maxResource"][WOOD]=10;
    sink["semantics"]["market"]["fetchesDirectStock"]=true;
    sink["semantics"]["market"]["fetchesStock"]=false;
    sink["properties"]["maxResource"][WOOD]=10;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* supplier=world.addBuilding("inn",8,8);
    Building* consumer=world.addBuilding("hospital",16,16);
    supplier->resources[WOOD]=5;
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
    CHECK(worker->carriedResource==WOOD);
    CHECK(supplier->resources[WOOD]==4);
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
    site["properties"]["maxResource"][WOOD]=0;
    site["semantics"]["constructionCost"]={{"wood",3}};
    site["semantics"]["market"]["sharedStock"]=true;
    json["variants"][3]["semantics"]["market"]["sharedStock"]=true;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.game.addBuilding(8,8,2,0,3,1);
    REQUIRE(b);
    b->addResourceIntoBuilding(WOOD);
    CHECK(b->constructionReserved[WOOD]==1);
    CHECK(b->resources[WOOD]==1);
    CHECK(b->availableResource(WOOD)==0);
    CHECK(b->resourceDeliveryNeed(WOOD)==2);
    BuildingResourceCost competing{}; competing[WOOD]=1;
    CHECK_FALSE(b->reserveResources(competing));
    const auto binary=saveProductionGame(world.game,false);
    glob2test::HeadlessGame continued({.loadDefaultRace=true});
    REQUIRE(loadProductionGame(continued.game,binary,false));
    Building* loaded=continued.game.teams[0]->myBuildings[Building::GIDtoID(b->gid)];
    REQUIRE(loaded);
    CHECK(loaded->constructionReserved==b->constructionReserved);
    CHECK(loaded->availableResource(WOOD)==0);
    for (Building* job : {b,loaded})
    {
        job->addResourceIntoBuilding(WOOD);
        job->addResourceIntoBuilding(WOOD);
        CHECK_FALSE(job->type->isBuildingSite);
        CHECK(job->resources[WOOD]==0);
        CHECK(job->reservedResources[WOOD]==0);
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
    json["variants"][4]["semantics"]["production"]["recipes"]={{"worker",{{"enabled",true},{"duration",10},{"cost",{{"wheat",1}}}}}};
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("hospital",8,8);
    const int origin=b->typeNum;
    b->launchConstruction(1,1);
    REQUIRE(b->getConstructionOriginTypeNum()==origin);
    REQUIRE(b->tryToBuildingSiteRoom());
    REQUIRE(b->type->key=="inn.1.site");
    b->addResourceIntoBuilding(WOOD);
    CHECK(b->constructionReserved[WOOD]==1);
    b->resources[WHEAT]=2;
    b->swarmStep();
    REQUIRE(b->productionUnit==WORKER);
    REQUIRE(b->reservedResources[WHEAT]==1);
    b->cancelConstruction(0);
    CHECK(b->productionUnit==-1);
    CHECK(b->resources[WHEAT]==2);
    CHECK(b->reservedResources[WHEAT]==0);
    CHECK(b->typeNum==origin);
    CHECK(b->resources[WOOD]==1);
    CHECK(b->reservedResources[WOOD]==0);
    CHECK(b->getConstructionOriginTypeNum()==-1);
}

TEST_CASE("repair budgets use independent fruit costs and cancellation creates no material credits")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    json["variants"][3]["semantics"]["repairCost"]={{"cherry",4}};
    json["variants"][2]["properties"]["multiplierResource"][CHERRY]=1;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("inn",8,8);
    b->hp=b->getEffectiveMaxHp()/2;
    int needed[MAX_RESOURCES]{};
    b->getResourceCountToRepair(needed);
    CHECK(needed[CHERRY]==2);
    b->launchConstruction(1,1);
    REQUIRE(b->tryToBuildingSiteRoom());
    CHECK(b->constructionBudget[CHERRY]==2);
    CHECK(b->resources[WOOD]==0);
    b->addResourceIntoBuilding(CHERRY);
    CHECK(b->constructionReserved[CHERRY]==1);
    b->cancelConstruction(1);
    CHECK(b->type->key=="inn.0.finished");
    CHECK(b->resources[CHERRY]==0);
    CHECK(b->hp==150);
    CHECK(b->resources[WOOD]==0);
    CHECK(b->reservedResources[CHERRY]==0);
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
    CHECK(b->resources[WOOD]==0); CHECK(b->resourceDeliveryNeed(WOOD)==1);
    glob2test::HeadlessGame resumed({.loadDefaultRace=true});
    REQUIRE(loadProductionGame(resumed.game,saveProductionGame(world.game,false),false));
    Building* copy=resumed.game.teams[0]->myBuildings[0];
    REQUIRE(copy);
    b->addResourceIntoBuilding(WOOD); copy->addResourceIntoBuilding(WOOD);
    CHECK(b->type->key=="inn.0.finished"); CHECK(copy->type->key==b->type->key);
    CHECK(b->hp==200); CHECK(copy->hp==b->hp); CHECK(copy->resources[WOOD]==0);
}

TEST_CASE("partial supplier packets preserve raw equivalents and save continuation")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    json["variants"][3]["properties"]["maxResource"][WOOD]=20;
    json["variants"][3]["properties"]["multiplierResource"][WOOD]=4;
    json["variants"][9]["properties"]["maxResource"][WOOD]=20;
    json["variants"][9]["properties"]["multiplierResource"][WOOD]=3;
    world.game.buildingsTypes.loadSnapshotJson(json.dump()); world.game.configureBuildingCatalog();
    Building* source=world.addBuilding("inn",8,8);
    Building* sink=world.addBuilding("hospital",16,16);
    source->resources[WOOD]=2;
    const ResourcePacket packet=source->withdrawResourcePacket(WOOD);
    CHECK(packet.numerator==1); CHECK(packet.denominator==2); CHECK(source->resources[WOOD]==0);
    Unit* courier=world.addUnit(WORKER,7,7); courier->receiveCarriedResource(WOOD,packet);
    glob2test::HeadlessGame copy({.loadDefaultRace=true});
    REQUIRE(loadProductionGame(copy.game,saveProductionGame(world.game,false),false));
    Unit* loaded=copy.game.teams[0]->myUnits[Unit::GIDtoID(courier->gid)];
    REQUIRE(loaded); CHECK(loaded->carriedPacket.numerator==1); CHECK(loaded->carriedPacket.denominator==2);
    Building* copiedSink=copy.game.teams[0]->myBuildings[Building::GIDtoID(sink->gid)];
    for (auto [receiver,cargo] : {std::pair{sink,packet},std::pair{copiedSink,loaded->carriedPacket}})
    {
        const auto delivered=receiver->deliverResourcePacket(WOOD,cargo);
        CHECK(delivered.acceptedStock==1); CHECK(receiver->resources[WOOD]==1);
        CHECK(delivered.discardedNumerator==1); CHECK(delivered.discardedDenominator==6);
        // 1/2 raw = 1 stock / 3 stock-per-raw + 1/6 deliberately discarded.
        CHECK(Uint64(cargo.numerator)*3*delivered.discardedDenominator
            == Uint64(cargo.denominator)*(Uint64(delivered.acceptedStock)*delivered.discardedDenominator+3*delivered.discardedNumerator));
        CHECK(receiver->owner->stats.measurements.resourceSpillageEvents==1);
        const auto fractional=receiver->deliverResourcePacket(WOOD,{1,10});
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
    b->addResourceIntoBuilding(WOOD); CHECK(b->hp==150);
    b->hp-=30;
    b->addResourceIntoBuilding(WOOD);
    CHECK(b->type->key=="inn.0.finished"); CHECK(b->hp==170);
    CHECK(b->resources[WOOD]==0); CHECK(b->reservedResources[WOOD]==0);
    b->hp=100;
    b->launchConstruction(1,1); REQUIRE(b->tryToBuildingSiteRoom());
    b->addResourceIntoBuilding(WOOD); CHECK(b->hp==150);
    b->cancelConstruction(1);
    CHECK(b->hp==150); CHECK(b->resources[WOOD]==0);
    CHECK(b->reservedResources[WOOD]==0);
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

}
