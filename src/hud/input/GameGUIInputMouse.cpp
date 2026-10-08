// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <iostream>
#include <algorithm>

#include <SDL3/SDL_keycode.h>

#include <FileManager.h>
#include <Stream.h>
#include <TextStream.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUITouch.h"
#include "GameGUIDialog.h"
#include "GameGUIInternal.h"
#include "InGameTouchTheme.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"

using std::shared_ptr;
using std::static_pointer_cast;

void GameGUI::minimapMouseToPos(int mx, int my, int *cx, int *cy, bool forScreenViewport)
{
	minimap.convertToMap(mx, my, *cx, *cy);

	///when for the screen viewport, center
	if (forScreenViewport)
	{
		camera.originX=*cx*32.0-camera.visibleW()/2;
		camera.originY=*cy*32.0-camera.visibleH()/2;
		camera.normalize();
		*cx=camera.tileX();*cy=camera.tileY();
	}

}

void GameGUI::handleMouseMotion(int mx, int my, int button)
{
	const int scrollZoneWidth = 10;
	const bool edgeScroll = globalContainer->settings.edgeScrollingEnabled(
		globalContainer->gfx->getOptionFlags() & GraphicContext::FULLSCREEN);
	mouseX=mx;mouseY=my;
	updateCamera();

	int oldViewportX = viewportX;
	int oldViewportY = viewportY;

	if (miniMapPushed)
	{
		stopViewportMotion();
		minimapMouseToPos(mx, my, &viewportX, &viewportY, true);
	}
	else
	{
		if (mapPanPushed || !edgeScroll)
			viewportSpeedX=viewportSpeedY=0;
		else if (mx<scrollZoneWidth)
			viewportSpeedX=-1;
		else if ((mx>globalContainer->gfx->getW()-scrollZoneWidth) )
			viewportSpeedX=1;
		else
			viewportSpeedX=0;

		if (mapPanPushed || !edgeScroll)
			viewportSpeedY=0;
		else if (my<scrollZoneWidth)
			viewportSpeedY=-1;
		else if (my>globalContainer->gfx->getH()-scrollZoneWidth)
			viewportSpeedY=1;
		else
			viewportSpeedY=0;
	}

	if (panPushed || mapPanPushed)
	{
		// handle panning
		const int direction = mapPanPushed ? -1 : 1;
		camera.originX += direction*(mx-panMouseX)/camera.zoom;
        camera.originY += direction*(my-panMouseY)/camera.zoom;
        camera.normalize();viewportX=camera.tileX();viewportY=camera.tileY();
        panMouseX=mx;panMouseY=my;
	}

	viewportChanged(oldViewportX, viewportX, oldViewportY, viewportY);

	dragStep(mx, my, button);
}

double GameGUI::flagReachAt(double screenX, double screenY) const
{
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	const double edge = std::min({screenX, screenY, gfx->getW() - screenX, gfx->getH() - screenY}) / unit;
	const double toward = std::clamp(1 - edge / InGameTouchTheme::flagReachEdgeBand, 0.0, 1.0);
	return InGameTouchTheme::flagReach + (InGameTouchTheme::flagReachEdge - InGameTouchTheme::flagReach) * toward;
}

const SnapshotBuilding* GameGUI::flagAt(int mx, int my, double reachPoints)
{
    if (!drawnScene().world.occupancy) return nullptr;
    const auto& map = drawnScene().map;
    int mapX, mapY;
    map.displayToMapCaseAligned(mx,my,&mapX,&mapY,viewportX,viewportY);
    std::vector<BuildingRef> flags;
    {
        const auto& e=drawnScene().entities;
        for (auto gid : e.virtualBuildings[localTeamNo])
            if (const auto* b=e.building(gid.gid); b && b->identity==gid) flags.push_back(gid);
    }
    for (const auto ref : flags)
        if (auto b=inputBuilding(ref); b && displayedPosX(*b)==mapX && displayedPosY(*b)==mapY) return b;
    if (reachPoints <= 0) return {};
    const double radius=reachPoints*globalContainer->gfx->logicalUnitsPerPoint()/camera.zoom;
    double nearestDistance=radius*radius;
    const SnapshotBuilding* nearest=nullptr;
    const double worldX=mx+viewportX*32., worldY=my+viewportY*32.;
    const auto wrapped=[](double delta,double period) { return MapCamera::wrap(delta+period/2,period)-period/2; };
    for (const auto ref : flags)
    {
        auto b=inputBuilding(ref); if (!b) continue;
        const double dx=wrapped(worldX-(displayedPosX(*b)*32.+16),map.getW()*32.);
        const double dy=wrapped(worldY-(displayedPosY(*b)*32.+16),map.getH()*32.);
        const double distance=dx*dx+dy*dy;
        if (distance<nearestDistance || (distance==nearestDistance && nearest && b->gid<nearest->gid))
        { nearest=b; nearestDistance=distance; }
    }
    return nearest;
}

