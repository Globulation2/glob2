// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "ScriptEditorScreen.h"
#include "Unit.h"
#include "Utilities.h"
#include "SDLCompat.h"

MapEditorWidget::MapEditorWidget(MapEdit& me, const widgetRectangle& rectangle, const std::string& group, const std::string& name, const std::string& action)
	: me(me), area(rectangle), group(group), name(name), action(action), enabled(false)
{

}



void MapEditorWidget::drawSelf()
{
	if(enabled)
		draw();
}



void MapEditorWidget::disable()
{
	enabled=false;
}



void MapEditorWidget::enable()
{
	enabled=true;
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

	BuildingType *bt = globalContainer->buildingsTypes.getByType(type.c_str(), me.buildingLevel, false);
	if(bt==NULL || !me.isUpgradable(IntBuildingType::shortNumberFromType(type)))
		bt = globalContainer->buildingsTypes.getByType(type.c_str(), 0, false);
	assert(bt);

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
		
	buildingSprite->setBaseColor(me.game.teams[me.team]->color);
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
	for(int n=0; n<16; ++n)
	{
		const int xpos = area.x + (n%6)*16;
		const int ypos = area.y + (n/6)*16;
		if(me.game.teams[n])
		{
			if(me.team==n)
				globalContainer->gfx->drawFilledRect(xpos, ypos, 16, 16, Color(me.game.teams[n]->color.r, me.game.teams[n]->color.g, me.game.teams[n]->color.b, 128));
			else
				globalContainer->gfx->drawFilledRect(xpos, ypos, 16, 16, me.game.teams[n]->color);

		}
	}
}



SingleLevelSelector::SingleLevelSelector(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, int level, int& levelNum)
	: MapEditorWidget(me, area, group, name, action), level(level), levelNum(levelNum)
{

}



void SingleLevelSelector::draw()
{
	globalContainer->gfx->drawSprite(area.x, area.y, me.menu, 30+level-1, (level-1)==levelNum ? 128 : 255);
}



PanelIcon::PanelIcon(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, int iconNumber, int panelModeHilight)
	: MapEditorWidget(me, area, group, name, action), iconNumber(iconNumber), panelModeHilight(panelModeHilight)
{

}



void PanelIcon::draw()
{
	// draw buttons
	if (me.panelMode==panelModeHilight)
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
	if(me.selectionMode==MapEdit::PlaceZone && isSelected)
	{
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 22);
	}
}


