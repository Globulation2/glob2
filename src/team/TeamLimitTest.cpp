// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AI.h"
#include "GameGUIDialog.h"
#include "Utilities.h"
#include "Player.h"
#include "shared_runtime/Runtime.h"
#include "CustomGameSetup.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include "BuildingUtils.h"
#include "UnitUtils.h"
#include "Ressource.h"
#include "Version.h"
#include "Unit.h"
#include "Building.h"
#include <GzipUtil.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <fstream>
#include <sstream>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <TextStream.h>

TEST_SUITE("TeamLimit")
{
TEST_CASE("sixteen controller slots survive binary and text headers [save-format]")
{
    CHECK(Team::MAX_COUNT == 16);
    glob2test::HeadlessGlobals globals;
    CustomGameSetup setup;
    setup.setCapacity(Team::MAX_COUNT);
    setup.random = false;
    for (int i = 0; i < Team::MAX_COUNT; ++i)
        REQUIRE(setup.setController(i, CustomGameSetup::Computer));
    CHECK(setup.controllerCount() == Team::MAX_COUNT);
    CHECK_FALSE(setup.setController(0, CustomGameSetup::Shared));
    GameHeader original;
    setup.writeHeader(original, "test");
    REQUIRE(original.getNumberOfPlayers() == 16);
    for (int i = 0; i < 16; ++i)
    {
        original.setAllyTeamNumber(i, i + 1);
        original.setAIConfig(i, "slot=" + std::to_string(i));
        auto unit = UnitUtils::GIDfrom(UnitUtils::MAX_COUNT - 1, i);
        auto building = BuildingUtils::GIDfrom(BuildingUtils::MAX_COUNT - 1, i);
        CHECK(UnitUtils::GIDtoTeam(unit) == i);
        CHECK(BuildingUtils::GIDtoTeam(building) == i);
        CHECK(UnitUtils::GIDtoID(unit) == UnitUtils::MAX_COUNT - 1);
        CHECK(BuildingUtils::GIDtoID(building) == BuildingUtils::MAX_COUNT - 1);
        CHECK(Team::teamNumberToMask(i) == Uint32(1) << i);
    }
    for (bool text : {false, true})
    {
        auto *storage = new GAGCore::MemoryStreamBackend;
        std::unique_ptr<GAGCore::OutputStream> output(text
            ? static_cast<GAGCore::OutputStream *>(new GAGCore::TextOutputStream(storage))
            : static_cast<GAGCore::OutputStream *>(new GAGCore::BinaryOutputStream(storage)));
        original.save(output.get());
        const auto bytes = storage->takeContents();
        GAGCore::MemoryStreamBackend source(bytes.data(), bytes.size());
        source.seekFromStart(0);
        std::unique_ptr<GAGCore::InputStream> input(text
            ? static_cast<GAGCore::InputStream *>(new GAGCore::TextInputStream(&source))
            : static_cast<GAGCore::InputStream *>(new GAGCore::BinaryInputStream(new GAGCore::MemoryStreamBackend(source))));
        GameHeader restored;
        REQUIRE(restored.load(input.get(), VERSION_MINOR));
        CHECK(restored.getNumberOfPlayers() == 16);
        for (int i = 0; i < 16; ++i)
        {
            CHECK(restored.getBasePlayer(i).teamNumber == i);
            CHECK(restored.getBasePlayer(i).number == i);
            CHECK(restored.getAllyTeamNumber(i) == i + 1);
            CHECK(restored.getAIConfig(i) == original.getAIConfig(i));
        }
    }
}

TEST_CASE("alliance and chat controls include the sixteenth controller")
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;
    options.teams = Team::MAX_COUNT;
    options.header = true;
    glob2test::HeadlessGame world(options);
    world.game.gameHeader.setAllyTeamsFixed(false);
    world.gui.localTeamNo = world.gui.localPlayer = 0;
    world.gui.adjustLocalTeam();
    InGameAllianceScreen dialog(&world.gui);
    REQUIRE(dialog.entries().size() == size_t(Team::MAX_COUNT - 1));
    CHECK(dialog.entries().back().player == Team::MAX_COUNT - 1);
    const auto last = Team::teamNumberToMask(Team::MAX_COUNT - 1);
    dialog.set(Team::MAX_COUNT - 1, InGameAllianceScreen::Alliance, true);
    CHECK((dialog.getAlliedMask() & last) == last);
    CHECK((dialog.getEnemyMask() & last) == 0);
    dialog.set(Team::MAX_COUNT - 1, InGameAllianceScreen::Chat, false);
    CHECK((dialog.getChatMask() & last) == 0);
    dialog.set(Team::MAX_COUNT - 1, InGameAllianceScreen::Chat, true);
    CHECK((dialog.getChatMask() & last) == last);
}

