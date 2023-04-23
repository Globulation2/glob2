/*
  Copyright (C) 2001-2008 Stephane Magnenat, Luc-Olivier de Charrière
  and Martin S. Nyffenegger
  for any question or comment contact us at <stephane at magnenat dot net>, <NuageBleu at gmail dot com>
  or barock@ysagoon.com

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

/*!	\file SGSL.cpp
	\brief SGSL: Simple Globulation Scripting Language: implementation of classes for map scripting
*/

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <string.h>
#include <math.h>

#include <Toolkit.h>
#include <StringTable.h>
#include <Stream.h>

#include "GameGUI.h"
#include "Player.h"
#include "GlobalContainer.h"
#include "SGSL.h"
#include "Unit.h"
#include "Utilities.h"

using std::string;
using std::cerr;
using std::endl;

SGSLToken::TokenSymbolLookupTable SGSLToken::table[] =
{
	{ INT, "int" },
	{ STRING, "string" },
	{ LANG, "lang" },
	{ FUNC_CALL, "function call" },

	{ S_PAROPEN, "("},
	{ S_PARCLOSE, ")"},
	{ S_SEMICOL, ","},
	{ S_STORY, "story" },

	{ S_EQUAL, "=" },
	{ S_HIGHER, ">" },
	{ S_LOWER, "<" },
	{ S_NOT, "not" },

	{ S_WAIT, "wait" },
	{ S_SPACE, "space" },
	{ S_TIMER, "timer" },
	{ S_SHOW, "show" },
	{ S_HIDE, "hide" },
	{ S_ALLIANCE, "alliance"},
	{ S_GUIENABLE, "guiEnable"},
	{ S_GUIDISABLE, "guiDisable"},
	{ S_SUMMONUNITS, "summonUnits" },
	{ S_SUMMONFLAG, "summonFlag" },
	{ S_DESTROYFLAG, "destroyFlag" },
	{ S_WIN, "win" },
	{ S_LOOSE, "loose" },
	{ S_LABEL, "label" },
	{ S_JUMP, "jump" },
	{ S_SETAREA, "setArea"},
	{ S_AREA, "area" },
	{ S_ISDEAD, "isdead" },
	{ S_ALLY, "ally" },
	{ S_ENEMY, "enemy" },
	{ S_ONLY, "only" },

	{ S_WORKER, "Worker" },
	{ S_EXPLORER, "Explorer" },
	{ S_WARRIOR, "Warrior" },
	{ S_SWARM_B, "Swarm" },
	{ S_FOOD_B, "Inn" },
	{ S_HEALTH_B, "Hospital" },
	{ S_WALKSPEED_B, "Racetrack" },
	{ S_SWIMSPEED_B, "Pool" },
	{ S_ATTACK_B, "Camp" },
	{ S_SCIENCE_B, "School" },
	{ S_DEFENCE_B, "Tower" },
	{ S_MARKET_B, "Market"},
	{ S_WALL_B, "Wall"},
	{ S_EXPLOR_F, "ExplorationFlag"},
	{ S_FIGHT_F, "WarFlag"},
	{ S_CLEARING_F, "ClearingFlag"},
	{ S_ALLIANCESCREEN, "AllianceScreen"},
	{ S_BUILDINGTAB, "BuildingTab"},
	{ S_FLAGTAB, "FlagTab"},
	{ S_TEXTSTATTAB, "TextStatTab"},
	{ S_GFXSTATTAB, "GfxStatTab"},

	// NIL must be at the end because it is a stop condition... not very clean
	{ NIL, "" },
};

SGSLToken::TokenType SGSLToken::getTypeByName(const std::string name)
{
	int i = 0;
	TokenType type=NIL;

	//std::cout << "Getting token for " << name << std::endl;

	if (name.empty())
	{
		std::cerr << "Warning, SGSLToken::getTypeByName(name) called with empty name!" << std::endl;
		return NIL;
	}
	
	while (table[i].type != NIL)
	{
		//NOTE: SM: I reverted back to case-insensitive, as the other one breaks tutorial
		if (strcasecmp(name.c_str(), table[i].name.c_str())==0)
		//if (name.compare(table[i].name) == 0)
		{
			type = table[i].type;
			break;
		}
		i++;
	}
	
	if (type == NIL)
	{
		std::cerr << "Warning, SGSLToken::getTypeByName(name) found no type for name " << name << "!" << std::endl;
	}
	
	return type;
}

std::string SGSLToken::getNameByType(SGSLToken::TokenType type)
{
	int i = 0;
	std::string name="";

	if (type != NIL)
		while (!table[i].name.empty())
		{
			if (type == table[i].type)
			{
				name = table[i].name;
				break;
			}
			i++;
		}
	return name;
}

Story::Story(MapScriptSGSL *mapscript)
{
	lineSelector = 0;
	internTimer=0;
	recievedSpace=false;
	this->mapscript=mapscript;
}

Story::~Story()
{

}

//get values from the game
int Story::valueOfVariable(const Game *game, SGSLToken::TokenType type, int teamNumber, int level)
{
	TeamStat *latestStat=game->teams[teamNumber]->stats.getLatestStat();
	switch(type)
	{
		case(SGSLToken::S_WORKER):
			return latestStat->numberUnitPerType[0];
		case(SGSLToken::S_EXPLORER):
			return latestStat->numberUnitPerType[1];
		case(SGSLToken::S_WARRIOR):
			return latestStat->numberUnitPerType[2];
		case(SGSLToken::S_SWARM_B):
			return latestStat->numberBuildingPerTypePerLevel[0][level];
		case(SGSLToken::S_FOOD_B):
			return latestStat->numberBuildingPerTypePerLevel[1][level];
		case(SGSLToken::S_HEALTH_B):
			return latestStat->numberBuildingPerTypePerLevel[2][level];
		case(SGSLToken::S_WALKSPEED_B):
			return latestStat->numberBuildingPerTypePerLevel[3][level];
		case(SGSLToken::S_SWIMSPEED_B):
			return latestStat->numberBuildingPerTypePerLevel[4][level];
		case(SGSLToken::S_ATTACK_B):
			return latestStat->numberBuildingPerTypePerLevel[5][level];
		case(SGSLToken::S_SCIENCE_B):
			return latestStat->numberBuildingPerTypePerLevel[6][level];
		case(SGSLToken::S_DEFENCE_B):
			return latestStat->numberBuildingPerTypePerLevel[7][level];
		case(SGSLToken::S_WALL_B):
			return latestStat->numberBuildingPerTypePerLevel[11][level];
		default:
			assert(false);
			return 0;
	}
}

// code for testing conditions. If readLevel is true, for building check for specific level. If atMin is true, add higher levels too
bool Story::conditionTester(const Game *game, int pc, bool readLevel, bool only)
{
	SGSLToken::TokenType type, operation;
	int level, teamNumber, amount;

	type = line[pc++].type;
	teamNumber = line[pc++].value;
	if (readLevel)
		level = line[pc++].value;
	else
		level = -1;
	operation = line[pc++].type;
	amount = line[pc].value;
	
	// if we want all unit over one level, sum
	int val = 0;
	if (!only)
		for (int i = level; i < 6; i++)
			val += valueOfVariable(game, type, teamNumber, i);
	else
		val = valueOfVariable(game, type, teamNumber, level);
		
	switch (operation)
	{
		case (SGSLToken::S_HIGHER):
		{
			if (verbose)
			std::cout << "Story::conditionTester : SGSL thread " << this << " testing "
				  << SGSLToken::getNameByType(type) << " ("
				  << teamNumber << ", " << level << ") : "
				  << val << " >? " << amount << std::endl;
			return (val > amount);
		}
		case (SGSLToken::S_LOWER):
		{
			if (verbose)
				std::cout << "Story::conditionTester : SGSL thread " << this << " testing "
				  << SGSLToken::getNameByType(type) << " ("
					  << teamNumber << ", " << level << ") : "
					  << val << " <? " << amount << std::endl;
			return (val < amount);
		}
		case (SGSLToken::S_EQUAL):
		{
			if (verbose)
				std::cout << "Story::conditionTester : SGSL thread " << this << " testing "
					  << SGSLToken::getNameByType(type) << " ("
					  << teamNumber << ", " << level << ") : "
					  << val << " =? " << amount << std::endl;
			return (val == amount);
		}
		default:
			return false;
	}
}

