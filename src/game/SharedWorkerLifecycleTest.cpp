// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Engine.h"
#include <BackgroundFileWriter.h>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <ThreadSupport.h>
#include <filesystem>
#include <fstream>
#include <thread>

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
    map.syncStep(0);
    REQUIRE(map.gradientPipelineStatus().jobs > 0);
    auto *storage = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream writer(storage);
    source.save(&writer, "pending worker continuation");
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
