// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Engine.h"
#include "field/GradientConstants.h"
#include <BackgroundFileWriter.h>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <ThreadSupport.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <nlohmann/json.hpp>

TEST_SUITE("SharedWorkerLifecycle") {
TEST_CASE("background saves finish on a worker and remain readable after teardown") {
    glob2test::HeadlessGlobals globals;
    const auto path = std::filesystem::path(SDL_getenv("GLOB2_USER_DATA_DIR")) / "worker-save.txt";
    const auto submittingThread = std::this_thread::get_id();
    std::thread::id writingThread;
    {
        GAGCore::BackgroundFileWriter writer(globals->fileManager);
        writer.write(path.string(), "finished", [&](std::string &bytes) {
            writingThread = std::this_thread::get_id();
            bytes += " on worker";
        });
        writer.waitUntilIdle();
    }
    std::ifstream file(path);
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    REQUIRE(bytes == "finished on worker");
    REQUIRE((writingThread != submittingThread) == GAGCore::ThreadSupport::available);
}
TEST_CASE("resource catalog import drains deferred material preparation before replacing stocks") {
    glob2test::HeadlessGlobals globals;
    GameGUI source;
    GameHeader header;
    header.setNumberOfPlayers(1);
    header.setRandomSeed(123456);
    header.getBasePlayer(0) = BasePlayer(0, "Test", 0, BasePlayer::P_LOCAL);
    REQUIRE(source.loadFromHeaders(Engine::loadMapHeader("maps/balanced.map"), header, true, true));
    source.game.edit = true;
    auto &map = source.game.map;
    using Json = nlohmann::json;
    const auto trees = *map.resourceRegistry().find("trees");
    auto definition = Json::parse(map.resourceRegistry().serialize())["resources"][resourceIndex(trees)];
    definition["key"] = "test:deferred-compound";
    definition["properties"]["primaryMaterial"] = "gold";
    definition["properties"]["persistsWhenEmpty"] = true;
    definition["yields"] = {{"gold", {{"capacity", 4}, {"initial", 2}, {"consumption", "one"}}},
        {"food", {{"capacity", 4}, {"initial", 3}, {"consumption", "one"}}}};
    auto install = [&] {
        map.installResourceDefinitions(Json{{"schemaVersion", 1}, {"resources", Json::array({definition})}}.dump());
    };
    install();
    const auto resource = *map.resourceRegistry().find("test:deferred-compound");
    size_t index = 0;
    for (; index < size_t(map.getW()) * map.getH(); ++index) {
        const auto &tile = map.getTile(index % map.getW(), index / map.getW());
        if (tile.building == NOGBID && tile.groundUnit == NOGUID && !tile.forbidden && map.terrainTypeAt(index) == GRASS) break;
    }
    REQUIRE(index < size_t(map.getW()) * map.getH());
    map.setResource(index % map.getW(), index / map.getW(), resource, 0);
    map.setMapDiscovered(index % map.getW(), index / map.getW(), Team::teamNumberToMask(0));
    REQUIRE(map.materialAmountAt(index, MaterialId::Gold) == 2);
    REQUIRE(map.materialAmountAt(index, MaterialId::Food) == 3);
    map.getMaterialGradient(0, MaterialId::Gold, 0);
    map.configureGradientPipeline(2, 3);
    map.advanceGradientPipeline();
    map.stagePeriodicGradientPreparation();
    REQUIRE(map.hasPendingGradientPreparation());
    REQUIRE(map.gradientPipelineStatus().pending == 1);
    definition["yields"]["gold"]["capacity"] = 1;
    definition["yields"]["gold"]["initial"] = 1;
    install();
    CHECK_FALSE(map.hasPendingGradientPreparation());
    CHECK(map.materialAmountAt(index, MaterialId::Gold) == 1);
    CHECK(map.materialAmountAt(index, MaterialId::Food) == 3);
    // Finish respects publication deadlines; advancing safely publishes the
    // prepared goal after the immutable definition snapshot was replaced.
    for (int tick = 0; tick < 3; ++tick) map.advanceGradientPipeline();
    CHECK(map.getMaterialGradient(0, MaterialId::Gold, 0)[index] == GRADIENT_AT_GOAL);
}
TEST_CASE("pending gradient work survives a save and continues at the same deadlines") {
    glob2test::HeadlessGlobals globals;
    GameGUI source;
    GameHeader header;
    header.setNumberOfPlayers(1);
    header.setRandomSeed(123456);
    header.getBasePlayer(0) = BasePlayer(0, "Test", 0, BasePlayer::P_LOCAL);
    auto mapHeader = Engine::loadMapHeader("maps/balanced.map");
    REQUIRE(source.loadFromHeaders(mapHeader, header, true, true));
    auto &map = source.game.map;
    map.getClearAreasGradient(0, 0);
    map.configureGradientPipeline(2, 3);
    map.advanceGradientPipeline();
    map.syncStep(0, false);
    map.stagePeriodicGradientPreparation();
    REQUIRE(map.hasPendingGradientPreparation());
    REQUIRE(map.gradientPipelineStatus().jobs > 0);
    auto *storage = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream writer(storage);
    source.save(&writer, "pending worker continuation");
    CHECK_FALSE(map.hasPendingGradientPreparation());
    const std::string checkpoint(storage->getBuffer(), storage->getPosition());
    const auto simulationState = [](Game &game) {
        std::vector<Uint32> state, buildings, units;
        game.checkSum(&state, &buildings, &units, true);
        // Saving upgrades the map format header; compare all simulation state.
        state.erase(state.begin());
        state.insert(state.end(), buildings.begin(), buildings.end());
        state.insert(state.end(), units.begin(), units.end());
        return state;
    };
    std::vector<std::vector<Uint32>> checksums;
    for (unsigned tick=0; tick<24; ++tick) {
        source.game.syncStep(0);
        checksums.push_back(simulationState(source.game));
    }
    GameGUI restored;
    GAGCore::BinaryInputStream reader(new GAGCore::MemoryStreamBackend(checkpoint.data(), checkpoint.size()));
    reader.seekFromStart(0);
    REQUIRE(restored.load(&reader));
    for (const auto &expected : checksums) {
        restored.game.syncStep(0);
        REQUIRE(simulationState(restored.game) == expected);
    }
}
}

