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

#include "EndGameScreen.h"
#include <algorithm>
#include <GUIText.h>
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <GraphicContext.h>
#include <algorithm>

EndGameStat::EndGameStat(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, Game *game)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;

	this->hAlignFlag=hAlign;
	this->vAlignFlag=vAlign;
	
	this->game = game;
	
	this->type = EndOfGameStat::TYPE_UNITS;
}

void EndGameStat::setStatType(EndOfGameStat::Type type)
{
	this->type=type;
}

void EndGameStat::paint(void)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);
		
	assert(parent);
	assert(parent->getSurface());
	
	// draw background
	parent->getSurface()->drawRect(x, y, w, h, ColorTheme::frontFrameColor);
	
	// find maximum
	int team, pos, maxValue = 1;
	for (team=0; team < game->session.numberOfTeam; team++)
		for (pos=0; pos<TeamStats::END_OF_GAME_STATS_SIZE; pos++)
			maxValue = std::max(maxValue, game->teams[team]->stats.endOfGameStats[pos].value[type]);
	
	// draw curve
	if (maxValue)
	{
		for (team=0; team < game->session.numberOfTeam; team++)
		{
			Uint8 r = game->teams[team]->colorR;
			Uint8 g = game->teams[team]->colorG;
			Uint8 b = game->teams[team]->colorB;

			int statsIndex = game->teams[team]->stats.endOfGameStatIndex;
			int oy, ox, nx, ny;

			ox = 1;
			oy = (game->teams[team]->stats.endOfGameStats[statsIndex].value[type] * (h-2))/maxValue;

			for (pos=1; pos<TeamStats::END_OF_GAME_STATS_SIZE; pos++)
			{
				int index=(statsIndex+pos)&0x7F;
				nx = (pos*(w-2))/TeamStats::END_OF_GAME_STATS_SIZE;
				ny = (game->teams[team]->stats.endOfGameStats[index].value[type] * (h-2))/maxValue;

				parent->getSurface()->drawLine(x+ox, y+h-oy-1, x+nx, y+h-ny-1, r, g, b);

				ox = nx;
				oy = ny;
				
				//std::cout << pos << " : " << team << " : " << game->teams[team]->stats.endOfGameStats[index].value[type] << std::endl;
			}
		}
	}
}


//! This function is used to sort the player array
struct MoreScore : public std::binary_function<const TeamEntry&, const TeamEntry&, bool>
{
	EndOfGameStat::Type type;
	bool operator()(const TeamEntry& t1, const TeamEntry& t2) { return t1.endVal[type] > t2.endVal[type]; }
};


EndGameScreen::EndGameScreen(GameGUI *gui)
{
	// title & graph
	const char *titleText;
	char *allocatedText=NULL;
	
	if (!gui->getLocalTeam()->isAlive)
	{
		titleText=Toolkit::getStringTable()->getString("[Lost : your colony is dead]");
	}
	else if (!gui->game.isGameEnded)
	{
		titleText=Toolkit::getStringTable()->getString("[The game has not been finished]");
	}
	else if (!gui->game.totalPrestigeReached)
	{
		titleText=Toolkit::getStringTable()->getString("[Won : you defeated your opponents]");
	}
	else
	{
		Team *t=gui->game.getTeamWithMostPrestige();
		assert(t);
		if (t==gui->getLocalTeam())
		{
			titleText=Toolkit::getStringTable()->getString("[Won : you have the biggest prestige]");
		}
		else
		{
			const char *strText;
			if ((t->allies) & (gui->getLocalTeam()->me))
				strText = Toolkit::getStringTable()->getString("[Won : your ally %s have the biggest prestige]");
			else
				strText = Toolkit::getStringTable()->getString("[Lost : %s has more prestige than you]");

			const char *playerText = t->getFirstPlayerName();
			assert(strText);
			assert(playerText);
			int len=strlen(strText)+strlen(playerText)-1;
			allocatedText=new char[len];
			snprintf(allocatedText, len, strText, playerText);
			titleText=allocatedText;
		}
	}
	
	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_LEFT, "menu", titleText));
	if (allocatedText)
		delete[] allocatedText;
	statWidget=new EndGameStat(20, 80, 180, 150, ALIGN_FILL, ALIGN_FILL, &(gui->game));
	addWidget(statWidget);
	
	// add buttons
	addWidget(new TextButton(90, 110, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Units]"), 0, '1'));
	addWidget(new TextButton(190, 110, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Buildings]"), 1, '2'));
	addWidget(new TextButton(290, 110, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Prestige]"), 2, '3'));
	addWidget(new TextButton(90, 85, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[hp]"), 3, '4'));
	addWidget(new TextButton(190, 85, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Attack]"), 4, '5'));
	addWidget(new TextButton(290, 85, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Defense]"), 5, '6'));
	addWidget(new TextButton(150, 25, 340, 40, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), 6, 13));
	
	// add players name
	Text *text;
	int inc = (gui->game.session.numberOfTeam < 16) ? 20 : 10;

	// set teams entries for later sort
	for (int i=0; i<gui->game.session.numberOfTeam; i++)
	{
		Team *t=gui->game.teams[i];
		int statsIndex = t->stats.endOfGameStatIndex;
		int endIndex=(statsIndex+(TeamStats::END_OF_GAME_STATS_SIZE-1))&0x7F;

		struct TeamEntry entry;
		entry.name=t->getFirstPlayerName();
		entry.r=t->colorR;
		entry.g=t->colorG;
		entry.b=t->colorB;
		entry.a=0;
		for (int j=0; j<EndOfGameStat::TYPE_NB_STATS; j++)
		{
			entry.endVal[j]=t->stats.endOfGameStats[endIndex].value[(EndOfGameStat::Type)j];
		}
		teams.push_back(entry);	
	}

	// sort
	MoreScore moreScore;
	moreScore.type=EndOfGameStat::TYPE_UNITS;
	std::sort(teams.begin(), teams.end(), moreScore);
	
	// add widgets
	for (unsigned i=0; i<teams.size(); i++)
	{
		text=new Text(20, 80+(i*inc), ALIGN_RIGHT, ALIGN_TOP, "standard", teams[i].name.c_str(), 140);
		text->setStyle(Font::Style(Font::STYLE_NORMAL, teams[i].r, teams[i].g, teams[i].b));
		names.push_back(text);
		addWidget(text);
	}
}

void EndGameScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1<6)
		{
			EndOfGameStat::Type type = (EndOfGameStat::Type)par1;
			statWidget->setStatType(type);
			sortAndSet(type);
		}
		else
			endExecute(par1);
	}
}


void EndGameScreen::sortAndSet(EndOfGameStat::Type type)
{
	MoreScore moreScore;
	moreScore.type=type;
	std::sort(teams.begin(), teams.end(), moreScore);
	for (unsigned i=0; i<names.size(); i++)
	{
		names[i]->setStyle(Font::Style(Font::STYLE_NORMAL, teams[i].r, teams[i].g, teams[i].b));
		names[i]->setText(teams[i].name.c_str());
	}
}
