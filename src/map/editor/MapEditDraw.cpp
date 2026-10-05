#include "MapZoomControls.h"
#include "DynamicClouds.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2022-2023 Nathan Mills
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include "../../render/MapCopies.h"
#include <FormatableString.h>
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "ScriptEditorScreen.h"
#include "Unit.h"
#include "UnitType.h"
#include "Utilities.h"
#include <SDL3/SDL.h>
#include <algorithm>

void MapEdit::draw(Uint64 frameTick)
{
	drawMap(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());

	drawMenu();
	drawMiniMap();
	wasMinimapRendered=false;
	drawWidgets();
	if (auto *dialog = activeDialog())
		dialog->update(Uint32(frameTick));
	drawDialog();
}

void MapEdit::drawMap(int sx, int sy, int sw, int sh)
{
	updateCamera();
	view.render.minimumZoom = camera.minimumZoom();
	globalContainer->gfx->setClipRect();
	globalContainer->gfx->drawFilledRect(0,0,globalContainer->gfx->getW(),globalContainer->gfx->getH(),0,0,32);
	globalContainer->gfx->beginMapTransform(camera.zoom, camera.offsetX-camera.fractionX()*camera.zoom, camera.offsetY-camera.fractionY()*camera.zoom, camera.offsetX, std::max(16, int(camera.offsetY)), camera.visibleW()*camera.zoom, camera.visibleH()*camera.zoom-std::max(0,16-int(camera.offsetY)));

	Uint32 drawOptions = Game::DRAW_WHOLE_MAP | Game::DRAW_BUILDING_RECT | Game::DRAW_AREA | Game::DRAW_HEALTH_FOOD_BAR | Game::DRAW_SCRIPT_AREAS | Game::DRAW_NO_RESOURCE_GROWTH_AREAS;
	if(isFertilityOn)
	{
		drawOptions |= Game::DRAW_OVERLAY;
	}

	game.drawMap(0, 0, int(std::ceil(camera.visibleW()+camera.fractionX())), int(std::ceil(camera.visibleH()+camera.fractionY())), 0, 0, viewportX, viewportY, team, view, drawOptions, nullptr, nullptr, false,
        DynamicClouds::gridLimitForZoom(game.map.getW(), game.map.getH(),
            globalContainer->settings.cloudPatchSize, camera.zoom));

	if(selectionMode==EditingBuilding && camera.contains(mouseX,mouseY) && mouseY>=16)
	{
		Building* selBuild=game.teams[Building::GIDtoTeam(selectedBuildingGID)]->myBuildings[Building::GIDtoID(selectedBuildingGID)];
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH());
		int centerX, centerY;
		// Map editor mutates buildings directly — no orderQueue, no pending shadow.
		// Use the authoritative position straight from the Building.
		game.map.buildingPosToCursor(selBuild->posX, selBuild->posY,  selBuild->type->width, selBuild->type->height, &centerX, &centerY, viewportX, viewportY);
		const int radius = selBuild->type->width*16;
		forEachMapCopy(centerX-radius, centerY-radius, centerX+radius, centerY+radius,
			game.map.getW()*32, game.map.getH()*32, game.map.displayViewportW,
			game.map.displayViewportH, [&](int dx, int dy) {
			if (selBuild->owner->teamNumber==team)
				globalContainer->gfx->drawCircle(centerX+dx, centerY+dy, selBuild->type->width*16, 0, 0, 190);
			else if ((game.teams[team]->allies) & (selBuild->owner->me))
				globalContainer->gfx->drawCircle(centerX+dx, centerY+dy, selBuild->type->width*16, 255, 196, 0);
			else if (!selBuild->type->isVirtual)
				globalContainer->gfx->drawCircle(centerX+dx, centerY+dy, selBuild->type->width*16, 190, 0, 0);
		});
		globalContainer->gfx->setClipRect();
	}

