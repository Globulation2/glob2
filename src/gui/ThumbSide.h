// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ViewportTransform.h>

// Phone controls gather in the bottom corner under the player's thumb. Every
// corner-anchored component mirrors through these helpers, never on its own.
namespace ThumbSide
{
//! True when the player chose the left thumb (Settings::thumbSide).
bool left();
//! A w×h rectangle whose bottom edge is `bottom`, `inset` in from the thumb-side
//! edge of `within`.
inline GAGCore::ViewRect corner(GAGCore::ViewRect within, double w, double h, double inset, double bottom,
								bool onLeft)
{
	return {onLeft ? within.x + inset : within.x + within.w - inset - w, bottom - h, w, h};
}
} // namespace ThumbSide
