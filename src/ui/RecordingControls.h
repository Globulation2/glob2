// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FrontendUI.h"
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
	parts.push_back(button("recording/files","Recordings",[] { GAGCore::Recording::requestFiles(); }));
	if (!status.error.empty())
		parts.push_back(paragraph(status.error, {FontRole::Support, true}));
	return column(std::move(parts));
}
} // namespace Glob2UI
