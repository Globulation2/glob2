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
#include <GUISelector.h>
#include <GUIButton.h>
#include <GUIText.h>
#include <GUIAnimation.h>
#include <GUISelector.h>
#include <Toolkit.h>
#include <StringTable.h>


//! Main menu screen
InGameMainScreen::InGameMainScreen(bool showAlliance)
:OverlayScreen(globalContainer->gfx, 300, 275)
{
	addWidget(new TextButton(10, 10, 280, 35, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[load game]"), LOAD_GAME));
	addWidget(new TextButton(10, 50, 280, 35, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[save game]"), SAVE_GAME));
	addWidget(new TextButton(10, 90, 280, 35, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[options]"), OPTIONS));
	if (showAlliance)
		addWidget(new TextButton(10, 130, 280, 35, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[alliances]"), ALLIANCES));
	addWidget(new TextButton(10, 180, 280, 35, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[quit the game]"), QUIT_GAME));
	addWidget(new TextButton(10, 230, 280, 35, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[return to game]"), RETURN_GAME, 27));
}

void InGameMainScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
		endValue=par1;
}

InGameEndOfGameScreen::InGameEndOfGameScreen(const char *title, bool canContinue)
:OverlayScreen(globalContainer->gfx, 300, canContinue ? 150 : 100)
{
	addWidget(new Text(10, 10, ALIGN_LEFT, ALIGN_LEFT, "menu", title, 280));
	addWidget(new TextButton(10, 50, 280, 35, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu",  Toolkit::getStringTable()->getString("[ok]"), QUIT, 13));
	if (canContinue)
		addWidget(new TextButton(10, 100, 280, 35, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu",  Toolkit::getStringTable()->getString("[Continue playing]"), CONTINUE, 27));
}

void InGameEndOfGameScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
		endValue=par1;
}

//! Alliance screen
InGameAllianceScreen::InGameAllianceScreen(GameGUI *gameGUI)
:OverlayScreen(globalContainer->gfx, (gameGUI->game.session.numberOfPlayer<8) ? 250 : 500, 295)
{
	// fill the slots
	int i;
	for (i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		int otherTeam=gameGUI->game.players[i]->teamNumber;

		// level 0 is peace, level 3 is total war
		unsigned defaultAlliance=0;
		if ((gameGUI->localTeam->allies)&(1<<otherTeam))
			defaultAlliance=0;// we are allied
		else if ((gameGUI->localTeam->sharedVisionFood)&(1<<otherTeam))
			defaultAlliance=1;
		else if ((gameGUI->localTeam->sharedVisionExchange)&(1<<otherTeam))
			defaultAlliance=2;
		else if ((gameGUI->localTeam->enemies)&(1<<otherTeam))
			defaultAlliance=3; // we are enemy
		else
			assert(false);
			
		int xBase = (i>>3)*250;
		int yBase = (i&0x7)*25;

		alliance[i]=new Selector(150+xBase, 40+yBase, ALIGN_LEFT, ALIGN_LEFT, 4, 1, defaultAlliance);
		addWidget(alliance[i]);

		bool chatState = (((gameGUI->chatMask)&(1<<i))!=0);
		chat[i]=new OnOffButton(218+xBase, 40+yBase, 20, 20, ALIGN_LEFT, ALIGN_LEFT, chatState, CHAT+i);
		addWidget(chat[i]);

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
		text->setColor(team->colorR, team->colorG, team->colorB);
		addWidget(text);
	}
	for (;i<16;i++)
	{
		alliance[i]=NULL;
		chat[i]=NULL;
	}

	// add static text and images
	addWidget(new Animation(149, 13, ALIGN_LEFT, ALIGN_LEFT, "data/gfx/gamegui", 13));
	addWidget(new Animation(182, 13, ALIGN_LEFT, ALIGN_LEFT, "data/gfx/gamegui", 14));
	addWidget(new Animation(220, 16, ALIGN_LEFT, ALIGN_LEFT, "data/gfx/gamegui", 15));
	
	if (gameGUI->game.session.numberOfPlayer<8)
	{
		// add ok button
		addWidget(new TextButton(10, 250, 230, 35, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 27));
	}
	else
	{
		addWidget(new Animation(250+149, 13, ALIGN_LEFT, ALIGN_LEFT, "data/gfx/gamegui", 13));
		addWidget(new Animation(250+182, 13, ALIGN_LEFT, ALIGN_LEFT, "data/gfx/gamegui", 14));
		addWidget(new Animation(250+220, 16, ALIGN_LEFT, ALIGN_LEFT, "data/gfx/gamegui", 15));
	
		// add ok button
		addWidget(new TextButton(135, 250, 230, 35, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 27));
	}
	this->gameGUI=gameGUI;
}

void InGameAllianceScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		endValue=par1;
	}
	else if (action==VALUE_CHANGED)
	{
		int i;
		for (i=0; i<gameGUI->game.session.numberOfPlayer; i++)
			if (source==alliance[i])
				break;
		setCorrectValueForPlayer(i);
	}
}

void InGameAllianceScreen::setCorrectValueForPlayer(int i)
{
	Game *game=&(gameGUI->game);
	assert(i<game->session.numberOfPlayer);
	for (int j=0; j<game->session.numberOfPlayer; j++)
	{
		if (j!=i)
		{
			// if two players are the same team, we must have the same alliance and vision
			if (game->players[j]->teamNumber==game->players[i]->teamNumber)
			{
				alliance[j]->setValue(alliance[i]->getValue());
			}
		}
	}
}

Uint32 InGameAllianceScreen::getAlliedMask(void)
{
	// allied is 0
	Uint32 mask=0;
	for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		if (alliance[i]->getValue()==0)
			mask|=1<<i;
	}
	return mask;
}

Uint32 InGameAllianceScreen::getEnemyMask(void)
{
	// enemy is 3
	Uint32 mask=0;
	for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		if (alliance[i]->getValue()!=0)
			mask|=1<<i;
	}
	return mask;
}

Uint32 InGameAllianceScreen::getExchangeVisionMask(void)
{
	// we have exchange vision in 2 and down
	Uint32 mask=0;
	for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		if (alliance[i]->getValue()<3)
			mask|=1<<i;
	}
	return mask;
}

Uint32 InGameAllianceScreen::getFoodVisionMask(void)
{
	// we have food vision in 1 and down
	Uint32 mask=0;
	for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		if (alliance[i]->getValue()<2)
			mask|=1<<i;
	}
	return mask;
}

Uint32 InGameAllianceScreen::getOtherVisionMask(void)
{
	// we have exchange vision in 0
	Uint32 mask=0;
	for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		if (alliance[i]->getValue()==0)
			mask|=1<<i;
	}
	return mask;
}


Uint32 InGameAllianceScreen::getChatMask(void)
{
	Uint32 mask=0;
	for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		if (chat[i]->getState())
			mask|=1<<i;
	}
	return mask;
}

//! Option Screen
InGameOptionScreen::InGameOptionScreen(GameGUI *gameGUI)
:OverlayScreen(globalContainer->gfx, 300, 295)
{
	musicVol=new Selector(19, 50, ALIGN_LEFT, ALIGN_TOP, 256, 8, globalContainer->settings.musicVolume, 1);
	addWidget(musicVol);
	Text *musicVolText=new Text(10, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[Music volume]"));
	addWidget(musicVolText);

	addWidget(new TextButton(10, 250, 280, 35, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 27));
}

void InGameOptionScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		endValue=par1;
	}
	else if (action==VALUE_CHANGED)
	{
		globalContainer->mix->setVolume(musicVol->getValue());
	}
}
