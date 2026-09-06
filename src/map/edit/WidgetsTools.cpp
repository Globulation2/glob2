// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include <GAG.h>
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "ScriptEditorScreen.h"
#include "Unit.h"
#include "Utilities.h"
#include "SDLCompat.h"

BrushSelector::BrushSelector(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, BrushTool& brushTool)
	: MapEditorWidget(me, area, group, name, action), brushTool(brushTool)
{

}



void BrushSelector::draw()
{
	brushTool.draw(area.x, area.y);
}



UnitSelector::UnitSelector(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, int unitType)
	: MapEditorWidget(me, area, group, name, action), unitType(unitType)
{

}



void UnitSelector::draw()
{
	// draw units
	Sprite *unitSprite=globalContainer->units;
	unitSprite->setBaseColor(me.game.teams[me.team]->color);
	bool drawSelection=false;
	if(unitType==WORKER)
	{
		if(me.selectionMode==MapEdit::PlaceUnit && me.placingUnit==MapEdit::Worker)
			drawSelection=true;
		globalContainer->gfx->drawSprite(area.x, area.y, unitSprite, 64);
	}
	else if(unitType==EXPLORER)
	{
		if(me.selectionMode==MapEdit::PlaceUnit && me.placingUnit==MapEdit::Explorer)
			drawSelection=true;
		globalContainer->gfx->drawSprite(area.x, area.y, unitSprite, 0);
	}
	else if(unitType==WARRIOR)
	{
		if(me.selectionMode==MapEdit::PlaceUnit && me.placingUnit==MapEdit::Warrior)
			drawSelection=true;
		globalContainer->gfx->drawSprite(area.x, area.y, unitSprite, 256);
	}
	if(drawSelection)
	{
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 23);
	}
}


TerrainSelector::TerrainSelector(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, TerrainType terrainType)
	: MapEditorWidget(me, area, group, name, action), terrainType(terrainType)
{

}




void TerrainSelector::draw()
{
	if(terrainType==Grass)
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->terrain, 0);
	if(terrainType==Sand)
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->terrain, 128);
	if(terrainType==Water)
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->terrain, 259);
	if(terrainType==Wheat)
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->ressources, 19);
	if(terrainType==Trees)
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->ressources, 2);
	if(terrainType==Stone)
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->ressources, 34);
	if(terrainType==Algae)
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->ressources, 44);
	if(terrainType==Papyrus)
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->ressources, 24);
	if(terrainType==CherryTree)
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->ressources, 54);
	if(terrainType==OrangeTree)
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->ressources, 59);
	if(terrainType==PruneTree)
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->ressources, 64);
	if(me.terrainType==terrainType)
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 22);
	if (terrainType == Grass || terrainType == Sand || terrainType == Water)
		globalContainer->gfx->finishDrawingSprite(globalContainer->terrain, 255);
	else
		globalContainer->gfx->finishDrawingSprite(globalContainer->ressources, 255);
	if (me.terrainType == terrainType)
		globalContainer->gfx->finishDrawingSprite(globalContainer->gamegui, 255);
}



BlueButton::BlueButton(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, const std::string& text)
	: MapEditorWidget(me, area, group, name, action), text(text), selected(false)
{

}



void BlueButton::draw()
{
	globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 12);
	if(selected)
		globalContainer->gfx->drawFilledRect(area.x+9, area.y+3, 94, 10, 128, 128, 192);

	std::string translatedText;
	translatedText=Toolkit::getStringTable()->getString(text.c_str());
	int len=globalContainer->littleFont->getStringWidth(translatedText.c_str());
	int h=globalContainer->littleFont->getStringHeight(translatedText.c_str());
	globalContainer->gfx->drawString(area.x+9+((94-len)/2), area.y+((16-h)/2), globalContainer->littleFont, translatedText);
}



void BlueButton::setSelected()
{
	selected=true;
}



void BlueButton::setUnselected()
{
	selected=false;
}



PlusIcon::PlusIcon(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action)
	: MapEditorWidget(me, area, group, name, action)
{
}



void PlusIcon::draw()
{
	globalContainer->gfx->drawFilledRect(area.x, area.y, 32, 32, Color(75,0,200));
	globalContainer->gfx->drawRect(area.x, area.y, 32, 32, Color::white);
	globalContainer->gfx->drawFilledRect(area.x + 15, area.y + 6, 2, 20, Color::white);
	globalContainer->gfx->drawFilledRect(area.x + 6, area.y + 15, 20, 2, Color::white);
}



MinusIcon::MinusIcon(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action)
	: MapEditorWidget(me, area, group, name, action)
{

}



void MinusIcon::draw()
{
	globalContainer->gfx->drawFilledRect(area.x, area.y, 32, 32, Color(75,0,200));
	globalContainer->gfx->drawRect(area.x, area.y, 32, 32, Color::white);
	globalContainer->gfx->drawFilledRect(area.x + 6, area.y + 15, 20, 2, Color::white);
}


