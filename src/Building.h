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

#ifndef __BUILDING_H
#define __BUILDING_H

#include <list>
#include <vector>
#include "Ressource.h"
#include "UnitType.h"
#include "Bullet.h"

class Unit;
class Team;
class BuildingType;
class BuildingsTypes;

class Building
{
public:

	enum BuildingState
	{
		DEAD=0,
		ALIVE=1,
		WAITING_FOR_DESTRUCTION=2,
		WAITING_FOR_CONSTRUCTION=3,
		WAITING_FOR_CONSTRUCTION_ROOM=4
	};
	
	enum ConstructionResultState
	{
		NO_CONSTRUCTION=0,
		NEW_BUILDING=1,
		UPGRADE=2,
		REPAIR=3
	};

	// type
	Uint32 typeNum; // number in BuildingTypes
	BuildingType *type;

	// construction state
	BuildingState buildingState;
	ConstructionResultState constructionResultState;

	// units
	Sint32 maxUnitWorkingLocal;
	Sint32 maxUnitWorking;  // (Uint16)
	Sint32 maxUnitWorkingPreferred;
	std::list<Unit *> unitsWorking;
	std::list<Unit *> unitsWorkingSubscribe;
	Sint32 lastWorkingSubscribe;
	Sint32 maxUnitInside;
	std::list<Unit *> unitsInside;
	std::list<Unit *> unitsInsideSubscribe;
	Sint32 lastInsideSubscribe;
	
	// optimisation and consistency
	Sint32 foodable; // Included in {0: unknow, 1:allready in owner->foodable, 2:not in owner->foodable
	Sint32 fillable; // Included in {0: unknow, 1:allready in owner->fillable, 2:not in owner->fillable
	Sint32 zonable[NB_UNIT_TYPE]; // Included in {0: unknow, 1:allready in owner->zonable, 2:not in owner->zonable
	
	Sint32 upgrade[NB_ABILITY]; // Included in {0: unknow, 1:allready in owner->upgrade[i], 2:not in owner->upgrade[i]}
	
	// identity
	Uint16 gid; // Sint16, for reservation see below TODO
	Team *owner; // if < 0, not allocated

	// position
	Sint32 posX, posY; // (Uint16)
	Sint32 posXLocal, posYLocal;

	// 256 flags
	// Flag usefull :
	Uint32 unitStayRange; // (Uint8)
	Uint32 unitStayRangeLocal;

	// 1024 buldings
	// Building specific :
	Sint32 ressources[NB_RESSOURCES]; // Ammount stocked, or used for building building.

	// quality parameters
	Sint32 hp; // (Uint16)

	// prefered parameters
	Sint32 productionTimeout;
	Sint32 totalRatio;
	Sint32 ratio[NB_UNIT_TYPE];
	Sint32 ratioLocal[NB_UNIT_TYPE];
	Sint32 percentUsed[NB_UNIT_TYPE];
	
	Uint32 shootingStep;
	Sint32 shootingCooldown;
	
	Uint32 seenByMask;

	// optimisation parameters
	Sint32 closestRessourceX[NB_RESSOURCES], closestRessourceY[NB_RESSOURCES];

public:
	Building(SDL_RWops *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor);
	Building(int x, int y, Uint16 gid, int typeNum, Team *team, BuildingsTypes *types);
	virtual ~Building(void) { }
	
	void load(SDL_RWops *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor);
	void save(SDL_RWops *stream);
	void loadCrossRef(SDL_RWops *stream, BuildingsTypes *types, Team *owner);
	void saveCrossRef(SDL_RWops *stream);

	bool isRessourceFull(void);
	int neededRessource(void);
	int neededRessource(int r);
	void launchConstruction(void);
	void cancelConstruction(void);
	void launchDelete(void);
	void cancelDelete(void);
	
	void updateConstructionState(void);
	void updateCallLists(void);
	void updateBuildingSite(void);
	void update(void);
	
	bool tryToBuildingSiteRoom(void); //Returns true if room is found.
	
	bool isHardSpaceForBuildingSite(void);
	bool isHardSpaceForBuildingSite(ConstructionResultState constructionResultState);
	void step(void);
	void removeSubscribers(void);
	bool fullWorking(void);
	bool fullInside(void);
	void subscribeToBringRessourcesStep(void);
	void subscribeForFlagingStep();
	void subscribeForInsideStep(void);
	void swarmStep(void);
	void turretStep(void);
	void kill(void);
	
	int getMidX(void);
	int getMidY(void);
	int getMaxUnitStayRange(void);
	bool findGroundExit(int *posX, int *posY, int *dx, int *dy);
	bool Building::findGroundExit(int *posX, int *posY, int *dx, int *dy, bool canSwim);
	bool findAirExit(int *posX, int *posY, int *dx, int *dy);

	//! get flag from units attached to flag
	void computeFlagStat(int *goingTo, int *onSpot);
	
	static Sint32 GIDtoID(Uint16 gid);
	static Sint32 GIDtoTeam(Uint16 gid);
	static Uint16 GIDfrom(Sint32 id, Sint32 team);

	Sint32 checkSum();
};

#endif
 
