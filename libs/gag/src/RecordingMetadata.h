// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <sstream>

namespace GAGCore::Recording::Detail
{
std::string json(const std::string &value);
std::string ffescape(const std::string &value);
struct Context
{
	std::string screen = "startup", dialog, mode, map;
	std::uint64_t match = 0;
	std::uint32_t tick = 0;
	int team = -1, speed = 0;
	bool paused = false;
	std::string phase() const
	{
		if (screen == "game_session")
			return dialog.empty() ? "gameplay" : "dialog";
		if (screen == "end_game")
			return dialog.empty() ? "results" : "dialog";
		if (screen == "game_load" || screen == "editor_load" || screen == "editor_generate")
			return "loading";
		return dialog.empty() ? "menu" : "dialog";
	}
	std::string key(std::uint32_t period) const
	{
		return phase() + ":" + screen + ":" + dialog + ":" + std::to_string(match) + ":" +
			   (phase() == "gameplay" ? std::to_string(tick / period) : "");
	}
	std::string fields(std::uint32_t period) const
	{
		std::ostringstream out;
		out << "\"phase\":" << json(phase()) << ",\"screen\":" << json(screen)
			<< ",\"dialog\":" << json(dialog) << ",\"match\":" << match
			<< ",\"mode\":" << json(mode) << ",\"map\":" << json(map) << ",\"team\":" << team;
		if (match)
			out << ",\"tick\":" << tick;
		if (phase() == "gameplay")
			out << ",\"era_start\":" << std::uint64_t(tick / period) * period
				<< ",\"era_end\":" << (std::uint64_t(tick / period) + 1) * period;
		return out.str();
	}
	std::string title(std::uint32_t period) const
	{
		if (phase() == "gameplay")
			return "Match " + std::to_string(match) + ": ticks " +
				   std::to_string(std::uint64_t(tick / period) * period) + "–" +
				   std::to_string((std::uint64_t(tick / period) + 1) * period);
		return dialog.empty() ? screen : screen + " / " + dialog;
	}
};

// Stage metadata first; the atomic, no-replace video link is the completion point.
// Where hard links are refused (Android app storage), metadata is copied and the
// video renamed, both without replacing. A failure undoes only what was published
// here, leaving foreign files untouched. Tests pass tryHardLinks false to exercise
// the fallback on filesystems that allow links.
void publishCompletedRecording(const std::filesystem::path &work,
							   const std::filesystem::path &video, bool tryHardLinks = true);
} // namespace GAGCore::Recording::Detail
