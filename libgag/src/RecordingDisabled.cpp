// SPDX-License-Identifier: GPL-3.0-or-later
// Codec-free implementation for distribution packages that omit recording.
#include <GameplayRecording.h>

namespace GAGCore::Recording
{
struct Recorder::Impl {};
Recorder::Recorder() = default;
Recorder::~Recorder() = default;
bool Recorder::start(const std::string &) { return false; }
void Recorder::stop() {}
void Recorder::abort(const std::string &) {}
void Recorder::shutdown() {}
Status Recorder::status() const { return {}; }
bool Recorder::active() const { return false; }
bool Recorder::wantsFrame() const { return false; }
void Recorder::frame(const SDL_Surface &, bool) {}
void Recorder::frame(const SDL_Surface &, const CaptureTicket &, bool) {}
std::optional<CaptureTicket> Recorder::reserveCapture() { return std::nullopt; }
void Recorder::dropCapture(const CaptureTicket &) {}
void Recorder::audio(const std::int16_t *, std::size_t, std::int64_t) {}
void Recorder::audio(const std::int16_t *, std::size_t) {}
void Recorder::screen(const std::string &) {}
void Recorder::dialog(const std::string &) {}
void Recorder::beginMatch(const std::string &, const std::string &, int, std::uint32_t) {}
void Recorder::matchFrame(std::uint32_t, bool, int) {}
void Recorder::event(const std::string &, const std::string &) {}
std::int64_t timestamp() { return 0; }
Recorder &recorder() { static Recorder instance; return instance; }
bool supported() { return false; }
bool available() { return false; }
bool toggle() { return false; }
std::string controlLabel() { return "[start recording]"; }
void requestFiles() {}
bool takeFilesRequest() { return false; }
void refreshFiles() {}
RecordingFilesStatus filesStatus() { return {}; }
void recoverFile(const std::string &) {}
void deleteFile(const std::string &) {}
} // namespace GAGCore::Recording
