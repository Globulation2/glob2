// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <FormatableString.h>
#include <GUIStyle.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "ReplayReader.h"
#include "Team.h"
#include "TeamDisplay.h"

void GameGUI::drawRessourceInfos(void)
{
	// Precondition (established by checkSelection() in drawPanel): when we
	// reach here the resource selection still references a live resource tile.
	// The early-return is defensive — should never trigger.
	const Ressource &r = game.map.getRessource(selectionRessource());
	if (r.type==NO_RES_TYPE)
		return;

	int ypos = YPOS_BASE_RESSOURCE;

	// Draw ressource name
	const std::string &ressourceName = getRessourceName(r.type);
	int titleLen = globalContainer->littleFont->getStringWidth(ressourceName.c_str());
	int titlePos = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+((RIGHT_MENU_WIDTH-titleLen)>>1);
	globalContainer->gfx->drawString(titlePos, ypos+(YOFFSET_TEXT_PARA>>1), globalContainer->littleFont, ressourceName.c_str());
	ypos += 2*YOFFSET_TEXT_PARA;

	// Draw ressource image
	const RessourceType* rt = globalContainer->ressourcesTypes.get(r.type);
	unsigned resImg = rt->gfxId + r.variety*rt->sizesCount + r.amount;
	if (!rt->eternal)
		resImg--;
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+16, ypos, globalContainer->ressources, resImg);

	// Draw ressource count
	if (rt->granular)
	{
		int sizesCount=rt->sizesCount;
		int amount=r.amount;
		const std::string amountS = FormatableString("%0/%1").arg(amount).arg(sizesCount);
		int amountSH = globalContainer->littleFont->getStringHeight(amountS.c_str());
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-64, ypos+((32-amountSH)>>1), globalContainer->littleFont, amountS.c_str());
	}
}

