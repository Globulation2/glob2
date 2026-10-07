// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MaterialPacket.h"
#include "UnitConsts.h"
#include <type_traits>

// Authoritative pointer-free unit state. Both live units and captured units use
// this exact record; capture copies it without translating fields or arrays.
// Relationships, query caches and rendering state remain outside the record.
struct UnitState
{
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
		DIS_CLEARING_RESOURCES=14,
		
		DIS_GOING_TO_RESOURCE=16,
		
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

	Sint32 typeNum;
	Uint32 scriptIdentity = 0; // Stable identity, excluded from legacy checksums.
	Uint16 gid;
	Sint32 isDead;
	Sint32 posX, posY, delta, dx, dy, direction;
	Sint32 terrainHealthRemainder = 0;
	Sint32 insideTimeout;
	bool serviceResourcesReserved = false;
	Sint32 speed;
	bool needToRecheckMedical;
	Medical medical;
	Activity activity;
	Displacement displacement;
	Movement movement;
	Abilities action = WALK; // Game::addUnit selects movement; loads restore saved action.
	Sint32 targetX, targetY; // Target lines and movement destination.
	bool validTarget;
	Sint32 magicActionTimeout;
	Uint8 underAttackTimer; // Counts down 240 frames after being attacked.
	Sint32 hp, trigHP;
	Sint32 hungry, hungriness, trigHungry, trigHungryCarrying;
	Uint32 fruitMask, fruitCount;
	Sint32 performance[NB_ABILITY];
	Sint32 level[NB_ABILITY];
	Sint32 constructionLevel = 0; // Independent of work/harvest speed.
	bool canLearn[NB_ABILITY];
	Sint32 experience, experienceLevel;
	Sint32 destinationPurpose;
	int carriedMaterial;
	MaterialPacket carriedPacket{};
	Sint32 jobTimer; // Waits 32 ticks for a job before seeking training or healing.
};
static_assert(std::is_trivially_copyable_v<UnitState>);
static_assert(std::is_standard_layout_v<UnitState>);
