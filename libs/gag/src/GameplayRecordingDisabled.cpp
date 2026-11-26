// SPDX-License-Identifier: GPL-3.0-or-later
// The gameplay recording API for editions built without recording
// (buildsys/edition.py, GLOB2_HAS_RECORDING 0). GameplayRecording.cpp, the
// recordings browser and the platform encoders are left out; the frame loop,
// mixer and screens keep calling this interface, which records nothing and
// reports recording unsupported, so no control offers it.
#include <BuildFeatures.h>
#if !GLOB2_HAS_RECORDING
#include <GameplayRecording.h>

namespace GAGCore::Recording
{
struct Recorder::Impl
{
};
Recorder::Recorder() = default;
Recorder::~Recorder() = default;
bool Recorder::start(const std::string &) { return false; }
void Recorder::stop() {}
void Recorder::abort(const std::string &) {}
void Recorder::shutdown() {}
Status Recorder::status() const { return {}; }
Completion Recorder::completion() const { return {}; }
bool Recorder::active() const { return false; }
bool Recorder::wantsFrame() const { return false; }
void Recorder::frame(const SDL_Surface &, bool) {}
std::optional<CaptureTicket> Recorder::reserveCapture() { return std::nullopt; }
void Recorder::frame(const SDL_Surface &, const CaptureTicket &, bool) {}
void Recorder::dropCapture(const CaptureTicket &) {}
void Recorder::audio(const std::int16_t *, std::size_t, std::int64_t) {}
void Recorder::audio(const std::int16_t *, std::size_t) {}
void Recorder::screen(const std::string &) {}
void Recorder::dialog(const std::string &) {}
void Recorder::beginMatch(const std::string &, const std::string &, int, std::uint32_t) {}
void Recorder::matchFrame(std::uint32_t, bool, int) {}
void Recorder::event(const std::string &, const std::string &) {}

std::int64_t timestamp() { return 0; }
Recorder &recorder()
{
	static Recorder instance;
	return instance;
}
bool supported() { return false; }
bool available() { return false; }
std::string recordingDirectory() { return {}; }
std::string recordingFileName(const std::tm &, unsigned) { return {}; }
std::string uniqueRecordingPath(const std::string &, const std::tm &, const std::function<bool(const std::string &)> &)
{
	return {};
}
#ifndef __EMSCRIPTEN__
std::string uniqueRecordingPath(const std::string &, const std::tm &) { return {}; }
std::vector<RecordingFileEntry> scanRecordings(const std::string &) { return {}; }
#endif
bool toggle() { return false; }
std::string controlLabel() { return {}; }
void requestFiles() {}
bool takeFilesRequest() { return false; }
void refreshFiles() {}
RecordingFilesStatus filesStatus() { return {}; }
void recoverFile(const std::string &) {}
void deleteFile(const std::string &) {}
} // namespace GAGCore::Recording
#endif
