// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière


#include "AICastor.h"

#include <assert.h>
#include <string.h>

#include <algorithm>


#include "BuildingType.h"
#include "DatasetWriter.h"
#include "Game.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "Utilities.h"
#include "GameGUI.h"
#include "SDLCompat.h"


#include "Brush.h"
#include "FertilityCalculatorDialog.h"


#define BULLET_IMGID 0

// Editor utilities and script-interface queries. Split out of Game.cpp.

// Script interface

int Game::isTeamAlive(int team)
{
	if (
		(team >= 0) && (team < mapHeader.getNumberOfTeams())
	)
		return teams[team]->isAlive;
	else
		return false;
}

int Game::unitsCount(int team, int type)
{
	if (
		(team >= 0) && (team < mapHeader.getNumberOfTeams()) &&
		(type >= 0) && (type < NB_UNIT_TYPE)
	)
		return teams[team]->stats.getLatestStat()->numberUnitPerType[type];
	else
		return 0;
}

int Game::unitsUpgradesCount(int team, int type, int ability, int level)
{
	if (
		(team >= 0) && (team < mapHeader.getNumberOfTeams()) &&
		(type >= 0) && (type < NB_UNIT_TYPE) &&
		(ability >= 0) && (ability < NB_ABILITY) &&
		(level >= 0) && (level < NB_UNIT_LEVELS)
	)
		return teams[team]->stats.getLatestStat()->upgradeStatePerType[type][ability][level];
	else
		return 0;
}

int Game::buildingsCount(int team, int type, int level)
{
	if (
		(team >= 0) && (team < mapHeader.getNumberOfTeams()) &&
		(type >= 0) && (type < IntBuildingType::NB_BUILDING) &&
		(level >= 0) && (level < MAX_BUILDING_LEVELS)
	)
		return teams[team]->stats.getLatestStat()->numberBuildingPerTypePerLevel[type][level];
	else
		return 0;
}


void Game::addTeam(int pos)
{
	assert(mapHeader.getNumberOfTeams()<Team::MAX_COUNT);
	if(pos==TEAM_POS_END)
		pos=mapHeader.getNumberOfTeams();
	teams[pos]=new Team(this);
	teams[pos]->teamNumber=mapHeader.getNumberOfTeams();
	teams[pos]->race.load();
	teams[pos]->setCorrectMasks();

	pos=mapHeader.getNumberOfTeams();
	pos+=1;
	mapHeader.setNumberOfTeams(pos);
	for (int i=0; i<pos; i++)
		teams[i]->setCorrectColor( ((float)i*TEAM_COLOR_HUE_DEGREES) /(float)pos );

	prestigeToReach = std::max(MIN_MAX_PRESTIGE, pos*TEAM_MAX_PRESTIGE);

	map.addTeam();

	sgslScript.addTeam();
}

void Game::removeTeam(int pos)
{
	if(pos==TEAM_POS_END)
	{
		pos=mapHeader.getNumberOfTeams();
		pos-=1;
		mapHeader.setNumberOfTeams(pos);
	}
	if (mapHeader.getNumberOfTeams()>0)
	{
		Team *team=teams[pos];

		team->clearMap();

		delete team;
		assert (mapHeader.getNumberOfTeams()!=0);
		for (int i=0; i<mapHeader.getNumberOfTeams(); ++i)
			teams[i]->setCorrectColor(((float)i*TEAM_COLOR_HUE_DEGREES)/(float)mapHeader.getNumberOfTeams());

		map.removeTeam();
		sgslScript.removeTeam(pos);
		teams[pos]=NULL;
	}
}

void Game::clearingUncontrolledTeams(void)
{
	for (int ti=0; ti<mapHeader.getNumberOfTeams(); ti++)
	{
		Team *team=teams[ti];
		if (team->playersMask==0)
		{
			team->clearMap();
			team->clearLists();
			team->clearMem();
		}
	}
}

