// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

#include "EntityRandom.h"

#include "MaterialPacket.h"
#include "UnitConsts.h"
#include <type_traits>

using UnitPerformanceStorage = Uint16;
inline constexpr Sint32 UnitPerformanceLimit = 65535;

enum class UnitJobPurpose : Uint8 { None, Transport, Clear, Explore, Defend };

// Authoritative pointer-free unit state. Both live units and captured units use
// this exact record; capture copies it without translating fields or arrays.
// Relationships and query caches remain outside the record.
struct UnitState
{
	enum Medical : Uint8
	{
		MED_FREE=0,
		MED_HUNGRY=1,
		MED_DAMAGED=2
	};

	enum Activity : Uint8
	{
		ACT_RANDOM=0,
		ACT_FILLING=1,
		ACT_FLAG=2,
		ACT_UPGRADING=3
	};
	
	enum Displacement : Uint8
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
	
	enum Movement : Uint8
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

	// Naturally aligned wide arithmetic state precedes the compact byte fields.
	Uint16 typeNum;
	Uint16 gid;
	Uint32 scriptIdentity = 0; // Stable identity, excluded from legacy checksums.
	EntityRandom entityRandom;
	Uint32 capabilityFlags = 0;
	Sint32 isDead;
	Sint32 posX, posY, delta;
	Sint32 insideTimeout;
	Sint32 configuredFoodCapacity = HUNGRY_MAX;
	Sint32 speed; // Keep arithmetic and surplus movement wide.
	Sint32 targetX, targetY;
	Sint32 magicActionTimeout;
	Sint32 hp, trigHP;
	Sint32 hungry, hungriness, trigHungry, trigHungryCarrying;
	Uint32 fruitMask, fruitCount;
	UnitPerformanceStorage performance[NB_ABILITY];
	Uint32 stateFlags = 0; // Learning and independent flags; unused bits stay zero.
	Sint32 experience, experienceLevel;
	MaterialPacket carriedPacket{};
	Sint32 jobTimer;
	Uint32 areaLastPulseTick = Uint32(-1);
	Sint16 terrainHealthRemainder = 0; // Signed continuation debt; arithmetic is wide.
	UnitJobPurpose jobPurpose = UnitJobPurpose::None;
	Uint8 regenerationRemainder = 0;
	Sint8 dx, dy;
	Uint8 direction;
	Medical medical;
	Activity activity;
	Displacement displacement;
	Movement movement;
	Uint8 action = WALK; // Game::addUnit chooses movement; loading restores action.
	Uint8 underAttackTimer;
	Uint8 configuredVisionRadius = 1;
	Uint8 level[NB_ABILITY];
	Uint8 constructionLevel = 0;
	Sint8 destinationPurpose;
	Sint8 carriedMaterial;
	Uint8 levelUpAnimation = 0, magicActionAnimation = 0; // Presentation clocks are captured.
	std::array<Uint8,3> areaServiceRemainders{};

    // Cached abilities have a deliberate runtime cap; catalog tables stay wide.
    // Apply rule arithmetic (notably HP division) before committing the cache.
    void setPerformance(int ability,Sint64 value) {
        assert(ability>=0 && ability<NB_ABILITY);
        performance[ability]=UnitPerformanceStorage(value<0 ? 0 : value>UnitPerformanceLimit ? UnitPerformanceLimit : value);
    }
	static constexpr Uint32 LearningMask = (Uint32(1) << NB_ABILITY) - 1;
	static_assert(NB_ABILITY<=28);
	enum Flag : Uint32 {
		ServiceReserved = Uint32(1)<<NB_ABILITY,
		WidePrimaryCargo = Uint32(1)<<(NB_ABILITY+1),
		MedicalRecheck = Uint32(1)<<(NB_ABILITY+2),
		ValidTarget = Uint32(1)<<(NB_ABILITY+3)
	};
	Uint32 learningFlags() const { return stateFlags & LearningMask; }
	void setLearningFlags(Uint32 mask) { stateFlags=(stateFlags & ~LearningMask) | (mask & LearningMask); }
	bool canLearn(int ability) const { return (stateFlags & (Uint32(1) << ability)) != 0; }
	void setCanLearn(int ability, bool enabled)
	{
		const Uint32 mask = Uint32(1) << ability;
		stateFlags = enabled ? stateFlags | mask : stateFlags & ~mask;
	}
	void setFlag(Flag mask, bool enabled) { stateFlags=enabled ? stateFlags | mask : stateFlags & ~Uint32(mask); }
	bool serviceResourcesReserved() const { return (stateFlags & ServiceReserved)!=0; }
	void setServiceResourcesReserved(bool enabled) { setFlag(ServiceReserved,enabled); }
	bool widePrimaryCargo() const { return (stateFlags & WidePrimaryCargo)!=0; }
	void setWidePrimaryCargo(bool enabled) { setFlag(WidePrimaryCargo,enabled); }
	bool needToRecheckMedical() const { return (stateFlags & MedicalRecheck)!=0; }
	void setNeedToRecheckMedical(bool enabled) { setFlag(MedicalRecheck,enabled); }
	bool validTarget() const { return (stateFlags & ValidTarget)!=0; }
	void setValidTarget(bool enabled) { setFlag(ValidTarget,enabled); }
	bool operator==(const UnitState&) const = default;
};
static_assert(std::is_trivially_copyable_v<UnitState>);
static_assert(std::is_standard_layout_v<UnitState>);
