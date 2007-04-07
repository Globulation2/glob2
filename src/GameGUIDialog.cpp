/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#include "GameGUIDialog.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "SoundMixer.h"
#include <GUIButton.h>
#include <GUIText.h>
#include <GUIAnimation.h>
#include <GUISelector.h>
#include <Toolkit.h>
#include <StringTable.h>


//! Main menu screen
InGameMainScreen::InGameMainScreen(bool showAlliance)
:OverlayScreen(globalContainer->gfx, 320, 310)
{
	addWidget(new TextButton(0, 10, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[load game]"), LOAD_GAME));
	addWidget(new TextButton(0, 60, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[save game]"), SAVE_GAME));
	addWidget(new TextButton(0, 110, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[options]"), OPTIONS));
	if (showAlliance)
		addWidget(new TextButton(0, 160, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[alliances]"), ALLIANCES));
	addWidget(new TextButton(0, 210, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[quit the game]"), QUIT_GAME));
	addWidget(new TextButton(0, 260, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[return to game]"), RETURN_GAME, 27));
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
:OverlayScreen(globalContainer->gfx, (gameGUI->game.session.numberOfPlayer<=8) ? 320 : 600, 395)
{
	// fill the slots
	int i;
	for (i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		int otherTeam = gameGUI->game.players[i]->teamNumber;
		unsigned otherTeamMask = 1 << otherTeam;

		int xBase = (i>>3)*300;
		int yBase = (i&0x7)*25;

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

		Text *text = new Text(10+xBase, 37+yBase, ALIGN_LEFT, ALIGN_LEFT, "menu", pname.c_str());
		Team *team = gameGUI->game.players[i]->team;
		text->setStyle(Font::Style(Font::STYLE_NORMAL, team->colorR, team->colorG, team->colorB));
		addWidget(text);
		
		alliance[i]=new OnOffButton(172+xBase, 40+yBase,  20, 20, ALIGN_LEFT, ALIGN_LEFT, (gameGUI->localTeam->allies & otherTeamMask) != 0, ALLIED+i);
		addWidget(alliance[i]);
		
		normalVision[i]=new OnOffButton(196+xBase, 40+yBase,  20, 20, ALIGN_LEFT, ALIGN_LEFT, (gameGUI->localTeam->sharedVisionOther & otherTeamMask) != 0, NORMAL_VISION+i);
		addWidget(normalVision[i]);
		
		foodVision[i]=new OnOffButton(220+xBase, 40+yBase,  20, 20, ALIGN_LEFT, ALIGN_LEFT, (gameGUI->localTeam->sharedVisionFood & otherTeamMask) != 0, FOOD_VISION+i);
		addWidget(foodVision[i]);
		
		marketVision[i]=new OnOffButton(244+xBase, 40+yBase,  20, 20, ALIGN_LEFT, ALIGN_LEFT, (gameGUI->localTeam->sharedVisionExchange & otherTeamMask) != 0, MARKET_VISION+i);
		addWidget(marketVision[i]);

		bool chatState = (((gameGUI->chatMask)&(1<<i))!=0);
		chat[i]=new OnOffButton(268+xBase, 40+yBase, 20, 20, ALIGN_LEFT, ALIGN_LEFT, chatState, CHAT+i);
		addWidget(chat[i]);
	}
	for (;i<16;i++)
	{
		alliance[i] = NULL;
		normalVision[i] = NULL;
		foodVision[i] = NULL;
		marketVision[i] = NULL;
		chat[i] = NULL;
	}

	// add static text and images
	addWidget(new Text(172+3, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "A")); 
	addWidget(new Text(196+3, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "V"));
	addWidget(new Text(220, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "fV"));
	addWidget(new Text(244, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "mV"));
	addWidget(new Text(268+3, 13, ALIGN_LEFT, ALIGN_LEFT, "standard", "C"));
	
	if (gameGUI->game.session.numberOfPlayer > 8)
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
		for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
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

void InGameAllianceScreen::setCorrectValueForPlayer(int i)
{
	Game *game=&(gameGUI->game);
	assert(i<game->session.numberOfPlayer);
	for (int j=0; j<game->session.numberOfPlayer; j++)
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
	for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		if (alliance[i]->getState())
			mask |= 1<<i;
	}
	return mask;
}

Uint32 InGameAllianceScreen::getEnemyMask(void)
{
	Uint32 mask = 0;
	for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		if (!alliance[i]->getState())
			mask |= 1<<i;
	}
	return mask;
}

Uint32 InGameAllianceScreen::getExchangeVisionMask(void)
{
	Uint32 mask = 0;
	for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		if (marketVision[i]->getState())
			mask |= 1<<i;
	}
	return mask;
}

Uint32 InGameAllianceScreen::getFoodVisionMask(void)
{
	Uint32 mask = 0;
	for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		if (foodVision[i]->getState())
			mask |= 1<<i;
	}
	return mask;
}

Uint32 InGameAllianceScreen::getOtherVisionMask(void)
{
	Uint32 mask = 0;
	for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		if (normalVision[i]->getState())
			mask |= 1<<i;
	}
	return mask;
}


Uint32 InGameAllianceScreen::getChatMask(void)
{
	Uint32 mask = 0;
	for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
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
	musicVol=new Selector(19, 50, ALIGN_LEFT, ALIGN_TOP, 256, globalContainer->settings.musicVolume, 256);
	addWidget(musicVol);
	Text *musicVolText=new Text(10, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[Music volume]"));
	addWidget(musicVolText);

	addWidget(new TextButton(0, 250, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 27));
	dispatchInit();
	
	std::ostringstream oss;
	oss << globalContainer->gfx->getW() << "x" << globalContainer->gfx->getH();
	if (globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
		oss << " GL";
	else
		oss << " SDL";
		
	addWidget(new Text(0, 200, ALIGN_FILL, ALIGN_TOP, "standard", oss.str().c_str()));
}

void InGameOptionScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		endValue=par1;
	}
	else if (action==VALUE_CHANGED)
	{
		globalContainer->mix->setVolume(musicVol->getValue(), globalContainer->settings.mute);
	}
}
