// SPDX-License-Identifier: GPL-3.0-or-later
#include "FogFade.h"

#include "scene/SceneMap.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
// The shade curve. Fully fogged, the shade lets through T = 1 - SHADE of the
// light under it. A tile at level l lets through T^(l/255), so a plain fill at
// level l has opacity 1 - T^(l/255) (fillAlpha). A shade sprite layer that darkens
// a tile r levels further must have opacity 1 - T^(r/255); the sprite's own pixels
// peak at SHADE, so it is drawn with (1 - T^(r/255)) / SHADE (layerAlpha), which is
// exactly 255 for a full rise. Transmittances multiply, T^(a/255) * T^(r/255) =
// T^((a+r)/255), so a fill at level a under a layer rising r looks like a fill at
// level a + r. That lets the fog pass shade a square with a fill at its lowest
// corner level and one sprite layer per higher corner level.
constexpr double SHADE = FogFade::SHADE_ALPHA / 255.0;

//! Opacity of the shade at `level`, 0 to SHADE.
double shadeOpacity(int level) { return 1.0 - std::pow(1.0 - SHADE, level / 255.0); }

//! A 256-entry alpha table of f(level), rounded and clamped to 0..255.
template <typename F>
std::array<Uint8, 256> alphaTable(F f)
{
	std::array<Uint8, 256> table{};
	for (int i = 0; i < 256; i++)
		table[i] = Uint8(std::clamp(std::lround(f(i)), 0L, 255L));
	return table;
}
}

void FogFade::update(const SceneMap &map, Uint32 visibleTeams, Uint32 tick, double time)
{
	update(map.getW(), map.getH(), map.identity(), visibleTeams, tick, time, map.fogOfWarData());
}

void FogFade::update(int newW, int newH, Uint64 mapIdentity, Uint32 visibleTeams, Uint32 tick, double time, const Uint32 *fog)
{
	// level() wraps coordinates with masks, like SceneMap::coordToIndex.
	assert(newW > 0 && (newW & (newW - 1)) == 0 && newH > 0 && (newH & (newH - 1)) == 0);
	now = time;
	const size_t size = size_t(newW) * size_t(newH);
	const bool sameView = w == newW && h == newH && identity == mapIdentity && teams == visibleTeams;
	// Several frames draw each tick; the fog only changes with the tick.
	if (sameView && tick == lastTick)
		return;
	// A new map or other visible teams, a step back in time (a seek or a load), or a
	// long gap: fading would replay changes from another view or long past, so settle.
	if (!sameView || tick < lastTick || tick - lastTick > SETTLE_JUMP_TICKS)
	{
		w = newW;
		h = newH;
		identity = mapIdentity;
		teams = visibleTeams;
		fogged.resize(size);
		for (size_t i = 0; i < size; i++)
			fogged[i] = (fog[i] & visibleTeams) == 0;
		settle(tick);
		return;
	}
	// A tile changing state starts a new fade from wherever its fade had reached on
	// this tick, so a tile changing back mid-fade never jumps.
	for (size_t i = 0; i < size; i++)
	{
		const Uint8 isFogged = (fog[i] & visibleTeams) == 0;
		if (isFogged == fogged[i])
			continue;
		startLevel[i] = levelAt(i, double(tick));
		startTick[i] = tick;
		fogged[i] = isFogged;
	}
	lastTick = tick;
}

Uint8 FogFade::levelAt(size_t index, double time) const
{
	// A frame drawn just before the tick a fade started counts as its start.
	const double elapsed = std::max(0.0, time - double(startTick[index]));
	const int start = startLevel[index];
	if (fogged[index])
		return Uint8(std::min(double(FOGGED), start + elapsed * FOGGED / DARKEN_TICKS));
	return Uint8(std::max(0.0, start - elapsed * FOGGED / REVEAL_TICKS));
}

void FogFade::settle(Uint32 tick)
{
	startLevel.resize(fogged.size());
	startTick.assign(fogged.size(), tick);
	for (size_t i = 0; i < fogged.size(); i++)
		startLevel[i] = fogged[i] ? FOGGED : 0;
	lastTick = tick;
}

Uint8 FogFade::fillAlpha(Uint8 level)
{
	static const std::array<Uint8, 256> alphas = alphaTable([](int l) { return 255.0 * shadeOpacity(l); });
	return alphas[level];
}

Uint8 FogFade::layerAlpha(Uint8 rise)
{
	static const std::array<Uint8, 256> alphas = alphaTable([](int r) { return 255.0 * shadeOpacity(r) / SHADE; });
	return alphas[rise];
}
