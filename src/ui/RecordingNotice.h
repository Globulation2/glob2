// SPDX-License-Identifier: GPL-3.0-or-later
// Where a recording goes, in words a player can act on: the full path on desktop,
// the download on the web. Shared by the recording controls, the Recordings
// screen, Settings > Recording and the in-game message after a recording ends.
#pragma once
#include <GameplayRecording.h>
#include <cstdint>
#include <optional>
#include <string>

namespace Glob2UI
{
// Browser recordings live in site storage and reach the player as downloads.
constexpr bool recordingsDownload()
{
#ifdef __EMSCRIPTEN__
	return true;
#else
	return false;
#endif
}
// The full path on desktop; only the file name in the browser, whose virtual
// file system path means nothing to the player.
std::string recordingDisplayPath(const std::string &path);
// One line for the recording controls: the destination while recording or
// saving, then where the last recording was saved. Empty when there is none.
std::string recordingLocationLine(const GAGCore::Recording::Status &status,
								  const GAGCore::Recording::Completion &last);
// The in-game message once a recording has been saved or has failed.
std::string recordingFinishedMessage(const GAGCore::Recording::Completion &done);
// "2026-11-18 14:35 · 0:23 · 2.9 MB", leaving out what is unknown.
std::string recordingFileDetails(const GAGCore::Recording::RecordingFileEntry &file);

// Reports each recording ending once. The first call only notes the current
// ending, so an ending from before the observer existed is never announced.
std::optional<GAGCore::Recording::Completion> takeRecordingEnding(std::optional<std::uint64_t> &seen,
																  const GAGCore::Recording::Completion &current);

// Browser builds download each recording as soon as it is saved; the copy in
// site storage stays listed under Recordings. Call once per application frame.
void deliverFinishedRecordings();
} // namespace Glob2UI
