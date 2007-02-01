/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  Copyright (C) 2006 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <cmath>

#include <GAG.h>
#include "Game.h"
#include "GameGUILoadSave.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "ScriptEditorScreen.h"
#include "UnitEditorScreen.h"
#include "Unit.h"
#include "UnitType.h"
#include "Utilities.h"

#include <FormatableString.h>
#include <Stream.h>
#include <StreamFilter.h>

#include <sstream>


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
		
	buildingSprite->setBaseColor(me.game.teams[me.team]->colorR, me.game.teams[me.team]->colorG, me.game.teams[me.team]->colorB);
	globalContainer->gfx->drawSprite(x, y, buildingSprite, imgid);

	// draw selection if needed
	if (me.selectionName == type)
	{
		if (largeSelector)
			globalContainer->gfx->drawSprite(x-8, y-5, globalContainer->gamegui, 8);
		else
			globalContainer->gfx->drawSprite(x-4, y-3, globalContainer->gamegui, 23);
	}
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
				globalContainer->gfx->drawFilledRect(xpos, ypos, 16, 16, Color(me.game.teams[n]->colorR, me.game.teams[n]->colorG, me.game.teams[n]->colorB, 128));
			else
				globalContainer->gfx->drawFilledRect(xpos, ypos, 16, 16, Color(me.game.teams[n]->colorR, me.game.teams[n]->colorG, me.game.teams[n]->colorB));

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
	unitSprite->setBaseColor(me.game.teams[me.team]->colorR, me.game.teams[me.team]->colorG, me.game.teams[me.team]->colorB);
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



TeamInfo::TeamInfo(MapEdit& me, const widgetRectangle& area, const std::string& group, const std::string& name, const std::string& action, int teamNum, std::vector<std::string>& options)
	: MapEditorWidget(me, area, group, name, action), teamNum(teamNum), selectorPos(0), options(options)
{

}



void TeamInfo::draw()
{
	if(me.game.teams[teamNum])
	{
		globalContainer->gfx->drawFilledRect(area.x, area.y, 16, 16, Color(me.game.teams[teamNum]->colorR, me.game.teams[teamNum]->colorG, me.game.teams[teamNum]->colorB));
		globalContainer->gfx->drawString(area.x+20, area.y+4, globalContainer->littleFont, Toolkit::getStringTable()->getString(options[selectorPos].c_str()));
	}
}



