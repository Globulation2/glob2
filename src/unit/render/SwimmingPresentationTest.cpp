// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "SwimmingPresentation.h"
#include <cmath>
#include <numbers>
#include <utility>

using GAGCore::RadialFade;
using GAGCore::RadialFadeCorner;

TEST_SUITE("SwimmingPresentation")
{
	TEST_CASE("the fade keeps the torso solid and holds the limbs at the floor")
	{
		const float reach = SWIM_REACH_WORKER;
		const RadialFade fade = swimmerFade(64, 32, reach);
		CHECK(fade.active());
		CHECK_EQ(fade.at(80, 48), doctest::Approx(1));
		CHECK_EQ(fade.at(80 + reach * SWIM_FADE_INNER, 48), doctest::Approx(1));
		CHECK_EQ(fade.at(80 + reach, 48), doctest::Approx(SWIM_FADE_FLOOR));
		CHECK_EQ(fade.at(80, 48 - 40), doctest::Approx(SWIM_FADE_FLOOR));
		float previous = 1;
		for (float d = 0; d <= reach + 4; d += 0.5f)
		{
			const float value = fade.at(80 + d * 0.6f, 48 + d * 0.8f);
			CHECK(value <= previous + 1e-6f);
			previous = value;
		}
	}

	TEST_CASE("an unset fade is inactive")
	{
		CHECK_FALSE(RadialFade{}.active());
		CHECK_FALSE((RadialFade{0, 0, 4, 4, 0.5f}.active()));
		CHECK_FALSE((RadialFade{0, 0, 4, 8, 1}.active()));
	}

	TEST_CASE("the fade grid tiles the destination and samples its own position")
	{
		const RadialFade fade = swimmerFade(0, 0, SWIM_REACH_WARRIOR);
		int cells = 0;
		float area = 0;
		GAGCore::forEachRadialFadeCell(fade, 10, 20, 40, 24, [&](const GAGCore::RadialFadeCell &cell)
			{
				const RadialFadeCorner &a = cell[0], &b = cell[1], &c = cell[2], &d = cell[3];
				++cells;
				area += (b.x - a.x) * (d.y - a.y);
				// Clockwise from top-left.
				CHECK(b.x > a.x);
				CHECK(d.y > a.y);
				CHECK_EQ(a.y, b.y);
				CHECK_EQ(b.x, c.x);
				CHECK_EQ(c.y, d.y);
				CHECK_EQ(d.x, a.x);
				for (const RadialFadeCorner &k : cell)
				{
					CHECK_EQ(k.x, doctest::Approx(10 + 40 * k.s));
					CHECK_EQ(k.y, doctest::Approx(20 + 24 * k.t));
					CHECK_EQ(k.fade, doctest::Approx(fade.at(k.x, k.y)));
				}
			});
		CHECK_EQ(cells, GAGCore::RADIAL_FADE_CELLS * GAGCore::RADIAL_FADE_CELLS);
		CHECK_EQ(area, doctest::Approx(40 * 24));
	}

	TEST_CASE("faded triangles span the requested texture range")
	{
		const RadialFade fade = swimmerFade(0, 0, SWIM_REACH_WARRIOR);
		const auto vertices = GAGCore::radialFadeTriangles(fade, 0, 0, 32, 32, 0.25f, 0.5f, 0.75f, 1.f, 0.5f);
		float minU = 1, maxU = 0, minV = 1, maxV = 0, area = 0;
		for (const SDL_Vertex &v : vertices)
		{
			minU = std::min(minU, v.tex_coord.x); maxU = std::max(maxU, v.tex_coord.x);
			minV = std::min(minV, v.tex_coord.y); maxV = std::max(maxV, v.tex_coord.y);
			// Straight alpha: the colour stays white as the opacity fades.
			CHECK_EQ(v.color.r, 1.f);
			CHECK_EQ(v.color.a, doctest::Approx(0.5f * fade.at(v.position.x, v.position.y)));
			// Texture coordinates follow the position across the rectangle.
			CHECK_EQ(v.tex_coord.x, doctest::Approx(0.25f + 0.5f * v.position.x / 32));
			CHECK_EQ(v.tex_coord.y, doctest::Approx(0.5f + 0.5f * v.position.y / 32));
		}
		CHECK_EQ(minU, doctest::Approx(0.25f));
		CHECK_EQ(maxU, doctest::Approx(0.75f));
		CHECK_EQ(minV, doctest::Approx(0.5f));
		CHECK_EQ(maxV, doctest::Approx(1.f));
		// Every triangle is filled exactly once and keeps one winding.
		for (size_t i = 0; i < vertices.size(); i += 3)
		{
			const SDL_FPoint a = vertices[i].position, b = vertices[i + 1].position, c = vertices[i + 2].position;
			const float doubled = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
			CHECK(doubled > 0);
			area += doubled / 2;
		}
		CHECK_EQ(area, doctest::Approx(32 * 32));
	}

	TEST_CASE("each stroke sends out one ring that grows fast and fades")
	{
		const float reach = SWIM_REACH_WARRIOR, start = reach - SWIM_RIPPLE_INSET;
		const int strokeStart = SWIM_STROKE_PHASE << 3;
		const SwimRipple born = swimRipple(strokeStart, reach);
		CHECK_EQ(born.radius, doctest::Approx(start));
		CHECK_EQ(born.alpha, doctest::Approx(SWIM_RIPPLE_ALPHA));
		CHECK_EQ(swimRipple(strokeStart - 1, reach).alpha, 0.f);
		float radius = born.radius, alpha = born.alpha, step = 1e9f;
		int live = 0;
		for (int delta = strokeStart + 1; delta < strokeStart + 256; ++delta)
		{
			const SwimRipple ring = swimRipple(delta & 255, reach);
			if (ring.alpha == 0)
				continue;
			++live;
			CHECK(ring.radius > radius);
			CHECK(ring.radius < start + SWIM_RIPPLE_GROWTH);
			CHECK(ring.alpha < alpha);
			// It slows as it spreads.
			CHECK(ring.radius - radius <= step + 1e-4f);
			step = ring.radius - radius;
			radius = ring.radius;
			alpha = ring.alpha;
		}
		// The ring lives for its share of the stroke, then the water is still.
		CHECK(live > 0);
		CHECK(live < int(256 * SWIM_RIPPLE_LIFE) + 1);
	}

	TEST_CASE("a moving swimmer's ripple spreads behind it, not ahead")
	{
		const auto weight = [](float angle, int dx, int dy)
		{ return swimRippleWeight(std::cos(angle), std::sin(angle), dx, dy); };
		const float pi = std::numbers::pi_v<float>;
		for (int dx = -1; dx <= 1; ++dx)
			for (int dy = -1; dy <= 1; ++dy)
			{
				const float heading = std::atan2(float(dy), float(dx));
				if (!dx && !dy)
				{
					CHECK_EQ(weight(heading, dx, dy), 1.f);
					continue;
				}
				CHECK_EQ(weight(heading, dx, dy), doctest::Approx(0));
				CHECK_EQ(weight(heading + 0.6f, dx, dy), doctest::Approx(0));
				CHECK_EQ(weight(heading + pi, dx, dy), doctest::Approx(1));
				const float side = weight(heading + pi / 2, dx, dy);
				CHECK(side > 0);
				CHECK(side < 1);
				// Symmetric about the heading.
				CHECK_EQ(weight(heading + 2, dx, dy), doctest::Approx(weight(heading - 2, dx, dy)));
			}
	}

	TEST_CASE("the ring's segments close a circle weighted at their middles")
	{
		const float cx = 40, cy = 24, radius = 13;
		for (const auto [dx, dy] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{-1, 1}, std::pair{0, -1}})
		{
			int count = 0;
			float firstX = 0, firstY = 0, lastX = 0, lastY = 0, behind = 0, ahead = 1;
			forEachSwimRippleSegment(cx, cy, radius, dx, dy, [&](const SwimRippleSegment &s)
			{
				if (count == 0)
				{
					firstX = s.x0;
					firstY = s.y0;
					// It starts straight right of the centre.
					CHECK_EQ(s.x0, doctest::Approx(cx + radius));
					CHECK_EQ(s.y0, doctest::Approx(cy));
				}
				else
				{
					// Consecutive segments join.
					CHECK_EQ(s.x0, lastX);
					CHECK_EQ(s.y0, lastY);
				}
				lastX = s.x1;
				lastY = s.y1;
				CHECK_EQ(std::hypot(s.x1 - cx, s.y1 - cy), doctest::Approx(radius));
				// Weighted at the middle of its arc.
				const float mx = (s.x0 + s.x1) / 2 - cx, my = (s.y0 + s.y1) / 2 - cy, m = std::hypot(mx, my);
				CHECK_EQ(s.weight, doctest::Approx(swimRippleWeight(mx / m, my / m, dx, dy)).epsilon(1e-4));
				behind = std::max(behind, s.weight);
				ahead = std::min(ahead, s.weight);
				++count;
			});
			CHECK_EQ(count, SWIM_RIPPLE_SEGMENTS);
			CHECK_EQ(lastX, doctest::Approx(firstX));
			CHECK_EQ(lastY, doctest::Approx(firstY));
			if (dx || dy)
			{
				CHECK(behind > 0.95f);
				CHECK_EQ(ahead, 0.f);
			}
			else
				CHECK_EQ(ahead, 1.f);
		}
	}
}
