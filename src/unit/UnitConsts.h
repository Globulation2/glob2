// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <assert.h>
#include <GAGSys.h>

enum Abilities
{
	STOP_WALK=0,
	STOP_SWIM=1,
	STOP_FLY=2,
	
	WALK=3,
	SWIM=4,
	FLY=5,
	BUILD=6,
	HARVEST=7,
	ATTACK_SPEED=8,
	ATTACK_STRENGTH=9,
	
	MAGIC_ATTACK_AIR=10,
	MAGIC_ATTACK_GROUND=11,
	MAGIC_CREATE_WOOD=12,
	MAGIC_CREATE_CORN=13,
	MAGIC_CREATE_ALGA=14,
	
	ARMOR=15, /* old 10 */
	HP=16, /* old 11 */
	
	HEAL=17, /* old 12 */
	FEED=18 /* old 13 */
};
const int NB_MOVE=9;
const int NB_ABILITY=17;

const int WORKER=0;
const int EXPLORER=1;
const int WARRIOR=2;
const int NB_UNIT_TYPE=3;

const int NB_UNIT_LEVELS=4;

// === Unit `delta` Uint8 wrap (cross-slice) ===
//! Maximum value of a unit's per-tile `delta` advancement counter. Used
//! when the counter is treated as "fully arrived" (Unit.cpp:296, 304;
//! UnitMovement.cpp; MapQuery.cpp; TypeSteps.cpp turret bullet timing).
static constexpr int UNIT_DELTA_MAX = 255;
//! Modular quantum that wraps a unit's `delta` counter, equal to
//! UNIT_DELTA_MAX + 1. Used in expressions like (256 - delta) / speed.
static constexpr int UNIT_DELTA_QUANTUM = 256;

// === Direction encoding (cross-slice) ===
// Units encode movement direction as one of 8 cardinal/intercardinal
// directions plus a "no direction" sentinel. The encoding numerically
// collides with COUNT (8 == UNIT_DIRECTION_NONE) — both names are kept so
// each call site reads in its intended meaning. See UnitGeometry.cpp /
// UnitMovement.cpp / MapStep.cpp (`syncRand()&7`).

//! Number of compass directions a unit can face.
static constexpr int UNIT_DIRECTION_COUNT = 8;
//! Bit-mask form of UNIT_DIRECTION_COUNT - 1; used with `& 7` to wrap a
//! direction index into [0, 8).
static constexpr int UNIT_DIRECTION_MASK = 7;
//! "No direction" sentinel for unit dx/dy encoding (the 9-cell encoding
//! reserves index 8 for "stationary"). Numerically equals
//! UNIT_DIRECTION_COUNT, but the meaning is distinct.
static constexpr int UNIT_DIRECTION_NONE = 8;

// === Unit attack tunables (cross-slice) ===
//! Square radius (in tiles) of a warrior's attack-target search around its
//! current position. See UnitMovement.cpp:232, 234.
static constexpr int UNIT_ATTACK_SEARCH_RADIUS = 8;

//! Maximum path length budget for MOV_GOING_TARGET's pathfindPointToPoint
//! call. Used both by UnitAction.cpp (random-fly fallback / target-acquire)
//! and UnitMovement.cpp:349 (tryAcquireAttackTarget); the value is shared so
//! both consumers stay in sync.
static constexpr int GOING_TARGET_MAX_PATH_LENGTH = 12;

// === Bullet damage floor (cross-slice) ===
//! Minimum damage a bullet can inflict — clamps any negative-armor or
//! over-mitigated calculation to at least this. See Sector.cpp:127, 128, 151.
static constexpr int BULLET_MIN_DAMAGE = 1;

// === Per-slice "none" sentinels ===
// `-1` is overloaded inside the unit slice (destination purpose, carried
// resource, free-slot search, "resource unreachable"); each gets its own
// name so the meaning is explicit at the call site, even though they share
// the integer value.

