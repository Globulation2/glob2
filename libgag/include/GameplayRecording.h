// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <string>

namespace GAGCore::Recording
{
inline constexpr int AudioSampleRate = 44100;
inline constexpr int AudioChannels = 2;
struct Options
{
	std::string ffmpeg = "ffmpeg";
	int fps = 60, width = 0, height = 0, crf = 18;
	std::uint32_t chapterTicks = 10000;
};
enum class State
{
	Idle,
	Starting,
	Recording,
	Finalizing,
	Complete,
	Failed
};
struct Status
{
	State state = State::Idle;
	std::string path, error;
	std::uint64_t droppedFrames = 0, droppedAudioSamples = 0;
};
// One local client perspective. No simulation, network, or save-state ownership.
// Options, context, frame submission, and controls belong to the main thread.
// status()/active() may be read from any thread. audio() belongs to the SDL
// callback: it neither allocates nor waits for the encoder. shutdown() joins workers.
class Recorder
{
  public:
	Recorder();
	~Recorder();
	Recorder(const Recorder &) = delete;
	Recorder &operator=(const Recorder &) = delete;
	Options options;
	bool start(const std::string &path);
	void stop();
	// A fatal producer error retains intermediates and cannot finalize successfully.
	void abort(const std::string &error);
	void shutdown();
	Status status() const;
	bool active() const;
	bool wantsFrame() const;
	void frame(const SDL_Surface &pixels);
	// Interleaved signed stereo samples at 44100 Hz; count includes both channels.
	// Timestamp the first chunk once, then advance by frames for split SDL refills.
	void audio(const std::int16_t *samples, std::size_t count, std::int64_t startUs);
	void audio(const std::int16_t *samples, std::size_t count);
	void screen(const std::string &id);
	void dialog(const std::string &id);
	void beginMatch(const std::string &mode, const std::string &map, int team, std::uint32_t tick);
	void matchFrame(std::uint32_t tick, bool paused, int speed);
	void event(const std::string &kind, const std::string &value);

  private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
// Shared monotonic clock used for frame timestamps and the first sample of a refill.
std::int64_t timestamp();
Recorder &recorder();
bool supported();
// UI helper: unique profile output; returns false while finalization is pending.
bool toggle();
std::string controlLabel();
} // namespace GAGCore::Recording
