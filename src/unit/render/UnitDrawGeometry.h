// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

//! Viewport-relative tile a unit's sprite is anchored on, along one wrapped axis.
//!
//! \param slotTile viewport-relative tile the draw loop found the unit's map slot on
//! \param viewportStart map coordinate of the viewport's first tile on this axis
//! \param unitPos the unit's own map coordinate on this axis (Unit::posX / posY)
//! \param mapSize the map's extent on this axis, always a power of two
//!
//! The two tiles normally coincide: every unit action clears the old map slot
//! and claims the destination one, so the visited tile already is the unit's
//! position. Entering a building is the deliberate exception — the map slot
//! stays on the tile the unit is leaving so it remains drawable, while
//! posX/posY already name the building tile (Unit::handleActionEnteringBuilding).
//! Anchoring on the visited tile alone would then run the arrival interpolation
//! one square too early, so the signed wrapped offset between the two is folded
//! back in.
//!
//! Working from the visited occurrence, rather than converting unitPos afresh,
//! is what keeps both copies of a unit standing on a map seam: a wrapped tile
//! visible at each screen edge is visited twice and must be drawn twice.
inline int unitDrawTile(int slotTile, int viewportStart, int unitPos, int mapSize)
{
	int off = (slotTile + viewportStart - unitPos) & (mapSize - 1);
	if (off > (mapSize >> 1))
		off -= mapSize;
	return slotTile - off;
}