void TeamInfo::handleClick(int relMouseX, int relMouseY)
{
	if(me.game.teams[teamNum])
	{
		if(relMouseX>15)
		{
			selectorPos++;
			if(static_cast<unsigned>(selectorPos)==options.size())
				selectorPos=0;
			MapEditorWidget::handleClick(relMouseX, relMouseY);
		}
	}
}


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
	title += Toolkit::getStringTable()->getString("[Unit type]", u->typeNum);
	title += " (";

	const char *textT=u->owner->getFirstPlayerName();
	if (!textT)
		textT=Toolkit::getStringTable()->getString("[Uncontrolled]");
	title += textT;
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
	unitSprite->setBaseColor(unit->owner->colorR, unit->owner->colorG, unit->owner->colorB);
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
	globalContainer->gfx->setClipRect(area.x, area.y, 112, 16);
	globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 9);

	int size=((*value)*92)/(*max);
	globalContainer->gfx->setClipRect(area.x+10, area.y, size, 16);
	globalContainer->gfx->drawSprite(area.x+10, area.y+3, globalContainer->gamegui, 10);
	
	globalContainer->gfx->setClipRect();
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
	title += Toolkit::getStringTable()->getString("[Building name]", buildingType->shortTypeNum);
	{
		title += " (";
		const char *textT=selBuild->owner->getFirstPlayerName();
		if (!textT)
			textT=Toolkit::getStringTable()->getString("[Uncontrolled]");
		title += textT;
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
	miniSprite->setBaseColor(selBuild->owner->colorR, selBuild->owner->colorG, selBuild->owner->colorB);
	globalContainer->gfx->drawSprite(area.x+dx, area.y+dy, miniSprite, imgid);
	globalContainer->gfx->drawSprite(area.x, area.y, globalContainer->gamegui, 18);
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



MapEdit::MapEdit() : game(NULL)
{
	doQuit=false;

	// default value;
	viewportX=0;
	viewportY=0;
	xSpeed=0;
	ySpeed=0;
	mouseX=0;
	mouseY=0;
	relMouseX=0;
	relMouseY=0;
	wasMinimapRendered=false;

	// load menu
	menu=Toolkit::getSprite("data/gui/editor");

	// editor facilities
	hasMapBeenModified=false;
	team=0;

	selectionMode=PlaceNothing;

	panelMode=AddBuildings;
	buildingView = new PanelIcon(*this, widgetRectangle(globalContainer->gfx->getW()-128, 128, 32, 32), "any", "building view icon", "switch to building view", 0, AddBuildings);
	flagsView = new PanelIcon(*this, widgetRectangle(globalContainer->gfx->getW()-96, 128, 32, 32), "any", "flag view icon", "switch to flag view", 0, AddFlagsAndZones);
	terrainView = new PanelIcon(*this, widgetRectangle(globalContainer->gfx->getW()-64, 128, 32, 32), "any", "terrain view icon", "switch to terrain view", 0, Terrain);
	teamsView = new PanelIcon(*this, widgetRectangle(globalContainer->gfx->getW()-32, 128, 32, 32), "any", "teams view icon", "switch to teams view", 0, Teams);
	menuIcon = new MenuIcon(*this, widgetRectangle(globalContainer->gfx->getW()-160, 0, 32, 32), "any", "menu icon", "open menu screen");
	addWidget(buildingView);
	addWidget(flagsView);
	addWidget(terrainView);
	addWidget(teamsView);
	addWidget(menuIcon);
	swarm = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-128+12, 128+32+6, 40, 40), "building view", "swarm", "set place building selection swarm", "swarm", true);
	inn = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-128+64+12, 128+32+6, 40, 40), "building view", "inn", "set place building selection inn", "inn", true);
	hospital = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-128+12, 128+32+46*1+6, 40, 40), "building view", "hospital", "set place building selection hospital", "hospital", true);
	racetrack = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-128+64+12, 128+32+46*1+6, 40, 40), "building view", "racetrack", "set place building selection racetrack", "racetrack", true);
	swimmingpool = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-128+12, 128+32+46*2+6, 40, 40), "building view", "swimmingpool", "set place building selection swimmingpool", "swimmingpool", true);
	barracks = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-128+64+12, 128+32+46*2+6, 40, 40), "building view", "barracks", "set place building selection barracks", "barracks", true);
	school = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-128+12, 128+32+46*3+6, 40, 40), "building view", "school", "set place building selection school", "school", true);
	defencetower = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-128+64+12, 128+32+46*3+6, 40, 40), "building view", "defencetower", "set place building selection defencetower", "defencetower", true);
	stonewall = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-128+12, 128+32+46*4+6, 40, 40), "building view", "stonewall", "set place building selection stonewall", "stonewall", true);
	market = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-128+64+12, 128+32+46*4+6, 40, 40), "building view", "market", "set place building selection market", "market", true);
	building_view_tcs = new TeamColorSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128 + 16, globalContainer->gfx->getH()-74, 96, 32 ), "building view", "building view team selector", "select active team");
	building_view_level1 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128, globalContainer->gfx->getH()-36, 32, 32), "building view", "building view level 1", "switch to building level 1", 1, buildingLevel);
	building_view_level2 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+32, globalContainer->gfx->getH()-36, 32, 32), "building view", "building view level 2", "switch to building level 2", 2, buildingLevel);
	building_view_level3 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+64, globalContainer->gfx->getH()-36, 32, 32), "building view", "building view level 3", "switch to building level 3", 3, buildingLevel);
	addWidget(swarm);
	addWidget(inn);
	addWidget(hospital);
	addWidget(racetrack);
	addWidget(swimmingpool);
	addWidget(barracks);
	addWidget(school);
	addWidget(defencetower);
	addWidget(stonewall);
	addWidget(market);
	addWidget(building_view_tcs);
	addWidget(building_view_level1);
	addWidget(building_view_level2);
	addWidget(building_view_level3);
	explorationflag = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-128+5, 128+32+7, 32, 32), "flag view", "explorationflag", "set place building selection explorationflag", "explorationflag", false);
	warflag = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-128+5+42, 128+32+7, 32, 32), "flag view", "warflag", "set place building selection warflag", "warflag", false);
	clearingflag = new BuildingSelectorWidget(*this, widgetRectangle(globalContainer->gfx->getW()-128+5+84, 128+32+7, 32, 32), "flag view", "clearingflag", "set place building selection clearingflag", "clearingflag", false);
	forbiddenZone = new ZoneSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 216, 32, 32), "flag view", "forbidden zone", "select forbidden zone", ZoneSelector::ForbiddenZone);
	guardZone = new ZoneSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+8+40, 216, 32, 32), "flag view", "guard zone", "select guard zone", ZoneSelector::GuardingZone);
	clearingZone = new ZoneSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+8+80, 216, 32, 32), "flag view", "clearing zone", "select clearing zone", ZoneSelector::ClearingZone);
	zoneBrushSelector = new BrushSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128, 216+40, 128, 96), "flag view", "zone brush selector", "handle zone click", brush);
	worker = new UnitSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 360, 38, 38), "flag view", "worker selector", "select worker", WORKER);
	explorer = new UnitSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+48, 360, 38, 38), "flag view", "explorer selector", "select explorer", EXPLORER);
	warrior = new UnitSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+88, 360, 38, 38), "flag view", "warrior selector", "select warrior", WARRIOR);
	flag_view_tcs = new TeamColorSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128 + 16, globalContainer->gfx->getH()-74, 96, 32 ), "flag view", "flag view team selector", "select active team");
	flag_view_level1 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128, globalContainer->gfx->getH()-36, 32, 32), "flag view", "flag view level 1", "select unit level 1", 1, placingUnitLevel);
	flag_view_level2 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+32, globalContainer->gfx->getH()-36, 32, 32), "flag view", "flag view level 2", "select unit level 2", 2, placingUnitLevel);
	flag_view_level3 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+64, globalContainer->gfx->getH()-36, 32, 32), "flag view", "flag view level 3", "select unit level 3", 3, placingUnitLevel);
	flag_view_level4 = new SingleLevelSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+96, globalContainer->gfx->getH()-36, 32, 32), "flag view", "flag view level 3", "select unit level 4", 4, placingUnitLevel);
	addWidget(warflag);
	addWidget(explorationflag);
	addWidget(clearingflag);
	addWidget(forbiddenZone);
	addWidget(guardZone);
	addWidget(clearingZone);
	addWidget(zoneBrushSelector);
	addWidget(worker);
	addWidget(warrior);
	addWidget(explorer);
	addWidget(flag_view_tcs);
	addWidget(flag_view_level1);
	addWidget(flag_view_level2);
	addWidget(flag_view_level3);
	addWidget(flag_view_level4);

	grass = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128, 172, 32, 32), "terrain view", "grass selector", "select grass", TerrainSelector::Grass);
	sand = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+32, 172, 32, 32), "terrain view", "sand selector", "select sand", TerrainSelector::Sand);
	water = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+64, 172, 32, 32), "terrain view", "water selector", "select water", TerrainSelector::Water);
	wheat = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+96, 172, 32, 32), "terrain view", "wheat selector", "select wheat", TerrainSelector::Wheat);
	trees = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128, 210, 32, 32), "terrain view", "trees selector", "select trees", TerrainSelector::Trees);
	stone = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+32, 210, 32, 32), "terrain view", "stone selector", "select stone", TerrainSelector::Stone);
	algae = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+64, 210, 32, 32), "terrain view", "algae selector", "select algae", TerrainSelector::Algae);
	papyrus = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+96, 210, 32, 32), "terrain view", "papyrus selector", "select papyrus", TerrainSelector::Papyrus);
	orange = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128, 248, 32, 32), "terrain view", "orange selector", "select orange tree", TerrainSelector::OrangeTree);
	cherry = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+32, 248, 32, 32), "terrain view", "cherry selector", "select cherry tree", TerrainSelector::CherryTree);
	prune = new TerrainSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128+64, 248, 32, 32), "terrain view", "prune selector", "select prune tree", TerrainSelector::PruneTree);
	deleteButton = new BlueButton(*this, widgetRectangle(globalContainer->gfx->getW()-128 + 8, 294, 112, 16), "terrain view", "delete button", "select delete objects", "[delete]");
	noRessourceGrowthButton = new BlueButton(*this, widgetRectangle(globalContainer->gfx->getW()-128 + 8, 320, 112, 16), "terrain view", "no ressources growth button", "select no ressources growth", "[no ressources growth areas]");
	areasButton = new BlueButton(*this, widgetRectangle(globalContainer->gfx->getW()-128 + 8, 346, 112, 16), "terrain view", "script areas button", "select change areas", "[Script Areas]");
	areaNumber = new NumberCycler(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 362, 8, 16), "terrain view", "script area number selector", "update script area number", 9);
	areaNameLabel = new TextLabel(*this, widgetRectangle(globalContainer->gfx->getW()-128+24, 362, 104, 16), "terrain view", "script area name label", "open area name", "", false, Toolkit::getStringTable()->getString("[Unnamed Area]"));
	terrainBrushSelector = new BrushSelector(*this, widgetRectangle(globalContainer->gfx->getW()-128, 388, 128, 96), "terrain view", "terrain brush selector", "handle terrain click", brush);
	addWidget(grass);
	addWidget(sand);
	addWidget(water);
	addWidget(wheat);
	addWidget(trees);
	addWidget(stone);
	addWidget(algae);
	addWidget(papyrus);
	addWidget(orange);
	addWidget(cherry);
	addWidget(prune);
	addWidget(deleteButton);
	addWidget(noRessourceGrowthButton);
	addWidget(areasButton);
	addWidget(areaNumber);
	addWidget(areaNameLabel);
	addWidget(terrainBrushSelector);

	increaseTeams = new PlusIcon(*this, widgetRectangle(globalContainer->gfx->getW()-128, 408, 32, 32), "teams view", "increase teams", "add team");
	decreaseTeams = new MinusIcon(*this, widgetRectangle(globalContainer->gfx->getW()-128+40, 408, 32, 32), "teams view", "decrease teams", "remove team");
	teamInfo1 = new TeamInfo(*this, widgetRectangle(globalContainer->gfx->getW()-128, 168, 128, 18), "teams view", "team info 1", "change team info 1", 0, teamViewSelectorKeys);
	teamInfo2 = new TeamInfo(*this, widgetRectangle(globalContainer->gfx->getW()-128, 186, 128, 18), "teams view", "team info 2", "change team info 2", 1, teamViewSelectorKeys);
	teamInfo3 = new TeamInfo(*this, widgetRectangle(globalContainer->gfx->getW()-128, 204, 128, 18), "teams view", "team info 3", "change team info 3", 2, teamViewSelectorKeys);
	teamInfo4 = new TeamInfo(*this, widgetRectangle(globalContainer->gfx->getW()-128, 222, 128, 18), "teams view", "team info 4", "change team info 4", 3, teamViewSelectorKeys);
	teamInfo5 = new TeamInfo(*this, widgetRectangle(globalContainer->gfx->getW()-128, 240, 128, 18), "teams view", "team info 5", "change team info 5", 4, teamViewSelectorKeys);
	teamInfo6 = new TeamInfo(*this, widgetRectangle(globalContainer->gfx->getW()-128, 258, 128, 18), "teams view", "team info 6", "change team info 6", 5, teamViewSelectorKeys);
	teamInfo7 = new TeamInfo(*this, widgetRectangle(globalContainer->gfx->getW()-128, 276, 128, 18), "teams view", "team info 7", "change team info 7", 6, teamViewSelectorKeys);
	teamInfo8 = new TeamInfo(*this, widgetRectangle(globalContainer->gfx->getW()-128, 294, 128, 18), "teams view", "team info 8", "change team info 8", 7, teamViewSelectorKeys);
	teamInfo9 = new TeamInfo(*this, widgetRectangle(globalContainer->gfx->getW()-128, 312, 128, 18), "teams view", "team info 9", "change team info 9", 8, teamViewSelectorKeys);
	teamInfo10 = new TeamInfo(*this, widgetRectangle(globalContainer->gfx->getW()-128, 330, 128, 18), "teams view", "team info 10", "change team info 10", 9, teamViewSelectorKeys);
	teamInfo11 = new TeamInfo(*this, widgetRectangle(globalContainer->gfx->getW()-128, 348, 128, 18), "teams view", "team info 11", "change team info 11", 10, teamViewSelectorKeys);
	teamInfo12 = new TeamInfo(*this, widgetRectangle(globalContainer->gfx->getW()-128, 364, 128, 18), "teams view", "team info 12", "change team info 12", 11, teamViewSelectorKeys);
	addWidget(increaseTeams);
	addWidget(decreaseTeams);
	addWidget(teamInfo1);
	addWidget(teamInfo2);
	addWidget(teamInfo3);
	addWidget(teamInfo4);
	addWidget(teamInfo5);
	addWidget(teamInfo6);
	addWidget(teamInfo7);
	addWidget(teamInfo8);
	addWidget(teamInfo9);
	addWidget(teamInfo10);
	addWidget(teamInfo11);
	addWidget(teamInfo12);

	unitInfoTitle = new UnitInfoTitle(*this, widgetRectangle(globalContainer->gfx->getW()-128, 173, 128, 16), "unit editor", "unit editor title", "", NULL);
	unitPicture = new UnitPicture(*this, widgetRectangle(globalContainer->gfx->getW()-128+2, 203, 40, 40), "unit editor", "unit editor picture", "", NULL);
	unitHPLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 252, 128, 16), "unit editor", "unit editor hp label", "", "[hp]", NULL, static_cast<Sint32*>(NULL));
	unitHPScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 268, 112, 16), "unit editor", "unit editor hp scroll box", "", NULL, static_cast<Sint32*>(NULL));
	unitWalkLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 284, 128, 16), "unit editor", "unit editor walk level label", "", "[Walk]", NULL, 3);
	unitWalkLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 300, 112, 16), "unit editor", "unit editor walk level scroll box", "update unit walk level", NULL, 3);
	unitSwimLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 316, 128, 16), "unit editor", "unit editor swim level label", "", "[Swim]", NULL, 3);
	unitSwimLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 332, 112, 16), "unit editor", "unit editor swim level scroll box", "update unit swim level", NULL, 3);
	unitHarvestLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 348, 128, 16), "unit editor", "unit editor harvest level label", "", "[Harvest]", NULL, 3);
	unitHarvestLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 364, 112, 16), "unit editor", "unit editor harvest level scroll box", "update unit harvest level", NULL, 3);
	unitBuildLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 380, 128, 16), "unit editor", "unit editor build level label", "", "[Build]", NULL, 3);
	unitBuildLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 396, 112, 16), "unit editor", "unit editor build level scroll box", "update unit build level", NULL, 3);
	unitAttackSpeedLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 348, 128, 16), "unit editor", "unit editor attack speed level label", "", "[At. speed]", NULL, 3);
	unitAttackSpeedLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 364, 112, 16), "unit editor", "unit editor attack speed level scroll box", "update unit attack speed level", NULL, 3);
	unitAttackStrengthLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 380, 128, 16), "unit editor", "unit editor attack strength level label", "", "[At. strength]", NULL, 3);
	unitAttackStrengthLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 396, 112, 16), "unit editor", "unit editor attack strength level scroll box", "update unit attack strength level", NULL, 3);
	unitMagicGroundAttackLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 284, 128, 16), "unit editor", "unit editor ground attack level label", "", "[Magic At. Ground]", NULL, 3);
	unitMagicGroundAttackLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 300, 112, 16), "unit editor", "unit editor magic ground attack level scroll box", "update unit magic ground attack level", NULL, 3);
	addWidget(unitInfoTitle);
	addWidget(unitPicture);
	addWidget(unitHPLabel);
	addWidget(unitHPScrollBox);
	addWidget(unitWalkLevelLabel);
	addWidget(unitWalkLevelScrollBox);
	addWidget(unitSwimLevelLabel);
	addWidget(unitSwimLevelScrollBox);
	addWidget(unitHarvestLevelLabel);
	addWidget(unitHarvestLevelScrollBox);
	addWidget(unitBuildLevelLabel);
	addWidget(unitBuildLevelScrollBox);
	addWidget(unitAttackSpeedLevelLabel);
	addWidget(unitAttackSpeedLevelScrollBox);
	addWidget(unitAttackStrengthLevelLabel);
	addWidget(unitAttackStrengthLevelScrollBox);
	addWidget(unitMagicGroundAttackLevelLabel);
	addWidget(unitMagicGroundAttackLevelScrollBox);

	buildingInfoTitle = new BuildingInfoTitle(*this, widgetRectangle(globalContainer->gfx->getW()-128+2, 173, 128, 16), "building editor", "building editor info title", "", NULL);
	buildingPicture = new BuildingPicture(*this, widgetRectangle(globalContainer->gfx->getW()-128+2, 203, 56, 46), "building editor", "building editor picture", "", NULL);
	buildingHPLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 252, 128, 16), "building editor", "building editor hp label", "", "[hp]", NULL, static_cast<Sint32*>(NULL));
	buildingHPScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 268, 128, 16), "building editor", "building editor hp scroll box", "", NULL, static_cast<Sint32*>(NULL));
	buildingFoodQuantityLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 252, 128, 16), "building editor", "building editor food label", "", "[wheat]", NULL, static_cast<Sint32*>(NULL));
	buildingFoodQuantityScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 268, 128, 16), "building editor", "building editor food scroll box", "", NULL, static_cast<Sint32*>(NULL));
	buildingAssignedLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 252, 128, 16), "building editor", "building editor assigned label", "", "[assigned]", NULL, 20);
	buildingAssignedScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 268, 128, 16), "building editor", "building editor assigned scroll box", "", NULL, 20);
	buildingWorkerRatioLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 252, 128, 16), "building editor", "building editor worker ratio label", "", "[Worker Ratio]", NULL, 16);
	buildingWorkerRatioScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 268, 128, 16), "building editor", "building editor worker ratio scroll box", "", NULL, 20);
	buildingExplorerRatioLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 252, 128, 16), "building editor", "building editor explorer ratio label", "", "[Explorer Ratio]", NULL, 16);
	buildingExplorerRatioScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 268, 128, 16), "building editor", "building editor explorer ratio scroll box", "", NULL, 20);
	buildingWarriorRatioLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 252, 128, 16), "building editor", "building editor warrior ratio label", "", "[Warrior Ratio]", NULL, 16);
	buildingWarriorRatioScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 268, 128, 16), "building editor", "building editor warrior ratio scroll box", "", NULL, 20);
	buildingCherryLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 252, 128, 16), "building editor", "building editor cherry label", "", "[Cherry]", NULL, static_cast<Sint32*>(NULL));
	buildingCherryScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 268, 128, 16), "building editor", "building editor cherry scroll box", "", NULL, static_cast<Sint32*>(NULL));
	buildingOrangeLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 252, 128, 16), "building editor", "building editor orange label", "", "[Orange]", NULL, static_cast<Sint32*>(NULL));
	buildingOrangeScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 268, 128, 16), "building editor", "building editor orange scroll box", "", NULL, static_cast<Sint32*>(NULL));
	buildingPruneLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 252, 128, 16), "building editor", "building editor prune label", "", "[Prune]", NULL, static_cast<Sint32*>(NULL));
	buildingPruneScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 268, 128, 16), "building editor", "building editor prune scroll box", "", NULL, static_cast<Sint32*>(NULL));
	buildingStoneLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 252, 128, 16), "building editor", "building editor stone label", "", "[Stone]", NULL, static_cast<Sint32*>(NULL));
	buildingStoneScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 268, 128, 16), "building editor", "building editor stone scroll box", "", NULL, static_cast<Sint32*>(NULL));
	buildingBulletsLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 252, 128, 16), "building editor", "building editor bullets label", "", "[Bullets]", NULL, static_cast<Sint32*>(NULL));
	buildingBulletsScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 268, 128, 16), "building editor", "building editor bullets scroll box", "", NULL, static_cast<Sint32*>(NULL));
	buildingMinimumLevelLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 252, 128, 16), "building editor", "building editor minimum level to flag label", "", "[Minimum Level To Flag]", NULL, 3);
	buildingMinimumLevelScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 268, 128, 16), "building editor", "building editor minimum level to flag scroll box", "", NULL, 3);
	buildingRadiusLabel = new FractionValueText(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 252, 128, 16), "building editor", "building editor range label", "", "[range]", NULL, static_cast<Sint32*>(NULL));
	buildingRadiusScrollBox = new ValueScrollBox(*this, widgetRectangle(globalContainer->gfx->getW()-128+8, 268, 128, 16), "building editor", "building editor range scroll box", "", NULL, static_cast<Sint32*>(NULL));
	addWidget(buildingInfoTitle);
	addWidget(buildingPicture);
	addWidget(buildingHPLabel);
	addWidget(buildingHPScrollBox);
	addWidget(buildingFoodQuantityLabel);
	addWidget(buildingFoodQuantityScrollBox);
	addWidget(buildingAssignedLabel);
	addWidget(buildingAssignedScrollBox);
	addWidget(buildingWorkerRatioLabel);
	addWidget(buildingWorkerRatioScrollBox);
	addWidget(buildingExplorerRatioLabel);
	addWidget(buildingExplorerRatioScrollBox);
	addWidget(buildingWarriorRatioLabel);
	addWidget(buildingWarriorRatioScrollBox);
	addWidget(buildingCherryLabel);
	addWidget(buildingCherryScrollBox);
	addWidget(buildingOrangeLabel);
	addWidget(buildingOrangeScrollBox);
	addWidget(buildingPruneLabel);
	addWidget(buildingPruneScrollBox);
	addWidget(buildingStoneLabel);
	addWidget(buildingStoneScrollBox);
	addWidget(buildingBulletsLabel);
	addWidget(buildingBulletsScrollBox);
	addWidget(buildingMinimumLevelLabel);
	addWidget(buildingMinimumLevelScrollBox);
	addWidget(buildingRadiusLabel);
	addWidget(buildingRadiusScrollBox);

	selectionName="";
	buildingLevel=0;
	brushType = NoBrush;
	enableOnlyGroup("building view");

	isDraggingMinimap=false;
	isDraggingZone=false;
	isDraggingTerrain=false;
	isDraggingDelete=false;
	isScrollDragging=false;
	isDraggingArea=false;
	isDraggingNoRessourceGrowthArea=false;

	lastPlacementX=-1;
	lastPlacementY=-1;

	showingMenuScreen=false;
	menuScreen = NULL;
	showingLoad=false;
	showingSave=false;
	showingScriptEditor=false;

	terrainType=TerrainSelector::NoTerrain;

	teamViewSelectorKeys.push_back("[human]");
	teamViewSelectorKeys.push_back("[ai]");


	placingUnit=NoUnit;
	placingUnitLevel=0;

	selectedUnitGID=NOGUID;
	selectedBuildingGID=NOGBID;

	areaName=NULL;
	isShowingAreaName=false;
}



