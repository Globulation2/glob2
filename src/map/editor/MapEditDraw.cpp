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
#include "render/scene/SceneExtract.h"
#include "render/scene/BuildingCatalogView.h"
#include "EditorDock.h"
#include "ScriptEditorScreen.h"
#include "Unit.h"
#include "render/UnitAnimation.h"
#include "render/UnitSkin.h"
#include "UnitType.h"
#include "Utilities.h"
#include <SDL3/SDL.h>
#include <algorithm>

void MapEdit::draw(Uint64 frameTick)
{
	drawMap(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());

	if (!dock)
	{
		drawMenu();
		drawMiniMap();
	}
	else
		drawMenuEyeCandy();
	wasMinimapRendered=false;
	if (dock)
		drawDock(Uint32(frameTick));
	else
		drawWidgets();
	if (auto *dialog = activeDialog())
		dialog->update(Uint32(frameTick));
	drawDialog();
}

void MapEdit::preparePresentation()
{
    SceneRequest request;
    request.localTeam=team;request.includePanels=false;request.includeScriptAreas=true;
    request.view.overlay=isFertilityOn ? OverlayArea::Fertility : OverlayArea::None;
    request.selectedBuilding=Game::refOf(view.selectedBuilding);
    request.selectedUnit=Game::refOf(view.selectedUnit);
    request.view.displayW=int(camera.visibleW());request.view.displayH=int(camera.visibleH());
    SceneExtractor().prepare(game.captureReadBoundary({},true,SceneExtractor::requirements(request)),request,view.render.ownScene);
    view.scene=&view.render.ownScene;
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

    preparePresentation();
	game.drawMap(0, 0, int(std::ceil(camera.visibleW()+camera.fractionX())), int(std::ceil(camera.visibleH()+camera.fractionY())), 0, 0, viewportX, viewportY, team, view, drawOptions, nullptr, nullptr, false,
        DynamicClouds::gridLimitForZoom(view.scene->map.getW(), view.scene->map.getH(),
            globalContainer->settings.cloudPatchSize, camera.zoom));

	// Previews follow the pointer only over the map itself, never under the
	// dock or an open dialog.
	const bool pointerOnMap = camera.contains(mouseX,mouseY) && mouseY>=16 && !pointerOverInterface();
	if(selectionMode==EditingBuilding && pointerOnMap)
	{
		const auto& frame=*view.scene;
        const auto* selBuild=frame.entities.building(selectedBuildingGID);
        const auto* type=selBuild ? frame.entities.type(*selBuild) : nullptr;
        if (selBuild && type) {
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-dockWidth(), globalContainer->gfx->getH());
		int centerX, centerY;
        // Selection decoration uses the exact world already drawn.
		frame.map.buildingPosToCursor(selBuild->posX, selBuild->posY,  type->width, type->height, &centerX, &centerY, viewportX, viewportY);
		const int radius = type->width*16;
		forEachMapCopy(centerX-radius, centerY-radius, centerX+radius, centerY+radius,
			frame.map.getW()*32, frame.map.getH()*32, frame.map.viewportWidth(),
			frame.map.viewportHeight(), [&](int dx, int dy) {
			if (selBuild->team==team)
				globalContainer->gfx->drawCircle(centerX+dx, centerY+dy, type->width*16, 0, 0, 190);
			else if ((frame.entities.teams[team].allies) & (frame.entities.teams[selBuild->team].mask))
				globalContainer->gfx->drawCircle(centerX+dx, centerY+dy, type->width*16, 255, 196, 0);
			else if (!type->isVirtual)
				globalContainer->gfx->drawCircle(centerX+dx, centerY+dy, type->width*16, 190, 0, 0);
		});
		globalContainer->gfx->setClipRect();
        }
	}

