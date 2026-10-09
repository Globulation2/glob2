// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <cstdint>

// Stable serialized identities, stored once per map vertex. Behaviour, names and
// colours live in TerrainTypeTable.h; append new identities before TERRAIN_COUNT.
// Format 146 retired the two shore profiles (5 and 6) and renumbered the
// catalogue behind TRAIL; files written earlier are remapped on load.
enum TerrainType : std::uint16_t
{
	WATER=0,
	SAND=1,
	GRASS=2,
	ICE=3,
	TRAIL=4,
	// Terrain catalogue. Groups are defined in TerrainGroup.h.
	BOULDERS=5,
	HEDGE=6,
	THICKET=7,
	RIDGE_ROCK=8,
	OUTCROP=9,
	DIRT=10,
	CLAY=11,
	GRAVEL=12,
	FLOWER_MEADOW=13,
	MUD=14,
	MARSH=15,
	DEEP_SNOW=16,
	SCREE=17,
	DIRT_TRACK=18,
	BOARDWALK=19,
	LAVA=20,
	EMBER_FIELD=21,
	LOAM=22,
	MOSS=23,
	SPRING_MEADOW=24,
	DEEP_WATER=25,
	DARK_WATER=26,
	VOID_HOLE=27,
	CHASM=28,
	TERRAIN_COUNT=29,
};
// Built-in counts of older files. Their custom definitions and terrain IDs start
// at the count they were written with and are remapped on load
// (TerrainRegistry::currentTerrainId).
// Before format 141 there were seven built-ins, two of them shore profiles.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_CATALOGUE = 7;
// Formats 141 to 145 still numbered the two shore profiles as 5 and 6.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_VERTEX = 31;
// Not an identity: the answer of per-cell type queries for a cell whose four
// corners hold different terrains. It is never stored.
inline constexpr TerrainType MIXED_TERRAIN = TerrainType(0xFFFF);