void Game::regenerateDiscoveryMap(void)
{
	map.unsetMapDiscovered();
	for (int t=0; t<mapHeader.getNumberOfTeams(); t++)
	{
		for (int i=0; i<Unit::MAX_COUNT; i++)
		{
			Unit *u=teams[t]->myUnits[i];
			if (u)
			{
				map.setMapDiscovered(u->posX-1, u->posY-1, 3, 3, teams[t]->sharedVisionOther);
			}
		}
		for (int i=0; i<Building::MAX_COUNT; i++)
		{
			Building *b=teams[t]->myBuildings[i];
			if (b)
			{
				b->setMapDiscovered();
			}
		}
	}
}

Unit *Game::addUnit(int x, int y, int team, Sint32 typeNum, int level, int delta, int dx, int dy)
{
	assert(team<mapHeader.getNumberOfTeams());

	UnitType *ut=teams[team]->race.getUnitType(typeNum, level);

	x = (x + map.getW()) % map.getW();
	y = (y + map.getH()) % map.getH();

	bool fly=ut->performance[FLY];
	bool free;
	if (fly)
		free=map.isFreeForAirUnit(x, y);
	else
		free=map.isFreeForGroundUnit(x, y, ut->performance[SWIM], Team::teamNumberToMask(team));
	if (!free)
		return NULL;

	int id=SLOT_INDEX_NONE;
	for (int i=0; i<Unit::MAX_COUNT; i++)//we search for a free place for a unit.
		if (teams[team]->myUnits[i]==NULL)
		{
			id=i;
			break;
		}
	if (id==SLOT_INDEX_NONE)
		return NULL;

	//ok, now we can safely deposite an unit.
	int gid=Unit::GIDfrom(id, team);
	if (fly)
		map.setAirUnit(x, y, gid);
	else
		map.setGroundUnit(x, y, gid);

	teams[team]->myUnits[id]= new Unit(x, y, gid, typeNum, teams[team], level);
	teams[team]->myUnits[id]->dx=dx;
	teams[team]->myUnits[id]->dy=dy;
	teams[team]->myUnits[id]->directionFromDxDy();
	teams[team]->myUnits[id]->delta=delta;
	teams[team]->myUnits[id]->selectPreferredMovement();
	return teams[team]->myUnits[id];
}

Building *Game::addBuilding(int x, int y, int typeNum, int teamNumber, Sint32 unitWorking, Sint32 unitWorkingFuture)
{
	Team *team=teams[teamNumber];
	assert(team);

	int id=SLOT_INDEX_NONE;
	for (int i=0; i<Building::MAX_COUNT; i++)//we search for a free place for a building.
		if (team->myBuildings[i]==NULL)
		{
			id=i;
			break;
		}
	if (id==SLOT_INDEX_NONE)
	{
		//TODO:Building limit reached!
		return NULL;
	}

	//ok, now we can safely deposite an building.
	int gid=Building::GIDfrom(id, teamNumber);

	int w=globalContainer->buildingsTypes.get(typeNum)->width;
	int h=globalContainer->buildingsTypes.get(typeNum)->height;

	Building *b=new Building(x&map.getMaskW(), y&map.getMaskH(), gid, typeNum, team, &globalContainer->buildingsTypes, unitWorking, unitWorkingFuture);

	if (b->type->canExchange)
		team->canExchange.push_front(b);
	if (b->type->isVirtual)
		team->virtualBuildings.push_front(b);
	else
		map.setBuilding(x, y, w, h, gid);
	team->myBuildings[id]=b;
	return b;
}

