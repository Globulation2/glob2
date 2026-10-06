// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "BuildingType.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <sstream>
#include <nlohmann/json.hpp>

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
    for (int resource=0; resource<MAX_RESOURCES; ++resource) building->resources[resource]=64;
    return building;
}
std::vector<Uint32> components(Game& game)
{
    std::vector<Uint32> result, buildings, units;
    game.checkSum(&result, &buildings, &units, true);
    // Only map-header version metadata differs at an initial save boundary.
    // Keep every simulation, scheduling, unit and building component untouched.
    result.erase(result.begin());
    result.insert(result.end(), buildings.begin(), buildings.end());
    result.insert(result.end(), units.begin(), units.end());
    return result;
}
void invariants(Game& game)
{
    REQUIRE(game.integrity());
    for (int team=0; team<game.mapHeader.getNumberOfTeams(); ++team)
        for (int id=0; id<Building::MAX_COUNT; ++id)
        {
            auto* building=game.teams[team]->myBuildings[id];
            if (!building) continue;
            CHECK(building->unitsInside.size() <= std::size_t(building->maxUnitInside));
            for (int resource=0; resource<MAX_RESOURCES; ++resource)
            {
                CHECK(building->resources[resource] >= 0);
                CHECK(building->reservedResources[resource] >= 0);
                CHECK(building->reservedResources[resource] <= building->resources[resource]);
            }
        }
}
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
        for (int resource=0; resource<MAX_RESOURCES; ++resource)
            for (int n=0; n<4; ++n) world.game.map.setResource(5+resource*4, 22+n, resource, 5);
        world.team->createLists();
        world.game.setWaitingOnMask(0);
        for (int tick=0; tick<257; ++tick) world.step();
        invariants(world.game);
        auto* memory=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(memory);
        world.game.save(&output, false, "seeded capability composition"); output.flush();
        const auto bytes=memory->takeContents();
        std::vector<std::vector<Uint32>> states;
        std::vector<MersenneTwister> random;
        std::ostringstream golden;
        for (int tick=0; tick<257; ++tick)
        {
            world.step(); invariants(world.game);
            states.push_back(components(world.game)); random.push_back(world.game.syncRandom);
            if (tick % 32 == 0)
            {
                golden << tick+258;
                for (const auto value : states.back()) golden << ' ' << value;
                golden << '\n';
            }
        }
        GameGUI restored(false);
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
        input.seekFromStart(0);
        REQUIRE(restored.game.load(&input)); restored.game.setWaitingOnMask(0);
        CHECK(restored.game.buildingsTypes.fingerprint() == world.game.buildingsTypes.fingerprint());
        for (int tick=0; tick<257; ++tick)
        {
            CAPTURE(tick);
            restored.game.syncStep(0); invariants(restored.game);
            CHECK(components(restored.game) == states[tick]);
            CHECK(restored.game.syncRandom == random[tick]);
        }
        glob2test::expectGolden("building-catalog/composition/seed-" + std::to_string(seed) + ".trace", golden.str());
    }
}
TEST_CASE("format136 terrain snapshot imports frozen buildings and continues after resave")
{
    glob2test::HeadlessGlobals globals;
    const auto bytes=glob2test::readFile(glob2test::inflated("building-catalog/terrain136.game.gz"));
    GameGUI imported(false);
    GAGCore::BinaryInputStream oldInput(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    oldInput.seekFromStart(0);
    REQUIRE(imported.game.load(&oldInput)); imported.game.setWaitingOnMask(0);
    CHECK(imported.game.mapHeader.getVersionMinor()==136);
    CHECK(imported.game.buildingsTypes.size()==55);
    CHECK(imported.game.map.terrainRegistry().size()>=7);
    auto* memory=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(memory);
    imported.game.save(&output,false,"imported format136"); output.flush();
    const auto migrated=memory->takeContents();
    std::vector<std::vector<Uint32>> states;
    std::vector<std::vector<std::string>> orders;
    for(int tick=0;tick<64;++tick)
    {
        orders.push_back(glob2test::stepAI(imported.game));
        states.push_back(components(imported.game));
    }
    GameGUI restored(false);
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(migrated.data(),migrated.size()));
    input.seekFromStart(0);
    REQUIRE(restored.game.load(&input)); restored.game.setWaitingOnMask(0);
    for(int tick=0;tick<64;++tick)
    {
        CHECK(glob2test::stepAI(restored.game)==orders[tick]);
        CHECK(components(restored.game)==states[tick]);
    }
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
            for (int resource=0; resource<MAX_RESOURCES; ++resource)
            {
                int spent=0;
                for (int unit=0; unit<NB_UNIT_TYPE; ++unit)
                    spent += world.team->stats.measurements.births[unit] *
                        forge->type->semantics.production.recipes[unit].cost[resource];
                CHECK(forge->resources[resource] + spent == 64);
                CHECK(forge->availableResource(resource) >= 0);
            }
            if (tick % 53 == 0)
            {
                forge->cancelProduction();
                for (int resource=0; resource<MAX_RESOURCES; ++resource)
                    CHECK(forge->availableResource(resource) == forge->resources[resource]);
            }
        }
        for (int unit=0; unit<NB_UNIT_TYPE; ++unit)
            CHECK(world.team->stats.measurements.births[unit] > 0);
    }
}
}
