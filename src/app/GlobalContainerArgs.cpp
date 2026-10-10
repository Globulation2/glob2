// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2007 Stephane Magnenat & Luc-Olivier de Charrière
#include "GlobalContainer.h"
#include "CommandLine.h"
#include "ComputeThreads.h"
#include "AINames.h"
#include "FileManager.h"
#include "InviteLink.h"
#include <Toolkit.h>
#include <GameplayRecording.h>
#include <algorithm>
#include <sstream>
#include <stdexcept>

void GlobalContainer::applyCommand(const Cli::Request &request)
{
	const auto &r = request;
	for (const auto &path : r.all("--data-dir"))
		fileManager->addDir(path);
	if (r.has("--compute-threads"))
		computeThreads = parseComputeThreadCount(r.get("--compute-threads"));
	auto toggle = [&](const char *name, unsigned bit)
	{
		if (r.has(std::string("--") + name))
			settings.screenFlags |= bit;
		if (r.has(std::string("--no-") + name))
			settings.screenFlags &= ~bit;
	};
	toggle("fullscreen", GraphicContext::FULLSCREEN);
	toggle("resizable", GraphicContext::RESIZABLE);
	toggle("custom-cursor", GraphicContext::CUSTOMCURSOR);
	if (r.has("--renderer"))
	{
		if (r.get("--renderer") == "gpu")
			settings.screenFlags |= GraphicContext::USEGPU;
		else
			settings.screenFlags &= ~GraphicContext::USEGPU;
	}
	if (r.has("--graphics-detail"))
		settings.setGraphicsDetail(r.get("--graphics-detail") == "full");
	if (r.has("--mute"))
		settings.mute = 1;
	if (r.has("--no-mute"))
		settings.mute = 0;
	if (r.has("--username"))
		settings.setUsername(r.get("--username").c_str());
	if (r.has("--editor-script"))
	{
		if (r.get("--editor-script") == "usl")
			settings.optionFlags |= OPTION_MAP_EDIT_USE_USL;
		else
			settings.optionFlags &= ~OPTION_MAP_EDIT_USE_USL;
	}
	if (r.has("--window-size"))
	{
		const auto text = r.get("--window-size");
		const auto x = text.find('x');
		settings.screenWidth = std::max(640, std::stoi(text.substr(0, x)));
		settings.screenHeight = std::max(480, std::stoi(text.substr(x + 1)));
	}
	if (r.has("--record"))
		recordingPath = r.get("--record");
	if (r.has("--videoshot"))
		videoshotName = r.get("--videoshot");
	if (r.has("--record") || r.has("--videoshot"))
	{
		auto &options = GAGCore::Recording::recorder().options;
		options.fps = std::stoi(r.get("--record-fps"));
		options.crf = std::stoi(r.get("--record-crf"));
		options.chapterTicks = unsigned(std::stoul(r.get("--record-chapter-ticks")));
		options.encoder = r.get("--record-encoder") == "software"
							  ? GAGCore::Recording::EncoderPreference::Software
							  : GAGCore::Recording::EncoderPreference::Auto;
	}
	if (r.command == "replay")
	{
		replaying = true;
		replayFileName = r.positionals.at(0);
	}
	if (r.command == "game repeat")
	{
		runNoX = true;
		automaticEndingGame = true;
		runNoXGameName = r.positionals.at(0);
		automaticEndingSteps = std::stoi(r.get("--ticks"));
		runNoXCountRuns = std::stoi(r.get("--runs"));
	}
	if (r.command == "dev random-games")
	{
		runTestGames = true;
		runNoX = !r.has("--display");
		automaticEndingGame = true;
		automaticGameGlobalEndConditions = true;
		if (r.has("--ticks"))
			automaticEndingSteps = std::stoi(r.get("--ticks"));
		runTestGamesCount = std::stoi(r.get("--runs"));
		testGamesMap = r.get("--map");
		testGamesSaveGameAs = r.get("--save-game-as");
		auto aiList = [&](const std::string &key, std::vector<int> &out)
		{
			std::istringstream list(r.get(key));
			std::string item;
			while (std::getline(list, item, ','))
			{
				const auto id = AINames::parseAIName(item);
				if (id <= 0 || id == AI::JAVASCRIPT)
					throw std::invalid_argument(key + ": invalid AI '" + item +
												"'; valid: " + AINames::validAINames());
				out.push_back(id);
			}
		};
		aiList("--ai-types", testGamesAIPool);
		aiList("--matchup", testGamesMatchup);
	}
	if (r.command == "dev stress-maps")
	{
		runTestMapGeneration = true;
		runNoX = true;
	}
	if (r.command == "dev textshots")
		GAGCore::DrawableSurface::translationPicturesDirectory = r.get("--output-dir");
	if (r.command.rfind("dev dump-", 0) == 0)
		runNoX = true;
	Online::acceptLaunchRequest(r);
}
