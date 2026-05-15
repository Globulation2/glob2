// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include <cmath>
#include <BinaryStream.h>
#include <FormatableString.h>
#include <GAG.h>
#include "GameGUILoadSave.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "MapEditKeyActions.h"
#include "ScriptEditorScreen.h"
#include <sstream>
#include <StreamFilter.h>
#include <Stream.h>
#include "UnitEditorScreen.h"
#include "Unit.h"
#include "UnitType.h"
#include "Utilities.h"
#include "FertilityCalculatorDialog.h"
#include "GUIMessageBox.h"
#include "SDLCompat.h"

bool MapEdit::load(const std::string filename)
{
	assert(filename.size());

	InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(filename));
	if (stream->isEndOfStream())
	{
		std::cerr << "MapEdit::load(\"" << filename << "\") : error, can't open file." << std::endl;
		delete stream;
		return false;
	}
	else
	{
		bool rv;

		try
		{
			rv = game.load(stream);
		}
		catch (std::exception &e)
		{
			std::cerr << "Failed to open map: bad format." << std::endl;

			if (!globalContainer->runNoX)
			{
				// Display an error message
				GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON, Toolkit::getStringTable()->getString("[ERROR_CANT_LOAD_MAP]"), Toolkit::getStringTable()->getString("[ok]"));
			}

			// We can't recover from this, so we quit
			doQuitAfterLoadSave = true;

			return false;
		}
		
		delete stream;
		if (!rv)
			return false;
		
		// set the editor default values
		team = 0;
	
		areaNameLabel->setLabel(game.map.getAreaName(areaNumber->getIndex()));
		
		minimap.resetMinimapDrawing();
		
		game.map.computeLocalForbidden(team);
		game.map.computeLocalClearArea(team);
		game.map.computeLocalGuardArea(team);
	
		hasMapBeenModified = false;
		return true;
	}
	return false;
}



bool MapEdit::save(const std::string filename, const std::string name)
{
	FertilityCalculatorDialog dialog(globalContainer->gfx, game.map);
	dialog.runModal();

	assert(filename.size());
	assert(name.size());

	hasMapBeenModified = false;

	OutputStream *stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(filename));
	if (stream->isEndOfStream())
	{
		std::cerr << "MapEdit::save(\"" << filename << "\",\"" << name << "\") : error, can't open file." << std::endl;
		delete stream;
		return false;
	}
	else
	{
		game.save(stream, true, name);
		delete stream;

		// Game::save() now restores mapHeader.mapName/isSavedGame so that
		// in-game saves don't permanently clobber the live map name. The
		// editor relies on the post-save mutation for its "current name"
		// UI (the LoadSaveScreen default), so re-apply explicitly.
		game.mapHeader.setMapName(name);
		game.mapHeader.setIsSavedGame(false);
		return true;
	}
}



int MapEdit::run(int sizeX, int sizeY, TerrainType terrainType)
{
	game.map.setSize(sizeX, sizeY, terrainType);
	game.map.setGame(&game);
	return run();
}



