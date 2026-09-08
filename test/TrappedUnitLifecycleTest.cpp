// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
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
#include <cassert>
#include <iostream>
#include <vector>

GlobalContainer* globalContainer = nullptr;
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
        assert(building);
        unit = game.addUnit(20, 19, 0, WORKER, 0, 0, 0, 0);
        assert(unit);
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
        // Keep the team viable under the existing feeding-availability rule until
        // its last worker dies. Training fixtures have a separate stocked inn.
        Building* food = purpose == FEED ? building : game.addBuilding(30, 30,
            globalContainer->buildingsTypes.getTypeNum("inn", 0, false), 0);
        assert(food);
        food->resources[CORN] = 10;
        food->updateConstructionState();
        int x, y, dx, dy;
        assert(!building->findGroundExit(&x, &y, &dx, &dy, false));
        assert(game.addUnit(40, 40, 1, WORKER, 0, 0, 0, 0));
        // The gradient scheduler expects an in-use field; allocation is now lazy.
        game.map.getResourceGradient(0, WOOD, 0);
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
    assert(building);
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
    assert(game.teams[0]->myUnits[id] == nullptr);
    assert(game.teams[0]->myBuildings[0]->unitsInside.empty());
    assert(!game.teams[0]->isAlive && game.teams[0]->hasLost);
    assert(game.teams[1]->hasWon);
    return trace;
}
Trace starvationAndSave(int resource, int purpose) {
    Fixture f(resource, purpose);
    f.game.syncStep(0);
    assert(f.unit->hungry == 8 && !f.unit->isDead);
    assert(f.game.teams[0]->isAlive && !f.game.teams[0]->hasLost);
    assert(!f.game.teams[1]->hasWon);
    assert(f.unit->attachedBuilding == f.building && !f.building->unitsInside.empty());
    auto* backend = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);
    f.game.save(&output, false, "trapped unit fixture");
    std::string bytes(backend->getBuffer(), backend->getPosition());
    const auto checkpointRng = randomGenerator;
    const auto before = state(f.game, f.id);
    const Trace original = finish(f.game, f.id);
    GameGUI restored;
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
    input.seekFromStart(0);
    assert(restored.game.load(&input));
    restored.game.setWaitingOnMask(0);
    restored.game.map.getResourceGradient(0, WOOD, 0);
    randomGenerator = checkpointRng;
    const auto after = state(restored.game, f.id);
    for (size_t i = 0; i < before.size(); ++i)
        if (after.at(i) != before[i]) std::cerr << "state[" << i << "] " << before[i] << " -> " << after[i] << std::endl;
    assert(after == before);
    assert(finish(restored.game, f.id) == original);
    Uint32 digest = 2166136261u;
    for (const auto& step : original)
        for (Uint32 value : step) digest = (digest ^ value) * 16777619u;
    std::cout << "TRACE resource=" << resource << " purpose=" << purpose
              << " steps=" << original.size() << " digest=" << digest << '\n';
    return original;
}
void rescue() {
    Fixture f;
    f.game.syncStep(0);
    assert(f.unit->hungry == 8);
    f.game.map.getResource(20, 19).clear();
    for (int i = 0; i < 16 && f.unit->attachedBuilding; ++i) f.game.syncStep(0);
    assert(f.game.teams[0]->myUnits[f.id] == f.unit && !f.unit->isDead);
    assert(f.unit->attachedBuilding == nullptr && f.building->unitsInside.empty());
    assert(f.unit->movement == Unit::MOV_EXITING_BUILDING);
}
void activeService() {
    Fixture f;
    f.unit->displacement = Unit::DIS_INSIDE;
    f.unit->insideTimeout = -100;
    for (int i = 0; i < 16; ++i) f.game.syncStep(0);
    assert(f.game.teams[0]->myUnits[f.id] == f.unit);
    assert(f.unit->hungry == 10 && f.unit->hp == 2);
    assert(f.unit->insideTimeout < 0 && f.unit->attachedBuilding == f.building);
}
}
int main(int argc, char** argv) {
    SDL_SetMainReady();
    assert(argc == 2 && std::string(argv[1]).find("glob2-save-test-") == 0);
    GlobalContainer container(argv[1]);
    globalContainer = &container;
    container.runNoX = true;
    container.settings.rememberUnit = false;
    container.buildingsTypes.init();
    IntBuildingType::init();
    for (int resource : {WOOD, CORN})
        for (int purpose : {FEED, WALK}) {
            const auto first = starvationAndSave(resource, purpose);
            assert(starvationAndSave(resource, purpose) == first);
        }
    rescue();
    activeService();
    std::cout << "PASS: trapped lifecycle, elimination, rescue, active service, repeated seeds and save continuation\n";
    return 0;
}
