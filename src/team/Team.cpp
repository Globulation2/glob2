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

// std::min/std::clamp in the teams editor bind this constant by reference.
const int Team::MAX_COUNT;

Uint32 Team::attackableTeams() const
{
	return game->gameHeader.isPeacefulModeEnabled() ? 0 : enemies;
}

Team::Team(Game *game)
:BaseTeam()
{
	assert(game);
	this->game=game;
	this->map=&game->map;
	race.setCatalog(game->gameHeader.getUnitCatalog());
	init();
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
	liveUnits.clear();
	liveBuildings.clear();

	startPosX=startPosY=0;
	startPosSet=START_POS_UNSET;

	isAlive=true;
	hasWon=false;
	hasLost=false;
	winCondition = WCUnknown;
	prestige=0;
	unitConversionLost = 0;
	unitConversionGained = 0;
	for(int i=0; i<MaterialSlotCount; ++i)
		teamMaterials[i]=0;

	for(int i=0; i<GESize; ++i)
		eventCooldownTimers[i]=0;

	noMoreBuildingSitesCountdown=0;
}




void Team::attachUnit(int id) { attachUnit(id, myUnits[id]); }
void Team::attachUnit(int id, Unit* unit)
{
	assert(id>=0 && id<Unit::MAX_COUNT && unit && unit->owner==this);
	assert(Unit::GIDtoID(unit->gid)==id && Unit::GIDtoTeam(unit->gid)==teamNumber);
	assert(!myUnits[id] || myUnits[id]==unit);
	liveUnits.append(*unit);
	myUnits[id]=unit;
    if(unit->hasCapability(UnitRuntimeTraits::GuardIdle))map->gradientGuardCostsChanged(teamNumber);
	if (taskSwapNext==0xffff) taskSwapNext=id;
}
Unit* Team::detachUnit(int id)
{
	assert(id>=0 && id<Unit::MAX_COUNT && myUnits[id]);
	Unit* unit=myUnits[id];
	assert(unit->owner==this);
	Unit* next=unit->populationNext;
	liveUnits.remove(*unit);
    if(unit->hasCapability(UnitRuntimeTraits::GuardIdle))map->gradientGuardCostsChanged(teamNumber);
	myUnits[id]=nullptr;
	if (taskSwapNext==id) taskSwapNext=next ? Unit::GIDtoID(next->gid)
		: liveUnits.empty() ? 0xffff : Unit::GIDtoID(liveUnits.front()->gid);
	return unit;
}
void Team::attachBuilding(int id) { attachBuilding(id, myBuildings[id]); }
void Team::attachBuilding(int id, Building* building)
{
	assert(id>=0 && id<Building::MAX_COUNT && building && building->owner==this);
	assert(Building::GIDtoID(building->gid)==id && Building::GIDtoTeam(building->gid)==teamNumber);
	assert(!myBuildings[id] || myBuildings[id]==building);
	liveBuildings.append(*building);
	myBuildings[id]=building;
}
Building* Team::detachBuilding(int id)
{
	assert(id>=0 && id<Building::MAX_COUNT && myBuildings[id]);
	Building* building=myBuildings[id];
	assert(building->owner==this);
	liveBuildings.remove(*building);
	myBuildings[id]=nullptr;
	return building;
}

bool Team::integrity(void)
{
	checkInvariant(noMoreBuildingSitesCountdown<=noMoreBuildingSitesCountdownMax);
	// Explicit diagnostic only: normal ticks never sweep ID capacity.
	checkInvariant(liveUnits.matches(myUnits,Unit::MAX_COUNT));
	checkInvariant(liveBuildings.matches(myBuildings,Building::MAX_COUNT));
	for (Building* building : liveBuildings) checkInvariant(building->integrity());
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
		checkInvariant((*it)->runtime->attractsRole(0));
		checkInvariant(myBuildings[Building::GIDtoID((*it)->gid)]);
	}

	for (Unit* unit : liveUnits) checkInvariant(unit->integrity());
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
	for (Building* building : liveBuildings) building->update();
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
