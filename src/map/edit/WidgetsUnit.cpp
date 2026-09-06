// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include <FormatableString.h>
#include <GAG.h>
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "TeamDisplay.h"
#include "UnitDisplayNames.h"
#include "UnitEditorScreen.h"
#include "Unit.h"
#include "UnitType.h"
#include "SDLCompat.h"

UnitInfoTitle::UnitInfoTitle(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, Unit* unit)
	: MapEditorWidget(me, area, group, name, action), unit(unit)
{

}



void UnitInfoTitle::draw()
{
	const int xpos=area.x;
	const int ypos=area.y;
	Unit* u=unit;

	// draw "unit of player" title
	Uint8 r, g, b;
	std::string title;
	title += getUnitName(u->typeNum);
	title += " (";

	title += displayPlayerName(*u->owner);
	title += ")";

	r=160;
	g=160;
	b=255;

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	int titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	int titlePos = xpos+((128-titleLen)/2);
	globalContainer->gfx->drawString(titlePos, ypos, globalContainer->littleFont, title.c_str());
	globalContainer->littleFont->popStyle();
}



void UnitInfoTitle::setUnit(Unit* aUnit)
{
	unit=aUnit;
}



UnitPicture::UnitPicture(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, Unit* unit)
	: MapEditorWidget(me, area, group, name, action), unit(unit)
{

}



void UnitPicture::draw()
{
	const int xpos=area.x;
	const int ypos=area.y;

	// draw unit's image
	int imgid;
	UnitType *ut=unit->race->getUnitType(unit->typeNum, 0);
	assert(unit->action>=0);
	assert(unit->action<NB_MOVE);
	imgid=ut->startImage[unit->action];

	int dir=unit->direction;
	int delta=unit->delta;
	assert(dir>=0);
	assert(dir<9);
	assert(delta>=0);
	assert(delta<256);
	if (dir==8)
	{
		imgid+=8*(delta>>5);
	}
	else
	{
		imgid+=8*dir;
		imgid+=(delta>>5);
	}

	Sprite *unitSprite=globalContainer->units;
	unitSprite->setBaseColor(unit->owner->color);
	int decX = (32-unitSprite->getW(imgid))/2;
	int decY = (32-unitSprite->getH(imgid))/2;
	globalContainer->gfx->drawSprite(xpos+12+decX, ypos+7+decY, unitSprite, imgid);
	globalContainer->gfx->drawSprite(xpos, ypos, globalContainer->gamegui, 18);
}



void UnitPicture::setUnit(Unit* aUnit)
{
	unit=aUnit;
}



FractionValueText::FractionValueText(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, const std::string& label, Sint32* numerator, Sint32* denominator)
	: MapEditorWidget(me, area, group, name, action), label(label), numerator(numerator), denominator(denominator), isDenominatorPreset(false)
{

}



FractionValueText::FractionValueText(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, const std::string& label, Sint32* numerator, Sint32 denominator)
	: MapEditorWidget(me, area, group, name, action), label(label), numerator(numerator), denominator(new Sint32(denominator)), isDenominatorPreset(true)
{

}



FractionValueText::~FractionValueText()
{
	if(isDenominatorPreset)
		delete denominator;
}



void FractionValueText::draw()
{
	globalContainer->gfx->drawString(area.x, area.y, globalContainer->littleFont, FormatableString("%0:  %1/%2").arg(Toolkit::getStringTable()->getString(label.c_str())).arg(*numerator).arg(*denominator).c_str());
}



void FractionValueText::setValues(Sint32* aNumerator, Sint32* aDenominator)
{
	numerator=aNumerator;
	denominator=aDenominator;
}



void FractionValueText::setValues(Sint32* aNumerator)
{
	numerator=aNumerator;
}



ValueScrollBox::ValueScrollBox(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, Sint32* value, Sint32* max)
	: MapEditorWidget(me, area, group, name, action), value(value), max(max), isMaxPreset(false)
{

}



ValueScrollBox::ValueScrollBox(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, Sint32* value, Sint32 max)
	: MapEditorWidget(me, area, group, name, action), value(value), max(new Sint32(max)), isMaxPreset(true)
{

}



ValueScrollBox::~ValueScrollBox()
{
	if(isMaxPreset)
		delete max;
}



void ValueScrollBox::draw()
{
	//Sometimes a scrollbox gets initiated with max-value 0. A turret construction site has 0/0 stone and 0/0 shots. To not run into arithmetic exceptions those cases are treated here.
	if((*max) != 0)
	{
		globalContainer->gfx->setClipRect(area.x, area.y, 112, 16);
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 9);
		int size=((*value)*92)/(*max);
		globalContainer->gfx->setClipRect(area.x+10, area.y, size, 16);
		globalContainer->gfx->drawSprite(area.x+10, area.y+3, globalContainer->gamegui, 10);
		globalContainer->gfx->setClipRect();
	}
}



void ValueScrollBox::handleClick(int relMouseX, int relMouseY)
{
	if(relMouseX<10)
		(*value)=std::max((*value)-1, 0);
	else if(relMouseX>102)
		(*value)=std::min((*value)+1, (*max));
	else
		(*value)=int(float(relMouseX-10) * (float(*max)/float(92))+0.5);
	MapEditorWidget::handleClick(relMouseX, relMouseY);
}



void ValueScrollBox::setValues(Sint32* aValue, Sint32* aMax)
{
	value=aValue;
	max=aMax;
}



void ValueScrollBox::setValues(Sint32* aValue)
{
	value=aValue;
}


