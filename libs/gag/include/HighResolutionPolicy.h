// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "DetailHysteresis.h"
#include <cstdint>

namespace GAGCore
{
//! The HD sprite policy (docs/architecture/rendering.md#hd-sprite-policy): the
//! one rule every renderer asks whether a view draws sprite frames from their
//! HD layers, and at which resolution.
//!
//! HD frames hold four texels for each logical pixel. A view's density is the
//! screen pixels one logical pixel covers (map scale times the display's
//! raster scale). Above one, native frames would be magnified, so HD frames
//! draw: their half-resolution level (two texels a logical pixel) up to a
//! density of two, the full frames beyond. At or below one, native frames draw;
//! HD would only be shrunk back to them. Higher detail is taken at once, lower
//! detail only after the view has wanted it for the hysteresis delay (ten
//! seconds in the game), so zooming across a threshold and back reloads
//! nothing. The terrain pages' HD materials use the same threshold.
//!
//! Wherever HD is wanted but a frame's HD layers are not all there (still
//! streaming, evicted, failed or missing from the pack, or the HD setting is
//! off) the native frame draws: never nothing, and never a wait.
class HighResolutionPolicy
{
  public:
	enum Level : unsigned
	{
		FullHD = 0, //!< Full HD frames: four texels a logical pixel.
		HalfHD = 1, //!< HD frames reduced by two: two texels a logical pixel.
		Native = 2, //!< Native frames.
	};
	//! The level a view of `density` screen pixels a logical pixel wants.
	static constexpr Level levelFor(double density)
	{
		return density > 2 ? FullHD : density > 1 ? HalfHD : Native;
	}
	//! A view's density: its map scale times the raster scale, the drawable's
	//! pixels for each logical point (GraphicContext::getRasterScale, so a 2x
	//! display counts twice whatever the renderer), and never less than the
	//! raster scale, at which the interface draws sprites.
	static constexpr double density(double mapScale, double rasterScale)
	{
		return mapScale * rasterScale > rasterScale ? mapScale * rasterScale : rasterScale;
	}
	//! Whether a view of `density` wants HD at all (terrain pages ask this).
	static constexpr bool wantsHighResolution(double density) { return levelFor(density) != Native; }

	explicit HighResolutionPolicy(Level initial = Native) : detail(initial) {}
	//! Follows a view's density, on the steady clock (`now` and `delay` in
	//! nanoseconds); returns whether the level changed.
	bool update(double density, std::int64_t now, std::int64_t delay)
	{
		return detail.update(levelFor(density), now, delay);
	}
	//! Starts at `level` with no pending change (a new view, a test).
	void reset(Level level) { detail = DetailHysteresis(level); }
	//! The level the view draws: with the HD setting off, always Native.
	Level level(bool enabled = true) const { return enabled ? Level(detail.level()) : Native; }
	bool shown(bool enabled = true) const { return level(enabled) != Native; }
	//! Mip levels below the full HD frame the view samples (0 or 1); the
	//! native level is two below it.
	unsigned reduction() const { return detail.level(); }

	//! Which layers one frame draws. A frame draws HD only when the view
	//! shows HD and every layer its native frame has (base and team colour) has
	//! an HD layer ready: an HD base under a native team layer, or the
	//! reverse, shows the coarse layer's blocks at its edges.
	static bool frameDrawsHighResolution(bool shown, bool nativeBase, bool nativeTeam, bool hdBase, bool hdTeam)
	{
		return shown && (hdBase || hdTeam) && (!nativeBase || hdBase) && (!nativeTeam || hdTeam);
	}

  private:
	DetailHysteresis detail;
};
} // namespace GAGCore
