// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Small map vocabulary shared by code that does not need the Map class itself:
// the empty-slot identifiers, neighbourhood offsets and area kinds.

//! No global unit identifier. This value means there is no unit. Used at Tile::groundUnit or Tile::airUnit.
#define NOGUID 0xFFFF

//! No global building identifier. This value means there is no building. Used at Tile::building.
#define NOGBID 0xFFFF

//! 2D grid offset returned by Map's 3x3-neighborhood "doesTouch" queries.
//! dx and dy are each in {-1, 0, +1}.
struct Offset
{
	int dx;
	int dy;
};

/// Types of areas
enum AreaType
{
	ClearingArea = 0,
	ForbiddenArea,
	GuardArea,
	FarmArea
};
