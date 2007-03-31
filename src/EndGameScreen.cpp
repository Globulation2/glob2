/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  Copyright (C) 2006 Bradley Arsenault

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
#include <FormatableString.h>
#include <GUIStyle.h>
#include <GUIText.h>
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <GraphicContext.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "GlobalContainer.h"


EndGameStat::EndGameStat(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, Game *game)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;

	this->hAlignFlag=hAlign;
	this->vAlignFlag=vAlign;
	
	this->game = game;

	for(int x=0; x<32; ++x)
		isTeamEnabled[x]=true;
	
	this->type = EndOfGameStat::TYPE_UNITS;
	mouse_x = -1;
	mouse_y = -1;
}

void EndGameStat::setStatType(EndOfGameStat::Type type)
{
	this->type=type;
}

void EndGameStat::setEnabledState(int teamNum, bool isEnabled)
{
	isTeamEnabled[teamNum]=isEnabled;
}

void EndGameStat::paint(void)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);
		
	assert(parent);
	assert(parent->getSurface());
	
	// draw background
	parent->getSurface()->drawRect(x, y, w, h, Style::style->frameColor);
	
	if(game->teams[0]->stats.endOfGameStats.size()==0)
		return;
	// find maximum
	int team, maxValue = 0;
	unsigned int pos=0;
	for (team=0; team < game->session.numberOfTeam; team++)
		for (pos=0; pos<game->teams[team]->stats.endOfGameStats.size(); pos++)
			maxValue = std::max(maxValue, game->teams[team]->stats.endOfGameStats[pos].value[type]);

	///You can't draw anything if the game ended so quickly that there wheren't two recorded values to draw a line between
	if(game->teams[0]->stats.endOfGameStats.size() >= 2)
	{
		//Calculate the maximum width of the numbers so they can be lined up
		int num=10;
		maxValue+=num-(maxValue%num);
		std::stringstream maxstr;
		maxstr<<maxValue<<std::endl;
		int max_digit_count=maxstr.str().size();
		int max_width=-1;
		double line_seperate=double(h)/double(num);
		double value_seperate=double(maxValue)/double(h);
		for(int n=0; n<num; ++n)
		{
			int pos=int(double(n)*line_seperate+0.5);
			int value=maxValue-int(double(pos)*value_seperate+0.5);
			std::stringstream str;
			str<<value<<std::endl;
			int width=globalContainer->littleFont->getStringWidth(str.str().c_str());
			max_width=std::max(width, max_width);
		}

		//Draw horizontal lines to given the scale of the graphs values.
		for(int n=0; n<num; ++n)
		{
			int pos=int(double(n)*line_seperate+0.5);
			int value=maxValue-int(double(pos)*value_seperate+0.5);
			if(n!=0)
				parent->getSurface()->drawHorzLine(x+w-5, y+pos, 10, 255, 255, 255);
			std::stringstream str;
			str<<std::setw(max_digit_count-1)<<std::setfill('0')<<value<<std::endl;
			int height=globalContainer->littleFont->getStringHeight(str.str().c_str());
			parent->getSurface()->drawString(x+w-max_width-8, y+pos-height/2, globalContainer->littleFont, str.str().c_str());
		}

		///Draw vertical lines to give the timescale
		double time_line_seperate=double(w)/double(15);
		int time_period=(game->teams[0]->stats.endOfGameStats.size()*512/250)*10;
		double time_value_seperate=double(time_period)/double(15);
		for(int n=1; n<16; ++n)
		{
			if(n!=16)
				parent->getSurface()->drawVertLine(int(double(x)+time_line_seperate*double(n)+0.5), y+h-5, 10, 255, 255, 255);
			std::stringstream str;
			int min=int(double(n)*time_value_seperate+0.5)/60;
			int sec=int(double(n)*time_value_seperate+0.5)%60;
			str<<min<<":"<<std::setw(2)<<std::setfill('0')<<sec<<std::endl;
			int width=globalContainer->littleFont->getStringWidth(str.str().c_str());
			parent->getSurface()->drawString(int(double(x)+time_line_seperate*double(n)+0.5)-width/2, y+h-30, globalContainer->littleFont, str.str().c_str());
		}

		float inc_x=static_cast<float>(w-2)/static_cast<float>(game->teams[0]->stats.endOfGameStats.size()-1);
		float inc_y=static_cast<float>(h-2)/static_cast<float>(maxValue);

		int best_circle_position_difference=100000;
		int best_circle_position_value=-1;
		int best_circle_position_x=-1;
		int best_circle_position_y=-1;

		// draw curve
		if (maxValue)
		{
			for (team=0; team < game->session.numberOfTeam; team++)
			{
				if(!isTeamEnabled[team])
					continue;
				Uint8 r = game->teams[team]->colorR;
				Uint8 g = game->teams[team]->colorG;
				Uint8 b = game->teams[team]->colorB;

				int statsIndex = 0;
				int oy, ox, nx, ny;

				ox = 1;
				oy = static_cast<int>(game->teams[team]->stats.endOfGameStats[statsIndex].value[type] * inc_y);

				for (pos=0; pos<game->teams[team]->stats.endOfGameStats.size(); pos++)
				{
					nx = static_cast<int>(pos*inc_x);
					ny = static_cast<int>(game->teams[team]->stats.endOfGameStats[pos].value[type] * inc_y);

					///This is for the small circle that indicates the numbers at a particular position.
					///It will only be shown if the cursor is within 40 pixels of the lines
					// and it will only show the circle at the closest line.
					if(mouse_x!=-1)
					{
						float line_slope=static_cast<float>((y+h-ny-1)-(y+h-oy-1)) / static_cast<float>((x+nx)-(x+ox));
						if(mouse_x>=(x+ox) && mouse_x<(x+nx))
						{
							int circle_x=mouse_x;
							int circle_y=y+h-oy-1 + static_cast<int>((mouse_x-(x+ox))*line_slope+0.5);
							if(std::abs(mouse_y+y-circle_y) < 40)
							{
								if(std::abs(mouse_y+y-circle_y) < best_circle_position_difference)
								{
									best_circle_position_x=circle_x;
									best_circle_position_y=circle_y;
									best_circle_position_difference=std::abs(mouse_y+y-circle_y);
									best_circle_position_value=maxValue-int(double(circle_y-y)*value_seperate+0.5);
								}
							}
						}
					}

					parent->getSurface()->drawLine(x+ox, y+h-oy-1, x+nx, y+h-ny-1, r, g, b);

					ox = nx;
					oy = ny;
					
					//std::cout << pos << " : " << team << " : " << game->teams[team]->stats.endOfGameStats[index].value[type] << std::endl;
				}
			}
		}
		if(best_circle_position_x!=-1)
		{
			parent->getSurface()->drawCircle(best_circle_position_x, best_circle_position_y, 10, Color::white);
			std::stringstream str;
			str<<best_circle_position_value;
			parent->getSurface()->drawString(best_circle_position_x+10, best_circle_position_y+10, globalContainer->littleFont, str.str());
		}
	}
}



