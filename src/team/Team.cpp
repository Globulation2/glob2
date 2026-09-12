// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière


#include "BuildingType.h"
#include "EngineTiming.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"
#include "Player.h"
#include "Integrity.h"
#include <stdexcept>

Team::Team(Game *game)
:BaseTeam()
{
	assert(game);
	this->game=game;
	this->map=&game->map;
	init();
}




Team::Team(GAGCore::InputStream *stream, Game *game, Sint32 versionMinor)
:Team(game)
{
	if (!load(stream, &(globalContainer->buildingsTypes), versionMinor))
		throw std::runtime_error("Failed to load team");
}




Team::~Team()
{
	if (!disableRecursiveDestruction)
	{
		clearMem();
		delete [] myUnits;
		delete [] myBuildings;
	}
}




void Team::init(void)
{
	myUnits = new Unit*[Unit::MAX_COUNT];
	myBuildings = new Building*[Building::MAX_COUNT];
	for (int i=0; i<Unit::MAX_COUNT; i++)
		myUnits[i]=NULL;

	for (int i=0; i<Building::MAX_COUNT; i++)
		myBuildings[i]=NULL;

	for (int r=0; r<MAX_NB_RESOURCES; r++)
		for (int s=0; s<SWIM_CLASS_COUNT; s++)
			fetchersGoing[r][s]=0;

	startPosX=startPosY=0;
	startPosSet=START_POS_UNSET;

	isAlive=true;
	hasWon=false;
	hasLost=false;
	winCondition = WCUnknown;
	prestige=0;
	unitConversionLost = 0;
	unitConversionGained = 0;
	for(int i=0; i<MAX_NB_RESOURCES; ++i)
		teamResources[i]=0;

	for(int i=0; i<GESize; ++i)
		eventCooldownTimers[i]=0;

	noMoreBuildingSitesCountdown=0;
}




bool Team::integrity(void)
{
	checkInvariant(noMoreBuildingSitesCountdown<=noMoreBuildingSitesCountdownMax);
	for (int id=0; id<Building::MAX_COUNT; id++)
	{
		Building *b=myBuildings[id];
		if (b)
			checkInvariant(b->integrity());
	}
	for (std::list<Building *>::iterator it=virtualBuildings.begin(); it!=virtualBuildings.end(); ++it)
	{
		checkInvariant(*it);
		checkInvariant((*it)->type);
		checkInvariant((*it)->type->isVirtual);
		checkInvariant(myBuildings[Building::GIDtoID((*it)->gid)]);
	}
	for (std::list<Building *>::iterator it=clearingFlags.begin(); it!=clearingFlags.end(); ++it)
	{
		checkInvariant(*it);
		checkInvariant((*it)->type);
		checkInvariant((*it)->type->isVirtual);
		checkInvariant(myBuildings[Building::GIDtoID((*it)->gid)]);
	}

	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=myUnits[i];
		if (u)
			checkInvariant(u->integrity());
	}
	return true;
}




void Team::setCorrectMasks(void)
{
	me=teamNumberToMask(teamNumber);
	allies=me;
	enemies=~allies;
	sharedVisionExchange=me;
	sharedVisionFood=me;
	sharedVisionOther=me;
}




void Team::setCorrectColor(const GAGCore::Color& color)
{
	this->color = color;
}

void Team::setCorrectColor(float value)
{
	float r, g, b;
	Utilities::HSVtoRGB(&r, &g, &b, value, TEAM_COLOR_SATURATION, TEAM_COLOR_VALUE);
	color = Color((Uint8)(COLOR_CHANNEL_MAX*r), (Uint8)(COLOR_CHANNEL_MAX*g), (Uint8)(COLOR_CHANNEL_MAX*b));
}




void Team::update()
{
	for (int i=0; i<Building::MAX_COUNT; i++)
		if (myBuildings[i])
			myBuildings[i]->update();
}




bool Team::openMarket()
{
	int numberOfTeam=game->mapHeader.getNumberOfTeams();
	for (int ti=0; ti<numberOfTeam; ti++)
		if (ti!=teamNumber && (game->teams[ti]->sharedVisionExchange & me))
			return true;
	return false;
}




void Team::pushGameEvent(GameEvent event)
{
	///Ignore events when the cooldown is above 0
	GameEventType eventType = event.getEventType();
	if(eventCooldownTimers[eventType] == 0)
	{
		events.push(std::move(event));
		eventCooldownTimers[eventType] = GAME_EVENT_COOLDOWN_TICKS;
	}
}



std::optional<GameEvent> Team::getEvent()
{
	if(events.empty())
		return std::nullopt;

	GameEvent event = std::move(events.front());
	events.pop();
	return event;
}



void Team::updateEvents()
{
	for(int i=0; i<GESize; ++i)
	{
		if(eventCooldownTimers[i]>0)
			eventCooldownTimers[i]-=1;
	}


	while(!events.empty())
	{
		const GameEvent& event = events.front();
		if((game->stepCounter - event.getStep()) > GAME_EVENT_MAX_AGE_TICKS)
		{
			events.pop();
		}
		else
		{
			break;
		}
	}
}


bool Team::wasRecentEvent(GameEventType type)
{
	// NOTE: structurally coupled to pushGameEvent — strict-equal returns true
	// only on the exact tick the event fired (updateEvents decrements next).
	// See bug #8 in the magic-number glossary.
	return eventCooldownTimers[type]==GAME_EVENT_COOLDOWN_TICKS;
}




std::string Team::getFirstPlayerName(void) const
{
	for (int i=0; i<game->gameHeader.getNumberOfPlayers(); i++)
	{
		if (game->players[i]->team == this)
			return game->players[i]->name;
	}
	return {};
}



void Team::checkWinConditions()
{
	std::list<std::shared_ptr<WinningCondition> >& conditions = game->gameHeader.getWinningConditions();
	for(std::list<std::shared_ptr<WinningCondition> >::iterator i = conditions.begin(); i!=conditions.end(); ++i)
	{
		if((*i)->hasTeamWon(teamNumber, game))
		{
			hasWon=true;
			hasLost=false;
			winCondition = (*i)->getType();
			break;
		}
		else if((*i)->hasTeamLost(teamNumber, game))
		{
			hasWon=false;
			hasLost=true;
			winCondition = (*i)->getType();
			break;
		}
		else
		{
			hasWon=false;
			hasLost=false;
			winCondition = WCUnknown;
		}
	}
}
