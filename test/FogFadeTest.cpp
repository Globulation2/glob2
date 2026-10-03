// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for FogFade, the per-view field that fades tiles into and out of the
// fog of war. The properties of interest: a view settles without animating when
// it first sees a map, a new map, other teams or a jump in time; a tile changing
// state fades monotonically and continues from wherever it had reached when it
// changes back; and the shade alphas stack to exactly the plain fog shade.

#include "Glob2Test.h"

#include "FogFade.h"

#include <cmath>
#include <vector>

namespace
{
constexpr int W = 8, H = 4;

struct Field
{
	FogFade fade;
	std::vector<bool> fogged = std::vector<bool>(W * H, false);
	Uint64 identity = 1;
	Uint32 teams = 1;

	void update(Uint32 tick)
	{
		fade.update(W, H, identity, teams, tick, [&](size_t i) { return bool(fogged[i]); });
	}
};

// Opacity of `alpha` drawn over `opacity`, both out of 1.
double over(double opacity, double alpha) { return opacity + (1 - opacity) * alpha; }
}

TEST_SUITE("FogFade")
{
	TEST_CASE("The first update settles every tile without fading")
	{
		Field field;
		field.fogged[3] = true;
		field.update(100);
		CHECK(field.fade.ready());
		CHECK(field.fade.level(3, 0, 100) == FogFade::FOGGED);
		CHECK(field.fade.level(4, 0, 100) == 0);
		// Coordinates wrap onto the map like SceneMap's.
		CHECK(field.fade.level(3 + W, -H, 100.5) == FogFade::FOGGED);
	}

	TEST_CASE("Darkening and revealing fade monotonically over their durations")
	{
		Field field;
		field.update(100);
		field.fogged[0] = true;
		field.update(101);
		int previous = -1;
		for (double t = 101; t <= 101 + FogFade::DARKEN_TICKS; t += 0.25)
		{
			const int level = field.fade.level(0, 0, t);
			CHECK(level >= previous);
			previous = level;
		}
		CHECK(field.fade.level(0, 0, 101) == 0);
		CHECK(field.fade.level(0, 0, 101 + FogFade::DARKEN_TICKS / 2.0) > 0);
		CHECK(field.fade.level(0, 0, 101 + FogFade::DARKEN_TICKS / 2.0) < FogFade::FOGGED);
		CHECK(field.fade.level(0, 0, 101 + FogFade::DARKEN_TICKS) == FogFade::FOGGED);

		const Uint32 revealed = 101 + FogFade::DARKEN_TICKS + 5;
		field.update(revealed - 1);
		field.fogged[0] = false;
		field.update(revealed);
		previous = 256;
		for (double t = revealed; t <= revealed + FogFade::REVEAL_TICKS; t += 0.25)
		{
			const int level = field.fade.level(0, 0, t);
			CHECK(level <= previous);
			previous = level;
		}
		CHECK(field.fade.level(0, 0, revealed) == FogFade::FOGGED);
		CHECK(field.fade.level(0, 0, revealed + FogFade::REVEAL_TICKS) == 0);
		// Darkening is the slow direction.
		CHECK(FogFade::DARKEN_TICKS > FogFade::REVEAL_TICKS);
	}

	TEST_CASE("Changing back mid-fade continues from the level reached")
	{
		Field field;
		field.update(10);
		field.fogged[1] = true;
		field.update(11);
		const Uint32 back = 11 + FogFade::DARKEN_TICKS / 2;
		field.update(back - 1);
		const int reached = field.fade.level(1, 0, back);
		CHECK(reached > 0);
		CHECK(reached < FogFade::FOGGED);
		field.fogged[1] = false;
		field.update(back);
		CHECK(field.fade.level(1, 0, back) == reached);
		CHECK(field.fade.level(1, 0, back + 0.5) < reached);
	}

	TEST_CASE("A repeated tick changes nothing")
	{
		Field field;
		field.update(10);
		field.fogged[2] = true;
		field.update(11);
		const Uint8 level = field.fade.level(2, 0, 14);
		field.update(11);
		CHECK(field.fade.level(2, 0, 14) == level);
	}

	TEST_CASE("A new map, other teams or a jump in time settle every tile")
	{
		const auto settles = [](auto change)
		{
			Field field;
			field.update(1000);
			field.fogged[5] = true;
			Uint32 tick = 1001;
			change(field, tick);
			field.update(tick);
			return field.fade.level(5, 0, tick) == FogFade::FOGGED;
		};
		CHECK(settles([](Field &f, Uint32 &) { f.identity = 2; }));
		CHECK(settles([](Field &f, Uint32 &) { f.teams = 3; }));
		CHECK(settles([](Field &, Uint32 &tick) { tick = 500; }));
		CHECK(settles([](Field &, Uint32 &tick) { tick = 1000 + FogFade::SETTLE_JUMP_TICKS + 1; }));
		CHECK_FALSE(settles([](Field &, Uint32 &tick) { tick = 1000 + FogFade::SETTLE_JUMP_TICKS; }));
	}

	TEST_CASE("Shade alphas stack to the plain fog shade")
	{
		const double shade = FogFade::SHADE_ALPHA / 255.0;
		CHECK(FogFade::fillAlpha(0) == 0);
		CHECK(FogFade::fillAlpha(FogFade::FOGGED) == FogFade::SHADE_ALPHA);
		// A full-height sprite over nothing draws the sprite as it always was.
		CHECK(FogFade::layerAlpha(FogFade::FOGGED) == 255);
		CHECK(FogFade::layerAlpha(0) == 0);
		for (int level = 1; level < 256; level++)
			CHECK(FogFade::fillAlpha(Uint8(level)) >= FogFade::fillAlpha(Uint8(level - 1)));
		// A fill at one level and a sprite layer up to another compose to the fill at
		// the higher level, within rounding.
		for (int low : {0, 40, 128, 200})
			for (int high : {low + 1, 180, 255})
			{
				if (high <= low)
					continue;
				const double layer = shade * FogFade::layerAlpha(Uint8(high - low)) / 255.0;
				const double composed = over(FogFade::fillAlpha(Uint8(low)) / 255.0, layer);
				CHECK(std::fabs(composed * 255.0 - FogFade::fillAlpha(Uint8(high))) < 1.5);
			}
	}
}
