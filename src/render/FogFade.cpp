// SPDX-License-Identifier: GPL-3.0-or-later
#include "FogFade.h"

#include "scene/SceneMap.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
// The shade lets this fraction of light through when fully fogged. Fading to
// level l lets through a power l/255 of it, so a fade from a to b and one from b
// to c compose exactly to the fade from a to c.
constexpr double SHADE = FogFade::SHADE_ALPHA / 255.0;

double opacity(int level) { return 1.0 - std::pow(1.0 - SHADE, level / 255.0); }

template <typename F>
std::array<Uint8, 256> table(F f)
{
	std::array<Uint8, 256> t{};
	for (int i = 0; i < 256; i++)
		t[i] = Uint8(std::clamp(std::lround(f(i)), 0L, 255L));
	return t;
}
}

void FogFade::update(const SceneMap &map, Uint32 visibleTeams, Uint32 tick)
{
	const Uint32 *fog = map.fogOfWarData();
	update(map.getW(), map.getH(), map.identity(), visibleTeams, tick,
		   [fog, visibleTeams](size_t i) { return (fog[i] & visibleTeams) == 0; });
}

Uint8 FogFade::levelAt(size_t index, double now) const
{
	const double elapsed = std::max(0.0, now - double(startTick[index]));
	const int start = startLevel[index];
	if (fogged[index])
		return Uint8(std::min(255.0, start + elapsed * 255.0 / DARKEN_TICKS));
	return Uint8(std::max(0.0, start - elapsed * 255.0 / REVEAL_TICKS));
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
	static const std::array<Uint8, 256> alphas = table([](int l) { return 255.0 * opacity(l); });
	return alphas[level];
}

Uint8 FogFade::layerAlpha(Uint8 rise)
{
	static const std::array<Uint8, 256> alphas = table([](int r) { return 255.0 * opacity(r) / SHADE; });
	return alphas[rise];
}