MapEdit::~MapEdit()
{
	Toolkit::releaseSprite("data/gui/editor");
	for(std::vector<MapEditorWidget*>::iterator i=mew.begin(); i!=mew.end(); ++i)
	{
		delete *i;
	}
}


bool MapEdit::load(const char *filename)
{
	assert(filename);

	InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(filename));
	if (stream->isEndOfStream())
	{
		std::cerr << "MapEdit::load(\"" << filename << "\") : error, can't open file." << std::endl;
		delete stream;
		return false;
	}
	else
	{
		bool rv = game.load(stream);
		
		delete stream;
		if (!rv)
			return false;
		
		// set the editor default values
		team = 0;
	
		if(game.teams[0])
			teamInfo1->setSelectionPos(game.teams[0]->type);
		if(game.teams[1])
			teamInfo2->setSelectionPos(game.teams[1]->type);
		if(game.teams[2])
			teamInfo3->setSelectionPos(game.teams[2]->type);
		if(game.teams[3])
			teamInfo4->setSelectionPos(game.teams[3]->type);
		if(game.teams[4])
			teamInfo5->setSelectionPos(game.teams[4]->type);
		if(game.teams[5])
			teamInfo6->setSelectionPos(game.teams[5]->type);
		if(game.teams[6])
			teamInfo7->setSelectionPos(game.teams[6]->type);
		if(game.teams[7])
			teamInfo8->setSelectionPos(game.teams[7]->type);
		if(game.teams[8])
			teamInfo9->setSelectionPos(game.teams[8]->type);
		if(game.teams[9])
			teamInfo10->setSelectionPos(game.teams[9]->type);
		if(game.teams[10])
			teamInfo11->setSelectionPos(game.teams[10]->type);
		if(game.teams[11])
			teamInfo12->setSelectionPos(game.teams[11]->type);

		renderMiniMap();
		areaNameLabel->setLabel(game.map.getAreaName(areaNumber->getIndex()));
		return true;
	}
}



bool MapEdit::save(const char *filename, const char *name)
{
	assert(filename);
	assert(name);

	OutputStream *stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(filename));
	if (stream->isEndOfStream())
	{
		std::cerr << "MapEdit::save(\"" << filename << "\",\"" << name << "\") : error, can't open file." << std::endl;
		delete stream;
		return false;
	}
	else
	{
		game.save(stream, true, name);
		delete stream;
		return true;
	}
}



int MapEdit::run(int sizeX, int sizeY, TerrainType terrainType)
{
	game.map.setSize(sizeX, sizeY, terrainType);
	game.map.setGame(&game);
	return run();
}



int MapEdit::run(void)
{
	//globalContainer->gfx->setRes(globalContainer->graphicWidth, globalContainer->graphicHeight , 32, globalContainer->graphicFlags, (DrawableSurface::GraphicContextType)globalContainer->settings.graphicType);

// 	regenerateClipRect();
	globalContainer->gfx->setClipRect();
	renderMiniMap();
	drawMenu();
	drawMap(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getW(), true, true);
	drawMiniMap();

	bool isRunning=true;
	int returnCode=0;
	Uint32 startTick, endTick, deltaTick;
	while (isRunning)
	{
		//SDL_Event event;
		startTick=SDL_GetTicks();
	
		// we get all pending events but for mousemotion we only keep the last one
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
 			returnCode=(processEvent(event) == -1) ? -1 : returnCode;
		}
			

		// redraw on scroll
// 		bool doRedraw=false;
		viewportX+=xSpeed;
		viewportY+=ySpeed;
		viewportX&=game.map.getMaskW();
		viewportY&=game.map.getMaskH();

		//special overrides here to allow for scrolling and painting terrain at the same time
		if(xSpeed!=0 || ySpeed!=0)
		{
			if(isDraggingZone)
				performAction("zone drag motion");
			else if(isDraggingTerrain)
				performAction("terrain drag motion");
		}
		
		drawMap(0, 0, globalContainer->gfx->getW()-0, globalContainer->gfx->getH(), true, true);
		drawMiniMap();
		wasMinimapRendered=false;
		drawMenu();
		drawWidgets();
		if(showingMenuScreen)
		{
			globalContainer->gfx->setClipRect();
			menuScreen->dispatchPaint();
			globalContainer->gfx->drawSurface((int)menuScreen->decX, (int)menuScreen->decY, menuScreen->getSurface());
		}
		if(showingLoad || showingSave)
		{
			globalContainer->gfx->setClipRect();
			loadSaveScreen->dispatchPaint();
			globalContainer->gfx->drawSurface((int)loadSaveScreen->decX, (int)loadSaveScreen->decY, loadSaveScreen->getSurface());
		}
		if(showingScriptEditor)
		{
			globalContainer->gfx->setClipRect();
			scriptEditor->dispatchPaint();
			globalContainer->gfx->drawSurface((int)scriptEditor->decX, (int)scriptEditor->decY, scriptEditor->getSurface());
		}
		if(isShowingAreaName)
		{
			globalContainer->gfx->setClipRect();
			areaName->dispatchPaint();
			globalContainer->gfx->drawSurface((int)areaName->decX, (int)areaName->decY, areaName->getSurface());
		}
		globalContainer->gfx->nextFrame();

		endTick=SDL_GetTicks();
		deltaTick=endTick-startTick;
		if (deltaTick<33)
			SDL_Delay(33-deltaTick);
		if (returnCode==-1)
		{
			isRunning=false;
		}
		if(doQuit)
			isRunning=false;
	}

	//globalContainer->gfx->setRes(globalContainer->graphicWidth, globalContainer->graphicHeight , 32, globalContainer->graphicFlags, (DrawableSurface::GraphicContextType)globalContainer->settings.graphicType);
	return returnCode;
}



void MapEdit::drawMap(int sx, int sy, int sw, int sh, bool needUpdate, bool doPaintEditMode)
{
// 	Utilities::rectClipRect(sx, sy, sw, sh, mapClip);

	globalContainer->gfx->setClipRect(sx, sy, sw, sh);

	game.drawMap(sx, sy, sw, sh, viewportX, viewportY, team, Game::DRAW_WHOLE_MAP | Game::DRAW_BUILDING_RECT | Game::DRAW_AREA | Game::DRAW_HEALTH_FOOD_BAR | Game::DRAW_SCRIPT_AREAS | Game::DRAW_NO_RESSOURCE_GROWTH_AREAS);
// 	if (doPaintEditMode)
// 		paintEditMode(false, false);

	if(widgetRectangle(sx, sy, sw, sh).is_in(mouseX, mouseY))
	{
		if(selectionMode==PlaceBuilding)
			drawBuildingSelectionOnMap();
		if(selectionMode==PlaceZone)
			brush.drawBrush(mouseX, mouseY);
		if(selectionMode==PlaceTerrain)
			brush.drawBrush(mouseX, mouseY, (terrainType>TerrainSelector::Water ? 0 : 1));
		if(selectionMode==PlaceUnit)
			drawPlacingUnitOnMap();
		if(selectionMode==RemoveObject)
			brush.drawBrush(mouseX, mouseY);
		if(selectionMode==EditingBuilding)
		{
			Building* selBuild=game.teams[Building::GIDtoTeam(selectedBuildingGID)]->myBuildings[Building::GIDtoID(selectedBuildingGID)];
			globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH());
			int centerX, centerY;
			game.map.buildingPosToCursor(selBuild->posXLocal, selBuild->posYLocal,  selBuild->type->width, selBuild->type->height, &centerX, &centerY, viewportX, viewportY);
			if (selBuild->owner->teamNumber==team)
				globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 0, 0, 190);
			else if ((game.teams[team]->allies) & (selBuild->owner->me))
				globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 255, 196, 0);
			else if (!selBuild->type->isVirtual)
				globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 190, 0, 0);
			globalContainer->gfx->setClipRect();
		}
		if(selectionMode==ChangeAreas)
		{
			brush.drawBrush(mouseX, mouseY);
		}
		if(selectionMode==ChangeNoRessourceGrowthAreas)
			brush.drawBrush(mouseX, mouseY);
	}

	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());
}



void MapEdit::drawMiniMap(void)
{
	game.drawMiniMap(globalContainer->gfx->getW()-128, 0, 128, 128, viewportX, viewportY, team);
// 	paintCoordinates();
}



void MapEdit::renderMiniMap(void)
{
	if(!wasMinimapRendered)
	{
		wasMinimapRendered=true;
		game.renderMiniMap(team, true);
	}
}