void GameGUI::handleMapClick(int mx, int my, int button)
{
    if (!drawnScene().map.getW()) return;
	updateCamera();
	if (!torusView.active() && (!camera.contains(mx,my) || my<16)) return;
	const double flagReach=flagReachAt(mx, my);
	if (!torusView.active()) {mx=mapMouseX(mx);my=mapMouseY(my);}
	if (selectionMode==TOOL_SELECTION)
	{
		toolManager.handleMouseDown(mx, my, localTeamNo, viewportX, viewportY);

	}
	else if (selectionMode==BRUSH_SELECTION)
	{
		toolManager.handleMouseDown(mx, my, localTeamNo, viewportX, viewportY);
	}
	else if (putMark)
	{
		int markx, marky;
		drawnScene().map.displayToMapCaseAligned(mx, my, &markx, &marky, viewportX, viewportY);
		enqueueOrder(shared_ptr<Order>(new MapMarkOrder(localTeamNo, markx, marky)));
		globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_NORMAL);
		putMark = false;
	}
	else
	{
		int mapX, mapY;
		drawnScene().map.displayToMapCaseAligned(mx, my, &mapX, &mapY, viewportX, viewportY);
		selectionPushedPosX=mapX;
		selectionPushedPosY=mapY;
		// check for flag first
		if (auto flag=flagAt(mx, my, 0))
		{
			setSelection(BUILDING_SELECTION, unsigned(flag->gid));
			selectionPushed=true;
			return;
		}
        // Selection always resolves against the displayed generation. Shift-click
        // additionally serializes that exact entity under the caller's owner guard;
        // an entity deleted since publication has nothing left to dump.
        const auto dump = [&](UnitRef unit, BuildingRef building) {
            if (!(inputState.modifiers() & SDL_KMOD_SHIFT)) return;
            Unit* liveUnit = game.resolveUnit(unit);
            Building* liveBuilding = game.resolveBuilding(building);
            if (!liveUnit && !liveBuilding) return;
            const char* filename = liveUnit ? "unit.dump.txt" : "building.dump.txt";
            TextOutputStream stream(Toolkit::getFileManager()->openOutputStreamBackend(filename));
            if (stream.isEndOfStream()) {
                std::cerr << "Can't dump entity to file " << filename << std::endl;
                return;
            }
            if (liveUnit) {
                liveUnit->save(&stream);
                liveUnit->saveCrossRef(&stream);
                liveBuilding = liveUnit->attachedBuilding;
            }
            if (liveBuilding) {
                liveBuilding->save(&stream);
                liveBuilding->saveCrossRef(&stream);
            }
        };
        const auto& scene=drawnScene();
        const auto* unit=scene.entities.unit(view.mouseUnit);
        const auto gid=scene.map.getBuilding(mapX,mapY);
        if (touch->usesHUD() && !torusView.active() && !unit && gid==NOGBID)
            if (auto nearest=flagAt(mx,my,flagReach))
            { setSelection(BUILDING_SELECTION,unsigned(nearest->gid)); selectionPushed=false; return; }
        if (unit)
        {
            setSelection(UNIT_SELECTION,unsigned(unit->gid)); selectionPushed=true;
            dump(unit->identity,{});
            return;
        }
        const auto me=Team::teamNumberToMask(localTeamNo);
        if (const auto* building=scene.entities.building(gid))
        {
            const int team=building->team;
            if (team==localTeamNo || scene.map.isFOWDiscovered(mapX,mapY,me) ||
                (scene.map.isMapDiscovered(mapX,mapY,me) && (scene.entities.teams[team].allies&me)) ||
                globalContainer->isViewingGame())
            {
                setSelection(BUILDING_SELECTION,unsigned(gid)); selectionPushed=true;
                dump({},building->identity);
            }
        }
        else if (scene.map.getResource(mapX,mapY).type!=NO_RES_TYPE && scene.map.isMapDiscovered(mapX,mapY,me))
        { setSelection(RESOURCE_SELECTION,unsigned(scene.map.coordToIndex(mapX,mapY))); selectionPushed=true; }
        else if (selectionMode==RESOURCE_SELECTION) clearSelection();
	}
}

void GameGUI::handleReplayProgressBarClick(int mx, int my, int button)
{
	// Check the play, pause and fast-forward buttons
	if (globalContainer->replaying)
	{
		int x = REPLAY_BAR_WIDTH - REPLAY_PROGRESS_BAR_X_OFFSET - REPLAY_PROGRESS_BAR_CAP_WIDTH;
		int y = REPLAY_BAR_Y + REPLAY_PROGRESS_BAR_Y_OFFSET;
		int inc = REPLAY_PROGRESS_BAR_BUTTON_WIDTH;

		if (my >= y && my <= y+20)
		{
			if (mx >= x-3*inc && mx <= x-2*inc)
			{
				// Play
				gamePaused = false;
				globalContainer->replayFastForward = false;
			}
			if (mx > x-2*inc && mx <= x-inc)
			{
				// Pause
				gamePaused = true;
			}
			if (mx > x-inc && mx <= x)
			{
				// Fast-forward
				gamePaused = false;
				globalContainer->replayFastForward = true;
			}
		}
	}
}
