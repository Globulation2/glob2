// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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
// Format 211 ended the built-ins at GLACIER; format 212 added ICEFALL through PETRIFIED_STUMPS.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_ICEFALL = 55;
// Formats 212 to 215 ended the built-ins at PETRIFIED_STUMPS.
inline constexpr unsigned TERRAIN_COUNT_BEFORE_STEPPE_GRASS = 64;