void Story::toto(GameGUI* gui)
{
	std::cout << "toto func : ";
	std::cout << SGSLToken::getNameByType(line[++lineSelector].type) << " ";
	std::cout << line[++lineSelector].value << "\n";
}



void Story::objectiveHidden(GameGUI* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game.objectives.getNumberOfObjectives(); ++i)
	{
		if(gui->game.objectives.getScriptNumber(i) == n)
		{
			gui->game.objectives.setObjectiveHidden(i);
			break;
		}
	}
}



void Story::objectiveVisible(GameGUI* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game.objectives.getNumberOfObjectives(); ++i)
	{
		if(gui->game.objectives.getScriptNumber(i) == n)
		{
			gui->game.objectives.setObjectiveVisible(i);
			break;
		}
	}
}



void Story::objectiveComplete(GameGUI* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game.objectives.getNumberOfObjectives(); ++i)
	{
		if(gui->game.objectives.getScriptNumber(i) == n)
		{
			gui->game.objectives.setObjectiveComplete(i);
			break;
		}
	}
}



void Story::objectiveFailed(GameGUI* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game.objectives.getNumberOfObjectives(); ++i)
	{
		if(gui->game.objectives.getScriptNumber(i) == n)
		{
			gui->game.objectives.setObjectiveFailed(i);
			break;
		}
	}
}



void Story::hintHidden(GameGUI* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game.gameHints.getNumberOfHints(); ++i)
	{
		if(gui->game.gameHints.getScriptNumber(i) == n)
		{
			gui->game.gameHints.setHintHidden(i);
			break;
		}
	}
}



void Story::hintVisible(GameGUI* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game.gameHints.getNumberOfHints(); ++i)
	{
		if(gui->game.gameHints.getScriptNumber(i) == n)
		{
			gui->game.gameHints.setHintVisible(i);
			break;
		}
	}
}

void Story::setHighlightItem(GameGUI* gui, bool doSet)
{
	std::string n = line[++lineSelector].msg;
	int t=0;
	if(n=="main menu icon")
	{
		t=GameGUI::HilightMainMenuIcon;
	}
	else if(n=="right side panel")
	{
		t=GameGUI::HilightRightSidePanel;
	}
	else if(n=="under minimap icons")
	{
		t=GameGUI::HilightUnderMinimapIcon;
	}
	else if(n=="units assigned bar")
	{
		t=GameGUI::HilightUnitsAssignedBar;
	}
	else if(n=="units ratio bar")
	{
		t=GameGUI::HilightRatioBar;
	}
	else if(n=="workers working free stat")
	{
		t=GameGUI::HilightWorkersWorkingFreeStat;
	}
	else if(n=="explorers working free stat")
	{
		t=GameGUI::HilightExplorersWorkingFreeStat;
	}
	else if(n=="warriors working free stat")
	{
		t=GameGUI::HilightWarriorsWorkingFreeStat;
	}
	else if(n=="forbidden zone on panel")
	{
		t=GameGUI::HilightWorkersWorkingFreeStat;
	}
	else if(n=="guard zone on panel")
	{
		t=GameGUI::HilightGuardZoneOnPanel;
	}
	else if(n=="clearing zone on panel")
	{
		t=GameGUI::HilightClearingZoneOnPanel;
	}
	else if(n=="brush selector")
	{
		t=GameGUI::HilightBrushSelector;
	}
	if(t!=0)
	{
		if(doSet)
		{
			gui->hilights.insert(t);
		}
		else
		{
			gui->hilights.erase(t);
		}
	}
}

void Story::hilightItem(GameGUI* gui)
{
	setHighlightItem(gui, true);
}



void Story::unhilightItem(GameGUI* gui)
{
	setHighlightItem(gui, false);
}



void Story::hilightUnits(GameGUI* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_WORKER;
	gui->hilights.insert(GameGUI::HilightWorkers+n);
}



void Story::unhilightUnits(GameGUI* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_WORKER;
	gui->hilights.erase(GameGUI::HilightWorkers+n);
}



void Story::hilightBuildings(GameGUI* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_SWARM_B;
	gui->hilights.insert(GameGUI::HilightBuildingOnMap+n);
}



void Story::unhilightBuildings(GameGUI* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_SWARM_B;
	gui->hilights.erase(GameGUI::HilightBuildingOnMap+n);
}



void Story::hilightBuildingOnPanel(GameGUI* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_SWARM_B;
	gui->hilights.insert(GameGUI::HilightBuildingOnPanel+n);
}



void Story::unhilightBuildingOnPanel(GameGUI* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_SWARM_B;
	gui->hilights.erase(GameGUI::HilightBuildingOnPanel+n);
}



void Story::resetAI(GameGUI* gui)
{
	int player = line[++lineSelector].value;
	int aitype = line[++lineSelector].value;
	if(gui->game.players[player])
	{
		gui->game.players[player]->makeItAI(static_cast<AI::ImplementitionID>(aitype));
	}
}


