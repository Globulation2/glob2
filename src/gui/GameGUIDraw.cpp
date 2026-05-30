// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <iostream>

#include <FormatableString.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "Player.h"
#include "ReplayReader.h"
#include "SoundMixer.h"
#include "TeamDisplay.h"
#include "Unit.h"
#include "VoiceRecorder.h"

void GameGUI::drawPanelButtons(int y)
{
	if (!globalContainer->replaying)
	{
		if (!(hiddenGUIElements & HIDABLE_BUILDINGS_LIST))
		{
			if (((selectionMode==NO_SELECTION) || (selectionMode==TOOL_SELECTION)) && (displayMode==CONSTRUCTION_VIEW))
				drawPanelButton(y, 0, NB_VIEWS, 1);
			else
				drawPanelButton(y, 0, NB_VIEWS, 0);
		}

		if (!(hiddenGUIElements & HIDABLE_FLAGS_LIST))
		{
			if (((selectionMode==NO_SELECTION) || (selectionMode==TOOL_SELECTION) || (selectionMode==BRUSH_SELECTION)) && (displayMode==FLAG_VIEW))
				drawPanelButton(y, 1, NB_VIEWS, 29);
			else
				drawPanelButton(y, 1, NB_VIEWS, 28);
		}

		if (!(hiddenGUIElements & HIDABLE_TEXT_STAT))
		{
			if ((selectionMode==NO_SELECTION) && (displayMode==STAT_TEXT_VIEW))
				drawPanelButton(y, 2, NB_VIEWS, 3);
			else
				drawPanelButton(y, 2, NB_VIEWS, 2);
		}

		if (!(hiddenGUIElements & HIDABLE_GFX_STAT))
		{
			if ((selectionMode==NO_SELECTION) && (displayMode==STAT_GRAPH_VIEW))
				drawPanelButton(y, 3, NB_VIEWS, 5);
			else
				drawPanelButton(y, 3, NB_VIEWS, 4);
		}
	}
	else
	{
		if (replayDisplayMode==RDM_REPLAY_VIEW)
			drawPanelButton(y, 0, RDM_NB_VIEWS, 48);
		else
			drawPanelButton(y, 0, RDM_NB_VIEWS, 49);

		if (replayDisplayMode==RDM_STAT_TEXT_VIEW)
			drawPanelButton(y, 1, RDM_NB_VIEWS, 3);
		else
			drawPanelButton(y, 1, RDM_NB_VIEWS, 2);

		if (replayDisplayMode==RDM_STAT_GRAPH_VIEW)
			drawPanelButton(y, 2, RDM_NB_VIEWS, 5);
		else
			drawPanelButton(y, 2, RDM_NB_VIEWS, 4);
	}

	if(hilights.find(HilightUnderMinimapIcon) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36, y, 38));
	}
}

void GameGUI::drawPanelButton(int y, int pos, int numButtons, int sprite)
{
	int dec = (RIGHT_MENU_WIDTH - numButtons*32)/2;

	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH + dec + pos*32, y, globalContainer->gamegui, sprite);
}

void GameGUI::drawValueAlignedRight(int y, int v)
{
	FormatableString s("%0");
	s.arg(v);
	int len = globalContainer->littleFont->getStringWidth(s.c_str());
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-len-2, y, globalContainer->littleFont, s.c_str());
}

void GameGUI::drawCosts(int ressources[BASIC_COUNT], Font *font)
{
	for (int i=0; i<BASIC_COUNT; i++)
	{
		int y = i>>1;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+4+(i&0x1)*64, 256+172-42+y*12,
			font,
			FormatableString("%0: %1").arg(getRessourceName(i)).arg(ressources[i]).c_str());
	}
}

void GameGUI::drawCheckButton(int x, int y, std::string caption, bool isSet)
{
	globalContainer->gfx->drawRect(x, y, 16, 16, Color::white);
	if(isSet)
	{
		globalContainer->gfx->drawLine(x+4, y+4, x+12, y+12, Color::white);
		globalContainer->gfx->drawLine(x+12, y+4, x+4, y+12, Color::white);
	}
	globalContainer->gfx->drawString(x+20, y, globalContainer->littleFont, caption);
}


void GameGUI::drawRadioButton(int x, int y, bool isSet)
{
	if(isSet)
	{
		globalContainer->gfx->drawSprite(x, y, globalContainer->gamegui, 20);
	}
	else
	{
		globalContainer->gfx->drawSprite(x, y, globalContainer->gamegui, 19);
	}
}

