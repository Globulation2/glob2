/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  Copyright (C) 2006 Bradley Arsenault

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
#include "Team.h"
#include "GameGUILoadSave.h"
#include "StreamBackend.h"
#include "ReplayWriter.h"

EndGameStat::EndGameStat(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, Game *game)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;

	this->hAlignFlag=hAlign;
	this->vAlignFlag=vAlign;
	
	this->game = game;

	isTeamEnabled=new bool[Team::MAX_COUNT];
	for(int x=0; x<Team::MAX_COUNT; ++x)
		isTeamEnabled[x]=true;
	
	this->type = EndOfGameStat::TYPE_UNITS;
	mouse_x = -1;
	mouse_y = -1;
}

EndGameStat::~EndGameStat()
{
	delete[] isTeamEnabled;
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
	
	
	if(game->teams[0]->stats.endOfGameStats.size()==0)
		return;
	// find maximum
	int team, maxValue = 0;
	unsigned int pos=0;
	for (team=0; team < game->mapHeader.getNumberOfTeams(); team++)
		for (pos=0; pos<game->teams[team]->stats.endOfGameStats.size(); pos++)
			maxValue = std::max(maxValue, game->teams[team]->stats.endOfGameStats[pos].value[type]);

	///You can't draw anything if the game ended so quickly that there wheren't two recorded values to draw a line between
	if(game->teams[0]->stats.endOfGameStats.size() >= 2)
	{
		//Calculate the number of digits used by the max value when rounded up to the nearest 10
		int num=10;
		maxValue+=num-(maxValue%num);
		std::stringstream maxstr;
		maxstr<<maxValue<<std::endl;
		int max_digit_count=maxstr.str().size();
		
		//Compute the maximum width used by the right-scale
		int max_width=-1;
		for(int n=0; n<num; ++n)
		{
			int value=maxValue - (maxValue*n)/num;
			std::string valueText = getRightScaleText(value, max_digit_count-1);
			int width=globalContainer->littleFont->getStringWidth(valueText.c_str());
			max_width=std::max(width, max_width);
		}
		
		//Compute the maximum height used by the time-scale
		int time_period=(game->teams[0]->stats.endOfGameStats.size()*512)/25;
		int max_height = 0;
		for(int n=1; n<16; ++n)
		{
			int time = (time_period * n) / 15;
			std::string timeText = getTimeText(time);
			int height=globalContainer->littleFont->getStringHeight(timeText.c_str());
			max_height = std::max(height, max_height);
		}
		
		///Effective width and height
		int e_width = w - max_width - 8;
		int e_height = h - max_height - 8;

		//Draw horizontal lines to given the scale of the graphs values.
		double line_seperate=double(e_height)/double(num);
		for(int n=0; n<num; ++n)
		{
			int pos=int(double(n)*line_seperate+0.5);
			int value=maxValue - (maxValue*n)/num;
			if(n!=0)
				parent->getSurface()->drawHorzLine(x+e_width-5, y+pos, 10, 255, 255, 255);
			std::string valueText = getRightScaleText(value, max_digit_count-1);
			int height=globalContainer->littleFont->getStringHeight(valueText.c_str());
			parent->getSurface()->drawString(x+e_width+8, y+pos-height/2, globalContainer->littleFont, valueText.c_str());
		}

		///Draw vertical lines to give the timescale
		double time_line_seperate=double(e_width)/double(15);
		for(int n=1; n<16; ++n)
		{
			int pos = int(double(x)+time_line_seperate*double(n)+0.5);
			int time = (time_period * n) / 15;
			if(n!=15)
				parent->getSurface()->drawVertLine(pos, y+e_height-5, 10, 255, 255, 255);
			std::string timeText = getTimeText(time);
			int width=globalContainer->littleFont->getStringWidth(timeText.c_str());
			parent->getSurface()->drawString(pos-width/2, y+e_height+8, globalContainer->littleFont, timeText);
		}

		// draw background
		parent->getSurface()->drawRect(x, y, e_width, e_height, Style::style->frameColor);

		int closest_position = 1601;
		int circle_position_value=-1;
		int circle_position_x=-1;
		int circle_position_y=-1;

		// draw curve
		if (maxValue)
		{
			for (team=0; team < game->mapHeader.getNumberOfTeams(); team++)
			{
				if(!isTeamEnabled[team])
				{
					//std::cout<<"team disabled "<<team<<std::endl;
					continue;
				}
				const Color& color = game->teams[team]->color;

				int previous_y = e_height - int(double(e_height) * getValue(0, team, type) / double(maxValue));
				
				for(int px=0; px<(e_width-2); ++px)
				{
					double value = getValue(double(px) / double(e_width-2), team, type);
					int ny = e_height - int(double(e_height) * value / double(maxValue));
					parent->getSurface()->drawLine(x + px + 1, y + previous_y, x + px, y + ny, color);
					previous_y = ny;
					int dist = (mouse_y-ny)*(mouse_y-ny) + (mouse_x-px-1)*(mouse_x-px-1);
					if(dist < closest_position)
					{
						circle_position_value = int(std::floor(value+0.5));
						circle_position_x = x + px;
						circle_position_y = y + ny;
						closest_position = dist;
					}
				}
			}
		}
		if(circle_position_x!=-1)
		{
			parent->getSurface()->drawCircle(circle_position_x, circle_position_y, 10, Color::white);
			std::stringstream str;
			str<<circle_position_value;
			parent->getSurface()->drawString(circle_position_x+10, circle_position_y+10, globalContainer->littleFont, str.str());
		}
		
		// Draw labels
		std::string label = Toolkit::getStringTable()->getString("[time]");
		int textwidth = globalContainer->standardFont->getStringWidth(label.c_str());
		parent->getSurface()->drawString(x - textwidth/2 + e_width/2, y+e_height-20, globalContainer->standardFont, label);
		
		label = getStatLabel();
		textwidth = globalContainer->standardFont->getStringWidth(label.c_str());
		parent->getSurface()->drawString(x + e_width - textwidth - 4, y + e_height/2, globalContainer->standardFont, label);
	}
	else
	{
		// draw background
		parent->getSurface()->drawRect(x, y, w, h, Style::style->frameColor);
	}
}



