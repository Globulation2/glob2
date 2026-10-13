// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "BuildingType.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>

namespace
{
void serviceCatalog(Game& game, bool shared = false)
{
    auto catalog = nlohmann::json::parse(game.buildingsTypes.snapshotJson());
    auto& b = catalog["variants"][3];
    b["properties"]["maxUnitInside"] = 3;
    b["properties"]["maxMaterial"][WOOD] = 20;
    b["semantics"]["market"]["sharedStock"] = shared;
    b["semantics"]["feeding"]["cost"] = {{"food", 2}};
    b["semantics"]["feeding"]["holdAdmissionUntilExit"] = false;
    b["semantics"]["healing"] = {
        {"enabled", true}, {"unitMask", 1u << WORKER}, {"duration", 4},
        {"cost", {{"wood", 3}, {"food", 1}}}};
    game.buildingsTypes.loadSnapshotJson(catalog.dump());
    game.configureBuildingCatalog();
}
void admit(Building* building, Unit* unit, int purpose)
{
    unit->core.destinationPurpose = purpose;
    REQUIRE(building->canOfferService(unit, purpose));
    building->subscribeUnitForInside(unit);
    REQUIRE(unit->detail.serviceResourcesReserved);
}
}

TEST_SUITE("BuildingServices")
{
TEST_CASE("mixed visits share capacity and inventory and settle once")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    serviceCatalog(world.game);
    Building* b = world.addBuilding("inn",8,8);
    b->materials[WHEAT]=3; b->materials[WOOD]=3;
    Unit* eater=world.addUnit(WORKER);
    Unit* patient=world.addUnit(WORKER);
    Unit* next=world.addUnit(WORKER);
    Unit* excluded=world.addUnit(WARRIOR);
    CHECK_FALSE(b->canOfferService(excluded,HEAL));
    admit(b,eater,FEED);
    admit(b,patient,HEAL);
    CHECK(b->availableMaterial(WHEAT)==0);
    CHECK(b->availableMaterial(WOOD)==0);
    CHECK_FALSE(b->canOfferService(next,FEED));
    b->settleService(patient);
    b->settleService(patient);
    CHECK(b->materials[WOOD]==0);
    CHECK(b->materials[WHEAT]==2);
    b->removeUnitFromInside(eater);
    eater->standardRandomActivity();
    CHECK(b->availableMaterial(WHEAT)==2);
    CHECK(b->materials[WHEAT]==2);
    CHECK(b->canOfferService(next,FEED));
}

TEST_CASE("shared inventory cannot be committed twice by separate buildings")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    serviceCatalog(world.game,true);
    Building* a=world.addBuilding("inn",4,4);
    Building* b=world.addBuilding("inn",12,12);
    world.team->teamMaterials[WHEAT]=2;
    Unit* first=world.addUnit(WORKER);
    Unit* second=world.addUnit(WORKER);
    admit(a,first,FEED);
    CHECK_FALSE(b->canOfferService(second,FEED));
    a->removeUnitFromInside(first);
    first->standardRandomActivity();
    CHECK(b->canOfferService(second,FEED));
    admit(b,second,FEED);
    b->settleService(second);
    CHECK(world.team->teamMaterials[WHEAT]==0);
    CHECK(world.team->reservedTeamMaterials[WHEAT]==0);
}

TEST_CASE("completed feeding admission buffer ignores unrelated mixed services")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    serviceCatalog(world.game);
    auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    catalog["variants"][3]["semantics"]["feeding"]["holdAdmissionUntilExit"]=true;
    world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("inn",8,8);
    b->materials[WHEAT]=5; b->materials[WOOD]=3;
    Unit* eater=world.addUnit(WORKER);
    Unit* patient=world.addUnit(WORKER);
    Unit* next=world.addUnit(WORKER);
    admit(b,eater,FEED); admit(b,patient,HEAL);
    CHECK(b->canOfferService(next,FEED));
    b->settleService(eater);
    CHECK_FALSE(b->canOfferService(next,FEED));
    b->removeUnitFromInside(eater); eater->standardRandomActivity();
    CHECK(b->canOfferService(next,FEED));
}

