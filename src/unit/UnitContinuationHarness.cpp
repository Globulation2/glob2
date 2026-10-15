// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise clearing arbitration and idle activity across a real game save/load.
#include "EngineFixtures.h"
#include "GradientFixture.h"
#include <string>
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Building.h"
#include "Utilities.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <vector>
#include <algorithm>

namespace
{
struct UnitActivityAccess : Unit
{
    static void run(Unit& unit) { const auto activity=&UnitActivityAccess::handleActivity; (unit.*activity)(); }
    static void route(Unit& unit) { const auto movement=&UnitActivityAccess::handleMovementGoingToFlagOrBuilding; (unit.*movement)(); }
};
static std::vector<Uint32> state(Game& game)
{
    std::vector<Uint32> result, buildings, units;
    game.checkSum(&result, &buildings, &units, true);
    result.erase(result.begin()); // The loaded map header records the saved format.
    result.insert(result.end(), buildings.begin(), buildings.end());
    result.insert(result.end(), units.begin(), units.end());
    for (int t = 0; t < game.teamsCount(); ++t)
    {
        const auto* team = game.teams[t];
        auto append = [&](const auto& list) {
            result.push_back(list.size());
            for (const auto* entry : list) result.push_back(entry->gid);
        };
        append(team->canFeedUnit);
        append(team->canHealUnit);
        append(team->canExchange);
        append(team->swarms);
        append(team->turrets);
        append(team->clearingFlags);
        append(team->virtualBuildings);
        append(team->buildingsWaitingForDestruction);
        append(team->buildingsToBeDestroyed);
        append(team->buildingsTryToBuildingSiteRoom);
        for (const auto& list : team->canUpgrade) append(list);
        for (const auto& entry : team->buildingsNeedingUnits)
        {
            result.push_back(entry.first);
            append(entry.second);
        }
        for (int id = 0; id < Building::MAX_COUNT; ++id) if (auto* b = team->myBuildings[id])
        {
            append(b->unitsInside);
            append(b->unitsWorking);
            result.push_back(b->canConvertUnit());
        }
        for (int id = 0; id < Unit::MAX_COUNT; ++id) if (auto* unit = game.teams[t]->myUnits[id])
        {
            result.push_back(unit->jobTimer);
            result.push_back(unit->previousClearingArea.has_value());
            if (unit->previousClearingArea)
            {
                result.push_back(unit->previousClearingArea->x);
                result.push_back(unit->previousClearingArea->y);
            }
            result.push_back(unit->previousClearingAreaDistance);
        }
    }
    return result;
}

static void checkContinuation(int checkpoint, bool hazards = false)
{
    setSyncRandSeed(731);
    GameGUI original;
    Game& game = original.game;
    game.gameHeader.setRandomSeed(731);
    glob2test::BoundGameRandom bound(game);
    game.map.setSize(5, 5, GRASS);
    game.map.setGame(&game);
    game.addTeam();
    game.teams[0]->race.loadDefault();
    game.setWaitingOnMask(0);
    if (hazards) {
        auto edit=game.map.editTerrain();
        for (int y=8;y<18;++y) for(int x=6;x<15;++x) game.map.paintCell(x,y,ICE);
    }
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) game.map.clearImmobileUnit(x, y);
    for (int x = 12; x < 15; ++x)
    {
        game.map.setResourceByIndex(x, 12, WOOD, 1);
        game.map.setAreaMask(game.map.coordToIndex(x, 12), &Tile::clearArea, game.teams[0]->me);
    }
    auto* first = game.addUnit(7, 12, 0, WORKER, 0, 0, 0, 0);
    auto* second = game.addUnit(7, 14, 0, WORKER, 0, 0, 0, 0);
    REQUIRE((first && second));
    const int innType = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
    auto* firstInn = game.addBuilding(4, 4, innType, 0);
    auto* secondInn = game.addBuilding(20, 4, innType, 0);
    REQUIRE((firstInn && secondInn));
    // Availability order deliberately differs from building-id order.
    secondInn->setMaterial(WHEAT, 10);
    secondInn->update();
    firstInn->setMaterial(WHEAT, 10);
    firstInn->update();
    auto* flyer = game.addUnit(12, 4, 0, EXPLORER, 0, 0, 0, 0);
    REQUIRE((flyer && game.teams[0]->findNearestFood(flyer) == firstInn));
    first->jobTimer = 40;
    second->jobTimer = 7;
    // A current reservation and a retained distance without a current reservation
    // are both meaningful: other workers use distance to arbitrate a claim.
    first->previousClearingArea = Unit::ClearingAreaClaim{12, 12};
    first->previousClearingAreaDistance = 5;
    second->previousClearingAreaDistance = UNIT_CLEAR_AREA_DISTANCE_NONE;
    game.map.setClearingAreaClaimed(12, 12, 0, first->gid);
    game.teams[0]->noMoreBuildingSitesCountdown = 91;
    for (int i = 0; i < checkpoint; ++i) game.syncStep(0);
    const auto before = state(game);
    auto* storage = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(storage);
    game.save(&output, false, "unit continuation");
    const std::string bytes(storage->getBuffer(), storage->getPosition());
    std::vector<std::vector<Uint32>> expected;
    for (int i = 0; i < 256; ++i)
    {
        game.syncStep(0);
        expected.push_back(state(game));
    }
    const auto expectedRandom = game.syncRandom;
    GameGUI restored;
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
    input.seekFromStart(0);
    REQUIRE(restored.game.load(&input));
    restored.game.setWaitingOnMask(0);
    REQUIRE(state(restored.game) == before);
    for (int i = 0; i < 256; ++i)
    {
        restored.game.syncStep(0);
        if (state(restored.game) != expected[i])
        {
            FAIL("continuation mismatch checkpoint=" << checkpoint << " offset=" << i);
        }
    }
    REQUIRE(restored.game.syncRandom == expectedRandom);
}
}

