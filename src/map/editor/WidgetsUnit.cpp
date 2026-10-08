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
#include "Unit.h"
#include "render/UnitAnimation.h"
#include "UnitType.h"
#include <SDL3/SDL.h>

UnitInfoTitle::UnitInfoTitle(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action)
	: MapEditorWidget(me, area, group, name, action)
{

}



void UnitInfoTitle::draw()
{
	const int xpos=area.x;
	const int ypos=area.y;
    const auto& frame=*me.view.scene;
    const auto* u=frame.entities.unit(frame.entities.selectedUnit);
    if (!u) return;

	// draw "unit of player" title
	Uint8 r, g, b;
	std::string title;
	title += getUnitName(u->typeNum);
	title += " (";

	title += displayPlayerName(frame.entities.teams[u->team].firstPlayerName);
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







UnitPicture::UnitPicture(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action)
	: MapEditorWidget(me, area, group, name, action)
{

}



void UnitPicture::draw()
{
	const int xpos=area.x;
	const int ypos=area.y;

    const auto& frame=*me.view.scene;
    const auto* unit=frame.entities.unit(frame.entities.selectedUnit);
    if (!unit) return;
	// draw unit's image
	int imgid;
	const UnitType *ut=&frame.world.catalogs->unitTypes[unit->typeNum][0];
	assert(unit->action>=0);
	assert(unit->action<NB_MOVE);
	imgid=ut->startImage[unit->action];

	int dir=unit->direction;
	int delta=unit->delta;
	assert(dir>=0);
	assert(dir<9);
	assert(delta>=0);
	assert(delta<256);
	imgid=unitAnimationFrame(imgid, dir, delta);

	Sprite *unitSprite=globalContainer->units;
	unitSprite->setBaseColor(presentationColor(frame.entities.teams[unit->team].color));
	int decX = (32-unitSprite->getW(imgid))/2;
	int decY = (32-unitSprite->getH(imgid))/2;
	globalContainer->gfx->drawSprite(xpos+12+decX, ypos+7+decY, unitSprite, imgid);
	globalContainer->gfx->drawSprite(xpos, ypos, globalContainer->gamegui, 18);
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
    if (!me.view.scene || !readValue || (!isDenominatorPreset && !readMax)) return;
    const auto value=readValue(*me.view.scene);
    const auto maximum=isDenominatorPreset ? *denominator : readMax(*me.view.scene);
	globalContainer->gfx->drawString(area.x, area.y, globalContainer->littleFont, FormattableString("%0:  %1/%2").arg(Toolkit::getStringTable()->getString(label.c_str())).arg(value).arg(maximum).c_str());
}



void FractionValueText::setValues(Sint32* aNumerator, Sint32* aDenominator, EditorValueReader reader, EditorValueReader maxReader)
{
	if (isDenominatorPreset) delete denominator;
	isDenominatorPreset=false;
	numerator=aNumerator;
    readValue=std::move(reader);
	denominator=aDenominator;
    readMax=std::move(maxReader);
}



void FractionValueText::setValues(Sint32* aNumerator, EditorValueReader reader)
{
	numerator=aNumerator;
    readValue=std::move(reader);
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
	if(maximumValue() != 0)
	{
		globalContainer->gfx->setClipRect(area.x, area.y, 112, 16);
		globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 9);
		int size=int((Sint64(currentValue())*92)/maximumValue());
		globalContainer->gfx->setClipRect(area.x+10, area.y, size, 16);
		globalContainer->gfx->drawSprite(area.x+10, area.y+3, globalContainer->gamegui, 10);
		globalContainer->gfx->setClipRect();
	}
}



void ValueScrollBox::handleClick(int relMouseX, int relMouseY)
{
    const auto before=*value;
	if(relMouseX<10)
		(*value)=std::max((*value)-1, 0);
	else if(relMouseX>102)
		(*value)=std::min((*value)+1, (*max));
	else
		(*value)=int(float(relMouseX-10) * (float(*max)/float(92))+0.5);
    if (*value!=before) me.game.snapshots().invalidateBoundary();
	MapEditorWidget::handleClick(relMouseX, relMouseY);
}



void ValueScrollBox::setValue(int requested)
{
    const auto before=*value;
    *value = std::clamp(requested, 0, std::max(0, int(*max)));
    if (*value!=before) me.game.snapshots().invalidateBoundary();
    me.mapHasBeenModified();
    activate();
}

void ValueScrollBox::setValues(Sint32* aValue, Sint32* aMax, EditorValueReader reader, EditorValueReader maxReader)
{
	if (isMaxPreset) delete max;
	isMaxPreset=false;
	value=aValue;
    readValue=std::move(reader);
	max=aMax;
    readMax=std::move(maxReader);
}



void ValueScrollBox::setValues(Sint32* aValue, EditorValueReader reader)
{
	value=aValue;
    readValue=std::move(reader);
}



int ValueScrollBox::currentValue() const
{
    return me.view.scene && readValue ? readValue(*me.view.scene) : 0;
}
int ValueScrollBox::maximumValue() const
{
    return isMaxPreset ? *max : me.view.scene && readMax ? readMax(*me.view.scene) : 0;
}
