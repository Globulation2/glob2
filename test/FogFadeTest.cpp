// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for FogFade, the per-view field that fades tiles into and out of the
// fog of war. The properties of interest: a view settles without animating when
// it first sees a map, a new map, other teams, a jump in time or a reset; a tile
// changing state fades monotonically and continues from wherever it had reached
// when it changes back; and the shade alphas stack to the plain fog shade, within
// rounding.

#include "Glob2Test.h"

#include "FogFade.h"

#include <cmath>
#include <initializer_list>
#include <vector>

namespace
{
constexpr int W = 8, H = 4;

//! A small map's fog for one team, and the fade following it.
struct Field
{
	FogFade fade;
	std::vector<Uint32> fog = std::vector<Uint32>(W * H, 1);
	Uint64 identity = 1;
	Uint32 teams = 1;

	//! Mark tile i of the top row fogged or in sight for `teams`.
	void setFogged(int i, bool fogged) { fog[i] = fogged ? 0 : teams; }
	//! Follow the fog as of `tick`, drawn at game time `now` (default: the tick).
	void update(Uint32 tick, double now = -1)
	{
		fade.update(W, H, identity, teams, tick, now < 0 ? tick : now, fog.data());
	}
	//! Level of top-row tile x at game time `now`, without a new tick.
	int levelAt(int x, Uint32 tick, double now)
	{
		update(tick, now);
		return fade.level(x, 0);
	}
};

// Opacity of `alpha` drawn over `opacity`, both out of 1.
double over(double opacity, double alpha) { return opacity + (1 - opacity) * alpha; }
// Opacity of a shade sprite layer drawn with `alpha`, where its pixels peak.
double layer(Uint8 alpha) { return FogFade::SHADE_ALPHA / 255.0 * alpha / 255.0; }
}

