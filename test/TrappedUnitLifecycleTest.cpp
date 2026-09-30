// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include "IntBuildingType.h"
#include "Utilities.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <vector>

namespace
{
namespace {
struct Fixture {
    GameGUI gui;
    Game& game = gui.game;
    Building* building;
    Unit* unit;
    int id;
    explicit Fixture(int resource = WOOD, int purpose = FEED) {
        setSyncRandSeed(180);
        game.setWaitingOnMask(0);
        game.map.setSize(6, 6, GRASS);
        game.map.setGame(&game);
        for (int t = 0; t < 2; ++t) {
            game.addTeam();
            game.teams[t]->race.loadDefault();
            game.teams[t]->playersMask = 1u << t;
        }
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                game.map.clearImmobileUnit(x, y);
        building = game.addBuilding(20, 20, globalContainer->buildingsTypes.getTypeNum(
            purpose == FEED ? "inn" : "racetrack", 0, false), 0);
        REQUIRE(building);
        unit = game.addUnit(20, 19, 0, WORKER, 0, 0, 0, 0);
        REQUIRE(unit);
        id = Unit::GIDtoID(unit->gid);
        game.map.setGroundUnit(20, 19, NOGUID);
        unit->posX = 20;
        unit->posY = 20;
        unit->attachedBuilding = building;
        unit->setTargetBuilding(building);
        building->unitsInside.push_back(unit);
        unit->activity = Unit::ACT_UPGRADING;
        unit->displacement = Unit::DIS_EXITING_BUILDING;
        unit->movement = Unit::MOV_INSIDE;
        unit->action = WALK;
        unit->delta = 255;
        unit->destinationPurpose = purpose;
        unit->insideTimeout = 0;
        unit->medical = purpose == FEED ? Unit::MED_HUNGRY : Unit::MED_FREE;
        unit->needToRecheckMedical = true;
        unit->hungry = 10;
        unit->hungriness = 2;
        unit->hp = 2;
        for (int y = 19; y <= 20 + building->type->height; ++y)
            for (int x = 19; x <= 20 + building->type->width; ++x) {
                if (x >= 20 && x < 20 + building->type->width && y >= 20 && y < 20 + building->type->height) continue;
                auto& r = game.map.getResource(x, y);
                r.type = resource;
                r.amount = 1;
            }
        // Keep the existing feeding-availability rule satisfied.
        // Training fixtures have a separate stocked inn.
        Building* food = purpose == FEED ? building : game.addBuilding(30, 30,
            globalContainer->buildingsTypes.getTypeNum("inn", 0, false), 0);
        REQUIRE(food);
        food->resources[WHEAT] = 10;
        food->updateConstructionState();
        int x, y, dx, dy;
        REQUIRE(!building->findGroundExit(&x, &y, &dx, &dy, false));
        REQUIRE(game.addUnit(40, 40, 1, WORKER, 0, 0, 0, 0));
    }
};
using Trace = std::vector<std::vector<Uint32>>;
std::vector<Uint32> state(Game& game, int id) {
    std::vector<Uint32> result{game.stepCounter};
    Unit* unit = game.teams[0]->myUnits[id];
    result.push_back(unit != nullptr);
    if (unit) unit->checkSum(&result);
    for (int t = 0; t < 2; ++t) {
        Team* team = game.teams[t];
        result.push_back(team->isAlive);
        result.push_back(team->hasLost);
        result.push_back(team->hasWon);
    }
    Building* building = game.teams[0]->myBuildings[0];
    REQUIRE(building);
    result.push_back(building->unitsInside.size());
    result.push_back(game.map.getGroundUnit(20, 20));
    return result;
}
Trace finish(Game& game, int id) {
    Trace trace;
    for (int tick = 0; tick < 128; ++tick) {
        game.syncStep(0);
        trace.push_back(state(game, id));
    }
    REQUIRE(game.teams[0]->myUnits[id] != nullptr);
    REQUIRE(!game.teams[0]->myUnits[id]->isDead);
    REQUIRE(game.teams[0]->myUnits[id]->hungry == 10);
    REQUIRE(game.teams[0]->myUnits[id]->hp == 2);
    REQUIRE(game.teams[0]->myBuildings[0]->unitsInside.size() == 1);
    REQUIRE((!game.teams[0]->isAlive && game.teams[0]->hasLost));
    REQUIRE(game.teams[1]->hasWon);
    return trace;
}
Trace eliminationAndSave(int resource, int purpose) {
    Fixture f(resource, purpose);
    REQUIRE((f.unit->hungry == 10 && !f.unit->isDead));
    REQUIRE((f.game.teams[0]->isAlive && !f.game.teams[0]->hasLost));
    REQUIRE(!f.game.teams[1]->hasWon);
    REQUIRE((f.unit->attachedBuilding == f.building && !f.building->unitsInside.empty()));
    auto* backend = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);
    f.game.save(&output, false, "trapped unit fixture");
    std::string bytes(backend->getBuffer(), backend->getPosition());
    const auto checkpointRng = syncRandEngine();
    const auto before = state(f.game, f.id);
    const Trace original = finish(f.game, f.id);
    GameGUI restored;
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
    input.seekFromStart(0);
    REQUIRE(restored.game.load(&input));
    restored.game.setWaitingOnMask(0);
    syncRandEngine() = checkpointRng;
    const auto after = state(restored.game, f.id);
    for (size_t i = 0; i < before.size(); ++i)
        if (after.at(i) != before[i]) std::cerr << "state[" << i << "] " << before[i] << " -> " << after[i] << std::endl;
    REQUIRE(after == before);
    REQUIRE(finish(restored.game, f.id) == original);
    Uint32 digest = 2166136261u;
    for (const auto& step : original)
        for (Uint32 value : step) digest = (digest ^ value) * 16777619u;
    std::cout << "TRACE resource=" << resource << " purpose=" << purpose
              << " steps=" << original.size() << " digest=" << digest << '\n';
    return original;
}
void rescue() {
    Fixture f;
    f.game.teams[0]->allies |= f.game.teams[1]->me;
    f.game.syncStep(0);
    REQUIRE((f.unit->hungry == 10 && f.game.teams[0]->isAlive));
    f.game.map.getResource(20, 19).clear();
    for (int i = 0; i < 16 && f.unit->attachedBuilding; ++i) f.game.syncStep(0);
    REQUIRE((f.game.teams[0]->myUnits[f.id] == f.unit && !f.unit->isDead));
    REQUIRE((f.unit->attachedBuilding == nullptr && f.building->unitsInside.empty()));
    REQUIRE(f.unit->movement == Unit::MOV_EXITING_BUILDING);
    REQUIRE(f.game.teams[0]->isAlive);
}
void allyProtection(int inactiveReason) {
    Fixture f;
    f.game.teams[0]->allies |= f.game.teams[1]->me;
    for (int i = 0; i < 32; ++i) f.game.syncStep(0);
    REQUIRE((f.game.teams[0]->isAlive && f.unit->hungry == 10));
    if (inactiveReason == 0) f.game.teams[1]->isAlive = false;
    if (inactiveReason == 1) f.game.teams[1]->playersMask = 0;
    if (inactiveReason == 2) f.game.teams[1]->hasLost = true;
    f.game.syncStep(0);
    REQUIRE(!f.game.teams[0]->isAlive);
}
void freeUnitProtection() {
    Fixture f;
    REQUIRE(f.game.addUnit(10, 10, 0, WORKER, 0, 0, 0, 0));
    for (int i = 0; i < 16; ++i) f.game.syncStep(0);
    REQUIRE((f.game.teams[0]->isAlive && f.unit->hungry == 10));
}
void openExitProtection() {
    Fixture f;
    f.game.map.getResource(20, 19).clear();
    // Even before its movement update, an available exit must protect the unit.
    f.unit->delta = 0;
    f.game.syncStep(0);
    REQUIRE(f.game.teams[0]->isAlive);
}
void productionRecovery(bool stocked, bool blocked) {
    Fixture f;
    Building* swarm = f.game.addBuilding(5, 5,
        globalContainer->buildingsTypes.getTypeNum("swarm", 0, false), 0);
    REQUIRE(swarm);
    f.game.teams[0]->addToStaticAbilitiesLists(swarm);
    swarm->resources[WHEAT] = stocked ? swarm->type->resourceForOneUnit : 0;
    swarm->productionTimeout = 100;
    // A player can enable production even when all sliders are at zero.
    for (int t = 0; t < NB_UNIT_TYPE; ++t) swarm->ratio[t] = 0;
    if (blocked) {
        for (int y = 4; y <= 5 + swarm->type->height; ++y)
            for (int x = 4; x <= 5 + swarm->type->width; ++x) {
                if (x >= 5 && x < 5 + swarm->type->width && y >= 5 && y < 5 + swarm->type->height) {
                    f.game.map.setAirUnit(x, y, 0);
                } else {
                    auto& r = f.game.map.getResource(x, y);
                    r.type = WOOD;
                    r.amount = 1;
                }
            }
    }
    f.game.syncStep(0);
    REQUIRE(f.game.teams[0]->isAlive == (stocked && !blocked));
    REQUIRE(f.unit->hungry == 10);
    if (stocked && !blocked) {
        swarm->ratio[WORKER] = 1;
        swarm->productionTimeout = 0;
        f.game.syncStep(0);
        REQUIRE(f.game.teams[0]->myUnits[1]);
        REQUIRE(f.game.teams[0]->isAlive);
    }
}
void activeService(int purpose) {
    Fixture f(WOOD, purpose);
    f.unit->displacement = Unit::DIS_INSIDE;
    f.unit->insideTimeout = -100;
    for (int i = 0; i < 16; ++i) f.game.syncStep(0);
    REQUIRE(f.game.teams[0]->myUnits[f.id] == f.unit);
    REQUIRE((f.unit->hungry == 10 && f.unit->hp == 2));
    REQUIRE((f.unit->insideTimeout < 0 && f.unit->attachedBuilding == f.building));
    REQUIRE(f.game.teams[0]->isAlive);
}
}
}

TEST_SUITE("TrappedUnitLifecycle")
{
	TEST_CASE("trapped elimination; indoor survival; rescue; service; hatchery recovery; repeated seeds and save continuation")
	{
		glob2test::HeadlessGlobals globals;
	    for (int resource : {WOOD, WHEAT})
	        for (int purpose : {FEED, WALK}) {
	            const auto first = eliminationAndSave(resource, purpose);
	            REQUIRE(eliminationAndSave(resource, purpose) == first);
	        }
	    rescue();
	    activeService(FEED);
	    activeService(WALK);
	    for (int reason = 0; reason < 3; ++reason) allyProtection(reason);
	    freeUnitProtection();
	    openExitProtection();
	    productionRecovery(true, false);
	    productionRecovery(false, false);
	    productionRecovery(true, true);
	}
}