TEST_SUITE("UnitContinuation")
{
    TEST_CASE("hungry and injured flyers defer cold service guidance and reserve only after publication")
    {
        glob2test::HeadlessGlobals globals;
        for(unsigned participants : {1u,2u,4u,8u})
            for(auto medical : {Unit::MED_HUNGRY,Unit::MED_DAMAGED})
        {
            CAPTURE(participants); CAPTURE(int(medical));
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            auto& game=world.game;
            auto& map=game.map;
            map.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[{"key":"fixture:air-service-barrier","name":"Barrier","base":"grass","properties":{"flyable":false},"appearance":"grass"}]})");
            map.paintCell(24,24,*map.terrainRegistry().find("fixture:air-service-barrier"));
            REQUIRE(map.hasAirTerrainConstraints());
            map.configureCompute(participants);
            map.configureGradientPipeline(participants-1,3);
            auto* unit=world.addUnit(EXPLORER,16,16);
            auto* service=world.addBuilding(medical==Unit::MED_HUNGRY?"inn":"hospital",4,4);
            REQUIRE(unit); REQUIRE(service);
            if(medical==Unit::MED_HUNGRY) service->setMaterial(WHEAT,20);
            world.team->addToStaticAbilitiesLists(service);
            service->update();
            const auto choose=[&] { return medical==Unit::MED_HUNGRY
                ? world.team->findNearestFood(unit) : world.team->findNearestHeal(unit); };
            REQUIRE(choose()==nullptr);
            unit->medical=medical;
            unit->setNeedToRecheckMedical(true);
            const auto random=unit->entityRandom;
            UnitActivityAccess::run(*unit);
            CHECK(unit->medical==medical);
            CHECK(unit->needToRecheckMedical());
            CHECK(unit->attachedBuilding==nullptr);
            CHECK(service->unitsInside.empty());
            CHECK(unit->entityRandom==random);
            // Completing private work cannot make a candidate visible early.
            map.stagePeriodicGradientPreparation();
            map.preparePendingGradient();
            map.finishGradientPipeline();
            CHECK(choose()==nullptr);
            for(unsigned boundary=0;boundary<512 && !choose();++boundary) {
                map.advanceGradientPipeline();
                ++game.stepCounter;
                map.stagePeriodicGradientPreparation();
                map.preparePendingGradient();
            }
            REQUIRE(choose()==service);
            UnitActivityAccess::run(*unit);
            CHECK(unit->attachedBuilding==service);
            CHECK(unit->activity==Unit::ACT_UPGRADING);
            CHECK(unit->destinationPurpose==(medical==Unit::MED_HUNGRY?FEED:HEAL));
            CHECK(unit->entityRandom==random);
            CHECK(std::count(service->unitsInside.begin(),service->unitsInside.end(),unit)==1);
            UnitActivityAccess::run(*unit);
            CHECK(std::count(service->unitsInside.begin(),service->unitsInside.end(),unit)==1);
            map.finishGradientPipeline();
        }
    }

    TEST_CASE("a pending building route retains assignment and random state until publication")
    {
        glob2test::HeadlessGlobals globals;
        for (unsigned participants : {1u,2u,4u,8u})
        {
            CAPTURE(participants);
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            auto& game=world.game;
            game.gameHeader.setBuildingGradientDelay(3);
            game.map.configureCompute(participants);
            game.map.configureGradientPipeline(participants-1,3);
            auto* unit=world.addUnit(WORKER,16,16);
            auto* building=world.addBuilding("inn",4,4);
            REQUIRE(unit); REQUIRE(building);
            unit->destinationPurpose=FEED;
            building->unitsInside.push_back(unit);
            unit->subscriptionSuccess(building,true,false,UnitJobPurpose::None);
            const auto activity=unit->activity;
            const auto random=unit->entityRandom;
            const auto assertWaiting=[&] {
                UnitActivityAccess::route(*unit);
                CHECK(unit->targetBuilding==building);
                CHECK(unit->attachedBuilding==building);
                CHECK(unit->activity==activity);
                CHECK(unit->jobPurpose==UnitJobPurpose::None);
                CHECK(unit->entityRandom==random);
                CHECK(unit->posX==16); CHECK(unit->posY==16);
                CHECK(unit->dx==0); CHECK(unit->dy==0);
                CHECK(std::find(building->unitsInside.begin(),building->unitsInside.end(),unit)!=building->unitsInside.end());
            };
            assertWaiting();
            for (int tick=0;tick<3;++tick) {
                game.map.advanceGradientPipeline();
                assertWaiting();
                ++game.stepCounter;
                game.map.stagePeriodicGradientPreparation();
                game.map.preparePendingGradient();
            }
            game.map.advanceGradientPipeline();
            UnitActivityAccess::route(*unit);
            CHECK(unit->attachedBuilding==building);
            CHECK((unit->dx!=0 || unit->dy!=0));
            game.map.finishGradientPipeline();
        }
    }

    TEST_CASE("medical abort clears every working purpose before idle or service continuation")
    {
        glob2test::HeadlessGlobals globals;
        for (auto purpose : {UnitJobPurpose::Transport,UnitJobPurpose::Clear,UnitJobPurpose::Explore,UnitJobPurpose::Defend})
            for (int medical : {Unit::MED_HUNGRY,Unit::MED_DAMAGED})
                for (bool serviceAvailable : {false,true})
        {
            CAPTURE(int(purpose)); CAPTURE(medical); CAPTURE(serviceAvailable);
            glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=4921});
            const int type=purpose==UnitJobPurpose::Explore?EXPLORER:purpose==UnitJobPurpose::Defend?WARRIOR:WORKER;
            auto* unit=world.addUnit(type,8,8); REQUIRE(unit);
            auto* work=world.addBuilding(purpose==UnitJobPurpose::Transport?"inn":
                purpose==UnitJobPurpose::Clear?"clearingflag":purpose==UnitJobPurpose::Explore?"explorationflag":"warflag",16,16);
            REQUIRE(work);
            unit->destinationPurpose=purpose==UnitJobPurpose::Transport?WHEAT:UNIT_DEST_PURPOSE_NONE;
            work->unitsWorking.push_back(unit);
            unit->subscriptionSuccess(work,false,purpose!=UnitJobPurpose::Transport,purpose);
            REQUIRE(unit->jobPurpose==purpose);
            Building* service=nullptr;
            if (serviceAvailable) {
                service=world.addBuilding(medical==Unit::MED_HUNGRY?"inn":"hospital",10,8);
                REQUIRE(service);
                if (medical==Unit::MED_HUNGRY) service->setMaterial(WHEAT, 20);
                world.team->addToStaticAbilitiesLists(service);
                service->update();
                // This case tests aborting an existing task into an available
                // service. Cold Pending/resume behavior is covered separately.
                if (!unit->performance[FLY])
                    glob2test::awaitBuildingGradient(world.game,service,unit->swimClass());
                REQUIRE((medical==Unit::MED_HUNGRY?world.team->findNearestFood(unit):world.team->findNearestHeal(unit))==service);
            }
            unit->medical=Unit::Medical(medical);
            unit->setNeedToRecheckMedical(true);
            const auto random=unit->entityRandom;
            UnitActivityAccess::run(*unit);
            CHECK(unit->jobPurpose==UnitJobPurpose::None);
            CHECK(work->unitsWorking.empty()); CHECK(work->unitsInside.empty());
            CHECK(unit->entityRandom==random);
            if (serviceAvailable) {
                CHECK(unit->activity==Unit::ACT_UPGRADING);
                CHECK(unit->attachedBuilding==service);
                CHECK(unit->destinationPurpose==(medical==Unit::MED_HUNGRY?FEED:HEAL));
                REQUIRE(std::find(service->unitsInside.begin(),service->unitsInside.end(),unit)!=service->unitsInside.end());
            } else {
                CHECK(unit->activity==Unit::ACT_RANDOM);
                CHECK_FALSE(unit->attachedBuilding); CHECK_FALSE(unit->targetBuilding);
            }
            auto* storage=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream output(storage);
            world.game.save(&output,false,"medical assignment abort");
            const auto bytes=storage->takeContents();
            GameGUI resumed;
            GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
            input.seekFromStart(0); REQUIRE(resumed.game.load(&input));
            auto* restored=resumed.game.teams[0]->myUnits[Unit::GIDtoID(unit->gid)]; REQUIRE(restored);
            CHECK(restored->jobPurpose==UnitJobPurpose::None);
            CHECK(restored->activity==unit->activity);
            CHECK(restored->entityRandom==random);
        }
    }

    TEST_CASE("hazard routes and idle escape preserve per-tick state across save load")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings = true});
        for (int checkpoint : {0,31,127}) checkContinuation(checkpoint,true);
    }

	TEST_CASE("five checkpoints replay 256 ticks with matching state and RNG")
	{
		glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings = true});
	    for (int checkpoint : {0, 1, 31, 64, 127}) checkContinuation(checkpoint);
	}
}