static const FunctionArgumentDescription totoDescription[] = {
	{ SGSLToken::S_WIN, SGSLToken::S_LOOSE },
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

static const FunctionArgumentDescription objectiveCompleteDescription[] = {
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

static const FunctionArgumentDescription objectiveHiddenDescription[] = {
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

static const FunctionArgumentDescription objectiveVisibleDescription[] = {
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

static const FunctionArgumentDescription objectiveFailedDescription[] = {
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

static const FunctionArgumentDescription hintHiddenDescription[] = {
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

static const FunctionArgumentDescription hintVisibleDescription[] = {
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

static const FunctionArgumentDescription hilightItemDescription[] = {
	{ SGSLToken::STRING, SGSLToken::STRING },
	{ -1, -1}
};

static const FunctionArgumentDescription unhilightItemDescription[] = {
	{ SGSLToken::STRING, SGSLToken::STRING },
	{ -1, -1}
};

static const FunctionArgumentDescription hilightUnitsDescription[] = {
	{ SGSLToken::S_WORKER, SGSLToken::S_WARRIOR },
	{ -1, -1}
};

static const FunctionArgumentDescription unhilightUnitsDescription[] = {
	{ SGSLToken::S_WORKER, SGSLToken::S_WARRIOR },
	{ -1, -1}
};

static const FunctionArgumentDescription hilightBuildingsDescription[] = {
	{ SGSLToken::S_SWARM_B, SGSLToken::S_MARKET_B },
	{ -1, -1}
};

static const FunctionArgumentDescription unhilightBuildingsDescription[] = {
	{ SGSLToken::S_SWARM_B, SGSLToken::S_MARKET_B },
	{ -1, -1}
};

static const FunctionArgumentDescription hilightBuildingOnPanelDescription[] = {
	{ SGSLToken::S_SWARM_B, SGSLToken::S_MARKET_B },
	{ -1, -1}
};

static const FunctionArgumentDescription unhilightBuildingOnPanelDescription[] = {
	{ SGSLToken::S_SWARM_B, SGSLToken::S_MARKET_B },
	{ -1, -1}
};

static const FunctionArgumentDescription resetAIDescription[] = {
	{ SGSLToken::INT, SGSLToken::INT },
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};



//main step-by-step machine
bool Story::testCondition(GameGUI *gui)
{
	Game *game = &gui->game;

	if (line.size())
		switch (line[lineSelector].type)
		{
			case (SGSLToken::S_STORY):
			{
				return false;
			}
			
			case (SGSLToken::FUNC_CALL):
			{
				Functions::const_iterator fIt = mapscript->functions.find(line[lineSelector].msg);
				assert(fIt != mapscript->functions.end());
				(this->*(fIt->second.second))(gui);
				return true;
			}
			
			case (SGSLToken::S_SHOW):
			{
				unsigned lsInc=0;
				if (line[lineSelector+2].type == SGSLToken::LANG)
				{
					if (line[lineSelector+2].msg != globalContainer->settings.language)
					{
						lineSelector += 2;
						return true;
					}
					lsInc = 1;
				}
				mapscript->isTextShown = true;
				mapscript->textShown = line[++lineSelector].msg;
				lineSelector += lsInc;
				return true;
			}

			case (SGSLToken::S_WIN):
			{
				mapscript->hasWon.at(line[++lineSelector].value)=true;
				return true;
			}

			case (SGSLToken::S_LOOSE):
			{
				mapscript->hasLost.at(line[++lineSelector].value)=true;
				return true;
			}

			case (SGSLToken::S_TIMER):
			{
				mapscript->mainTimer=line[++lineSelector].value;
				return true;
			}

			case (SGSLToken::S_ALLIANCE):
			{
				int team1 = line[++lineSelector].value;
				int team2 = line[++lineSelector].value;
				int level = line[++lineSelector].value;

				// Who do I thrust and don't fire on.
				Uint32 allies[4] = { 0, 0, 0, 1};
				// Who I don't thrust and fire on.
				Uint32 enemies[4] = { 1, 0, 0, 0};
				// Who does I share the vision of Exchange building to.
				Uint32 sharedVisionExchange[4] = { 0, 1, 1, 1};
				// Who does I share the vision of Food building to.
				Uint32 sharedVisionFood[4] = { 0, 0, 1, 1};
				// Who does I share the vision to.
				Uint32 sharedVisionOther[4] = { 0, 0, 0, 1};

				if (allies[level])
					gui->game.teams[team1]->allies |= 1<<team2;
				else
					gui->game.teams[team1]->allies &= ~(1<<team2);

				if (enemies[level])
					gui->game.teams[team1]->enemies |= 1<<team2;
				else
					gui->game.teams[team1]->enemies &= ~(1<<team2);

				if (sharedVisionExchange[level])
					gui->game.teams[team1]->sharedVisionExchange |= 1<<team2;
				else
					gui->game.teams[team1]->sharedVisionExchange &= ~(1<<team2);

				if (sharedVisionFood[level])
					gui->game.teams[team1]->sharedVisionFood |= 1<<team2;
				else
					gui->game.teams[team1]->sharedVisionFood &= ~(1<<team2);

				if (sharedVisionOther[level])
					gui->game.teams[team1]->sharedVisionOther |= 1<<team2;
				else
					gui->game.teams[team1]->sharedVisionOther &= ~(1<<team2);

				return true;
			}

			case (SGSLToken::S_LABEL):
			{
				lineSelector++;
				return true;
			}

			case (SGSLToken::S_JUMP):
			{
				lineSelector = labels[line[lineSelector+1].msg];
				return true;
			}

			case (SGSLToken::INT):
			{
				internTimer--;
				if (internTimer==0)
					return true;
				else
					return false;
			}

			case (SGSLToken::S_GUIENABLE):
			{
				// TODO : be clean and dynamic and generic here !
				SGSLToken::TokenType object = line[++lineSelector].type;
				if (object <= SGSLToken::S_WARRIOR)
				{
					// Units : TODO
				}
				else if (object <= SGSLToken::S_MARKET_B)
				{
					gui->enableBuildingsChoice(IntBuildingType::typeFromShortNumber(object - SGSLToken::S_SWARM_B));
				}
				else if (object <= SGSLToken::S_CLEARING_F)
				{
					gui->enableFlagsChoice(IntBuildingType::typeFromShortNumber(object - SGSLToken::S_EXPLOR_F + IntBuildingType::EXPLORATION_FLAG));
				}
				else if (object <= SGSLToken::S_ALLIANCESCREEN)
				{
					gui->enableGUIElement(object - SGSLToken::S_BUILDINGTAB);
				}
				return true;
			}

			case (SGSLToken::S_GUIDISABLE):
			{
				SGSLToken::TokenType object = line[++lineSelector].type;
				if (object <= SGSLToken::S_WARRIOR)
				{
					// Units : TODO
				}
				else if (object <= SGSLToken::S_MARKET_B)
				{
					gui->disableBuildingsChoice(IntBuildingType::typeFromShortNumber(object - SGSLToken::S_SWARM_B));
				}
				else if (object <= SGSLToken::S_CLEARING_F)
				{
					gui->disableFlagsChoice(IntBuildingType::typeFromShortNumber(object - SGSLToken::S_EXPLOR_F + IntBuildingType::EXPLORATION_FLAG));
				}
				else if (object <= SGSLToken::S_ALLIANCESCREEN)
				{
					gui->disableGUIElement(object - SGSLToken::S_BUILDINGTAB);
				}
				return true;
			}

			case (SGSLToken::S_SUMMONUNITS):
			{
				const std::string& areaName = line[++lineSelector].msg;
				int globulesAmount = line[++lineSelector].value;
				int type = line[++lineSelector].type - SGSLToken::S_WORKER;
				int level = line[++lineSelector].value;
				int team = line[++lineSelector].value;
				
				int areaN=-1;
				//First, check if there is a script area in the map with the same name
				for(int n=0; n<9; ++n)
				{
					if(game->map.getAreaName(n)==areaName)
					{
						areaN=n;
						break;
					}
				}
				//There isn't a map script area with the same name, try the old map scripts
				if(areaN==-1)
				{
					AreaMap::const_iterator fi;
					if ((fi = mapscript->areas.find(areaName)) != mapscript->areas.end())
					{
						int number = globulesAmount;
						int maxTest = number * 3;

						while ((number>0) && (maxTest>0))
						{
							int x = fi->second.x;
							int y = fi->second.y;
							int r = fi->second.r;
							int dx=(syncRand()%(2*r))+1;
							int dy=(syncRand()%(2*r))+1;
							dx-=r;
							dy-=r;

							if (dx*dx+dy*dy<r*r)
							{
								if (game->addUnit(x+dx, y+dy, team, type, level, 0, 0, 0))
								{
									number --;
								}
							}

							maxTest--;
						}
					}
				}
				else
				{
					int number = globulesAmount;
					for(int x=0; x<game->map.getW() && number; ++x)
					{
						for(int y=0; y<game->map.getH() && number; ++y)
						{
							if(game->map.isPointSet(areaN, x, y))
							{
								if (game->addUnit(x, y, team, type, level, 0, 0, 0))
								{
									number --;
								}
							}
						}
					}
				}
				return true;
			}

			case SGSLToken::S_SUMMONFLAG:
			{
				const std::string& flagName = line[++lineSelector].msg;
				int x = line[++lineSelector].value;
				int y = line[++lineSelector].value;
				int r = line[++lineSelector].value;
				int unitCount = line[++lineSelector].value;
				int team = line[++lineSelector].value;

				int typeNum = globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);

				Building *b = game->addBuilding(x, y, typeNum, team);

				b->unitStayRange = r;
				b->maxUnitWorking = unitCount;
				b->maxUnitWorkingPreferred = unitCount;
				b->maxUnitWorkingLocal = unitCount;
				b->update();

				mapscript->flags[flagName] = b;

				return true;
			}

			case SGSLToken::S_DESTROYFLAG:
			{
				const std::string& flagName = line[++lineSelector].msg;
				BuildingMap::iterator i;
				if ((i = mapscript->flags.find(flagName)) != mapscript->flags.end())
				{
					i->second->launchDelete();
					mapscript->flags.erase(i);
				}
				else
				{
					std::cerr << "SGSL : Unexistant flag " << flagName << " destroyed !" << std::endl;
				}

				return true;
			}

			case (SGSLToken::S_SETAREA):
			{
				Area flag;

				std::string name = line[++lineSelector].msg;
				flag.x = line[++lineSelector].value;
				flag.y = line[++lineSelector].value;
				flag.r = line[++lineSelector].value;

				mapscript->areas[name] = flag;

				return true;
			}

			case (SGSLToken::S_HIDE):
			{
				mapscript->isTextShown = false;
				return true;
			}

			case (SGSLToken::S_SPACE):
			{
				if (recievedSpace)
				{
					return true;
				}
				else
				{
					gui->setSwallowSpaceKey(true);
					return false;
				}
			}

			case (SGSLToken::S_WAIT):
			{
				bool negate = false;
				int execLine = lineSelector+1;

				if (line[execLine].type == SGSLToken::S_NOT)
				{
					negate = true;
					execLine++;
				}
				switch (line[execLine].type)
				{
					case (SGSLToken::INT):
					{
						// The idea is to put an int token on execution which stands for decrement and waiting
						internTimer = line[execLine].value;
						lineSelector = execLine;
						return false;
					}
					case (SGSLToken::S_ISDEAD):
					{
						execLine++;
						if (!game->teams[line[execLine].value]->isAlive)
						{
							lineSelector = execLine;
							return true;
						}
						else
							return false;
					}
					case (SGSLToken::S_AREA):
					{
						execLine++;

						std::string areaName=line[execLine].msg;

						execLine++;
						Uint32 teamsToTestMask = 0;
						//A team number is given
						if (line[execLine].type==SGSLToken::INT)
						{
							teamsToTestMask = 1<<(line[execLine].value);
						}
						//All of the enemies are given
						else if (line[execLine].type==SGSLToken::S_ENEMY)
						{
							execLine++;
							teamsToTestMask = game->teams[line[execLine].value]->enemies;
						}
						//All of the allies are given
						else if (line[execLine].type==SGSLToken::S_ALLY)
						{
							execLine++;
							teamsToTestMask = game->teams[line[execLine].value]->allies;
						}
						else
							assert(false);

						int areaN=-1;
						bool foundUnit=false;
						//First, check if there is a script area in the map with the same name
						for(int n=0; n<9; ++n)
						{
							if(game->map.getAreaName(n)==areaName)
							{
								areaN=n;
								break;
							}
						}
						//There isn't a map script area with the same name, try the old map scripts
						if(areaN==-1)
						{
							AreaMap::const_iterator fi;
							if ((fi = mapscript->areas.find(line[execLine].msg)) == mapscript->areas.end())
								assert(false);
	
							int x = fi->second.x;
							int y = fi->second.y;
							int r = fi->second.r;
							int dx, dy;
							for (dy=y-r; dy<y+r && !foundUnit; dy++)
							{
								for (dx=x-r; dx<x+r && !foundUnit; dx++)
								{
									Uint16 gid=game->map.getGroundUnit(dx, dy);
									if (gid!=NOGUID)
									{
										int team=Unit::GIDtoTeam(gid);
										if ((1<<team) & teamsToTestMask)
										{
											foundUnit = true;
										}
									}
								}
							}
						}
						//There is a map script area with the same name, check the positions
						else
						{
							for(int x=0; x<game->map.getW() && !foundUnit; ++x)
							{
								for(int y=0; y<game->map.getH() && !foundUnit; ++y)
								{
									if(game->map.isPointSet(areaN, x, y))
									{
										Uint16 gid=game->map.getGroundUnit(x, y);
										if (gid!=NOGUID)
										{
											int team=Unit::GIDtoTeam(gid);
											if ((1<<team) & teamsToTestMask)
											{
												foundUnit = true;
											}
										}
									}
								}
							}
						}

						if ((foundUnit && !negate) || (!foundUnit && negate))
						{
							lineSelector = execLine;
							return true;
						}
						else
						{
							return false;
						}
					}
					case (SGSLToken::S_WORKER):
					case (SGSLToken::S_EXPLORER):
					case (SGSLToken::S_WARRIOR):
					{
						bool conditionResult = conditionTester(game, execLine, false, true);
						conditionResult ^= negate;
						if (conditionResult)
						{
							lineSelector += 4;
							lineSelector += negate ? 1 : 0;
							return true;
						}
						else
							return false;
					}
					break;
					default: //Test conditions
					{
						// Check if we have the "atmin" keyword
						bool only = false;
						if (line[execLine].type == SGSLToken::S_ONLY)
						{
							only = true;
							execLine++;
						}
						
						// Do the test
						bool conditionResult = conditionTester(game, execLine, true, only);
						conditionResult ^= negate;
						if (conditionResult)
						{
							lineSelector += 5;
							lineSelector += negate ? 1 : 0;
							lineSelector += only ? 1 : 0;
							return true;
						}
						else
							return false;
					}
				}
			}

			default:
				return false;
		}
	return false;
}

void Story::syncStep(GameGUI *gui)
{
	int cycleLeft = 256;

	if (verbose)
		std::cout << "Story::syncStep : SGSL thread " << this << " PC : " << lineSelector << " (" << SGSLToken::getNameByType(line[lineSelector].type) << ")" << std::endl;
	while (testCondition(gui) && cycleLeft)
	{
		lineSelector++;
		cycleLeft--;
		if (verbose)
			std::cout << "Story::syncStep : SGSL thread " << this << " PC : " << lineSelector << " (" << SGSLToken::getNameByType(line[lineSelector].type) << ")" << std::endl;
		recievedSpace=false;
	}

	if (!cycleLeft)
		std::cout << "Story::syncStep : SGSL : Warning, story step took more than 256 cycles, perhaps you have infinite loop in your script" << std::endl;
}


std::string ErrorReport::getErrorString(void) const
{
	static const std::string strings[]={
		"No error",
		"Invalid Value ",
		"Syntax error",
		"Invalid team",
		"No such file",
		"Area name not defined",
		"Area name already defined",
		"Label not defined",
		"Missing \"(\"",
		"Missing \")\"",
		"Missing \",\"",
		"Missing argument",
		"Invalid alliance level. Level must be between 0 and 3",
		"Not a valid language identifier",
		"Summing of a specific level is only valid for buildings",
		"The type of the argument to the function is wrong",
		"Unknown error"
	};
	assert(type >= 0);
	assert(type < ET_NB_ET);
	assert(ET_NB_ET == sizeof(strings)/sizeof(const char *));
	return strings[(int)type];
}

//Text aquisition by the parser
Aquisition::~Aquisition(void)
{

}

Aquisition::Aquisition(const Functions& functions) :
	functions(functions)
{
	token.type=SGSLToken::NIL;
	actLine=0;
	actCol=0;
	actPos=0;
	lastLine=0;
	lastCol=0;
	lastPos=0;
	newLine=true;
}

#define HANDLE_ERROR_POS(c) { actPos++; if (c=='\n') { actLine++; actCol=0; } else { actCol++; } }
#undef getc

#ifdef WIN32 
const char *index(const char *str, char f)
{
	for(const char *a=str;*a;a++)
	{
		if(*a==f)
			return a;
	}
	return NULL;
}
#endif

//Tokenizer
void Aquisition::nextToken()
{
	string word;
	int c;
	lastCol=actCol;
	lastLine=actLine;
	lastPos=actPos;
	// eat empty char
	while(( c=this->getChar() )!=EOF)
	{
		if (c=='#' && newLine)
		{
			while ((c!=EOF) && (c!='\n'))
			{
				c=this->getChar();
				HANDLE_ERROR_POS(c);
			}
		}
		newLine=false;
		//if (index(" \t\r\n().,", c)==NULL)
		if (index(" \t\r\n", c)==NULL)
		{
			this->ungetChar(c);
			break;
		}
		else if (c == '\n')
		{
			newLine=true;
		}
		HANDLE_ERROR_POS(c);
	}

	if (c==EOF)
	{
		token.type=SGSLToken::S_EOF;
		return;
	}

	// push char in word
	bool isInString=false;
	bool isInMot = false;
	while(( c=this->getChar() )!=EOF)
	{
		if ((char)c=='"')
			isInString=!isInString;
		if (isInString)
		{
			if (index("\t\r\n", c)!=NULL)
			{
				if (c == '\n')
					newLine=true;
				this->ungetChar(c);
				break;
			}
		}
		else
		{
			//if (index(" \t\r\n().,", c)!=NULL)
			if (index(" \t\r\n", c)!=NULL)
			{
				if (c == '\n')
					newLine=true;

				this->ungetChar(c);
				break;
			}
			else if (index("().,", c)!=NULL)
			{
				if (isInMot)
					this->ungetChar(c);
				else
				{
					//no need to come back
					HANDLE_ERROR_POS(c);
					word+= (char)c;
				}
				break;
			}
			isInMot=true;
		}
		HANDLE_ERROR_POS(c);
		word+= (char)c;
	}


	if (word.size()>0)
	{
		if ((word[0]>='0') && (word[0]<='9'))
		{
			token.type = SGSLToken::INT;
			token.value = atoi(word.c_str());
		}
		else if (word[0]=='"')
		{
			string::size_type start=word.find_first_of("\"");
			string::size_type end=word.find_last_of("\"");
			if ((start!=string::npos) && (end!=string::npos))
			{
				token.type = SGSLToken::STRING;
				assert(end-start-1>=0);
				token.msg = word.substr(start+1, end-start-1);
			}
			else
				token.type = SGSLToken::NIL;
		}
		else
		{
			// is it a function call ?
			Functions::const_iterator fIt = functions.find(word);
			if (fIt != functions.end())
			{
				token.type = SGSLToken::FUNC_CALL;
				token.msg = word;
				return;
			}
			
			// is it a language ?
			for (int i=0; i<Toolkit::getStringTable()->getNumberOfLanguage(); i++)
			{
				if (word == std::string(Toolkit::getStringTable()->getStringInLang("[language-code]", i)))
				{
					token.type = SGSLToken::LANG;
					token.value = i;
					token.msg = word;
					return;
				}
			}
			
			// so it is another token
			token.type = SGSLToken::getTypeByName(word.c_str());
		}
	}
	else
		token.type = SGSLToken::NIL;
}

bool FileAquisition::open(const std::string filename)
{
	if (fp != NULL)
		fclose(fp);
	if ((fp = fopen(filename.c_str(),"r")) == NULL)
	{
		fprintf(stderr,"SGSL : Can't open file %s\n", filename.c_str());
		return false;
	}
	return true;
}


StringAquisition::StringAquisition(const Functions& functions) :
	Aquisition(functions)
{
	pos=0;
}

StringAquisition::~StringAquisition()
{
	
}

void StringAquisition::open(const std::string& text)
{
	buffer = text;
	pos=0;
}

int StringAquisition::getChar(void)
{
	if (pos < int(buffer.length()))
	{
		return (buffer[pos++]);
	}
	else
		return EOF;
}

int StringAquisition::ungetChar(char c)
{
	if (pos > 0)
	{
		buffer[--pos]=c;
	}
	return 0;
}

// MapScriptSGSL creation

MapScriptSGSL::MapScriptSGSL()
{
	functions["toto"] = std::make_pair(totoDescription, &Story::toto);
	functions["objectiveHidden"] = std::make_pair(objectiveHiddenDescription, &Story::objectiveHidden);
	functions["objectiveVisible"] = std::make_pair(objectiveVisibleDescription, &Story::objectiveVisible);
	functions["objectiveComplete"] = std::make_pair(objectiveCompleteDescription, &Story::objectiveComplete);
	functions["objectiveFailed"] = std::make_pair(objectiveFailedDescription, &Story::objectiveFailed);
	functions["hintHidden"] = std::make_pair(hintHiddenDescription, &Story::hintHidden);
	functions["hintVisible"] = std::make_pair(hintVisibleDescription, &Story::hintVisible);
	functions["hilightItem"] = std::make_pair(hilightItemDescription, &Story::hilightItem);
	functions["unhilightItem"] = std::make_pair(unhilightItemDescription, &Story::unhilightItem);
	functions["hilightUnits"] = std::make_pair(hilightUnitsDescription, &Story::hilightUnits);
	functions["unhilightUnits"] = std::make_pair(unhilightUnitsDescription, &Story::unhilightUnits);
	functions["hilightBuildings"] = std::make_pair(hilightBuildingsDescription, &Story::hilightBuildings);
	functions["unhilightBuildings"] = std::make_pair(unhilightBuildingsDescription, &Story::unhilightBuildings);
	functions["hilightBuildingOnPanel"] = std::make_pair(hilightBuildingOnPanelDescription, &Story::hilightBuildingOnPanel);
	functions["unhilightBuildingOnPanel"] = std::make_pair(unhilightBuildingOnPanelDescription, &Story::unhilightBuildingOnPanel);
	functions["resetAI"] = std::make_pair(resetAIDescription, &Story::resetAI);
}

MapScriptSGSL::~MapScriptSGSL(void)
{
	
}

bool MapScriptSGSL::load(GAGCore::InputStream *stream, Game *game)
{
	stream->readEnterSection("SGSL");
	
	// load source code
	sourceCode = stream->readText("sourceCode");
	
	// compile source code
	ErrorReport er = compileScript(game);
	if (er.type != ErrorReport::ET_OK)
	{
		std::cout << "SGSL : " << er.getErrorString()
				<< " at line " << er.line+1
				<< " on col " << er.col
				<< std::endl;
		stream->readLeaveSection();
		return false;
	}
	
	// load state
	// load main timer
	mainTimer = stream->readSint32("mainTimer");
	
	// load hasWon / hasLost vectors
	stream->readEnterSection("victoryConditions");
	for (unsigned i = 0; i < (unsigned)game->mapHeader.getNumberOfTeams(); i++)
	{
		stream->readEnterSection(i);
		hasWon[i] = stream->readSint32("hasWon") != 0;
		hasLost[i] = stream->readSint32("hasLost") != 0;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	
	// load stories datas
	stream->readEnterSection("stories");
	for (unsigned i = 0; i < stories.size(); i++)
	{
		stream->readEnterSection(i);
		stories[i].lineSelector = stream->readSint32("ProgramCounter");
		stories[i].internTimer = stream->readSint32("internTimer");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	
	// load areas
	stream->readEnterSection("areas");
	unsigned areasCount = stream->readUint32("areasCount");
	for (unsigned i = 0; i < areasCount; i++)
	{
		stream->readEnterSection(i);
		std::string name = stream->readText("name");
		areas[name].x = stream->readSint32("x");
		areas[name].y = stream->readSint32("y");
		areas[name].r = stream->readSint32("r");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	
	// load flags
	stream->readEnterSection("flags");
	unsigned flagsCount = stream->readUint32("flagsCount");
	for (unsigned i = 0; i < flagsCount; i++)
	{
		stream->readEnterSection(i);
		std::string name = stream->readText("name");
		Uint16 gbid = stream->readUint16("gbid");
		Building *b = game->teams[Building::GIDtoTeam(gbid)]->myBuildings[Building::GIDtoID(gbid)];
		assert(b);
		flags[name] = b;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}

void MapScriptSGSL::save(GAGCore::OutputStream *stream, const Game *game)
{
	stream->writeEnterSection("SGSL");
	
	stream->writeText(sourceCode, "sourceCode");
	
	// save state
	
	// save main timer
	stream->writeSint32(mainTimer, "mainTimer");
	
	// save hasWon / hasLost vectors
	stream->writeEnterSection("victoryConditions");
	for (unsigned i = 0; i < (unsigned)game->mapHeader.getNumberOfTeams(); i++)
	{
		stream->writeEnterSection(i);
		stream->writeSint32(hasWon[i] ? 1 : 0, "hasWon");
		stream->writeSint32(hasLost[i] ? 1 : 0, "hasLost");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	
	// save stories datas
	stream->writeEnterSection("stories");
	for (unsigned i = 0; i < stories.size(); i++)
	{
		stream->writeEnterSection(i);
		stream->writeSint32(stories[i].lineSelector, "ProgramCounter");
		stream->writeSint32(stories[i].internTimer, "internTimer");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	
	// save areas
	stream->writeEnterSection("areas");
	stream->writeUint32(areas.size(), "areasCount");
	unsigned i = 0;
	for (AreaMap::iterator it = areas.begin(); it != areas.end(); ++it)
	{
		stream->writeEnterSection(i);
		stream->writeText(it->first, "name");
		stream->writeSint32(it->second.x, "x");
		stream->writeSint32(it->second.y, "y");
		stream->writeSint32(it->second.r, "r");
		stream->writeLeaveSection();
		i++;
	}
	stream->writeLeaveSection();
	
	// save flags
	stream->writeEnterSection("flags");
	stream->writeUint32(flags.size(), "flagsCount");
	i = 0;
	for (BuildingMap::iterator it = flags.begin(); it != flags.end(); ++it)
	{
		stream->writeEnterSection(i);
		stream->writeText(it->first, "name");
		stream->writeUint16(it->second->gid, "x");
		stream->writeLeaveSection();
		i++;
	}
	stream->writeLeaveSection();
	
	stream->writeLeaveSection();
}

void MapScriptSGSL::reset(void)
{
	isTextShown = false;
	mainTimer=0;
	stories.clear();
	areas.clear();
	flags.clear();
}

bool MapScriptSGSL::testMainTimer()
{
	return (mainTimer <= 0);
}

void MapScriptSGSL::syncStep(GameGUI *gui)
{
	if (mainTimer)
		mainTimer--;
	for (std::vector<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
	{
		if (gui->isSpaceSet())
			it->sendSpace();
		it->syncStep(gui);
	}
	if(gui->isSpaceSet())
	{
		gui->setIsSpaceSet(false);
		gui->setSwallowSpaceKey(false);
	}
}

Sint32 MapScriptSGSL::checkSum()
{
	Sint32 cs=0;
	for (std::vector<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
	{
		cs^=it->checkSum();
		cs=(cs<<28)|(cs>>4);
	}
	return cs;
}


ErrorReport MapScriptSGSL::compileScript(Game *game, const char *script)
{
	StringAquisition aquisition(functions);
	aquisition.open(script);
	return parseScript(&aquisition, game);
}

ErrorReport MapScriptSGSL::compileScript(Game *game)
{
	return compileScript(game, sourceCode.c_str());
}

ErrorReport MapScriptSGSL::loadScript(const std::string filename, Game *game)
{
	FileAquisition aquisition(functions);
	if (aquisition.open(filename))
		return parseScript(&aquisition, game);
	else
		return ErrorReport(ErrorReport::ET_NO_SUCH_FILE);
}

// Control of the syntax of the script
ErrorReport MapScriptSGSL::parseScript(Aquisition *donnees, Game *game)
{
	// Gets next token and sets right error position
	#define NEXT_TOKEN \
	{ \
		er.line=donnees->getLine(); \
		er.col=donnees->getCol(); \
		er.pos=donnees->getPos(); \
		donnees->nextToken(); \
	}

	// Check for open parentesis
	#define CHECK_PAROPEN \
	{ \
		NEXT_TOKEN; \
		if (donnees->getToken()->type != SGSLToken::S_PAROPEN) \
		{ \
			er.type=ErrorReport::ET_MISSING_PAROPEN; \
			return er; \
		} \
	}

	// Check for closed parentesis
	#define CHECK_PARCLOSE \
	{ \
		NEXT_TOKEN; \
		if (donnees->getToken()->type != SGSLToken::S_PARCLOSE) \
		{ \
			er.type=ErrorReport::ET_MISSING_PARCLOSE; \
			return er; \
		} \
	}

	// Checks for semicolon
	#define CHECK_SEMICOL \
	{ \
		NEXT_TOKEN; \
		if (donnees->getToken()->type != SGSLToken::S_SEMICOL) \
		{ \
			er.type=ErrorReport::ET_MISSING_SEMICOL; \
			return er; \
		} \
	}

	// Checks for right number of arguments
	#define CHECK_ARGUMENT \
	{ \
		if (donnees->getToken()->type == SGSLToken::S_PARCLOSE || donnees->getToken()->type == SGSLToken::S_SEMICOL) \
		{ \
			er.type=ErrorReport::ET_MISSING_ARGUMENT; \
			return er; \
		} \
	}

	ErrorReport er;
	er.type=ErrorReport::ET_OK;

	reset();

	// Set the size of the won/lost arrays and clear them
	hasWon.resize(game->mapHeader.getNumberOfTeams());
	std::fill(hasWon.begin(), hasWon.end(), false);
	hasLost.resize(game->mapHeader.getNumberOfTeams());
	std::fill(hasLost.begin(), hasLost.end(), false);

	NEXT_TOKEN;
	while (donnees->getToken()->type != SGSLToken::S_EOF)
	{
		Story thisone(this);
		if (er.type != ErrorReport::ET_OK)
		{
			break;
		}
		while ((donnees->getToken()->type != SGSLToken::S_STORY) && (donnees->getToken()->type !=SGSLToken::S_EOF))
		{
			if (er.type != ErrorReport::ET_OK)
			{
				break;
			}
			
			// Grammar check
			switch (donnees->getToken()->type)
			{
				// function call
				case (SGSLToken::FUNC_CALL):
				{
					thisone.line.push_back(*donnees->getToken());
					
					Functions::const_iterator fIt = functions.find(donnees->getToken()->msg);
					assert(fIt != functions.end());
					const FunctionArgumentDescription *argument = fIt->second.first;
					
					CHECK_PAROPEN;
					NEXT_TOKEN; 
					
					while (true)
					{
						CHECK_ARGUMENT;
						
						int argumentTokenType = donnees->getToken()->type;
						if ((argumentTokenType < argument->argRangeFirst) || (argumentTokenType > argument->argRangeLast))
						{
							er.type=ErrorReport::ET_WRONG_FUNCTION_ARGUMENT;
							return er;
						}
						
						thisone.line.push_back(*donnees->getToken());
						
						argument++;
						if (argument->argRangeFirst<0)
							break;
						
						CHECK_SEMICOL;
						NEXT_TOKEN;
					}
					
					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;
				
				// summonUnits(flag_name , globules_amount , globule_type , globule_level , team_int)
				case (SGSLToken::S_SUMMONUNITS):
				{
					//<-summon
					thisone.line.push_back(*donnees->getToken());

					CHECK_PAROPEN;
					NEXT_TOKEN; // <- flag_name
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::STRING)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					
					std::string areaName=donnees->getToken()->msg;
					int areaN=-1;
					//Check if there is a script area in the map with the same name
					for(int n=0; n<9; ++n)
					{
						if(game->map.getAreaName(n)==areaName)
						{
							areaN=n;
							break;
						}
					}
					
					if (areaN == -1 && areas.find(areaName) == areas.end())
					{
						er.type=ErrorReport::ET_UNDEFINED_AREA_NAME;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; //<- globules_amount
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (donnees->getToken()->value > 30) //Max number of globules to summon
					{
						er.type=ErrorReport::ET_INVALID_VALUE;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; //<- globules type
					CHECK_ARGUMENT;
					if ((donnees->getToken()->type != SGSLToken::S_WARRIOR) && (donnees->getToken()->type != SGSLToken::S_WORKER) && (donnees->getToken()->type != SGSLToken::S_EXPLORER))
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; //<- globules level
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (donnees->getToken()->value > 3)
					{
						er.type=ErrorReport::ET_INVALID_VALUE;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; //<- team
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (donnees->getToken()->value >= game->mapHeader.getNumberOfTeams())
					{
						er.type=ErrorReport::ET_INVALID_TEAM;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;

				// setArea(string name, int x , int y , int r)
				case (SGSLToken::S_SETAREA):
				{
					Area area;

					std::cerr << "SGSL : Use of setArea is deprecated. Use newer script areas in the map editor instead." << std::endl;

					thisone.line.push_back(*donnees->getToken());

					CHECK_PAROPEN;
					NEXT_TOKEN; // <- string name
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::STRING)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (areas.find(donnees->getToken()->msg) != areas.end())
					{
						er.type=ErrorReport::ET_DUPLICATED_AREA_NAME;
						break;
					}
					std::string name=donnees->getToken()->msg;
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; // <- int x
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					area.x=donnees->getToken()->value;
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; //<- int y
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					area.y=donnees->getToken()->value;
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; // <- int r
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					if (donnees->getToken()->value == 0)
					{
						er.type=ErrorReport::ET_INVALID_VALUE;
						break;
					}
					area.r=donnees->getToken()->value;
					thisone.line.push_back(*donnees->getToken());

					areas[name] = area;

					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;

				// summonFlag(string name, int x, int y, int r, int unitcount, int team)
				case (SGSLToken::S_SUMMONFLAG):
				{
					thisone.line.push_back(*donnees->getToken());

					CHECK_PAROPEN;
					NEXT_TOKEN; // <- string name
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::STRING)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; // <- int x
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; // <- int y
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; // <- int r
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; // <- int unitcount
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_SEMICOL;
					NEXT_TOKEN; // <- int team
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (donnees->getToken()->value >= game->mapHeader.getNumberOfTeams())
					{
						er.type=ErrorReport::ET_INVALID_TEAM;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;

				// destroyFlag(string name)
				case (SGSLToken::S_DESTROYFLAG):
				{
					thisone.line.push_back(*donnees->getToken());

					CHECK_PAROPEN;
					NEXT_TOKEN; // <- string name
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::STRING)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;

				// Alliance
				case (SGSLToken::S_ALLIANCE):
				{
					thisone.line.push_back(*donnees->getToken()); //<-SETAREA

					// team 1
					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					if (donnees->getToken()->value >= game->mapHeader.getNumberOfTeams())
					{
						er.type=ErrorReport::ET_INVALID_TEAM;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					// team 2
					CHECK_SEMICOL;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					if (donnees->getToken()->value >= game->mapHeader.getNumberOfTeams())
					{
						er.type=ErrorReport::ET_INVALID_TEAM;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					// level
					CHECK_SEMICOL;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					if (donnees->getToken()->value > 3)
					{
						er.type=ErrorReport::ET_INVALID_ALLIANCE_LEVEL;
						break;
					}
					thisone.line.push_back(*donnees->getToken());
					
					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;

				// Show, Label, Jump
				case (SGSLToken::S_SHOW):
				case (SGSLToken::S_LABEL):
				case (SGSLToken::S_JUMP):
				{
					SGSLToken::TokenType type = donnees->getToken()->type;

					thisone.line.push_back(*donnees->getToken());
					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::STRING)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}

					if (type == SGSLToken::S_LABEL)
					{
						// add label to table
						thisone.labels[donnees->getToken()->msg] = thisone.line.size();
					}
					if (type == SGSLToken::S_JUMP)
					{
						// complain if label doesn't exists
						if (thisone.labels.find(donnees->getToken()->msg) == thisone.labels.end())
						{
							er.type=ErrorReport::ET_UNDEFINED_LABEL;
							break;
						}
					}

					if (type == SGSLToken::S_SHOW)
					{
						thisone.line.push_back(*donnees->getToken());
						NEXT_TOKEN;
						if (donnees->getToken()->type != SGSLToken::S_PARCLOSE)
						{
							// This is a multilingual show
							if (donnees->getToken()->type != SGSLToken::S_SEMICOL)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							NEXT_TOKEN;
							CHECK_ARGUMENT;
							if (donnees->getToken()->type != SGSLToken::LANG)
							{
								er.type=ErrorReport::ET_NOT_VALID_LANG_ID;
								break;
							}
							thisone.line.push_back(*donnees->getToken());
							CHECK_PARCLOSE;
							NEXT_TOKEN;
						}
						else
						{
							NEXT_TOKEN;
						}
					}
					else
					{
						thisone.line.push_back(*donnees->getToken());
						CHECK_PARCLOSE;
						NEXT_TOKEN;
					}
				}
				break;

				// Wait | wait ( int) or wait ( isdead( teamNumber ) ) or wait( [only] condition ) or wait( not( [atmin] condition ) )
				case (SGSLToken::S_WAIT):
				{
					bool enter = false;
					bool negate = false;
					thisone.line.push_back(*donnees->getToken());
					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					// int
					if (donnees->getToken()->type == SGSLToken::INT  && !enter)
					{
						enter = true;
						if (donnees->getToken()->value <=0)
						{
							er.type=ErrorReport::ET_INVALID_VALUE;
							break;
						}
						thisone.line.push_back(*donnees->getToken());
					}

					// isdead( teamNumber )
					if (donnees->getToken()->type == SGSLToken::S_ISDEAD && !enter)
					{
						enter = true;
						thisone.line.push_back(*donnees->getToken());
						CHECK_PAROPEN;
						NEXT_TOKEN;
						CHECK_ARGUMENT;
						if (donnees->getToken()->type != SGSLToken::INT)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						else if (donnees->getToken()->value >= game->mapHeader.getNumberOfTeams())
						{
							er.type=ErrorReport::ET_INVALID_TEAM;
							break;
						}
						thisone.line.push_back(*donnees->getToken());
						CHECK_PARCLOSE;
					}
					// following two arguments can be negated !
					if (donnees->getToken()->type == SGSLToken::S_NOT && !enter)
					{
						negate = true;
						thisone.line.push_back(*donnees->getToken());
						CHECK_PAROPEN;
						NEXT_TOKEN;
						CHECK_ARGUMENT;
					}
					// area ("areaname" , who*)
					if (donnees->getToken()->type == SGSLToken::S_AREA && !enter)
					{
						enter = true;
						thisone.line.push_back(*donnees->getToken());

						CHECK_PAROPEN;
						NEXT_TOKEN;
						CHECK_ARGUMENT;
						if (donnees->getToken()->type != SGSLToken::STRING)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}

						std::string areaName=donnees->getToken()->msg;
						int areaN=-1;
						for(int n=0; n<9; ++n)
						{
							if(game->map.getAreaName(n)==areaName)
							{
								areaN=n;
								break;
							}
						}
						if (areaN==-1 && areas.find(areaName) == areas.end())
						{
							er.type=ErrorReport::ET_UNDEFINED_AREA_NAME;
							break;
						}
						thisone.line.push_back(*donnees->getToken());

						CHECK_SEMICOL;
						NEXT_TOKEN;
						CHECK_ARGUMENT;
						thisone.line.push_back(*donnees->getToken());
						if (donnees->getToken()->type != SGSLToken::INT)
						{
							if ((donnees->getToken()->type != SGSLToken::S_ENEMY)
								&& (donnees->getToken()->type != SGSLToken::S_ALLY))
								{
									er.type=ErrorReport::ET_SYNTAX_ERROR;
									break;
								}
							else
							{
								// we have enemy or ally
								CHECK_PAROPEN;
								NEXT_TOKEN;
								if (donnees->getToken()->type != SGSLToken::INT)
								{
									er.type=ErrorReport::ET_SYNTAX_ERROR;
									break;
								}
								thisone.line.push_back(*donnees->getToken());
								CHECK_PARCLOSE;
							}
						}
						CHECK_PARCLOSE;
					}
					
					// only
					bool only = false;
					if (donnees->getToken()->type == SGSLToken::S_ONLY && !enter)
					{
						only = true;
						thisone.line.push_back(*donnees->getToken());
						NEXT_TOKEN;
					}
					
					//Comparaison| ( Variable( team , "flagName" ) cond value ) : variable = unit
					//( Variable( level , team , "flagName" ) cond value ) : variable = building
					//"flagName" can be omitted !
					if ((donnees->getToken()->type >= SGSLToken::S_WORKER) && (donnees->getToken()->type <= SGSLToken::S_MARKET_B) && !enter)
					{
						enter = true;
						
						thisone.line.push_back(*donnees->getToken());
						if (donnees->getToken()->type >= SGSLToken::S_SWARM_B)
						{
							// Buildings
							CHECK_PAROPEN;
							NEXT_TOKEN;
							CHECK_ARGUMENT;
							// team
							if (donnees->getToken()->type != SGSLToken::INT)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							else if (donnees->getToken()->value >= game->mapHeader.getNumberOfTeams())
							{
								er.type=ErrorReport::ET_INVALID_TEAM;
								break;
							}
							thisone.line.push_back(*donnees->getToken());
							CHECK_SEMICOL;
							NEXT_TOKEN;
							CHECK_ARGUMENT;

							// level
							if (donnees->getToken()->type != SGSLToken::INT)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							else if ((donnees->getToken()->value < 0) || (donnees->getToken()->value > 5))
							{
								er.type=ErrorReport::ET_INVALID_VALUE;
								break;
							}
							thisone.line.push_back(*donnees->getToken());
						}
						else
						{
							// only is invalid for units
							if (only)
							{
								er.type=ErrorReport::ET_INVALID_ONLY;
								break;
							}
							
							// Units
							CHECK_PAROPEN;
							NEXT_TOKEN;
							CHECK_ARGUMENT;
							// team
							if (donnees->getToken()->type != SGSLToken::INT)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							else if (donnees->getToken()->value >= game->mapHeader.getNumberOfTeams())
							{
								er.type=ErrorReport::ET_INVALID_TEAM;
								break;
							}
							thisone.line.push_back(*donnees->getToken());
						}
						CHECK_PARCLOSE;

						/*NEXT_TOKEN;
						//Optional "areaName"
						if (donnees->getToken()->type != SGSLToken::S_PARCLOSE)
						{
							NEXT_TOKEN;
							//there is a areaName
							if (donnees->getToken()->type != SGSLToken::STRING)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							else if (areas.find(donnees->getToken()->msg) == areas.end())
							{
								er.type=ErrorReport::ET_UNDEFINED_AREA_NAME;
								break;
							}
							thisone.line.push_back(*donnees->getToken());
							CHECK_PARCLOSE;
						}
						else
						{ // there was no flag name but a closing parenthesis
						}*/

						NEXT_TOKEN;
						CHECK_ARGUMENT;
						if ((donnees->getToken()->type < SGSLToken::S_EQUAL) || (donnees->getToken()->type > SGSLToken::S_LOWER))
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						thisone.line.push_back(*donnees->getToken());
						NEXT_TOKEN;
						CHECK_ARGUMENT;
						if (donnees->getToken()->type != SGSLToken::INT)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						thisone.line.push_back(*donnees->getToken());
					}
					if (!enter)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					if (negate)
					{
						negate = !negate;
						CHECK_PARCLOSE;
					}
					CHECK_PARCLOSE;
					NEXT_TOKEN;
					break;
				}
				break;

				case (SGSLToken::NIL):
				{
					//NEXT_TOKEN;
					er.type=ErrorReport::ET_UNKNOWN;
				}
				break;

				// Grammar of Timer( int )
				case (SGSLToken::S_TIMER):
				{
					thisone.line.push_back(*donnees->getToken());

					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;

				// Enable or disable a GUI element
				case (SGSLToken::S_GUIENABLE):
				case (SGSLToken::S_GUIDISABLE):
				{
					thisone.line.push_back(*donnees->getToken());

					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if ((donnees->getToken()->type < SGSLToken::S_WORKER) || (donnees->getToken()->type > SGSLToken::S_ALLIANCESCREEN))
					{
						er.type=ErrorReport::ET_INVALID_VALUE;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;

				case (SGSLToken::S_LOOSE):
				case (SGSLToken::S_WIN):
				{
					thisone.line.push_back(*donnees->getToken());

					CHECK_PAROPEN;
					NEXT_TOKEN;
					CHECK_ARGUMENT;
					if (donnees->getToken()->type != SGSLToken::INT)
					{
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
					}
					else if (donnees->getToken()->value >= game->mapHeader.getNumberOfTeams())
					{
						er.type=ErrorReport::ET_INVALID_TEAM;
						break;
					}
					thisone.line.push_back(*donnees->getToken());

					CHECK_PARCLOSE;
					NEXT_TOKEN;
				}
				break;

				case (SGSLToken::S_HIDE):
				{
					thisone.line.push_back(*donnees->getToken());
					NEXT_TOKEN;
				}
				break;

				case (SGSLToken::S_SPACE):
				{
					thisone.line.push_back(*donnees->getToken());
					NEXT_TOKEN;
				}
				break;

				case (SGSLToken::S_EOF):
				{
				}
				break;

				default:
				{
					cerr << "SGSL: unknown token found: " << donnees->getToken()->type << endl;
					er.type=ErrorReport::ET_UNKNOWN;
				}
				break;
			}
		}
		thisone.line.push_back(SGSLToken(SGSLToken::S_STORY));
		stories.push_back(thisone);
		// Debug code
		/*printf("SGSL : story loaded, %d tokens, dumping now :\n", (int)thisone.line.size());
		for (unsigned  i=0; i<thisone.line.size(); i++)
			cout << "SGSLToken type " << SGSLToken::getNameByType(thisone.line[i].type) << endl;*/
		NEXT_TOKEN;
	}
	return er;
}

bool MapScriptSGSL::hasTeamWon(unsigned teamNumber)
{
	// Seb: Cheapo hack. Script should intialize hasWon first :-)
	if (testMainTimer() && hasWon.size()>teamNumber)
	{
		return hasWon.at(teamNumber);
	}
	return false;
}

bool MapScriptSGSL::hasTeamLost(unsigned teamNumber)
{
	// Seb: Cheapo hack. Script should intialize hasLost first :-)
	if(hasLost.size()>teamNumber)
		return hasLost.at(teamNumber);
	return false;
}



void MapScriptSGSL::addTeam()
{
	hasWon.push_back(false);
	hasLost.push_back(false);
}



void MapScriptSGSL::removeTeam(int n)
{
	hasWon.erase(hasWon.begin()+n);
	hasLost.erase(hasLost.begin()+n);
}

