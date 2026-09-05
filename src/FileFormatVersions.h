// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// FileFormatVersions.h
//
// Save-format minor-version gates and 4-byte ASCII section signatures used
// throughout the save / replay loaders. Every `if (versionMinor >= N)` /
// `< N` comparison in Game_io / GameHeader / Player / Lifecycle / etc. has
// a corresponding named constant here, so that adding a new save-format
// feature only requires touching this one file plus the consumer.
//
// Per-AI minor-version gates (Nicowar's 59 / 60 / 66, etc.) live in their
// own AI tuning headers so that AI internals do not bleed into the
// engine-wide loader.

#pragma once

// === Save-format minor-version feature gates ===
// Each constant is the FIRST versionMinor at which the named feature
// appears in the save stream. Loaders write/read the new field if
// versionMinor >= FILE_FORMAT_VERSION_*; otherwise they fall back to the
// pre-feature default. VALUES MUST NEVER MOVE — they are wire-locked.

//! Building::underAttackTimer added (Lifecycle.cpp:201; UnitSerialization.cpp:62).
static constexpr int FILE_FORMAT_VERSION_UNDER_ATTACK_TIMER = 61;

//! Pre-fertility marker — saves before this lacked the fertility map.
static constexpr int FILE_FORMAT_VERSION_PRE_FERTILITY = 63;

//! Unified seed introduced; "GaBt" sig appeared (Game_io.cpp:134, 146, 417;
//! GameHeader.cpp:54, 75, 77, 129, 150, 152).
static constexpr int FILE_FORMAT_VERSION_UNIFIED_SEED = 64;

//! Building::maxUnitWorkingPrevious field added (Lifecycle.cpp:423).
static constexpr int FILE_FORMAT_VERSION_MAX_UNIT_WORKING_PREVIOUS = 65;

//! Building::maxUnitWorkingFuture field added (Lifecycle.cpp:427).
static constexpr int FILE_FORMAT_VERSION_MAX_UNIT_WORKING_FUTURE = 70;

//! Allies + winning conditions added (GameHeader.cpp:54, 129).
static constexpr int FILE_FORMAT_VERSION_ALLIES_AND_WIN_CONDITIONS = 71;

//! mapDiscovered flag added (GameHeader.cpp:77, 152).
static constexpr int FILE_FORMAT_VERSION_MAP_DISCOVERED_FLAG = 72;

//! Team::race field added (TeamSerialization.cpp:135).
static constexpr int FILE_FORMAT_VERSION_RACE_FIELD = 73;

//! Building::unitsFailingRequirements stored as int (Lifecycle.cpp:432).
//! Predates the array form (see _ARRAY below).
static constexpr int FILE_FORMAT_VERSION_UNITS_FAILING_REQUIREMENTS_INT = 74;

//! Campaign text / objectives section added (Game_io.cpp:204).
static constexpr int FILE_FORMAT_VERSION_CAMPAIGN_TEXT_OBJECTIVES = 75;

//! Briefing + hints + objective `failed` flag (Game_io.cpp:215;
//! GameObjectives.cpp:225).
static constexpr int FILE_FORMAT_VERSION_BRIEFING_HINTS_OBJ_FAILED = 76;

//! Building::unitsFailingRequirements promoted from int to array
//! (Lifecycle.cpp:432).
static constexpr int FILE_FORMAT_VERSION_UNITS_FAILING_REQUIREMENTS_ARRAY = 77;

//! OrderCreate payload grew 20 -> 28 bytes to carry flagRadius
//! (OrderBuilding.cpp:47-49).
static constexpr int FILE_FORMAT_VERSION_ORDER_CREATE_FLAG_RADIUS = 78;

//! Building::priority field added (Lifecycle.cpp:211).
static constexpr int FILE_FORMAT_VERSION_BUILDING_PRIORITY_FIELD = 79;

//! Building::unitsHarvesting list added (Lifecycle.cpp:459).
static constexpr int FILE_FORMAT_VERSION_UNITS_HARVESTING_LIST = 80;

//! Building::canNotConvertUnitTimer added (Lifecycle.cpp:205, 208).
static constexpr int FILE_FORMAT_VERSION_CANNOT_CONVERT_TIMER = 81;

//! USL mapscript serialization (Game_io.cpp:197).
static constexpr int FILE_FORMAT_VERSION_USL_MAPSCRIPT = 82;

//! Drop unit-skin name into the save stream (UnitSerialization.cpp:25).
static constexpr int FILE_FORMAT_VERSION_DROP_UNIT_SKIN_NAME = 84;

// === Save-file section signatures (4-byte ASCII tags) ===
// Embedded as four chars at the start of each save section so a corrupted
// stream fails fast. NEVER change these values — old saves on disk depend
// on them.

//! Length in bytes of every section signature above.
static constexpr int FILE_SIG_LEN = 4;

//! Top-level "Game Begin" sig (Game_io.cpp:128, 415).
inline constexpr const char FILE_SIG_GAME_BEGIN[5]   = "GaBe";
//! "Game Sync" — pre-step state (Game_io.cpp:141, 428).
inline constexpr const char FILE_SIG_GAME_SYNC[5]    = "GaSy";
//! "Game Built" — post-construction marker for unified-seed saves
//! (Game_io.cpp:146).
inline constexpr const char FILE_SIG_GAME_BUILT[5]   = "GaBt";
//! "Game Team" sig (Game_io.cpp:160, 435).
inline constexpr const char FILE_SIG_GAME_TEAM[5]    = "GaTe";
//! "Game Map" sig (Game_io.cpp:167, 446).
inline constexpr const char FILE_SIG_GAME_MAP[5]     = "GaMa";
//! "Game Player" sig (Game_io.cpp:180).
inline constexpr const char FILE_SIG_GAME_PLAYER[5]  = "GaPl";
//! Player section "begin" sig (Player.cpp:107).
inline constexpr const char FILE_SIG_PLAYER_BEGIN[5] = "PLYb";
//! Player section "end" sig (Player.cpp:152).
inline constexpr const char FILE_SIG_PLAYER_END[5]   = "PLYe";
//! Game checksum sidecar v1 magic (ChecksumSidecar.cpp:41).
inline constexpr const char FILE_SIG_CHECKSUM_SIDECAR[5] = "GCS1";

//! SHA-1 hash byte length, used by the checksum sidecar (Game_io.cpp:459-461).
static constexpr int SHA1_BYTE_LEN = 20;

