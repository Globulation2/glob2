// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

#include "EntityRandom.h"

#include "MaterialPacket.h"
#include "UnitConsts.h"
#include <type_traits>
#include <cstddef>
#include <stdexcept>

enum class UnitJobPurpose : Uint8 { None, Transport, Clear, Explore, Defend };

// Stable enum values shared by all three representations.
struct UnitEnums
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

};

// The hot, pointer-free authoritative prefix shared by movement and rendering.
struct UnitCoreState
{
    using Medical=UnitEnums::Medical; using Activity=UnitEnums::Activity;
    using Displacement=UnitEnums::Displacement; using Movement=UnitEnums::Movement;

    Uint16 typeNum, gid;
    Uint32 capabilityFlags = 0;
    Uint32 scriptIdentity = 0;
    Sint32 isDead;
    Sint32 posX, posY, delta, dx, dy, direction;
    Sint32 speed;
    Medical medical;
    Activity activity;
    Displacement displacement;
    Movement movement;
    Abilities action = WALK;
    Sint32 targetX, targetY;
    Sint32 hp, hungry;
    Sint32 constructionLevel = 0;
    Sint32 experienceLevel;
    Sint32 destinationPurpose;
    int carriedMaterial;
    Sint32 levelUpAnimation = 0, magicActionAnimation = 0;
    // Authoritative values, not a cache of another ability array.
    std::array<Sint32,6> abilities;
    UnitJobPurpose jobPurpose = UnitJobPurpose::None;
    bool validTarget;
    Uint8 underAttackTimer;
    bool operator==(const UnitCoreState&) const = default;
    static constexpr int abilityIndex(int ability) {
        switch (ability) {
        case WALK: return 0; case SWIM: return 1; case FLY: return 2;
        case BUILD: return 3; case HARVEST: return 4; case HP: return 5;
        default: return -1;
        }
    }
    Sint32& performance(int ability) { return abilities.at(abilityIndex(ability)); }
    const Sint32& performance(int ability) const { return abilities.at(abilityIndex(ability)); }
};
struct UnitDetailState
{

    EntityRandom entityRandom;
    Sint32 terrainHealthRemainder = 0;
    Sint32 insideTimeout;
    Sint32 configuredFoodCapacity = UnitEnums::HUNGRY_MAX;
    Sint32 magicActionTimeout;
    Sint32 trigHP;
    Sint32 hungriness, trigHungry, trigHungryCarrying;
    Uint32 fruitMask, fruitCount;
    std::array<Sint32,NB_ABILITY-6> abilities;
    Sint32 level[NB_ABILITY];
    Sint32 experience;
    MaterialPacket carriedPacket{};
    Sint32 jobTimer;
    Uint32 areaLastPulseTick = Uint32(-1);
    bool canLearn[NB_ABILITY];
    bool serviceResourcesReserved = false, widePrimaryCargo = false;
    bool needToRecheckMedical;
    Uint8 regenerationRemainder = 0, configuredVisionRadius = 1;
    std::array<Uint8,3> areaServiceRemainders{};
    bool operator==(const UnitDetailState&) const = default;
};

// Composition guarantees the hot core begins at offset zero. Live-only pointers
// and caches belong to Unit, outside this value record.
struct UnitState : UnitEnums
{
    UnitCoreState core;
    UnitDetailState detail;
    static constexpr int detailAbilityIndex(int ability) {
        // Cold slots are STOP_* followed by ATTACK_* through ARMOR.
        // Callers select the core first, so only these cold indices reach here.
        return ability<STOP_FLY+1 ? ability : ability-5;
    }
    Sint32& performance(int ability) {
        if (ability<0 || ability>=NB_ABILITY) throw std::out_of_range("unit ability");
        const int hot=UnitCoreState::abilityIndex(ability);
        return hot>=0 ? core.abilities[hot] : detail.abilities[detailAbilityIndex(ability)];
    }
    const Sint32& performance(int ability) const {
        if (ability<0 || ability>=NB_ABILITY) throw std::out_of_range("unit ability");
        const int hot=UnitCoreState::abilityIndex(ability);
        return hot>=0 ? core.abilities[hot] : detail.abilities[detailAbilityIndex(ability)];
    }
};
static_assert(std::is_trivially_copyable_v<UnitCoreState>);
static_assert(std::is_standard_layout_v<UnitCoreState>);
static_assert(std::is_trivially_copyable_v<UnitDetailState>);
static_assert(std::is_standard_layout_v<UnitDetailState>);
static_assert(std::is_trivially_copyable_v<UnitState>);
static_assert(std::is_standard_layout_v<UnitState>);
static_assert(offsetof(UnitState,core)==0);
