// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include <FileManager.h>
#include <FormatableString.h>

#include "EndGameScreen.h"
#include "Engine.h"
#include "hive/HiveClient.h"
#include "OnlineServices.h"
#include "GameDiagnostics.h"
#include "TurnMatchPresenter.h"
#include "sim/SimulationRunner.h"
#include "sim/presentation/ScenePreparation.h"
#include "EngineTiming.h"
#include "GlobalContainer.h"
#include "OnlineMatch.h"
#include "online/SkinDownloads.h"
#include "online/ReplayAppearance.h"
#include <Toolkit.h>
#include "ReplayWriter.h"
#include "SoundMixer.h"

#include <iostream>

Engine::Engine()
{
	PerformanceTelemetry::collector().reset();
}

Engine::~Engine()
{
	stopSimulationThread();
	PerformanceTelemetry::collector().enabled = false;
	globalContainer->liveSpectating=false;
	if(previousCustomSpeed>=0) {
        globalContainer->settings.gameSpeed=previousCustomSpeed;
        // In-game options may have persisted the temporary match speed.
        globalContainer->settings.save();
    }
	// Closing the window stops every screen without finishing the session: a turn
	// match still says goodbye, and its relay connection lingers until the Quit is
	// written (RelayTransport).
	try { leaveTurnMatch(); }
	catch (...) {}
	// Finalize the replay of the session this Engine ran, if any.
	// initGame allocated the writer; destroying it (ReplayWriter::finish)
	// writes the NullOrder terminator and flushes the replay file. This must
	// happen after run() returns because the EndGameScreen shown there needs
	// the writer alive to offer "save replay".
	globalContainer->replayWriter.reset();
}

void Engine::prepareRun()
{
	if (globalContainer->runNoX)
	{
		assert(globalContainer->mix==nullptr);
		printf("nox::game started\n");
		automaticGameStartTick = SDL_GetTicks();
	}
	else
	{
		if (!globalContainer->mix->selectMusicSet(globalContainer->settings.musicSet))
		{
			std::cerr << "Music set unavailable; trying random selection." << std::endl;
			globalContainer->mix->selectMusicSet("");
		}

		// Stop menu music, load game music
		globalContainer->mix->setNextTrack(MusicTrack::InGameDefault, true);
		globalContainer->gfx->cursorManager.setDrawColor(gui.getLocalTeam()->color);
	}

}

void Engine::setOnlineResult(std::shared_ptr<Online::OnlineMatchResult> result)
{
    onlineResult = std::move(result);
    if (onlineResult)
    {
        gui.networkMatch.online = true;
        gui.hive=std::make_shared<Hive::Client>(gui,Online::services().client,onlineResult->matchId,gui.localPlayer);
        gui.networkMatch.rated = onlineResult->rated;
        gui.networkMatch.fromRoom = onlineResult->fromRoom;
    }
}

std::unique_ptr<GAGGUI::Screen> Engine::endRunScreen()
{
    if (gui.exitGlobCompletely || globalContainer->runNoX || globalContainer->automaticEndingGame)
        return {};
    assert(globalContainer->mix);
    globalContainer->mix->setNextTrack(MusicTrack::Menu, true);
    auto screen = std::make_unique<EndGameScreen>(&gui);
    if (onlineResult)
        screen->setOnlineResult(onlineResult);
    return screen;
}

Team* Engine::gameTeam(int team)
{
    if (team < 0 || team >= gui.game.mapHeader.getNumberOfTeams())
        return nullptr;
    return gui.game.teams[team];
}

void Engine::restoreCursor()
{
    if (!globalContainer->runNoX) globalContainer->gfx->cursorManager.setDefaultColor();
}

int Engine::run(void)
{
    prepareRun();
    bool doRunOnceAgain = true;
    while (doRunOnceAgain) runOneGameSession(doRunOnceAgain);
    auto endScreen = endRunScreen();
    const int result = endScreen ? endScreen->execute(globalContainer->gfx, GAME_TICK_MS) : -1;
    restoreCursor();
    return result == -1 ? -1 : EE_NO_ERROR;
}

void Engine::setColonySkins(std::unique_ptr<Online::SkinDownloads> downloads)
{
    if (downloads && globalContainer->replayWriter)
    {
        const Online::ReplayAppearance context{downloads->origin(),downloads->matchId()};
        globalContainer->replayWriter->setSaveObserver([context](const std::string &filename) {
            Online::writeReplayAppearance(*GAGCore::Toolkit::getFileManager(),filename,context);
        });
    }
    gui.setColonySkins(std::move(downloads));
}
