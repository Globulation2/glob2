// SPDX-License-Identifier: GPL-3.0-or-later
#include "MenuColony.h"
#include "GlobalContainer.h"
#include "EngineTiming.h"
#include "ReplayWriter.h"
#include "DatasetWriter.h"
#include "Order.h"
#include "Player.h"
#include <sstream>
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>
#include <algorithm>
#include <iostream>

namespace
{
// Simulation uses a legacy global RNG and optional recording sinks. Swap them
// only while the menu game is executing, including during construction/loading.
struct ColonyContext
{
	boost::mt19937& rng;
	std::unique_ptr<ReplayWriter> replay;
	std::unique_ptr<DatasetWriter> dataset;
	explicit ColonyContext(boost::mt19937& state) : rng(state),
		replay(std::move(globalContainer->replayWriter)),
		dataset(std::move(globalContainer->datasetWriter))
	{ std::swap(randomGenerator, rng); }
	~ColonyContext()
	{
		std::swap(randomGenerator, rng);
		globalContainer->replayWriter = std::move(replay);
		globalContainer->datasetWriter = std::move(dataset);
	}
};
}

bool MenuColony::load(const std::string& path)
{
	ColonyContext context(rng);
	pause();
	game.reset();
	try
	{
		GAGCore::BinaryInputStream input(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(path));
		if (input.isEndOfStream()) throw std::runtime_error("missing menu snapshot");
		if (input.readText("format") != "glob2-menu-colony-1") throw std::runtime_error("invalid menu snapshot");
		auto loaded = std::make_unique<Game>(nullptr);
		if (!loaded->load(&input) || loaded->mapHeader.getNumberOfTeams() != 1 ||
			loaded->gameHeader.getNumberOfPlayers() != 1 || !loaded->players[0] ||
			!loaded->players[0]->ai || loaded->players[0]->ai->implementationID != AI::ECONO)
			throw std::runtime_error("incompatible menu colony");
		std::istringstream state(input.readText("rng") + " ");
		if (!(state >> randomGenerator)) throw std::runtime_error("invalid colony RNG");
		loaded->setWaitingOnMask(0);
		// The map round-robin updater expects at least one lazy gradient.
		loaded->map.getResourceGradient(0, WHEAT, 0);
		centerX = loaded->teams[0]->startPosX;
		centerY = loaded->teams[0]->startPosY;

		game = std::move(loaded);
		view = Game::ViewState();
		return true;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Menu colony unavailable: " << error.what() << '\n';
		return false;
	}
}

void MenuColony::pause()
{
	clockStarted = false;
	pending = 0;
}

void MenuColony::update(Uint64 now, bool visible)
{
	if (!game || !visible) { pause(); return; }
	if (!clockStarted) { lastTime = now; clockStarted = true; return; }
	pending += std::min<Uint64>(now - lastTime, 2 * GAME_TICK_MS);
	lastTime = now;
	ColonyContext context(rng);
	// At most two steps per menu frame; never accumulate hidden-time debt.
	while (pending >= GAME_TICK_MS)
	{
		auto order = game->players[0]->ai->getOrder(false);
		order->sender = 0;
		game->executeOrder(order, 0);
		game->syncStep(0);
		pending -= GAME_TICK_MS;
	}
}

void MenuColony::draw(int width, int height)
{
	if (!game) return;
	ColonyContext context(rng);
	struct ViewContext
	{
		bool replaying=globalContainer->replaying, flags=globalContainer->replayShowFlags;
		Uint32 teams=globalContainer->replayVisibleTeams;
		ViewContext()
		{
			globalContainer->replaying=true;
			globalContainer->replayShowFlags=false;
			globalContainer->replayVisibleTeams=1;
		}
		~ViewContext()
		{
			globalContainer->replaying=replaying;
			globalContainer->replayShowFlags=flags;
			globalContainer->replayVisibleTeams=teams;
		}
	} viewContext;
	// Place the starting settlement to the right of the front-page panel.
	const int x = (centerX - width * 2 / 3 / 32) & game->map.getMaskW();
	const int y = (centerY - height / 2 / 32) & game->map.getMaskH();
	game->drawMap(0, 0, width, height, 0, 0, x, y, 0, view, Game::DRAW_WHOLE_MAP | Game::DRAW_HEALTH_FOOD_BAR);
}

Uint32 MenuColony::checksum() const
{
	return game ? game->checkSum(nullptr, nullptr, nullptr) : 0;
}
