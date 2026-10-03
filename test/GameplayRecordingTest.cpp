// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/Glob2Test.h"
#include <GameplayRecording.h>
#include <RecordingProcess.h>
#include <SDL3/SDL.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace GAGCore::Recording;
namespace {
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
std::filesystem::path output(const char* label) {
    return glob2test::artifactDir() / (std::string(label) + "-" + std::to_string(SDL_GetPerformanceCounter()) + ".mp4");
}
void draw(Recorder& recorder, SDL_Surface* pixels, int milliseconds) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    std::int16_t samples[882];
    std::uint64_t index = 0;
    while(std::chrono::steady_clock::now() < deadline) {
        if(recorder.wantsFrame()) recorder.frame(*pixels);
        for(int i = 0; i < 441; ++i) {
            auto sample = std::int16_t(std::sin(2 * 3.14159265358979323846 * 440 * index++ / 44100) * 12000);
            samples[2*i] = samples[2*i+1] = sample;
        }
        recorder.audio(samples, 882);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
}
TEST_SUITE("GameplayRecording") {
TEST_CASE("recording rejects invalid options and never overwrites output") {
    Recorder recorder;
    recorder.options.fps = 0;
    CHECK_FALSE(recorder.start(output("invalid").string()));
    CHECK(recorder.status().state == State::Failed);
    recorder.options.fps = 60;
    auto path = output("existing");
    std::ofstream(path) << "preserve";
    CHECK_FALSE(recorder.start(path.string()));
    CHECK(read(path) == "preserve");
}
TEST_CASE("missing encoder fails asynchronously without holding the caller") {
    if(!supported()) return;
    Recorder recorder;
    recorder.options.ffmpeg = "glob2-nonexistent-ffmpeg";
    auto path = output("missing-encoder");
    REQUIRE(recorder.start(path.string()));
    recorder.shutdown();
    CHECK(recorder.status().state == State::Failed);
    CHECK_FALSE(std::filesystem::exists(path));
    CHECK(std::filesystem::exists(path.string() + ".recording/manifest.json"));
}
TEST_CASE("recording encodes audio, odd resized frames, and semantic chapters [recording]") {
    // Explicit opt-in: normal unit tests must not require an installed FFmpeg.
    const char* encoder = SDL_getenv("GLOB2_TEST_FFMPEG");
    if(!encoder || !supported()) { MESSAGE("Set GLOB2_TEST_FFMPEG to exercise real video encoding"); return; }
    auto path = output("recording-sections");
    Recorder recorder;
    recorder.options.ffmpeg = encoder; recorder.options.fps = 60;
    auto pixels = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(SDL_CreateSurface(321, 181, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    REQUIRE(pixels);
    SDL_FillSurfaceRect(pixels.get(), nullptr, SDL_MapSurfaceRGBA(pixels.get(), 200, 20, 10, 255));
    recorder.screen("main_menu");
    REQUIRE(recorder.start(path.string()));
    const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (recorder.status().state == State::Starting && std::chrono::steady_clock::now() < readyDeadline) draw(recorder, pixels.get(), 50);
    REQUIRE(recorder.status().state == State::Recording);
    draw(recorder, pixels.get(), 350);
    recorder.screen("multiplayer_game");
    SDL_FillSurfaceRect(pixels.get(), nullptr, SDL_MapSurfaceRGBA(pixels.get(), 20, 200, 10, 255));
    draw(recorder, pixels.get(), 200);
    recorder.beginMatch("multiplayer", "chapter test", 2, 15000);
    recorder.matchFrame(15000, false, 40);
    draw(recorder, pixels.get(), 200);
    recorder.matchFrame(20000, false, 40);
    draw(recorder, pixels.get(), 200);
    recorder.matchFrame(20005, true, 40);
    recorder.dialog("in_game_main");
    draw(recorder, pixels.get(), 200);
    pixels.reset(SDL_CreateSurface(400, 240, SDL_PIXELFORMAT_RGBA32));
    REQUIRE(pixels);
    SDL_FillSurfaceRect(pixels.get(), nullptr, SDL_MapSurfaceRGBA(pixels.get(), 10, 20, 200, 255));
    recorder.matchFrame(20005, false, 40);
    draw(recorder, pixels.get(), 200);
    recorder.screen("end_game");
    draw(recorder, pixels.get(), 200);
    recorder.stop(); CHECK_FALSE(recorder.active()); CHECK_FALSE(recorder.start(output("busy").string()));
    recorder.shutdown();
    INFO(recorder.status().error);
    REQUIRE(recorder.status().state == State::Complete);
    CHECK(std::filesystem::file_size(path) > 1000);
    CHECK_FALSE(std::filesystem::exists(path.string() + ".recording"));
    const auto manifest = read(path.string() + ".json");
    CHECK(manifest.find("\"width\":322") != std::string::npos);
    CHECK(manifest.find("\"height\":182") != std::string::npos);
    CHECK(manifest.find("\"era_start\":10000") != std::string::npos);
    CHECK(manifest.find("\"era_start\":20000") != std::string::npos);
    CHECK(manifest.find("\"phase\":\"results\"") != std::string::npos);
    CHECK(manifest.find("\"screen\":\"multiplayer_game\"") != std::string::npos);
    CHECK(manifest.find("\"mode\":\"multiplayer\"") != std::string::npos);
    CHECK(manifest.find("\"team\":2") != std::string::npos);
    CHECK(read(path.string() + ".events.jsonl").find("\"kind\":\"resize\"") != std::string::npos);
}
TEST_CASE("stopping a recording before its first frame completes without hanging") {
    if(!supported()) return;
    Recorder recorder;
    recorder.options.ffmpeg = "glob2-nonexistent-ffmpeg";
    REQUIRE(recorder.start(output("no-frame").string()));
    recorder.stop(); recorder.shutdown();
    CHECK(recorder.status().state == State::Failed);
}

}
