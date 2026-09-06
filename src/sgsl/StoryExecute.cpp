// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2008 Stephane Magnenat
// Copyright (C) 2001-2008 Luc-Olivier de Charrière
// Copyright (C) 2001-2008 Martin S. Nyffenegger

/*!	\file StoryExecute.cpp
	\brief SGSL story stepping: runs the statements of one Story against the game
*/

#include <iostream>
#include <optional>
#include <string>

#include "Building.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "SGSL.h"
#include "Utilities.h"

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

void Story::setAlliance(Game *game)
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
		game->teams[team1]->allies |= 1<<team2;
	else
		game->teams[team1]->allies &= ~(1<<team2);

	if (enemies[level])
		game->teams[team1]->enemies |= 1<<team2;
	else
		game->teams[team1]->enemies &= ~(1<<team2);

	if (sharedVisionExchange[level])
		game->teams[team1]->sharedVisionExchange |= 1<<team2;
	else
		game->teams[team1]->sharedVisionExchange &= ~(1<<team2);

	if (sharedVisionFood[level])
		game->teams[team1]->sharedVisionFood |= 1<<team2;
	else
		game->teams[team1]->sharedVisionFood &= ~(1<<team2);

	if (sharedVisionOther[level])
		game->teams[team1]->sharedVisionOther |= 1<<team2;
	else
		game->teams[team1]->sharedVisionOther &= ~(1<<team2);
}

void Story::summonUnits(Game *game)
{
	const std::string& areaName = line[++lineSelector].msg;
	int globulesAmount = line[++lineSelector].value;
	int type = line[++lineSelector].type - SGSLToken::S_WORKER;
	int level = line[++lineSelector].value;
	int team = line[++lineSelector].value;

	const std::optional<int> areaN = mapAreaNumber(game, areaName);
	//There isn't a map script area with the same name, try the old map scripts
	if(!areaN)
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
				if(game->map.isPointSet(*areaN, x, y))
				{
					if (game->addUnit(x, y, team, type, level, 0, 0, 0))
					{
						number --;
					}
				}
			}
		}
	}
}

void Story::summonFlag(Game *game)
{
	const std::string& flagName = line[++lineSelector].msg;
	int x = line[++lineSelector].value;
	int y = line[++lineSelector].value;
	int r = line[++lineSelector].value;
	int unitCount = line[++lineSelector].value;
	int team = line[++lineSelector].value;

	int typeNum = globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);

	Building *b = game->addBuilding(x, y, typeNum, team);

	// addBuilding returns NULL when the team already holds
	// Building::MAX_COUNT buildings (no free slot). Skip the flag
	// rather than dereferencing NULL; report it like destroyFlag
	// does for its own error case.
	if (b)
	{
		b->unitStayRange = r;
		b->maxUnitWorking = unitCount;
		b->maxUnitWorkingPreferred = unitCount;
		b->update();

		mapscript->flags[flagName] = b;
	}
	else
	{
		std::cerr << "SGSL : Could not summon flag " << flagName << " : team " << team << " building limit reached !" << std::endl;
	}
}

void Story::destroyFlag()
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
}

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
				setAlliance(game);
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
				SGSLToken::TokenType object = line[++lineSelector].type;
				setGUIChoice(gui, object, true);
				return true;
			}

			case (SGSLToken::S_GUIDISABLE):
			{
				SGSLToken::TokenType object = line[++lineSelector].type;
				setGUIChoice(gui, object, false);
				return true;
			}

			case (SGSLToken::S_SUMMONUNITS):
			{
				summonUnits(game);
				return true;
			}

			case SGSLToken::S_SUMMONFLAG:
			{
				summonFlag(game);
				return true;
			}

			case SGSLToken::S_DESTROYFLAG:
			{
				destroyFlag();
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
				return waitConditionMet(game);
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
