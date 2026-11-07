// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <cstdint>

// Stable serialized identities, stored once per map vertex. Each type's group (and
// so its behaviour) lives in TerrainGroupTable.h, its names and colours in
// TerrainCatalog.cpp; append new identities before TERRAIN_COUNT and record the
// previous count in io/TerrainFormatHistory.h.
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
	// A crevassed glacier tongue on fluted blue ice walls (Obstacles, like cliffs), format 212.
	ICEFALL=55,
	// Flat-topped sandstone benches on stepped beds (Obstacles, like cliffs), format 212.
	SANDSTONE_BENCHES=56,
	// A heap of glassy black obsidian shards (Obstacles, like cliffs), format 212.
	OBSIDIAN_RIDGE=57,
	// Rounded, mossy granite blocks (Obstacles, like cliffs), format 212.
	GRANITE_TORS=58,
	// Calcite terraces holding milky pools (Obstacles, like cliffs), format 212.
	TRAVERTINE_TERRACES=59,
	// Fields of snow-ice blades (Ridges, like rock spires), format 212.
	PENITENTES=60,
	// Tall red-earth termite mounds (Ridges, like rock spires), format 212.
	TERMITE_MOUNDS=61,
	// Banded sandstone pillars under dark caprocks (Ridges, like rock spires), format 212.
	HOODOOS=62,
	// Snapped stone stumps with growth rings (Ridges, like rock spires), format 212.
	PETRIFIED_STUMPS=63,
	TERRAIN_COUNT=64,
};
// Not an identity: the answer of per-cell type queries for a cell whose four
// corners hold different terrains. It is never stored.
inline constexpr TerrainType MIXED_TERRAIN = TerrainType(0xFFFF);
