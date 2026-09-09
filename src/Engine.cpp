// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <FileManager.h>
#include <FormatableString.h>

#include "EndGameScreen.h"
#include "Engine.h"
#include "FrontendTheme.h"
#include "EngineTiming.h"
#include "GlobalContainer.h"
#include "ReplayWriter.h"
#include "SoundMixer.h"

#include <iostream>


Engine::Engine() = default;

Engine::~Engine()
{
	globalContainer->liveSpectating=false;
	if(previousCustomSpeed>=0) {
        globalContainer->settings.gameSpeed=previousCustomSpeed;
        // In-game options may have persisted the temporary match speed.
        globalContainer->settings.save();
    }
	// Finalize the replay of the session this Engine ran, if any.
	// initGame allocated the writer; destroying it (ReplayWriter::finish)
	// writes the NullOrder terminator and flushes the replay file. This must
	// happen after run() returns because the EndGameScreen shown there needs
	// the writer alive to offer "save replay".
	globalContainer->replayWriter.reset();
}

int Engine::run(void)
{
	FrontendScope gameplay(false);
	bool doRunOnceAgain=true;
	if (globalContainer->runNoX)
	{
		assert(globalContainer->mix==nullptr);
		printf("nox::game started\n");
		automaticGameStartTick = SDL_GetTicks64();
	}
	else
	{
		if (!globalContainer->mix->selectMusicSet(globalContainer->settings.musicSet))
		{
			std::cerr << "Music set unavailable; trying random selection." << std::endl;
			globalContainer->settings.musicSet.clear();
			globalContainer->mix->selectMusicSet("");
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
		FrontendScope results(true);
		EndGameScreen endGameScreen(&gui);
		int result = endGameScreen.execute(globalContainer->gfx, GAME_TICK_MS);

		// Return to default color
		globalContainer->gfx->cursorManager.setDefaultColor();

		// Return
		return (result == -1) ? -1 : EE_NO_ERROR;
	}
}