globalContainer->gfx->drawMapCopies(game.map.getW()*32,game.map.getH()*32,game.map.displayViewportW,game.map.displayViewportH,[&](){
	if(camera.contains(mouseX,mouseY) && mouseY>=16)
	{
		// BrushTool treats -1 as "no stroke origin" for checkerboard parity alignment
		const int firstX = firstPlacement ? firstPlacement->x : -1;
		const int firstY = firstPlacement ? firstPlacement->y : -1;
		if(selectionMode==PlaceBuilding)
			drawBuildingSelectionOnMap();
		if(selectionMode==PlaceZone)
			brush.drawBrush(int(MapCamera::wrap(mapMouseX(mouseX),game.map.getW()*32)), int(MapCamera::wrap(mapMouseY(mouseY),game.map.getH()*32)), viewportX, viewportY, firstX, firstY);
		if(selectionMode==PlaceTerrain)
			brush.drawBrush(int(MapCamera::wrap(mapMouseX(mouseX),game.map.getW()*32)), int(MapCamera::wrap(mapMouseY(mouseY),game.map.getH()*32)), viewportX, viewportY, firstX, firstY, (terrainType>TerrainSelector::Water ? 0 : 1));
		if(selectionMode==PlaceUnit)
			drawPlacingUnitOnMap();
		if(selectionMode==RemoveObject)
			brush.drawBrush(int(MapCamera::wrap(mapMouseX(mouseX),game.map.getW()*32)), int(MapCamera::wrap(mapMouseY(mouseY),game.map.getH()*32)), viewportX, viewportY, firstX, firstY);

		if(selectionMode==ChangeAreas)
		{
			brush.drawBrush(int(MapCamera::wrap(mapMouseX(mouseX),game.map.getW()*32)), int(MapCamera::wrap(mapMouseY(mouseY),game.map.getH()*32)), viewportX, viewportY, firstX, firstY);
		}
		if(selectionMode==ChangeNoResourceGrowthAreas)
			brush.drawBrush(int(MapCamera::wrap(mapMouseX(mouseX),game.map.getW()*32)), int(MapCamera::wrap(mapMouseY(mouseY),game.map.getH()*32)), viewportX, viewportY, firstX, firstY);
	}

});
	globalContainer->gfx->endMapTransform();
	drawMapZoomControls(camera);
	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());
}



void MapEdit::drawMiniMap(void)
{
	minimap.draw(view.drawnScene(), team, viewportX, viewportY, int(std::ceil(camera.visibleW()/32)), int(std::ceil(camera.visibleH()/32)) );
}



void MapEdit::drawMenu(void)
{
	int menuStartW=globalContainer->gfx->getW()-menuWidth();
	int yposition=133;

	if (!globalContainer->settings.translucentPanels)
		globalContainer->gfx->drawFilledRect(menuStartW, yposition, menuWidth(), globalContainer->gfx->getH()-128, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(menuStartW, yposition, menuWidth(), globalContainer->gfx->getH()-128, 0, 0, 40, 180);

	drawMenuEyeCandy();
}



void MapEdit::drawBuildingSelectionOnMap()
{
	if (selectionName!="")
	{
		// we get the type of building
		int typeNum=buildingSelectionType(selectionName);
		if (typeNum<0) return;
		BuildingType *bt = game.buildingsTypes.get(typeNum);
		Sprite *sprite = bt->gameSpritePtr;

		// we translate dimensions and situation
		int tempX, tempY;
		int mapX, mapY;
		bool isRoom;
		game.map.cursorToBuildingPos(mapMouseX(mouseX), mapMouseY(mouseY), bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);
		if (bt->isVirtual)
			isRoom = game.checkRoomForBuilding(tempX, tempY, bt, &mapX, &mapY, team);
		else
			isRoom = game.checkHardRoomForBuilding(tempX, tempY, bt, &mapX, &mapY);

		// we get the screen dimensions of the building
		int rectW = (bt->width)<<5;
		int rectH = sprite->getH(bt->gameSpriteImage);
		int rectX = (((mapX-viewportX)&(game.map.wMask))<<5);
		int rectY = (((mapY-viewportY)&(game.map.hMask))<<5)-(rectH-(bt->height<<5));

		// we draw the building
		sprite->setBaseColor(game.teams[team]->color);
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-menuWidth(), globalContainer->gfx->getH());
		int spriteIntensity = 127;
		globalContainer->gfx->drawSprite(rectX, rectY, sprite, bt->gameSpriteImage, spriteIntensity);

		if (!bt->isVirtual)
		{
			if (game.teams[team]->noMoreBuildingSitesCountdown>0)
			{
				globalContainer->gfx->drawRect(rectX, rectY, rectW, rectH, 255, 0, 0, 127);
				globalContainer->gfx->drawLine(rectX, rectY, rectX+rectW-1, rectY+rectH-1, 255, 0, 0, 127);
				globalContainer->gfx->drawLine(rectX+rectW-1, rectY, rectX, rectY+rectH-1, 255, 0, 0, 127);

				globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 0, 0, 127));
				globalContainer->gfx->drawString(rectX, rectY-12, globalContainer->littleFont, FormattableString("%0.%1").arg(game.teams[team]->noMoreBuildingSitesCountdown/40).arg((game.teams[team]->noMoreBuildingSitesCountdown%40)/4).c_str());
				globalContainer->littleFont->popStyle();
			}
			else
			{
				if (isRoom)
					globalContainer->gfx->drawRect(rectX, rectY, rectW, rectH, 255, 255, 255, 127);
				else
					globalContainer->gfx->drawRect(rectX, rectY, rectW, rectH, 255, 0, 0, 127);

				BuildingType *upgradedType=game.buildingsTypes.getLastLevel(typeNum);
				int upgradedMapX, upgradedMapY;
				bool isUpgradedRoom = game.checkHardRoomForBuilding(tempX, tempY, upgradedType, &upgradedMapX, &upgradedMapY);
				int upgradedRectX=((upgradedMapX-viewportX)&(game.map.wMask))<<5;
				int upgradedRectY=((upgradedMapY-viewportY)&(game.map.hMask))<<5;
				int upgradedRectW=(upgradedType->width)<<5;
				int upgradedRectH=(upgradedType->height)<<5;

				if (isRoom && isUpgradedRoom)
					globalContainer->gfx->drawRect(upgradedRectX-1, upgradedRectY-1, upgradedRectW+2, upgradedRectH+2, 255, 255, 255, 127);
				else
					globalContainer->gfx->drawRect(upgradedRectX-1, upgradedRectY-1, upgradedRectW+2, upgradedRectH+2, 255, 0, 0, 127);
			}
		}

	}

}



