// SPDX-License-Identifier: GPL-3.0-or-later
#include "MenuColony.h"
#include "GlobalContainer.h"
#include "EngineTiming.h"
#include "ReplayWriter.h"
#include "DatasetWriter.h"
#include "Order.h"
#include "Player.h"
#include "sim/presentation/ScenePreparation.h"
#include <sstream>
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>
#include <algorithm>
#include <array>
#include <iostream>

namespace
{
SceneRequest colonyRequest(Uint64 tickTime)
{
	SceneRequest request;
	request.includePanels = false;
	request.spectating = true;
	request.tickTime = tickTime;
	request.tickInterval = GAME_TICK_MS;
	return request;
}
// The colony game owns its synchronized random stream. Detach optional recording
// sinks only while the menu game is executing, including during construction/loading.
struct ColonyContext
{
	std::unique_ptr<ReplayWriter> replay;
	std::unique_ptr<DatasetWriter> dataset;
	ColonyContext() :
		replay(std::move(globalContainer->replayWriter)),
		dataset(std::move(globalContainer->datasetWriter))
	{}
	~ColonyContext()
	{
		globalContainer->replayWriter = std::move(replay);
		globalContainer->datasetWriter = std::move(dataset);
	}
};
}

MenuColony::MenuColony() = default;
MenuColony::~MenuColony() = default;

bool MenuColony::load(const std::string& path)
{
	ColonyContext context;
	pause();
	presentation.reset();
	view = Game::ViewState();
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
		if (!(state >> loaded->syncRandom)) throw std::runtime_error("invalid colony RNG");
		loaded->setWaitingOnMask(0);
		// The map round-robin updater expects at least one lazy gradient.
		loaded->map.getMaterialGradient(0, MaterialId::Food, 0);
		centerX = loaded->teams[0]->startPosX;
		centerY = loaded->teams[0]->startPosY;

		game = std::move(loaded);
		worldTickTime = SDL_GetTicks();
		if (!globalContainer->runNoX)
		{
			presentation = std::make_unique<ScenePreparation>(game->map.computeExecutor());
			const auto request = colonyRequest(worldTickTime);
			const auto required = SceneExtractor::requirements(request);
			const std::array<unsigned, 1> actors{0};
			const auto world = game->captureReadBoundary(actors, false, required);
			presentation->submit(SceneExtractor::inputs(world.project(required), request));
		}
		return true;
	}
	catch (const std::exception& error)
	{
		presentation.reset();
		view = Game::ViewState();
		game.reset();
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
	pending += std::min<Uint64>((now - lastTime) * 1000000ULL, 2 * GAME_TICK_NS);
	lastTime = now;
	ColonyContext context;
	// At most two steps per menu frame; never accumulate hidden-time debt.
	while (pending >= GAME_TICK_NS)
	{
        const std::array<unsigned,1> actors{0};
        const bool admitted = presentation && presentation->readyToCapture();
        const auto request = colonyRequest(worldTickTime);
        const auto required = admitted ? SceneExtractor::requirements(request) : 0;
        const auto world = game->captureReadBoundary(actors, false, required);
        if (admitted) presentation->submit(SceneExtractor::inputs(world.project(required), request));
        for(const auto& [actor,scheduled]:game->prepareAIOrders(actors,false,nullptr,&world)) {
            scheduled->sender=actor;
            auto order=game->validateAIOrder(scheduled,actor);
            game->executeOrder(order,0);
        }
		game->syncStep(0);
		worldTickTime = SDL_GetTicks();
		pending -= GAME_TICK_NS;
	}
}

void MenuColony::draw(int width, int height, bool showStatus)
{
	if (!presentation) return;
	view.scene = presentation->acquire();
	if (!view.scene) return;
	ColonyContext context;
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
	const int x = (centerX - width * 2 / 3 / 32) & view.scene->map.getMaskW();
	const int y = (centerY - height / 2 / 32) & view.scene->map.getMaskH();
	Game::drawMap(0, 0, width, height, 0, 0, x, y, 0, view,
		Game::DRAW_WHOLE_MAP | (showStatus ? Game::DRAW_HEALTH_FOOD_BAR : 0));
}

Uint32 MenuColony::checksum() const
{
	return game ? game->checkSum(nullptr, nullptr, nullptr) : 0;
}
