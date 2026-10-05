// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <cstdint>

// Stable serialized identities. Sprite frames are deliberately not terrain IDs.
enum TerrainType : std::uint16_t
{
	WATER=0,
	SAND=1,
	GRASS=2,
	ICE=3,
	ROAD=4,
	// Compatibility profiles for old corner-based shores, not paintable types.
	GRASS_SAND_SHORE=5,
	SAND_WATER_SHORE=6,
	TERRAIN_COUNT=7,
};
