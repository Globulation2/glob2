// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <cstdint>

// Stable serialized identities. Sprite frames are deliberately not terrain IDs.
// Behaviour, names, colours and saved-frame contracts live in TerrainTypeTable.h;
// append new identities before TERRAIN_COUNT and never renumber existing ones.
enum TerrainType : std::uint16_t
{
	WATER=0,
	SAND=1,
	GRASS=2,
	ICE=3,
	TRAIL=4,
	// Compatibility profiles for old corner-based shores, not paintable types.
	GRASS_SAND_SHORE=5,
	SAND_WATER_SHORE=6,
	// Terrain catalogue (format 141). Groups are defined in TerrainGroup.h.
	BOULDERS=7,
	HEDGE=8,
	THICKET=9,
	RIDGE_ROCK=10,
	OUTCROP=11,
	DIRT=12,
	CLAY=13,
	GRAVEL=14,
	FLOWER_MEADOW=15,
	MUD=16,
	MARSH=17,
	DEEP_SNOW=18,
	SCREE=19,
	DIRT_TRACK=20,
	BOARDWALK=21,
	LAVA=22,
	EMBER_FIELD=23,
	LOAM=24,
	MOSS=25,
	SPRING_MEADOW=26,
	DEEP_WATER=27,
	DARK_WATER=28,
	VOID_HOLE=29,
	CHASM=30,
	TERRAIN_COUNT=31,
};
// Files older than format 141 were written when seven built-ins existed; their
// custom definitions and tile identities start at this count and are remapped on load.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_CATALOGUE = 7;
