// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "Player.h"
#include "SpriteCentering.h"
#include "TeamDisplay.h"
#include "Unit.h"
#include "render/UnitAnimation.h"
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
		FormattableString("%0 (%1) : %2")
			.arg(Toolkit::getStringTable()->getString(labelKey))
			.arg(displayLevel)
			.arg(performance)
			.c_str());
}
} // namespace

void GameGUI::drawUnitInfos(void)
{
	const SceneUnitPanel* selUnit = &drawnScene().panels.unit;
	const auto* selected = std::get_if<UnitRef>(&selection);
    if (!selUnit->valid || !selected || selected->gid != selUnit->state().gid || selected->generation != selUnit->state().scriptIdentity
        || drawnScene().panels.local.state().number != localTeamNo)
		return;
	int ypos = YPOS_BASE_UNIT;
	Uint8 r, g, b;

	// draw "unit" of "player"
	std::string title;
	title += getUnitName(selUnit->state().typeNum);
	title += " (";

	title += displayPlayerName(selUnit->owner().firstPlayerName);
	title += ")";

	if (drawnScene().panels.local.state().number == selUnit->owner().number)
		{ r=160; g=160; b=255; }
	else if (drawnScene().panels.local.state().allies & selUnit->owner().mask)
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
	const SceneUnitPanel* unit=selUnit;
	int imgid;
	const UnitType *ut=&unit->unitTypes[0];
	assert(unit->state().action>=0);
	assert(unit->state().action<NB_MOVE);
	imgid=ut->startImage[unit->state().action];

	int dir=unit->state().direction;
	int delta=unit->state().delta;
	assert(dir>=0);
	assert(dir<9);
	assert(delta>=0);
	assert(delta<256);
	imgid=unitAnimationFrame(imgid, dir, delta);

	Sprite *unitSprite=globalContainer->units;
	unitSprite->setBaseColor(presentationColor(unit->owner().color));
	// The unit icon is centered in a 32x32 tile-sized frame.
	constexpr int UNIT_ICON_BOX_PX = 32;
	const SpriteCenterOffset off = centerSprite(UNIT_ICON_BOX_PX, UNIT_ICON_BOX_PX, unitSprite, imgid);
	int ddx = (RIGHT_MENU_HALF_WIDTH - 56) / 2 + 2;
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx+12+off.dx, ypos+7+4+off.dy, unitSprite, imgid);

	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+ddx, ypos+4, globalContainer->gamegui, 18);

	// draw HP
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos, globalContainer->littleFont, FormattableString("%0:").arg(Toolkit::getStringTable()->getString("[hp]")).c_str());

	if (selUnit->state().hp<=selUnit->state().trigHP)
		{ r=255; g=0; b=0; }
	else
		{ r=0; g=255; b=0; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormattableString("%0/%1").arg(selUnit->state().hp).arg(selUnit->state().performance[HP]).c_str());
	globalContainer->littleFont->popStyle();

	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+YOFFSET_TEXT_LINE+YOFFSET_TEXT_PARA, globalContainer->littleFont, FormattableString("%0:").arg(Toolkit::getStringTable()->getString("[food]")).c_str());

	// draw food
	if (selUnit->unitHungry)
		{ r=255; g=0; b=0; }
	else
		{ r=0; g=255; b=0; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_HALF_WIDTH, ypos+2*YOFFSET_TEXT_LINE+YOFFSET_TEXT_PARA, globalContainer->littleFont, FormattableString("%0 % (%1)").arg(((float)selUnit->state().hungry*100.0f)/(float)Unit::HUNGRY_MAX, 0, 0).arg(selUnit->state().fruitCount).c_str());
	globalContainer->littleFont->popStyle();

	ypos += YOFFSET_ICON+10;

	int rdec = (RIGHT_MENU_WIDTH-128)/2;

	if (selUnit->state().performance[HARVEST])
	{
		if (selUnit->state().carriedMaterial>=0)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+8, globalContainer->littleFont, Toolkit::getStringTable()->getString("[carry]"));
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-32-8-rdec, ypos, globalContainer->resourceMini, selUnit->state().carriedMaterial);
			globalContainer->gfx->finishDrawingSprite(globalContainer->resourceMini, 255);
		}
		else
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos+8, globalContainer->littleFont, Toolkit::getStringTable()->getString("[don't carry anything]"));
		}
	}
	ypos += YOFFSET_CARRYING+10;

	globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormattableString("%0 : %1").arg(Toolkit::getStringTable()->getString("[current speed]")).arg(selUnit->state().speed).c_str());
	ypos += YOFFSET_TEXT_PARA+10;

	if (selUnit->state().performance[ARMOR])
	{
		int armorReductionPerHappyness = selUnit->unitTypes[selUnit->state().level[ARMOR]].armorReductionPerHappyness;
		// Custom-game "glass cannon" rule: show the actual armor combat uses
		// (getRealArmor()), not just the pre-scale breakdown below -- at the
		// rule's default (scale 1), this is identical to the old
		// performance[ARMOR]-fruitCount*reduction computation.
		int realArmor = selUnit->realArmor;
		if (realArmor < 0)
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 0, 0));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormattableString("%0 : %1 = %2 - %3 * %4").arg(Toolkit::getStringTable()->getString("[armor]")).arg(realArmor).arg(selUnit->state().performance[ARMOR]).arg(selUnit->state().fruitCount).arg(armorReductionPerHappyness).c_str());
		if (realArmor < 0)
			globalContainer->littleFont->popStyle();
	}
	ypos += YOFFSET_TEXT_PARA;

	if (selUnit->state().typeNum!=EXPLORER)
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormattableString("%0:").arg(Toolkit::getStringTable()->getString("[levels]")).c_str());
	ypos += YOFFSET_TEXT_PARA;

	const int rowX = globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4;

	if (selUnit->state().performance[WALK])
		drawAbilityRow(rowX, ypos, "[Walk]", 1 + selUnit->state().level[WALK], selUnit->state().performance[WALK]);
	ypos += YOFFSET_TEXT_LINE;

	// SWIM is stored 1-based (0 = can't swim); pass raw, not 1+.
	if (selUnit->state().performance[SWIM])
		drawAbilityRow(rowX, ypos, "[Swim]", selUnit->state().level[SWIM], selUnit->state().performance[SWIM]);
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->state().performance[BUILD])
		drawAbilityRow(rowX, ypos, "[Build]", 1 + selUnit->state().level[BUILD], selUnit->state().performance[BUILD]);
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->state().performance[ATTACK_SPEED])
		drawAbilityRow(rowX, ypos, "[At. speed]", 1 + selUnit->state().level[ATTACK_SPEED], selUnit->state().performance[ATTACK_SPEED]);
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->state().performance[ATTACK_STRENGTH])
	{
		std::string attackLine = FormattableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[At. strength]")).arg(1+selUnit->state().level[ATTACK_STRENGTH]).arg(selUnit->state().experienceLevel).arg(selUnit->state().performance[ATTACK_STRENGTH]).arg(selUnit->state().experienceLevel).c_str();
		// Custom-game "glass cannon" rule: the breakdown above shows the base
		// stat and experience bonus exactly as it always has, so append the
		// scale actual combat applies on top -- at the rule's default (scale
		// 1) this appends nothing, leaving the line unchanged.
		const int glassCannonScale = selUnit->glassCannonScale;
		if (glassCannonScale != 1)
			attackLine += FormattableString(" x%0").arg(glassCannonScale).c_str();
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, attackLine.c_str());

		ypos += YOFFSET_TEXT_PARA + 2;
	}

	if (selUnit->state().performance[MAGIC_ATTACK_AIR])
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormattableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[Magic At. Air]")).arg(1+selUnit->state().level[MAGIC_ATTACK_AIR]).arg(selUnit->state().experienceLevel).arg(selUnit->state().performance[MAGIC_ATTACK_AIR]).arg(selUnit->state().experienceLevel).c_str());

		ypos += YOFFSET_TEXT_PARA + 2;
	}

	if (selUnit->state().performance[MAGIC_ATTACK_GROUND])
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET+4, ypos, globalContainer->littleFont, FormattableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[Magic At. Ground]")).arg(1+selUnit->state().level[MAGIC_ATTACK_GROUND]).arg(selUnit->state().experienceLevel).arg(selUnit->state().performance[MAGIC_ATTACK_GROUND]).arg(selUnit->state().experienceLevel).c_str());

		ypos += YOFFSET_TEXT_PARA + 2;
	}

	if (selUnit->state().performance[ATTACK_STRENGTH] || selUnit->state().performance[MAGIC_ATTACK_AIR] || selUnit->state().performance[MAGIC_ATTACK_GROUND])
		drawXPProgressBar(globalContainer->gfx->getW()-RIGHT_MENU_RIGHT_OFFSET, ypos, selUnit->state().experience, selUnit->nextLevelThreshold);
}
