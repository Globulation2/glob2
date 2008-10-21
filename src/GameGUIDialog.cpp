/*
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

#include "boost/lexical_cast.hpp"
#include "GameGUIDialog.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "GUIAnimation.h"
#include "GUIButton.h"
#include "GUISelector.h"
#include "GUITextArea.h"
#include "GUIText.h"
#include "Player.h"
#include "SoundMixer.h"
#include "StringTable.h"
#include "Toolkit.h"


//! Main menu screen
InGameMainScreen::InGameMainScreen()
:OverlayScreen(globalContainer->gfx, 320, 260)
{
	addWidget(new TextButton(0, 10, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[load game]"), LOAD_GAME));
	addWidget(new TextButton(0, 60, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[save game]"), SAVE_GAME));
	addWidget(new TextButton(0, 110, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[options]"), OPTIONS));
	addWidget(new TextButton(0, 160, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[quit the game]"), QUIT_GAME));
	addWidget(new TextButton(0, 210, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[return to game]"), RETURN_GAME, 27));
	dispatchInit();
}

void InGameMainScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
		endValue=par1;
}

InGameEndOfGameScreen::InGameEndOfGameScreen(const char *title, bool canContinue)
:OverlayScreen(globalContainer->gfx, 320, canContinue ? 150 : 100)
{
	addWidget(new Text(0, 10, ALIGN_FILL, ALIGN_LEFT, "menu", title));
	addWidget(new TextButton(10, 50, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu",  Toolkit::getStringTable()->getString("[ok]"), QUIT, 13));
	if (canContinue)
		addWidget(new TextButton(10, 100, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu",  Toolkit::getStringTable()->getString("[Continue playing]"), CONTINUE, 27));
	dispatchInit();
}

void InGameEndOfGameScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
		{
			endValue=par1;
		}
}

//! Alliance screen
InGameAllianceScreen::InGameAllianceScreen(GameGUI *gameGUI)
:OverlayScreen(globalContainer->gfx, (gameGUI->game.gameHeader.getNumberOfPlayers() - countNumberPlayersForLocalTeam(gameGUI->game.gameHeader, gameGUI->localTeamNo)<=8) ? 320 : 600, 395)
{

	// fill the slots
	int i;
	int xBase=0;
	int yBase=0;
	int n=0;
	for (i=0; i<gameGUI->game.gameHeader.getNumberOfPlayers(); i++)
	{
		int otherTeam = gameGUI->game.players[i]->teamNumber;
		Uint32 otherTeamMask = 1 << otherTeam;

		std::string pname;
		if (gameGUI->game.players[i]->type>=Player::P_AI || gameGUI->game.players[i]->type==Player::P_IP || gameGUI->game.players[i]->type==Player::P_LOCAL)
		{
			pname = gameGUI->game.players[i]->name;
		}
		else
		{
			pname ="(";
			pname += gameGUI->game.players[i]->name;
			pname += ")";
		}

		texts[i] = new Text(10+xBase, 37+yBase, ALIGN_LEFT, ALIGN_LEFT, "menu", pname.c_str());
		Team *team = gameGUI->game.players[i]->team;
		texts[i]->setStyle(Font::Style(Font::STYLE_NORMAL, team->color));
		addWidget(texts[i]);
		
	
		alliance[i]=new OnOffButton(172+xBase, 40+yBase,  20, 20, ALIGN_LEFT, ALIGN_LEFT, (gameGUI->localTeam->allies & otherTeamMask) != 0, ALLIED+i);
		addWidget(alliance[i]);
	
		normalVision[i]=new OnOffButton(196+xBase, 40+yBase,  20, 20, ALIGN_LEFT, ALIGN_LEFT, (gameGUI->localTeam->sharedVisionOther & otherTeamMask) != 0, NORMAL_VISION+i);
		addWidget(normalVision[i]);
		
		if(gameGUI->game.gameHeader.areAllyTeamsFixed())
		{
			alliance[i]->visible=false;
			normalVision[i]->visible=false;
		}
		
		foodVision[i]=new OnOffButton(220+xBase, 40+yBase,  20, 20, ALIGN_LEFT, ALIGN_LEFT, (gameGUI->localTeam->sharedVisionFood & otherTeamMask) != 0, FOOD_VISION+i);
		addWidget(foodVision[i]);
		
		marketVision[i]=new OnOffButton(244+xBase, 40+yBase,  20, 20, ALIGN_LEFT, ALIGN_LEFT, (gameGUI->localTeam->sharedVisionExchange & otherTeamMask) != 0, MARKET_VISION+i);
		addWidget(marketVision[i]);

		bool chatState = (((gameGUI->chatMask)&(1<<i))!=0);
		chat[i]=new OnOffButton(268+xBase, 40+yBase, 20, 20, ALIGN_LEFT, ALIGN_LEFT, chatState, CHAT+i);
		addWidget(chat[i]);
		
		if(otherTeam == gameGUI->localTeamNo)
		{
			alliance[i]->visible=false;
			normalVision[i]->visible=false;
			texts[i]->visible=false;
			foodVision[i]->visible=false;
			marketVision[i]->visible=false;
			chat[i]->visible=false;
		}
		else
		{
			yBase += 25;
			if(n==7)
			{
				xBase += 300;
				yBase = 0;
			}
			n+=1;
		}
	}
	for (;i<16;i++)
	{
		texts[i] = NULL;
		alliance[i] = NULL;
		normalVision[i] = NULL;
		foodVision[i] = NULL;
		marketVision[i] = NULL;
		chat[i] = NULL;
	}
	
	//Put locks if needed
	if(gameGUI->game.gameHeader.areAllyTeamsFixed())
	{
		int np = std::max(2, gameGUI->game.gameHeader.getNumberOfPlayers() - countNumberPlayersForLocalTeam(gameGUI->game.gameHeader, gameGUI->localTeamNo));
		//Although this is the animation widget, we are just using it to display a still frame
		addWidget(new Animation(172, 40 + std::min(4, np/2)*25 - 16, ALIGN_LEFT, ALIGN_TOP, "data/gfx/gamegui", 35));
		addWidget(new Animation(196, 40 + std::min(4, np/2)*25 - 16, ALIGN_LEFT, ALIGN_TOP, "data/gfx/gamegui", 35));
		
		if(np>8)
		{
			addWidget(new Animation(172, 40 + std::min(4, (np-8)/2)*25 - 16, ALIGN_LEFT, ALIGN_TOP, "data/gfx/gamegui", 35));
			addWidget(new Animation(196, 40 + std::min(4, (np-8)/2)*25 - 16, ALIGN_LEFT, ALIGN_TOP, "data/gfx/gamegui", 35));
		}
	}

	// add static text and images
	addWidget(new Text(172+3, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "A")); 
	addWidget(new Text(196+3, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "V"));
	addWidget(new Text(220, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "fV"));
	addWidget(new Text(244, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "mV"));
	addWidget(new Text(268+3, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "C"));
	
	if (gameGUI->game.gameHeader.getNumberOfPlayers() > 8)
	{
		addWidget(new Text(300+172+3, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "A")); 
		addWidget(new Text(300+196+3, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "V"));
		addWidget(new Text(300+220, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "fV"));
		addWidget(new Text(300+244, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "mV"));
		addWidget(new Text(300+268+3, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "C"));
	}
	
	// add ok button
	addWidget(new TextButton(0, 345, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 27));
	
	// add keyboard shortcut explanations
	addWidget(new Text(10, 245, ALIGN_LEFT, ALIGN_LEFT, "little", Toolkit::getStringTable()->getString("[abreaviation explanation A]")));
	addWidget(new Text(10, 258, ALIGN_LEFT, ALIGN_LEFT, "little", Toolkit::getStringTable()->getString("[abreaviation explanation V]")));
	addWidget(new Text(10, 271, ALIGN_LEFT, ALIGN_LEFT, "little", Toolkit::getStringTable()->getString("[abreaviation explanation fV]")));
	addWidget(new Text(10, 284, ALIGN_LEFT, ALIGN_LEFT, "little", Toolkit::getStringTable()->getString("[abreaviation explanation mV]")));
	addWidget(new Text(10, 297, ALIGN_LEFT, ALIGN_LEFT, "little", Toolkit::getStringTable()->getString("[abreaviation explanation C]")));
	
	addWidget(new Text(10, 310, ALIGN_LEFT, ALIGN_LEFT, "little", Toolkit::getStringTable()->getString("[shortcut explanation enter]")));
	addWidget(new Text(10, 323, ALIGN_LEFT, ALIGN_LEFT, "little", Toolkit::getStringTable()->getString("[shortcut explanation v]")));
	
	this->gameGUI=gameGUI;
	dispatchInit();
}

void InGameAllianceScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		endValue = par1;
	}
	else if (action == BUTTON_STATE_CHANGED)
	{
		for (int i=0; i<gameGUI->game.gameHeader.getNumberOfPlayers(); i++)
			if ((source == alliance[i]) ||
				(source == normalVision[i]) ||
				(source == foodVision[i]) ||
				(source == marketVision[i]))
			{
				setCorrectValueForPlayer(i);
				break;
			}
	}
}



int InGameAllianceScreen::countNumberPlayersForLocalTeam(GameHeader& gameHeader, int localteam)
{
	int count = 0;
	for (int i=0; i<gameHeader.getNumberOfPlayers(); i++)
	{
		if(gameHeader.getBasePlayer(i).teamNumber == localteam)
		{
			count += 1;
		}
	}
	return count;
}



void InGameAllianceScreen::setCorrectValueForPlayer(int i)
{
	Game *game=&(gameGUI->game);
	assert(i<game->gameHeader.getNumberOfPlayers());
	for (int j=0; j<game->gameHeader.getNumberOfPlayers(); j++)
	{
		if (j != i)
		{
			// if two players are the same team, we must have the same alliance and vision
			if (game->players[j]->teamNumber == game->players[i]->teamNumber)
			{
				alliance[j]->setState(alliance[i]->getState());
				normalVision[j]->setState(normalVision[i]->getState());
				foodVision[j]->setState(foodVision[i]->getState());
				marketVision[j]->setState(marketVision[i]->getState());
			}
		}
	}
}

Uint32 InGameAllianceScreen::getAlliedMask(void)
{
	Uint32 mask = 0;
	for (int i=0; i<gameGUI->game.gameHeader.getNumberOfPlayers(); i++)
	{
		if (alliance[i]->getState())
			mask |= 1<<i;
	}
	return mask;
}

Uint32 InGameAllianceScreen::getEnemyMask(void)
{
	Uint32 mask = 0;
	for (int i=0; i<gameGUI->game.gameHeader.getNumberOfPlayers(); i++)
	{
		if (!alliance[i]->getState())
			mask |= 1<<i;
	}
	return mask;
}

Uint32 InGameAllianceScreen::getExchangeVisionMask(void)
{
	Uint32 mask = 0;
	for (int i=0; i<gameGUI->game.gameHeader.getNumberOfPlayers(); i++)
	{
		if (marketVision[i]->getState())
			mask |= 1<<i;
	}
	return mask;
}

Uint32 InGameAllianceScreen::getFoodVisionMask(void)
{
	Uint32 mask = 0;
	for (int i=0; i<gameGUI->game.gameHeader.getNumberOfPlayers(); i++)
	{
		if (foodVision[i]->getState())
			mask |= 1<<i;
	}
	return mask;
}

Uint32 InGameAllianceScreen::getOtherVisionMask(void)
{
	Uint32 mask = 0;
	for (int i=0; i<gameGUI->game.gameHeader.getNumberOfPlayers(); i++)
	{
		if (normalVision[i]->getState())
			mask |= 1<<i;
	}
	return mask;
}


Uint32 InGameAllianceScreen::getChatMask(void)
{
	Uint32 mask = 0;
	for (int i=0; i<gameGUI->game.gameHeader.getNumberOfPlayers(); i++)
	{
		if (chat[i]->getState())
			mask |= 1<<i;
	}
	return mask;
}

//! Option Screen
InGameOptionScreen::InGameOptionScreen(GameGUI *gameGUI)
:OverlayScreen(globalContainer->gfx, 320, 300)
{
	Text *audioMuteText=new Text(10, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[Mute]"), 200);
	addWidget(audioMuteText);

	mute = new OnOffButton(19, 50, 20, 20, ALIGN_LEFT, ALIGN_TOP, globalContainer->settings.mute, MUTE);
	addWidget(mute);	

	musicVolText=new Text(10, 80, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[Music volume]"));
	addWidget(musicVolText);

	voiceVolText=new Text(10, 130, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[Voice volume]"));
	addWidget(voiceVolText);
	
	musicVol=new Selector(19, 110, ALIGN_LEFT, ALIGN_TOP, 256, globalContainer->settings.musicVolume, 256, true);
	addWidget(musicVol);
	
	voiceVol=new Selector(19, 160, ALIGN_LEFT, ALIGN_TOP, 256, globalContainer->settings.voiceVolume, 256, true);
	addWidget(voiceVol);

	if(globalContainer->settings.mute)
	{
		musicVol->visible=false;
		voiceVol->visible=false;
		musicVolText->visible=false;
		voiceVolText->visible=false;
	}

	addWidget(new TextButton(0, 250, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 27));
	
	std::ostringstream oss;
	oss << globalContainer->gfx->getW() << "x" << globalContainer->gfx->getH();
	if (globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
		oss << " GL";
	else
		oss << " SDL";
		
	addWidget(new Text(0, 200, ALIGN_FILL, ALIGN_TOP, "standard", oss.str().c_str()));
	dispatchInit();
}



InGameOptionScreen::~InGameOptionScreen()
{
	globalContainer->settings.save();
}



void InGameOptionScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		endValue=par1;
	}
	else if (action==VALUE_CHANGED)
	{
		globalContainer->settings.musicVolume = musicVol->getValue();
		globalContainer->settings.voiceVolume = voiceVol->getValue();
		globalContainer->mix->setVolume(musicVol->getValue(), voiceVol->getValue(), mute->getState());
	}
	else if (action==BUTTON_STATE_CHANGED)
	{
		globalContainer->settings.mute = mute->getState();
		musicVol->visible = ! globalContainer->settings.mute;
		voiceVol->visible = ! globalContainer->settings.mute;
		musicVolText->visible = ! globalContainer->settings.mute;
		voiceVolText->visible = ! globalContainer->settings.mute;
		globalContainer->mix->setVolume(musicVol->getValue(), voiceVol->getValue(), mute->getState());
	}
}



InGameObjectivesScreen::InGameObjectivesScreen(GameGUI* gui, bool showBriefing)
:OverlayScreen(globalContainer->gfx, 470, 390)
{

	int second_offset = 0;
	int hints_x = 317;
	if(gui->game.missionBriefing.empty())
	{
		hints_x = 163;
		second_offset = 163/2;
	}
	else
	{
		addWidget(new TextButton(163, 40, 144, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[briefing]"), BRIEFING));
	}
	addWidget(new TextButton(second_offset+10, 40, 143, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[objectives]"), OBJECTIVES));
	addWidget(new TextButton(second_offset+hints_x, 40, 143, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[hints]"), HINTS));
	
	
	
	objectives = new Text(0, 10, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[objectives]"));
	briefing = new Text(0, 10, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[briefing]"));
	hints = new Text(0, 10, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[hints]"));
	
	objectivesWidgets.push_back(objectives);
	briefingWidgets.push_back(briefing);
	hintsWidgets.push_back(hints);
	
	
	std::string text;
	
	//This group of widgets is all for the objectives tab
	objectivesWidgets.push_back(new Text(10, 70, ALIGN_LEFT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[Primary Objectives]")));
	int n=0;
	for(int i=0; i<gui->game.objectives.getNumberOfObjectives(); ++i)
	{
		if(gui->game.objectives.isObjectiveVisible(i) && gui->game.objectives.getObjectiveType(i) == GameObjectives::Primary)
		{
			text = gui->game.objectives.getGameObjectiveText(i);
			if(Toolkit::getStringTable()->doesStringExist(text.c_str()))
				text = Toolkit::getStringTable()->getString(text.c_str());
			objectivesWidgets.push_back(new Text(50, 100 + 30*n, ALIGN_LEFT, ALIGN_TOP, "standard", text.c_str()));
			Uint8 state = 0;
			if(gui->game.objectives.isObjectiveComplete(i))
				state = 1;
			else if(gui->game.objectives.isObjectiveFailed(i))
				state = 2;
			TriButton* b = new TriButton(20, 100 + 30*n, 20, 20, ALIGN_LEFT, ALIGN_TOP, state, i);
			b->setClickable(false);
			objectivesWidgets.push_back(b);
			n+=1;
		}
	}
	if(n == 0)
	{
		objectivesWidgets.push_back(new Text(50, 100 + 30*n, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[No Objectives]")));
		n+=1;
	}
	
	bool isSecondary = false;
	for(int i=0; i<gui->game.objectives.getNumberOfObjectives(); ++i)
	{
		if(gui->game.objectives.getObjectiveType(i) == GameObjectives::Secondary)
		{
			isSecondary = true;
			break;
		}
	}
	
	if(isSecondary)
	{
		objectivesWidgets.push_back(new Text(10, 100 + 30*n, ALIGN_LEFT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[Secondary Objectives]")));
		for(int i=0; i<gui->game.objectives.getNumberOfObjectives(); ++i)
		{
			if(gui->game.objectives.isObjectiveVisible(i) && gui->game.objectives.getObjectiveType(i) == GameObjectives::Secondary)
			{
				text = gui->game.objectives.getGameObjectiveText(i);
				if(Toolkit::getStringTable()->doesStringExist(text.c_str()))
					text = Toolkit::getStringTable()->getString(text.c_str());
				objectivesWidgets.push_back(new Text(50, 130 + 30*n, ALIGN_LEFT, ALIGN_TOP, "standard", text.c_str()));
				Uint8 state = 0;
				if(gui->game.objectives.isObjectiveComplete(i))
					state = 1;
				else if(gui->game.objectives.isObjectiveFailed(i))
					state = 2;
				TriButton* b = new TriButton(20, 130 + 30*n, 20, 20, ALIGN_LEFT, ALIGN_TOP, state, i);
				b->setClickable(false);
				objectivesWidgets.push_back(b);
				n+=1;
			}
		}
	}
	
	text = gui->game.missionBriefing;
	if(Toolkit::getStringTable()->doesStringExist(text.c_str()))
		text = Toolkit::getStringTable()->getString(text.c_str());
		
	//This group of widgets is for the mission briefing tab
	briefingWidgets.push_back(new TextArea(10, 70, 450, 260, ALIGN_LEFT, ALIGN_TOP, "standard", true, text.c_str()));
	
	//This group of widgets is for the hints tab
	n=0;
	for(int i=0; i<gui->game.gameHints.getNumberOfHints(); ++i)
	{
		if(gui->game.gameHints.isHintVisible(i))
		{
			text = gui->game.gameHints.getGameHintText(i);
			if(Toolkit::getStringTable()->doesStringExist(text.c_str()))
				text = Toolkit::getStringTable()->getString(text.c_str());
			text = boost::lexical_cast<std::string>(n+1) + ") " + text;
			hintsWidgets.push_back(new Text(50, 70 + 25*n, ALIGN_LEFT, ALIGN_TOP, "standard", text.c_str()));
			n+=1;
		}
	}
	if(n == 0)
	{
		hintsWidgets.push_back(new Text(50, 70 + 25*n, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[No Hints]")));
		n+=1;
	}
	
	//Add the widgets to the menu
	for(int i=0; i<objectivesWidgets.size(); i++)
	{
		objectivesWidgets[i]->visible=!showBriefing;
		addWidget(objectivesWidgets[i]);
	}
	for(int i=0; i<briefingWidgets.size(); i++)
	{
		briefingWidgets[i]->visible=showBriefing;
		addWidget(briefingWidgets[i]);
	}
	for(int i=0; i<hintsWidgets.size(); i++)
	{
		hintsWidgets[i]->visible=false;
		addWidget(hintsWidgets[i]);
	}
	
	// add ok button
	addWidget(new TextButton(0, 340, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 27));
	dispatchInit();
}



void InGameObjectivesScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if(par1 == OK)
		{
			endValue=par1;
		}
		else if(par1 == OBJECTIVES)
		{
			for(int i=0; i<objectivesWidgets.size(); i++)
			{
				objectivesWidgets[i]->visible=true;
			}
			for(int i=0; i<briefingWidgets.size(); i++)
			{
				briefingWidgets[i]->visible=false;
			}
			for(int i=0; i<hintsWidgets.size(); i++)
			{
				hintsWidgets[i]->visible=false;
			}
		}
		else if(par1 == BRIEFING)
		{
			for(int i=0; i<objectivesWidgets.size(); i++)
			{
				objectivesWidgets[i]->visible=false;
			}
			for(int i=0; i<briefingWidgets.size(); i++)
			{
				briefingWidgets[i]->visible=true;
			}
			for(int i=0; i<hintsWidgets.size(); i++)
			{
				hintsWidgets[i]->visible=false;
			}
		}
		else if(par1 == HINTS)
		{
			for(int i=0; i<objectivesWidgets.size(); i++)
			{
				objectivesWidgets[i]->visible=false;
			}
			for(int i=0; i<briefingWidgets.size(); i++)
			{
				briefingWidgets[i]->visible=false;
			}
			for(int i=0; i<hintsWidgets.size(); i++)
			{
				hintsWidgets[i]->visible=true;
			}
		}
	}
}