void GameGUI::drawPanel(void)
{
	// ensure we have a valid selection and associate pointers
	checkSelection();

	// set the clipping rectangle
	globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 128, RIGHT_MENU_WIDTH, globalContainer->gfx->getH()-128);

	// draw menu background, black if low speed graphics, transparent otherwise
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 133, RIGHT_MENU_WIDTH, globalContainer->gfx->getH()-128, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 133, RIGHT_MENU_WIDTH, globalContainer->gfx->getH()-128, 0, 0, 40, 180);

	if(hilights.find(HilightRightSidePanel) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-36, globalContainer->gfx->getH()/2, 38));
	}

	// draw the panel selection buttons
	drawPanelButtons(YPOS_BASE_DEFAULT-32);

	dispatchSelectionPanel();
}

void GameGUI::dispatchSelectionPanel(void)
{
	switch(selectionMode)
	{
	case BUILDING_SELECTION:
		drawBuildingInfos();
		break;
	case UNIT_SELECTION:
		drawUnitInfos();
		break;
	case RESSOURCE_SELECTION:
		drawRessourceInfos();
		break;
	default:
		if (!globalContainer->replaying)
			dispatchDisplayModePanel();
		else
			dispatchReplayDisplayModePanel();
		break;
	}
}

void GameGUI::dispatchDisplayModePanel(void)
{
	switch(displayMode)
	{
	case CONSTRUCTION_VIEW:
		drawChoice(YPOS_BASE_CONSTRUCTION, buildingsChoiceName, buildingsChoiceState);
		break;
	case FLAG_VIEW:
		drawFlagView();
		break;
	case STAT_TEXT_VIEW:
		teamStats->drawText(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+RIGHT_MENU_OFFSET, YPOS_BASE_STAT);
		break;
	case STAT_GRAPH_VIEW:
		teamStats->drawStat(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+RIGHT_MENU_OFFSET, YPOS_BASE_STAT);
		drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+140+64, Toolkit::getStringTable()->getString("[Starving Map]"), showStarvingMap);
		drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+140+88, Toolkit::getStringTable()->getString("[Damaged Map]"), showDamagedMap);
		drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+140+112, Toolkit::getStringTable()->getString("[Defense Map]"), showDefenseMap);
		drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+140+136, Toolkit::getStringTable()->getString("[Fertility Map]"), showFertilityMap);
		break;
	default:
		std::cout << "Was not expecting displayMode" << displayMode;
		assert(false);
	}
}

void GameGUI::dispatchReplayDisplayModePanel(void)
{
	switch(replayDisplayMode)
	{
	case RDM_REPLAY_VIEW:
		drawReplayPanel();
		break;
	case RDM_STAT_TEXT_VIEW:
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+15, YPOS_BASE_STAT+5, globalContainer->littleFont, FormatableString("%0 %1").arg(Toolkit::getStringTable()->getString("[watching:]")).arg(displayPlayerName(*localTeam)).c_str());
		teamStats->drawText(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+RIGHT_MENU_OFFSET, YPOS_BASE_STAT+15);
		break;
	case RDM_STAT_GRAPH_VIEW:
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+15, YPOS_BASE_STAT+5, globalContainer->littleFont, FormatableString("%0 %1").arg(Toolkit::getStringTable()->getString("[watching:]")).arg(displayPlayerName(*localTeam)).c_str());
		teamStats->drawStat(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+RIGHT_MENU_OFFSET, YPOS_BASE_STAT+15);
		drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+155+64, Toolkit::getStringTable()->getString("[Starving Map]"), showStarvingMap);
		drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+155+88, Toolkit::getStringTable()->getString("[Damaged Map]"), showDamagedMap);
		drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+155+112, Toolkit::getStringTable()->getString("[Defense Map]"), showDefenseMap);
		drawCheckButton(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+8, YPOS_BASE_STAT+155+136, Toolkit::getStringTable()->getString("[Fertility Map]"), showFertilityMap);
		break;
	default:
		std::cout << "Was not expecting replayDisplayMode" << replayDisplayMode;
		assert(false);
	}
}

