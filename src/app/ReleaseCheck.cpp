// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReleaseCheck.h"
#include "CustomGameScreen.h"
#include "Engine.h"
#include "EndGameScreen.h"
#include "GameSessionScreen.h"
#include "GlobalContainer.h"
#include "MainMenuScreen.h"
#include "ReplayWriter.h"
#include "SinglePlayerFlow.h"
#include "tutorial/TutorialModel.h"
#include <FileManager.h>
#include <ScreenStack.h>
#include <Toolkit.h>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace ReleaseCheck
{
std::vector<std::string> parseSteps(const std::string &value)
{
	if (value.empty() || value == "1" || value == "all")
		return allSteps();
	std::vector<std::string> steps;
	std::stringstream list(value);
	for (std::string step; std::getline(list, step, ',');)
		if (!step.empty())
			steps.push_back(step);
	return steps;
}

std::optional<std::vector<std::string>> requested()
{
	const char *value = std::getenv("GLOB2_RELEASE_CHECK");
	if (!value || !*value || std::string(value) == "0")
		return std::nullopt;
	return parseSteps(value);
}

namespace
{
std::optional<int> &finishedStatus()
{
	static std::optional<int> status;
	return status;
}
} // namespace

void setResult(int status)
{
	finishedStatus() = status;
}

std::optional<int> result()
{
	return finishedStatus();
}

namespace
{
void say(const std::string &line)
{
	SDL_Log("Glob2 release check: %s", line.c_str());
}

const char *outcomeName(EndGameScreen::Outcome outcome)
{
	switch (outcome)
	{
	case EndGameScreen::Outcome::Victory:
		return "victory";
	case EndGameScreen::Outcome::Defeat:
		return "defeat";
	case EndGameScreen::Outcome::Ended:
		return "ended";
	case EndGameScreen::Outcome::Left:
		return "left";
	}
	return "ended";
}
} // namespace

Runner::Runner(GAGGUI::ScreenStack &screens, SinglePlayerFlow &singlePlayer, std::vector<std::string> steps)
	: screens(screens), singlePlayer(singlePlayer), steps(std::move(steps))
{
	// A fresh profile asks its first-run questions; answer them for this run
	// only (the settings are not saved), and play as fast as the game allows.
	auto &settings = globalContainer->settings;
	settings.audioPreferenceChosen = true;
	settings.thumbSideChosen = true;
	settings.introductionOffered = true;
	settings.autosaveGames = false;
	settings.gameSpeed = Settings::GAME_SPEED_MAXIMUM;
	std::string names;
	for (const auto &step : this->steps)
		names += (names.empty() ? "" : ",") + step;
	say("starting " + names);
}

Runner::~Runner() = default;

std::optional<int> Runner::fail(const std::string &why)
{
	SDL_Log("Glob2 release check failed: %s (step %s)", why.c_str(),
			current < steps.size() ? steps[current].c_str() : "end");
	return 1;
}

GameSessionScreen *Runner::session() const
{
	return dynamic_cast<GameSessionScreen *>(screens.top());
}

EndGameScreen *Runner::results() const
{
	return dynamic_cast<EndGameScreen *>(screens.top());
}

void Runner::launch(std::function<void()> start)
{
	quitSent = resultsSeen = returned = ended = false;
	firstTick.reset();
	start();
}

int Runner::openEveryResultsView(EndGameScreen &results)
{
	int views = 0;
	results.selectMetric(EndGameScreen::OVERVIEW);
	++views;
	for (int metric = 0; metric < int(results.metrics().size()); ++metric)
	{
		results.selectMetric(metric);
		++views;
	}
	results.expandChart(true);
	results.showTeamFilters(true);
	for (int row = 0; row < int(results.results().size()); ++row)
		results.toggleTeam(row);
	results.showTeamFilters(false);
	results.expandChart(false);
	results.selectMetric(EndGameScreen::OVERVIEW);
	return views;
}

void Runner::begin(const std::string &step, std::uint32_t tick)
{
	started = tick;
	phase = Phase::Waiting;
	auto finished = [this](int) { returned = true; };
	if (step == "custom")
		launch([this] { singlePlayer.custom(); });
	else if (step == "load")
		launch([this, finished] { singlePlayer.play([](Engine &engine) { return engine.initCustomTask(LOAD_SAVE); }, finished); });
	else if (step == "lesson")
		launch([this, finished] { singlePlayer.play([](Engine &engine) { return engine.initCampaignTask(LESSON_MAP); }, finished); });
	else if (step == "tutorial")
		launch([this, finished]
			   { singlePlayer.play([](Engine &engine) { return engine.initCampaignTask(Tutorial::IntroductionMap); }, finished); });
	else if (step == "victory")
		launch([this, finished] { singlePlayer.play([](Engine &engine) { return engine.initCustomTask(VICTORY_SAVE); }, finished); });
}

std::optional<int> Runner::frame(std::uint32_t tick)
{
	if (current >= steps.size())
	{
		say("passed");
		SDL_Log("Glob2 release check passed");
		return 0;
	}
	const std::string &step = steps[current];
	if (std::find(allSteps().begin(), allSteps().end(), step) == allSteps().end())
		return fail("unknown step " + step);
	auto *top = screens.top();
	if (!top)
		return std::nullopt;
	if (phase == Phase::Start)
	{
		// Every step starts from the main menu.
		if (!dynamic_cast<MainMenuScreen *>(top))
		{
			if (tick - started > STEP_TIMEOUT_MS && started)
				return fail("the main menu did not come back");
			return std::nullopt;
		}
		if (step == "menu")
		{
			say("menu ok");
			++current;
			started = tick;
			return std::nullopt;
		}
		begin(step, tick);
		return std::nullopt;
	}
	if (tick - started > STEP_TIMEOUT_MS)
		return fail("timed out");

	if (step == "custom")
		if (auto *setup = dynamic_cast<CustomGameScreen *>(top))
		{
			if (!quitSent && !resultsSeen)
				setup->launch(); // Start, with the setup the screen opened with
			else
			{
				setup->endExecute(CustomGameScreen::CANCEL); // back to the main menu
				returned = true;
			}
			return std::nullopt;
		}
	if (auto *running = session())
	{
		Engine *engine = running->runningEngine();
		if (!engine)
			return std::nullopt;
		const auto tickNow = engine->drawnTick();
		if (!firstTick)
			firstTick = tickNow;
		if (step == "victory" && !quitSent && engine->endOfGameShown())
		{
			// The game ended on its own and shows "You have won": leave it as a player does.
			ended = true;
			say("victory: the game ended at tick " + std::to_string(tickNow));
			engine->quitFromMenu();
			quitSent = true;
		}
		else if (step != "victory" && !quitSent && tickNow >= *firstTick + MATCH_TICKS)
		{
			engine->quitFromMenu(); // the in-game menu's Leave
			quitSent = true;
		}
		return std::nullopt;
	}
	if (auto *end = results())
	{
		if (resultsSeen)
			return std::nullopt;
		resultsSeen = true;
		if (step == "victory")
		{
			if (!ended)
				return fail("the game did not end on its own");
			const int views = openEveryResultsView(*end);
			auto *files = GAGCore::Toolkit::getFileManager();
			if (!globalContainer->replayWriter || !globalContainer->replayWriter->write(REPLAY_FILE) ||
				!files || !files->exists(REPLAY_FILE))
				return fail("the replay was not saved");
			say(std::string("victory: ") + outcomeName(end->shownOutcome()) + ", " + std::to_string(views) +
				" results views, replay saved");
		}
		end->activateResultControl(EndGameScreen::QUIT);
		return std::nullopt;
	}
	if (returned && dynamic_cast<MainMenuScreen *>(top))
	{
		if (step != "tutorial" && step != "victory" && !resultsSeen)
			return fail("the results screen never showed");
		say(step + " ok");
		++current;
		phase = Phase::Start;
		started = tick;
	}
	return std::nullopt;
}
} // namespace ReleaseCheck
