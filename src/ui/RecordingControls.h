// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FrontendUI.h"
#include <GameplayRecording.h>

namespace Glob2UI
{
inline Element recordingControls()
{
	if (!GAGCore::Recording::supported())
		return empty();
	const auto status = GAGCore::Recording::recorder().status();
	ButtonOptions options;
	options.enabled = status.state != GAGCore::Recording::State::Finalizing;
	std::vector<Element> parts{button(
		"recording/toggle", tr(GAGCore::Recording::controlLabel()),
		[] { GAGCore::Recording::toggle(); }, options)};
	if (!status.error.empty())
		parts.push_back(paragraph(status.error, {FontRole::Body, false}));
	return column(std::move(parts));
}
} // namespace Glob2UI
