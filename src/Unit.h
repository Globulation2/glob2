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

#ifndef __UNIT_H
#define __UNIT_H

#include <assert.h>
#include <string.h>
#include <SDL/SDL_rwops.h>

#include "UnitConsts.h"

class Team;
class Race;
class Building;

// a unit
class Unit
{
public:
	Unit(SDL_RWops *stream, Team *owner);
	Unit(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, int level);
	virtual ~Unit(void) { }
	
	void load(SDL_RWops *stream, Team *owner);
	void save(SDL_RWops *stream);
	void loadCrossRef(SDL_RWops *stream, Team *owner);
	void saveCrossRef(SDL_RWops *stream);
	
	void subscriptionSuccess(void); //Called by the building the unit has subscribed to.
	void syncStep(void);
	
	void directionFromDxDy(void);
	void dxdyfromDirection(void);
	static int directionFromDxDy(int dx, int dy);
	inline static void dxdyfromDirection(int direction, int *dx, int *dy)
	{
	const int tab[9][2]={	{ -1, -1},
							{ 0, -1},
							{ 1, -1},
							{ 1, 0},
							{ 1, 1},
							{ 0, 1},
							{ -1, 1},
							{ -1, 0},
							{ 0, 0} };
	assert(direction>=0);
	assert(direction<=8);
	*dx=tab[direction][0];
	*dy=tab[direction][1];
	}
	
	static Sint32 GIDtoID(Uint16 gid);
	static Sint32 GIDtoTeam(Uint16 gid);
	static Uint16 GIDfrom(Sint32 id, Sint32 team);

	void selectPreferedMovement(void);
	void selectPreferedGroundMovement(void);
	bool isUnitHungry(void);
	void standardRandomActivity();
	
public:

	enum Medical
	{
		MED_FREE=0,
		MED_HUNGRY=1,
		MED_DAMAGED=2
	};

	enum Activity
	{
		ACT_RANDOM=0,
		ACT_FILLING=1,
		ACT_FLAG=2,
		ACT_UPGRADING=3
	};
	
	enum Displacement
	{
		DIS_RANDOM=0,
		
		DIS_HARVESTING=2,
		
		DIS_FILLING_BUILDING=4,
		DIS_EMPTYING_BUILDING=6,
		
		DIS_GOING_TO_FLAG=8,
		DIS_ATTACKING_AROUND=10,
		DIS_REMOVING_BLACK_AROUND=12,
		DIS_CLEARING_RESSOURCES=14,
		
		DIS_GOING_TO_RESSOURCE=16,
		
		DIS_GOING_TO_BUILDING=18,
		DIS_ENTERING_BUILDING=20,
		DIS_INSIDE=22,
		DIS_EXITING_BUILDING=24
	};
	
	enum Movement
	{
		MOV_RANDOM_GROUND=0,
		MOV_RANDOM_FLY=1,
		MOV_GOING_TARGET=2,
		MOV_FLYING_TARGET=3,
		MOV_GOING_DXDY=4,
		MOV_HARVESTING=5,
		MOV_FILLING=6,
		MOV_ENTERING_BUILDING=7,
		MOV_INSIDE=8,
		MOV_EXITING_BUILDING=9,
		MOV_ESCAPING_FORBIDDEN=10,
		MOV_ATTACKING_TARGET=11
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
	void stopAttachedForBuilding(bool goingInside);
	void handleMedical(void);
	void handleActivity(void);
	void handleDisplacement(void);
	void handleMovement(void);
	void handleAction(void);
	
	void endOfAction(void);
	
	void setNewValidDirectionGround(void);
	void setNewValidDirectionAir(void);
	void flytoTarget(); //This will set (dx,dy) given targetX/Y. air asserted.
	void gotoGroundTarget(); //This will set (dx,dy) given targetX/Y. ground asserted.
	void escapeGroundTarget(); //This will set (dx,dy) opposed to the given targetX/Y, without the care of forbidden flags ground asserted.
	void simplifyDirection(int ldx, int ldy, int *cdx, int *cdy);
	
public:
	
	// unit specification
	Sint32 typeNum; // Uint8, WORKER, EXPLORER, WARRIOR
	Race *race;
	
	// identity
	Uint16 gid; // for reservation see GIDtoID() and GIDtoTeam().
	Team *owner;
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

	// trigger parameters
	Sint32 hp; // (Uint8)
	Sint32 trigHP; // (Uint8)

	// hungry : maxfood = 100000
	Sint32 hungry; // (Uint16)
	Sint32 trigHungry; // (Uint16)
	Sint32 trigHungryCarying;
	Uint32 fruitMask;
	Uint32 fruitCount;

	// quality parameters
	Sint32 performance[NB_ABILITY];
	Sint32 level[NB_ABILITY];
	bool canLearn[NB_ABILITY];
	
	// building attraction handling
	Building *attachedBuilding;
	Building *targetBuilding;
	Building *ownExchangeBuilding;
	Building *foreingExchangeBuilding;
	Sint32 destinationPurprose;
	bool subscribed;
	int caryedRessource;

public:
	void integrity();
	Uint32 checkSum();
	bool verbose;
	
protected:
	FILE *logFile;
};

#endif
