// SPDX-License-Identifier: GPL-3.0-or-later
#include "Material.h"
#include "Map.h"
#include "ProfileNeeds.h"
#include "HeadlessGlobals.h"
#include "HeadlessGame.h"
#include "GameFixtures.h"
#include "Continuation.h"
#include "SimulationInvariants.h"
#include "BuildingType.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <sstream>
#include <nlohmann/json.hpp>
#include "Unit.h"
#include "Building.h"

namespace
{
void installFixture(Game& game, int seed)
{
    game.buildingsTypes.loadManifest(glob2test::fixture("building-catalog/composition/seed-" +
        std::to_string(seed) + ".manifest.json").string());
    game.configureBuildingCatalog();
}
Building* place(glob2test::HeadlessGame& world, const char* key, int x, int y)
{
    const int type = world.game.buildingsTypes.findByKey(key);
    REQUIRE(type >= 0);
    auto* building = world.game.addBuilding(x, y, type, 0, 0, 0);
    REQUIRE(building);
    if (building->type->semantics.occupiesGround)
        world.game.map.setBuilding(x, y, building->type->width, building->type->height, building->gid);
    world.team->addToStaticAbilitiesLists(building);
    for (int resource=0; resource<MaterialCount; ++resource) building->setMaterial(resource, 64);
    return building;
}
// Keep the historical trace representation while save/resume comparisons retain
// current simulation components except the normalized map-header version.
std::vector<Uint32> historicalGoldenState(Game& game) { return glob2test::simulationComponents(game, true); }
}
TEST_SUITE("BuildingCatalogFixtures")
{
TEST_CASE("ordinary building aliases never suppress sprite binding [display]")
{
    glob2test::HeadlessGlobals globals({.display=true});
    BuildingsTypes catalog;
    catalog.initLegacy();
    auto json = nlohmann::json::parse(catalog.snapshotJson());
    json["variants"][1]["properties"]["type"] = "null";
    catalog.loadSnapshotJson(json.dump());
    REQUIRE(catalog.get(1)->miniSpriteImage >= 0);
    REQUIRE(catalog.get(1)->gameSpritePtr == nullptr);
    catalog.loadSprites();
    CHECK(catalog.get(1)->gameSpritePtr != nullptr);
    CHECK(catalog.get(1)->miniSpritePtr != nullptr);
}

TEST_CASE("retained seeded compositions preserve full simulation continuation [golden]")
{
    glob2test::HeadlessGlobals globals;
    for (const int seed : {713, 714, 715})
    {
        CAPTURE(seed);
        glob2test::HeadlessGame world({.wDec=6, .hDec=6, .discovered=true,
            .clearImmobile=true, .loadDefaultRace=true, .header=true, .seed=Uint32(seed)});
        installFixture(world.game, seed);
        place(world, "refuge", 4, 4);
        place(world, "forge", 16, 8);
        place(world, "signal", 30, 30);
        place(world, "warehouse", 40, 40);
        for (int unit=0; unit<9; ++unit) world.addUnit(unit % NB_UNIT_TYPE, 4+unit, 14);
        for (int resource=0; resource<MaterialCount; ++resource)
            for (int n=0; n<4; ++n) world.game.map.setResourceByIndex(5+resource*4, 22+n, resource, 5);
        world.team->createLists();
        world.game.setWaitingOnMask(0);
        for (int tick=0; tick<257; ++tick) world.step();
        glob2test::expectBuildingInvariants(world.game);
        std::ostringstream golden;
        const auto result=glob2test::expectContinuation(world.game, {.ticks=257,
            .step=[](Game& game) { glob2test::stepEngine(game); return std::vector<std::string>{}; },
            .name="seeded capability composition",
            .afterLoad=[](Game& original, Game& loaded) {
                CHECK(loaded.buildingsTypes.fingerprint() == original.buildingsTypes.fingerprint());
            },
            .each=[&](Game& original, Game& loaded, int tick) {
                glob2test::expectBuildingInvariants(original); glob2test::expectBuildingInvariants(loaded);
                if (tick % 32 == 0)
                {
                    golden << tick+258;
                    for (const auto value : historicalGoldenState(original)) golden << ' ' << value;
                    golden << '\n';
                }
            }});
        REQUIRE(result.matched);
        glob2test::expectGolden("building-catalog/composition/seed-" + std::to_string(seed) + ".trace", golden.str());
    }
}
TEST_CASE("format136 terrain snapshot imports frozen buildings and continues after resave" * glob2test::needs({"ai:castor"}))
{
    glob2test::HeadlessGlobals globals;
    const auto bytes=glob2test::readFile(glob2test::inflated("building-catalog/terrain136.game.gz"));
    GameGUI imported(false);
    GAGCore::BinaryInputStream oldInput(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    oldInput.seekFromStart(0);
    REQUIRE(imported.game.load(&oldInput)); imported.game.setWaitingOnMask(0);
    CHECK(imported.game.mapHeader.getVersionMinor()==136);
    // A pre-snapshot file gets the frozen legacy table, never the repository catalog,
    // whatever buildings have been added since.
    BuildingsTypes legacy; legacy.initLegacy();
    CHECK(imported.game.buildingsTypes.size()==legacy.size());
    CHECK(imported.game.buildingsTypes.fingerprint()==legacy.fingerprint());
    CHECK(imported.game.map.terrainRegistry().size()>=7);
    // Resave in the current format, then continue both under the AI scheduler.
    const auto result=glob2test::expectContinuation(imported.game, {.ticks=64, .step=glob2test::stepAI,
        .name="imported format136"});
    CHECK(result.matched);
}
TEST_CASE("retained split recipes conserve every charged resource through cancellation")
{
    glob2test::HeadlessGlobals globals;
    for (const int seed : {713, 714})
    {
        CAPTURE(seed);
        glob2test::HeadlessGame world({.wDec=6, .hDec=6, .loadDefaultRace=true, .header=true});
        installFixture(world.game, seed);
        auto* forge=place(world, "forge", 16, 8);
        for (int tick=0; tick<300; ++tick)
        {
            forge->swarmStep();
            for (int resource=0; resource<MaterialCount; ++resource)
            {
                int spent=0;
                for (int unit=0; unit<NB_UNIT_TYPE; ++unit)
                    spent += world.team->stats.measurements().births[unit] *
                        forge->type->semantics.production.recipes[unit].cost[resource];
                CHECK(forge->material(resource) + spent == 64);
                CHECK(forge->availableMaterial(resource) >= 0);
            }
            if (tick % 53 == 0)
            {
                forge->cancelProduction();
                for (int resource=0; resource<MaterialCount; ++resource)
                    CHECK(forge->availableMaterial(resource) == forge->material(resource));
            }
        }
        for (int unit=0; unit<NB_UNIT_TYPE; ++unit)
            CHECK(world.team->stats.measurements().births[unit] > 0);
    }
}
}
