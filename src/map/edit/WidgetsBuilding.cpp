// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include <GAG.h>
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "TeamDisplay.h"
#include <sstream>
#include "Unit.h"
#include "SDLCompat.h"

BuildingInfoTitle::BuildingInfoTitle(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, Building* building)
	: MapEditorWidget(me, area, group, name, action), building(building)
{

}



void BuildingInfoTitle::draw()
{
	Building* selBuild = building;
	BuildingType *buildingType = selBuild->type;
	Uint8 r, g, b;

	// draw "building" of "player"
	std::string title;
	std::string key = "[" + buildingType->type + "]";
	title += Toolkit::getStringTable()->getString(key.c_str());
	{
		title += " (";
		title += displayPlayerName(*selBuild->owner);
		title += ")";
	}

	r=160;
	g=160;
	b=255;

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	int titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	int titlePos = area.x+((area.width-titleLen)/2);
	globalContainer->gfx->drawString(titlePos, area.y, globalContainer->littleFont, title.c_str());
	globalContainer->littleFont->popStyle();
}



void BuildingInfoTitle::setBuilding(Building* aBuilding)
{
	building=aBuilding;
}



BuildingPicture::BuildingPicture(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, Building* building)
	: MapEditorWidget(me, area, group, name, action), building(building)
{

}



void BuildingPicture::draw()
{
	Building* selBuild = building;
	BuildingType *buildingType = selBuild->type;

	// building icon
	Sprite *miniSprite;
	int imgid;
	if (buildingType->miniSpriteImage >= 0)
	{
		miniSprite = buildingType->miniSpritePtr;
		imgid = buildingType->miniSpriteImage;
	}
	else
	{
		miniSprite = buildingType->gameSpritePtr;
		imgid = buildingType->gameSpriteImage;
	}
	int dx = (56-miniSprite->getW(imgid))/2;
	int dy = (46-miniSprite->getH(imgid))/2;
	miniSprite->setBaseColor(selBuild->owner->color);
	globalContainer->gfx->drawSprite(area.x+dx, area.y+dy, miniSprite, imgid);
	globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 18);
	globalContainer->gfx->finishDrawingSprite(miniSprite, 255);
	globalContainer->gfx->finishDrawingSprite(globalContainer->gamegui, 255);
}



void BuildingPicture::setBuilding(Building* aBuilding)
{
	building=aBuilding;
}



TextLabel::TextLabel(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, const std::string& label, bool centered, const std::string& emptyLabel)
	: MapEditorWidget(me, area, group, name, action), label(label), emptyLabel(emptyLabel), centered(centered)
{
	
}



void TextLabel::draw()
{
	std::string label=this->label;
	if(label=="")
		label=this->emptyLabel;
	int titleWidth = globalContainer->littleFont->getStringWidth(label.c_str());
	int titleHeight = globalContainer->littleFont->getStringHeight(label.c_str());
	if(centered)
		globalContainer->gfx->drawString(area.x+(area.width-titleWidth)/2, area.y+(area.height-titleHeight)/2, globalContainer->littleFont, label.c_str());
	else
		globalContainer->gfx->drawString(area.x, area.y, globalContainer->littleFont, label.c_str());
}



void TextLabel::setLabel(const std::string& aLabel)
{
	label=aLabel;
}



NumberCycler::NumberCycler(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, int maxNumber)
	: MapEditorWidget(me, area, group, name, action), maxNumber(maxNumber), currentNumber(1)
{

}



void NumberCycler::draw()
{
	std::stringstream s;
	s<<currentNumber;
	globalContainer->gfx->drawString(area.x, area.y, globalContainer->standardFont, s.str().c_str());
}



int NumberCycler::getIndex()
{
	return currentNumber-1;
}



void NumberCycler::handleClick(int relMouseX, int relMouseY)
{
	currentNumber++;
	if(currentNumber>maxNumber)
		currentNumber=1;
	MapEditorWidget::handleClick(relMouseX, relMouseY);
}




Checkbox::Checkbox(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, const std::string& text, bool& isActivated)
	: MapEditorWidget(me, area, group, name, action), text(text), isActivated(isActivated)
{

}



void Checkbox::draw()
{
	globalContainer->gfx->drawRect(area.x, area.y, 16, 16, Color::white);
	if(isActivated)
	{
		globalContainer->gfx->drawLine(area.x+4, area.y+4, area.x+12, area.y+12, Color::white);
		globalContainer->gfx->drawLine(area.x+12, area.y+4, area.x+4, area.y+12, Color::white);
	}
	
	std::string translatedText;
	translatedText=Toolkit::getStringTable()->getString(text.c_str());
	
	globalContainer->gfx->drawString(area.x+20, area.y, globalContainer->littleFont, translatedText); 
}



void Checkbox::handleClick(int relMouseX, int relMouseY)
{
	isActivated = !isActivated;
	MapEditorWidget::handleClick(relMouseX, relMouseY);
}