void GameGUI::drawTopScreenBar(void)
{
	// bar background
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 16, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 16, 0, 0, 40, 180);

	// draw unit stats
	Uint8 redC[]={200, 0, 0};
	Uint8 greenC[]={0, 200, 0};
	Uint8 whiteC[]={200, 200, 200};
	Uint8 yellowC[]={200, 200, 0};
	Uint8 actC[3];
	int free, tot;

	int dec = (globalContainer->gfx->getW()-640)>>2;
	dec += 10;

	globalContainer->unitmini->setBaseColor(localTeam->color);
	for (int i=0; i<3; i++)
	{
		free = teamStats->getFreeUnits(i);
		// worker is a special case
		if (i==0)
			free -= teamStats->getWorkersNeeded();
		tot = teamStats->getTotalUnits(i);
		if (free<0)
			memcpy(actC, redC, sizeof(redC));
		else if (free>0)
			memcpy(actC, greenC, sizeof(greenC));
		else
			memcpy(actC, whiteC, sizeof(whiteC));

		globalContainer->gfx->drawSprite(dec+2, -1, globalContainer->unitmini, i);
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, actC[0], actC[1], actC[2]));
		globalContainer->gfx->drawString(dec+22, 0, globalContainer->littleFont, FormatableString("%0 / %1").arg(free).arg(tot).c_str());
		globalContainer->littleFont->popStyle();

		if(i==WORKER && hilights.find(HilightWorkersWorkingFreeStat) != hilights.end())
		{
			arrowPositions.push_back(HilightArrowPosition(dec+22, 32, 39));
		}

		else if(i==WARRIOR && hilights.find(HilightExplorersWorkingFreeStat) != hilights.end())
		{
			arrowPositions.push_back(HilightArrowPosition(dec+22, 32, 39));
		}

		else if(i==EXPLORER && hilights.find(HilightWarriorsWorkingFreeStat) != hilights.end())
		{
			arrowPositions.push_back(HilightArrowPosition(dec+22, 32, 39));
		}

		dec += 70;
	}

	// draw prestige stats
	globalContainer->gfx->drawString(dec+0, 0, globalContainer->littleFont, FormatableString("%0 / %1 / %2").arg(localTeam->prestige).arg(game.totalPrestige).arg(game.prestigeToReach).c_str());

	dec += 90;

	// draw unit conversion stats
	globalContainer->gfx->drawString(dec, 0, globalContainer->littleFont, FormatableString("+%0 / -%1").arg(localTeam->unitConversionGained).arg(localTeam->unitConversionLost).c_str());

	// draw CPU load
	dec += 70;
	int cpuLoad=0;
	for (unsigned i=0; i<SMOOTHED_CPU_SIZE; i++)
		cpuLoad += smoothedCPULoad[i];

	cpuLoad /= SMOOTHED_CPU_SIZE;

	if (cpuLoad<50)
		memcpy(actC, greenC, sizeof(greenC));
	else if (cpuLoad<75)
		memcpy(actC, yellowC, sizeof(yellowC));
	else
		memcpy(actC, redC, sizeof(redC));

	int cpuLength = int(float(cpuLoad) / 100.0 * 40.0);

	globalContainer->gfx->drawFilledRect(dec, 4, cpuLength, 8, actC[0], actC[1], actC[2]);
	globalContainer->gfx->drawVertLine(dec, 2, 12, 200, 200, 200);
	globalContainer->gfx->drawVertLine(dec+40, 2, 12, 200, 200, 200);

	// draw window bar
	int pos=globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-16;
	for (int i=0; i<pos; i+=32)
	{
		globalContainer->gfx->drawSprite(i, 16, globalContainer->gamegui, 16);
	}
	for (int i=16; i<globalContainer->gfx->getH(); i+=32)
	{
		globalContainer->gfx->drawSprite(pos+12, i, globalContainer->gamegui, 17);
	}


	int index;
	// draw main menu button
	if (inGameMenu == IGM_MAIN)
		index = 7;
	else
		index = 6;
	globalContainer->gfx->drawSprite(pos, IGM_MAIN_MENU_ICON_Y, globalContainer->gamegui, index);

	// draw alliance button
	if ( !(hiddenGUIElements & HIDABLE_ALLIANCE) )
	{
		if (inGameMenu == IGM_ALLIANCE)
			index = 44;
		else
			index = 45;
		globalContainer->gfx->drawSprite(pos, IGM_ALLIANCE_ICON_Y, globalContainer->gamegui, index);
	}

	// draw objectives button
	if (inGameMenu == IGM_OBJECTIVES)
		index = 46;
	else
		index = 47;
	globalContainer->gfx->drawSprite(pos, IGM_OBJECTIVES_ICON_Y, globalContainer->gamegui, index);

	if(hilights.find(HilightMainMenuIcon) != hilights.end())
	{
		arrowPositions.push_back(HilightArrowPosition(pos-32, 32, 43));
	}
}