int MapEdit::buildingSelectionType(const std::string& key)
{
    int id=game.buildingsTypes.getFinishedTypeNum(key);
    if (!game.isBuildingTypeAvailable(id)) return -1;
    for (int depth=0; depth<buildingLevel; ++depth)
    {
        const auto* current=game.buildingsTypes.get(id);
        int next=current->nextLevel;
        if (!game.isBuildingTypeAvailable(next)) break;
        while (game.buildingsTypes.get(next)->isBuildingSite)
        {
            next=game.buildingsTypes.get(next)->nextLevel;
            if (!game.isBuildingTypeAvailable(next)) return id;
        }
        id=next;
    }
    return id;
}

void MapEdit::layoutBuildingSelectors()
{
    if (buildingLevelNextPage) buildingLevelNextPage->enabled=panelMode==AddBuildings && buildingLevelCount>3;
    auto layout=[&](auto& list,int& firstRow,int columns,int rows,int top,int pitch,bool active) {
        const int totalRows=(static_cast<int>(list.size())+columns-1)/columns;
        firstRow=std::clamp(firstRow,0,std::max(0,totalRows-rows));
        for (size_t index=0; index<list.size(); ++index)
        {
            const int row=static_cast<int>(index)/columns-firstRow;
            list[index]->area.y=top+row*pitch;
            list[index]->enabled=active && row>=0 && row<rows;
        }
    };
    const int rows=std::max(1,(globalContainer->gfx->getH()-42-TeamColorSelector::HEIGHT-166)/46);
    layout(buildingSelectors,buildingSelectorRow,2,rows,166,46,panelMode==AddBuildings);
    layout(flagSelectors,flagSelectorRow,3,1,167,40,panelMode==AddFlagsAndZones);
}

bool MapEdit::scrollBuildingSelectors(double delta)
{
    if (mouseX<globalContainer->gfx->getW()-menuWidth() || mouseY<166) return false;
    if (panelMode==BuildingEditor && mouseY>=252)
    {
        buildingEditFirstRow+=delta>0 ? -1 : delta<0 ? 1 : 0;
        layoutBuildingEditRows(); return true;
    }
    if (panelMode==AddBuildings) buildingSelectorRow+=delta>0 ? -1 : delta<0 ? 1 : 0;
    else if (panelMode==AddFlagsAndZones && mouseY<207) flagSelectorRow+=delta>0 ? -1 : delta<0 ? 1 : 0;
    else return false;
    layoutBuildingSelectors();
    return true;
}