TEST_CASE("pending mixed visits restore reservations and cancellation releases only unpaid costs")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    serviceCatalog(world.game,true);
    Building* b=world.addBuilding("inn",8,8);
    b->materials[WHEAT]=10; b->materials[WOOD]=6;
    Unit* eater=world.addUnit(WORKER);
    Unit* patient=world.addUnit(WORKER);
    admit(b,eater,FEED); admit(b,patient,HEAL);
    auto* bytes=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(bytes);
    world.game.save(&output,false,"Mixed service commitments"); output.flush();
    auto* data=new GAGCore::MemoryStreamBackend(*bytes); data->seekFromStart(0);
    GAGCore::BinaryInputStream input(data);
    glob2test::HeadlessGame copy({.loadDefaultRace=true,.header=true});
    REQUIRE(copy.game.load(&input));
    Building* restored=copy.game.teams[0]->myBuildings[Building::GIDtoID(b->gid)];
    REQUIRE(restored);
    CHECK(restored->availableMaterial(WHEAT)==7);
    CHECK(restored->availableMaterial(WOOD)==3);
    CHECK(restored->unitsInside.size()==2);
    restored->kill();
    CHECK(restored->owner->reservedTeamMaterials[WHEAT]==0);
    CHECK(restored->owner->reservedTeamMaterials[WOOD]==0);
    CHECK(restored->materials[WHEAT]==10);
    CHECK(restored->materials[WOOD]==6);
}

TEST_CASE("construction eligibility and work speed are independently trainable")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    Unit* worker=world.addUnit(WORKER);
    BuildingTrainingSpec training;
    training.enabled=true; training.targetLevel=2;
    worker->applyTraining(training,BUILD);
    CHECK(worker->detail.level[BUILD]==2);
    CHECK(worker->detail.level[HARVEST]==0);
    CHECK(worker->workerLevel()==0);
    training.targetLevel=0; training.constructionLevel=3;
    CHECK(worker->needsTraining(training,BUILD));
    worker->applyTraining(training,BUILD);
    CHECK(worker->workerLevel()==3);
    CHECK(worker->detail.level[BUILD]==2);
    CHECK(worker->detail.level[HARVEST]==0);
}
TEST_CASE("shared delivery never discards inventory above a recipient capacity")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    serviceCatalog(world.game,true);
    Building* b=world.addBuilding("inn",8,8);
    b->materials[WHEAT]=b->type->maxMaterial[WHEAT]+7;
    const int before=b->materials[WHEAT];
    b->addMaterialIntoBuilding(WHEAT);
    CHECK(b->materials[WHEAT]==before);
}

TEST_CASE("parallel training waits for the slowest requested course")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& b=catalog["variants"][3];
    b["semantics"]["trainingInParallel"]=true;
    b["semantics"]["training"]["walk"]={{"enabled",true},{"unitMask",1},{"targetLevel",1},{"duration",3}};
    b["semantics"]["training"]["build"]={{"enabled",true},{"unitMask",1},{"targetLevel",1},{"duration",19}};
    world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
    Building* school=world.addBuilding("inn",8,8);
    Unit* worker=world.addUnit(WORKER);
    admit(school,worker,WALK);
    worker->core.displacement=Unit::DIS_ENTERING_BUILDING;
    worker->core.delta=255;
    worker->syncStep();
    CHECK(worker->detail.insideTimeout==-19);
    CHECK(worker->core.speed==school->type->insideSpeed);
}

TEST_CASE("save loading rejects a reservation detached from its visitor list")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    serviceCatalog(world.game);
    Building* b=world.addBuilding("inn",8,8);
    b->materials[WHEAT]=10;
    Unit* eater=world.addUnit(WORKER);
    admit(b,eater,FEED);
    b->unitsInside.clear();
    auto* bytes=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(bytes);
    world.game.save(&output,false,"Invalid service membership"); output.flush();
    auto* data=new GAGCore::MemoryStreamBackend(*bytes); data->seekFromStart(0);
    GAGCore::BinaryInputStream input(data);
    glob2test::HeadlessGame copy({.loadDefaultRace=true,.header=true});
    CHECK_THROWS_WITH(copy.game.load(&input),"Saved unit reservation has no building service visit");
    // Restore the intentionally malformed source fixture before teardown.
    b->unitsInside.push_back(eater);
}