void GameGUI::drawOverlayInfos(void)
{
	if (selectionMode==TOOL_SELECTION)
	{
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH());
		toolManager.drawTool(mouseX, mouseY, localTeamNo, viewportX, viewportY);
	}
	else if (selectionMode==BRUSH_SELECTION)
	{
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH());
		toolManager.drawTool(mouseX, mouseY, localTeamNo, viewportX, viewportY);
	}
	else if (selectionMode==BUILDING_SELECTION)
	{
		Building* selBuild=selectionBuilding();
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH());
		int centerX, centerY;
		game.map.buildingPosToCursor(displayedPosX(*selBuild), displayedPosY(*selBuild),  selBuild->type->width, selBuild->type->height, &centerX, &centerY, viewportX, viewportY);
		if (selBuild->owner->teamNumber==localTeamNo)
			globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 0, 0, 190);
		else if ((localTeam->allies) & (selBuild->owner->me))
			globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 255, 196, 0);
		else if (!selBuild->type->isVirtual)
			globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 190, 0, 0);

		// draw a white circle around units that are working at building
		if ((showUnitWorkingToBuilding)
			&& ((selBuild->owner->allies) &(1<<localTeamNo)))
		{
			for (std::list<Unit *>::iterator unitsWorkingIt=selBuild->unitsWorking.begin(); unitsWorkingIt!=selBuild->unitsWorking.end(); ++unitsWorkingIt)
			{
				Unit *unit=*unitsWorkingIt;
				int px, py;
				game.map.mapCaseToDisplayable(unit->posX, unit->posY, &px, &py, viewportX, viewportY);
				int deltaLeft=255-unit->delta;
				if (unit->action<BUILD)
				{
					px-=(unit->dx*deltaLeft)>>3;
					py-=(unit->dy*deltaLeft)>>3;
				}
				globalContainer->gfx->drawCircle(px+16, py+16, 16, 255, 255, 255, 180);
			}
		}
	}
	else if (selectionMode==RESSOURCE_SELECTION)
	{
		int ressource = selectionRessource();
		int rx = ressource & game.map.getMaskW();
		int ry = ressource >> game.map.getShiftW();
		int px, py;
		game.map.mapCaseToDisplayable(rx, ry, &px, &py, viewportX, viewportY);
		globalContainer->gfx->drawCircle(px+16, py+16, 16, 0, 0, 190);
	}

	// draw message List
	if (game.anyPlayerWaited && game.maskAwayPlayer && game.anyPlayerWaitedTimeFor>2)
	{
		int nbap=0; // Number of away players
		Uint32 pm=1;
		Uint32 apm=game.maskAwayPlayer;
		for(int pi=0; pi<game.gameHeader.getNumberOfPlayers(); pi++)
		{
			if (pm&apm)
				nbap++;
			pm=pm<<1;
		}

		globalContainer->gfx->drawFilledRect(32, 32, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-64, 22+nbap*20, 0, 0, 140, 127);
		globalContainer->gfx->drawRect(32, 32, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-64, 22+nbap*20, 255, 255, 255);
		pm=1;
		int pnb=0;
		for(int pi2=0; pi2<game.gameHeader.getNumberOfPlayers(); pi2++)
		{
			if (pm&apm)
			{
				globalContainer->gfx->drawString(44, 44+pnb*20, globalContainer->standardFont, FormatableString(Toolkit::getStringTable()->getString("[waiting for %0]")).arg(game.players[pi2]->name).c_str());
				pnb++;
			}
			pm=pm<<1;
		}
	}
	else
	{
		int ymesg = 32;
		int yinc = 0;

		// TODO: die with SGSL
		// show script text
		if (game.sgslScript.isTextShown)
		{
			std::vector<std::string> lines;
			setMultiLine(game.sgslScript.textShown, &lines);
			globalContainer->gfx->drawFilledRect(24, ymesg-8, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-64+16, lines.size()*20+16, 0,0,0,128);
			for (unsigned i=0; i<lines.size(); i++)
			{
				globalContainer->gfx->drawString(32, ymesg+yinc, globalContainer->standardFont, lines[i].c_str());
				yinc += 20;
			}

			if (swallowSpaceKey)
			{
				globalContainer->gfx->drawFilledRect(24, ymesg+yinc+8, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-64+16, 20, 0,0,0,128);
				globalContainer->gfx->drawString(32, ymesg+yinc, globalContainer->standardFont, Toolkit::getStringTable()->getString("[press space]"));
				yinc += 20;
			}
			yinc += 8;
		}

		// show script text
		if (!scriptText.empty())
		{
			std::vector<std::string> lines;
			setMultiLine(scriptText, &lines);
			globalContainer->gfx->drawFilledRect(24, ymesg-8, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-64+16, lines.size()*20+16, 0,0,0,128);
			for (unsigned i=0; i<lines.size(); i++)
			{
				globalContainer->gfx->drawString(32, ymesg+yinc, globalContainer->standardFont, lines[i].c_str());
				yinc += 20;
			}
		}

		// show script counter
		if (game.sgslScript.getMainTimer())
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-165, ymesg, globalContainer->standardFont, FormatableString("%0").arg(game.sgslScript.getMainTimer()).c_str());
			yinc = std::max(yinc, 32);
		}

		ymesg += yinc+2;

		messageManager.drawAllGameMessages(32, ymesg);
	}

	// display map mark
	globalContainer->gfx->setClipRect();
	markManager.drawAll(localTeamNo, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+20, 10, 128, viewportX, viewportY, game);

	// display text if placing a building
	if(selectionMode == TOOL_SELECTION && toolManager.getBuildingName() != "")
	{
		globalContainer->standardFont->pushStyle(Font::Style(Font::STYLE_NORMAL, Color(255,255,255)));
		globalContainer->gfx->drawString(10, globalContainer->gfx->getH()-100, globalContainer->standardFont,  Toolkit::getStringTable()->getString("[Building Tool Line Explanation]"), 0, 75);
		globalContainer->gfx->drawString(10, globalContainer->gfx->getH()-100+12, globalContainer->standardFont,  Toolkit::getStringTable()->getString("[Building Tool Box Explanation]"), 0, 75);
		globalContainer->standardFont->popStyle();
	}

	// Draw icon if trasmitting
	if (globalContainer->voiceRecorder->recordingNow)
		globalContainer->gfx->drawSprite(5, globalContainer->gfx->getH()-50, globalContainer->gamegui, 24);

	// Draw which players are transmitting voice
	int xinc = 42;
	for(int p=0; p<Team::MAX_COUNT; ++p)
	{
		if(globalContainer->mix->isPlayerTransmittingVoice(p))
		{
			if(xinc==42)
			{
				globalContainer->gamegui->setBaseColor(game.teams[game.players[p]->teamNumber]->color);
				globalContainer->gfx->drawSprite(42, globalContainer->gfx->getH()-55, globalContainer->gamegui, 30);
				xinc += 47;
			}
			int height = globalContainer->standardFont->getStringHeight(game.players[p]->name.c_str());

			globalContainer->standardFont->pushStyle(Font::Style(Font::STYLE_NORMAL, game.teams[game.players[p]->teamNumber]->color));
			globalContainer->gfx->drawString(xinc, globalContainer->gfx->getH()-35-height/2, globalContainer->standardFont, game.players[p]->name);
			xinc += globalContainer->standardFont->getStringWidth(game.players[p]->name.c_str()) + 5;
			globalContainer->standardFont->popStyle();
		}
	}

	if(!scrollableText)
		messageManager.drawAllChatMessages(32, globalContainer->gfx->getH() - 165);

	// Draw the bar contining number of units, CPU load, etc...
	drawTopScreenBar();
}

