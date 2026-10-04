// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <vector>
#include "Types.h"

/// Pure radial-accumulation kernels shared by OverlayArea's heatmaps.
///
/// The field is a width*height grid indexed as field[x*height + y] and wraps
/// toroidally, matching the game map. Each kernel adds a radial "bump" that
/// peaks at the centre and falls off with the squared distance, and keeps
/// `max` updated with the running maximum so the renderer can normalise the
/// field to an alpha ramp.
///
/// The field element type is Uint32 (not Uint16): the Defence overlay scales
/// every increment by a turret's attack power, and a dense cluster of strong
/// turrets can accumulate well past 65535 on a single tile. A narrower field
/// would wrap around, corrupting both the tile and the normalisation maximum.
/// These functions have no dependency on Game/Map/Unit so they can be unit
/// tested directly.
namespace OverlayFill
{
	/// Accumulate an unweighted bump of the given radius. Used by the Starving
	/// and Damage unit overlays, where every unit contributes equally. Cells
	/// strictly inside the radius (relx*relx + rely*rely < distance*distance)
	/// are incremented.
	void increasePoint(int x, int y, int distance, int width, int height,
	                   std::vector<Uint32>& field, Uint32& max);

	/// Accumulate a bump scaled by `value`. Used by the Defence overlay, where
	/// `value` is a turret's attack power so stronger turrets paint a hotter
	/// footprint. Cells on or inside the radius
	/// (relx*relx + rely*rely <= distance*distance) are incremented by
	/// value * (distance - (relx*relx + rely*rely) / distance).
	void spreadPoint(int x, int y, int value, int distance, int width, int height,
	                 std::vector<Uint32>& field, Uint32& max);
}
