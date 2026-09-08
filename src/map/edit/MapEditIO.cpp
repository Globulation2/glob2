// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include <ApplicationHost.h>
#include <BinaryStream.h>
#include <GAG.h>
#include "GameGUILoadSave.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "ScriptEditorScreen.h"
#include <Stream.h>
#include "Unit.h"
#include "UnitType.h"
#include "Utilities.h"
#include "FertilityCalculatorDialog.h"
#include "GUIMessageBox.h"
#include "SDLCompat.h"

bool MapEdit::load(const std::string filename)
{
    return loadTask(filename).run();
}

GAGCore::CooperativeTask MapEdit::loadTask(std::string filename)
{
    auto stream = std::make_unique<BinaryInputStream>(Toolkit::getFileManager()->openInputStreamBackend(filename));
    if (stream->isEndOfStream()) co_return false;
    try {
        if (!(co_await game.loadTask(stream.get()))) { doQuitAfterLoadSave = true; co_return false; }
    } catch (const std::exception&) {
        doQuitAfterLoadSave = true;
        co_return false;
    }
    team = 0;
    areaNameLabel->setLabel(game.map.getAreaName(areaNumber->getIndex()));
    minimap.resetMinimapDrawing();
    game.map.computeDisplayedForbidden(team);
    game.map.computeDisplayedClearArea(team);
    game.map.computeDisplayedGuardArea(team);
    hasMapBeenModified = false;
    co_return true;
}


bool MapEdit::save(const std::string filename, const std::string name)
{
	assert(filename.size());
	assert(name.size());

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
		hasMapBeenModified = false;
		game.mapHeader.setMapName(name);
		game.mapHeader.setIsSavedGame(false);
		return true;
	}
}



void MapEdit::beginEditing()
{
	minimap.setGame(game);
	globalContainer->gfx->setClipRect();
	drawMap(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH());
	drawMiniMap();
	drawMenu();


	if(game.gameHeader.getNumberOfPlayers() == 0)
		regenerateGameHeader();

    editing = true;
    editingResult = 0;
}

bool MapEdit::advanceEditing(const std::vector<SDL_Event>& events, Uint32 tick)
{
    if (!editing || quitDecision || fertilityRequested) return editing;
    for (auto event : events) {
        GAGCore::GraphicContext::translateMouseEvent(&event);
        processEvent(event);
        if (doFullQuit || doQuit || fertilityRequested || (doQuitAfterLoadSave && !showingSave)) break;
    }
    if (doFullQuit) { editingResult = -1; editing = false; return false; }
    if (fertilityRequested) return true;
	// While processing events the user could've tried to load a map that failed.
	// Then we can't go through drawing everything because that would segfault.
	if(doQuitAfterLoadSave && !showingSave)
	{
            editing = false;
            return false;
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
		else if(isDraggingNoResourceGrowthArea)
			performAction("no ressource growth area drag motion");
	}

    if (showingMenuScreen) menuScreen->dispatchTimer(tick);
    if (showingLoad || showingSave) loadSaveScreen->dispatchTimer(tick);
    if (showingScriptEditor) scriptEditor->dispatchTimer(tick);
    if (showingTeamsEditor) teamsEditor->dispatchTimer(tick);
    if (isShowingAreaName) areaName->dispatchTimer(tick);
    if (doFullQuit) { editingResult = -1; editing = false; }
    else if (doQuit) {
        doQuit = false;
        if (hasMapBeenModified) quitDecision = true;
        else editing = false;
    }
    return editing;
}

void MapEdit::drawEditing()
{
    if (!editing) return;
	drawMap(0, 0, globalContainer->gfx->getW()-0, globalContainer->gfx->getH());

	drawMenu();
	drawMiniMap();
	wasMinimapRendered=false;
	drawWidgets();
	if(showingMenuScreen)
	{
		globalContainer->gfx->setClipRect();
		menuScreen->dispatchPaint();
		globalContainer->gfx->drawSurface((int)menuScreen->decX, (int)menuScreen->decY, menuScreen->getSurface());
	}
	if(showingLoad || showingSave)
	{
		globalContainer->gfx->setClipRect();
		loadSaveScreen->dispatchPaint();
		globalContainer->gfx->drawSurface((int)loadSaveScreen->decX, (int)loadSaveScreen->decY, loadSaveScreen->getSurface());
	}
	if(showingScriptEditor)
	{
		globalContainer->gfx->setClipRect();
		scriptEditor->dispatchPaint();
		globalContainer->gfx->drawSurface((int)scriptEditor->decX, (int)scriptEditor->decY, scriptEditor->getSurface());
	}
	if(showingTeamsEditor)
	{
		globalContainer->gfx->setClipRect();
		teamsEditor->dispatchPaint();
		globalContainer->gfx->drawSurface((int)teamsEditor->decX, (int)teamsEditor->decY, teamsEditor->getSurface());
	}
	if(isShowingAreaName)
	{
		globalContainer->gfx->setClipRect();
		areaName->dispatchPaint();
		globalContainer->gfx->drawSurface((int)areaName->decX, (int)areaName->decY, areaName->getSurface());
	}


	globalContainer->gfx->nextFrame();


}

void MapEdit::resolveQuitDecision(int choice)
{
    if (!quitDecision) return;
    quitDecision = false;
    if (choice == 0) {
        doQuitAfterLoadSave = true;
        performAction("open save screen");
    } else if (choice == 1) editing = false;
}

bool MapEdit::finishFertility(bool completed)
{
    fertilityRequested = false;
    bool saved = true;
    if (!pendingSaveFilename.empty()) {
        if (completed) saved = save(pendingSaveFilename, pendingSaveName);
        if (!completed || !saved) doQuitAfterLoadSave = false;
        pendingSaveFilename.clear(); pendingSaveName.clear();
    } else if (completed) {
        overlay.forceRecompute();
        overlay.compute(game, OverlayArea::Fertility, team);
    } else isFertilityOn = false;
    return saved;
}
