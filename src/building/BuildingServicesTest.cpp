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
    b["properties"]["maxResource"][WOOD] = 20;
    b["semantics"]["market"]["sharedStock"] = shared;
    b["semantics"]["feeding"]["cost"] = {{"wheat", 2}};
    b["semantics"]["feeding"]["holdAdmissionUntilExit"] = false;
    b["semantics"]["healing"] = {
        {"enabled", true}, {"unitMask", 1u << WORKER}, {"duration", 4},
        {"cost", {{"wood", 3}, {"wheat", 1}}}};
    game.buildingsTypes.loadSnapshotJson(catalog.dump());
    game.configureBuildingCatalog();
}
void admit(Building* building, Unit* unit, int purpose)
{
    unit->destinationPurpose = purpose;
    REQUIRE(building->canOfferService(unit, purpose));
    building->subscribeUnitForInside(unit);
    REQUIRE(unit->serviceResourcesReserved);
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
    b->resources[WHEAT]=3; b->resources[WOOD]=3;
    Unit* eater=world.addUnit(WORKER);
    Unit* patient=world.addUnit(WORKER);
    Unit* next=world.addUnit(WORKER);
    Unit* excluded=world.addUnit(WARRIOR);
    CHECK_FALSE(b->canOfferService(excluded,HEAL));
    admit(b,eater,FEED);
    admit(b,patient,HEAL);
    CHECK(b->availableResource(WHEAT)==0);
    CHECK(b->availableResource(WOOD)==0);
    CHECK_FALSE(b->canOfferService(next,FEED));
    b->settleService(patient);
    b->settleService(patient);
    CHECK(b->resources[WOOD]==0);
    CHECK(b->resources[WHEAT]==2);
    b->removeUnitFromInside(eater);
    eater->standardRandomActivity();
    CHECK(b->availableResource(WHEAT)==2);
    CHECK(b->resources[WHEAT]==2);
    CHECK(b->canOfferService(next,FEED));
}

TEST_CASE("shared inventory cannot be committed twice by separate buildings")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    serviceCatalog(world.game,true);
    Building* a=world.addBuilding("inn",4,4);
    Building* b=world.addBuilding("inn",12,12);
    world.team->teamResources[WHEAT]=2;
    Unit* first=world.addUnit(WORKER);
    Unit* second=world.addUnit(WORKER);
    admit(a,first,FEED);
    CHECK_FALSE(b->canOfferService(second,FEED));
    a->removeUnitFromInside(first);
    first->standardRandomActivity();
    CHECK(b->canOfferService(second,FEED));
    admit(b,second,FEED);
    b->settleService(second);
    CHECK(world.team->teamResources[WHEAT]==0);
    CHECK(world.team->reservedTeamResources[WHEAT]==0);
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
    b->resources[WHEAT]=5; b->resources[WOOD]=3;
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
    b->resources[WHEAT]=10; b->resources[WOOD]=6;
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
    CHECK(restored->availableResource(WHEAT)==7);
    CHECK(restored->availableResource(WOOD)==3);
    CHECK(restored->unitsInside.size()==2);
    restored->kill();
    CHECK(restored->owner->reservedTeamResources[WHEAT]==0);
    CHECK(restored->owner->reservedTeamResources[WOOD]==0);
    CHECK(restored->resources[WHEAT]==10);
    CHECK(restored->resources[WOOD]==6);
}

TEST_CASE("construction eligibility and work speed are independently trainable")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    Unit* worker=world.addUnit(WORKER);
    BuildingTrainingSpec training;
    training.enabled=true; training.targetLevel=2;
    worker->applyTraining(training,BUILD);
    CHECK(worker->level[BUILD]==2);
    CHECK(worker->level[HARVEST]==0);
    CHECK(worker->workerLevel()==0);
    training.targetLevel=0; training.constructionLevel=3;
    CHECK(worker->needsTraining(training,BUILD));
    worker->applyTraining(training,BUILD);
    CHECK(worker->workerLevel()==3);
    CHECK(worker->level[BUILD]==2);
    CHECK(worker->level[HARVEST]==0);
}
TEST_CASE("shared delivery never discards inventory above a recipient capacity")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    serviceCatalog(world.game,true);
    Building* b=world.addBuilding("inn",8,8);
    b->resources[WHEAT]=b->type->maxResource[WHEAT]+7;
    const int before=b->resources[WHEAT];
    b->addResourceIntoBuilding(WHEAT);
    CHECK(b->resources[WHEAT]==before);
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
    worker->displacement=Unit::DIS_ENTERING_BUILDING;
    worker->delta=255;
    worker->syncStep();
    CHECK(worker->insideTimeout==-19);
    CHECK(worker->speed==school->type->insideSpeed);
}

TEST_CASE("save loading rejects a reservation detached from its visitor list")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    serviceCatalog(world.game);
    Building* b=world.addBuilding("inn",8,8);
    b->resources[WHEAT]=10;
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

}
