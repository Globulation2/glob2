// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <vector>
#include <string>
#include <optional>
#include <assert.h>
#include <string.h>

#include "UnitUtils.h"
#include <GAGSys.h>
#include "UnitConsts.h"
#include "Ressource.h"

#define LEVEL_UP_ANIMATION_FRAME_COUNT 20
#define MAGIC_ACTION_ANIMATION_FRAME_COUNT 8

class Team;
class Race;
class Building;

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

// a unit
class Unit : public UnitUtils
{
	void init(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, int level);
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
private:
	void dxDyFromDirection(void);
public:
	static int directionFromDxDy(int dx, int dy);
	inline static void dxDyFromDirection(int direction, int *dx, int *dy)
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

	void selectPreferredMovement(void);
	void selectPreferredGroundMovement(void);
	bool isUnitHungry(void);
	void standardRandomActivity();
	
	int getRealArmor(bool isMagic) const;
	int getRealAttackStrength(void) const; //!< Return the real attack strength for warriors
	int getNextLevelThreshold(void) const;
	void incrementExperience(int increment);
	
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
//NOT USED:
//		//!Markets can get emptied
//		DIS_EMPTYING_BUILDING=6,
		
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
		MOV_GOING_DX_DY=4,
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
	// handleMovement() helpers — one per Displacement state, plus the pre-switch
	// "claim adjacent clearing-area cell" check. Behavior-preserving split of the
	// original 555-line switch; keep helpers in lockstep with the dispatcher.
	bool tryClaimClearingAreaForHarvesting();
	void handleMovementRemovingBlackAround();
	void handleMovementAttackingAround();
	void handleMovementClearingResources();
	void handleMovementRandom();
	void handleMovementGoingToFlagOrBuilding();
	void handleMovementEnteringBuilding();
	void handleMovementInside();
	void handleMovementExitingBuilding();
	void handleMovementGoingToRessource();
	void handleMovementHarvesting();
	void handleMovementFillingBuilding();
	// Shared post-quality-comparison logic for handleMovementAttackingAround().
	// If newQuality beats `quality`, calls pathfindPointToPoint and on success
	// updates movement/dx/dy/targetX/targetY/validTarget and lowers `quality`.
	// pathfindPointToPoint writes &dx,&dy regardless of success — by design.
	void tryAcquireAttackTarget(int x, int y, int newQuality, int& quality);
	void handleAction(void);
	// handleAction() helpers — collapse the repeated clear-slot/wrap-move/claim-slot
	// pattern. Air vs ground is selected by performance[FLY], matching the existing
	// asserts in cases that hardcode one or the other.
	void wrapPosition();
	void clearOccupiedMapSlot();
	void claimOccupiedMapSlot();
	// One per Movement (MOV_*) enum value. handleAction() switches into these.
	// MOV_INSIDE is a no-op so it has no helper.
	void handleActionRandomGround();
	void handleActionRandomFly();
	void handleActionGoingTarget();
	void handleActionFlyingTarget();
	void handleActionGoingDxDy();
	void handleActionEnteringBuilding();
	void handleActionExitingBuilding();
	void handleActionFilling();
	void handleActionAttackingTarget();
	void handleActionHarvesting();

	void endOfAction(void);
	
	void setNewValidDirectionGround(void);
	void setNewValidDirectionAir(void);
	void flyToTarget(); //This will set (dx,dy) given targetX/Y. air asserted.
	void gotoGroundTarget(); //This will set (dx,dy) given targetX/Y. ground asserted.
	void escapeGroundTarget(); //This will set (dx,dy) opposed to the given targetX/Y, without the care of forbidden flags ground asserted.
	void simplifyDirection(int ldx, int ldy, int *cdx, int *cdy);

	bool locationIsInEnemyGuardTowerRange(int x, int y)const;
	
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
	/// These coordinates are used for target-lines only (Hotkey T in game)
	Sint32 targetX, targetY;
	/// Maybe this is also only for GUI to tag if a line may be drawn or not
	bool validTarget;
	Sint32 magicActionTimeout;

	// Timer counts down 240 frames after being attacked
	Uint8 underAttackTimer;

	// trigger parameters
	Sint32 hp; // (Uint8)
	Sint32 trigHP; // (Uint8)

	// hungry
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
	
	//! building the Unit is working for
	Building *attachedBuilding;
	//! building the Unit is going to
	Building *targetBuilding;
	//! no idea what this is. TODO: Explain
	Building *ownExchangeBuilding;
	Sint32 destinationPurpose;
	int carriedRessource;
	/// This counts 32 ticks to wait for a job before a unit goes off
	/// to upgrade or heal when it is otherwise doing nothing.
	Sint32 jobTimer;
	
	// gui
	int levelUpAnimation;
	int magicActionAnimation;
	
	// (x, y) of the clearing-area cell this unit has claimed on the map. nullopt =
	// no current claim. The pre-tick reset in handleMovement() releases the claim
	// and resets this back to nullopt.
	struct ClearingAreaClaim { Uint32 x; Uint32 y; };
	std::optional<ClearingAreaClaim> previousClearingArea;
	// Distance from this unit's position to its claimed cell at the time of the
	// last gradient-based claim (handleMovementRandom only — tryClaimClearingArea
	// ForHarvesting does not update this). Read by other units via the cross-unit
	// theft check; intentionally retains its prior value across the per-tick reset.
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
	bool integrity();
	Uint32 checkSum(std::vector<Uint32> *checkSumsVector);
    void setTargetBuilding(Building * b);
	bool verbose;
};