TEST_CASE("canceling a service capable site restores each origin service exactly once")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    for (int type : {3,4}) {
        auto& variant=catalog["variants"][type];
        variant["properties"]["maxUnitInside"]=3;
        for (const char* service : {"feeding","healing"}) {
            variant["semantics"][service]["enabled"]=true;
            variant["semantics"][service]["cost"]=nlohmann::json::object();
        }
        variant["semantics"]["training"]["walk"]={{"enabled",true},{"unitMask",1},{"targetLevel",1},{"duration",1}};
    }
    world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
    Building* b=world.addBuilding("inn",8,8);
    const auto checkMembership=[&] {
        CHECK(std::count(world.team->canFeedUnit.begin(),world.team->canFeedUnit.end(),b)==1);
        CHECK(std::count(world.team->canHealUnit.begin(),world.team->canHealUnit.end(),b)==1);
        CHECK(std::count(world.team->canUpgrade[WALK].begin(),world.team->canUpgrade[WALK].end(),b)==1);
    };
    b->updateCallLists(); checkMembership();
    b->launchConstruction(0,0); REQUIRE(b->tryToBuildingSiteRoom());
    b->updateCallLists(); checkMembership();
    b->cancelConstruction(0); REQUIRE(b->typeNum==3);
    b->updateCallLists(); checkMembership();
    b->updateCallLists(); checkMembership();
}

TEST_CASE("zero duration parallel courses select the slowest inclusive action clock")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& b=catalog["variants"][3]; b["properties"]["insideSpeed"]=12;
    b["semantics"]["trainingInParallel"]=true;
    b["semantics"]["training"]["walk"]={{"enabled",true},{"unitMask",1},{"targetLevel",1},{"duration",0}};
    b["semantics"]["training"]["build"]={{"enabled",true},{"unitMask",1},{"targetLevel",3},{"duration",0}};
    world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
    Building* school=world.addBuilding("inn",8,8);
    Unit* worker=world.addUnit(WORKER);
    admit(school,worker,WALK);
    worker->core.displacement=Unit::DIS_ENTERING_BUILDING; worker->core.delta=255; worker->syncStep();
    REQUIRE(worker->core.displacement==Unit::DIS_INSIDE);
    CHECK(worker->core.speed==4); worker->core.delta=0;
    for (int tick=1;tick<64;++tick) {
        worker->syncStep(); CHECK(worker->detail.level[BUILD]==0); CHECK(worker->detail.level[WALK]==0);
    }
    worker->syncStep(); CHECK(worker->detail.level[BUILD]==3); CHECK(worker->detail.level[WALK]==1);
}


TEST_CASE("parallel training compares whole remaining ticks from the entry phase")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& b=catalog["variants"][3]; b["properties"]["insideSpeed"]=12;
    b["semantics"]["trainingInParallel"]=true;
    b["semantics"]["training"]["walk"]={{"enabled",true},{"unitMask",1},{"targetLevel",2},{"duration",0}};
    b["semantics"]["training"]["build"]={{"enabled",true},{"unitMask",1},{"targetLevel",1},{"duration",1}};
    world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
    Building* school=world.addBuilding("inn",8,8);
    Unit* worker=world.addUnit(WORKER);
    admit(school,worker,WALK);
    worker->core.displacement=Unit::DIS_ENTERING_BUILDING;
    worker->core.action=STOP_WALK; worker->core.speed=32; worker->core.delta=255;
    worker->syncStep();
    REQUIRE(worker->core.displacement==Unit::DIS_INSIDE);
    REQUIRE(worker->core.delta==31);
    CHECK(worker->detail.insideTimeout==-1); CHECK(worker->core.speed==12);
    // WALK would finish at ceil((256-31)/6)=38, but BUILD needs
    // ceil((512-31)/12)=41. Equal duration/speed ratios are insufficient.
    for (int tick=1;tick<41;++tick) {
        worker->syncStep(); CHECK(worker->detail.level[BUILD]==0); CHECK(worker->detail.level[WALK]==0);
    }
    worker->syncStep(); CHECK(worker->detail.level[BUILD]==1); CHECK(worker->detail.level[WALK]==2);
}