void MapEdit::drawMenu(void)
{
 	int menuStartW=globalContainer->gfx->getW()-128;
	int yposition=128;

	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		globalContainer->gfx->drawFilledRect(menuStartW, yposition, 128, globalContainer->gfx->getH()-128, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(menuStartW, yposition, 128, globalContainer->gfx->getH()-128, 0, 0, 40, 180);

	drawMenuEyeCandy();
}



void MapEdit::drawBuildingSelectionOnMap()
{
	if (selectionName!="")
	{
		// we get the type of building
		int typeNum=globalContainer->buildingsTypes.getTypeNum(selectionName, buildingLevel, false);
		if(!isUpgradable(IntBuildingType::shortNumberFromType(selectionName)))
			typeNum = globalContainer->buildingsTypes.getTypeNum(selectionName, 0, false);
		BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
		Sprite *sprite = bt->gameSpritePtr;
		
		// we translate dimensions and situation
		int tempX, tempY;
		int mapX, mapY;
		bool isRoom;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);
		if (bt->isVirtual)
			isRoom = game.checkRoomForBuilding(tempX, tempY, bt, &mapX, &mapY, team);
		else
			isRoom = game.checkHardRoomForBuilding(tempX, tempY, bt, &mapX, &mapY);
			
		// modifiy highlight given room
// 		if (isRoom)
// 			highlightSelection = std::min(highlightSelection + 0.1f, 1.0f);
// / 		else
//  			highlightSelection = std::max(highlightSelection - 0.1f, 0.0f);
		
		// we get the screen dimensions of the building
		int batW = (bt->width)<<5;
		int batH = sprite->getH(bt->gameSpriteImage);
		int batX = (((mapX-viewportX)&(game.map.wMask))<<5);
		int batY = (((mapY-viewportY)&(game.map.hMask))<<5)-(batH-(bt->height<<5));
		
		// we draw the building
		sprite->setBaseColor(game.teams[team]->colorR, game.teams[team]->colorG, game.teams[team]->colorB);
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH());
// 		int spriteIntensity = 127+static_cast<int>(128.0f*splineInterpolation(1.f, 0.f, 1.f, highlightSelection));
	 	int spriteIntensity = 127;
		globalContainer->gfx->drawSprite(batX, batY, sprite, bt->gameSpriteImage, spriteIntensity);
		
		if (!bt->isVirtual)
		{
			if (game.teams[team]->noMoreBuildingSitesCountdown>0)
			{
				globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 0, 0, 127);
				globalContainer->gfx->drawLine(batX, batY, batX+batW-1, batY+batH-1, 255, 0, 0, 127);
				globalContainer->gfx->drawLine(batX+batW-1, batY, batX, batY+batH-1, 255, 0, 0, 127);
				
				globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 0, 0, 127));
				globalContainer->gfx->drawString(batX, batY-12, globalContainer->littleFont, FormatableString("%0.%1").arg(game.teams[team]->noMoreBuildingSitesCountdown/40).arg((game.teams[team]->noMoreBuildingSitesCountdown%40)/4).c_str());
				globalContainer->littleFont->popStyle();
			}
			else
			{
				if (isRoom)
					globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 255, 255, 127);
				else
					globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 0, 0, 127);
				
				// We look for its maximum extension size
				// we find last's level type num:
				BuildingType *lastbt=globalContainer->buildingsTypes.get(typeNum);
				int lastTypeNum=typeNum;
				int max=0;
				while (lastbt->nextLevel>=0)
				{
					lastTypeNum=lastbt->nextLevel;
					lastbt=globalContainer->buildingsTypes.get(lastTypeNum);
					if (max++>200)
					{
						printf("GameGUI: Error: nextLevel architecture is broken.\n");
						assert(false);
						break;
					}
				}
					
				int exMapX, exMapY; // ex prefix means EXtended building; the last level building type.
				bool isExtendedRoom = game.checkHardRoomForBuilding(tempX, tempY, lastbt, &exMapX, &exMapY);
				int exBatX=((exMapX-viewportX)&(game.map.wMask))<<5;
				int exBatY=((exMapY-viewportY)&(game.map.hMask))<<5;
				int exBatW=(lastbt->width)<<5;
				int exBatH=(lastbt->height)<<5;

				if (isRoom && isExtendedRoom)
					globalContainer->gfx->drawRect(exBatX-1, exBatY-1, exBatW+2, exBatH+2, 255, 255, 255, 127);
				else
					globalContainer->gfx->drawRect(exBatX-1, exBatY-1, exBatW+2, exBatH+2, 255, 0, 0, 127);
			}
		}

	}

}



bool MapEdit::isUpgradable(int buildingLevel)
{
	if(buildingLevel==IntBuildingType::SWARM_BUILDING)
		return false;
	if(buildingLevel==IntBuildingType::EXPLORATION_FLAG)
		return false;
	if(buildingLevel==IntBuildingType::WAR_FLAG)
		return false;
	if(buildingLevel==IntBuildingType::CLEARING_FLAG)
		return false;
	if(buildingLevel==IntBuildingType::STONE_WALL)
		return false;
	if(buildingLevel==IntBuildingType::MARKET_BUILDING)
		return false;
	return true;
}



void MapEdit::drawMenuEyeCandy()
{
	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());

	// bar background 
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-128, 16, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-128, 16, 0, 0, 40, 180);

	// draw window bar
	int pos=globalContainer->gfx->getW()-128-32;
	for (int i=0; i<pos; i+=32)
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
	int type;
	if(placingUnit==Worker)
		type=WORKER;
	else if(placingUnit==Warrior)
		type=WARRIOR;
	else if(placingUnit==Explorer)
		type=EXPLORER;
	
	int level=placingUnitLevel;
	
	int cx=(mouseX>>5)+viewportX;
	int cy=(mouseY>>5)+viewportY;

	int px=mouseX&0xFFFFFFE0;
	int py=mouseY&0xFFFFFFE0;
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
	unitSprite->setBaseColor(game.teams[team]->colorR, game.teams[team]->colorG, game.teams[team]->colorB);

	globalContainer->gfx->drawSprite(px, py, unitSprite, imgid);

	if (isRoom)
		globalContainer->gfx->drawRect(px, py, pw, ph, 255, 255, 255, 128);
	else
		globalContainer->gfx->drawRect(px, py, pw, ph, 255, 0, 0, 128);
}

int MapEdit::processEvent(SDL_Event& event)
{
	int returnCode=0;
	if (event.type==SDL_QUIT)
	{
		returnCode=-1;
	}
	else if(showingMenuScreen || showingLoad || showingSave || showingScriptEditor || isShowingAreaName)
	{
		delegateMenu(event);
		return 0;
	}
	else if(event.type==SDL_MOUSEMOTION)
	{
		mouseX=event.motion.x;
		mouseY=event.motion.y;
		relMouseX=event.motion.xrel;
		relMouseY=event.motion.yrel;
		if(isDraggingMinimap)
		{
			performAction("minimap drag motion", relMouseX, relMouseY);
			performAction("scroll horizontal stop", relMouseX, relMouseY);
			performAction("scroll vertical stop", relMouseX, relMouseY);
		}
		else if(isDraggingZone)
		{
			if(widgetRectangle(0, 16, globalContainer->gfx->getW()-128, globalContainer->gfx->getH()-16).is_in(mouseX, mouseY))
				performAction("zone drag motion", relMouseX, relMouseY);
		}
		else if(isDraggingTerrain)
		{
			if(widgetRectangle(0, 16, globalContainer->gfx->getW()-128, globalContainer->gfx->getH()-16).is_in(mouseX, mouseY))
				performAction("terrain drag motion", relMouseX, relMouseY);
		}
		else if(isScrollDragging)
		{
			performAction("scroll drag motion", relMouseX, relMouseY);
		}
		else if(isDraggingDelete)
		{
			performAction("delete drag motion", relMouseX, relMouseY);
		}
		else if(isDraggingArea)
		{
			performAction("area drag motion", relMouseX, relMouseY);
		}
		else if(isDraggingNoRessourceGrowthArea)
		{
			performAction("no ressource growth area drag motion", relMouseX, relMouseY);
		}
		else
		{
			if(globalContainer->gfx->getW()-event.motion.x<15)
			{
				performAction("scroll right", relMouseX, relMouseY);
			}
			else if(event.motion.x<15)
			{
				performAction("scroll left", relMouseX, relMouseY);
			}
			else if(xSpeed!=0)
			{
				performAction("scroll horizontal stop", relMouseX, relMouseY);
			}
	
			if(globalContainer->gfx->getH()-event.motion.y<15)
			{
				performAction("scroll down", relMouseX, relMouseY);
			}
			else if(event.motion.y<15)
			{
				performAction("scroll up", relMouseX, relMouseY);
			}
			else if(ySpeed!=0)
			{
				performAction("scroll vertical stop", relMouseX, relMouseY);
			}
		}
	}
	else if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT)
	{
		if(!findAction(event.button.x, event.button.y) && widgetRectangle(0, 16, globalContainer->gfx->getW()-128, globalContainer->gfx->getH()).is_in(mouseX, mouseY))
		{
			//The button wasn't clicked in any registered area
			if(selectionMode==PlaceBuilding)
				performAction("place building");
			else if(selectionMode==PlaceZone)
				performAction("zone drag start");
			else if(selectionMode==PlaceTerrain)
				performAction("terrain drag start");
			else if(selectionMode==PlaceUnit)
				performAction("place unit");
			else if(selectionMode==RemoveObject)
				performAction("delete drag start");
			else if(selectionMode==ChangeAreas)
				performAction("area drag start");
			else if(selectionMode==ChangeNoRessourceGrowthAreas)
				performAction("no ressource growth area drag start");
			else
			{
				performAction("select map unit");
				performAction("select map building");
			}
		}
		else if(widgetRectangle(globalContainer->gfx->getW()-128+14, 14, 100, 100).is_in(mouseX, mouseY))
			performAction("minimap drag start");
	}
	else if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_RIGHT)
	{
		if(selectionMode==PlaceNothing || selectionMode==EditingUnit || selectionMode==EditingBuilding)
			performAction("change menu");
		if(selectionMode!=PlaceNothing)
			performAction("unselect");
	}
	else if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_MIDDLE)
	{
		performAction("scroll drag start");
	}
	else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)
	{
		if(isDraggingMinimap)
			performAction("minimap drag stop");
		if(isDraggingZone)
			performAction("zone drag end");
		if(isDraggingTerrain)
			performAction("terrain drag end");
		if(isDraggingDelete)
			performAction("delete drag end");
		if(isDraggingArea)
			performAction("area drag end");
		if(isDraggingNoRessourceGrowthArea)
			performAction("no ressource growth area drag end");
	}
	else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_MIDDLE)
	{
		if(isScrollDragging)
			performAction("scroll drag stop");
	}
	else if(event.type==SDL_KEYDOWN)
	{
		handleKeyPressed(event.key.keysym.sym, true);
	}
	else if(event.type==SDL_KEYUP)
	{
		handleKeyPressed(event.key.keysym.sym, false);
	}
	return returnCode;
}