void GameGUI::drawReplayPanel(void)
{
	Font *font=globalContainer->littleFont;

	int x = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + REPLAY_PANEL_XOFFSET;
	int y = REPLAY_PANEL_YOFFSET;
	int inc = REPLAY_PANEL_SPACE_BETWEEN_OPTIONS;

	globalContainer->gfx->drawString(x, y, font, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[Options]")));

	drawCheckButton(x, y + 1*inc, Toolkit::getStringTable()->getString("[fog of war]"), globalContainer->replayShowFog);
	drawCheckButton(x, y + 2*inc, Toolkit::getStringTable()->getString("[combined vision]"), (globalContainer->replayVisibleTeams == 0xFFFFFFFF));
	drawCheckButton(x, y + 3*inc, Toolkit::getStringTable()->getString("[show areas]"), (globalContainer->replayShowAreas));
	drawCheckButton(x, y + 4*inc, Toolkit::getStringTable()->getString("[show flags]"), (globalContainer->replayShowFlags));

	globalContainer->gfx->drawString(x, y + REPLAY_PANEL_PLAYERLIST_YOFFSET, font, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[players]")));

	for (int i = 0; i < game.teamsCount(); i++)
	{
		// I know this is a matter of taste, but I prefer checkboxes here. Radio buttons are a totally different style
		//drawRadioButton(x, y + REPLAY_PANEL_PLAYERLIST_YOFFSET + (i+1)*inc, game.teams[i]->getFirstPlayerName().c_str(), localTeamNo == i);
		drawRadioButton(x + 1, y + REPLAY_PANEL_PLAYERLIST_YOFFSET + (i+1)*inc + 1, localTeamNo == i);
		globalContainer->gfx->drawString(x + 20, y + REPLAY_PANEL_PLAYERLIST_YOFFSET + (i+1)*inc, font, displayPlayerName(*game.teams[i]).c_str());
	}
}

void GameGUI::drawReplayProgressBar(bool drawBackground)
{
	assert(globalContainer->replaying);
	assert(globalContainer->replayReader);
	assert(globalContainer->replayReader->isValid());

	// set the clipping rectangle
	globalContainer->gfx->setClipRect( 0, REPLAY_BAR_Y - 4, REPLAY_BAR_WIDTH, REPLAY_BAR_HEIGHT + 4);

	// draw menu background, black if low speed graphics, transparent otherwise
	if (drawBackground)
	{
		if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
			globalContainer->gfx->drawFilledRect( 0, REPLAY_BAR_Y, REPLAY_BAR_WIDTH, REPLAY_BAR_HEIGHT, 0, 0, 0);
		else
			globalContainer->gfx->drawFilledRect( 0, REPLAY_BAR_Y, REPLAY_BAR_WIDTH, REPLAY_BAR_HEIGHT, 0, 0, 40, 180);
	}

	// Progress bar y
	int y = REPLAY_BAR_Y + REPLAY_PROGRESS_BAR_Y_OFFSET;

	// Draw the actual progress bar
	Style::style->drawProgressBar(globalContainer->gfx,
		REPLAY_PROGRESS_BAR_X_OFFSET + REPLAY_PROGRESS_BAR_CAP_WIDTH - 1, y,
		REPLAY_BAR_WIDTH - 2*REPLAY_PROGRESS_BAR_X_OFFSET - REPLAY_PROGRESS_BAR_NUM_BUTTONS * REPLAY_PROGRESS_BAR_BUTTON_WIDTH - 2*REPLAY_PROGRESS_BAR_CAP_WIDTH + 2,
		globalContainer->replayReader->getCurrentStep(),
		globalContainer->replayReader->getNumStepsTotal());

	// Draw the round caps
	globalContainer->gfx->drawSprite(
		REPLAY_PROGRESS_BAR_X_OFFSET, y,
		globalContainer->gamegui,
		REPLAY_BAR_LEFT_CAP_SPRITE);
	globalContainer->gfx->drawSprite(
		REPLAY_BAR_WIDTH - REPLAY_PROGRESS_BAR_X_OFFSET - REPLAY_PROGRESS_BAR_CAP_WIDTH, y,
		globalContainer->gamegui,
		REPLAY_BAR_RIGHT_CAP_SPRITE);

	// Draw the buttons for play, pause and fast-forward
	int x = REPLAY_BAR_WIDTH - REPLAY_PROGRESS_BAR_X_OFFSET - REPLAY_PROGRESS_BAR_CAP_WIDTH;
	int inc = REPLAY_PROGRESS_BAR_BUTTON_WIDTH;

	globalContainer->gfx->drawSprite( x - inc*3, y, globalContainer->gamegui, (!gamePaused && !globalContainer->replayFastForward ? REPLAY_BAR_PLAY_BUTTON_ACTIVE_SPRITE : REPLAY_BAR_PLAY_BUTTON_SPRITE));
	globalContainer->gfx->drawSprite( x - inc*2, y, globalContainer->gamegui, (gamePaused ? REPLAY_BAR_PAUSE_BUTTON_ACTIVE_SPRITE : REPLAY_BAR_PAUSE_BUTTON_SPRITE));
	globalContainer->gfx->drawSprite( x - inc*1, y, globalContainer->gamegui, (!gamePaused && globalContainer->replayFastForward ? REPLAY_BAR_FAST_FORWARD_BUTTON_ACTIVE_SPRITE : REPLAY_BAR_FAST_FORWARD_BUTTON_SPRITE));

	// Calculate the time
	// This is based on default speed 25 fps, not the actual Engine's speed
	// because if we fast-forward we still want to see the old time
	unsigned int time1_sec = (globalContainer->replayReader->getCurrentStep()/25)%60;
	unsigned int time1_min = (globalContainer->replayReader->getCurrentStep()/(25*60))%60;
	unsigned int time1_hour = (globalContainer->replayReader->getCurrentStep()/(25*3600));

	unsigned int time2_sec = (globalContainer->replayReader->getNumStepsTotal()/25)%60;
	unsigned int time2_min = (globalContainer->replayReader->getNumStepsTotal()/(25*60))%60;
	unsigned int time2_hour = (globalContainer->replayReader->getNumStepsTotal()/(25*3600));

	// Draw the time
	if (time2_hour <= 99)
	{
		globalContainer->gfx->drawString(REPLAY_BAR_TIMER_X, y+3, globalContainer->littleFont,
			FormatableString("%0:%1:%2 / %3:%4:%5")
			.arg(time1_hour)
			.arg(time1_min,2,10,'0')
			.arg(time1_sec,2,10,'0')
			.arg(time2_hour)
			.arg(time2_min,2,10,'0')
			.arg(time2_sec,2,10,'0')
			.c_str());
	}
	else
	{
		// Time did not get saved properly, don't show it
		globalContainer->gfx->drawString(REPLAY_BAR_TIMER_X, y+3, globalContainer->littleFont,
			FormatableString("%0:%1:%2")
			.arg(time1_hour)
			.arg(time1_min,2,10,'0')
			.arg(time1_sec,2,10,'0')
			.c_str());
	}

	// Draw the filename of the replay
	std::string replayName = glob2FilenameToName(globalContainer->replayFileName);
	int stringWidth = globalContainer->littleFont->getStringWidth(replayName.c_str());
	int pos = (globalContainer->settings.screenWidth-RIGHT_MENU_WIDTH)/2 - stringWidth/2;
	globalContainer->gfx->drawString(pos, y+3, globalContainer->littleFont, replayName.c_str());

	// Draw the border
	if (drawBackground)
	{
		for (int i = 0; i < REPLAY_BAR_WIDTH; i += 32)
		{
			globalContainer->gfx->drawSprite(i, REPLAY_BAR_Y-4, globalContainer->gamegui, 16);
		}
	}
}

void GameGUI::drawFlagView(void)
{
	int dec = (RIGHT_MENU_WIDTH - 128)/2;
	// draw flags
	drawChoice(YPOS_BASE_FLAG, flagsChoiceName, flagsChoiceState, 3);

	// draw choice of area
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 13);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+48+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 14);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+88+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 25);
	if (brush.getType() != BrushTool::MODE_NONE)
	{
		int decX = 8 + ((int)toolManager.getZoneType()) * 40 + dec;
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+decX, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 22);
	}
	globalContainer->gfx->finishDrawingSprite(globalContainer->gamegui, 255);
	if(hilights.find(HilightForbiddenZoneOnPanel) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36+8+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, 38));
	}
	if(hilights.find(HilightGuardZoneOnPanel) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36+48+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, 38));
	}
	if(hilights.find(HilightClearingZoneOnPanel) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36+88+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH, 38));
	}

	// draw brush
	brush.draw(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH+40);

	if(hilights.find(HilightBrushSelector) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36+dec, YPOS_BASE_FLAG+YOFFSET_BRUSH+40+30, 38));
	}

	// draw brush help text
	if ((mouseX>globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+dec) && (mouseY>YPOS_BASE_FLAG+YOFFSET_BRUSH))
	{
		int buildingInfoStart = globalContainer->gfx->getH()-50;
		if (mouseY<YPOS_BASE_FLAG+YOFFSET_BRUSH+40)
		{
			int panelMouseX = mouseX - globalContainer->gfx->getW() + RIGHT_MENU_WIDTH;
			if (panelMouseX < 44)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[forbidden area]");
			else if (panelMouseX < 84)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[guard area]");
			else
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[clear area]");
		}
		else
		{
			if (toolManager.getZoneType() == GameGUIToolManager::Forbidden)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[forbidden area]");
			else if (toolManager.getZoneType() == GameGUIToolManager::Guard)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[guard area]");
			else if (toolManager.getZoneType() == GameGUIToolManager::Clearing)
				drawTextCenter(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, buildingInfoStart-32, "[clear area]");
			else
				assert(false);
		}
	}
}
