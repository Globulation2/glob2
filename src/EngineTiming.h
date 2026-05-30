// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// EngineTiming.h
//
// Cross-slice engine cadence constants. The engine ticks at a fixed rate
// (GAME_TICKS_PER_SECOND) and every tick is GAME_TICK_MS milliseconds long.
// Anything in the simulation expressed in "ticks" (cooldowns, timers,
// refresh intervals, event ages) is gated by these values, so they live in
// one shared header to avoid drift between Engine, Map, Team, Building,
// Unit, and AI subsystems.

#pragma once

// === Engine cadence ===

//! Engine fixed-step rate. The simulation advances exactly this many ticks
//! per real-time second. EngineRun.cpp's main loop is built around this.
static constexpr int GAME_TICKS_PER_SECOND = 25;

//! Length of a single engine tick in milliseconds. Equal to
//! 1000 / GAME_TICKS_PER_SECOND. Used by Engine.cpp / EngineInit.cpp /
//! EngineRun.cpp for sleep budgeting and frame pacing.
static constexpr int GAME_TICK_MS = 40;

//! Maximum amount of accumulated lag (in milliseconds) the engine will try
//! to catch up by running ticks back-to-back without sleeping. Beyond this
//! the engine drops the excess instead of spiral-of-death-ing. See
//! EngineRun.cpp.
static constexpr int MAX_CATCHUP_MS = 500;

//! Tick interval (ms) the engine targets while replaying with fast-forward
//! enabled. Pairs with REPLAY_FAST_FORWARD_DRAW_RATIO so the GUI is drawn
//! once per N game-steps. ~3.33x normal speed at GAME_TICK_MS=40. See
//! EngineRun.cpp.
static constexpr int REPLAY_FAST_FORWARD_MS = 12;

//! During replay fast-forward, draw 1 frame per (RATIO+1) simulation steps.
//! Encoded in the loop as `nextGuiStep = REPLAY_FAST_FORWARD_DRAW_RATIO - 1`
//! after each draw, so the GUI updates every (RATIO+1)-th tick. See
//! EngineRun.cpp.
static constexpr int REPLAY_FAST_FORWARD_DRAW_RATIO = 3;

// === Engine init-time constants ===

//! Number of selectable AI implementations picked from when generating a
//! random matchup. The pick is `syncRand() % AI_RANDOM_PICK_COUNT + 1`,
//! skipping AI::NONE=0. Tracks the count of real AIs (AINumbi / AICastor /
//! AIWarrush / AIReachToInfinity / AINicowar) = AI::SIZE - 1.
//! See EngineLoaders.cpp.
static constexpr int AI_RANDOM_PICK_COUNT = 5;

//! Bitmask value meaning "every team is visible" for replay viewing. Used
//! as the initial value of GlobalContainer::replayVisibleTeams (a Uint32
//! per-team bitmask). See EngineInit.cpp.
static constexpr unsigned int REPLAY_VISIBLE_TEAMS_ALL = 0xFFFFFFFFu;

// === Per-team / per-unit gameplay timers (in ticks) ===

//! How long a unit / building stays flagged as "under attack" after taking
//! damage. ~9.6 s at 25 TPS. Drives the under-attack icon, defensive flag
//! retargeting, and event throttling. See Unit.cpp / Building.h.
static constexpr int UNDER_ATTACK_TIMER_TICKS = 240;

//! Initial value for Building::canNotConvertUnitTimer when a building
//! cannot recruit a unit; ticked down each step. ~6 s. See
//! Construction.cpp / building/Lifecycle.cpp.
static constexpr int CANNOT_CONVERT_TIMER_INIT = 150;

// === Map mark / event lifetimes (in ticks) ===

//! Default time-to-live for a player-placed map mark before it disappears.
//! ~2 s. See MarkManager.cpp.
static constexpr int MARK_DEFAULT_LIFETIME_TICKS = 50;

//! Per-event-type cooldown applied to GameEvent emission. Drops repeated
//! events of the same type for ~2 s after the previous one fired.
//! NOTE: Team::wasRecentEvent uses == against this exact value, so this
//! literal is structurally coupled — see bug #8 in the glossary.
static constexpr int GAME_EVENT_COOLDOWN_TICKS = 50;

//! Maximum age (~4 s) of a GameEvent kept in Team's event list. Older
//! events are discarded when the list is updated. See Team.cpp.
static constexpr int GAME_EVENT_MAX_AGE_TICKS = 100;

// === Map / minimap refresh intervals (in ticks) ===

//! Minimap full-redraw cadence — exactly one redraw per second at 25 TPS.
//! See Minimap.cpp.
static constexpr int MINIMAP_REFRESH_TICKS = 25;

//! Clearing-flag local-resources gradient refresh cadence. ~5 s. See
//! TypeSteps.cpp.
static constexpr int CLEARING_FLAG_REFRESH_TICKS = 125;