void MapEdit::handleKeyPressed(SDLKey key, bool pressed)
{
	switch(key)
	{
		case SDLK_ESCAPE:
			if(pressed)
			{
				if (showingMenuScreen==false)
					performAction("open menu screen");
				else if (showingMenuScreen==true)
					performAction("close menu screen");		
			}
			break;
		case SDLK_UP:
			if(pressed)
				performAction("scroll up");
			else
				performAction("scroll vertical stop");
			break;
		case SDLK_DOWN:
			if(pressed)
				performAction("scroll down");
			else
				performAction("scroll vertical stop");
			break;
		case SDLK_LEFT:
			if(pressed)
				performAction("scroll left");
			else
				performAction("scroll horizontal stop");
			break;
		case SDLK_RIGHT:
			if(pressed)
				performAction("scroll right");
			else
				performAction("scroll horizontal stop");
			break;
		case SDLK_a :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["akey"]);
			break;
		case SDLK_b :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["bkey"]);
			break;
		case SDLK_c :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["ckey"]);
			break;
		case SDLK_d :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["dkey"]);
			break;
		case SDLK_e :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["ekey"]);
			break;
		case SDLK_f :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["fkey"]);
			break;
		case SDLK_g :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["gkey"]);
			break;
		case SDLK_h :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["hkey"]);
			break;
		case SDLK_i :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["ikey"]);
			break;
		case SDLK_j :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["jkey"]);
			break;
		case SDLK_k :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["kkey"]);
			break;
		case SDLK_l :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["lkey"]);
			break;
		case SDLK_m :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["mkey"]);
			break;
		case SDLK_n :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["nkey"]);
			break;
		case SDLK_o :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["okey"]);
			break;
		case SDLK_p :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["pkey"]);
			break;
		case SDLK_q :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["qkey"]);
			break;
		case SDLK_r :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["rkey"]);
			break;
		case SDLK_s :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["skey"]);
			break;
		case SDLK_t :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["tkey"]);
			break;
		case SDLK_u :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["ukey"]);
			break;
		case SDLK_v :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["vkey"]);
			break;
		case SDLK_w :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["wkey"]);
			break;
		case SDLK_x :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["xkey"]);
			break;
		case SDLK_y :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["ykey"]);
			break;
		case SDLK_z :
			if(pressed)
				performAction(globalContainer->settings.editor_keyboard_shortcuts["zkey"]);
			break;
		default:
		break;
	}
}



