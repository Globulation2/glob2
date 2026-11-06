// SPDX-License-Identifier: GPL-3.0-or-later

#include "Glob2Test.h"
#include "IntroCover.h"

#include <algorithm>

namespace
{
double mean(const std::valarray<unsigned char> &v)
{
	double sum = 0;
	for (unsigned char c : v)
		sum += c;
	return v.size() ? sum / double(v.size()) : 0;
}
}

TEST_SUITE("IntroCover")
{
TEST_CASE("five seconds: covered, then parting; a skip parts at once")
{
	IntroCover cover;
	const Uint64 t0 = 10000;
	REQUIRE_FALSE(cover.active(t0));
	REQUIRE_FALSE(cover.blocksInput(t0));
	cover.begin(t0, {"Map", {}});
	REQUIRE(cover.active(t0));
	REQUIRE(cover.blocksInput(t0));
	REQUIRE(cover.part(t0 + IntroCover::COVER_MS - 1) == 0);
	REQUIRE(cover.part(t0 + IntroCover::COVER_MS + IntroCover::PART_MS / 2) == doctest::Approx(.5f).epsilon(.01));
	REQUIRE_FALSE(cover.blocksInput(t0 + IntroCover::COVER_MS + IntroCover::PART_MS / 2));
	REQUIRE(cover.active(t0 + IntroCover::TOTAL_MS - 1));
	REQUIRE_FALSE(cover.active(t0 + IntroCover::TOTAL_MS));

	IntroCover skipped;
	skipped.begin(t0, {"Map", {}});
	skipped.skip(t0 + 300);
	REQUIRE(skipped.part(t0 + 300) == 0);
	REQUIRE(skipped.part(t0 + 300 + IntroCover::PART_MS / 4) > .4f);
	REQUIRE_FALSE(skipped.active(t0 + 300 + IntroCover::PART_MS / 2));
}

TEST_CASE("the bank is opaque when closed, thins steadily and is gone when parted")
{
	for (auto [w, h] : {std::pair{960, 540}, std::pair{1920, 1080}, std::pair{390, 844}})
	{
		IntroCover::Layers closed, middle, open, again;
		IntroCover::computeLayers(w, h, 1.25, 0.f, closed);
		// A power-of-two texture of at least a third of the screen's resolution (up to 512).
		for (int texels : {closed.w, closed.h})
			REQUIRE((texels >= 64 && texels <= 512 && (texels & (texels - 1)) == 0));
		REQUIRE((closed.w * 3 >= std::min(w, 1536) && closed.h * 3 >= std::min(h, 1536)));
		REQUIRE(closed.body.size() == size_t(closed.w) * closed.h);
		for (unsigned char a : closed.body)
			REQUIRE(a == 255);

		IntroCover::computeLayers(w, h, 1.25, 1.f, open);
		for (size_t i = 0; i < open.body.size(); ++i)
		{
			REQUIRE(open.body[i] == 0);
			REQUIRE(open.light[i] == 0);
			REQUIRE(open.shadow[i] == 0);
		}

		// Coverage falls steadily as the bank parts.
		double previous = 256;
		for (float part : {0.f, .2f, .4f, .6f, .8f, 1.f})
		{
			IntroCover::Layers l;
			IntroCover::computeLayers(w, h, 2.0, part, l);
			const double coverage = mean(l.body);
			REQUIRE(coverage <= previous);
			previous = coverage;
		}

		IntroCover::computeLayers(w, h, 3.0, .4f, middle);
		for (size_t i = 0; i < middle.body.size(); ++i)
			REQUIRE(middle.light[i] <= middle.body[i]);

		// Pure: the same inputs give the same lattice.
		IntroCover::computeLayers(w, h, 3.0, .4f, again);
		REQUIRE((again.body == middle.body).min());
		REQUIRE((again.shadow == middle.shadow).min());
	}
}
}