TEST_CASE("enemy team searches terminate when every team slot is occupied")
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;
    options.teams = Team::MAX_COUNT;
    options.header = true;
    glob2test::HeadlessGame world(options);
    auto *player = world.game.players[0];
    AISharedRuntime::Runtime runtime(nullptr, player);
    const auto enemies = [&]() {
        AISharedRuntime::Runtime::OwnerObservationScope observationScope(runtime);
        std::vector<int> teams;
        using AISharedRuntime::SearchTools::enemy_team_iterator;
        for (enemy_team_iterator it(runtime); it != enemy_team_iterator(); ++it)
        {
            REQUIRE(teams.size() < size_t(Team::MAX_COUNT));
            teams.push_back(*it);
        }
        return teams;
    };
    player->team->enemies = ~player->team->me;
    std::vector<int> expected;
    for (int team = 1; team < Team::MAX_COUNT; ++team) expected.push_back(team);
    CHECK(enemies() == expected);
    player->team->enemies = Team::teamNumberToMask(Team::MAX_COUNT - 1);
    CHECK(enemies() == std::vector<int>{Team::MAX_COUNT - 1});
    player->team->enemies = 0;
    CHECK(enemies().empty());
}

TEST_CASE("growth bands attribute overlapping coverage to all sixteen teams")
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;
    options.wDec = options.hDec = 7;
    options.teams = Team::MAX_COUNT;
    glob2test::HeadlessGame world(options);
    auto &game = world.game;
    for (int t = 0; t < Team::MAX_COUNT; ++t)
    {
        auto &stats = game.teams[t]->stats;
        stats.coverageBuildings = {{8, 8, 1, 1}, {8, 8, 1, 1}};
        ++stats.coverageBuildingGeneration;
    }
    game.map.rebuildGrowthCoverage();
    for (int distance : {8, 9, 16, 17, 32, 33})
    {
        const int x = 8 + distance;
        REQUIRE(game.map.incResource(x, 8, WHEAT, 0));
        game.map.recordNaturalGrowth(x, 8, WHEAT, NO_RES_TYPE, 0);
    }
    for (int t = 0; t < Team::MAX_COUNT; ++t)
    {
        auto &m = game.teams[t]->stats.measurements;
        CHECK(m.growthTiles[0][WHEAT] == 1);
        CHECK(m.growthTiles[1][WHEAT] == 3);
        CHECK(m.growthTiles[2][WHEAT] == 5);
        CHECK(m.growthGlobal[0][WHEAT] == 6);
        game.teams[t]->stats.coverageBuildings.pop_back();
        ++game.teams[t]->stats.coverageBuildingGeneration;
    }
    game.map.rebuildGrowthCoverage();
    const size_t tile = 8 * game.map.getW() + 8;
    const Uint64 expected = (Uint64(1) << (GROWTH_COVERAGE_BANDS * Team::MAX_COUNT)) - 1;
    CHECK(game.map.growthCoverage[tile] == expected);
    game.teams[15]->stats.coverageBuildings.clear();
    ++game.teams[15]->stats.coverageBuildingGeneration;
    game.map.rebuildGrowthCoverage();
    for (int band = 0; band < GROWTH_COVERAGE_BANDS; ++band)
    {
        CHECK((game.map.growthCoverage[tile] & (Uint64(1) << (band * Team::MAX_COUNT + 15))) == 0);
        CHECK((game.map.growthCoverage[tile] & (Uint64(1) << (band * Team::MAX_COUNT + 14))) != 0);
    }
}

