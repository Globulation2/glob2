// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charriere
// Copyright (C) 2005 Eli Dupree
//
// AIWarrushTuning.h
//
// Behavior-preserving tuning constants for AIWarrush, extracted from
// AIWarrush.cpp during the magic-number cleanup pass that prepares the
// codebase for the Rust port. Every value here is byte-for-byte identical
// to the literal it replaces; nothing in the AI's decision logic changes.
//
// Constants are file-scope `static constexpr int` per the slice convention.

#pragma once

#include "Team.h"

// ---------------------------------------------------------------------------
// Tick / phase delays (formerly the BUILDING_DELAY and AREAS_DELAY #defines
// at the top of AIWarrush.cpp).
// ---------------------------------------------------------------------------

// Cooldown (in 40ms ticks) after AIWarrush issues a build order. Prevents
// the AI from spamming repeated build requests at the same target tile.
static constexpr int AI_WARRUSH_BUILDING_DELAY_TICKS = 30;

// Recurring period (in ticks) for the guard-area maintenance cycle. The
// cycle is divided into three phases (prune at 2/3, place at 1/3, refill
// at 0) using AI_WARRUSH_AREAS_PRUNE_PHASE_* and AI_WARRUSH_AREAS_PLACE_*.
static constexpr int AI_WARRUSH_AREAS_DELAY_TICKS = 50;

// ---------------------------------------------------------------------------
// Bootstrap: place an exploration flag on each enemy team's starting swarm
// during the first AI_WARRUSH_BOOTSTRAP_EXPLORE_WINDOW ticks of the game,
// stepping by AI_WARRUSH_BOOTSTRAP_EXPLORE_INTERVAL (so two ticks per team,
// mapping tick -> teamIndex via division by the interval). The window must
// cover exactly Team::MAX_COUNT teams — making it any longer would index past
// the end of game->teams[] (this was a latent OOB read when MAX_COUNT was 32
// and the literal 64 happened to match; the derived form keeps them aligned).
// ---------------------------------------------------------------------------
static constexpr int AI_WARRUSH_BOOTSTRAP_EXPLORE_INTERVAL = 2;
static constexpr int AI_WARRUSH_BOOTSTRAP_EXPLORE_WINDOW   = Team::MAX_COUNT * AI_WARRUSH_BOOTSTRAP_EXPLORE_INTERVAL;

// ---------------------------------------------------------------------------
// Guard-area cycle phase shifts. Original code:
//   if(areaUpdatingDelay == AREAS_DELAY*2/3) prune
//   if(areaUpdatingDelay == AREAS_DELAY/3)   place
// Splitting numerator and denominator keeps the original arithmetic visible.
// ---------------------------------------------------------------------------
static constexpr int AI_WARRUSH_AREAS_PRUNE_PHASE_NUM = 2;
static constexpr int AI_WARRUSH_AREAS_PRUNE_PHASE_DEN = 3;
static constexpr int AI_WARRUSH_AREAS_PLACE_PHASE_DEN = 3;

// ---------------------------------------------------------------------------
// Build-more gate: percentageOfBuildingsAreFullyWorked(70) decides whether
// the AI is "ready to build more stuff."
// ---------------------------------------------------------------------------
static constexpr int AI_WARRUSH_BUILD_MORE_PCT_THRESHOLD = 70;

// ---------------------------------------------------------------------------
// Swarm/inn balance: build a new swarm only if FOOD >= SWARM * 2.
// Build a new inn only if extras >= FOOD - 1 (look-ahead of one inn).
// ---------------------------------------------------------------------------
static constexpr int AI_WARRUSH_INNS_PER_SWARM_RATIO = 2;
static constexpr int AI_WARRUSH_INN_LOOKAHEAD        = 1;

// ---------------------------------------------------------------------------
// Random-building probability ladder (syncRand() % 100):
//
//   <70  -> HEAL          (or HEAL when there are zero heal buildings; see
//                          bug L11 in bugs_surfaced_during_magic_number_audit.md
//                          -- the OR clause is intentionally preserved.)
//   <80  -> WALKSPEED     (or WALKSPEED when there are zero of those)
//   <87  -> SWIMSPEED
//   <94  -> SCIENCE
//   else -> DEFENSE
// ---------------------------------------------------------------------------
static constexpr int AI_WARRUSH_RANDOM_BUILDING_DENOM       = 100;
static constexpr int AI_WARRUSH_HEAL_PCT_THRESHOLD          = 70;
static constexpr int AI_WARRUSH_WALKSPEED_PCT_THRESHOLD     = 80;
static constexpr int AI_WARRUSH_SWIMSPEED_PCT_THRESHOLD     = 87;
static constexpr int AI_WARRUSH_SCIENCE_PCT_THRESHOLD       = 94;

