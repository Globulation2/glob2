/*
  Copyright (C) 2006-2008 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "FormatableString.h"
#include "Game.h"
#include "AINames.h"
#include "GameHeader.h"
#include "GlobalContainer.h"
#include "GUIButton.h"
#include "GUIText.h"
#include "GUITextInput.h"
#include "MapEditDialog.h"
#include "MapHeader.h"
#include "StringTable.h"
#include "Toolkit.h"

MapEditMenuScreen::MapEditMenuScreen() : OverlayScreen(globalContainer->gfx, 370, 310)
{
	addWidget(new TextButton(0, 10, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[load map]"), LOAD_MAP));
	addWidget(new TextButton(0, 60, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[save map]"), SAVE_MAP));
	addWidget(new TextButton(0, 110, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[open scenario editor]"), OPEN_SCRIPT_EDITOR, 27));
	addWidget(new TextButton(0, 160, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[open teams editor]"), OPEN_TEAMS_EDITOR, 27));
	addWidget(new TextButton(0, 210, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[quit the editor]"), QUIT_EDITOR));
	addWidget(new TextButton(0, 260, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[return to editor]"), RETURN_EDITOR, 27));
	dispatchInit();
}

void MapEditMenuScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
		endValue=par1;
}




AskForTextInput::AskForTextInput(const std::string& aLabel, const std::string& aCurrent) : OverlayScreen(globalContainer->gfx, 300, 120), labelText(aLabel), currentText(aCurrent)
{
	label = new Text(0, 5, ALIGN_FILL, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString(labelText.c_str()));
	textEntry = new TextInput(10, 35, 280, 25, ALIGN_LEFT, ALIGN_LEFT, "standard", currentText, true);
	ok = new TextButton(10, 70, 135, 40, ALIGN_LEFT, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[ok]"), OK);
	cancel =  new TextButton(155, 70, 135, 40, ALIGN_LEFT, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL);
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



TeamsEditor::TeamsEditor(Game* game)
 : OverlayScreen(globalContainer->gfx, 500, 420), game(game)
{
	addWidget(new Text(0, 5, ALIGN_FILL, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[teams editor]")));
	addWidget(new TextButton(155, 370, 135, 40, ALIGN_RIGHT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[ok]"), OK));
	addWidget(new TextButton(10, 370, 135, 40, ALIGN_RIGHT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL));

	GameHeader& gameHeader = game->gameHeader;
	MapHeader& mapHeader = game->mapHeader;

	for(int i=0; i<NumberOfPlayerSelectors; ++i)
	{
		isPlayerActive[i] = new OnOffButton(10, 60+i*25, 21, 21, ALIGN_LEFT, ALIGN_TOP, gameHeader.getBasePlayer(i).type != BasePlayer::P_NONE, 100+i);
		addWidget(isPlayerActive[i]);
		if(i==0)
		{
			isPlayerActive[i]->visible=false;
		}

		color[i] = new ColorButton(35, 60+25*i, 21, 21, ALIGN_LEFT, ALIGN_TOP, 200+i);
		for (int j = 0; j<mapHeader.getNumberOfTeams(); j++)
			color[i]->addColor(mapHeader.getBaseTeam(j).color);
		color[i]->setSelectedColor(gameHeader.getBasePlayer(i).teamNumber);
		addWidget(color[i]);

		if(i==0)
		{
			playerName[i] = new Text(60, 60+25*i, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[Human]"));
			aiSelector[i]=NULL;
			addWidget(playerName[i]);
		}
		else
		{
			playerName[i]=NULL;
			aiSelector[i]=new MultiTextButton(60, 60+i*25, 100, 21, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[AI]"), 300+i);
			for (int aii=0; aii<AI::SIZE; aii++)
				aiSelector[i]->addText(AINames::getAIText(aii));
			if(gameHeader.getBasePlayer(i).type >= BasePlayer::P_AI)
				aiSelector[i]->setIndex(gameHeader.getBasePlayer(i).type - BasePlayer::P_AI);
			else
				aiSelector[i]->setIndex(AI::NONE);
			addWidget(aiSelector[i]);
		}

		allyTeamNumbers[i] = new MultiTextButton(185, 60+25*i, 21, 21, ALIGN_LEFT, ALIGN_TOP, "standard", "", 400+i);
		allyTeamNumbers[i]->clearTexts();
		for(int j=0; j<mapHeader.getNumberOfTeams(); ++j)
		{
			std::stringstream s;
			s<<j+1;
			allyTeamNumbers[i]->addText(s.str());
		}
		allyTeamNumbers[i]->setIndex(gameHeader.getAllyTeamNumber(gameHeader.getBasePlayer(i).teamNumber)-1);
		addWidget(allyTeamNumbers[i]);


		if(gameHeader.getBasePlayer(i).type == BasePlayer::P_NONE)
		{
			color[i]->visible=false;
			allyTeamNumbers[i]->visible=false;
			if(aiSelector[i])
				aiSelector[i]->visible=false;
			if(playerName[i])
				playerName[i]->visible=false;
		}
	}
	dispatchInit();
}



void TeamsEditor::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if(par1==OK)
		{
			generateGameHeader();
			endValue=OK;
		}
		else if(par1==CANCEL)
		{
			endValue=CANCEL;
		}
		else if(par1>=100 && par1<200)
		{
			int n = par1-100;
			color[n]->visible=isPlayerActive[n]->getState();
			aiSelector[n]->visible=isPlayerActive[n]->getState();
			allyTeamNumbers[n]->visible=isPlayerActive[n]->getState();
		}
	}
	if(action==BUTTON_PRESSED || action==BUTTON_SHORTCUT)
	{
		if(par1>=200 && par1<300)
		{
			int n = par1-200;
			for(int i=0; i<NumberOfPlayerSelectors; ++i)
			{
				if(color[i]->getSelectedColor() == color[n]->getSelectedColor() && i!=n)
				{
					allyTeamNumbers[n]->setIndex(allyTeamNumbers[i]->getIndex());
				}
			}
		}

		if(par1>=400)
		{
			GameHeader& gameHeader = game->gameHeader;
			int team = -1;
			int nth = 0;
			int n = 0;
			///Find which team number this widget is for
			for(int i=0; i<gameHeader.getNumberOfPlayers(); ++i)
			{
				if(allyTeamNumbers[i] == source)
				{
					team = color[i]->getSelectedColor();
					nth = allyTeamNumbers[i]->getIndex();
					n = nth+1;
					break;
				}
			}
			///Adjust all widgets that have this team number
			for(int i=0; i<gameHeader.getNumberOfPlayers(); ++i)
			{
				if(gameHeader.getBasePlayer(i).teamNumber == team)
				{
					allyTeamNumbers[i]->setIndex(nth);
				}
			}
		}
	}
}

void TeamsEditor::generateGameHeader()
{
	GameHeader gameHeader;
	int count = 0;
	for (int i=0; i<NumberOfPlayerSelectors; i++)
	{
		if (isPlayerActive[i]->getState())
		{
			int teamColor=color[i]->getSelectedColor();
			if (i==0)
			{
				gameHeader.getBasePlayer(count) = BasePlayer(0, Toolkit::getStringTable()->getString("[Human]"), teamColor, BasePlayer::P_LOCAL);
			}
			else
			{
				AI::ImplementitionID iid=static_cast<AI::ImplementitionID>(aiSelector[i]->getIndex());
				FormatableString name("%0 %1");
				name.arg(AINames::getAIText(iid)).arg(i-1);
				gameHeader.getBasePlayer(count) = BasePlayer(i, name.c_str(), teamColor, BasePlayer::playerTypeFromImplementitionID(iid));
			}
			gameHeader.setAllyTeamNumber(teamColor, allyTeamNumbers[i]->getIndex()+1);
			count+=1;
		}
		else
		{
			gameHeader.getBasePlayer(i) = BasePlayer();
		}
	}
	gameHeader.setNumberOfPlayers(count);
	game->setGameHeader(gameHeader);
}