TEST_CASE("sixteen team saves continue identically and preserve entity generations [save-format][artifacts]")
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;
    options.wDec = options.hDec = 7;
    options.teams = Team::MAX_COUNT;
    options.loadDefaultRace = options.header = options.clearImmobile = true;
    options.seed = 271828;
    glob2test::HeadlessGame world(options);
    auto &game = world.game;
    game.gameHeader.setHungerDisabled(true);
    REQUIRE(game.sgslScript.compileScript(&game, "").type == ErrorReport::ET_OK);
    for (int t = 0; t < Team::MAX_COUNT; ++t)
    {
        const int x = 8 + (t % 4) * 30, y = 8 + (t / 4) * 30;
        world.addBuilding("swarm", x, y, 0, t);
        world.addUnit(WORKER, x + 6, y, t);
    }
    // Exercise both ends of real Maxima controller storage, including enemy 15
    // in controller zero's opponent assessment, before preserving its director.
    auto header = game.gameHeader;
    for (int player : {0, Team::MAX_COUNT - 1})
        header.getBasePlayer(player).type = BasePlayer::playerTypeFromImplementationID(AI::MAXIMA);
    game.setGameHeader(header, true);
    for (int tick = 0; tick < 16; ++tick) glob2test::stepAI(game);
    auto *storage = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(storage);
    game.save(&output, false, "sixteen teams");
    const auto bytes = storage->takeContents();
    glob2test::writeFile(glob2test::artifactDir() / "sixteen-teams.game", bytes);
    const auto identities = game.scriptGenerations;
    std::vector<Uint32> expected;
    for (int tick = 0; tick < 64; ++tick)
    {
        glob2test::stepAI(game);
        expected.push_back(game.checkSum());
    }
    GameGUI restored;
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
    input.seekFromStart(0);
    REQUIRE(restored.game.load(&input));
    CHECK(restored.game.teamsCount() == 16);
    CHECK(restored.game.scriptGenerations == identities);
    std::ostringstream trace;
    for (int tick = 0; tick < 64; ++tick)
    {
        glob2test::stepAI(restored.game);
        const auto checksum = restored.game.checkSum();
        CHECK(checksum == expected[tick]);
        trace << tick + 1 << ',' << checksum << '\n';
    }
    glob2test::writeFile(glob2test::artifactDir() / "checksums.csv", trace.str());
}