// ---------------------------------------------------------------------------
// Production switchover: once the team has at least this many units with
// HARVEST > 0, AIWarrush retunes its swarms to the dedicated war ratio.
// ---------------------------------------------------------------------------
static constexpr int AI_WARRUSH_HARVESTER_THRESHOLD = 6;

// ---------------------------------------------------------------------------
// Swarm production ratio used by the dedicated-warrushing mode:
// (worker, explorer, warrior) = (4, 1, 3).
// ---------------------------------------------------------------------------
static constexpr int AI_WARRUSH_SWARM_RATIO_WORKER   = 4;
static constexpr int AI_WARRUSH_SWARM_RATIO_EXPLORER = 1;
static constexpr int AI_WARRUSH_SWARM_RATIO_WARRIOR  = 3;

// ---------------------------------------------------------------------------
// Default worker counts AIWarrush forces onto each building type.
// ---------------------------------------------------------------------------
static constexpr int AI_WARRUSH_SWARM_WORKER_COUNT    = 5;
static constexpr int AI_WARRUSH_INN_WORKER_COUNT      = 3;
static constexpr int AI_WARRUSH_BARRACKS_WORKER_COUNT = 3;

// ---------------------------------------------------------------------------
// Guard-area brush radius applied at each enemy-building tile in
// placeGuardAreas().
// ---------------------------------------------------------------------------
static constexpr int AI_WARRUSH_GUARD_BRUSH_SIZE = 6;

// ---------------------------------------------------------------------------
// Gradient propagation cap: AIWarrush stores Uint8 gradients with 255 as the
// "source" marker (water tiles, resource tiles) and 0/1 elsewhere before
// calling Map::updateGlobalGradient().
//
// AI_WARRUSH_WATER_NEAR_OFFSET is the slack below the cap that still counts
// as "near water" when picking wood/wheat plant locations:
//     water_gradient(x, y) > (AI_WARRUSH_GRADIENT_MAX - AI_WARRUSH_WATER_NEAR_OFFSET)
// i.e. > 240 in the original code. Both constants are kept separate so the
// 255-15 arithmetic stays visible at the call site.
// ---------------------------------------------------------------------------
static constexpr int AI_WARRUSH_GRADIENT_MAX        = 255;
static constexpr int AI_WARRUSH_WATER_NEAR_OFFSET   = 15;

// ---------------------------------------------------------------------------
// "Heavily worked" swarm/inn fudge: a swarm or inn whose stored CORN exceeds
// (wished * 2/3) is counted as fully-worked even if its worker slot is empty.
// Numerator and denominator kept separate to preserve the literal `*2/3`.
// ---------------------------------------------------------------------------
static constexpr int AI_WARRUSH_HEAVILY_WORKED_RATIO_NUM = 2;
static constexpr int AI_WARRUSH_HEAVILY_WORKED_RATIO_DEN = 3;

// ---------------------------------------------------------------------------
// Hunger predicate divisor. Bug L10: the function is named
// `isAnyUnitWithLessThanOneThirdFood` but the divisor is 2, so it actually
// fires at the half-food mark. The function name is intentionally NOT
// changed here -- only the literal is named, the bug is preserved.
// See bugs_surfaced_during_magic_number_audit.md L10.
// ---------------------------------------------------------------------------
static constexpr int AI_WARRUSH_HUNGRY_THRESHOLD_DIVISOR = 2;

// ---------------------------------------------------------------------------
// Building placement geometry used in buildBuildingOfType():
//   isHardSpaceForBuilding(x - bt->width/2,
//                          y - bt->width/2,    // (sic; original uses width here too)
//                          bt->width  * 2,
//                          bt->height * 2)
// Splits the divisor and the clearance multiplier so reviewers can see the
// "look one half-width up-and-left, then check a 2x clearance box" intent.
// ---------------------------------------------------------------------------
static constexpr int AI_WARRUSH_BUILDING_CENTER_DIVISOR = 2;
static constexpr int AI_WARRUSH_BUILDING_CLEARANCE_MULT = 2;
