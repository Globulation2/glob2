// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <FormatableString.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Player.h"
#include "TeamDisplay.h"
#include "Unit.h"
#include "UnitDisplayNames.h"

namespace {
// Render one "[label] (displayLevel) : performance" row of the unit-info panel.
// Caller chooses what number to show: WALK/BUILD/HARVEST/ATTACK_SPEED store
// 0-based levels and pass `1 + level[X]` for a 1-based display. SWIM is stored
// 1-based already (`level[SWIM]==0` means "can't swim", which is also the
// guard on whether the row renders at all) and passes the raw value.
void drawAbilityRow(int xpos, int ypos, const char* labelKey, int displayLevel, int performance)
{
	globalContainer->gfx->drawString(
		xpos, ypos, globalContainer->littleFont,
		FormatableString("%0 (%1) : %2")
			.arg(Toolkit::getStringTable()->getString(labelKey))
			.arg(displayLevel)
			.arg(performance)
			.c_str());
}
} // namespace

void GameGUI::drawUnitInfos(void)
{
	Unit* selUnit=selectionUnit();
	assert(selUnit);
	int ypos = YPOS_BASE_UNIT;
	Uint8 r, g, b;

	// draw "unit" of "player"
	std::string title;
	title += getUnitName(selUnit->typeNum);
	title += " (";

	title += displayPlayerName(*selUnit->owner);
	title += ")";

	if (localTeam->teamNumber == selUnit->owner->teamNumber)
		{ r=160; g=160; b=255; }
	else if (localTeam->allies & selUnit->owner->me)
		{ r=255; g=210; b=20; }
	else
		{ r=255; g=50; b=50; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	int titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	int titlePos = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+((RIGHT_MENU_WIDTH-titleLen)>>1);
	globalContainer->gfx->drawString(titlePos, ypos+5, globalContainer->littleFont, title.c_str());
	globalContainer->littleFont->popStyle();

	ypos += YOFFSET_NAME;

	// draw unit's image
	Unit* unit=selUnit;
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
	int decX = (32-unitSprite->getW(imgid))>>1;
	int decY = (32-unitSprite->getH(imgid))>>1;
	int ddx = (RIGHT_MENU_HALF_WIDTH - 56) / 2 + 2;
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx+12+decX, ypos+7+4+decY, unitSprite, imgid);

	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx, ypos+4, globalContainer->gamegui, 18);

	// draw HP
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos, globalContainer->littleFont, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[hp]")).c_str());

	if (selUnit->hp<=selUnit->trigHP)
		{ r=255; g=0; b=0; }
	else
		{ r=0; g=255; b=0; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0/%1").arg(selUnit->hp).arg(selUnit->performance[HP]).c_str());
	globalContainer->littleFont->popStyle();

	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE+YOFFSET_TEXT_PARA, globalContainer->littleFont, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[food]")).c_str());

	// draw food
	if (selUnit->isUnitHungry())
		{ r=255; g=0; b=0; }
	else
		{ r=0; g=255; b=0; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+2*YOFFSET_TEXT_LINE+YOFFSET_TEXT_PARA, globalContainer->littleFont, FormatableString("%0 % (%1)").arg(((float)selUnit->hungry*100.0f)/(float)Unit::HUNGRY_MAX, 0, 0).arg(selUnit->fruitCount).c_str());
	globalContainer->littleFont->popStyle();

	ypos += YOFFSET_ICON+10;

	int rdec = (RIGHT_MENU_WIDTH-128)/2;

	if (selUnit->performance[HARVEST])
	{
		if (selUnit->carriedRessource>=0)
		{
			const RessourceType* rt = globalContainer->ressourcesTypes.get(selUnit->carriedRessource);
			unsigned resImg = rt->gfxId + rt->sizesCount - 1;
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+8, globalContainer->littleFont, Toolkit::getStringTable()->getString("[carry]"));
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-32-8-rdec, ypos, globalContainer->ressources, resImg);
		}
		else
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+8, globalContainer->littleFont, Toolkit::getStringTable()->getString("[don't carry anything]"));
		}
	}
	ypos += YOFFSET_CARYING+10;

	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 : %1").arg(Toolkit::getStringTable()->getString("[current speed]")).arg(selUnit->speed).c_str());
	ypos += YOFFSET_TEXT_PARA+10;

	if (selUnit->performance[ARMOR])
	{
		int armorReductionPerHappyness = selUnit->race->getUnitType(selUnit->typeNum, selUnit->level[ARMOR])->armorReductionPerHappyness;
		int realArmor = selUnit->performance[ARMOR] - selUnit->fruitCount * armorReductionPerHappyness;
		if (realArmor < 0)
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 0, 0));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 : %1 = %2 - %3 * %4").arg(Toolkit::getStringTable()->getString("[armor]")).arg(realArmor).arg(selUnit->performance[ARMOR]).arg(selUnit->fruitCount).arg(armorReductionPerHappyness).c_str());
		if (realArmor < 0)
			globalContainer->littleFont->popStyle();
	}
	ypos += YOFFSET_TEXT_PARA;

	if (selUnit->typeNum!=EXPLORER)
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[levels]")).c_str());
	ypos += YOFFSET_TEXT_PARA;

	const int rowX = globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4;

	if (selUnit->performance[WALK])
		drawAbilityRow(rowX, ypos, "[Walk]", 1 + selUnit->level[WALK], selUnit->performance[WALK]);
	ypos += YOFFSET_TEXT_LINE;

	// SWIM is stored 1-based (0 = can't swim); pass raw, not 1+.
	if (selUnit->performance[SWIM])
		drawAbilityRow(rowX, ypos, "[Swim]", selUnit->level[SWIM], selUnit->performance[SWIM]);
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[BUILD])
		drawAbilityRow(rowX, ypos, "[Build]", 1 + selUnit->level[BUILD], selUnit->performance[BUILD]);
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[HARVEST])
		drawAbilityRow(rowX, ypos, "[Harvest]", 1 + selUnit->level[HARVEST], selUnit->performance[HARVEST]);
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[ATTACK_SPEED])
		drawAbilityRow(rowX, ypos, "[At. speed]", 1 + selUnit->level[ATTACK_SPEED], selUnit->performance[ATTACK_SPEED]);
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[ATTACK_STRENGTH])
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[At. strength]")).arg(1+selUnit->level[ATTACK_STRENGTH]).arg(selUnit->experienceLevel).arg(selUnit->performance[ATTACK_STRENGTH]).arg(selUnit->experienceLevel).c_str());

		ypos += YOFFSET_TEXT_PARA + 2;
	}

	if (selUnit->performance[MAGIC_ATTACK_AIR])
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[Magic At. Air]")).arg(1+selUnit->level[MAGIC_ATTACK_AIR]).arg(selUnit->experienceLevel).arg(selUnit->performance[MAGIC_ATTACK_AIR]).arg(selUnit->experienceLevel).c_str());

		ypos += YOFFSET_TEXT_PARA + 2;
	}

	if (selUnit->performance[MAGIC_ATTACK_GROUND])
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[Magic At. Ground]")).arg(1+selUnit->level[MAGIC_ATTACK_GROUND]).arg(selUnit->experienceLevel).arg(selUnit->performance[MAGIC_ATTACK_GROUND]).arg(selUnit->experienceLevel).c_str());

		ypos += YOFFSET_TEXT_PARA + 2;
	}

	if (selUnit->performance[ATTACK_STRENGTH] || selUnit->performance[MAGIC_ATTACK_AIR] || selUnit->performance[MAGIC_ATTACK_GROUND])
		drawXPProgressBar(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, selUnit->experience, selUnit->getNextLevelThreshold());
}
