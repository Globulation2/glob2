// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include <ApplicationHost.h>
#include <BinaryStream.h>
#include <GAG.h>
#include "LoadSaveDialog.h"
#include "Game.h"
#include "SaveSnapshot.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "FrontendTheme.h"
#include "PhoneEditor.h"
#include "ScriptEditorScreen.h"
#include <Stream.h>
#include "Unit.h"
#include "UnitType.h"
#include "Utilities.h"
#include <SDL3/SDL.h>

bool MapEdit::load(const std::string filename)
{
    return loadTask(filename).run();
}

GAGCore::CooperativeTask MapEdit::loadTask(std::string filename)
{
    auto stream = std::make_unique<BinaryInputStream>(glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(), filename));
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
    game.map.computeDisplayedFarmArea(team);
    hasMapBeenModified = false;
    co_return true;
}


bool MapEdit::save(const std::string filename, const std::string name)
{
	assert(filename.size());
	assert(name.size());

    if (!Toolkit::getFileManager()->writeGzipAtomically(glob2GzipWritePath(filename), [&](OutputStream& stream) {
        game.save(&stream, true, name);
    })) return false;

    // Only publish the new editor name after the complete file was replaced.
    hasMapBeenModified = false;
    game.mapHeader.setMapName(name);
    game.mapHeader.setIsSavedGame(false);
    return true;
}



void MapEdit::beginEditing()
{
	FrontendScope editor(false);
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

bool MapEdit::touchAnimating() const
{
	return phone && phone->animating();
}

bool MapEdit::advanceEditing(const std::vector<SDL_Event>& events, Uint32 tick)
{
    if(saveWriter) saveWriter->poll();
    if (!editing || quitDecision || fertilityRequested || !pendingLoadFilename.empty()) return editing;
    const bool wasPersisting=showingSave && loadSaveScreen->isPersisting();
    const auto isQuitRequest = [](const SDL_Event& event) {
        if (event.type == SDL_EVENT_QUIT) return true;
#ifdef USE_OSX
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_Q &&
            (event.key.mod & SDL_KMOD_GUI)) return true;
#endif
#ifdef USE_WIN32
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F4 &&
            (event.key.mod & SDL_KMOD_ALT)) return true;
#endif
        return false;
    };
    // Record close intent before polling: persistence may fail in this frame.
    if (wasPersisting)
        for (const auto& event : events)
            if (isQuitRequest(event)) quitAfterSave = true;
    if (showingSave && loadSaveScreen->pollPersistence()) {
        hasMapBeenModified = false;
        performAction("close save screen");
    }
    if(wasPersisting && showingSave && loadSaveScreen->isPersisting()) {
        return true;
    }
    for (auto event : events) {
        if (quitAfterSave && showingSave && isQuitRequest(event)) continue;
        if(!(phone && phone->event(event))) {
            GAGCore::GraphicContext::translateMouseEvent(&event);
            processEvent(event);
        }
        if (doFullQuit || doQuit || fertilityRequested || !pendingLoadFilename.empty() || (doQuitAfterLoadSave && !showingSave)) break;
    }
    if (quitAfterSave && !showingSave) doFullQuit = true;
    if (doFullQuit) { editingResult = -1; editing = false; return false; }
    if (fertilityRequested || !pendingLoadFilename.empty()) return true;
	// While processing events the user could've tried to load a map that failed.
	// Then we can't go through drawing everything because that would segfault.
	if(doQuitAfterLoadSave && !showingSave)
	{
            editing = false;
            return false;
	}

	if(!showingMenuScreen && !showingLoad && !showingSave && !showingScriptEditor && !showingTeamsEditor)
	{
		if (!phone) handleMapScroll();
		if (phone) phone->advance(tick);
		updateCamera();
		camera.originX+=xSpeed*32/camera.zoom;
		camera.originY+=ySpeed*32/camera.zoom;
		camera.normalize();
		viewportX=camera.tileX();
		viewportY=camera.tileY();
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

    if (auto *dialog = activeDialog())
    {
        dialog->update(tick);
        // Browser-native text editing can finish a dialog without an SDL
        // event; poll its result every frame.
        if (dialog->finished())
        {
            SDL_Event poll{};
            poll.type = SDL_EVENT_USER;
            delegateMenu(poll);
        }
    }
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

	if(!phone) {drawMenu();drawMiniMap();}
	wasMinimapRendered=false;
	if(phone) {phone->draw();globalContainer->gfx->nextFrame();return;}
	drawWidgets();
	drawDialog();


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
    if (!pendingSaveFilename.empty()) {
        if (completed) {
            try {
                if (GAGCore::ApplicationHost::storageRestoreFailed()) loadSaveScreen->showSaveFailure();
                else {
                    if(!saveWriter) saveWriter=std::make_unique<GAGCore::BackgroundFileWriter>(Toolkit::getFileManager());
                    const auto name=pendingSaveName;
                    loadSaveScreen->beginPersistence(std::make_unique<SaveOperation>(*saveWriter,glob2GzipWritePath(pendingSaveFilename),
                        [this,name]{return captureSave([&](OutputStream* stream,DeferredGameSHA1* sha){game.save(stream,true,name,sha);});},
                        [this,name]{hasMapBeenModified=false;game.mapHeader.setMapName(name);game.mapHeader.setIsSavedGame(false);}));
                }
            } catch (const std::exception&) { loadSaveScreen->showSaveFailure(); }
            // A local write is not a durable browser save. Keep the editor and
            // its quit intent until the shared save dialog acknowledges it.
            hasMapBeenModified = true;
        } else {
            doQuitAfterLoadSave = false;
            performAction("close save screen");
        }
        pendingSaveFilename.clear(); pendingSaveName.clear();
    } else if (completed) {
        overlay.forceRecompute();
        overlay.compute(game, OverlayArea::Fertility, team);
    } else isFertilityOn = false;
    return true;
}

void MapEdit::viewportResized(int oldWidth, int oldHeight, int width, int height)
{
    minimap.resizeViewport(width);
    viewportX = (viewportX + (oldWidth - RIGHT_MENU_WIDTH) / 64 - (width - RIGHT_MENU_WIDTH) / 64) & game.map.wMask;
    viewportY = (viewportY + oldHeight / 64 - height / 64) & game.map.hMask;
    for (auto* widget : mew) widget->area.updateWindowWidth(width);
    for (MapEditorWidget* widget : std::initializer_list<MapEditorWidget*>{mapCoordinatesLabel, building_view_tcs,
         building_view_level1, building_view_level2, building_view_level3, flag_view_tcs,
         flag_view_level1, flag_view_level2, flag_view_level3, flag_view_level4})
        widget->area.y += height - oldHeight;
    if (auto *dialog = activeDialog()) dialog->cancelInput();
}