void MapEdit::performAction(const std::string& action, int relMouseX, int relMouseY)
{
//	std::cout<<action<<std::endl;
	if(action.find("&")!=std::string::npos)
	{
		int pos=action.find("&");
		performAction(action.substr(0, pos));
		performAction(action.substr(pos+1, action.size()-pos-1));
	}
	if (action=="scroll left")
	{
		xSpeed=-1;
	}
	else if(action=="scroll right")
	{
		xSpeed=1;
	}
	else if(action=="scroll horizontal stop")
	{
		xSpeed=0;
	}
	else if(action=="scroll up")
	{
		ySpeed=-1;
	}
	else if(action=="scroll down")
	{
		ySpeed=1;
	}
	else if(action=="scroll vertical stop")
	{
		ySpeed=0;
	}
	else if(action=="scroll drag start")
	{
		isScrollDragging=true;
	}
	else if(action=="scroll drag motion")
	{
		viewportX+=relMouseX;
		viewportY+=relMouseY;
		viewportX&=game.map.getMaskW();
		viewportY&=game.map.getMaskH();
	}
	else if(action=="scroll drag stop")
	{
		isScrollDragging=false;
	}
	else if(action=="switch to building view")
	{
		panelMode=AddBuildings;
		enableOnlyGroup("building view");
		performAction("unselect");
	}
	else if(action=="switch to flag view")
	{
		panelMode=AddFlagsAndZones;
		enableOnlyGroup("flag view");
		performAction("unselect");
	}
	else if(action=="switch to terrain view")
	{
		panelMode=Terrain;
		enableOnlyGroup("terrain view");
		performAction("unselect");
	}
	else if(action=="switch to teams view")
	{
		panelMode=Teams;
		enableOnlyGroup("teams view");
		performAction("unselect");
	}
	else if(action.substr(0, 29)=="set place building selection ")
	{
		performAction("unselect");
		std::string type=action.substr(29, action.size()-29);
		selectionName=type;
		selectionMode=PlaceBuilding;
	}
	else if(action=="unselect")
	{
		selectionName="";
		brush.unselect();
		selectionMode=PlaceNothing;
		brushType=NoBrush;
		terrainType=TerrainSelector::NoTerrain;
		placingUnit=NoUnit;
		selectedUnitGID=NOGUID;
		game.selectedUnit=NULL;
		deleteButton->setUnselected();
		areasButton->setUnselected();
		noRessourceGrowthButton->setUnselected();
		if(panelMode==UnitEditor)
			performAction("switch to building view");
	}
	else if(action=="change menu")
	{
		if(panelMode==AddBuildings)
			performAction("switch to flag view");
		else if(panelMode==AddFlagsAndZones)
			performAction("switch to terrain view");
		else if(panelMode==Terrain)
			performAction("switch to teams view");
		else if(panelMode==Teams)
			performAction("switch to building view");
		else
			performAction("switch to building view");
	}
	else if(action=="minimap drag start")
	{
		isDraggingMinimap=true;
		minimapMouseToPos(mouseX-globalContainer->gfx->getW()+128, mouseY, &viewportX, &viewportY, true);
	}
	else if(action=="minimap drag motion")
	{
		minimapMouseToPos(mouseX-globalContainer->gfx->getW()+128, mouseY, &viewportX, &viewportY, true);
	}
	else if(action=="minimap drag stop")
	{
		isDraggingMinimap=false;
	}
	else if(action=="place building")
	{
		int typeNum=globalContainer->buildingsTypes.getTypeNum(selectionName, buildingLevel, false);
		if(!isUpgradable(IntBuildingType::shortNumberFromType(selectionName)))
			typeNum = globalContainer->buildingsTypes.getTypeNum(selectionName, 0, false);
		BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
		int tempX, tempY, x, y;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);

		if (game.checkRoomForBuilding(tempX, tempY, bt, &x, &y, team, false))
		{
			game.addBuilding(x, y, typeNum, team);
			if (selectionName=="swarm")
			{
				if (game.teams[team]->startPosSet<3)
				{
					game.teams[team]->startPosX=tempX;
					game.teams[team]->startPosY=tempY;
					game.teams[team]->startPosSet=3;
				}
			}
			else
			{
				if (game.teams[team]->startPosSet<2)
				{
					game.teams[team]->startPosX=tempX;
					game.teams[team]->startPosY=tempY;
					game.teams[team]->startPosSet=2;
				}
			}
			game.regenerateDiscoveryMap();
			hasMapBeenModified = true;
			renderMiniMap();
		}
	}
	else if(action=="switch to building level 1")
	{
		buildingLevel=0;
	}
	else if(action=="switch to building level 2")
	{
		buildingLevel=1;
	}
	else if(action=="switch to building level 3")
	{
		buildingLevel=2;
	}
	else if(action=="open menu screen")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		menuScreen=new MapEditMenuScreen;
		showingMenuScreen=true;
	}
	else if(action=="close menu screen")
	{
		delete menuScreen;
		menuScreen=NULL;
		showingMenuScreen=false;
	}
	else if(action=="open load screen")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		loadSaveScreen=new LoadSaveScreen("maps", "map", true, game.session.getMapNameC(), glob2FilenameToName, glob2NameToFilename);
		showingLoad=true;
	}
	else if(action=="close load screen")
	{
		delete loadSaveScreen;
		showingLoad=false;
		loadSaveScreen=NULL;
	}
	else if(action=="open save screen")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		loadSaveScreen=new LoadSaveScreen("maps", "map", false, game.session.getMapNameC(), glob2FilenameToName, glob2NameToFilename);
		showingSave=true;
	}
	else if(action=="close save screen")
	{
		delete loadSaveScreen;
		showingSave=false;
		loadSaveScreen=NULL;
	}
	else if(action=="open script editor")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		scriptEditor=new ScriptEditorScreen(&game.script, &game);
		showingScriptEditor=true;
	}
	else if(action=="close script editor")
	{
		delete scriptEditor;
		showingScriptEditor=false;
		scriptEditor=NULL;
	}
	else if(action=="open area name")
	{
		performAction("unselect");
		performAction("scroll horizontal stop");
		performAction("scroll vertical stop");
		areaName=new AskForTextInput("[Change Area Name]", game.map.getAreaName(areaNumber->getIndex()));
		isShowingAreaName=true;
	}
	else if(action=="close area name")
	{
		game.map.setAreaName(areaNumber->getIndex(), areaName->getText());
		performAction("update script area number");
		delete areaName;
		isShowingAreaName=false;
		areaName=NULL;
	}
	else if(action=="select forbidden zone")
	{
		performAction("unselect");
		brushType = ForbiddenBrush;
		selectionMode=PlaceZone;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select clearing zone")
	{
		performAction("unselect");
		brushType = ClearAreaBrush;
		selectionMode=PlaceZone;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select guard zone")
	{
		performAction("unselect");
		brushType = GuardAreaBrush;
		selectionMode=PlaceZone;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="handle zone click")
	{
		if(brushType==NoBrush)
		{
			performAction("unselect");
			performAction("select forbidden zone");
		}
		brush.handleClick(relMouseX, relMouseY);
	}
	else if(action=="zone drag start")
	{
		isDraggingZone=true;
		handleBrushClick(mouseX, mouseY);
	}
	else if(action=="zone drag motion")
	{
		handleBrushClick(mouseX, mouseY);
	}
	else if(action=="zone drag end")
	{
		isDraggingZone=false;
		lastPlacementX=-1;
		lastPlacementY=-1;
	}
	else if(action=="select grass")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Grass;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select sand")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Sand;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select water")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Water;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select wheat")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Wheat;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select trees")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Trees;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select stone")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Stone;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select algae")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Algae;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select papyrus")
	{
		performAction("unselect");
		terrainType=TerrainSelector::Papyrus;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select cherry tree")
	{
		performAction("unselect");
		terrainType=TerrainSelector::CherryTree;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select orange tree")
	{
		performAction("unselect");
		terrainType=TerrainSelector::OrangeTree;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select prune tree")
	{
		performAction("unselect");
		terrainType=TerrainSelector::PruneTree;
		selectionMode=PlaceTerrain;
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select delete objects")
	{
		performAction("unselect");
		selectionMode=RemoveObject;
		deleteButton->setSelected();
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="select no ressources growth")
	{
		performAction("unselect");
		selectionMode=ChangeNoRessourceGrowthAreas;
		noRessourceGrowthButton->setSelected();
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="handle terrain click")
	{
		if(terrainType==TerrainSelector::NoTerrain && selectionMode!=RemoveObject && selectionMode!=ChangeAreas && selectionMode!=ChangeNoRessourceGrowthAreas)
			performAction("select grass");
		brush.handleClick(relMouseX, relMouseY);
		renderMiniMap();
	}
	else if(action=="terrain drag start")
	{
		isDraggingTerrain=true;
		handleTerrainClick(mouseX, mouseY);
	}
	else if(action=="terrain drag motion")
	{
		handleTerrainClick(mouseX, mouseY);
	}
	else if(action=="terrain drag end")
	{
		isDraggingTerrain=false;
		lastPlacementX=-1;
		lastPlacementY=-1;
	}
	else if(action=="delete drag start")
	{
		isDraggingDelete=true;
		handleDeleteClick(mouseX, mouseY);
	}
	else if(action=="delete drag motion")
	{
		handleDeleteClick(mouseX, mouseY);
	}
	else if(action=="delete drag end")
	{
		isDraggingDelete=false;
		lastPlacementX=-1;
		lastPlacementY=-1;
	}
	else if(action=="update script area number")
	{
		areaNameLabel->setLabel(game.map.getAreaName(areaNumber->getIndex()));
	}
	else if(action=="select change areas")
	{
		performAction("unselect");
		selectionMode=ChangeAreas;
		areasButton->setSelected();
		if (brush.getType() == BrushTool::MODE_NONE)
			brush.defaultSelection();
	}
	else if(action=="area drag start")
	{
		isDraggingArea=true;
		handleAreaClick(mouseX, mouseY);
	}
	else if(action=="area drag motion")
	{
		handleAreaClick(mouseX, mouseY);
	}
	else if(action=="area drag end")
	{
		isDraggingArea=false;
		lastPlacementX=-1;
		lastPlacementY=-1;
	}
	else if(action=="no ressource growth area drag start")
	{
		isDraggingNoRessourceGrowthArea=true;
		handleNoRessourceGrowthClick(mouseX, mouseY);
	}
	else if(action=="no ressource growth area drag motion")
	{
		handleNoRessourceGrowthClick(mouseX, mouseY);
	}
	else if(action=="no ressource growth area drag end")
	{
		isDraggingNoRessourceGrowthArea=false;
		lastPlacementX=-1;
		lastPlacementY=-1;
	}
	else if(action=="add team")
	{
		if(game.session.numberOfTeam < 12)
			game.addTeam();
		renderMiniMap();
	}
	else if(action=="remove team")
	{
		if(game.session.numberOfTeam > 1)
		{
			if(team==game.session.numberOfTeam-1)
				team-=1;
			game.removeTeam();
		}
		renderMiniMap();
	}
	else if(action.substr(0, 17)=="change team info ")
	{
		std::string snumber=action.substr(17, action.size()-17);
		int number=0;
		std::stringstream s;
		s<<snumber;
		s>>number;
		int selectionPos=0;
		if(number==1)
			selectionPos=teamInfo1->getSelectionPos();
		if(number==2)
			selectionPos=teamInfo2->getSelectionPos();
		if(number==3)
			selectionPos=teamInfo3->getSelectionPos();
		if(number==4)
			selectionPos=teamInfo4->getSelectionPos();
		if(number==5)
			selectionPos=teamInfo5->getSelectionPos();
		if(number==6)
			selectionPos=teamInfo6->getSelectionPos();
		if(number==7)
			selectionPos=teamInfo7->getSelectionPos();
		if(number==8)
			selectionPos=teamInfo8->getSelectionPos();
		if(number==9)
			selectionPos=teamInfo9->getSelectionPos();
		if(number==10)
			selectionPos=teamInfo10->getSelectionPos();
		if(number==11)
			selectionPos=teamInfo11->getSelectionPos();
		if(number==12)
			selectionPos=teamInfo12->getSelectionPos();
		if(selectionPos==0)
			game.teams[number-1]->type=BaseTeam::T_HUMAN;
		if(selectionPos==1)
			game.teams[number-1]->type=BaseTeam::T_AI;
	}
	else if(action=="select active team")
	{
		int n=relMouseX/16 + (relMouseY/16)*6;
		if(game.teams[n])
		{
			team=n;
			renderMiniMap();
			game.map.computeLocalForbidden(team);
			game.map.computeLocalClearArea(team);
			game.map.computeLocalGuardArea(team);
		}
	}
	else if(action=="select worker")
	{
		performAction("unselect");
		placingUnit=Worker;
		selectionMode=PlaceUnit;
	}
	else if(action=="select warrior")
	{
		performAction("unselect");
		placingUnit=Warrior;
		selectionMode=PlaceUnit;
	}
	else if(action=="select explorer")
	{
		performAction("unselect");
		placingUnit=Explorer;
		selectionMode=PlaceUnit;
	}
	else if(action=="select unit level 1")
	{
		placingUnitLevel=0;
	}
	else if(action=="select unit level 2")
	{
		placingUnitLevel=1;
	}
	else if(action=="select unit level 3")
	{
		placingUnitLevel=2;
	}
	else if(action=="select unit level 4")
	{
		placingUnitLevel=3;
	}
	else if(action=="place unit")
	{
		int type;
		if(placingUnit==Worker)
			type=WORKER;
		else if(placingUnit==Warrior)
			type=WARRIOR;
		else if(placingUnit==Explorer)
			type=EXPLORER;
		int level=placingUnitLevel;

		int x;
		int y;
		game.map.displayToMapCaseAligned(mouseX, mouseY, &x, &y, viewportX, viewportY);

		Unit *unit=game.addUnit(x, y, team, type, level, rand()%256, 0, 0);
		if (unit)
		{
			if (game.teams[team]->startPosSet<1)
			{
				game.teams[team]->startPosX=viewportX;
				game.teams[team]->startPosY=viewportY;
				game.teams[team]->startPosSet=1;
			}
			game.regenerateDiscoveryMap();
		}
		renderMiniMap();
	}
	else if(action=="select map unit")
	{
		int x;
		int y;
		int gid=NOGUID;
		game.map.displayToMapCaseAligned(mouseX, mouseY, &x, &y, viewportX, viewportY);
		if(game.map.getAirUnit(x, y)!=NOGUID)
		{
			gid=game.map.getAirUnit(x, y);
		}
		else if(game.map.getGroundUnit(x, y)!=NOGUID)
		{
			gid=game.map.getGroundUnit(x, y);
		}
		if(gid!=NOGUID)
		{
			performAction("unselect");
			selectedUnitGID=gid;
			game.selectedUnit=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
			selectionMode=EditingUnit;
			panelMode=UnitEditor;
			unitInfoTitle->setUnit(game.selectedUnit);
			unitPicture->setUnit(game.selectedUnit);
			unitHPLabel->setValues(&game.selectedUnit->hp, &game.selectedUnit->performance[HP]);
			unitHPScrollBox ->setValues(&game.selectedUnit->hp, &game.selectedUnit->performance[HP]);
			unitWalkLevelLabel->setValues(&game.selectedUnit->level[WALK]);
			unitWalkLevelScrollBox->setValues(&game.selectedUnit->level[WALK]);
			unitSwimLevelLabel->setValues(&game.selectedUnit->level[SWIM]);
			unitSwimLevelScrollBox->setValues(&game.selectedUnit->level[SWIM]);
			unitHarvestLevelLabel->setValues(&game.selectedUnit->level[HARVEST]);
			unitHarvestLevelScrollBox->setValues(&game.selectedUnit->level[HARVEST]);
			unitBuildLevelLabel->setValues(&game.selectedUnit->level[BUILD]);
			unitBuildLevelScrollBox->setValues(&game.selectedUnit->level[BUILD]);
			unitAttackSpeedLevelLabel->setValues(&game.selectedUnit->level[ATTACK_SPEED]);
			unitAttackSpeedLevelScrollBox->setValues(&game.selectedUnit->level[ATTACK_SPEED]);
			unitAttackStrengthLevelLabel->setValues(&game.selectedUnit->level[ATTACK_STRENGTH]);
			unitAttackStrengthLevelScrollBox->setValues(&game.selectedUnit->level[ATTACK_STRENGTH]);
			unitMagicGroundAttackLevelLabel->setValues(&game.selectedUnit->level[MAGIC_ATTACK_GROUND]);
			unitMagicGroundAttackLevelScrollBox->setValues(&game.selectedUnit->level[MAGIC_ATTACK_GROUND]);
			enableOnlyGroup("unit editor");
			if(!game.selectedUnit->canLearn[WALK])
			{
				unitWalkLevelLabel->disable();
				unitWalkLevelScrollBox->disable();
			}
			if(!game.selectedUnit->canLearn[SWIM])
			{
				unitSwimLevelLabel->disable();
				unitSwimLevelScrollBox->disable();
			}
			if(!game.selectedUnit->canLearn[HARVEST])
			{
				unitHarvestLevelLabel->disable();
				unitHarvestLevelScrollBox->disable();
			}
			if(!game.selectedUnit->canLearn[BUILD])
			{
				unitBuildLevelLabel->disable();
				unitBuildLevelScrollBox->disable();
			}
			if(!game.selectedUnit->canLearn[ATTACK_SPEED])
			{
				unitAttackSpeedLevelLabel->disable();
				unitAttackSpeedLevelScrollBox->disable();
			}
			if(!game.selectedUnit->canLearn[ATTACK_STRENGTH])
			{
				unitAttackStrengthLevelLabel->disable();
				unitAttackStrengthLevelScrollBox->disable();
			}
			if(!game.selectedUnit->canLearn[MAGIC_ATTACK_GROUND])
			{
				unitMagicGroundAttackLevelLabel->disable();
				unitMagicGroundAttackLevelScrollBox->disable();
			}
		}
	}
	else if(action=="update unit walk level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[WALK]);
		u->performance[WALK] = ut->performance[WALK];
	}
	else if(action=="update unit swim level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[SWIM]);
		u->performance[SWIM] = ut->performance[SWIM];
	}
	else if(action=="update unit harvest level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[HARVEST]);
		u->performance[HARVEST] = ut->performance[HARVEST];
	}
	else if(action=="update unit build level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[BUILD]);
		u->performance[BUILD] = ut->performance[BUILD];
	}
	else if(action=="update unit attack speed level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[ATTACK_SPEED]);
		u->performance[ATTACK_SPEED] = ut->performance[ATTACK_SPEED];
	}
	else if(action=="update unit attack strength level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[ATTACK_STRENGTH]);
		u->performance[ATTACK_STRENGTH] = ut->performance[ATTACK_STRENGTH];
	}
	else if(action=="update unit magic ground attack level")
	{
		Unit* u=game.teams[Unit::GIDtoTeam(selectedUnitGID)]->myUnits[Unit::GIDtoID(selectedUnitGID)];
		UnitType *ut = u->race->getUnitType(u->typeNum, u->level[MAGIC_ATTACK_GROUND]);
		u->performance[MAGIC_ATTACK_GROUND] = ut->performance[MAGIC_ATTACK_GROUND];
	}
	else if(action=="select map building")
	{
		int x;
		int y;
		game.map.displayToMapCaseAligned(mouseX, mouseY, &x, &y, viewportX, viewportY);
		int gid=NOGBID;
		for(int t=0; t<32; ++t)
		{
			if(game.teams[t] && gid==NOGBID)
			{
				for (std::list<Building *>::iterator virtualIt=game.teams[t]->virtualBuildings.begin();
						virtualIt!=game.teams[t]->virtualBuildings.end(); ++virtualIt)
				{
					{
						Building *b=*virtualIt;
						if ((b->posX==x) && (b->posY==y))
						{
							gid=b->gid;
							break;
						}
					}
				}
			}
		}
		if(gid==NOGBID && game.map.getBuilding(x, y)!=NOGUID)
		{
			gid=game.map.getBuilding(x, y);
		}
		if(gid!=NOGBID)
		{
			performAction("unselect");
			Building* b=game.teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)];
			selectionMode=EditingBuilding;
			panelMode=BuildingEditor;
			selectedBuildingGID=gid;
			enableOnlyGroup("building editor");
			buildingInfoTitle->setBuilding(b);
			buildingPicture->setBuilding(b);
			bool hpLabel=false;
			buildingHPLabel->setValues(&b->hp, &b->type->hpMax);
			buildingHPScrollBox->setValues(&b->hp, &b->type->hpMax);
			bool foodLabel=false;
			buildingFoodQuantityLabel->setValues(&b->ressources[CORN], &b->type->maxRessource[CORN]);
			buildingFoodQuantityScrollBox->setValues(&b->ressources[CORN], &b->type->maxRessource[CORN]);
			bool assignedLabel=false;
			buildingAssignedLabel->setValues(&b->maxUnitWorking);
			buildingAssignedScrollBox->setValues(&b->maxUnitWorking);
			bool workerRatioLabel=false;
			buildingWorkerRatioLabel->setValues(&b->ratio[WORKER]);
			buildingWorkerRatioScrollBox->setValues(&b->ratio[WORKER]);
			bool explorerRatioLabel=false;
			buildingExplorerRatioLabel->setValues(&b->ratio[EXPLORER]);
			buildingExplorerRatioScrollBox->setValues(&b->ratio[EXPLORER]);
			bool warriorRatioLabel=false;
			buildingWarriorRatioLabel->setValues(&b->ratio[WARRIOR]);
			buildingWarriorRatioScrollBox->setValues(&b->ratio[WARRIOR]);
			bool cherryLabel=false;
			buildingCherryLabel->setValues(&b->ressources[CHERRY], &b->type->maxRessource[CHERRY]);
			buildingCherryScrollBox->setValues(&b->ressources[CHERRY], &b->type->maxRessource[CHERRY]);
			bool orangeLabel=false;
			buildingOrangeLabel->setValues(&b->ressources[ORANGE], &b->type->maxRessource[ORANGE]);
			buildingOrangeScrollBox->setValues(&b->ressources[ORANGE], &b->type->maxRessource[ORANGE]);
			bool pruneLabel=false;
			buildingPruneLabel->setValues(&b->ressources[PRUNE], &b->type->maxRessource[PRUNE]);
			buildingPruneScrollBox->setValues(&b->ressources[PRUNE], &b->type->maxRessource[PRUNE]);
			bool stoneLabel=false;
			buildingStoneLabel->setValues(&b->ressources[STONE], &b->type->maxRessource[STONE]);
			buildingStoneScrollBox->setValues(&b->ressources[STONE], &b->type->maxRessource[STONE]);
			bool bulletsLabel=false;
			buildingBulletsLabel->setValues(&b->bullets, &b->type->maxBullets);
			buildingBulletsScrollBox->setValues(&b->bullets, &b->type->maxBullets);
			bool minimumLevel=false;
			buildingMinimumLevelLabel->setValues(&b->minLevelToFlag);
			buildingMinimumLevelScrollBox->setValues(&b->minLevelToFlag);
			bool radius=false;
			buildingRadiusLabel->setValues(&b->unitStayRange, &b->type->maxUnitStayRange);
			buildingRadiusScrollBox->setValues(&b->unitStayRange, &b->type->maxUnitStayRange);
			if(b->shortTypeNum==IntBuildingType::SWARM_BUILDING)
			{
				hpLabel=true;
				foodLabel=true;
				assignedLabel=true;
				workerRatioLabel=true;
				explorerRatioLabel=true;
				warriorRatioLabel=true;
			}
			if(b->shortTypeNum==IntBuildingType::FOOD_BUILDING)
			{
				hpLabel=true;
				foodLabel=true;
				assignedLabel=true;
			}
			if(b->shortTypeNum==IntBuildingType::HEAL_BUILDING)
			{
				hpLabel=true;
			}
			if(b->shortTypeNum==IntBuildingType::WALKSPEED_BUILDING)
			{
				hpLabel=true;
			}
			if(b->shortTypeNum==IntBuildingType::SWIMSPEED_BUILDING)
			{
				hpLabel=true;
			}
			if(b->shortTypeNum==IntBuildingType::ATTACK_BUILDING)
			{
				hpLabel=true;
			}
			if(b->shortTypeNum==IntBuildingType::SCIENCE_BUILDING)
			{
				hpLabel=true;
			}
			if(b->shortTypeNum==IntBuildingType::DEFENSE_BUILDING)
			{
				hpLabel=true;
				assignedLabel=true;
				stoneLabel=true;
				bulletsLabel=true;
			}
			if(b->shortTypeNum==IntBuildingType::EXPLORATION_FLAG)
			{
				assignedLabel=true;
				radius=true;
			}
			if(b->shortTypeNum==IntBuildingType::WAR_FLAG)
			{
				assignedLabel=true;
				minimumLevel=true;
				radius=true;
			}
			if(b->shortTypeNum==IntBuildingType::CLEARING_FLAG)
			{
				assignedLabel=true;
				minimumLevel=true;
				radius=true;
			}
			if(b->shortTypeNum==IntBuildingType::STONE_WALL)
			{
				hpLabel=true;
			}
			if(b->shortTypeNum==IntBuildingType::MARKET_BUILDING)
			{
				hpLabel=true;
				assignedLabel=true;
				cherryLabel=true;
				orangeLabel=true;
				pruneLabel=true;
			}

			int ypos=252;
			if(!hpLabel)
			{
				buildingHPLabel->disable();
				buildingHPScrollBox->disable();
			}
			else
			{
				buildingHPLabel->area.y=ypos;
				buildingHPScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!foodLabel)
			{
				buildingFoodQuantityLabel->disable();
				buildingFoodQuantityScrollBox->disable();
			}
			else
			{
				buildingFoodQuantityLabel->area.y=ypos;
				buildingFoodQuantityScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!assignedLabel)
			{
				buildingAssignedLabel->disable();
				buildingAssignedScrollBox->disable();
			}
			else
			{
				buildingAssignedLabel->area.y=ypos;
				buildingAssignedScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!workerRatioLabel)
			{
				buildingWorkerRatioLabel->disable();
				buildingWorkerRatioScrollBox->disable();
			}
			else
			{
				buildingWorkerRatioLabel->area.y=ypos;
				buildingWorkerRatioScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!explorerRatioLabel)
			{
				buildingExplorerRatioLabel->disable();
				buildingExplorerRatioScrollBox->disable();
			}
			else
			{
				buildingExplorerRatioLabel->area.y=ypos;
				buildingExplorerRatioScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!warriorRatioLabel)
			{
				buildingWarriorRatioLabel->disable();
				buildingWarriorRatioScrollBox->disable();
			}
			else
			{
				buildingWarriorRatioLabel->area.y=ypos;
				buildingWarriorRatioScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!cherryLabel)
			{
				buildingCherryLabel->disable();
				buildingCherryScrollBox->disable();
			}
			else
			{
				buildingCherryLabel->area.y=ypos;
				buildingCherryScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!orangeLabel)
			{
				buildingOrangeLabel->disable();
				buildingOrangeScrollBox->disable();
			}
			else
			{
				buildingOrangeLabel->area.y=ypos;
				buildingOrangeScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!pruneLabel)
			{
				buildingPruneLabel->disable();
				buildingPruneScrollBox->disable();
			}
			else
			{
				buildingPruneLabel->area.y=ypos;
				buildingPruneScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!stoneLabel)
			{
				buildingStoneLabel->disable();
				buildingStoneScrollBox->disable();
			}
			else
			{
				buildingStoneLabel->area.y=ypos;
				buildingStoneScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!bulletsLabel)
			{
				buildingBulletsLabel->disable();
				buildingBulletsScrollBox->disable();
			}
			else
			{
				buildingBulletsLabel->area.y=ypos;
				buildingBulletsScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!minimumLevel)
			{
				buildingMinimumLevelLabel->disable();
				buildingMinimumLevelScrollBox->disable();
			}
			else
			{
				buildingMinimumLevelLabel->area.y=ypos;
				buildingMinimumLevelScrollBox->area.y=ypos+16;
				ypos+=32;
			}

			if(!radius)
			{
				buildingRadiusLabel->disable();
				buildingRadiusScrollBox->disable();
			}
			else
			{
				buildingRadiusLabel->area.y=ypos;
				buildingRadiusScrollBox->area.y=ypos+16;
				ypos+=32;
			}
		}
	}
	else if(action=="quit editor")
	{
		doQuit=true;
	}
}



void MapEdit::delegateMenu(SDL_Event& event)
{
	if(showingMenuScreen)
	{
		menuScreen->translateAndProcessEvent(&event);
		switch (menuScreen->endValue)
		{
			case MapEditMenuScreen::LOAD_MAP:
			{
				performAction("close menu screen");
				performAction("open load screen");
			}
			break;
			case MapEditMenuScreen::SAVE_MAP:
			{
				performAction("close menu screen");
				performAction("open save screen");
			}
			break;
			case MapEditMenuScreen::OPEN_SCRIPT_EDITOR:
			{
				performAction("close menu screen");
				performAction("open script editor");
			}
			case MapEditMenuScreen::RETURN_EDITOR:
			{
				performAction("close menu screen");
			}
			break;
			case MapEditMenuScreen::QUIT_EDITOR:
			{
				performAction("close menu screen");
				performAction("quit editor");
			}
			break;
		}
	}
	if(showingLoad)
	{
		loadSaveScreen->translateAndProcessEvent(&event);
		switch (loadSaveScreen->endValue)
		{
			case LoadSaveScreen::OK:
			{
				load(loadSaveScreen->getFileName());
				performAction("close load screen");
			}
			break;
			case LoadSaveScreen::CANCEL:
			{
				performAction("close load screen");
			}
			break;
		}
	}
	if(showingSave)
	{
		loadSaveScreen->translateAndProcessEvent(&event);
		switch (loadSaveScreen->endValue)
		{
			case LoadSaveScreen::OK:
			{
				save(loadSaveScreen->getFileName(), loadSaveScreen->getName());
				performAction("close save screen");
			}
			case LoadSaveScreen::CANCEL:
			{
				performAction("close save screen");
			}
		}
	}
	if(showingScriptEditor)
	{
		scriptEditor->translateAndProcessEvent(&event);
		switch(scriptEditor->endValue)
		{
			case ScriptEditorScreen::OK:
			case ScriptEditorScreen::CANCEL:
			{
				performAction("close script editor");
			}
		}
	}
	if(isShowingAreaName)
	{
		areaName->translateAndProcessEvent(&event);
		switch(areaName->endValue)
		{
			case AskForTextInput::OK:
			case AskForTextInput::CANCEL:
			{
				performAction("close area name");
			}
		}
	}
}



void MapEdit::addWidget(MapEditorWidget* widget)
{
	mew.push_back(widget);
}



bool MapEdit::findAction(int x, int y)
{
	for(std::vector<MapEditorWidget*>::iterator i=mew.begin(); i!=mew.end(); ++i)
	{
		MapEditorWidget* mi=*i;
		if(mi->is_in(x, y) && mi->enabled)
		{
			mi->handleClick(mouseX-mi->area.x, mouseY-mi->area.y);
			return true;
		}
	}
	return false;
}



void MapEdit::enableOnlyGroup(const std::string& group)
{
	for(std::vector<MapEditorWidget*>::iterator i=mew.begin(); i!=mew.end(); ++i)
	{
		if((*i)->group == group || (*i)->group=="any")
		{
			(*i)->enable();
		}
		else
			(*i)->disable();
	}
}



void MapEdit::drawWidgets()
{
	for(std::vector<MapEditorWidget*>::iterator i=mew.begin(); i!=mew.end(); ++i)
	{
		(*i)->drawSelf();
	}
}


void MapEdit::minimapMouseToPos(int mx, int my, int *cx, int *cy, bool forScreenViewport)
{
	// get data for minimap
	int mMax;
	int szX, szY;
	int decX, decY;
	Utilities::computeMinimapData(100, game.map.getW(), game.map.getH(), &mMax, &szX, &szY, &decX, &decY);

	mx-=14+decX;
	my-=14+decY;
	*cx=((mx*game.map.getW())/szX);
	*cy=((my*game.map.getH())/szY);
	*cx+=game.teams[team]->startPosX-(game.map.getW()/2);
	*cy+=game.teams[team]->startPosY-(game.map.getH()/2);
	if (forScreenViewport)
	{
		*cx-=((globalContainer->gfx->getW()-128)>>6);
		*cy-=((globalContainer->gfx->getH())>>6);
	}

	*cx&=game.map.getMaskW();
	*cy&=game.map.getMaskH();
}



void MapEdit::handleBrushClick(int mx, int my)
{
	// if we have an area over 32x32, which mean over 128 bytes, send it
// 	if (brushAccumulator.getAreaSurface() > 32*32)
// 	{
// 		sendBrushOrders();
// 	}
	// we add brush to accumulator
	int mapX, mapY;
	game.map.displayToMapCaseAligned(mx, my, &mapX, &mapY,  viewportX, viewportY);
	if(lastPlacementX==mapX && lastPlacementY==mapY)
		return;
	int fig = brush.getFigure();
	brushAccumulator.applyBrush(&game.map, BrushApplication(mapX, mapY, fig));
	// we get coordinates
	int startX = mapX-BrushTool::getBrushDimX(fig);
	int startY = mapY-BrushTool::getBrushDimY(fig);
	int width  = BrushTool::getBrushWidth(fig);
	int height = BrushTool::getBrushHeight(fig);
	// we update local values
	if (brush.getType() == BrushTool::MODE_ADD)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					if (brushType == ForbiddenBrush)
					{
						game.map.getCase(x, y).forbidden |= (1<<team);
						game.map.localForbiddenMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), true);
					}
					else if (brushType == GuardAreaBrush)
					{
						game.map.getCase(x, y).guardArea |= (1<<team);
						game.map.localGuardAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), true);
					}
					else if (brushType == ClearAreaBrush)
					{
						game.map.getCase(x, y).clearArea |= (1<<team);
						game.map.localClearAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), true);
					}
					else
						assert(false);
				}
	}
	else if (brush.getType() == BrushTool::MODE_DEL)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					if (brushType == ForbiddenBrush)
					{
						game.map.getCase(x, y).forbidden ^= game.map.getCase(x, y).forbidden & (1<<team);
						game.map.localForbiddenMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), false);
					}
					else if (brushType == GuardAreaBrush)
					{
						game.map.getCase(x, y).guardArea ^= game.map.getCase(x, y).guardArea & (1<<team);
						game.map.localGuardAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), false);
					}
					else if (brushType == ClearAreaBrush)
					{
						game.map.getCase(x, y).clearArea ^= game.map.getCase(x, y).clearArea & (1<<team);
						game.map.localClearAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), false);
					}
					else
						assert(false);
				}
	}
	else
		assert(false);
	lastPlacementX=mapX;
	lastPlacementY=mapY;
}



