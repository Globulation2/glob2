// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <FileManager.h>
#include <FormatableString.h>

#include "EndGameScreen.h"
#include "Engine.h"
#include "EngineTiming.h"
#include "GlobalContainer.h"
#include "ReplayWriter.h"
#include "SoundMixer.h"

#include <iostream>


Engine::Engine() = default;

Engine::~Engine()
{
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
		automaticGameStartTick = SDL_GetTicks64();
	}
	else
	{
		// look for all available musics
		globalContainer->fileManager->initDirectoryListing("data/zik/", "", true);
		std::string filename;
		std::vector<std::string> musicDirs;
		while (!(filename = globalContainer->fileManager->getNextDirectoryEntry()).empty())
		{
			if (globalContainer->fileManager->isDir(FormattableString("%0/%1").arg("data/zik/").arg(filename)))
			{
				std::cerr << "music dir found: " << filename << std::endl;
				musicDirs.push_back(filename);
			}
		}

		// select a music randomly
		if (!musicDirs.empty())
		{
			size_t musicIndex(rand() % musicDirs.size());
			const std::string& musicDir(musicDirs[musicIndex]);
			std::cerr << "selecting music dir " << musicDir << std::endl;
			globalContainer->mix->loadTrack(FormattableString("data/zik/%0/a1.ogg").arg(musicDir), MusicTrack::InGameDefault);
			globalContainer->mix->loadTrack(FormattableString("data/zik/%0/a2.ogg").arg(musicDir), MusicTrack::BuildingEvent);
			globalContainer->mix->loadTrack(FormattableString("data/zik/%0/a3.ogg").arg(musicDir), MusicTrack::WarEvent);
		}
		else
		{
			std::cerr << "Warning, no music found!" << std::endl;
		}

		// Stop menu music, load game music
		globalContainer->mix->setNextTrack(MusicTrack::InGameDefault, true);
		globalContainer->gfx->cursorManager.setDrawColor(gui.getLocalTeam()->color);
	}

}

std::unique_ptr<GAGGUI::Screen> Engine::endRunScreen()
{
    if (gui.exitGlobCompletely || globalContainer->runNoX || globalContainer->automaticEndingGame)
        return {};
    assert(globalContainer->mix);
    globalContainer->mix->setNextTrack(MusicTrack::Menu, true);
    return std::make_unique<EndGameScreen>(&gui);
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
