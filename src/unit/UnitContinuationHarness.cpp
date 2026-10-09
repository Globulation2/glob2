// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise clearing arbitration and idle activity across a real game save/load.
#include "EngineFixtures.h"
#include <string>
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Building.h"
#include "Utilities.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <vector>

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
    secondInn->materials[WHEAT] = 10;
    secondInn->update();
    firstInn->materials[WHEAT] = 10;
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
