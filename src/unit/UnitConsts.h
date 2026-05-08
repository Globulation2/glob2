// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <assert.h>
#include <string>

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

std::string getUnitName(int type);

 
