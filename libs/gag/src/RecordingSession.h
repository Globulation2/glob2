// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "RecordingEncoder.h"
#include "RecordingMetadata.h"
#include <GameplayRecording.h>
#include <functional>

namespace GAGCore::Recording::Detail
{
struct CaptureFrame
{
	std::int64_t time = 0;
	int width = 0, height = 0;
	std::vector<unsigned char> rgba;
	Context context;
	// Byte layouts: RGBA, BGRA, ARGB, ABGR, RGB, BGR. Normalize on the worker.
	unsigned pixelLayout = 0;
	bool bottomUp = false;
};
struct SessionStorage
{
	OpenRecordingFile open = openNativeRecordingFile;
	std::function<void(const std::string &)> reserve;
	std::function<void(const std::string &, const std::string &)> publish;
	std::function<void(const std::string &)> cleanup;
	std::function<std::unique_ptr<VideoEncoder>(const VideoConfiguration &)> video = [](const VideoConfiguration &configuration) { return createVideoEncoder(configuration); };
};
SessionStorage nativeSessionStorage();
void recoverRecording(const std::string &path, const SessionStorage &storage);
// The same bounded pump runs on a native thread or inside a browser worker.
// All methods belong to that worker; it never owns simulation or render state.
class Session
{
  public:
	Session(std::string path, Options options, SessionStorage storage,
		std::function<void(const Status &)> report,
		std::unique_ptr<VideoEncoder> externalEncoder = {});
	~Session();
	void frame(CaptureFrame frame);
	std::size_t queuedFrames() const;
	void audio(const std::int16_t *samples, std::size_t count, std::int64_t time);
	void event(std::int64_t time, const std::string &kind, const std::string &value);
	void drops(std::uint64_t frames, std::uint64_t audio);
	void stop(std::int64_t time);
	// Returns true once finalization is complete. Call at least once per frame slot.
	bool step(std::int64_t time);
	void fail(const std::string &error);
  private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
} // namespace GAGCore::Recording::Detail
