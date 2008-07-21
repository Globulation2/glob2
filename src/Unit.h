/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef __UNIT_H
#define __UNIT_H

#include <vector>
#include <string>
#include <assert.h>
#include <string.h>

#include <GAGSys.h>
#include "UnitConsts.h"
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
	void loadCrossRef(GAGCore::InputStream *stream, Team *owner, Sint32 versionMinor);
	void saveCrossRef(GAGCore::OutputStream *stream);
	
	///This function is called by a Building that has subscribed this unit.
	///If the unit has been subscribed for upgrading or for food, as opposed
	///to being subscribed for work, inside is set to true.
	void subscriptionSuccess(Building* building, bool inside);
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
	
	int getRealArmor(bool isMagic) const;
	int getRealAttackStrength(void) const; //!< Return the real attack strengh for warriors
	int getNextLevelThreshold(void) const;
	void incrementExperience(int increment);
	
	void skinPointerFromName(void);
	
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
	void handleMagic(void);
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
	void defaultSkinNameFromType(void);

	bool locationIsInEnemyGuardTowerRange(int x, int y)const;
	
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
	Displacement displacement;
	Movement movement;
	Abilities action;
	Sint32 targetX, targetY;
	bool validTarget;
	Sint32 magicActionTimeout;

	// Timer counts down 240 frames after being attacked
	Uint8 underAttackTimer;

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
	Building *targetBuilding;
	Building *ownExchangeBuilding;
	Sint32 destinationPurprose;
	int caryedRessource;
	/// This counts 32 ticks to wait for a job before a unit goes off
	/// to upgrade or heal when it is otherwise doing nothing.
	Sint32 jobTimer;
	
	// gui
	int levelUpAnimation;
	int magicActionAnimation;
	
	// These store the previous clearing area target cordinates
	Uint32 previousClearingAreaX;
	Uint32 previousClearingAreaY;
	Uint32 previousClearingAreaDistance;
	
public:
	// optimisation cached values
	int stepsLeftUntilHungry;
	int minDistToResource[MAX_RESSOURCES];
	bool allResourcesAreTooFar;

public:
	// computing optimisation cached values
	int numberOfStepsLeftUntilHungry(void);
	void computeMinDistToResources(void);
	
public:
	void integrity();
	Uint32 checkSum(std::vector<Uint32> *checkSumsVector);
	bool verbose;
	
protected:
	FILE *logFile;
};

#endif
