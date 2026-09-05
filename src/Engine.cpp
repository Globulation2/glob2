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

int Engine::run(void)
{
	bool doRunOnceAgain=true;
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
			if (globalContainer->fileManager->isDir(FormatableString("%0/%1").arg("data/zik/").arg(filename)))
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
			globalContainer->mix->loadTrack(FormatableString("data/zik/%0/a1.ogg").arg(musicDir), MusicTrack::InGameDefault);
			globalContainer->mix->loadTrack(FormatableString("data/zik/%0/a2.ogg").arg(musicDir), MusicTrack::BuildingEvent);
			globalContainer->mix->loadTrack(FormatableString("data/zik/%0/a3.ogg").arg(musicDir), MusicTrack::WarEvent);
		}
		else
		{
			std::cerr << "Warning, no music found!" << std::endl;
		}

		// Stop menu music, load game music
		globalContainer->mix->setNextTrack(MusicTrack::InGameDefault, true);
		globalContainer->gfx->cursorManager.setDrawColor(gui.getLocalTeam()->color);
	}

	while (doRunOnceAgain)
	{
		runOneGameSession(doRunOnceAgain);
	}

	if (gui.exitGlobCompletely)
		return -1; // There is no bypass for the "close window button"

	if (globalContainer->runNoX || globalContainer->automaticEndingGame)
	{
		if(!globalContainer->runNoX)
			globalContainer->gfx->cursorManager.setDefaultColor();
		return -1;
	}
	else
	{
		// Restart menu music
		assert(globalContainer->mix);
		globalContainer->mix->setNextTrack(MusicTrack::Menu, true);

		// Display End Game Screen
		EndGameScreen endGameScreen(&gui);
		int result = endGameScreen.execute(globalContainer->gfx, GAME_TICK_MS);

		// Return to default color
		globalContainer->gfx->cursorManager.setDefaultColor();

		// Return
		return (result == -1) ? -1 : EE_NO_ERROR;
	}
}
