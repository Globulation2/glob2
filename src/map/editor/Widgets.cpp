// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2022-2023 Nathan Mills
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "ScriptEditorScreen.h"
#include "Unit.h"
#include "Utilities.h"
#include <SDL3/SDL.h>

MapEditorWidget::MapEditorWidget(MapEdit& me, const widgetRectangle& rectangle, const std::string& group, const std::string& name, const std::string& action)
	: me(me), area(rectangle, globalContainer->gfx->getW()), group(group), name(name), action(action), enabled(false)
{

}



void MapEditorWidget::drawSelf()
{
	area.updateWindowWidth(globalContainer->gfx->getW());
	if(enabled)
		draw();
}



bool MapEditorWidget::is_in(int x, int y)
{
	area.updateWindowWidth(globalContainer->gfx->getW());
	return area.is_in(x, y);
}


void MapEditorWidget::disable()
{
	enabled=false;
}



void MapEditorWidget::enable()
{
	enabled=true;
}



void MapEditorWidget::activate()
{
    me.performAction(action);
}

void MapEditorWidget::handleClick(int relMouseX, int relMouseY)
{
	me.performAction(action, relMouseX, relMouseY);
}



BuildingSelectorWidget::BuildingSelectorWidget(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, const std::string& building_type, bool largeSelector) : MapEditorWidget(me, area, group, name, action), building_type(building_type), largeSelector(largeSelector)
{

}



void BuildingSelectorWidget::draw()
{
	std::string &type = building_type;

	const int id=me.displayedBuildingSelectionType(type);
	if (id<0) return;
	const auto* bt=&me.view.scene->buildingTypes->at(id);
	if (!bt) return;

	int imgid = bt->miniSpriteImage;
	int x, y;

	x=area.x;
	y=area.y;

	Sprite *buildingSprite;
	if (imgid >= 0)
	{
		buildingSprite = bt->miniSpritePtr;
	}
	else
	{
		buildingSprite = bt->gameSpritePtr;
		imgid = bt->gameSpriteImage;
	}
		
	buildingSprite->setBaseColor(presentationColor(me.view.scene->entities.teams[me.team].color));
	globalContainer->gfx->drawSprite(x, y, buildingSprite, imgid);

	// draw selection if needed
	if (me.selectionName == type)
	{
		if (largeSelector)
			globalContainer->gfx->drawSprite(x-8, y-5, globalContainer->gamegui, 8);
		else
			globalContainer->gfx->drawSprite(x-4, y-3, globalContainer->gamegui, 23);
	}
	globalContainer->gfx->finishDrawingSprite(buildingSprite, 255);
	globalContainer->gfx->finishDrawingSprite(globalContainer->gamegui, 255);
}



TeamColorSelector::TeamColorSelector(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action)
	: MapEditorWidget(me, area, group, name, action)
{

}



void TeamColorSelector::draw()
{
    for (const auto& team:me.view.scene->entities.teams) {
        const int x=area.x+(team.number%COLUMNS)*SWATCH_SIZE;
        const int y=area.y+(team.number/COLUMNS)*SWATCH_SIZE;
        auto color=presentationColor(team.color);
        if (me.team==team.number) color.a=128;
        globalContainer->gfx->drawFilledRect(x,y,SWATCH_SIZE,SWATCH_SIZE,color);
    }
}

SingleLevelSelector::SingleLevelSelector(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, int level, int& levelNum, bool catalogPages)
	: MapEditorWidget(me, area, group, name, action), level(level), levelNum(levelNum), catalogPages(catalogPages)
{

}



void SingleLevelSelector::draw()
{
	const int value=level+(catalogPages ? (levelNum/3)*3 : 0);
	if (catalogPages && value>me.buildingLevelCount) return;
	if (value<=4)
		globalContainer->gfx->drawSprite(area.x,area.y,me.menu,30+value-1,(value-1)==levelNum ? 128 : 255);
	else
	{
		globalContainer->gfx->drawRect(area.x,area.y,32,32,128,128,128);
		globalContainer->gfx->drawString(area.x+4,area.y+8,globalContainer->littleFont,std::to_string(value));
	}
}

void SingleLevelSelector::handleClick(int x,int y)
{
	if (!catalogPages) { MapEditorWidget::handleClick(x,y); return; }
	const int value=level+(levelNum/3)*3;
	if (value<=me.buildingLevelCount)
		me.performAction("switch to building level "+std::to_string(value));
}



PanelIcon::PanelIcon(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, int iconNumber, int panelModeHighlight)
	: MapEditorWidget(me, area, group, name, action), iconNumber(iconNumber), panelModeHighlight(panelModeHighlight)
{

}



void PanelIcon::draw()
{
	// draw buttons
	if (me.panelMode==panelModeHighlight)
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, iconNumber+1);
	else
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, iconNumber);

}



MenuIcon::MenuIcon(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action)
	: MapEditorWidget(me, area, group, name, action)
{

}



void MenuIcon::draw()
{
	// draw buttons
	if (me.showingMenuScreen)
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 7);
	else
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 6);

}



ZoneSelector::ZoneSelector(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, ZoneType zoneType)
	: MapEditorWidget(me, area, group, name, action), zoneType(zoneType)
{
	
}



void ZoneSelector::draw()
{
	bool isSelected=false;
	if(zoneType==ForbiddenZone)
	{
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 13);
		if(me.brushType==MapEdit::ForbiddenBrush)
			isSelected=true;
	}
	else if(zoneType==GuardingZone)
	{
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 14);
		if(me.brushType==MapEdit::GuardAreaBrush)
			isSelected=true;
	}
	else if(zoneType==ClearingZone)
	{
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 25);
		if(me.brushType==MapEdit::ClearAreaBrush)
			isSelected=true;
	}
	else if(zoneType==FarmingZone)
	{
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 58);
		if(me.brushType==MapEdit::FarmAreaBrush)
			isSelected=true;
	}
	if(me.selectionMode==MapEdit::PlaceZone && isSelected)
	{
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 22);
	}
}