#include "ReadOnlyPhase.h"
TEST_SUITE("SimulationReadPhase") {
TEST_CASE("direct completed steps match deferred preparation across worker counts") {
    glob2test::HeadlessGlobals globals;
    GameHeader header;
    header.setNumberOfPlayers(1);
    header.setRandomSeed(123456);
    header.getBasePlayer(0) = BasePlayer(0, "Test", 0, BasePlayer::P_LOCAL);
    auto mapHeader = Engine::loadMapHeader("maps/balanced.map");
    GameGUI direct, deferred;
    REQUIRE(direct.loadFromHeaders(mapHeader, header, true, true));
    REQUIRE(deferred.loadFromHeaders(mapHeader, header, true, true));
    for (Game *game : {&direct.game, &deferred.game}) {
        game->map.getClearAreasGradient(0, 0);
        game->map.getMaterialGradient(0, MaterialId::Wood, 0);
        game->map.configureGradientPipeline(0, 8);
    }
    for (unsigned threads : {1, 4}) {
        deferred.game.map.configureCompute(threads, Map::ComputeAI);
        for (unsigned tick = 0; tick < 64; ++tick) {
            direct.game.syncStep(0);
            CHECK_FALSE(direct.game.map.hasPendingGradientPreparation());
            deferred.game.syncStep(0, Game::PreparationCompletion::Deferred);
            REQUIRE(deferred.game.map.hasPendingGradientPreparation());
            ReadOnlyPhase phase;
            auto prepare = [&](size_t) { deferred.game.map.preparePendingGradient(); };
            auto read = [&](size_t) { deferred.game.map.getMaterialGradient(0, MaterialId::Wood, 0); };
            phase.add(1, prepare); phase.add(1, read);
            phase.run(deferred.game.map.computeExecutor());
            CHECK_FALSE(deferred.game.map.hasPendingGradientPreparation());
            CHECK(direct.game.checkSum(nullptr, nullptr, nullptr, true) == deferred.game.checkSum(nullptr, nullptr, nullptr, true));
        }
    }
    deferred.game.syncStep(0, Game::PreparationCompletion::Deferred);
    REQUIRE(deferred.game.map.hasPendingGradientPreparation());
    deferred.game.map.configureCompute(1, 0);
    CHECK_FALSE(deferred.game.map.hasPendingGradientPreparation());
    deferred.game.syncStep(0, Game::PreparationCompletion::Deferred);
    deferred.game.map.setGradientWorkerCount(2);
    CHECK_FALSE(deferred.game.map.hasPendingGradientPreparation());
    deferred.game.syncStep(0, Game::PreparationCompletion::Deferred);
    const auto waitingTick = deferred.game.stepCounter;
    deferred.game.anyPlayerWaited = true;
    deferred.game.syncStep(0);
    CHECK(deferred.game.stepCounter == waitingTick);
    CHECK_FALSE(deferred.game.map.hasPendingGradientPreparation());
    deferred.game.anyPlayerWaited = false;
    deferred.game.syncStep(0, Game::PreparationCompletion::Deferred);
    deferred.game.map.clear(); // Reserved jobs must not outlive their destination.
    CHECK_FALSE(deferred.game.map.hasPendingGradientPreparation());
}
}