globalContainer->gfx->drawMapCopies(view.scene->map.getW()*32,view.scene->map.getH()*32,view.scene->map.viewportWidth(),view.scene->map.viewportHeight(),[&](){
	if(pointerOnMap)
	{
		// BrushTool treats -1 as "no stroke origin" for checkerboard parity alignment
		const int firstX = firstPlacement ? firstPlacement->x : -1;
		const int firstY = firstPlacement ? firstPlacement->y : -1;
		if(selectionMode==PlaceBuilding)
			drawBuildingSelectionOnMap();
		if(selectionMode==PlaceZone)
			brush.drawBrush(int(MapCamera::wrap(mapMouseX(mouseX),view.scene->map.getW()*32)), int(MapCamera::wrap(mapMouseY(mouseY),view.scene->map.getH()*32)), viewportX, viewportY, firstX, firstY);
		if(selectionMode==PlaceTerrain)
			drawTerrainBrushPreview();
		if(selectionMode==PlaceUnit)
			drawPlacingUnitOnMap();
		if(selectionMode==RemoveObject)
			brush.drawBrush(int(MapCamera::wrap(mapMouseX(mouseX),view.scene->map.getW()*32)), int(MapCamera::wrap(mapMouseY(mouseY),view.scene->map.getH()*32)), viewportX, viewportY, firstX, firstY);

		if(selectionMode==ChangeAreas)
		{
			brush.drawBrush(int(MapCamera::wrap(mapMouseX(mouseX),view.scene->map.getW()*32)), int(MapCamera::wrap(mapMouseY(mouseY),view.scene->map.getH()*32)), viewportX, viewportY, firstX, firstY);
		}
		if(selectionMode==ChangeNoResourceGrowthAreas)
			brush.drawBrush(int(MapCamera::wrap(mapMouseX(mouseX),view.scene->map.getW()*32)), int(MapCamera::wrap(mapMouseY(mouseY),view.scene->map.getH()*32)), viewportX, viewportY, firstX, firstY);
	}

});
	globalContainer->gfx->endMapTransform();
	drawMapZoomControls(camera);
	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());
	drawStatus();
}



void MapEdit::drawMiniMap(void)
{
	minimap.draw(view.drawnScene(), team, viewportX, viewportY, int(std::ceil(camera.visibleW()/32)), int(std::ceil(camera.visibleH()/32)) );
}