bool Game::removeUnitAndBuildingAndFlags(int x, int y, unsigned flags)
{
	bool found=false;
	if (flags & DEL_GROUND_UNIT)
	{
		Uint16 gauid=map.getAirUnit(x, y);
		if (gauid!=NOGUID)
		{
			int id=Unit::GIDtoID(gauid);
			int team=Unit::GIDtoTeam(gauid);
			map.setAirUnit(x, y, NOGUID);
			delete (teams[team]->myUnits[id]);
			teams[team]->myUnits[id]=NULL;
			found=true;
		}
	}
	if (flags & DEL_AIR_UNIT)
	{
		Uint16 gguid=map.getGroundUnit(x, y);
		if (gguid!=NOGUID)
		{
			int id=Unit::GIDtoID(gguid);
			int team=Unit::GIDtoTeam(gguid);
			map.setGroundUnit(x, y, NOGUID);
			delete (teams[team]->myUnits[id]);
			teams[team]->myUnits[id]=NULL;
			found=true;
		}
	}
	if (flags & DEL_BUILDING)
	{
		Uint16 gbid=map.getBuilding(x, y);
		if (gbid!=NOGBID)
		{
			int id=Building::GIDtoID(gbid);
			int team=Building::GIDtoTeam(gbid);
			Building *b=teams[team]->myBuildings[id];
			if (!b->type->isVirtual)
				map.setBuilding(b->posX, b->posY, b->type->width, b->type->height, NOGBID);
			delete b;
			teams[team]->myBuildings[id]=NULL;
			found=true;
		}
	}
	if (flags & DEL_FLAG)
	{
		for (int ti=0; ti<mapHeader.getNumberOfTeams(); ti++)
			for (std::list<Building *>::iterator bi=teams[ti]->virtualBuildings.begin(); bi!=teams[ti]->virtualBuildings.end(); ++bi)
				if ((*bi)->posX==x && (*bi)->posY==y)
				{
					teams[ti]->myBuildings[Building::GIDtoID((*bi)->gid)]=NULL;
					delete *bi;
					teams[ti]->virtualBuildings.erase(bi);
					found=true;
					break;
				}
	}
	return found;
}

bool Game::removeUnitAndBuildingAndFlags(int x, int y, int size, unsigned flags)
{
	int sts = size>>1;
	int stp = (~size)&1;
	bool somethingInRect = false;

	for (int scx=(x-sts); scx<=(x+sts-stp); scx++)
		for (int scy=(y-sts); scy<=(y+sts-stp); scy++)
			if (removeUnitAndBuildingAndFlags((scx&(map.getMaskW())), (scy&(map.getMaskH())), flags))
				somethingInRect = true;

	return somethingInRect;
}

bool Game::checkRoomForBuilding(int mousePosX, int mousePosY, const BuildingType *bt, int *buildingPosX, int *buildingPosY, int teamNumber, bool checkFow)
{
	int x=mousePosX+bt->decLeft;
	int y=mousePosY+bt->decTop;

	*buildingPosX=x;
	*buildingPosY=y;

	return checkRoomForBuilding(x, y, bt, teamNumber, checkFow);
}

bool Game::checkRoomForBuilding(int x, int y, const BuildingType *bt, int teamNumber, bool checkFow)
{
	Team *team=teams[teamNumber];
	assert(team);

	int w=bt->width;
	int h=bt->height;

	bool isRoom=true;
	if (bt->isVirtual)
	{
		if (teamNumber<0)
			return true;

		for (std::list<Building *>::iterator vb=team->virtualBuildings.begin(); vb!=team->virtualBuildings.end(); ++vb)
		{
			Building *b=*vb;
			if ((b->posX==(x&map.getMaskW())) && (b->posY==(y&map.getMaskH())))
				return false;
		}
		return true;
	}
	else
		isRoom=map.isFreeForBuilding(x, y, w, h);

	if (!checkFow)
		return isRoom;

	if (isRoom)
	{
		for (int dy=y; dy<y+h; dy++)
			for (int dx=x; dx<x+w; dx++)
				if (map.isMapDiscovered(dx, dy, team->me))
					return true;
		return false;
	}
	else
		return false;
}

bool Game::checkHardRoomForBuilding(int coordX, int coordY, const BuildingType *bt, int *mapX, int *mapY)
{
	int x=coordX+bt->decLeft;
	int y=coordY+bt->decTop;

	*mapX=x;
	*mapY=y;

	return checkHardRoomForBuilding(x, y, bt);
}

bool Game::checkHardRoomForBuilding(int x, int y, const BuildingType *bt)
{
	int w=bt->width;
	int h=bt->height;
	assert(!bt->isVirtual); // This method is not for flags!
	return map.isHardSpaceForBuilding(x, y, w, h);
}



Unit* Game::getUnit(int guid)
{
	if(guid == NOGUID)
		return NULL;
	return teams[Unit::GIDtoTeam(guid)]->myUnits[Unit::GIDtoID(guid)];
}