TEST_CASE("minimum service speed completes after diagonal entry")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& b=catalog["variants"][3]; b["properties"]["insideSpeed"]=1;
    b["semantics"]["trainingInParallel"]=true;
    b["semantics"]["training"]["walk"]={{"enabled",true},{"unitMask",1},{"targetLevel",1},{"duration",0}};
    world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
    Building* school=world.addBuilding("inn",8,8);
    Unit* worker=world.addUnit(WORKER);
    admit(school,worker,WALK);
    worker->core.displacement=Unit::DIS_ENTERING_BUILDING;
    worker->core.action=WALK; worker->core.dx=worker->core.dy=1; worker->core.speed=2; worker->core.delta=255;
    worker->syncStep();
    REQUIRE(worker->core.displacement==Unit::DIS_INSIDE);
    REQUIRE(worker->core.delta==0); CHECK(worker->core.speed==1);
    for (int tick=1;tick<256;++tick) { worker->syncStep(); CHECK(worker->detail.level[WALK]==0); }
    worker->syncStep(); CHECK(worker->detail.level[WALK]==1);
}


TEST_CASE("long high speed healing retains bounded phase across save continuation")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto catalog=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto& b=catalog["variants"][3]; b["properties"]["insideSpeed"]=256;
    b["semantics"]["healing"]={{"enabled",true},{"unitMask",1},{"duration",1000000}};
    world.game.buildingsTypes.loadSnapshotJson(catalog.dump()); world.game.configureBuildingCatalog();
    world.game.gameHeader.setHungerDisabled(true);
    Building* hospital=world.addBuilding("inn",8,8);
    Unit* patient=world.addUnit(WORKER,7,8);
    patient->core.hp=patient->performance(HP)-1;
    admit(hospital,patient,HEAL);
    patient->core.displacement=Unit::DIS_ENTERING_BUILDING;
    patient->core.action=STOP_WALK; patient->core.dx=patient->core.dy=0; patient->core.speed=1; patient->core.delta=255;
    patient->syncStep();
    REQUIRE(patient->core.displacement==Unit::DIS_INSIDE);
    REQUIRE(patient->core.speed>256); REQUIRE(patient->core.delta==0);
    for (int tick=0;tick<1024;++tick) { patient->syncStep(); REQUIRE(patient->core.delta==0); }
    CHECK(patient->detail.insideTimeout==-1000000+1024);
    auto* bytes=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(bytes);
    world.game.save(&output,false,"Bounded healing phase"); output.flush();
    auto* data=new GAGCore::MemoryStreamBackend(*bytes); data->seekFromStart(0);
    GAGCore::BinaryInputStream input(data);
    glob2test::HeadlessGame copy({.loadDefaultRace=true,.header=true});
    REQUIRE(copy.game.load(&input));
    Unit* resumed=copy.game.teams[0]->myUnits[Unit::GIDtoID(patient->core.gid)]; REQUIRE(resumed);
    for (int tick=0;tick<256;++tick) {
        patient->syncStep(); resumed->syncStep();
        CHECK(patient->core.delta==0); CHECK(resumed->core.delta==0);
        CHECK(resumed->detail.insideTimeout==patient->detail.insideTimeout);
        CHECK(resumed->core.hp==patient->core.hp);
    }
    patient->detail.insideTimeout=0; resumed->detail.insideTimeout=0;
    patient->syncStep(); resumed->syncStep();
    CHECK(patient->core.hp==patient->performance(HP)); CHECK(resumed->core.hp==patient->core.hp);
    CHECK(patient->core.delta==0); CHECK(resumed->core.delta==0);
}

TEST_CASE("stock tiny deficit healing completes without storing a post exit speed burst")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    Building* hospital=world.addBuilding("hospital",8,8);
    Unit* patient=world.addUnit(WORKER,7,8);
    patient->core.hp=patient->performance(HP)-1;
    for (int resource=0;resource<MaterialSlotCount;++resource)
        hospital->materials[resource]=hospital->type->semantics.healing.cost[resource];
    admit(hospital,patient,HEAL);
    patient->core.displacement=Unit::DIS_ENTERING_BUILDING;
    patient->core.action=STOP_WALK; patient->core.dx=patient->core.dy=0; patient->core.speed=1; patient->core.delta=255;
    patient->syncStep(); REQUIRE(patient->core.speed>256);
    const int duration=hospital->type->semantics.healing.duration;
    for (int tick=0;tick<duration;++tick) { patient->syncStep(); CHECK(patient->core.hp==patient->performance(HP)-1); }
    patient->syncStep(); CHECK(patient->core.hp==patient->performance(HP)); CHECK(patient->core.delta==0);
}

}