double EndGameStat::getValue(double position, int team, int type)
{
	int s = game->teams[team]->stats.endOfGameStats.size()-1;
	int lower = int(position * float(s));
	int upper = lower+1;
	double mu = (position * float(s)) - lower; 
	
	//int y0 = game->teams[team]->stats.endOfGameStats[std::max(lower-1, 0)].value[type];
	int y1 = game->teams[team]->stats.endOfGameStats[lower].value[type];
	int y2 = game->teams[team]->stats.endOfGameStats[upper].value[type];
	//int y3 = game->teams[team]->stats.endOfGameStats[std::min(upper+1, s)].value[type];

	//Linear interpolation
	return (1-mu) * y1 + mu * y2;

/*
	//Cubic interpolation
	double mu2 = mu * mu;
	double a0 = y3 - y2 - y0 + y1;
	double a1 = y0 - y1 - a0;
	double a2 = y2 - y0;
	double a3 = y1;
	return a0*mu*mu2+a1*mu2+a2*mu+a3;
*/
/*
	//Cosine interpolation
	double mu2 = (1-std::cos(mu*3.141592653))/2;
	return(y1*(1-mu2)+y2*mu2);
*/
}
	


std::string EndGameStat::getTimeText(int seconds)
{
	int min=int(seconds)/60;
	int sec=int(seconds)%60;
	std::stringstream str;
	str<<min<<":"<<std::setw(2)<<std::setfill('0')<<sec<<std::endl;
	return str.str();
}



std::string EndGameStat::getRightScaleText(int value, int digits)
{
	std::stringstream str;
	str<<std::setw(digits)<<std::setfill('0')<<value<<std::endl;
	return str.str();
}

std::string EndGameStat::getStatLabel()
{
	switch(type)
	{
		case EndOfGameStat::TYPE_UNITS:
			return Toolkit::getStringTable()->getString("[Number Of Units]");
		case EndOfGameStat::TYPE_BUILDINGS:
			return Toolkit::getStringTable()->getString("[Number Of Buildings]");
		case EndOfGameStat::TYPE_PRESTIGE:
			return Toolkit::getStringTable()->getString("[Prestige Score]");
		case EndOfGameStat::TYPE_HP:
			return Toolkit::getStringTable()->getString("[Total Hitpoints]");
		case EndOfGameStat::TYPE_ATTACK:
			return Toolkit::getStringTable()->getString("[Total Attack Power]");
		case EndOfGameStat::TYPE_DEFENSE:
			return Toolkit::getStringTable()->getString("[Total Defence Power]");
		default:
			assert(false);
			return "No clue how we got here.";
	}
	/*if(type == EndOfGameStat::TYPE_UNITS)
		return Toolkit::getStringTable()->getString("[Number Of Units]");
	if(type == EndOfGameStat::TYPE_BUILDINGS)
		return Toolkit::getStringTable()->getString("[Number Of Buildings]");
	if(type == EndOfGameStat::TYPE_PRESTIGE)
		return Toolkit::getStringTable()->getString("[Prestige Score]");
	if(type == EndOfGameStat::TYPE_HP)
		return Toolkit::getStringTable()->getString("[Total Hitpoints]");
	if(type == EndOfGameStat::TYPE_ATTACK)
		return Toolkit::getStringTable()->getString("[Total Attack Power]");
	if(type == EndOfGameStat::TYPE_DEFENSE)
		return Toolkit::getStringTable()->getString("[Total Defence Power]");*/
}