void GameGUI::drawInGameMenu(void)
{
	gameMenuScreen->dispatchPaint();
	globalContainer->gfx->drawSurface((int)gameMenuScreen->decX, (int)gameMenuScreen->decY, gameMenuScreen->getSurface());

	// Draw a-la-aqua drop shadows
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
	{
		int x = gameMenuScreen->decX;
		int y = gameMenuScreen->decY;
		int w = gameMenuScreen->getSurface()->getW();
		int h = gameMenuScreen->getSurface()->getH();

		globalContainer->gfx->drawSprite(x-8, y+h, globalContainer->terrainShader, 17);
		globalContainer->gfx->drawSprite(x+w, y+h, globalContainer->terrainShader, 18);
		globalContainer->gfx->setClipRect(x, y+h, w, 16);
		for (int i=0; i<w+31; i+=32)
		{
			globalContainer->gfx->drawSprite(x+i, y+h, globalContainer->terrainShader, 16);
		}
		globalContainer->gfx->setClipRect(x-8, y, w+16, h);
		for (int i=0; i<h+31; i+=32)
		{
			globalContainer->gfx->drawSprite(x-8, y+i, globalContainer->terrainShader, 19);
			globalContainer->gfx->drawSprite(x+w, y+i, globalContainer->terrainShader, 20);
		}
	}
}

