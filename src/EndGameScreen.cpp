/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charri?re
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

#include "EndGameScreen.h"
#include "GlobalContainer.h"

EndGameStat::EndGameStat(int x, int y, Game *game)
{
	this->x=x;
	this->y=y;
	this->game = game;
	this->type = EndOfGameStat::TYPE_UNITS;
}

void EndGameStat::paint(void)
{
	repaint();
}

void EndGameStat::setStatType(EndOfGameStat::Type type)
{
	this->type=type;
	repaint();
}

void EndGameStat::repaint(void)
{
	assert(parent);
	assert(parent->getSurface());
	
	// draw background
	parent->getSurface()->drawFilledRect(x, y, 128*3, 256, 249, 247, 219);
	
	// find maximum
	int team, pos, maxValue=0;
	for (team=0; team < game->session.numberOfTeam; team++)
	{
		for (pos=0; pos<TeamStats::END_OF_GAME_STATS_SIZE; pos++)
		{
			int val=game->teams[team]->stats.endOfGameStats[pos].value[type];
			if (val>maxValue)
				maxValue=val;
		}
	}
	
	// draw curve
	if (maxValue)
		for (team=0; team < game->session.numberOfTeam; team++)
		{
			Uint8 r = game->teams[team]->colorR;
			Uint8 g = game->teams[team]->colorG;
			Uint8 b = game->teams[team]->colorB;

			int statsIndex = game->teams[team]->stats.endOfGameStatIndex;
			int oy, ox, nx, ny;

			ox = 1;
			oy = (game->teams[team]->stats.endOfGameStats[statsIndex].value[type] * 256)/maxValue;

			for (pos=1; pos<TeamStats::END_OF_GAME_STATS_SIZE; pos++)
			{
				int index=(statsIndex+pos)&0x7F;
				nx = ox+3;
				ny = (game->teams[team]->stats.endOfGameStats[index].value[type] * 256)/maxValue;

				parent->getSurface()->drawLine(x+ox, y+256-oy, x+nx, y+256-ny, r, g, b);

				ox = nx;
				oy = ny;
			}
		}
	
	// update
	parent->addUpdateRect(x, y, 128*3, 256);
}


EndGameScreen::EndGameScreen(GameGUI *gui)
{
	this->gui=gui;
	
	// title & graph
	const char *titleText;
	if (!gui->getLocalTeam()->isAlive)
	{
		titleText=globalContainer->texts.getString("[Lost : your colony is dead]");
	}
	else if (!gui->game.isGameEnded)
	{
		titleText=globalContainer->texts.getString("[The game has not been finished]");
	}
	else if (!gui->game.totalPrestigeReached)
	{
		titleText=globalContainer->texts.getString("[Won : you defeated your opponents]");
	}
	else
	{
		Team *t=gui->game.getTeamWithMostPrestige();
		assert(t);
		if (t==gui->getLocalTeam())
			titleText=globalContainer->texts.getString("[Won : you have the biggest prestige]");
		else
			titleText=globalContainer->texts.getString("[Lost : %s has more prestige than you]");
	}
	
	addWidget(new Text(20, 18, globalContainer->menuFont, titleText, 600));
	statWidget=new EndGameStat(208, 80, &(gui->game));
	addWidget(statWidget);
	
	// add buttons
	addWidget(new TextButton(260, 350, 80, 20, NULL, -1, -1, globalContainer->standardFont, globalContainer->texts.getString("[Units]"), 0, '1'));
	addWidget(new TextButton(360, 350, 80, 20, NULL, -1, -1, globalContainer->standardFont, globalContainer->texts.getString("[Buildings]"), 1, '2'));
	addWidget(new TextButton(460, 350, 80, 20, NULL, -1, -1, globalContainer->standardFont, globalContainer->texts.getString("[Prestige]"), 2, '3'));
	addWidget(new TextButton(150, 415, 340, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[ok]"), 3, 13));
	
	// add players name
	Text *text;
	int inc = (gui->game.session.numberOfTeam < 16) ? 20 : 10;
	Font *font = (gui->game.session.numberOfTeam < 16) ? globalContainer->standardFont : globalContainer->littleFont;
	for (int i=0; i<gui->game.session.numberOfTeam; i++)
	{
		Team *t=gui->game.teams[i];
		text=new Text(30, 80+(i*inc), font, t->getFirstPlayerName());
		text->setColor(t->colorR, t->colorG, t->colorB);
		addWidget(text);
	}
}

void EndGameScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1<3)
			statWidget->setStatType((EndOfGameStat::Type)par1);
		else
			endExecute(par1);
	}
}
