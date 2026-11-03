// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>

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
	//! Only meaningful while active().
	float at(float px, float py) const
	{
		const float dx = px - x, dy = py - y;
		const float t = std::clamp((std::sqrt(dx * dx + dy * dy) - inner) / (outer - inner), 0.f, 1.f);
		return 1 - (1 - floor) * t * t * (3 - 2 * t);
	}
};

// A faded draw becomes this many cells per side, with the fade interpolated
// across each cell; at unit sprite sizes a cell spans a few pixels.
constexpr int RADIAL_FADE_CELLS = 8;
// Two triangles per cell.
constexpr int RADIAL_FADE_TRIANGLE_VERTICES = RADIAL_FADE_CELLS * RADIAL_FADE_CELLS * 6;

// One grid corner: its position, its place (s, t) across the destination
// rectangle from 0 to 1, and the fade there.
struct RadialFadeCorner
{
	float x, y, s, t, fade;
};
// A grid cell's corners, clockwise from top-left: the order of a GL quad.
using RadialFadeCell = std::array<RadialFadeCorner, 4>;

// Calls cell(corners) for each grid cell of the destination rectangle of an
// active fade.
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
			cell(RadialFadeCell{corners[j][i], corners[j][i + 1], corners[j + 1][i + 1], corners[j + 1][i]});
}

// Triangles for a faded textured rectangle sampling [u0, u1] x [v0, v1], two
// per cell, with straight alpha. Returned by value so that the per-draw
// geometry stays off the heap.
inline std::array<SDL_Vertex, RADIAL_FADE_TRIANGLE_VERTICES> radialFadeTriangles(const RadialFade &fade,
	float x, float y, float w, float h, float u0, float v0, float u1, float v1, float alpha)
{
	std::array<SDL_Vertex, RADIAL_FADE_TRIANGLE_VERTICES> vertices;
	size_t n = 0;
	const auto vertex = [&](const RadialFadeCorner &c)
	{
		return SDL_Vertex{{c.x, c.y}, {1, 1, 1, alpha * c.fade}, {u0 + (u1 - u0) * c.s, v0 + (v1 - v0) * c.t}};
	};
	forEachRadialFadeCell(fade, x, y, w, h, [&](const RadialFadeCell &cell)
	{
		const SDL_Vertex a = vertex(cell[0]), b = vertex(cell[1]), c = vertex(cell[2]), d = vertex(cell[3]);
		for (const SDL_Vertex &v : {a, b, c, a, c, d})
			vertices[n++] = v;
	});
	return vertices;
}
} // namespace GAGCore
