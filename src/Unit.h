/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __UNIT_H
#define __UNIT_H

#include "GAG.h"
#include "UnitType.h"
#include <string.h>

class Team;
class Race;
class Building;

// a unit
class Unit
{

public:
	Unit(SDL_RWops *stream, Team *owner);
	Unit(int x, int y, Sint16 uid, UnitType::TypeNum type, Team *team, int level);
	virtual ~Unit(void) { }
	
	void load(SDL_RWops *stream, Team *owner);
	void save(SDL_RWops *stream);
	void loadCrossRef(SDL_RWops *stream, Team *owner);
	void saveCrossRef(SDL_RWops *stream);
	
	void unsubscribed(void);
	bool step(void);
	
	void directionFromDxDy(void);
	void dxdyfromDirection(void);
	static int directionFromDxDy(int dx, int dy);
	static void dxdyfromDirection(int direction, int *dx, int *dy);
	
	static Sint32 UIDtoID(Sint32 uid);
	static Sint32 UIDtoTeam(Sint32 uid);
	static Sint32 UIDfrom(Sint32 id, Sint32 team);
	
	void selectPreferedMovement(void);
	
public:

	enum Medical
	{
		MED_FREE,
		MED_HUNGRY,
		MED_DAMAGED
	};

	enum Activity
	{
		ACT_RANDOM,
		ACT_HARVESTING,
		ACT_BUILDING,
		ACT_FLAG,
		ACT_UPGRADING
	};
	
	enum Displacement
	{
		DIS_RANDOM,
		
		DIS_HARVESTING,
		DIS_BUILDING,
		
		DIS_GOING_TO_FLAG,
		DIS_ATTACKING_AROUND,
		DIS_REMOVING_BLACK_AROUND,
		DIS_CLEARING_RESSOURCES,
		
		DIS_GOING_TO_RESSOURCE,
		DIS_GIVING_TO_BUILDING,
		
		DIS_GOING_TO_BUILDING,
		DIS_ENTERING_BUILDING,
		DIS_INSIDE,
		DIS_EXITING_BUILDING
	};
	
	enum Movement
	{
		MOV_RANDOM,
		MOV_GOING_TARGET,
		MOV_GOING_DXDY,
		MOV_HARVESTING,
		MOV_GIVING,
		MOV_BUILDING,
		MOV_ENTERING_BUILDING,
		MOV_INSIDE,
		MOV_EXITING_BUILDING,
		MOV_ATTACKING_TARGET
	};
	
	enum BypassDirection
	{
		DIR_UNSET=0,
		DIR_LEFT=1,
		DIR_RIGHT=2
	};

	enum 
	{
		HUNGRY_MAX=150000
	};
	
protected:
	void handleMedical(void);
	void handleActivity(void);
	void handleDisplacement(void);
	void handleMovement(void);
	void handleAction(void);
	
	bool endOfAction(void);
	void setNewValidDirection(void);
	bool valid(int x, int y);
	bool validHard(int x, int y);
	bool validMed(int x, int y);
	void pathFind(void);
	void gotoTempTarget(void);
	bool areOnlyUnitsAround(void);
	bool areOnlyUnitsInFront(int dx, int dy);
	void gotoTarget(int targetX, int targetY);
	void newTargetWasSet(void);
	void simplifyDirection(int ldx, int ldy, int *cdx, int *cdy);
	void secondaryDirection(int ldx, int ldy, int *cdx, int *cdy);
	
public:
	
	// unit specification
	UnitType::TypeNum typeNum; // Uint8, WORKER, EXPLORER, WARRIOR
	Race *race;
	
	// identity
	Sint32 UID; // team * 1024 (Uint16)
	Team *owner; // if < 0, not allocated
	Sint32 isDead; // (bool) if true is dead, will be garbage collected next turn

	// position
	Sint32 posX, posY; // (Uint16)
	Sint32 delta; // (Sint8)
	Sint32 dx, dy; // (Sint8)
	Sint32 direction; // (Sint8). direction=8 is no direction.
	Sint32 insideTimeout; // (Sint16) (if < 0, is in a building, otherwise is out)
	Sint32 speed;

	// states
	bool needToRecheckMedical;
	Medical medical;
	Activity activity;
	Displacement displacement;
	Movement movement;
	Abilities action;
	Sint32 targetX, targetY;
	Sint32 tempTargetX, tempTargetY;
	BypassDirection bypassDirection;
	Sint32 obstacleX, obstacleY;
	Sint32 borderX, borderY;

	// trigger parameters
	Sint32 hp; // (Uint8)
	Sint32 trigHP; // (Uint8)

	// hungry : maxfood = 100000
	Sint32 hungry; // (Uint16)
	Sint32 trigHungry; // (Uint16)
	Sint32 trigHungryCarying;
	
	// quality parameters
	Sint32 performance[NB_ABILITY];
	Sint32 level[NB_ABILITY];
	
	// building attraction handling
	// FIXME : should we duplicate this
	Building *attachedBuilding;
	int destinationPurprose;
	bool subscribed;
	int caryedRessource;

public:
	Sint32 checkSum();
	bool verbose;
};

#endif