void GameGUI::drawInGameTextInput(void)
{
	typingInputScreen->decX=(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-492)/2;
	typingInputScreen->decY=globalContainer->gfx->getH()-typingInputScreenPos;
	typingInputScreen->dispatchPaint();
	globalContainer->gfx->drawSurface((int)typingInputScreen->decX, (int)typingInputScreen->decY, typingInputScreen->getSurface());
	if (typingInputScreenInc>0)
	{
		if (typingInputScreenPos<TYPING_INPUT_MAX_POS-TYPING_INPUT_BASE_INC)
			typingInputScreenPos+=typingInputScreenInc;
		else
		{
			typingInputScreenInc=0;
			typingInputScreenPos=TYPING_INPUT_MAX_POS;
		}
	}
	else if (typingInputScreenInc<0)
	{
		if (typingInputScreenPos>TYPING_INPUT_BASE_INC)
			typingInputScreenPos+=typingInputScreenInc;
		else
		{
			typingInputScreenInc=0;
			delete typingInputScreen;
			typingInputScreen=NULL;
		}
	}
}

void GameGUI::drawInGameScrollableText(void)
{
	scrollableText->decX=28;
	scrollableText->decY=globalContainer->gfx->getH() - 165;
	scrollableText->dispatchPaint();
	globalContainer->gfx->drawSurface(scrollableText->decX, scrollableText->decY, scrollableText->getSurface());
}

void GameGUI::drawAll(int team)
{
	// draw the map
	Uint32 drawOptions =	(drawHealthFoodBar ? Game::DRAW_HEALTH_FOOD_BAR : 0) |
								(drawPathLines ?  Game::DRAW_PATH_LINE : 0) |
								(drawAccessibilityAids ? Game::DRAW_ACCESSIBILITY : 0 ) |
								((selectionMode==TOOL_SELECTION) ? Game::DRAW_BUILDING_RECT : 0) |
								((showStarvingMap) ? Game::DRAW_OVERLAY : 0) |
								((showDamagedMap) ? Game::DRAW_OVERLAY : 0) |
								((showDefenseMap) ? Game::DRAW_OVERLAY : 0) |
								((showFertilityMap) ? Game::DRAW_OVERLAY : 0) |
								((globalContainer->replaying && !globalContainer->replayShowFog) ? Game::DRAW_WHOLE_MAP : 0) |
								Game::DRAW_AREA;

	updateHilightInGame();
	arrowPositions.clear();
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
	{
		globalContainer->gfx->setClipRect(0, 16, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH()-16);
		game.drawMap(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH(), 0, 16, viewportX, viewportY, localTeamNo, drawOptions, nullptr, &buildingGuiState);
	}
	else
	{
		std::set<Building*> visibleBuildings;

		globalContainer->gfx->setClipRect();

		game.drawMap(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH(), RIGHT_MENU_WIDTH, 16, viewportX, viewportY, localTeamNo, drawOptions, &visibleBuildings, &buildingGuiState);

		// generate and draw particles
		generateNewParticles(&visibleBuildings);
		drawParticles();
	}

	///Draw ghost buildings
	if (!globalContainer->replaying) ghostManager.drawAll(viewportX, viewportY, localTeamNo);

	// if paused, tint the game area
	if (gamePaused)
	{
		std::string s;

		if (globalContainer->replaying && globalContainer->replayReader->isFinished())
		{
			s = Toolkit::getStringTable()->getString("[replay ended]");
		}
		else
		{
			globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, globalContainer->gfx->getH(), 0, 0, 0, 20);
			s = Toolkit::getStringTable()->getString("[Paused]");
		}

		int x = (globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-globalContainer->menuFont->getStringWidth(s))/2;
		globalContainer->gfx->drawString(x, globalContainer->gfx->getH()-80, globalContainer->menuFont, s);
	}

	// draw the panel
	globalContainer->gfx->setClipRect();
	drawPanel();

	// draw the minimap
	drawOptions = 0;
	//globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 0, 128, 128);
	//game.drawMiniMap(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, 0, 128, 128, viewportX, viewportY, team, drawOptions);

	globalContainer->gfx->setClipRect();
	minimap.draw(localTeamNo, viewportX, viewportY, (globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)/32, globalContainer->gfx->getH()/32 );

	// draw the progress bar if this is a replay
	if (globalContainer->replaying) drawReplayProgressBar();

	// draw the top bar and other infos
	globalContainer->gfx->setClipRect();
	drawOverlayInfos();

	// draw menu if any
	if (inGameMenu)
	{
		globalContainer->gfx->setClipRect();
		drawInGameMenu();
	}

	// draw input box if any
	if (typingInputScreen)
	{
		globalContainer->gfx->setClipRect();
		drawInGameTextInput();
	}
	if (scrollableText)
		drawInGameScrollableText();

	// draw the hilight arrows
	for(int i=0; i<(int)arrowPositions.size(); ++i)
	{
		globalContainer->gfx->drawSprite(arrowPositions[i].x, arrowPositions[i].y, globalContainer->gamegui, arrowPositions[i].sprite);

	}
}