void EndGameStat::onSDLMouseMotion(SDL_Event* event)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);
	if(event->motion.x > x && event->motion.x < x+w && event->motion.y > y && event->motion.y < y+h)
	{
		mouse_x=event->motion.x-x+21;
		mouse_y=event->motion.y-y;
	}
	else
	{
		mouse_x=-1;
		mouse_y=-1;
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
	std::string titleText;
	
	if (gui->game.totalPrestigeReached)
	{
		Team *t=gui->game.getTeamWithMostPrestige();
		assert(t);
		if (t==gui->getLocalTeam())
		{
			titleText=Toolkit::getStringTable()->getString("[Won : you have the most prestige]");
		}
		else
		{
			FormatableString strText;
			if ((t->allies) & (gui->getLocalTeam()->me))
				strText = Toolkit::getStringTable()->getString("[Won : your ally %0 has the most prestige]");
			else
				strText = Toolkit::getStringTable()->getString("[Lost : %0 has more prestige than you]");

			const char *playerText = t->getFirstPlayerName();
			assert(playerText);
			strText.arg(playerText);
			titleText = strText;
		}
	}
	else if (!gui->getLocalTeam()->isAlive)
	{
		titleText=Toolkit::getStringTable()->getString("[Lost : your colony is dead]");
	}
	else if (!gui->game.isGameEnded)
	{
		titleText=Toolkit::getStringTable()->getString("[The game has not been finished]");
	}
	else
	{
		titleText=Toolkit::getStringTable()->getString("[Won : you defeated your opponents]");
	}
	
	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_LEFT, "menu", titleText.c_str()));
	statWidget=new EndGameStat(20, 80, 180, 150, ALIGN_FILL, ALIGN_FILL, &(gui->game));
	addWidget(statWidget);

	graphLabel=new Text(25, 85, ALIGN_LEFT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[Units]"));
	addWidget(graphLabel);
	
	// add buttons
	addWidget(new TextButton(90, 90, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Units]"), 0, '1'));
	addWidget(new TextButton(190, 90, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Buildings]"), 1, '2'));
	addWidget(new TextButton(290, 90, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Prestige]"), 2, '3'));
	addWidget(new TextButton(90, 65, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[hp]"), 3, '4'));
	addWidget(new TextButton(190, 65, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Attack]"), 4, '5'));
	addWidget(new TextButton(290, 65, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Defense]"), 5, '6'));
	addWidget(new TextButton(0, 5, 300, 40, ALIGN_CENTERED, ALIGN_BOTTOM, "data/gfx/gamegui", 26, 27, "menu", Toolkit::getStringTable()->getString("[ok]"), 38, 13));
	
	// add players name
	Text *text;
	int inc = (gui->game.session.numberOfTeam < 16) ? 20 : 10;

	// set teams entries for later sort
	for (int i=0; i<gui->game.session.numberOfTeam; i++)
	{
		Team *t=gui->game.teams[i];
		int endIndex=t->stats.endOfGameStats.size()-1;

		struct TeamEntry entry;
		entry.name=t->getFirstPlayerName();
		entry.teamNum=i;
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
		OnOffButton* enabled_button = new OnOffButton(10, 80+(i*inc), inc, inc, ALIGN_RIGHT, ALIGN_TOP, true, 6+teams[i].teamNum);
		team_enabled_buttons.push_back(enabled_button);
		addWidget(enabled_button);
		text=new Text(10+inc, 80+(i*inc), ALIGN_RIGHT, ALIGN_TOP, "standard", teams[i].name.c_str(), 140);
		text->setStyle(Font::Style(Font::STYLE_NORMAL, teams[i].r, teams[i].g, teams[i].b));
		names.push_back(text);
		addWidget(text);
	}
}

void EndGameScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		///This is a change in the graph type
		if (par1<6)
		{
			EndOfGameStat::Type type = (EndOfGameStat::Type)par1;
			statWidget->setStatType(type);
			sortAndSet(type);
			if(type==EndOfGameStat::TYPE_UNITS)
				graphLabel->setText(Toolkit::getStringTable()->getString("[Units]"));
			else if(type==EndOfGameStat::TYPE_BUILDINGS)
				graphLabel->setText(Toolkit::getStringTable()->getString("[Buildings]"));
			else if(type==EndOfGameStat::TYPE_PRESTIGE)
				graphLabel->setText(Toolkit::getStringTable()->getString("[Prestige]"));
			else if(type==EndOfGameStat::TYPE_HP)
				graphLabel->setText(Toolkit::getStringTable()->getString("[hp]"));
			else if(type==EndOfGameStat::TYPE_ATTACK)
				graphLabel->setText(Toolkit::getStringTable()->getString("[Attack]"));
			else if(type==EndOfGameStat::TYPE_DEFENSE)
				graphLabel->setText(Toolkit::getStringTable()->getString("[Defense]"));

			// Resort the names on the side of the graph based on their respective scores
			MoreScore moreScore;
			moreScore.type=type;
			std::sort(teams.begin(), teams.end(), moreScore);

			int prev_num=1;
			for (unsigned i=0; i<teams.size(); i++)
			{
				std::stringstream str;
				if(i>0 && teams[i].endVal[type] == teams[i-1].endVal[type])
				{
					str<<prev_num<<") "<<teams[i].name.c_str()<<std::endl;
				}
				else
				{
					str<<i+1<<") "<<teams[i].name.c_str()<<std::endl;
					prev_num=i+1;
				}
			
				names[i]->setText(str.str().c_str());
				names[i]->setStyle(Font::Style(Font::STYLE_NORMAL, teams[i].r, teams[i].g, teams[i].b));
				team_enabled_buttons[i]->returnCode=6+i;
			}
		}
		///One of the buttons beside the team names where selected
		else if(par1 >= 6 && par1 < static_cast<int>(6+teams.size()))
		{
			int n=par1-6;
			statWidget->setEnabledState(teams[n].teamNum, team_enabled_buttons[n]->getState());
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
