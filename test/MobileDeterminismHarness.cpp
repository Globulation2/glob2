// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
#include "Engine.h"
#include "GlobalContainer.h"
#include "Utilities.h"
#include "AI.h"
#include "Map.h"
#include <BinaryStream.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#ifdef __EMSCRIPTEN__
#include <emscripten/heap.h>
#else
#include <sys/resource.h>
#endif
GlobalContainer* globalContainer = nullptr;
class MobileDeterminismHarness {
public:
    static int run(const std::string& filename, unsigned steps) {
        GlobalContainer globals("glob2-determinism");
        globalContainer = &globals;
        globals.runNoX = true;
        globals.settings.mute = true;
        globals.load();
        GAGCore::BinaryInputStream input(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(filename));
        MapHeader map;
        if (!map.load(&input)) throw std::runtime_error("Invalid map fixture");
        GameHeader players;
        players.setNumberOfPlayers(map.getNumberOfTeams());
        players.setRandomSeed(42);
        for (int i = 0; i < map.getNumberOfTeams(); ++i) {
            players.getBasePlayer(i) = BasePlayer(i, "Fixture AI " + std::to_string(i), i, BasePlayer::P_AI);
            players.getBasePlayer(i).makeItAI(AI::CASTOR);
        }
        Engine engine;
        if (engine.initCustom(map, players, 0) != Engine::EE_NO_ERROR) throw std::runtime_error("Fixture initialization failed");
        auto& game = engine.gui.game;
        std::printf("GLOB2_FIXTURE width=%d height=%d teams=%d seed=42 ai=castor steps=%u\n", game.map.getW(), game.map.getH(), map.getNumberOfTeams(), steps);
        const auto startStep = game.stepCounter;
        auto checkpoint = [&] {
            std::vector<Uint32> components;
            const Uint32 checksum = game.checkSum(&components, nullptr, nullptr, true);
            Uint32 rng = 2166136261u;
            for (unsigned char c : getSyncRandState()) rng = (rng ^ c) * 16777619u;
            std::printf("GLOB2_DETERMINISM step=%u checksum=%08x rng=%08x components=", game.stepCounter-startStep, checksum, rng);
            for (auto value : components) std::printf("%08x,", value);
            std::printf("\n"); std::fflush(stdout);
        };
        checkpoint();
        engine.beginSession(0);
        std::vector<double> durations;
        durations.reserve(steps);
        const auto started = std::chrono::steady_clock::now();
        for (unsigned frame = 0; game.stepCounter-startStep < steps; ++frame) {
            if (frame > steps*4+100) throw std::runtime_error("Simulation stopped advancing");
            const auto before = game.stepCounter;
            const auto begin = std::chrono::steady_clock::now();
            if (!engine.stepSession(Uint64(frame)*40, {})) throw std::runtime_error("Fixture session ended early");
            if (game.stepCounter != before) {
                durations.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count());
                if ((game.stepCounter-startStep)%1000 == 0 || game.stepCounter-startStep == steps) checkpoint();
            }
        }
        const double elapsed = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        std::sort(durations.begin(), durations.end());
        auto percentile = [&](double quantile) { return durations.empty() ? 0.0 : durations[std::min(durations.size()-1, size_t(quantile*durations.size()))]; };
#ifdef __EMSCRIPTEN__
        const auto memory = emscripten_get_heap_size();
        const char* memoryKind = "wasm_heap_bytes";
#else
        struct rusage usage{}; getrusage(RUSAGE_SELF, &usage);
        const auto memory = usage.ru_maxrss
#ifndef __APPLE__
            *1024
#endif
            ;
        const char* memoryKind = "peak_rss_bytes";
#endif
        std::printf("GLOB2_PERF steps=%zu wall_ms=%.3f median_us=%.3f p95_us=%.3f p99_us=%.3f max_us=%.3f %s=%llu\n", durations.size(), elapsed, percentile(.5), percentile(.95), percentile(.99), percentile(1), memoryKind, static_cast<unsigned long long>(memory));
        engine.gui.isRunning = false;
        engine.stepSession(Uint64(steps)*40, {});
        engine.finishSessionForHost();
        return 0;
    }
};
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: determinism MAP STEPS");
        const auto steps = std::stoul(argv[2]);
        if (!steps || steps > 1000000) throw std::runtime_error("Steps must be between 1 and 1000000");
        return MobileDeterminismHarness::run(argv[1], steps);
    } catch (const std::exception& error) { std::fprintf(stderr, "GLOB2_DETERMINISM_FAILED %s\n", error.what()); return 1; }
}