void MapEdit::handleTerrainClick(int mx, int my)
{
	// if we have an area over 32x32, which mean over 128 bytes, send it
// 	if (brushAccumulator.getAreaSurface() > 32*32)
// 	{
// 		sendBrushOrders();
// 	}
	// we add brush to accumulator
	int mapX, mapY;
	game.map.displayToMapCaseAligned(mx+(terrainType>TerrainSelector::Water ? 0 : 16), my+(terrainType>TerrainSelector::Water ? 0 : 16), &mapX, &mapY,  viewportX, viewportY);
	if(lastPlacementX==mapX && lastPlacementY==mapY)
		return;
	int fig = brush.getFigure();
	brushAccumulator.applyBrush(&game.map, BrushApplication(mapX, mapY, fig));
	// we get coordinates
	int startX = mapX-BrushTool::getBrushDimX(fig);
	int startY = mapY-BrushTool::getBrushDimY(fig);
	int width  = BrushTool::getBrushWidth(fig);
	int height = BrushTool::getBrushHeight(fig);
	// we update local values
	if (brush.getType() == BrushTool::MODE_ADD)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					if (terrainType == TerrainSelector::Grass)
					{
						game.removeUnitAndBuildingAndFlags(x, y, 3, Game::DEL_BUILDING | Game::DEL_UNIT);
						game.map.setNoRessource(x, y, 3);
						game.map.setUMatPos(x, y, GRASS, 1);
					}
					else if (terrainType == TerrainSelector::Sand)
					{
						game.removeUnitAndBuildingAndFlags(x, y, 1, Game::DEL_BUILDING | Game::DEL_UNIT);
						game.map.setNoRessource(x, y, 1);
						game.map.setUMatPos(x, y, SAND, 1);
					}
					else if (terrainType == TerrainSelector::Water)
					{
						game.removeUnitAndBuildingAndFlags(x, y, 3, Game::DEL_BUILDING | Game::DEL_UNIT);
						game.map.setNoRessource(x, y, 3);
						game.map.setUMatPos(x, y, WATER, 1);
					}
					else if (terrainType == TerrainSelector::Wheat)
					{
						if(game.map.isRessourceAllowed(x, y, CORN))
							game.map.setRessource(x, y, CORN, 1);
					}
					else if (terrainType == TerrainSelector::Trees)
					{
						if(game.map.isRessourceAllowed(x, y, WOOD))
							game.map.setRessource(x, y, WOOD, 1);
					}
					else if (terrainType == TerrainSelector::Stone)
					{
						if(game.map.isRessourceAllowed(x, y, STONE))
							game.map.setRessource(x, y, STONE, 1);
					}
					else if (terrainType == TerrainSelector::Algae)
					{
						if(game.map.isRessourceAllowed(x, y, ALGA))
							game.map.setRessource(x, y, ALGA, 1);
					}
					else if (terrainType == TerrainSelector::Papyrus)
					{
						if(game.map.isRessourceAllowed(x, y, PAPYRUS))
							game.map.setRessource(x, y, PAPYRUS, 1);
					}
					else if (terrainType == TerrainSelector::CherryTree)
					{
						if(game.map.isRessourceAllowed(x, y, CHERRY))
							game.map.setRessource(x, y, CHERRY, 1);
					}
					else if (terrainType == TerrainSelector::OrangeTree)
					{
						if(game.map.isRessourceAllowed(x, y, ORANGE))
							game.map.setRessource(x, y, ORANGE, 1);
					}
					else if (terrainType == TerrainSelector::PruneTree)
					{
						if(game.map.isRessourceAllowed(x, y, PRUNE))
							game.map.setRessource(x, y, PRUNE, 1);
					}
				}
	}
	else if (brush.getType() == BrushTool::MODE_DEL)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					if (terrainType == TerrainSelector::Sand || terrainType == TerrainSelector::Water)
					{
						game.map.setUMatPos(x, y, GRASS, 1);
						game.map.setNoRessource(x, y, 3);
					}
					else if (terrainType == TerrainSelector::Wheat)
					{
						if(game.map.isRessourceTakeable(x, y, CORN))
							game.map.setNoRessource(x, y, 1);
					}
					else if (terrainType == TerrainSelector::Trees)
					{
						if(game.map.isRessourceTakeable(x, y, WOOD))
							game.map.setNoRessource(x, y, 1);
					}
					else if (terrainType == TerrainSelector::Stone)
					{
						if(game.map.isRessourceTakeable(x, y, STONE))
							game.map.setNoRessource(x, y, 1);
					}
					else if (terrainType == TerrainSelector::Algae)
					{
						if(game.map.isRessourceTakeable(x, y, ALGA))
							game.map.setNoRessource(x, y, 1);
					}
					else if (terrainType == TerrainSelector::Papyrus)
					{
						if(game.map.isRessourceTakeable(x, y, PAPYRUS))
							game.map.setNoRessource(x, y, 1);
					}
					else if (terrainType == TerrainSelector::CherryTree || terrainType == TerrainSelector::OrangeTree || terrainType == TerrainSelector::PruneTree)
					{
						if(game.map.isRessourceTakeable(x, y, CHERRY) || game.map.isRessourceTakeable(x, y, ORANGE) || game.map.isRessourceTakeable(x, y, PRUNE))
							game.map.setNoRessource(x, y, 1);
					}
				}
	}
	else
		assert(false);
	lastPlacementX=mapX;
	lastPlacementY=mapY;
	renderMiniMap();
}



