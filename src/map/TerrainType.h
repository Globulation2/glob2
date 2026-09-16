// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

enum TerrainType
{
	WATER=0,
	SAND=1,
	GRASS=2,
	// Prototype terrains. Their tiles use their own sprite ranges and no transition art
	// (see Map::regenerateMap); a tile with any ice corner is ice.
	ICE=3,
	COBBLESTONE=4,
};

static constexpr int TERRAIN_TYPE_COUNT = 5;

