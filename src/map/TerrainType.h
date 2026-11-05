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
	SHALE=12,
	HEATH=13,
	MUD=14,
	MUDFLAT=15,
	DEEP_SNOW=16,
	SCREE=17,
	BRICK_PATH=18,
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
	// Looks of classic grass and sand (format 191): same rules, other artwork.
	LUSH_GRASS=29,
	DRY_GRASS=30,
	DUNE_SAND=31,
	PALE_SAND=32,
	// Sandstone flagstones (Paths), pumice beside lava (Rough) and a frozen lake
	// (Ice), format 195.
	SANDSTONE=33,
	PUMICE=34,
	FROZEN_LAKE=35,
	// Snow carved into drifts (snow and ice, like deep snow), format 196.
	WIND_SWEPT_SNOW=36,
	// Ochre bedrock worn into rounded slabs (Barren, like dirt), format 197.
	WEATHERED_SANDSTONE=37,
	// Sand swept into wind ripples (Sand, like sand), format 198.
	WINDY_SAND_DUNE=38,
	// Pale baked crust in small cracked pillows (Rough, like mud), format 199.
	HARDPAN=39,
	// Cold heath (Barren), format 200.
	TUNDRA=40,
	// Dark volcanic ash (Barren, like dirt), format 201.
	VOLCANIC_ASH=41,
	// Cracked blue glacier ice (Ice, like ice), format 202.
	GLACIER_ICE=42,
	// Hexagonal setts reclaimed by moss (Paths, like road), format 203.
	OVERGROWN_ROAD=43,
	// Iron-red earth packed with hardened nodules (Barren, like dirt), format 204.
	LATERITE=44,
	// A raised rock plateau walled by cliffs (Obstacles, like hedge), format 206.
	CLIFFS=45,
	// Bright molten lava pool (Lava, like lava), format 207.
	MOLTEN_LAVA_POOL=46,
	// Rock paths (Paths, like road), format 208: blue-grey slate slabs in random
	// ashlar, polygonal basalt drifted with ash, and dressed scoria in basketweave.
	SLATE_ASHLAR=47,
	BASALT_CAUSEWAY=48,
	SCORIA_BLOCKS=49,
	// Ice paths (Paths, like road), format 209: ice blocks cut from the glacier in
	// ashlar, white marble lozenges with moonstone keystones, and frosted quartz
	// pavers in a pinwheel around ice squares, all drifting back under snow.
	RIME_ASHLAR=50,
	MOONSTONE_LOZENGE=51,
	FROST_PINWHEEL=52,
	// Snow-drifted frost-patterned ground (Barren, like dirt), format 210.
	ARCTIC_TUNDRA=53,
	// A raised glacier walled by blue ice (Obstacles, like cliffs), format 211.
	GLACIER=54,
	TERRAIN_COUNT=55,
};
// Built-in counts of older files. Their custom definitions and terrain IDs start
// at the count they were written with and are remapped on load
// (TerrainRegistry::currentTerrainId).
// Before format 141 there were seven built-ins, two of them shore profiles.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_CATALOGUE = 7;
// Formats 141 to 145 still numbered the two shore profiles as 5 and 6.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_VERTEX = 31;
// Formats 146 to 190 ended the built-ins at CHASM.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_GROUND_LOOKS = 29;
// Formats 191 to 194 ended the built-ins at PALE_SAND.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_SANDSTONE_PUMICE = 33;
// Format 195 ended the built-ins at FROZEN_LAKE.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_WIND_SWEPT_SNOW = 36;
// Format 196 ended the built-ins at WIND_SWEPT_SNOW.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_WEATHERED_SANDSTONE = 37;
// Format 197 ended the built-ins at WEATHERED_SANDSTONE.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_WINDY_SAND_DUNE = 38;
// Format 198 ended the built-ins at WINDY_SAND_DUNE.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_HARDPAN = 39;
// Format 199 ended the built-ins at HARDPAN.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_TUNDRA = 40;
// Format 200 ended the built-ins at TUNDRA.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_VOLCANIC_ASH = 41;
// Format 201 ended the built-ins at VOLCANIC_ASH.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_GLACIER_ICE = 42;
// Format 202 ended the built-ins at GLACIER_ICE.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_OVERGROWN_ROAD = 43;
// Format 203 ended the built-ins at OVERGROWN_ROAD.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_LATERITE = 44;
// Formats 204 and 205 ended the built-ins at LATERITE.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_CLIFFS = 45;
// Format 206 ended the built-ins at CLIFFS.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_MOLTEN_LAVA_POOL = 46;
// Format 207 ended the built-ins at MOLTEN_LAVA_POOL.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_ROCK_PATHS = 47;
// Format 208 ended the built-ins at SCORIA_BLOCKS.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_ICE_PATHS = 50;
// Format 209 ended the built-ins at FROST_PINWHEEL.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_ARCTIC_TUNDRA = 53;
// Format 210 ended the built-ins at ARCTIC_TUNDRA.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_GLACIER = 54;
// Not an identity: the answer of per-cell type queries for a cell whose four
// corners hold different terrains. It is never stored.
inline constexpr TerrainType MIXED_TERRAIN = TerrainType(0xFFFF);