void MapEdit::handleDeleteClick(int mx, int my)
{
	int mapX, mapY;
	game.map.displayToMapCaseAligned(mx, my, &mapX, &mapY,  viewportX, viewportY);
	if(lastPlacementX==mapX && lastPlacementY==mapY)
		return;
	int fig = brush.getFigure();
	brushAccumulator.applyBrush(&game.map, BrushApplication(mapX, mapY, fig));
	// we get coordinates
	int startX = mapX-BrushTool::getBrushDimX(fig);
	int startY = mapY-BrushTool::getBrushDimY(fig);
	int width  = BrushTool::getBrushWidth(fig);
	int height = BrushTool::getBrushHeight(fig);
	// we update local values
	if (brush.getType() == BrushTool::MODE_ADD)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					game.removeUnitAndBuildingAndFlags(x, y, 1, Game::DEL_BUILDING | Game::DEL_UNIT | Game::DEL_FLAG);
				}
	}
	else if (brush.getType() == BrushTool::MODE_DEL)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					game.removeUnitAndBuildingAndFlags(x, y, 1, Game::DEL_BUILDING | Game::DEL_UNIT | Game::DEL_FLAG);
				}
	}
	lastPlacementX=mapX;
	lastPlacementY=mapY;
	renderMiniMap();
}



void MapEdit::handleAreaClick(int mx, int my)
{
	int mapX, mapY;
	game.map.displayToMapCaseAligned(mx, my, &mapX, &mapY,  viewportX, viewportY);
	if(lastPlacementX==mapX && lastPlacementY==mapY)
		return;
	int fig = brush.getFigure();
	brushAccumulator.applyBrush(&game.map, BrushApplication(mapX, mapY, fig));
	// we get coordinates
	int startX = mapX-BrushTool::getBrushDimX(fig);
	int startY = mapY-BrushTool::getBrushDimY(fig);
	int width  = BrushTool::getBrushWidth(fig);
	int height = BrushTool::getBrushHeight(fig);
	// we update local values
	if (brush.getType() == BrushTool::MODE_ADD)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					game.map.setPoint(areaNumber->getIndex(), x, y);
				}
	}
	else if (brush.getType() == BrushTool::MODE_DEL)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					game.map.unsetPoint(areaNumber->getIndex(), x, y);
				}
	}
	lastPlacementX=mapX;
	lastPlacementY=mapY;
}



void MapEdit::handleNoRessourceGrowthClick(int mx, int my)
{
	int mapX, mapY;
	game.map.displayToMapCaseAligned(mx, my, &mapX, &mapY,  viewportX, viewportY);
	if(lastPlacementX==mapX && lastPlacementY==mapY)
		return;
	int fig = brush.getFigure();
	brushAccumulator.applyBrush(&game.map, BrushApplication(mapX, mapY, fig));
	// we get coordinates
	int startX = mapX-BrushTool::getBrushDimX(fig);
	int startY = mapY-BrushTool::getBrushDimY(fig);
	int width  = BrushTool::getBrushWidth(fig);
	int height = BrushTool::getBrushHeight(fig);
	// we update local values
	if (brush.getType() == BrushTool::MODE_ADD)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					game.map.getCase(x, y).canRessourcesGrow=false;
				}
	}
	else if (brush.getType() == BrushTool::MODE_DEL)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					game.map.getCase(x, y).canRessourcesGrow=true;
				}
	}
	lastPlacementX=mapX;
	lastPlacementY=mapY;
}



MapEditMenuScreen::MapEditMenuScreen() : OverlayScreen(globalContainer->gfx, 320, 260)
{
	addWidget(new TextButton(0, 10, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "data/gfx/gamegui", 26, 27, "menu", Toolkit::getStringTable()->getString("[load map]"), LOAD_MAP));
	addWidget(new TextButton(0, 60, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "data/gfx/gamegui", 26, 27, "menu", Toolkit::getStringTable()->getString("[save map]"), SAVE_MAP));
	addWidget(new TextButton(0, 110, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "data/gfx/gamegui", 26, 27, "menu", Toolkit::getStringTable()->getString("[open script editor]"), OPEN_SCRIPT_EDITOR, 27));
	addWidget(new TextButton(0, 160, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "data/gfx/gamegui", 26, 27, "menu", Toolkit::getStringTable()->getString("[quit the editor]"), QUIT_EDITOR));
	addWidget(new TextButton(0, 210, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "data/gfx/gamegui", 26, 27, "menu", Toolkit::getStringTable()->getString("[return to editor]"), RETURN_EDITOR, 27));
	dispatchInit();
}

void MapEditMenuScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
		endValue=par1;
}




AskForTextInput::AskForTextInput(const std::string& aLabel, const std::string& aCurrent) : OverlayScreen(globalContainer->gfx, 300, 110), labelText(aLabel), currentText(aCurrent)
{
	label = new Text(0, 5, ALIGN_FILL, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString(labelText.c_str()));
	textEntry = new TextInput(10, 35, 280, 25, ALIGN_LEFT, ALIGN_LEFT, "standard", currentText, true);
	ok = new TextButton(10, 70, 135, 35, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), OK);
	cancel =  new TextButton(155, 70, 135, 35, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL);
	addWidget(label);
	addWidget(textEntry);
	addWidget(ok);
	addWidget(cancel);
	dispatchInit();
}



void AskForTextInput::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if(par1==OK)
		{
			currentText=textEntry->getText();
			endValue=OK;
		}
		else if(par1==CANCEL)
		{
			endValue=CANCEL;
		}
	}
}



std::string AskForTextInput::getText()
{
	return currentText;
}



