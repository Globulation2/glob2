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
#include <TextStream.h>
#include <StreamBackend.h>
#include <vector>
#include <algorithm>

struct UnitActivityAccess : Unit
{
    static bool pending(const Unit& unit) { return unit.gradientDecisionPending; }
	static void route(Unit &unit)
	{
		const auto movement = &UnitActivityAccess::handleMovement;
		(unit.*movement)();
	}
	static void move(Unit &unit)
	{
		const auto action = &UnitActivityAccess::handleActionGoingDxDy;
		(unit.*action)();
	}
	static void run(Unit &unit)
	{
		const auto activity = &UnitActivityAccess::handleActivity;
		(unit.*activity)();
	}
};
namespace
{
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
    for (int i = 0; i < checkpoint; ++i) game.step(0);
    const auto before = state(game);
    auto* storage = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(storage);
    game.save(&output, false, "unit continuation");
    const std::string bytes(storage->getBuffer(), storage->getPosition());
    std::vector<std::vector<Uint32>> expected;
    for (int i = 0; i < 256; ++i)
    {
        game.step(0);
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
        restored.game.step(0);
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
	TEST_CASE("a pending building route steers while retaining assignment and random state until "
			  "publication")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned participants : {1u, 2u, 4u, 8u})
		{
			CAPTURE(participants);
			glob2test::HeadlessGame world({.clearImmobile = true, .header = true, .seed = 4921});
			auto &game = world.game;
			game.gameHeader.setBuildingGradientDelay(3);
			game.map.configureCompute(participants);
			game.map.configureGradientPipeline(participants - 1, 3);
			auto *unit = world.addUnit(WORKER, 16, 16);
			auto *building = world.addBuilding("inn", 4, 4);
			REQUIRE(unit);
			REQUIRE(building);
			unit->destinationPurpose = FEED;
			building->unitsInside.push_back(unit);
			unit->subscriptionSuccess(building, true, false, UnitJobPurpose::None);
			const auto activity = unit->activity;
			const auto random = unit->entityRandom;
			const auto assertSteering = [&]
			{
				UnitActivityAccess::route(*unit);
				CHECK(unit->targetBuilding == building);
				CHECK(unit->attachedBuilding == building);
				CHECK(unit->activity == activity);
				CHECK(unit->jobPurpose == UnitJobPurpose::None);
				CHECK(unit->entityRandom == random);
				CHECK(unit->posX == 16);
				CHECK(unit->posY == 16);
				CHECK(unit->dx == -1);
				CHECK(unit->dy == -1);
				CHECK(UnitActivityAccess::pending(*unit));
				CHECK(std::find(building->unitsInside.begin(), building->unitsInside.end(), unit) !=
					  building->unitsInside.end());
			};
			assertSteering();
            // Missing fields publish one advance after admission, regardless of refresh delay.
			for (int tick = 0; tick < 1; ++tick)
			{
				game.map.advanceGradientPipeline();
				assertSteering();
				++game.stepCounter;
				game.map.stagePeriodicGradientPreparation();
				game.map.preparePendingGradient(game.captureReadBoundary());
			}
			game.map.advanceGradientPipeline();
			UnitActivityAccess::route(*unit);
			CHECK(unit->attachedBuilding == building);
			CHECK(!UnitActivityAccess::pending(*unit));
			CHECK((unit->dx != 0 || unit->dy != 0));
			game.map.finishGradientPipeline();
		}
	}

	TEST_CASE("pending steering tries flyer turns around occupied ground and waits when surrounded")
	{
		glob2test::HeadlessGlobals globals;
		// An eastward heading tries E, SE, NE, S, N, SW, NW, W in that order.
		const int steps[8][2] = {{1, 0},  {1, 1},  {1, -1},  {0, 1},
								 {0, -1}, {-1, 1}, {-1, -1}, {-1, 0}};
		for (int blocked = 0; blocked <= 8; ++blocked)
		{
			CAPTURE(blocked);
			glob2test::HeadlessGame world({.clearImmobile = true, .header = true, .seed = 4921});
			world.game.map.configureGradientPipeline(0, 3);
			auto *unit = world.addUnit(WORKER, 8, 8);
			auto *building = world.addBuilding("inn", 20, 7);
			REQUIRE(unit);
			REQUIRE(building);
			for (int i = 0; i < blocked; ++i)
				REQUIRE(world.addUnit(WORKER, 8 + steps[i][0], 8 + steps[i][1]));
			unit->destinationPurpose = FEED;
			building->unitsInside.push_back(unit);
			unit->subscriptionSuccess(building, true, false, UnitJobPurpose::None);
			const auto random = unit->entityRandom;
			UnitActivityAccess::route(*unit);
			REQUIRE(UnitActivityAccess::pending(*unit));
			CHECK(unit->dx == (blocked == 8 ? 0 : steps[blocked][0]));
			CHECK(unit->dy == (blocked == 8 ? 0 : steps[blocked][1]));
			CHECK(unit->attachedBuilding == building);
			CHECK(unit->targetBuilding == building);
			CHECK(unit->entityRandom == random);
		}
	}

	TEST_CASE("pending steering respects forbidden terrain and mobility and wraps toward flags")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.clearImmobile = true, .header = true, .seed = 4921});
		auto *unit = world.addUnit(WORKER, 1, 8);
		auto *flag = world.addBuilding("warflag", 29, 8);
		REQUIRE(unit);
		REQUIRE(flag);
		const auto random = unit->entityRandom;
		unit->followPendingBuildingRoute(*flag, BuildingRoute::Combat);
		CHECK(unit->dx == -1);
		CHECK(unit->dy == 0);
		for (int y = 7; y <= 9; ++y)
			for (int x = 0; x <= 2; ++x)
				if (x != 1 || y != 8)
					world.game.map.setAreaMask(world.game.map.coordToIndex(x, y), &Tile::forbidden,
											   world.team->me);
		unit->followPendingBuildingRoute(*flag, BuildingRoute::Combat);
		CHECK(unit->dx == 0);
		CHECK(unit->dy == 0);
		for (int y = 7; y <= 9; ++y)
			for (int x = 0; x <= 2; ++x)
				world.game.map.setAreaMask(world.game.map.coordToIndex(x, y), &Tile::forbidden, 0);
		unit->performance[WALK] = 0;
		unit->performance[SWIM] = 16;
		unit->followPendingBuildingRoute(*flag, BuildingRoute::Combat);
		CHECK(unit->dx == 0);
		CHECK(unit->dy == 0); // A pure swimmer cannot take a land step.
		unit->performance[SWIM] = 0;
		unit->followPendingBuildingRoute(*flag, BuildingRoute::Combat);
		CHECK(unit->dx == 0);
		CHECK(unit->dy == 0);
		CHECK(unit->entityRandom == random);
	}

	TEST_CASE("pending flag and clearing routes preserve the working job and clearing claim")
	{
		glob2test::HeadlessGlobals globals;
		for (bool clearing : {false, true})
		{
			CAPTURE(clearing);
			glob2test::HeadlessGame world({.clearImmobile = true, .header = true, .seed = 4921});
			world.game.map.configureGradientPipeline(0, 3);
			auto *unit = world.addUnit(clearing ? WORKER : WARRIOR, 4, 16);
			auto *flag = world.addBuilding(clearing ? "clearingflag" : "warflag", 20, 16);
			REQUIRE(unit);
			REQUIRE(flag);
			const auto purpose = clearing ? UnitJobPurpose::Clear : UnitJobPurpose::Defend;
			flag->unitsWorking.push_back(unit);
			unit->subscriptionSuccess(flag, false, true, purpose);
			if (clearing)
			{
				unit->displacement = Unit::DIS_CLEARING_RESOURCES;
				unit->previousClearingArea = Unit::ClearingAreaClaim{8, 8};
				unit->previousClearingAreaDistance = 7;
				world.game.map.setClearingAreaClaimed(8, 8, 0, unit->gid);
			}
			const auto random = unit->entityRandom;
			UnitActivityAccess::route(*unit);
			REQUIRE(UnitActivityAccess::pending(*unit));
			CHECK(unit->dx == 1);
			CHECK(unit->dy == 0);
			CHECK(unit->attachedBuilding == flag);
			CHECK(unit->targetBuilding == flag);
			CHECK(unit->jobPurpose == purpose);
			CHECK(unit->entityRandom == random);
			if (clearing)
			{
				REQUIRE(unit->previousClearingArea);
				CHECK(unit->previousClearingAreaDistance == 7);
				CHECK(world.game.map.isClearingAreaClaimed(8, 8, 0) == unit->gid);
			}
		}
	}

	TEST_CASE(
		"movement chosen while pending survives binary and text save continuation [artifacts]")
	{
		glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings = true});
		for (bool text : {false, true})
		{
			CAPTURE(text);
			glob2test::HeadlessGame world(
				{.clearImmobile = true, .loadDefaultRace = true, .header = true, .seed = 4921});
			auto &game = world.game;
			game.gameHeader.setBuildingGradientDelay(3);
			game.map.configureGradientPipeline(0, 3);
			auto *unit = world.addUnit(WORKER, 16, 16);
			auto *building = world.addBuilding("inn", 4, 4);
			REQUIRE(unit);
			REQUIRE(building);
			building->setMaterial(WHEAT, 20);
			unit->destinationPurpose = FEED;
			building->unitsInside.push_back(unit);
			unit->subscriptionSuccess(building, true, false, UnitJobPurpose::None);
			UnitActivityAccess::route(*unit);
			REQUIRE(UnitActivityAccess::pending(*unit));
			UnitActivityAccess::move(*unit);
			REQUIRE(unit->posX == 15);
			REQUIRE(unit->posY == 15);
			const auto before = state(game);
			auto *storage = new GAGCore::MemoryStreamBackend;
			std::unique_ptr<GAGCore::OutputStream> output(
				text ? static_cast<GAGCore::OutputStream *>(new GAGCore::TextOutputStream(storage))
					 : static_cast<GAGCore::OutputStream *>(
						   new GAGCore::BinaryOutputStream(storage)));
			game.save(output.get(), false, "pending steering continuation");
			output->flush();
			const auto bytes = storage->takeContents();
			glob2test::writeFile(glob2test::artifactDir() / (text ? "pending.txt" : "pending.game"),
								 bytes);
			std::vector<std::vector<Uint32>> expected;
			for (int tick = 0; tick < 128; ++tick)
			{
				game.step(0);
				expected.push_back(state(game));
			}
			const auto random = game.syncRandom;
			GameGUI resumed;
			auto backend =
				std::make_unique<GAGCore::MemoryStreamBackend>(bytes.data(), bytes.size());
			backend->seekFromStart(0);
			std::unique_ptr<GAGCore::InputStream> input(
				text ? static_cast<GAGCore::InputStream *>(
						   new GAGCore::TextInputStream(backend.release()))
					 : static_cast<GAGCore::InputStream *>(
						   new GAGCore::BinaryInputStream(backend.release())));
			REQUIRE(resumed.game.load(input.get()));
			resumed.game.setWaitingOnMask(0);
			REQUIRE(state(resumed.game) == before);
			for (int tick = 0; tick < 128; ++tick)
			{
				CAPTURE(tick);
				resumed.game.step(0);
				REQUIRE(state(resumed.game) == expected[tick]);
			}
			CHECK(resumed.game.syncRandom == random);
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
                if (!unit->performance[FLY] || world.game.map.hasAirRoutingConstraints())
                    glob2test::awaitBuildingGradient(world.game,service,unit->movementClass());
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