TEST_CASE("retained format126 Maxima save preserves identities and subsequent AI decisions [save-format][artifacts]")
{
    glob2test::HeadlessGlobals globals;
    GameGUI legacy;
    GAGCore::BinaryInputStream input(glob2OpenMapOrSaveInputStreamBackend(
        *GAGCore::Toolkit::getFileManager(), glob2test::fixture("team-limit/pre-v127-maxima.game.gz").string()));
    REQUIRE(legacy.game.load(&input));
    CHECK(legacy.game.mapHeader.getVersionMinor() == 126);
    CHECK(legacy.game.teamsCount() == 4);
    for (int t = 0; t < legacy.game.teamsCount(); ++t)
        for (int slot = 0; slot < Game::SCRIPT_ENTITY_SLOTS_PER_TEAM; ++slot)
        {
            if (auto *unit = legacy.game.teams[t]->myUnits[slot])
                CHECK(unit->scriptIdentity == legacy.game.scriptGenerations[Game::scriptGenerationIndex(false, t, slot)]);
            if (auto *building = legacy.game.teams[t]->myBuildings[slot])
                CHECK(building->scriptIdentity == legacy.game.scriptGenerations[Game::scriptGenerationIndex(true, t, slot)]);
        }
    for (int t = 12; t < Team::MAX_COUNT; ++t)
        for (int slot = 0; slot < Game::SCRIPT_ENTITY_SLOTS_PER_TEAM; ++slot)
        {
            CHECK(legacy.game.scriptGenerations[Game::scriptGenerationIndex(false, t, slot)] == 0);
            CHECK(legacy.game.scriptGenerations[Game::scriptGenerationIndex(true, t, slot)] == 0);
        }
    auto *storage = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(storage);
    legacy.game.save(&output, false, "migrated team capacity");
    const auto bytes = storage->takeContents();
    const auto generations = legacy.game.scriptGenerations;
    GameGUI restored;
    GAGCore::BinaryInputStream newInput(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
    newInput.seekFromStart(0);
    REQUIRE(restored.game.load(&newInput));
    CHECK(restored.game.scriptGenerations == generations);
    std::vector<Uint32> before, after, beforeBuildings, afterBuildings, beforeUnits, afterUnits;
    legacy.game.checkSum(&before, &beforeBuildings, &beforeUnits);
    restored.game.checkSum(&after, &afterBuildings, &afterUnits);
    // Saving upgrades the MapHeader format field. These checks cover the
    // simulation/entity checksums; AI director state is checked separately below.
    before.erase(before.begin());
    after.erase(after.begin());
    CHECK(after == before);
    CHECK(afterBuildings == beforeBuildings);
    CHECK(afterUnits == beforeUnits);
    const auto directors = [](Game &game) {
        std::vector<std::string> states;
        for (int p = 0; p < game.gameHeader.getNumberOfPlayers(); ++p)
        {
            REQUIRE(game.players[p]->ai != nullptr);
            auto *memory = new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream out(memory);
            game.players[p]->ai->save(&out);
            states.push_back(memory->takeContents());
        }
        return states;
    };
    CHECK(directors(restored.game) == directors(legacy.game));
    CHECK(restored.game.syncRandom == legacy.game.syncRandom);
    std::ostringstream trace;
    for (int tick = 0; tick < 128; ++tick)
    {
        CAPTURE(tick);
        const auto expectedOrders = glob2test::stepAI(legacy.game);
        CHECK(glob2test::stepAI(restored.game) == expectedOrders);
        CHECK(restored.game.syncRandom == legacy.game.syncRandom);
        before.clear(); beforeBuildings.clear(); beforeUnits.clear();
        after.clear(); afterBuildings.clear(); afterUnits.clear();
        legacy.game.checkSum(&before, &beforeBuildings, &beforeUnits, true);
        restored.game.checkSum(&after, &afterBuildings, &afterUnits, true);
        before.erase(before.begin()); after.erase(after.begin());
        CHECK(after == before);
        CHECK(afterBuildings == beforeBuildings);
        CHECK(afterUnits == beforeUnits);
        // The header format intentionally differs; hash the compared vectors.
        const auto digest = [](const auto &state, const auto &buildings, const auto &units) {
            Uint32 hash = 2166136261u;
            for (const auto *values : {&state, &buildings, &units})
                for (Uint32 value : *values) hash = (hash ^ value) * 16777619u;
            return hash;
        };
        trace << tick + 1 << ',' << digest(before, beforeBuildings, beforeUnits)
              << ',' << digest(after, afterBuildings, afterUnits) << '\n';
    }
    CHECK(directors(restored.game) == directors(legacy.game));
    glob2test::writeFile(glob2test::artifactDir() / "migration-ai-checksums.csv", trace.str());

}

TEST_CASE("script generation slot counts reject missing capacity and truncated planes [save-format]")
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;
    options.wDec = options.hDec = 6;
    options.teams = 2;
    options.loadDefaultRace = options.header = true;
    glob2test::HeadlessGame world(options);
    REQUIRE(world.game.sgslScript.compileScript(&world.game, "").type == ErrorReport::ET_OK);
    struct SlotWriter : GAGCore::BinaryOutputStream
    {
        using BinaryOutputStream::BinaryOutputStream;
        size_t slotsOffset = 0;
        void writeUint32(Uint32 value, const std::string name) override
        {
            if (name == "teamSlots") slotsOffset = getPosition();
            BinaryOutputStream::writeUint32(value, name);
        }
    };
    auto *memory = new GAGCore::MemoryStreamBackend;
    SlotWriter out(memory);
    world.game.save(&out, false, "slot count bounds");
    const auto saved = memory->takeContents();
    REQUIRE(out.slotsOffset > 0);
    auto loads = [](const std::string &bytes) {
        GameGUI candidate;
        GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
        in.seekFromStart(0);
        return candidate.game.load(&in);
    };
    REQUIRE(loads(saved));
    for (Uint32 slots : {0u, 1u, Uint32(Team::MAX_COUNT + 1), Uint32(-1)})
    {
        CAPTURE(slots);
        auto corrupt = saved;
        // Use the actual binary serializer's network byte order, so count one
        // exercises undersized capacity rather than accidentally becoming huge.
        auto *patchMemory = new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream patch(patchMemory);
        patch.writeUint32(slots, "teamSlots");
        corrupt.replace(out.slotsOffset, sizeof(Uint32), patchMemory->takeContents());
        CHECK_THROWS_WITH_AS(loads(corrupt), "Invalid script generation team-slot count", std::runtime_error);
    }
    // An empty sparse table is exactly eight bytes: capacity and zero count.
    CHECK_THROWS(loads(saved.substr(0, out.slotsOffset + 7)));
}

TEST_CASE("dense designed maps accept thirteen through sixteen only on the largest square")
{
    for (const char *id : {"gauntlet", "faulted-city", "portage-lakes", "hungry-marches", "encircled-kingdom"})
    {
        const auto &registry = GeneratorRegistry::builtins();
        GenerationRequest request;
        request.setMethodDefaults(registry.idOf(id));
        const auto &definition = registry.at(request.method);
        for (int count = 13; count <= Team::MAX_COUNT; ++count)
        {
            CAPTURE(id);
            CAPTURE(count);
            request.nbTeams = count;
            request.wDec = request.hDec = 9;
            CHECK(validateGenerationRequest(request, definition).empty());
            request.hDec = 8;
            CHECK_FALSE(validateGenerationRequest(request, definition).empty());
            request.hDec = 9;
            request.wDec = 8;
            CHECK_FALSE(validateGenerationRequest(request, definition).empty());
        }
        request.wDec = request.hDec = 9;
        request.nbTeams = 17;
        CHECK_FALSE(validateGenerationRequest(request, definition).empty());
    }
}
}