void MapEdit::rebuildBuildingSelectors()
{
    for (auto* list : {&buildingSelectors,&flagSelectors})
    {
        for (auto* widget : *list)
        {
            mew.erase(std::remove(mew.begin(),mew.end(),widget),mew.end());
            delete widget;
        }
        list->clear();
    }
    buildingLevelCount=1;
    const int left=globalContainer->gfx->getW()-RIGHT_MENU_WIDTH;
    for (size_t id=0; id<game.buildingsTypes.size(); ++id)
    {
        const auto* type=game.buildingsTypes.get(id);
        if (!type->runtimeAvailable || !type->semantics.placeable) continue;
        int depth=0;
        int variant=game.buildingsTypes.getFinishedTypeNum(type->key);
        while (game.isBuildingTypeAvailable(variant))
        {
            const auto* stage=game.buildingsTypes.get(variant);
            if (!stage->isBuildingSite) ++depth;
            variant=stage->nextLevel;
        }
        buildingLevelCount=std::max(buildingLevelCount,depth);
        const bool overlay=!type->semantics.occupiesGround;
        auto& list=overlay ? flagSelectors : buildingSelectors;
        const int index=static_cast<int>(list.size());
        const int x=left+(overlay ? 5+42*(index%3) : 12+64*(index%2));
        const int y=overlay ? 167+40*(index/3) : 166+46*(index/2);
        auto* widget=new BuildingSelectorWidget(*this,widgetRectangle(x,y,overlay?32:40,overlay?32:40),
            overlay ? "flag view" : "building view",type->key,"set place building selection "+type->key,type->key,!overlay);
        list.push_back(widget);
        addWidget(widget);
    }
}




void MapEdit::drawMenuEyeCandy()
{
	globalContainer->gfx->endMapTransform();
	drawMapZoomControls(camera);
	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());

	// bar background
	if (!globalContainer->settings.translucentPanels)
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-menuWidth(), 16, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-menuWidth(), 16, 0, 0, 40, 180);

	// draw window bar
	int pos=globalContainer->gfx->getW()-menuWidth()-32;
	for (int i=0; i<=pos; i+=32)
	{
		globalContainer->gfx->drawSprite(i, 16, globalContainer->gamegui, 16);
	}
	for (int i=16; i<globalContainer->gfx->getH(); i+=32)
	{
		globalContainer->gfx->drawSprite(pos+28, i, globalContainer->gamegui, 17);
	}
}



void MapEdit::drawPlacingUnitOnMap()
{
	int type=0;
	if(placingUnit==Worker)
		type=WORKER;
	else if(placingUnit==Warrior)
		type=WARRIOR;
	else if(placingUnit==Explorer)
		type=EXPLORER;

	int level=placingUnitLevel;

	int cx=((mapMouseX(mouseX)>>5)+viewportX)&game.map.getMaskW();
	int cy=((mapMouseY(mouseY)>>5)+viewportY)&game.map.getMaskH();

	int px=int(MapCamera::wrap(mapMouseX(mouseX),game.map.getW()*32))&0xFFFFFFE0;
	int py=int(MapCamera::wrap(mapMouseY(mouseY),game.map.getH()*32))&0xFFFFFFE0;
	int pw=32;
	int ph=32;

	bool isRoom;
	if (type==EXPLORER)
		isRoom=game.map.isFreeForAirUnit(cx, cy);
	else
	{
		UnitType *ut=game.teams[team]->race.getUnitType(type, level);
		isRoom=game.map.isFreeForGroundUnit(cx, cy, ut->performance[SWIM], Team::teamNumberToMask(team));
	}

	int imgid;
	if (type==WORKER)
		imgid=64;
	else if (type==EXPLORER)
		imgid=0;
	else if (type==WARRIOR)
		imgid=256;
	else
	{
		imgid=0;
		assert(false);
	}

	Sprite *unitSprite=globalContainer->units;
	unitSprite->setBaseColor(game.teams[team]->color);

	globalContainer->gfx->drawSprite(px, py, unitSprite, imgid);

	if (isRoom)
		globalContainer->gfx->drawRect(px, py, pw, ph, 255, 255, 255, 128);
	else
		globalContainer->gfx->drawRect(px, py, pw, ph, 255, 0, 0, 128);
}
