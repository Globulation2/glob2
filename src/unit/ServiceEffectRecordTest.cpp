// SPDX-License-Identifier: GPL-3.0-or-later
//
// A unit a building heals, feeds or trains records one render-only effect
// (render/UnitEffects.h) when it comes out, and none when it leaves without the
// whole service: expelled mid-stay, or a trainee while upgrades are disabled.
#include "HeadlessGlobals.h"
#include "HeadlessGame.h"
#include "BuildingType.h"
#include "render/GameAnimations.h"
#include "render/UnitEffects.h"
#include <nlohmann/json.hpp>
#include <vector>

namespace
{
//! Headless games do not record animations; give this one a container that does.
void recordAnimations(Game& game)
{
    game.animations = std::make_unique<GameAnimations>(true, game.map.getSectorW() * game.map.getSectorH());
}

std::vector<UnitEffectAnimation> effects(const Game& game)
{
    std::vector<UnitEffectAnimation> all;
    for (int i = 0; i < game.map.getSectorW() * game.map.getSectorH(); ++i)
        for (const auto& effect : game.animations->getUnitEffects(i)) all.push_back(effect);
    return all;
}

void admit(Building* building, Unit* unit, int purpose)
{
    unit->destinationPurpose = purpose;
    REQUIRE(building->canOfferService(unit, purpose));
    building->subscribeUnitForInside(unit);
    REQUIRE(unit->serviceResourcesReserved());
    unit->displacement = Unit::DIS_ENTERING_BUILDING;
    unit->action = STOP_WALK; unit->dx = unit->dy = 0; unit->delta = 255;
}

//! Step until the unit has come out of its building; no effect may play before.
void stepOut(Unit* unit, const Game& game)
{
    for (int tick = 0; tick < 4096 && unit->attachedBuilding; ++tick)
    {
        unit->syncStep();
        if (unit->attachedBuilding) REQUIRE(effects(game).empty());
    }
    REQUIRE(unit->attachedBuilding == nullptr);
}

//! The inn variant trains walking instead, as the services tests do.
void walkTrainingInn(Game& game)
{
    auto catalog = nlohmann::json::parse(game.buildingsTypes.snapshotJson());
    auto& b = catalog["variants"][3];
    b["semantics"]["training"]["walk"] = {{"enabled", true}, {"unitMask", 1}, {"targetLevel", 1}, {"duration", 2}};
    game.buildingsTypes.loadSnapshotJson(catalog.dump());
    game.configureBuildingCatalog();
}
}

TEST_SUITE("ServiceEffectRecord")
{
TEST_CASE("a healed unit plays the hospital's effect where it comes out")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace = true});
    recordAnimations(world.game);
    Building* hospital = world.addBuilding("hospital", 8, 8);
    Unit* patient = world.addUnit(WORKER, 7, 8);
    patient->hp = patient->performance[HP] - 1;
    for (int resource = 0; resource < MaterialSlotCount; ++resource)
        hospital->setMaterial(resource, hospital->type->semantics.healing.cost[resource]);
    admit(hospital, patient, HEAL);
    stepOut(patient, world.game);

    const auto all = effects(world.game);
    REQUIRE(all.size() == 1);
    CHECK(all[0].kind == Uint8(UnitEffect::Kind::Heal));
    CHECK(all[0].unitGid == patient->gid);
    CHECK(all[0].buildingGid == hospital->gid);
    CHECK(all[0].buildingType == hospital->typeNum);
    CHECK(all[0].level == 0);
    CHECK(all[0].x == patient->posX);
    CHECK(all[0].y == patient->posY);
}

TEST_CASE("a trained unit plays the effect with its new level")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace = true});
    recordAnimations(world.game);
    walkTrainingInn(world.game);
    Building* school = world.addBuilding("inn", 8, 8);
    Unit* worker = world.addUnit(WORKER);
    admit(school, worker, WALK);
    stepOut(worker, world.game);

    REQUIRE(worker->level[WALK] == 1);
    const auto all = effects(world.game);
    REQUIRE(all.size() == 1);
    CHECK(all[0].kind == Uint8(UnitEffect::Kind::Train));
    CHECK(all[0].level == 2); // walk levels are displayed 1-based
}

TEST_CASE("a trainee leaving while upgrades are disabled plays nothing")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace = true});
    recordAnimations(world.game);
    walkTrainingInn(world.game);
    Building* school = world.addBuilding("inn", 8, 8);
    Unit* worker = world.addUnit(WORKER);
    admit(school, worker, WALK);
    world.game.gameHeader.setUnitUpgradesDisabled(true);
    stepOut(worker, world.game);
    CHECK(worker->level[WALK] == 0);
    CHECK(effects(world.game).empty());
}

TEST_CASE("an expelled unit plays the effect only after its whole service")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace = true});
    recordAnimations(world.game);
    Building* hospital = world.addBuilding("hospital", 8, 8);
    for (int resource = 0; resource < MaterialSlotCount; ++resource)
        hospital->setMaterial(resource, 2 * hospital->type->semantics.healing.cost[resource]);
    Unit* early = world.addUnit(WORKER, 7, 8);
    early->hp = early->performance[HP] - 1;
    admit(hospital, early, HEAL);
    early->syncStep();
    REQUIRE(early->displacement == Unit::DIS_INSIDE);
    int x, y, dx, dy;
    REQUIRE(hospital->findExpelTile(false, false, &x, &y, &dx, &dy));
    early->expelFromBuilding(x, y, dx, dy);
    CHECK(effects(world.game).empty());

    Unit* served = world.addUnit(WORKER, 7, 9);
    served->hp = served->performance[HP] - 1;
    admit(hospital, served, HEAL);
    // Wall the hospital in with wood so the healed unit waits inside to come out.
    const int left = hospital->posX - 1, top = hospital->posY - 1;
    const int right = hospital->posX + hospital->type->width, bottom = hospital->posY + hospital->type->height;
    for (int wy = top; wy <= bottom; ++wy)
        for (int wx = left; wx <= right; ++wx)
            if (wx == left || wx == right || wy == top || wy == bottom)
            {
                world.game.map.setGroundUnit(wx, wy, NOGUID);
                auto wood = world.game.map.getResource(wx, wy);
                wood.type = WOOD; wood.amount = 1;
                world.game.map.replaceResource(wx, wy, wood);
            }
    for (int tick = 0; tick < 4096 && served->displacement != Unit::DIS_EXITING_BUILDING; ++tick)
        served->syncStep();
    REQUIRE(served->displacement == Unit::DIS_EXITING_BUILDING);
    served->syncStep();
    REQUIRE(served->attachedBuilding != nullptr);
    CHECK(effects(world.game).empty());
    auto cleared = world.game.map.getResource(left, top);
    cleared.clear();
    world.game.map.replaceResource(left, top, cleared);
    served->expelFromBuilding(left, top, -1, -1);
    const auto all = effects(world.game);
    REQUIRE(all.size() == 1);
    CHECK(all[0].unitGid == served->gid);
    CHECK(all[0].kind == Uint8(UnitEffect::Kind::Heal));
}
}
