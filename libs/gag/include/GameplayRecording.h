// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "RecordingDefaults.h"
#include <SDL3/SDL.h>
#include <AudioFormat.h>
#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GAGCore::Recording
{
namespace Detail { struct Context; }
struct CaptureTicket
{
    std::uint64_t identity = 0, generation = 0;
    std::int64_t timestamp = 0;
    std::shared_ptr<const Detail::Context> context;
};
inline constexpr int AudioSampleRate = GAGCore::AudioSampleRate;
inline constexpr int AudioChannels = 2;
enum class EncoderPreference { Auto, Software };
struct Options
{
	int fps = DefaultFps, crf = DefaultCrf;
	EncoderPreference encoder = EncoderPreference::Auto;
	std::uint32_t chapterTicks = DefaultChapterTicks;
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
	std::string encoder, fallbackReason;
	unsigned segment = 0;
	int width = 0, height = 0, sourceWidth = 0, sourceHeight = 0;
	std::vector<std::string> outputs;
};
// The end of one recording, kept until the next one ends. serial counts endings,
// so a presenter can announce each saved or failed recording exactly once.
struct Completion
{
	std::uint64_t serial = 0;
	bool failed = false;
	std::vector<std::string> outputs;
	std::string error;
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
	Completion completion() const;
	bool active() const;
	bool wantsFrame() const;
	void frame(const SDL_Surface &pixels, bool bottomUp = false);
    // Reserve cadence and semantic context before asynchronous GPU readback.
    std::optional<CaptureTicket> reserveCapture();
    void frame(const SDL_Surface &pixels, const CaptureTicket &, bool bottomUp = false);
    void dropCapture(const CaptureTicket &);
	// Interleaved signed stereo samples at 48000 Hz; count includes both channels.
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
    void enqueueFrame(const SDL_Surface &, std::int64_t, const Detail::Context &, bool);
	std::unique_ptr<Impl> impl;
};
// Shared monotonic clock used for frame timestamps and the first sample of a refill.
std::int64_t timestamp();
Recorder &recorder();
bool supported();
// Recording is built into supported clients; runtime storage/encoder failures
// are reported by the session.
bool available();
// Folder for recordings started from the UI: videoshots/ in the user profile.
std::string recordingDirectory();
// A readable name that sorts chronologically: glob2-2026-11-18_14-35-02.mp4, with
// -2, -3, ... appended for a second recording in the same second.
std::string recordingFileName(const std::tm &local, unsigned attempt = 1);
// The first recordingFileName in directory for that time that taken() rejects.
std::string uniqueRecordingPath(const std::string &directory, const std::tm &local,
								const std::function<bool(const std::string &path)> &taken);
#ifndef __EMSCRIPTEN__
// The first name not used by a video or an interrupted recording in directory.
std::string uniqueRecordingPath(const std::string &directory, const std::tm &local);
#endif
// UI helper: unique profile output; returns false while finalization is pending.
bool toggle();
std::string controlLabel();
struct RecordingFileEntry
{
	std::string path;
	bool recoverable = false;
	std::uint64_t bytes = 0;			// video size, 0 when unknown
	std::int64_t modified = 0;			// seconds since the Unix epoch, 0 when unknown
	std::int64_t durationUs = 0;		// from the manifest, 0 when unknown
};
struct RecordingFilesStatus
{
	bool busy = false;
	std::string error;
	std::vector<RecordingFileEntry> files;
};
#ifndef __EMSCRIPTEN__
// Completed and interrupted recordings in directory, newest first.
std::vector<RecordingFileEntry> scanRecordings(const std::string &directory);
#endif
void requestFiles();
bool takeFilesRequest();
void refreshFiles();
RecordingFilesStatus filesStatus();
void recoverFile(const std::string &path);
void deleteFile(const std::string &path);
} // namespace GAGCore::Recording
