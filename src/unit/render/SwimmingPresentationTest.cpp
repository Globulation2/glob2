// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "SwimmingPresentation.h"
#include <cmath>

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
		GAGCore::forEachRadialFadeCell(fade, 10, 20, 40, 24,
			[&](const RadialFadeCorner &a, const RadialFadeCorner &b, const RadialFadeCorner &c, const RadialFadeCorner &d)
			{
				++cells;
				area += (b.x - a.x) * (d.y - a.y);
				CHECK_EQ(a.y, b.y);
				CHECK_EQ(b.x, c.x);
				CHECK_EQ(c.y, d.y);
				for (const RadialFadeCorner *k : {&a, &b, &c, &d})
				{
					CHECK_EQ(k->x, doctest::Approx(10 + 40 * k->s));
					CHECK_EQ(k->y, doctest::Approx(20 + 24 * k->t));
					CHECK_EQ(k->fade, doctest::Approx(fade.at(k->x, k->y)));
				}
			});
		CHECK_EQ(cells, GAGCore::RADIAL_FADE_CELLS * GAGCore::RADIAL_FADE_CELLS);
		CHECK_EQ(area, doctest::Approx(40 * 24));
	}

	TEST_CASE("faded triangles span the requested texture range")
	{
		const RadialFade fade = swimmerFade(0, 0, SWIM_REACH_WARRIOR);
		const auto straight = GAGCore::radialFadeTriangles(fade, 0, 0, 32, 32, 0.25f, 0.5f, 0.75f, 1.f, 0.5f, false);
		REQUIRE_EQ(straight.size(), size_t(GAGCore::RADIAL_FADE_CELLS * GAGCore::RADIAL_FADE_CELLS * 6));
		float minU = 1, maxU = 0, minV = 1, maxV = 0;
		for (const SDL_Vertex &v : straight)
		{
			minU = std::min(minU, v.tex_coord.x); maxU = std::max(maxU, v.tex_coord.x);
			minV = std::min(minV, v.tex_coord.y); maxV = std::max(maxV, v.tex_coord.y);
			CHECK_EQ(v.color.r, 1.f);
			CHECK_EQ(v.color.a, doctest::Approx(0.5f * fade.at(v.position.x, v.position.y)));
		}
		CHECK_EQ(minU, doctest::Approx(0.25f));
		CHECK_EQ(maxU, doctest::Approx(0.75f));
		CHECK_EQ(minV, doctest::Approx(0.5f));
		CHECK_EQ(maxV, doctest::Approx(1.f));
		for (const SDL_Vertex &v : GAGCore::radialFadeTriangles(fade, 0, 0, 32, 32, 0, 0, 1, 1, 0.5f, true))
			CHECK_EQ(v.color.r, doctest::Approx(v.color.a));
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
		for (int dx = -1; dx <= 1; ++dx)
			for (int dy = -1; dy <= 1; ++dy)
			{
				const float heading = std::atan2(float(dy), float(dx));
				if (!dx && !dy)
				{
					CHECK_EQ(swimRippleWeight(heading, dx, dy), 1.f);
					continue;
				}
				CHECK_EQ(swimRippleWeight(heading, dx, dy), doctest::Approx(0));
				CHECK_EQ(swimRippleWeight(heading + 0.6f, dx, dy), doctest::Approx(0));
				CHECK_EQ(swimRippleWeight(heading + float(M_PI), dx, dy), doctest::Approx(1));
				const float side = swimRippleWeight(heading + float(M_PI) / 2, dx, dy);
				CHECK(side > 0);
				CHECK(side < 1);
				// Symmetric about the heading.
				CHECK_EQ(swimRippleWeight(heading + 2, dx, dy), doctest::Approx(swimRippleWeight(heading - 2, dx, dy)));
			}
	}
}
