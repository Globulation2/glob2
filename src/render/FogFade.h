// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_stdinc.h>

#include <cassert>
#include <cstddef>
#include <vector>

class SceneMap;

//! How far each tile of one view has faded into the fog of war, from 0 (in sight)
//! to FOGGED (fully fogged). Presentation only: the simulation never reads it.
//!
//! The simulation's fog is binary per tile, and its two buffers swap every
//! FOW_SWITCH_TICK_MASK + 1 ticks (Map::switchFogOfWar), so every tile that left
//! sight during one window turns fogged on the same tick. Drawn as is, that is a
//! jolt across the whole map about once a second. This field turns each change of
//! a tile into a linear fade from wherever the tile's fade had reached: slowly
//! into the fog (DARKEN_TICKS) and quickly out of it (REVEAL_TICKS), so new sight
//! still feels immediate.
//!
//! Time is game time in ticks: the drawn Scene's tick plus the fraction of the tick
//! interval that has elapsed since (unitMotionFraction). Fades therefore stop while
//! the game is paused and follow the game speed.
//!
//! Usage, once per drawn frame: update() with the drawn Scene, then level() for the
//! tiles being drawn. reset() when the fade is not drawn, so that its next use
//! starts settled instead of fading through changes it did not see.
class FogFade
{
public:
	//! Ticks to fade fully into the fog (1.5 seconds at the default speed) and
	//! fully out of it.
	static constexpr double DARKEN_TICKS = 37.5;
	static constexpr int REVEAL_TICKS = 4;
	//! A jump forward of more than this many ticks (a load, a minimised window, a
	//! fast-forward) settles every tile in its state instead of fading it.
	static constexpr Uint32 SETTLE_JUMP_TICKS = 64;
	//! The level of a fully fogged tile.
	static constexpr Uint8 FOGGED = 255;
	//! Alpha of the plain fog shade, and the peak alpha of the shade sprite's pixels.
	static constexpr Uint8 SHADE_ALPHA = 127;

	//! Follow the fog of the drawn Scene, for the teams whose sight the viewer
	//! shares, as of the Scene's `tick`; `time` is the game time this frame is drawn
	//! at (tick plus the elapsed fraction of the tick interval).
	void update(const SceneMap &map, Uint32 visibleTeams, Uint32 tick, double time);
	//! The same over a w x h map (both powers of two) whose fog of war is `fog`,
	//! one team mask per tile at y * w + x; for callers without a SceneMap.
	void update(int w, int h, Uint64 mapIdentity, Uint32 visibleTeams, Uint32 tick, double time, const Uint32 *fog);
	//! Forget every tile; the next update settles them instead of fading them.
	void reset() { *this = FogFade(); }
	//! Whether update has been called since construction or the last reset.
	bool active() const { return w > 0; }

	//! Fade of the tile at (x, y), wrapped onto the map like SceneMap's
	//! coordinates, at the time of the last update.
	Uint8 level(int x, int y) const
	{
		assert(active());
		return levelAt(size_t(y & (h - 1)) * size_t(w) + size_t(x & (w - 1)), now);
	}

	//! Alpha of a plain fill shading a tile at `level`: 0 in sight, SHADE_ALPHA fully
	//! fogged. See FogFade.cpp for the curve.
	static Uint8 fillAlpha(Uint8 level);
	//! Alpha to draw a shade sprite with (its own pixels peak at SHADE_ALPHA) so that
	//! it darkens a tile `rise` levels further. A fill at one level with such layers
	//! stacked on it composes to the fill of the top level, within rounding, where
	//! the sprite's pixels are at their peak.
	static Uint8 layerAlpha(Uint8 rise);

private:
	//! Level of the tile at `index` at game time `time`.
	Uint8 levelAt(size_t index, double time) const;
	//! Put every tile in its state at `tick`, with no fade in progress.
	void settle(Uint32 tick);

	int w = 0, h = 0;
	//! What the fades were computed from: a change of map or of the teams whose
	//! sight is shown makes earlier fades meaningless, so it settles.
	Uint64 identity = 0;
	Uint32 teams = 0;
	//! The last Scene tick seen, and the game time of the last update.
	Uint32 lastTick = 0;
	double now = 0;
	//! Per tile: whether the tile is fogged, its level when that last changed, and
	//! the tick it changed on. Its level at time t moves from startLevel towards 0
	//! or FOGGED by the time elapsed since startTick.
	std::vector<Uint8> fogged, startLevel;
	std::vector<Uint32> startTick;
};
