/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
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
#include "GlobalContainer.h"
#include "GameGUI.h"


OverlayScreen::OverlayScreen(int w, int h)
{
	gfxCtx=globalContainer->gfx->createDrawableSurface();
	gfxCtx->setRes(w, h);
	gfxCtx->setAlpha(false, 128);
	decX=(globalContainer->gfx->getW()-w)>>1;
	decY=(globalContainer->gfx->getH()-h)>>1;
	endValue=-1;
}

OverlayScreen::~OverlayScreen()
{
	delete gfxCtx;
}

void OverlayScreen::translateAndProcessEvent(SDL_Event *event)
{
	SDL_Event ev=*event;
	switch (ev.type)
	{
		case SDL_MOUSEMOTION:
			ev.motion.x-=decX;
			ev.motion.y-=decY;
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			ev.button.x-=decX;
			ev.button.y-=decY;
			break;
		default:
			break;
	}
	dispatchEvents(&ev);
}

void OverlayScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 255);
}


//! Main menu screen
InGameMainScreen::InGameMainScreen()
:OverlayScreen(300, 275)
{
	addWidget(new TextButton(10, 10, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[load game]"), LOAD_GAME));
	addWidget(new TextButton(10, 50, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[save game]"), SAVE_GAME));
	addWidget(new TextButton(10, 90, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[options]"), OPTIONS));
	addWidget(new TextButton(10, 130, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[alliances]"), ALLIANCES));
	addWidget(new TextButton(10, 180, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[return to game]"), RETURN_GAME, 27));
	addWidget(new TextButton(10, 230, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[quit the game]"), QUIT_GAME));
}

void InGameMainScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
		endValue=par1;
}

void InGameMainScreen::onSDLEvent(SDL_Event *event)
{

}

//! Alliance screen
InGameAlliance8Screen::InGameAlliance8Screen(GameGUI *gameGUI)
:OverlayScreen(300, 295)
{
	// fill the slots
	int i;
	for (i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		int otherTeam=gameGUI->game.players[i]->teamNumber;

		bool alliedState = (gameGUI->localTeam->allies)&(1<<otherTeam);
		allied[i]=new OnOffButton(200, 40+i*25, 20, 20, alliedState, ALLIED+i);
		addWidget(allied[i]);

		bool visionState = (gameGUI->localTeam->sharedVision)&(1<<otherTeam);
		vision[i]=new OnOffButton(235, 40+i*25, 20, 20, visionState, VISION+i);
		addWidget(vision[i]);

		bool chatState = (gameGUI->chatMask)&(1<<i);
		chat[i]=new OnOffButton(270, 40+i*25, 20, 20, chatState, CHAT+i);
		addWidget(chat[i]);
		
		Text *text=new Text(10, 40+i*25, globalContainer->menuFont);
		Team *team = gameGUI->game.players[i]->team;
		text->setColor(team->colorR, team->colorG, team->colorB);
		addWidget(text);
		if (gameGUI->game.players[i]->type==Player::P_AI || gameGUI->game.players[i]->type==Player::P_IP || gameGUI->game.players[i]->type==Player::P_LOCAL)
			text->setText("%s", gameGUI->game.players[i]->name);
		else
			text->setText("(%s)", gameGUI->game.players[i]->name);
	}
	for (;i<8;i++)
	{
		allied[i]=vision[i]=chat[i]=NULL;
	}

	// add static text
	addWidget(new Text(200, 10, globalContainer->menuFont, "A"));
	addWidget(new Text(236, 10, globalContainer->menuFont, "V"));
	addWidget(new Text(272, 10, globalContainer->menuFont, "C"));

	// add ok button
	addWidget(new TextButton(10, 250, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), OK, 27));
	this->gameGUI=gameGUI;
}

void InGameAlliance8Screen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		endValue=par1;
	}
	else if (action==BUTTON_STATE_CHANGED)
		setCorrectValueForPlayer(par1%32);
}

void InGameAlliance8Screen::setCorrectValueForPlayer(int i)
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
				allied[j]->setState(allied[i]->getState());
				vision[j]->setState(vision[i]->getState());
			}
		}
	}
}

Uint32 InGameAlliance8Screen::getAllianceMask(void)
{
	Uint32 mask=0;
	for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		if (allied[i]->getState())
			mask|=1<<i;
	}
	return mask;
}

Uint32 InGameAlliance8Screen::getVisionMask(void)
{
	Uint32 mask=0;
	for (int i=0; i<gameGUI->game.session.numberOfPlayer; i++)
	{
		if (vision[i]->getState())
			mask|=1<<i;
	}
	return mask;
}

Uint32 InGameAlliance8Screen::getChatMask(void)
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
:OverlayScreen(300, 295)
{
	// speed
	
	const int speedDec=45;
	addWidget(new Text(20, speedDec-35, globalContainer->menuFont, globalContainer->texts.getString("[game speed]")));
	speed[0]=new OnOffButton(30, speedDec, 20, 20, false, SPEED+0);
	addWidget(speed[0]);
	addWidget(new Text(60, speedDec, globalContainer->standardFont, globalContainer->texts.getString("[small]")));
	speed[1]=new OnOffButton(30, speedDec+25, 20, 20, false, SPEED+1);
	addWidget(speed[1]);
	addWidget(new Text(60, speedDec+25, globalContainer->standardFont, globalContainer->texts.getString("[medium]")));
	speed[2]=new OnOffButton(30, speedDec+50, 20, 20, true, SPEED+2);
	addWidget(speed[2]);
	addWidget(new Text(60, speedDec+50, globalContainer->standardFont, globalContainer->texts.getString("[large]")));
	
	// latency
	const int latDec=speedDec+120;
	addWidget(new Text(20, latDec-35, globalContainer->menuFont, globalContainer->texts.getString("[network latency]")));
	latency[0]=new OnOffButton(30, latDec, 20, 20, false, LATENCY+0);
	addWidget(latency[0]);
	addWidget(new Text(60, latDec, globalContainer->standardFont, globalContainer->texts.getString("[small]")));
	latency[1]=new OnOffButton(30, latDec+25, 20, 20, false, LATENCY+1);
	addWidget(latency[1]);
	addWidget(new Text(60, latDec+25, globalContainer->standardFont, globalContainer->texts.getString("[medium]")));
	latency[2]=new OnOffButton(30, latDec+50, 20, 20, true, LATENCY+2);
	addWidget(latency[2]);
	addWidget(new Text(60, latDec+50, globalContainer->standardFont, globalContainer->texts.getString("[large]")));
	
	addWidget(new TextButton(10, 250, 280, 35, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), OK, 27));
	this->gameGUI=gameGUI;
}

void InGameOptionScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		endValue=par1;
	}
	else if (action==BUTTON_STATE_CHANGED)
	{
		if (par1>=LATENCY)
		{
			int id=par1-LATENCY;
			for (int i=0; i<3; i++)
			{
				if (i!=id)
					latency[i]->setState(false);
			}
		}
		else if (par1>=SPEED)
		{
			int id=par1-SPEED;
			for (int i=0; i<3; i++)
			{
				if (i!=id)
					speed[i]->setState(false);
			}
		}
	}
}
