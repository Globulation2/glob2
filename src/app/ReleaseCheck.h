// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The release check (docs/releases/release-check.md): a scripted run of the
// common paths, through the same screens and calls a player's taps reach, run
// by the release workflows on every platform before a build is published. It
// starts when GLOB2_RELEASE_CHECK is set (1 or a comma-separated list of
// steps), logs "Glob2 release check: <step> ok" as each step passes, and ends
// the application with "Glob2 release check passed" (exit 0) or
// "Glob2 release check failed: <why>" (exit 1). A crash ends it too, and the
// crash capture (src/app/CrashCapture.h) keeps that crash like a player's.

#include <SDL3/SDL_stdinc.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GAGGUI
{
class ScreenStack;
class Screen;
}
class SinglePlayerFlow;
class EndGameScreen;
class GameSessionScreen;

namespace ReleaseCheck
{
// The steps, in order.
inline const std::vector<std::string> &allSteps()
{
	static const std::vector<std::string> steps{"menu", "custom", "load", "lesson", "tutorial", "victory"};
	return steps;
}
// The steps GLOB2_RELEASE_CHECK asks for ("1" or "all" for every step), or
// nothing when it is unset. Unknown names are an error the runner reports.
std::optional<std::vector<std::string>> requested();
std::vector<std::string> parseSteps(const std::string &value);
// The finished check's exit status, which main() returns once the application
// has shut down normally (desktop); unset while no check has finished.
void setResult(int status);
std::optional<int> result();

// Ticks a match runs (from its first drawn tick) before the check quits it,
// and how long a step may take.
inline constexpr std::uint32_t MATCH_TICKS = 100;
inline constexpr std::uint32_t STEP_TIMEOUT_MS = 10 * 60 * 1000;
// The saved game a few hundred ticks before a win (tools/release/make_release_check_save.py),
// shipped with every package's data; the load step plays it too, and quits first.
inline constexpr const char *VICTORY_SAVE = "data/release-check/victory.game.gz";
inline constexpr const char *LESSON_MAP = "campaigns/walking-training.map.gz";
inline constexpr const char *LOAD_SAVE = VICTORY_SAVE;
inline constexpr const char *REPLAY_FILE = "replays/release-check.replay";

class Runner
{
  public:
	Runner(GAGGUI::ScreenStack &screens, SinglePlayerFlow &singlePlayer, std::vector<std::string> steps);
	~Runner();
	// Advances the check after the screens' frame. Returns the exit code once
	// the check is over.
	std::optional<int> frame(std::uint32_t tick);

	// The results views the victory step opens (exposed for tests).
	static int openEveryResultsView(EndGameScreen &results);

  private:
	enum class Phase
	{
		Start,
		Waiting,
	};
	void begin(const std::string &step, std::uint32_t tick);
	std::optional<int> fail(const std::string &why);
	// The running match of the top session, if a GameSessionScreen is under the screens on top.
	GameSessionScreen *session() const;
	EndGameScreen *results() const;
	void launch(std::function<void()> start);

	GAGGUI::ScreenStack &screens;
	SinglePlayerFlow &singlePlayer;
	std::vector<std::string> steps;
	std::size_t current = 0;
	Phase phase = Phase::Start;
	std::uint32_t started = 0;
	bool quitSent = false, resultsSeen = false, returned = false, ended = false;
	std::optional<std::uint32_t> firstTick;
};
} // namespace ReleaseCheck
