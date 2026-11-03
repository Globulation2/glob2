// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace GAGCore
{
// Opacity falling off with distance from a point. While one is set on the
// graphic context, sprite, surface and skin draws are multiplied by it: full
// opacity within `inner`, `floor` beyond `outer`, a smoothstep between. The
// point and radii are in the coordinates the draws receive, so the fade follows
// map zoom and scrolling with the sprite it is centred on.
struct RadialFade
{
	float x = 0, y = 0, inner = 0, outer = 0, floor = 1;
	bool active() const { return outer > inner && floor < 1; }
	float at(float px, float py) const
	{
		const float t = std::clamp((std::hypot(px - x, py - y) - inner) / (outer - inner), 0.f, 1.f);
		return 1 - (1 - floor) * t * t * (3 - 2 * t);
	}
};

// A faded draw becomes this many cells per side, with the fade interpolated
// across each cell; at unit sprite sizes a cell spans a few pixels.
constexpr int RADIAL_FADE_CELLS = 8;

// One grid corner: its position, its place (s, t) across the destination
// rectangle from 0 to 1, and the fade there.
struct RadialFadeCorner
{
	float x, y, s, t, fade;
};

// Calls cell(a, b, c, d) for each grid cell of the destination rectangle, with
// the corners clockwise from top-left.
template <class Cell>
void forEachRadialFadeCell(const RadialFade &fade, float x, float y, float w, float h, Cell cell)
{
	constexpr int N = RADIAL_FADE_CELLS;
	RadialFadeCorner corners[N + 1][N + 1];
	for (int j = 0; j <= N; ++j)
		for (int i = 0; i <= N; ++i)
		{
			const float s = float(i) / N, t = float(j) / N;
			const float px = x + w * s, py = y + h * t;
			corners[j][i] = {px, py, s, t, fade.at(px, py)};
		}
	for (int j = 0; j < N; ++j)
		for (int i = 0; i < N; ++i)
			cell(corners[j][i], corners[j][i + 1], corners[j + 1][i + 1], corners[j + 1][i]);
}

// Triangles for a faded textured rectangle sampling [u0, u1] x [v0, v1], two
// per cell. A premultiplied source fades its colour together with its alpha.
inline std::vector<SDL_Vertex> radialFadeTriangles(const RadialFade &fade, float x, float y, float w, float h,
	float u0, float v0, float u1, float v1, float alpha, bool premultiplied)
{
	std::vector<SDL_Vertex> vertices;
	vertices.reserve(RADIAL_FADE_CELLS * RADIAL_FADE_CELLS * 6);
	const auto vertex = [&](const RadialFadeCorner &c)
	{
		const float a = alpha * c.fade, rgb = premultiplied ? a : 1.f;
		return SDL_Vertex{{c.x, c.y}, {rgb, rgb, rgb, a}, {u0 + (u1 - u0) * c.s, v0 + (v1 - v0) * c.t}};
	};
	forEachRadialFadeCell(fade, x, y, w, h,
		[&](const RadialFadeCorner &a, const RadialFadeCorner &b, const RadialFadeCorner &c, const RadialFadeCorner &d)
		{
			const SDL_Vertex va = vertex(a), vb = vertex(b), vc = vertex(c), vd = vertex(d);
			vertices.insert(vertices.end(), {va, vb, vc, va, vc, vd});
		});
	return vertices;
}
} // namespace GAGCore
