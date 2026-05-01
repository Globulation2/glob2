/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <FileManager.h>
#include <FormatableString.h>
#include <GraphicContext.h>

#include "EndGameScreen.h"
#include "engine.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "SoundMixer.h"

#include <iostream>


Engine::Engine()
{
	net=NULL;
	checksumSidecar=NULL;
	logFile = globalContainer->logFileManager->getFile("Engine.log");
}

Engine::~Engine()
{
	fprintf(logFile, "\n");

	if (net)
	{
		delete net;
		net=NULL;
	}
}

int Engine::run(void)
{
	bool doRunOnceAgain=true;
	if (globalContainer->runNoX)
	{
		assert(globalContainer->mix==NULL);
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
		// FIXME: implement more intelligent music choosing policy
		if (!musicDirs.empty())
		{
			size_t musicIndex(rand() % musicDirs.size());
			const std::string& musicDir(musicDirs[musicIndex]);
			std::cerr << "selecting music dir " << musicDir << std::endl;
			globalContainer->mix->loadTrack(FormatableString("data/zik/%0/a1.ogg").arg(musicDir), 2);
			globalContainer->mix->loadTrack(FormatableString("data/zik/%0/a2.ogg").arg(musicDir), 3);
			globalContainer->mix->loadTrack(FormatableString("data/zik/%0/a3.ogg").arg(musicDir), 4);
		}
		else
		{
			std::cerr << "Warning, no music found!" << std::endl;
		}

		// Stop menu music, load game music
		globalContainer->mix->setNextTrack(2, true);
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
		globalContainer->mix->setNextTrack(1, true);

		// Display End Game Screen
		EndGameScreen endGameScreen(&gui);
		int result = endGameScreen.execute(globalContainer->gfx, 40);

		// Return to default color
		globalContainer->gfx->cursorManager.setDefaultColor();

		// Return
		return (result == -1) ? -1 : EE_NO_ERROR;
	}
}
