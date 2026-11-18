#include "ui/DiagnosticText.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FrontendUI.h"
#include "ui/RecordingNotice.h"
#include <GameplayRecording.h>

namespace Glob2UI
{
// Supported clients offer recording, including controls during finalization.
inline bool recordingOffered()
{
	const auto state = GAGCore::Recording::recorder().status().state;
	return GAGCore::Recording::available() || GAGCore::Recording::recorder().active() ||
		   state == GAGCore::Recording::State::Finalizing;
}

// Start/stop button, completed files and the last error.
inline Element recordingControls(bool always = false)
{
	if (!GAGCore::Recording::supported() || (!always && !recordingOffered()))
		return empty();
	const auto status = GAGCore::Recording::recorder().status();
	ButtonOptions options;
	options.enabled = status.state != GAGCore::Recording::State::Finalizing &&
					  (GAGCore::Recording::available() || GAGCore::Recording::recorder().active());
	std::vector<Element> parts{button(
		"recording/toggle", tr(GAGCore::Recording::controlLabel()),
		[] { GAGCore::Recording::toggle(); }, options)};
	// Where the current or last recording goes, so the player never has to guess.
	const auto location = recordingLocationLine(status, GAGCore::Recording::recorder().completion());
	if (!location.empty() && status.error.empty())
		parts.push_back(paragraph(location, {FontRole::Support, true}));
	parts.push_back(button("recording/files",tr("[recordings]"),[] { GAGCore::Recording::requestFiles(); }));
	if (!status.error.empty())
	{
		static DiagnosticText recordingError;
		parts.push_back(paragraph(recordingError(status.error), {FontRole::Support, true}));
	}
	return column(std::move(parts));
}
} // namespace Glob2UI