void EndGameStat::onSDLMouseMotion(SDL_Event* event)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);
	if(event->motion.x > x && event->motion.x < x+w && event->motion.y > y && event->motion.y < y+h)
	{
		mouse_x=event->motion.x-x;
		mouse_y=event->motion.y-y;
	}
	else
	{
		mouse_x=-1;
		mouse_y=-1;
	}
}



//! This function is used to sort the player array
struct MoreScore
{
	EndOfGameStat::Type type;
	bool operator()(const TeamEntry& t1, const TeamEntry& t2)
	{
		if(t1.endVal[type] == t2.endVal[type])
		{
			if(t1.teamNum == t2.teamNum)
				return t1.name > t2.name;
			return t1.teamNum > t2.teamNum;
		}
		return t1.endVal[type] > t2.endVal[type];
	}
};


EndGameScreen::EndGameScreen(GameGUI *gui)
{
	// We're no longer replaying a game
	globalContainer->replaying = false;

	// title & graph
	std::string titleText;
	
	if (gui->game.totalPrestigeReached && gui->game.isPrestigeWinCondition())
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

			std::string playerText = t->getFirstPlayerName();
			strText.arg(playerText);
			titleText = strText;
		}
	}
	else if(gui->getLocalTeam()->hasWon)
	{
		titleText=Toolkit::getStringTable()->getString("[Won : you defeated your opponents]");
	}
	else if (gui->getLocalTeam()->hasLost)
	{
		titleText=Toolkit::getStringTable()->getString("[Lost : your colony is dead]");
	}
	else if (!gui->game.isGameEnded)
	{
		titleText=Toolkit::getStringTable()->getString("[The game has not been finished]");
	}
	
	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_LEFT, "menu", titleText.c_str()));
	statWidget=new EndGameStat(20, 80, 180, 120, ALIGN_FILL, ALIGN_FILL, &(gui->game));
	addWidget(statWidget);

	graphLabel=new Text(25, 85, ALIGN_LEFT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[Units]"));
	addWidget(graphLabel);
	
	// add buttons
	// FIXME: magic numbers!
	addWidget(new TextButton(90, 65, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "standard", Toolkit::getStringTable()->getString("[Units]"), 0, '1'));
	addWidget(new TextButton(190, 65, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "standard", Toolkit::getStringTable()->getString("[Buildings]"), 1, '2'));
	addWidget(new TextButton(290, 65, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "standard", Toolkit::getStringTable()->getString("[Prestige]"), 2, '3'));
	addWidget(new TextButton(90, 40, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "standard", Toolkit::getStringTable()->getString("[hp]"), 3, '4'));
	addWidget(new TextButton(190, 40, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "standard", Toolkit::getStringTable()->getString("[Attack]"), 4, '5'));
	addWidget(new TextButton(290, 40, 80, 20, ALIGN_SCREEN_CENTERED, ALIGN_BOTTOM, "standard", Toolkit::getStringTable()->getString("[Defense]"), 5, '6'));

	if (globalContainer->replayWriter && globalContainer->replayWriter->isValid())
	{
		addWidget(new TextButton(15, 65, 250, 40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[save replay]"), 39, 's'));
		addWidget(new TextButton(15, 15, 250, 40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[quit]"), 38, 13));
	}
	else
	{
		addWidget(new TextButton(15, 40, 250, 40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[quit]"), 38, 13));
	}

	// add players name
	Text *text;
	int inc = (gui->game.mapHeader.getNumberOfTeams() <= 16) ? 20 : 10;

	// set teams entries for later sort
	for (int i=0; i<gui->game.gameHeader.getNumberOfPlayers(); i++)
	{
		struct TeamEntry entry;
		entry.name=gui->game.gameHeader.getBasePlayer(i).name;
		entry.teamNum=gui->game.gameHeader.getBasePlayer(i).teamNumber;
		entry.color=gui->game.teams[entry.teamNum]->color;
		int endIndex=gui->game.teams[entry.teamNum]->stats.endOfGameStats.size()-1;
		for (int j=0; j<EndOfGameStat::TYPE_NB_STATS; j++)
		{
			entry.endVal[j]=gui->game.teams[entry.teamNum]->stats.endOfGameStats[endIndex].value[(EndOfGameStat::Type)j];
		}
		teams.push_back(entry);
	}

	// add widgets
	for (unsigned i=0; i<teams.size(); i++)
	{
		OnOffButton* enabled_button = new OnOffButton(10, 80+(i*inc), inc, inc, ALIGN_RIGHT, ALIGN_TOP, true, 6+i);
		team_enabled_buttons.push_back(enabled_button);
		addWidget(enabled_button);
		
		if((i>0 && teams[i-1].teamNum != teams[i].teamNum) || i==0)
		{
			team_enabled_buttons[i]->visible=true;
		}
		else
		{
			team_enabled_buttons[i]->visible=false;
		}
		
		text=new Text(10+inc, 80+(i*inc), ALIGN_RIGHT, ALIGN_TOP, "standard", "", 140);
		names.push_back(text);
		addWidget(text);
	}
	
	// Save the step and order count
	game = &(gui->game);
	
	sortAndSet(EndOfGameStat::TYPE_UNITS);
}

void EndGameScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if(par1==38)
		{
			endExecute(par1);
		}
		///This is a change in the graph type
		else if (par1<6)
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
		}
		///One of the buttons beside the team names where selected
		else if(par1 >= 6 && par1 < static_cast<int>(6+teams.size()))
		{
			int n=par1-6;
			statWidget->setEnabledState(teams[n].teamNum, team_enabled_buttons[n]->getState());
			for (unsigned i=0; i<teams.size(); i++)
			{
				if(teams[i].teamNum == teams[n].teamNum)
				{
					team_enabled_buttons[i]->setState(team_enabled_buttons[n]->getState());
				}
			}
		}
		/// The "Save Replay" button was pressed
		else if (par1 == 39)
		{
			saveReplay("replays","replay");
		}
		else assert(false);
	}
}


void EndGameScreen::sortAndSet(EndOfGameStat::Type type)
{
	// Resort the names on the side of the graph based on their respective scores
	MoreScore moreScore;
	moreScore.type=type;
	std::sort(teams.begin(), teams.end(), moreScore);
	
	int prev_num=1;
	for (unsigned i=0; i<teams.size(); i++)
	{
		std::stringstream str;
		if(i>0 && teams[i].teamNum == teams[i-1].teamNum)
		{
			str<<"    "<<teams[i].name;
		}
		else if(i>0 && teams[i].endVal[type] == teams[i-1].endVal[type])
		{
			str<<"#"<<prev_num<<": "<<teams[i].name;
		}
		else
		{
			str<<"#"<<i+1<<": "<<teams[i].name;
			prev_num=i+1;
		}
	
		names[i]->setText(str.str().c_str());
		names[i]->setStyle(Font::Style(Font::STYLE_NORMAL, teams[i].color));
		if(team_enabled_buttons[i])
			team_enabled_buttons[i]->returnCode=6+i;
		
		
		if((i>0 && teams[i-1].teamNum != teams[i].teamNum) || i==0)
		{
			team_enabled_buttons[i]->visible=true;
		}
		else
		{
			team_enabled_buttons[i]->visible=false;
		}
	}
}

std::string replayFilenameToName(const std::string& fullfilename)
{
	std::string filename = fullfilename;
	filename.erase(0, 8);
	filename.erase(filename.find(".replay"));
	std::replace(filename.begin(), filename.end(), '_', ' ');
	return filename;
}

void EndGameScreen::saveReplay(const char *dir, const char *ext)
{
	// create dialog box
	LoadSaveScreen *loadSaveScreen=new LoadSaveScreen(dir, ext, false, std::string(Toolkit::getStringTable()->getString("[save replay]")), "", replayFilenameToName, glob2NameToFilename);
	loadSaveScreen->dispatchPaint();

	// save screen
	globalContainer->gfx->setClipRect();
	
	DrawableSurface *background = new DrawableSurface(globalContainer->gfx->getW(), globalContainer->gfx->getH());
	background->drawSurface(0, 0, globalContainer->gfx);

	SDL_Event event;
	while(loadSaveScreen->endValue<0)
	{
		Uint64 time = SDL_GetTicks64();
		while (SDL_PollEvent(&event))
		{
			loadSaveScreen->translateAndProcessEvent(&event);
		}
		loadSaveScreen->dispatchPaint();
		
		globalContainer->gfx->drawSurface(0, 0, background);
		globalContainer->gfx->drawSurface(loadSaveScreen->decX, loadSaveScreen->decY, loadSaveScreen->getSurface());
		globalContainer->gfx->nextFrame();
		Uint64 ntime = SDL_GetTicks64();
		SDL_Delay(std::max<Uint64>(0, 40 - ntime + time));
	}

	if (loadSaveScreen->endValue==0)
	{
		// Write the replay to the file
		assert(globalContainer->replayWriter);
		assert(globalContainer->replayWriter->isValid());
		globalContainer->replayWriter->write(loadSaveScreen->getFileName());
	}

	// clean up
	delete loadSaveScreen;
	
	// destroy temporary surface
	delete background;
}
