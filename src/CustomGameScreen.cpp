// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "CustomGameScreen.h"
#include "Utilities.h"
#include "AINames.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "GUIGlob2FileList.h"
#include "GUIMapPreview.h"
#include <GUIButton.h>
#include <GUIText.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <Stream.h>
#include <FormatableString.h>
#include "Player.h"
#include "CustomGameOtherOptions.h"
#include "AIDescriptionScreen.h"

CustomGameScreen::CustomGameScreen() :
	ChooseMapScreen("maps", "map", true)
{
	for (int i=0; i<Team::MAX_COUNT; i++)
	{
		isPlayerActive[i]=new OnOffButton(230, 60+i*25, 21, 21, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, i == 0, 100+i);
		addWidget(isPlayerActive[i]);
		color[i]=new ColorButton(265, 60+i*25, 21, 21, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 200+i);
		addWidget(color[i]);
		if (i==0)
		{
			closedText[i]=new Text(300, 60+i*25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", globalContainer->settings.getUsername());
			addWidget(closedText[i]);
			
			aiSelector[i]=NULL;
		}
		else
		{
			color[i]->hide();
			
			closedText[i]=new Text(300, 60+i*25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[closed]"));
			addWidget(closedText[i]);
			
			aiSelector[i]=new MultiTextButton(300, 60+i*25, 100, 21, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[AI]"), 300+i);
			for (int aii=0; aii<AI::SIZE; aii++)
				aiSelector[i]->addText(AINames::getAIText(aii));
			addWidget(aiSelector[i]);
			aiSelector[i]->hide();
			aiSelector[i]->setIndex(AI::NUMBI);
		}
	}
	otherOptions = new TextButton(230, 420, 170, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Other Options]"), 0);
	addWidget(otherOptions);
	aiDescriptions = new TextButton(230, 370, 170, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[AI Descriptions]"), 0);
	addWidget(aiDescriptions);
	
	otherOptions->visible=false;
}



CustomGameScreen::~CustomGameScreen()
{
}



// Invoked by ChooseMapScreen::updateMapInformation when the user picks a map.
// Lays out the player-selector widgets to match the map's team count. The
// widget arrays are sized Team::MAX_COUNT, which is also the engine-wide cap
// on team count (enforced by MapHeader::load), so the loops below cannot
// index past array end.
void CustomGameScreen::validMapSelectedhandler(void)
{
	int i;
	// set the correct number of colors
	for (i = 0; i<Team::MAX_COUNT; i++)
	{
		color[i]->clearColors();
		for (int j = 0; j<mapHeader.getNumberOfTeams(); j++)
			color[i]->addColor(mapHeader.getBaseTeam(j).color);
		color[i]->setSelectedColor();
	}
	// find team for human player, not in every map
	for (i = 0; i<mapHeader.getNumberOfTeams(); i++)
	{
		if (mapHeader.getBaseTeam(i).type == BaseTeam::T_HUMAN)
		{
			color[0]->setSelectedColor(i);
			break;
		}
	}
	// Fill the others
	int c = color[0]->getSelectedColor();
	for (i = 1; i<mapHeader.getNumberOfTeams(); i++)
	{
		c = (c+1)%mapHeader.getNumberOfTeams();
		color[i]->setSelectedColor(c);
		color[i]->show();
		isPlayerActive[i]->setState(true);
		closedText[i]->hide();
		aiSelector[i]->show();
	}
	// Close the rest
	for (; i<Team::MAX_COUNT; i++)
	{
		isPlayerActive[i]->setState(false);
		color[i]->hide();
		aiSelector[i]->hide();
		closedText[i]->show();
	}
	updatePlayers();
	otherOptions->visible=true;
}



void CustomGameScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	// call parent
	ChooseMapScreen::onAction(source, action, par1, par2);
	if (action==BUTTON_STATE_CHANGED)
	{
		if (par1==100)
		{
			isPlayerActive[0]->setState(true);
		}
		else if ((par1>100) && (par1<200))
		{
			int n=par1-100;
			if (isPlayerActive[n]->getState())
			{
				color[n]->show();
				closedText[n]->hide();
				aiSelector[n]->show();
			}
			else
			{
				color[n]->hide();
				closedText[n]->show();
				aiSelector[n]->hide();
			}
		}
		updatePlayers();
	}
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if(source == otherOptions)
		{
			CustomGameOtherOptions settings(gameHeader, mapHeader, false);
			int rc = settings.execute(globalContainer->gfx, 40);
			if(rc == -1)
				endExecute(-1);
		}
		if(source == aiDescriptions)
		{
			AIDescriptionScreen descriptions;
			int rc = descriptions.execute(globalContainer->gfx, 40);
			if(rc == -1)
				endExecute(-1);
		}
	}
}



bool CustomGameScreen::isActive(int i)
{
	return isPlayerActive[i]->getState();
}



AI::ImplementitionID CustomGameScreen::getAiImplementation(int i)
{
	return (AI::ImplementitionID)aiSelector[i]->getIndex();
}



int CustomGameScreen::getSelectedColor(int i)
{
	return color[i]->getSelectedColor();
}



namespace
{
	// 1-based ally-team IDs. GameHeader::reset() seeds allyTeamNumbers[i] = i+1,
	// so team 1 and team 2 are the lowest two groups available.
	constexpr Uint8 HUMAN_ALLY_TEAM = 1;
	constexpr Uint8 ENEMY_ALLY_TEAM = 2;

	FormatableString aiSelectorName(AI::ImplementitionID iid, int selectorIndex)
	{
		// selectorIndex is the position in the visible selector list; selector 0
		// is the human, so AI selectors start at 1 and display as "AI Name N"
		// with N = selectorIndex - 1.
		FormatableString name("%0 %1");
		name.arg(AINames::getAIText(iid)).arg(selectorIndex - 1);
		return name;
	}
}

// Rebuilds gameHeader.players[] from the current selector widget state.
// Active selectors are packed into slots [0..count); slots [count..MAX_COUNT)
// are cleared to default BasePlayer{} so stale entries from earlier edits
// (or earlier map selections) cannot leak into save files or network packets,
// which serialize all MAX_COUNT_ON_DISK slots regardless of numberOfPlayers.
// Ally teams: the human's color goes on HUMAN_ALLY_TEAM, every other color
// on ENEMY_ALLY_TEAM.
void CustomGameScreen::updatePlayers()
{
	for (int i = 0; i < Team::MAX_COUNT; i++)
		gameHeader.getBasePlayer(i) = BasePlayer();

	int count = 0;
	int humanColor = 0;
	for (int i = 0; i < Team::MAX_COUNT; i++)
	{
		if (!isActive(i))
			continue;

		int teamColor = getSelectedColor(i);
		if (i == 0)
		{
			gameHeader.getBasePlayer(count) = BasePlayer(0, globalContainer->settings.getUsername().c_str(), teamColor, BasePlayer::P_LOCAL);
			humanColor = teamColor;
			gameHeader.setAllyTeamNumber(teamColor, HUMAN_ALLY_TEAM);
		}
		else
		{
			AI::ImplementitionID iid = getAiImplementation(i);
			FormatableString name = aiSelectorName(iid, i);
			gameHeader.getBasePlayer(count) = BasePlayer(i, name.c_str(), teamColor, Player::playerTypeFromImplementitionID(iid));
			if (teamColor != humanColor)
				gameHeader.setAllyTeamNumber(teamColor, ENEMY_ALLY_TEAM);
		}
		count += 1;
	}
	gameHeader.setNumberOfPlayers(count);
}



