// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "UnitState.h"
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
struct TerrainProperties;
struct BuildingTrainingSpec;

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

// a unit
class Unit : public UnitUtils, public UnitState
{
	friend struct TeamStatsMeasurementFixture;
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
	void subscriptionSuccess(Building* building, bool inside, bool attraction = false);
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
	/// Puts a unit from a destroyed building back on the map at (x, y), walking out along (dx, dy).
	void expelFromBuilding(int x, int y, int dx, int dy);
	
	int getRealArmor(bool isMagic) const;
	int getRealAttackStrength(void) const; //!< Return the real attack strength for warriors
	int getNextLevelThreshold(void) const;
	void incrementExperience(int increment);
	
public:

	
protected:
	void stopAttachedForBuilding(bool goingInside);
	void handleMagic(void);
	void handleMedical(void);
	void resolveDeath();
	void applyTerrainHealth();
	void applyTerrainHealth(const TerrainProperties& terrain);
	void applyTerrainHealthRate(int rate);
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
	void handleMovementGoingToResource();
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
	void applyPartialInsideBenefit();
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
	Race *race;

	// identity
	Team *owner;
	int diagnosticDeathCause = 4; // GameplayMeasurements::UNKNOWN; never checksum this field.
	void recordLethalDamage(int damage, int cause);

	//! Pathfinding swim class from the unit's walk and swim speeds (see Map::swimClass).
	int swimClass() const;
	//! Construction eligibility is independent of work/harvest speed.
	Sint32 workerLevel() const { return constructionLevel; }
	bool needsTraining(const BuildingTrainingSpec& training, int ability) const;
	void applyTraining(const BuildingTrainingSpec& training, int ability);
	//! Re-creates a freshly placed unit at `newLevel` in every ability, keeping its place,
	//! identity and team (the lobby's Veteran/Fast start rule, before the map is saved).
	void resetAtLevel(Sint32 newLevel);
	void setWorkerLevel(Sint32 newLevel);
	
	//! building the Unit is working for
	Building *attachedBuilding;
	//! building the Unit is going to
	Building *targetBuilding;
	//! no idea what this is. TODO: Explain
	Building *ownExchangeBuilding;
	void receiveCarriedResource(int resource, ResourcePacket packet);
	
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

public:
	// computing optimisation cached values
	int numberOfStepsLeftUntilHungry(void);

public:
	bool integrity();
	Uint32 checkSum(std::vector<Uint32> *checkSumsVector);
    void setTargetBuilding(Building * b);
	bool verbose;
};
