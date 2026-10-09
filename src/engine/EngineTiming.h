// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// EngineTiming.h
//
// Cross-slice engine cadence constants. The engine ticks at a fixed rate
// (GAME_TICKS_PER_SECOND) with nanosecond deadlines. GAME_TICK_MS is only
// a rounded UI interval.
// Anything in the simulation expressed in "ticks" (cooldowns, timers,
// refresh intervals, event ages) is gated by these values, so they live in
// one shared header to avoid drift between Engine, Map, Team, Building,
// Unit, and AI subsystems.

#pragma once

// === Engine cadence ===

//! Engine fixed-step rate. The simulation advances exactly this many ticks
//! per real-time second. EngineRun.cpp's main loop is built around this.
static constexpr int GAME_TICKS_PER_SECOND = 30;

//! Normal-speed duration. Accumulate nanoseconds before rounding host waits to
//! milliseconds, so 30 TPS alternates 33/34 ms waits rather than running at 30.3 TPS.
static constexpr unsigned long long GAME_TICK_NS = 1000000000ULL / GAME_TICKS_PER_SECOND;

//! Rounded interval for millisecond-only UI and presentation APIs, not pacing.
static constexpr int GAME_TICK_MS = int(GAME_TICK_NS / 1000000ULL);

//! Maximum amount of accumulated lag (in milliseconds) the engine will try
//! to catch up by running ticks back-to-back without sleeping. Beyond this
//! the engine drops the excess instead of spiral-of-death-ing. See
//! EngineRun.cpp.
static constexpr int MAX_CATCHUP_MS = 500;

//! Turn games: the longest the host loop sleeps between polls of the relay
//! connection while waiting for the next tick. Bundle arrival times are what the
//! jitter buffer sizes itself from, so they must not be rounded up to whole frames.
static constexpr unsigned TURN_POLL_MS = 5;

//! Tick interval (ms) the engine targets while replaying with fast-forward
//! enabled: zero means uncapped, the simulation runs as fast as the CPU
//! allows. Pairs with REPLAY_FAST_FORWARD_DRAW_RATIO so the GUI is still
//! drawn once per N game-steps. See EngineRun.cpp.
static constexpr int REPLAY_FAST_FORWARD_MS = 0;

//! During replay fast-forward, draw 1 frame per RATIO simulation steps.
//! Encoded in the loop as `nextGuiStep = REPLAY_FAST_FORWARD_DRAW_RATIO - 1`
//! after each draw, so the GUI updates every RATIO-th tick. See
//! EngineRun.cpp.
static constexpr int REPLAY_FAST_FORWARD_DRAW_RATIO = 16;

// === Engine init-time constants ===

//! Number of selectable AI implementations picked from when generating a
//! random matchup. The pick is `seatRandom.nextU32() % AI_RANDOM_PICK_COUNT + 1`,
//! skipping AI::NONE=0. Covers the five shipping AIs AINumbi / AICastor /
//! AIWarrush / AIEcono / AINicowar (ids 1..5). It is intentionally
//! NOT AI::SIZE - 1: the experimental AICortex scaffold (id 6) is excluded
//! from random matchups, and widening this count would also shift the
//! private seat selection and break replay determinism.
//! See EngineLoaders.cpp.
static constexpr int AI_RANDOM_PICK_COUNT = 5;

//! Bitmask value meaning "every team is visible" for replay viewing. Used
//! as the initial value of GlobalContainer::replayVisibleTeams (a Uint32
//! per-team bitmask). See EngineInit.cpp.
static constexpr unsigned int REPLAY_VISIBLE_TEAMS_ALL = 0xFFFFFFFFu;

// === Per-team / per-unit gameplay timers (in ticks) ===

//! How long a unit / building stays flagged as "under attack" after taking
//! damage. ~8 s at 30 TPS. Drives the under-attack icon, defensive flag
//! retargeting, and event throttling. See Unit.cpp / Building.h.
static constexpr int UNDER_ATTACK_TIMER_TICKS = 240;

//! Initial value for Building::canNotConvertUnitTimer when a building
//! cannot recruit a unit; ticked down each step. ~5 s. See
//! Construction.cpp / building/Lifecycle.cpp.
static constexpr int CANNOT_CONVERT_TIMER_INIT = 150;

// === Map mark / event lifetimes (in ticks) ===

//! Default time-to-live for a player-placed map mark before it disappears.
//! ~1.7 s. See MarkManager.cpp.
static constexpr int MARK_DEFAULT_LIFETIME_TICKS = 50;

//! Per-event-type cooldown applied to GameEvent emission. Drops repeated
//! events of the same type for ~1.7 s after the previous one fired.
//! NOTE: Team::wasRecentEvent uses == against this exact value, so this
//! literal is structurally coupled — see bug #8 in the glossary.
static constexpr int GAME_EVENT_COOLDOWN_TICKS = 50;

//! Maximum age (~3.3 s) of a GameEvent kept in Team's event list. Older
//! events are discarded when the list is updated. See Team.cpp.
static constexpr int GAME_EVENT_MAX_AGE_TICKS = 100;

// === Map / minimap refresh intervals (in ticks) ===

//! Minimap full-redraw cadence — about one redraw per 0.83 seconds at 30 TPS.
//! See Minimap.cpp.
static constexpr int MINIMAP_REFRESH_TICKS = 25;

//! Clearing-flag local-resources gradient refresh cadence. ~4.2 s. See
//! TypeSteps.cpp.
static constexpr int CLEARING_FLAG_REFRESH_TICKS = 125;

// === Autosave cadence (in ticks) ===

//! Ticks between autosaves at normal speed (60 s). Faster speed presets
//! scale it up to keep about the same real-time spacing. See GameGUIStep.cpp.
static constexpr int AUTOSAVE_INTERVAL_TICKS = 60 * GAME_TICKS_PER_SECOND;

//! A session's first autosave lands on this tick modulo the interval.
static constexpr int AUTOSAVE_PHASE_TICKS = 79;

//! A building's route field, once invalidated by a map change, is rebuilt on
//! its next use at most this often (~3.3 s). The map's topology generation
//! invalidates every field of every team on any structural change, so this
//! interval, not the invalidation, is what bounds the rebuild load.
//! Measured on gd-bigarena-long (Oazis, 11 teams) over 8000 ticks: at 25 the
//! topology generation costs +42.6% simulation time over a build without it;
//! at 100 that falls to +10.3% while the symptom it exists to fix stays fully
//! suppressed (0 stuck-unit forced rebuilds, against 195 without it). The fix
//! erodes past ~200 (57 stuck at 200, 76 at 400, converging on 195), so 100
//! sits inside the safe range rather than at its edge. Same shape on
//! gd-large-4ai and at 4000 ticks. Shared with
//! src/map/gradient/BuildingGradientInvalidationHarness.cpp, which has to let this
//! interval elapse before it can judge a field. See
//! MapPathfindBuilding.cpp.
static constexpr unsigned int GRADIENT_DIRTY_REBUILD_TICKS = 100;

