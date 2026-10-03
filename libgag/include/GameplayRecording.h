// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <string>

namespace GAGCore::Recording {
struct Options {
    std::string ffmpeg = "ffmpeg";
    int fps = 60, width = 0, height = 0, crf = 18;
    std::uint32_t chapterTicks = 10000;
};
enum class State { Idle, Starting, Recording, Finalizing, Complete, Failed };
struct Status {
    State state = State::Idle;
    std::string path, error;
    std::uint64_t droppedFrames = 0, droppedAudioSamples = 0;
};
// One local client perspective. No simulation, network, or save-state ownership.
// Context/frame/control calls belong to the main thread; audio() is nonblocking.
class Recorder {
public:
    Recorder();
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    Options options;
    bool start(const std::string& path);
    void stop();
    void shutdown();
    Status status() const;
    bool active() const;
    bool wantsFrame() const;
    void frame(const SDL_Surface& pixels);
    void audio(const std::int16_t* samples, std::size_t count);
    void screen(const std::string& id);
    void dialog(const std::string& id);
    void beginMatch(const std::string& mode, const std::string& map, int team, std::uint32_t tick);
    void matchFrame(std::uint32_t tick, bool paused, int speed);
    void event(const std::string& kind, const std::string& value);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
Recorder& recorder();
bool supported();
// UI helper: unique profile output; returns false while finalization is pending.
bool toggle();
std::string controlLabel();
} // namespace GAGCore::Recording