void MapEdit::drawMenu(void)
{
	int menuStartW=globalContainer->gfx->getW()-dockWidth();
	int yposition=133;

	if (!globalContainer->settings.translucentPanels)
		globalContainer->gfx->drawFilledRect(menuStartW, yposition, dockWidth(), globalContainer->gfx->getH()-128, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(menuStartW, yposition, dockWidth(), globalContainer->gfx->getH()-128, 0, 0, 40, 180);

	drawMenuEyeCandy();
}



void MapEdit::drawBuildingSelectionOnMap()
{
	if (selectionName!="")
	{
		// we get the type of building
		int typeNum=displayedBuildingSelectionType(selectionName);
		if (typeNum<0) return;
		const auto& frame=*view.scene;
        const auto& map=frame.map;
        const auto& owner=frame.entities.teams[team];
        const BuildingCatalogView catalog(*frame.buildingTypes);
        const auto* bt=catalog.get(typeNum);
		Sprite *sprite = bt->gameSpritePtr;

		// we translate dimensions and situation
		int tempX, tempY;
		int mapX, mapY;
		bool isRoom;
		map.cursorToBuildingPos(mapMouseX(mouseX), mapMouseY(mouseY), bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);
        mapX=tempX+bt->decLeft;mapY=tempY+bt->decTop;
        isRoom=bt->isVirtual || map.isHardSpaceForBuilding(mapX,mapY,bt->width,bt->height);
        if (bt->isVirtual) for (const auto ref:owner.virtualBuildings) {
            const auto* other=frame.entities.building(ref);
            if (other && other->posX==(mapX&map.getMaskW()) && other->posY==(mapY&map.getMaskH())) isRoom=false;
        }

		// we get the screen dimensions of the building
		int rectW = (bt->width)<<5;
		int rectH = sprite->getH(bt->gameSpriteImage);
		int rectX = (((mapX-viewportX)&(map.getMaskW()))<<5);
		int rectY = (((mapY-viewportY)&(map.getMaskH()))<<5)-(rectH-(bt->height<<5));

		// we draw the building
		sprite->setBaseColor(presentationColor(owner.color));
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-dockWidth(), globalContainer->gfx->getH());
		int spriteIntensity = 127;
		globalContainer->gfx->drawSprite(rectX, rectY, sprite, bt->gameSpriteImage, spriteIntensity);

		if (!bt->isVirtual)
		{
			if (owner.noMoreBuildingSitesCountdown>0)
			{
				globalContainer->gfx->drawRect(rectX, rectY, rectW, rectH, 255, 0, 0, 127);
				globalContainer->gfx->drawLine(rectX, rectY, rectX+rectW-1, rectY+rectH-1, 255, 0, 0, 127);
				globalContainer->gfx->drawLine(rectX+rectW-1, rectY, rectX, rectY+rectH-1, 255, 0, 0, 127);

				globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 0, 0, 127));
				globalContainer->gfx->drawString(rectX, rectY-12, globalContainer->littleFont, FormattableString("%0.%1").arg(owner.noMoreBuildingSitesCountdown/40).arg((owner.noMoreBuildingSitesCountdown%40)/4).c_str());
				globalContainer->littleFont->popStyle();
			}
			else
			{
				if (isRoom)
					globalContainer->gfx->drawRect(rectX, rectY, rectW, rectH, 255, 255, 255, 127);
				else
					globalContainer->gfx->drawRect(rectX, rectY, rectW, rectH, 255, 0, 0, 127);

				const auto* upgradedType=catalog.getLastLevel(typeNum);
				int upgradedMapX=tempX+upgradedType->decLeft, upgradedMapY=tempY+upgradedType->decTop;
				bool isUpgradedRoom = map.isHardSpaceForBuilding(upgradedMapX,upgradedMapY,upgradedType->width,upgradedType->height);
				int upgradedRectX=((upgradedMapX-viewportX)&(map.getMaskW()))<<5;
				int upgradedRectY=((upgradedMapY-viewportY)&(map.getMaskH()))<<5;
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

int MapEdit::displayedBuildingSelectionType(const std::string& key) const
{
    if (!view.scene) return -1;
    const BuildingCatalogView catalog(*view.scene->buildingTypes);
    const auto available=[&](int id) { return id>=0 && size_t(id)<view.scene->world.catalogs->buildings->size()
        && view.scene->world.catalogs->buildings->at(id).available; };
    int id=catalog.getFinishedTypeNum(key);
    if (!available(id)) return -1;
    for (int depth=0; depth<buildingLevel; ++depth)
    {
        const auto* current=catalog.get(id);
        int next=current->nextLevel;
        if (!available(next)) break;
        while (catalog.get(next)->isBuildingSite)
        {
            next=catalog.get(next)->nextLevel;
            if (!available(next)) return id;
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
    if (mouseX<globalContainer->gfx->getW()-dockWidth() || mouseY<166) return false;
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
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-dockWidth(), 16, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-dockWidth(), 16, 0, 0, 40, 180);

	// draw window bar
	int pos=globalContainer->gfx->getW()-dockWidth()-32;
	for (int i=0; i<=pos; i+=32)
	{
		globalContainer->gfx->drawSprite(i, 16, globalContainer->gamegui, 16);
	}
	if (!dock)
		for (int i=16; i<globalContainer->gfx->getH(); i+=32)
		{
			globalContainer->gfx->drawSprite(pos+28, i, globalContainer->gamegui, 17);
		}
}



void MapEdit::drawPlacingUnitOnMap()
{
	const auto& frame=*view.scene;
    const auto& map=frame.map;
    int type=0;
	if(placingUnit==Worker)
		type=WORKER;
	else if(placingUnit==Warrior)
		type=WARRIOR;
	else if(placingUnit==Explorer)
		type=EXPLORER;

	int level=placingUnitLevel;

	int cx=((mapMouseX(mouseX)>>5)+viewportX)&map.getMaskW();
	int cy=((mapMouseY(mouseY)>>5)+viewportY)&map.getMaskH();

	int px=int(MapCamera::wrap(mapMouseX(mouseX),map.getW()*32))&0xFFFFFFE0;
	int py=int(MapCamera::wrap(mapMouseY(mouseY),map.getH()*32))&0xFFFFFFE0;
	int pw=32;
	int ph=32;

	bool isRoom;
	if (type==EXPLORER)
		isRoom=map.isFreeForAirUnit(cx, cy);
	else
	{
		const auto* ut=&frame.world.catalogs->unitTypes[type][level];
		isRoom=map.isFreeForGroundUnit(cx, cy, ut->performance[SWIM], Team::teamNumberToMask(team));
	}

	const int imgid=unitAnimationFrame(g_unitSkins[type].startImage[STOP_WALK], 0, 0);

	Sprite *unitSprite=globalContainer->units;
	unitSprite->setBaseColor(presentationColor(frame.entities.teams[team].color));

	globalContainer->gfx->drawSprite(px, py, unitSprite, imgid);

	if (isRoom)
		globalContainer->gfx->drawRect(px, py, pw, ph, 255, 255, 255, 128);
	else
		globalContainer->gfx->drawRect(px, py, pw, ph, 255, 0, 0, 128);
}


void MapEdit::drawDock(Uint32 tick)
{
	if (!dock || globalContainer->runNoX)
		return;
	dock->update(tick);
	globalContainer->gfx->setClipRect();
	dock->draw(tick);
}

void MapEdit::centerViewOnMinimap(int x, int y)
{
	if (globalContainer->runNoX || !minimap.insideMinimap(x, y))
		return;
	int cellX = 0, cellY = 0;
	minimap.convertToMap(x, y, cellX, cellY);
	updateCamera();
	viewportX = (cellX - int(camera.visibleW() / 64)) & game.map.getMaskW();
	viewportY = (cellY - int(camera.visibleH() / 64)) & game.map.getMaskH();
	updateCamera();
}