//! `Unit::destinationPurpose` sentinel meaning "no destination chosen yet".
static constexpr int UNIT_DEST_PURPOSE_NONE = -1;
//! `Unit::carriedRessource` sentinel meaning "not carrying anything".
static constexpr int UNIT_CARRIED_RESSOURCE_NONE = -1;
//! Free-slot search sentinel: starting `targetID = -1` means "no free slot
//! found yet" (UnitActivity.cpp conversion code).
static constexpr int UNIT_TARGETID_NONE = -1;
//! `Unit::minDistToResource[]` sentinel meaning "this resource is not
//! reachable from the unit's current position" (UnitStats.cpp).
static constexpr int UNIT_MIN_DIST_NOT_REACHABLE = -1;
//! `Unit::previousClearingAreaDistance` (`Uint32`) sentinel meaning "no
//! claim recorded". Stored as `0xFFFFFFFF`. UnitMovement.cpp:474.
static constexpr Uint32 UNIT_CLEAR_AREA_DISTANCE_NONE = static_cast<Uint32>(-1);
// NOTE: the "no clearing-gradient target" sentinel (254) lives in the map
// slice as `GRADIENT_FORBIDDEN_BORDER` / `GRADIENT_AT_GOAL` and is consumed
// from UnitMovement.cpp:441-442 — no per-slice unit constant is needed.

// === HP / hunger trigger ratios ===
//! Numerator of the "low HP, retreat to heal" trigger: `trigHP = (hp*3)/10`.
//! Set in `Unit::init` after `hp` is overwritten from `performance[HP]`.
static constexpr int UNIT_HP_TRIG_NUM = 3;
//! Denominator of the low-HP retreat trigger.
static constexpr int UNIT_HP_TRIG_DEN = 10;

//! Numerator for warriors' hunger retreat trigger:
//! `trigHungry = (hungry * 2) / 10` (≈20% remaining food).
static constexpr int UNIT_HUNGRY_TRIG_NUM_WARRIOR = 2;
//! Denominator for warriors' hunger retreat trigger.
static constexpr int UNIT_HUNGRY_TRIG_DEN = 10;

//! Divisor for non-warriors' hunger retreat trigger:
//! `trigHungry = hungry / 4` (25% remaining food).
static constexpr int UNIT_HUNGRY_TRIG_DIVISOR_DEFAULT = 4;
//! Divisor for the carrying-a-resource hunger trigger:
//! `trigHungryCarying = hungry / 10` (10% remaining food).
static constexpr int UNIT_HUNGRY_TRIG_DIVISOR_CARRYING = 10;

//! Vision radius (in tiles) granted to flying units; produces a 7x7 reveal
//! window centered on the unit. See Unit.cpp:310-313.
static constexpr int UNIT_VISION_RADIUS_FLY = 3;
//! Vision radius (in tiles) granted to ground units; produces a 3x3 reveal
//! window centered on the unit. See Unit.cpp:316-319.
static constexpr int UNIT_VISION_RADIUS_GROUND = 1;

//! HP threshold below which a unit is considered dead. The check is
//! strictly `<`, so a unit with `hp == UNIT_HP_DEATH_THRESHOLD` is still
//! alive (UnitMedical.cpp:204).
static constexpr int UNIT_HP_DEATH_THRESHOLD = 0;

//! Inverse fraction of HP missing that triggers an idle worker to seek
//! healing: `hp + (performance[HP] / N) < performance[HP]`, where
//! `N == UNIT_HEAL_TRIGGER_INV_RATIO`. UnitActivity.cpp:65.
static constexpr int UNIT_HEAL_TRIGGER_INV_RATIO = 10;

//! Numerator of the "explorer must be ≥90% fed before exiting heal /
//! ≥90% healed before exiting feed" forced-rebound checks.
//! UnitMedical.cpp:173, 180.
static constexpr int EXPLORER_FORCE_FEED_RATIO_NUM = 9;
//! Denominator of the explorer ≥90% rebound check.
static constexpr int EXPLORER_FORCE_FEED_RATIO_DEN = 10;

//! Magic-attack square radius (tiles) around the casting unit. Used as
//! the half-extent of the loop bounds in UnitMedical.cpp:114.
static constexpr int UNIT_MAGIC_ATTACK_RANGE = 3;

//! Midpoint of the per-action `delta` window at which a warrior's swing
//! actually lands a hit: hits trigger when `delta` is in
//! [UNIT_ATTACK_HIT_DELTA, UNIT_ATTACK_HIT_DELTA + speed). With
//! `delta` in [0, UNIT_DELTA_QUANTUM), this is the second half of the
//! tick. See Unit.cpp:239.
static constexpr int UNIT_ATTACK_HIT_DELTA = 128;