void GameGUI::drawButton(int x, int y, std::string caption, int r, int g, int b, bool doLanguageLookup)
{
	globalContainer->gfx->drawSprite(x+8, y, globalContainer->gamegui, 12);
	globalContainer->gfx->drawFilledRect(x+17, y+3, 94, 10, r, g, b);

	std::string textToDraw;
	if (doLanguageLookup)
		textToDraw=Toolkit::getStringTable()->getString(caption);
	else
		textToDraw=caption;
	int len=globalContainer->littleFont->getStringWidth(textToDraw);
	int h=globalContainer->littleFont->getStringHeight(textToDraw);
	globalContainer->gfx->drawString(x+17+((94-len)>>1), y+((16-h)>>1), globalContainer->littleFont, textToDraw);
}

void GameGUI::drawBlueButton(int x, int y, std::string caption, bool doLanguageLookup)
{
	drawButton(x,y,caption,128,128,192,doLanguageLookup);
}

void GameGUI::drawRedButton(int x, int y, std::string caption, bool doLanguageLookup)
{
	drawButton(x,y,caption,192,128,128,doLanguageLookup);
}

void GameGUI::drawTextCenter(int x, int y, std::string caption)
{
	std::string text;

	text=Toolkit::getStringTable()->getString(caption);
	int dec=(RIGHT_MENU_WIDTH-globalContainer->littleFont->getStringWidth(text))>>1;
	globalContainer->gfx->drawString(x+dec, y, globalContainer->littleFont, text);
}

// Draws a two-channel scrollbox bar. `valueLocal` is the local/pending value the
// user has dialed in (drawn as the lighter localBar); `act` is the simulation-
// confirmed value (drawn as the darker actualBar on top). When the two agree
// the actualBar fully overlays the localBar; while an order is in flight they
// differ briefly. `max` is the divisor for both channels.
void GameGUI::drawScrollBox(int x, int y, int valueLocal, int act, int max)
{
	//scrollbar borders
	globalContainer->gfx->setClipRect(x+8, y, 112, 16);
	globalContainer->gfx->drawSprite(x+8, y, globalContainer->gamegui, 9);

	//localBar
	int size=(valueLocal*92)/max;
	globalContainer->gfx->setClipRect(x+18, y, size, 16);
	globalContainer->gfx->drawSprite(x+18, y+3, globalContainer->gamegui, 10);

	//actualBar
	size=(act*92)/max;
	globalContainer->gfx->setClipRect(x+18, y, size, 16);
	globalContainer->gfx->drawSprite(x+18, y+4, globalContainer->gamegui, 11);

	globalContainer->gfx->setClipRect();
}

void GameGUI::drawXPProgressBar(int x, int y, int act, int max)
{
	globalContainer->gfx->setClipRect(x+8, y, 112, 16);

	globalContainer->gfx->setClipRect(x+18, y, 92, 16);
	globalContainer->gfx->drawSprite(x+18, y+3, globalContainer->gamegui, 10);

	globalContainer->gfx->setClipRect(x+18, y, (act*92)/max, 16);
	globalContainer->gfx->drawSprite(x+18, y+4, globalContainer->gamegui, 11);

	globalContainer->gfx->setClipRect();
}
