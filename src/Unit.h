/*
  Copyright (C) 2001-2006 Stephane Magnenat & Luc-Olivier de Charriere
  for any question or comment contact us at nct at ysagoon dot com or
  nuage at ysagoon dot com

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

#include <vector>
#include <string>
#include <assert.h>
#include <string.h>

#include <GAGSys.h>
#include "UnitConsts.h"
#include "MatcherConsts.h"
#include "Ressource.h"

#define LEVEL_UP_ANIMATION_FRAME_COUNT 20
#define MAGIC_ACTION_ANIMATION_FRAME_COUNT 8

class Team;
class Race;
class UnitSkin;
class Building;

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

// a unit
class Unit
{
public:
	Unit(GAGCore::InputStream *stream, Team *owner, Sint32 versionMinor);
	Unit(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, int level);
	virtual ~Unit(void) { }
	
	void load(GAGCore::InputStream *stream, Team *owner, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	void loadCrossRef(GAGCore::InputStream *stream, Team *owner);
	void saveCrossRef(GAGCore::OutputStream *stream);
	
	void subscriptionSuccess(void); //Called by the building the unit has subscribed to.
	void matchSuccess(Building *matchBuilding, MatcherConsts::Activity matchActivity);
	
	
	// helpers called by state machine
protected:
	void applyWarriorAttack(void);
	void applyResourceHarvest(void);
	void applyResourceFill(void);
	
	void animationStep(void);
	void setMapDiscovered(void);
	void biologicalStep(void);
	void insideStep(void);
	void linkFree(void);
	void linkHungry(void);
	void linkDamaged(void);
	void unlink(void);
	void selectDisplacement(void);
	void recheckLink(void);
	void displacementInMatcher(void);
	void middleOfMovement(void);
	
public:
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
	
	void selectPreferedMovement(void);
	void selectPreferedGroundMovement(void);
	bool isUnitHungry(void);
	void standardRandomActivity();
	
	int getRealArmor(void) const;
	int getRealAttackStrength(void) const; //!< Return the real attack strengh for warriors
	int getNextLevelThreshold(void) const;
	void incrementExperience(int increment);
	
	void skinPointerFromName(void);
	
public:

	enum Medical
	{
		MED_FREE = 0,
		MED_HUNGRY = 1,
		MED_DAMAGED = 2
	};

	enum Activity
	{
		ACT_RANDOM = 0,
		ACT_FILLING = 1,
		ACT_FLAG = 2,
		ACT_UPGRADING = 3
	};
	
	enum InsideBuildingState
	{
		IBS_NONE = 0,
		IBS_ENTERING = 1,
		IBS_INSIDE = 2,
	};
	
	enum Movement
	{
		MOV_NORMAL = 0,
		MOV_ATTACK = 1,
		MOV_HARVEST = 2,
		MOV_FILL = 3,
		MOV_EXITING_BUILDING = 4
	};
	/*enum Displacement
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
	};*/
	
	/*
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
		MOV_ATTACKING_TARGET=11
	};*/

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
	void handleMagic(void);
	void handleMedical(void);
	void handleActivity(void);
	void handleDisplacement(void);
	void handleMovement(void);
	void handleAction(void);
	
	void endOfAction(void);
	
	void setNewValidDirectionGround(void);
	void setNewValidDirectionAir(void);
	void flytoTarget(); //This will set (dx,dy) given visualTargetX/Y. air asserted.
	void gotoGroundTarget(); //This will set (dx,dy) given visualTargetX/Y. ground asserted.
	void escapeGroundTarget(); //This will set (dx,dy) opposed to the given visualTargetX/Y, without the care of forbidden flags ground asserted.
	void simplifyDirection(int ldx, int ldy, int *cdx, int *cdy);
	void defaultSkinNameFromType(void);
	
public:
	
	// unit specification
	Sint32 typeNum; // Uint8, WORKER, EXPLORER, WARRIOR
	Race *race;
	UnitSkin *skin;
	std::string skinName;
	
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
	//Displacement displacement;
	Movement movement;
	Abilities action;
	Sint32 visualTargetX, visualTargetY;
	bool validVisualTarget;
	Sint32 magicActionTimeout;
	
	// new states
	InsideBuildingState insideBuildingState;
	MatcherConsts::Activity currentActivity; //!< What the unit is currently really doing. Set by a successfull match, reset by Unit::unlink() 
	MatcherConsts::State matcherState;	//!< If we are in matcher, in which category we are (hungry, damaged, free). Set when entering matcher, reset by Unit::matchSuccess().

	// trigger parameters
	Sint32 hp; // (Uint8)
	Sint32 trigHP; // (Uint8)

	// hungry : maxfood = 100000
	Sint32 hungry; // (Uint16)
	Sint32 hungryness;
	Sint32 trigHungry; // (Uint16)
	Sint32 trigHungryCarying;
	Uint32 fruitMask;
	Uint32 fruitCount;

	// quality parameters
	Sint32 performance[NB_ABILITY];
	Sint32 level[NB_ABILITY];
	bool canLearn[NB_ABILITY];
	Sint32 experience;
	Sint32 experienceLevel;
	
	// building attraction handling
	Building *attachedBuilding;
	//Building *targetBuilding;
	Building *ownExchangeBuilding;
	Building *foreingExchangeBuilding;
	Sint32 destinationResource; //!< the resource we are concerned with our currentActivity (harvesting, transporting, ...)
	Sint32 destinationPurprose;
	bool subscribed;
	int caryedResource;
	
	// gui
	int levelUpAnimation;
	int magicActionAnimation;
	
public:
	// optimisation cached values
	int stepsLeftUntilHungry;
	int stepsLeftUntilDead;
	int minDistToResource[MAX_RESSOURCES]; //!< distance to resource, even if not accessible with current hungryness. -1 if not accessible at all
	bool allResourcesAreTooFar; //!< true if no resource is accessible with current hungryness

public:
	// computing optimisation cached values
	int numberOfStepsLeftUntilHungry(void);
	int numberOfStepsLeftUntilDead(void);
	void computeMinDistToResources(void);
	
public:
	void integrity();
	Uint32 checkSum(std::vector<Uint32> *checkSumsVector);
	bool verbose;
	
protected:
	FILE *logFile;
};

#endif