int MapEdit::run(void)
{
	//globalContainer->gfx->setRes(globalContainer->graphicWidth, globalContainer->graphicHeight , 32, globalContainer->graphicFlags, (DrawableSurface::GraphicContextType)globalContainer->settings.graphicType);

// 	regenerateClipRect();

		
	minimap.setGame(game);
	globalContainer->gfx->setClipRect();
	drawMap(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH(), true, true);
	drawMiniMap();
	drawMenu();
	
	
	if(game.gameHeader.getNumberOfPlayers() == 0)
		regenerateGameHeader();

	bool isRunning=true;
	int returnCode=0;
	Uint64 startTick, endTick, deltaTick;
	while (isRunning)
	{
		//SDL_Event event;
		startTick=SDL_GetTicks64();
	
		// we get all pending events but for mousemotion we only keep the last one
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
 			processEvent(event);
		}

		// While processing events the user could've tried to load a map that failed.
		// Then we can't go through drawing everything because that would segfault.
		if(doQuitAfterLoadSave && !showingSave)
		{
			isRunning = false;
			break;
		}
		
		if(!showingMenuScreen && !showingLoad && !showingSave && !showingScriptEditor && !showingTeamsEditor)
		{
			handleMapScroll();
			viewportX+=xSpeed;
			viewportY+=ySpeed;
			viewportX&=game.map.getMaskW();
			viewportY&=game.map.getMaskH();
		}

		//special overrides here to allow for scrolling and painting terrain at the same time
		if(xSpeed!=0 || ySpeed!=0)
		{
			if(isDraggingZone)
				performAction("zone drag motion");
			else if(isDraggingTerrain)
				performAction("terrain drag motion");
			else if(isDraggingDelete)
				performAction("delete drag motion");
			else if(isDraggingArea)
				performAction("area drag motion");
			else if(isDraggingNoRessourceGrowthArea)
				performAction("no ressource growth area drag motion");
		}
		
		drawMap(0, 0, globalContainer->gfx->getW()-0, globalContainer->gfx->getH(), true, true);
		
		drawMenu();
		drawMiniMap();
		wasMinimapRendered=false;
		drawWidgets();
		if(showingMenuScreen)
		{
			globalContainer->gfx->setClipRect();
			menuScreen->dispatchTimer(startTick);
			menuScreen->dispatchPaint();
			globalContainer->gfx->drawSurface((int)menuScreen->decX, (int)menuScreen->decY, menuScreen->getSurface());
		}
		if(showingLoad || showingSave)
		{
			globalContainer->gfx->setClipRect();
			loadSaveScreen->dispatchTimer(startTick);
			loadSaveScreen->dispatchPaint();
			globalContainer->gfx->drawSurface((int)loadSaveScreen->decX, (int)loadSaveScreen->decY, loadSaveScreen->getSurface());
		}
		if(showingScriptEditor)
		{
			globalContainer->gfx->setClipRect();
			scriptEditor->dispatchTimer(startTick);
			scriptEditor->dispatchPaint();
			globalContainer->gfx->drawSurface((int)scriptEditor->decX, (int)scriptEditor->decY, scriptEditor->getSurface());
		}
		if(showingTeamsEditor)
		{
			globalContainer->gfx->setClipRect();
			teamsEditor->dispatchTimer(startTick);
			teamsEditor->dispatchPaint();
			globalContainer->gfx->drawSurface((int)teamsEditor->decX, (int)teamsEditor->decY, teamsEditor->getSurface());
		}
		if(isShowingAreaName)
		{
			globalContainer->gfx->setClipRect();
			areaName->dispatchTimer(startTick);
			areaName->dispatchPaint();
			globalContainer->gfx->drawSurface((int)areaName->decX, (int)areaName->decY, areaName->getSurface());
		}
		
		
		globalContainer->gfx->nextFrame();
		

		endTick=SDL_GetTicks64();
		deltaTick=std::max<Sint64>(0, static_cast<Sint64>(endTick) - static_cast<Sint64>(startTick));
		if (deltaTick<33)
			SDL_Delay(33-deltaTick);
		if (returnCode==-1)
		{
			isRunning=false;
		}
		if(doQuitAfterLoadSave && !showingSave)
		{
			isRunning=false;
		}
		if(doQuit)
		{
			if(hasMapBeenModified)
			{
				int ret = GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_THREEBUTTONS, Toolkit::getStringTable()->getString("[save before quit?]"), Toolkit::getStringTable()->getString("[Yes]"), Toolkit::getStringTable()->getString("[No]"), Toolkit::getStringTable()->getString("[Cancel]"));
				if(ret == 0)
				{
					doQuit=false;
					doQuitAfterLoadSave=true;
					performAction("open save screen");
				}
				else if(ret == 1)
				{
					isRunning=false;
				}
				else
				{
					doQuit=false;
				}
			}
			else
			{
				isRunning=false;
			}
		}
		if(doFullQuit)
		{
			returnCode = -1;
		}
		if(!isRunning)
		{
				SDL_Event event;
			while (SDL_PollEvent(&event));
		}
	}

	//globalContainer->gfx->setRes(globalContainer->graphicWidth, globalContainer->graphicHeight , 32, globalContainer->graphicFlags, (DrawableSurface::GraphicContextType)globalContainer->settings.graphicType);
	return returnCode;
}