TEST_SUITE("FogFade")
{
	TEST_CASE("The first update settles every tile without fading")
	{
		Field field;
		CHECK_FALSE(field.fade.active());
		field.setFogged(3, true);
		field.update(100);
		CHECK(field.fade.active());
		CHECK(field.fade.level(3, 0) == FogFade::FOGGED);
		CHECK(field.fade.level(4, 0) == 0);
		// Coordinates wrap onto the map like SceneMap's, negative ones included.
		CHECK(field.fade.level(3 + W, -H) == FogFade::FOGGED);
		CHECK(field.fade.level(3 - W, 2 * H) == FogFade::FOGGED);
	}

	TEST_CASE("Darkening and revealing fade monotonically over their durations")
	{
		Field field;
		field.update(100);
		field.setFogged(0, true);
		field.update(101);
		int previous = -1;
		for (double t = 101; t <= 101 + FogFade::DARKEN_TICKS; t += 0.25)
		{
			const int level = field.levelAt(0, 101, t);
			CHECK(level >= previous);
			previous = level;
		}
		CHECK(field.levelAt(0, 101, 101) == 0);
		const int halfway = field.levelAt(0, 101, 101 + FogFade::DARKEN_TICKS / 2.0);
		CHECK((halfway > 0 && halfway < FogFade::FOGGED));
		CHECK(field.levelAt(0, 101, 101 + FogFade::DARKEN_TICKS) == FogFade::FOGGED);
		// A frame drawn before the tick the fade started on counts as its start.
		CHECK(field.levelAt(0, 101, 100.5) == 0);

		const Uint32 revealed = 101 + FogFade::DARKEN_TICKS + 5;
		field.update(revealed - 1);
		field.setFogged(0, false);
		field.update(revealed);
		previous = 256;
		for (double t = revealed; t <= revealed + FogFade::REVEAL_TICKS; t += 0.25)
		{
			const int level = field.levelAt(0, revealed, t);
			CHECK(level <= previous);
			previous = level;
		}
		CHECK(field.levelAt(0, revealed, revealed) == FogFade::FOGGED);
		CHECK(field.levelAt(0, revealed, revealed + FogFade::REVEAL_TICKS) == 0);
		// Darkening is the slow direction.
		CHECK(FogFade::DARKEN_TICKS > FogFade::REVEAL_TICKS);
	}

	TEST_CASE("Changing back mid-fade continues from the level reached")
	{
		Field field;
		field.update(10);
		field.setFogged(1, true);
		field.update(11);
		const Uint32 back = 11 + FogFade::DARKEN_TICKS / 2;
		const int reached = field.levelAt(1, back - 1, back);
		CHECK((reached > 0 && reached < FogFade::FOGGED));
		field.setFogged(1, false);
		field.update(back);
		CHECK(field.fade.level(1, 0) == reached);
		CHECK(field.levelAt(1, back, back + 0.5) < reached);
	}

	TEST_CASE("A short gap between ticks keeps fading from the new tick")
	{
		// No frame was drawn for a few ticks (a slow frame, a short hitch): the tile
		// starts its fade at the tick the change is seen, not a later time.
		Field field;
		field.update(10);
		field.setFogged(2, true);
		const Uint32 seen = 10 + FogFade::SETTLE_JUMP_TICKS / 2;
		field.update(seen);
		CHECK(field.fade.level(2, 0) == 0);
		CHECK(field.levelAt(2, seen, seen + 1) > 0);
	}

	TEST_CASE("A repeated tick changes nothing")
	{
		Field field;
		field.update(10);
		field.setFogged(2, true);
		field.update(11);
		const int level = field.levelAt(2, 11, 14);
		CHECK(field.levelAt(2, 11, 14) == level);
	}

	TEST_CASE("A new map, other teams, a jump in time or a reset settle every tile")
	{
		const auto settles = [](auto change)
		{
			Field field;
			field.update(1000);
			field.setFogged(5, true);
			Uint32 tick = 1001;
			change(field, tick);
			field.update(tick);
			return field.fade.level(5, 0) == FogFade::FOGGED;
		};
		CHECK(settles([](Field &f, Uint32 &) { f.identity = 2; }));
		CHECK(settles([](Field &f, Uint32 &) { f.teams = 3; }));
		CHECK(settles([](Field &, Uint32 &tick) { tick = 500; }));
		CHECK(settles([](Field &, Uint32 &tick) { tick = 1000 + FogFade::SETTLE_JUMP_TICKS + 1; }));
		CHECK(settles([](Field &f, Uint32 &) { f.fade.reset(); }));
		CHECK_FALSE(settles([](Field &, Uint32 &tick) { tick = 1000 + FogFade::SETTLE_JUMP_TICKS; }));
	}

	TEST_CASE("Shade alphas stack to the plain fog shade")
	{
		CHECK(FogFade::fillAlpha(0) == 0);
		CHECK(FogFade::fillAlpha(FogFade::FOGGED) == FogFade::SHADE_ALPHA);
		// A full-height sprite over nothing draws the sprite as it always was.
		CHECK(FogFade::layerAlpha(FogFade::FOGGED) == 255);
		CHECK(FogFade::layerAlpha(0) == 0);
		for (int level = 1; level < 256; level++)
		{
			CHECK(FogFade::fillAlpha(Uint8(level)) >= FogFade::fillAlpha(Uint8(level - 1)));
			CHECK(FogFade::layerAlpha(Uint8(level)) >= FogFade::layerAlpha(Uint8(level - 1)));
		}
		// A fill at one level and a sprite layer up to another compose to the fill at
		// the higher level, within rounding.
		for (int low : {0, 40, 128, 200})
			for (int high : {low + 1, 180, 255})
			{
				if (high <= low)
					continue;
				const double composed = over(FogFade::fillAlpha(Uint8(low)) / 255.0, layer(FogFade::layerAlpha(Uint8(high - low))));
				CHECK(std::fabs(composed * 255.0 - FogFade::fillAlpha(Uint8(high))) < 1.5);
			}
		// So do three layers, as the most a square with four corner levels draws.
		double composed = FogFade::fillAlpha(30) / 255.0;
		for (int rise : {50, 70, 105})
			composed = over(composed, layer(FogFade::layerAlpha(Uint8(rise))));
		CHECK(std::fabs(composed * 255.0 - FogFade::fillAlpha(255)) < 2.5);
	}
}
