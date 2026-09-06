// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "CustomGameOtherOptions.h"

#include <Toolkit.h>
#include <StringTable.h>
#include <GUIButton.h>
#include <GUIText.h>
#include <optional>
#include <sstream>

namespace
{
	/// Ally team numbers in GameHeader are 1-based (its constructor assigns
	/// team i the value i+1), while the ally-team widget rows are 0-based.
	/// A malformed or uninitialized header can carry values outside
	/// [1, teamCount] -- notably 0, which would underflow to setIndex(-1)
	/// and throw inside MultiTextButton::setIndex. Map any such value to a
	/// defined widget state: the first entry (index 0).
	int allyTeamNumberToWidgetIndex(Uint8 allyTeamNumber, int teamCount)
	{
		const int index = static_cast<int>(allyTeamNumber) - 1;
		if (index < 0 || index >= teamCount)
			return 0;
		return index;
	}
}

CustomGameOtherOptions::CustomGameOtherOptions(GameHeader& gameHeader, MapHeader& mapHeader, bool readOnly)
	:	gameHeader(gameHeader), oldGameHeader(gameHeader)
{
	ok = new TextButton(440, (readOnly ? 420 : 360), 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13);
	addWidget(ok);
	
	if(!readOnly)
	{
		cancel = new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27);
		addWidget(cancel);
	}
	
	title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Other Options]"));
	addWidget(title);

	for(int i=0; i<gameHeader.getNumberOfPlayers(); ++i)
	{
		playerNames[i] = new Text(125, 60+25*i, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", gameHeader.getBasePlayer(i).name);
		color[i] = new ColorButton(100, 60+25*i, 21, 21, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 100+i);
		allyTeamNumbers[i] = new MultiTextButton(250, 60+25*i, 21, 21, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 200+i);
		allyTeamNumbers[i]->clearTexts();
		for(int j=0; j<mapHeader.getNumberOfTeams(); ++j)
		{
			std::stringstream s;
			s<<j+1;
			allyTeamNumbers[i]->addText(s.str());
		}
		allyTeamNumbers[i]->setIndex(allyTeamNumberToWidgetIndex(
			gameHeader.getAllyTeamNumber(gameHeader.getBasePlayer(i).teamNumber),
			mapHeader.getNumberOfTeams()));

		color[i]->clearColors();
		color[i]->addColor(mapHeader.getBaseTeam(gameHeader.getBasePlayer(i).teamNumber).color);
		color[i]->setSelectedColor(0);
		
		if(readOnly)
		{
			allyTeamNumbers[i]->setClickable(false);
		}
		
		addWidget(playerNames[i]);
		addWidget(color[i]);
		addWidget(allyTeamNumbers[i]);
	}
	
	teamsFixed = new OnOffButton(300, 60, 21, 21, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, gameHeader.areAllyTeamsFixed(), TEAMSFIXED);
	addWidget(teamsFixed);
	teamsFixedText = new Text(325, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Teams Fixed]"));
	addWidget(teamsFixedText);
	if(readOnly)
		teamsFixed->setClickable(false);
	
	//These are for winning conditions
	prestigeWinEnabled = new OnOffButton(300, 90, 21, 21, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, true, PRESTIGEWINENABLED);
	addWidget(prestigeWinEnabled);
	prestigeWinEnabledText = new Text(325, 90, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Prestige Win Enabled]"));
	addWidget(prestigeWinEnabledText);
	updateScreenWinningConditions();
	if(readOnly)
		prestigeWinEnabled->setClickable(false);
	
	//Map discovered.
	mapDiscovered = new OnOffButton(300, 120, 21, 21, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, gameHeader.isMapDiscovered(), MAPDISCOVERED);
	addWidget(mapDiscovered);
	mapDiscoveredText = new Text(325, 120, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Map Discovered]"));
	addWidget(mapDiscoveredText);
	if(readOnly)
		mapDiscovered->setClickable(false);
}



CustomGameOtherOptions::~CustomGameOtherOptions()
{

}


void CustomGameOtherOptions::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if(par1 == OK)
		{
			endExecute(Finished);
		}
		if(par1 == CANCEL)
		{
			gameHeader = oldGameHeader;
			endExecute(Canceled);
		}
	}
	else if (action==BUTTON_STATE_CHANGED)
	{
		if(par1>=200 && par1<300)
		{
			///Find which player row this widget is for. The lookup can
			///genuinely fail (e.g. a stale event for a widget of a player
			///slot that no longer exists); there is nothing to update then.
			std::optional<int> playerRow;
			for(int i=0; i<gameHeader.getNumberOfPlayers(); ++i)
			{
				if(allyTeamNumbers[i] == source)
				{
					playerRow = i;
					break;
				}
			}
			if(!playerRow)
				return;
			const int team = gameHeader.getBasePlayer(*playerRow).teamNumber;
			const int allyIndex = allyTeamNumbers[*playerRow]->getIndex();
			///Adjust all widgets that have this team number
			for(int i=0; i<gameHeader.getNumberOfPlayers(); ++i)
			{
				if(gameHeader.getBasePlayer(i).teamNumber == team)
				{
					allyTeamNumbers[i]->setIndex(allyIndex);
				}
			}
			///Widget indices are 0-based; ally team numbers are 1-based
			gameHeader.setAllyTeamNumber(team, allyIndex+1);
		}
		else if(par1 == TEAMSFIXED)
		{
			gameHeader.setAllyTeamsFixed(teamsFixed->getState());
		}
		else if(par1 == PRESTIGEWINENABLED)
		{
			updateGameHeaderWinningConditions();
		}
		else if(par1 == MAPDISCOVERED)
		{
			gameHeader.setMapDiscovered(mapDiscovered->getState());
		}
	}
}



void CustomGameOtherOptions::updateGameHeaderWinningConditions()
{
	WinningCondition::setPrestigeWinCondition(gameHeader.getWinningConditions(), prestigeWinEnabled->getState());
}



void CustomGameOtherOptions::updateScreenWinningConditions()
{
	std::list<std::shared_ptr<WinningCondition> >& winningConditions = gameHeader.getWinningConditions();
	
	//Update the prestige condition
	prestigeWinEnabled->setState(false);
	for(std::list<std::shared_ptr<WinningCondition> >::iterator i = winningConditions.begin(); i!=winningConditions.end(); ++i)
	{
		if((*i)->getType() == WCPrestige)
		{
			prestigeWinEnabled->setState(true);
			break;
		}
	}
}

