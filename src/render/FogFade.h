// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_stdinc.h>

#include <cstddef>
#include <vector>

class SceneMap;

//! How far each tile of one view has faded into the fog of war, 0 (in sight) to
//! 255 (fully fogged). The simulation only knows fogged or in sight, and its fog
//! buffers swap every FOW_SWITCH_TICK_MASK + 1 ticks, so every tile that left sight
//! during a window darkens on the same tick. This field turns those steps into
//! fades: a tile that changes state moves linearly towards its new state, slowly
//! into the fog and quickly out of it. Time is game time in ticks, with the
//! fraction of the tick interval elapsed, so fades stop while paused and follow
//! the game speed. Presentation only: the simulation never sees it.
class FogFade
{
public:
	//! Ticks to fade fully into the fog (about a second) and fully out of it.
	static constexpr int DARKEN_TICKS = 25;
	static constexpr int REVEAL_TICKS = 4;
	//! A tick jump longer than this (a load, a seek, a fast-forward) settles every
	//! tile in its state instead of fading it.
	static constexpr Uint32 SETTLE_JUMP_TICKS = 64;
	static constexpr Uint8 FOGGED = 255;

	//! Follow the scene's fog for the teams in visibleTeams as of `tick`. Tiles that
	//! changed since the previous tick start fading from that tick; a new map, other
	//! teams, or a jump in time settle every tile at once.
	void update(const SceneMap &map, Uint32 visibleTeams, Uint32 tick);
	//! The same over a w x h map (powers of two) whose tile at index i is fogged
	//! when isFogged(i); for callers without a SceneMap.
	template <typename Fogged>
	void update(int w, int h, Uint64 mapIdentity, Uint32 visibleTeams, Uint32 tick, Fogged isFogged);

	//! Fade of the tile at (x, y), wrapped onto the map, at game time `now`: the
	//! drawn scene's tick plus the elapsed fraction of the tick interval.
	Uint8 level(int x, int y, double now) const
	{
		return levelAt((size_t(y & (h - 1)) * size_t(w)) + size_t(x & (w - 1)), now);
	}
	//! Whether update has seen a map.
	bool ready() const { return w > 0; }

	//! Opacity of the fog shade at `level`, as an alpha out of 255; FOGGED gives
	//! SHADE_ALPHA, the darkness of the shade sprites and of the plain fog fill.
	static Uint8 fillAlpha(Uint8 level);
	//! The alpha to draw a shade sprite (whose own alpha peaks at SHADE_ALPHA) with,
	//! so that it darkens a tile from one level to another `rise` levels higher. Fills
	//! and sprites stacked this way compose exactly to fillAlpha of the top level.
	static Uint8 layerAlpha(Uint8 rise);
	static constexpr Uint8 SHADE_ALPHA = 127;

private:
	Uint8 levelAt(size_t index, double now) const;
	void settle(Uint32 tick);

	int w = 0, h = 0;
	Uint64 identity = 0;
	Uint32 teams = 0, lastTick = 0;
	//! Per tile: whether the tile is fogged now, its level when that last changed,
	//! and the tick it changed on.
	std::vector<Uint8> fogged, startLevel;
	std::vector<Uint32> startTick;
};

template <typename Fogged>
void FogFade::update(int newW, int newH, Uint64 mapIdentity, Uint32 visibleTeams, Uint32 tick, Fogged isFogged)
{
	const size_t size = size_t(newW) * size_t(newH);
	const bool sameMap = w == newW && h == newH && identity == mapIdentity && teams == visibleTeams;
	if (sameMap && tick == lastTick)
		return;
	if (!sameMap || tick < lastTick || tick - lastTick > SETTLE_JUMP_TICKS)
	{
		w = newW;
		h = newH;
		identity = mapIdentity;
		teams = visibleTeams;
		fogged.resize(size);
		for (size_t i = 0; i < size; i++)
			fogged[i] = isFogged(i) ? 1 : 0;
		settle(tick);
		return;
	}
	// A tile changing state starts from wherever its fade had reached on this tick.
	for (size_t i = 0; i < size; i++)
	{
		const Uint8 f = isFogged(i) ? 1 : 0;
		if (f == fogged[i])
			continue;
		startLevel[i] = levelAt(i, double(tick));
		startTick[i] = tick;
		fogged[i] = f;
	}
	lastTick = tick;
}
