// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the swarm spawn effect's timing (UnitEffects.h). The properties
// of interest: the unit starts invisible and ends drawn normally, its silhouette
// settles monotonically, the swarm glow and dust are over within the record's
// lifetime, and a spawn's particles are the same from frame to frame, so they
// move smoothly rather than jumping.

#include "Glob2Test.h"

#include "UnitEffects.h"

#include <cmath>
#include <vector>

namespace
{
std::vector<UnitEffect::Sparkle> motes(std::uint32_t seed, float age)
{
	std::vector<UnitEffect::Sparkle> result;
	UnitEffect::forEachDustMote(seed, age, [&](const UnitEffect::Sparkle &mote) { result.push_back(mote); });
	return result;
}
}

TEST_SUITE("UnitEffect")
{
	TEST_CASE("A produced unit fades in and its silhouette settles")
	{
		const auto first = UnitEffect::unitLook(0);
		CHECK(first.opacity == 0);
		CHECK(first.silhouette == doctest::Approx(0.95f));
		CHECK(first.slide > 0);
		float previous = first.silhouette;
		for (float age = 0.25f; age <= UnitEffect::ShimmerTicks; age += 0.25f)
		{
			const auto look = UnitEffect::unitLook(age);
			CHECK(look.silhouette <= previous + 1e-6f);
			previous = look.silhouette;
		}
		const auto settled = UnitEffect::unitLook(UnitEffect::ShimmerTicks);
		CHECK(settled.opacity == 1);
		CHECK(settled.slide == 0);
		CHECK(settled.silhouette == 0);
		CHECK(UnitEffect::unitLook(UnitEffect::EmergeTicks).opacity == 1);
	}

	TEST_CASE("The swarm glow peaks once and is over within the lifetime")
	{
		CHECK(UnitEffect::swarmGlow(-1) == 0);
		CHECK(UnitEffect::swarmGlow(0) == 0);
		CHECK(UnitEffect::swarmGlow(UnitEffect::PulseRiseTicks) ==
			doctest::Approx(UnitEffect::SwarmGlow * UnitEffect::PulseStrength));
		for (float age = 0; age < UnitEffect::PulseRiseTicks + UnitEffect::PulseFallTicks; age += 0.5f)
			CHECK(UnitEffect::swarmGlow(age) <= UnitEffect::SwarmGlow * UnitEffect::PulseStrength + 1e-6f);
		CHECK(UnitEffect::swarmGlow(UnitEffect::PulseRiseTicks + UnitEffect::PulseFallTicks) == 0);
		CHECK(UnitEffect::swarmGlow(UnitEffect::LifetimeTicks) == 0);
	}

	TEST_CASE("Dust appears with the unit and is gone by its duration")
	{
		CHECK(motes(7, -0.5f).empty());
		CHECK(motes(7, UnitEffect::DustTicks).empty());
		const auto early = motes(7, 2);
		CHECK(early.size() == UnitEffect::DustMotes);
		for (const auto &mote : early)
		{
			CHECK(mote.alpha >= 0);
			CHECK(mote.alpha <= UnitEffect::DustIntensity);
			CHECK(mote.glint <= UnitEffect::DustIntensity * UnitEffect::DustGlint);
		}
		int glows = 0;
		UnitEffect::forEachDustGlow(UnitEffect::DustTicks, [&](const UnitEffect::Glow &) { ++glows; });
		CHECK(glows == 0);
		CHECK(UnitEffect::DustTicks <= UnitEffect::LifetimeTicks);
	}

	TEST_CASE("A spawn's motes keep their identity from frame to frame")
	{
		CHECK(motes(42, 6).size() == motes(42, 6).size());
		const auto a = motes(42, 6), b = motes(42, 6.05f);
		REQUIRE(a.size() == b.size());
		for (size_t i = 0; i < a.size(); ++i)
		{
			CHECK(std::fabs(a[i].x - b[i].x) < 1);
			CHECK(std::fabs(a[i].y - b[i].y) < 1);
		}
		// Different spawns do not share a cloud.
		const auto other = motes(43, 6);
		REQUIRE(other.size() > 0);
		CHECK((other[0].x != a[0].x || other[0].y != a[0].y));
		CHECK(UnitEffect::seedOf(3, 4, 10) != UnitEffect::seedOf(4, 3, 10));
	}

	TEST_CASE("Shimmer twinkles finish within the lifetime")
	{
		int count = 0;
		UnitEffect::forEachShimmerTwinkle(5, UnitEffect::TwinklesEnd, [&](const UnitEffect::Sparkle &) { ++count; });
		CHECK(count == 0);
		UnitEffect::forEachShimmerTwinkle(5, UnitEffect::ShimmerTicks / 2, [&](const UnitEffect::Sparkle &) { ++count; });
		CHECK(count > 0);
	}

	TEST_CASE("A serviced unit's shimmer settles over a second and a half")
	{
		CHECK(UnitEffect::serviceSilhouette(0) == doctest::Approx(UnitEffect::ServiceShimmerPeak));
		float previous = UnitEffect::serviceSilhouette(0);
		for (float age = 0.5f; age <= UnitEffect::ServiceShimmerTicks; age += 0.5f)
		{
			CHECK(UnitEffect::serviceSilhouette(age) <= previous + 1e-6f);
			previous = UnitEffect::serviceSilhouette(age);
		}
		CHECK(UnitEffect::serviceSilhouette(UnitEffect::ServiceShimmerTicks) == 0);
	}

	TEST_CASE("Service icons scatter, keep clear of the level number and finish")
	{
		int seen = 0;
		for (std::uint32_t seed = 1; seed < 200; ++seed)
			for (float age = 0; age < UnitEffect::IconDelayTicks + UnitEffect::IconTicks; age += 0.5f)
				UnitEffect::forEachServiceIcon(seed, age, [&](const UnitEffect::Icon &icon) {
					++seen;
					CHECK(icon.alpha >= 0);
					CHECK(icon.alpha <= 1);
					// Level numbers rise from 32 pixels above the tile centre, centred on it.
					CHECK_FALSE((icon.y < -24 && std::fabs(icon.x) < 10));
				});
		CHECK(seen > 0);
		int late = 0;
		UnitEffect::forEachServiceIcon(9, UnitEffect::IconDelayTicks + UnitEffect::IconTicks, [&](const UnitEffect::Icon &) { ++late; });
		CHECK(late == 0);
		// Identity from frame to frame: small steps move icons a little.
		std::vector<UnitEffect::Icon> a, b;
		UnitEffect::forEachServiceIcon(42, 10, [&](const UnitEffect::Icon &icon) { a.push_back(icon); });
		UnitEffect::forEachServiceIcon(42, 10.05f, [&](const UnitEffect::Icon &icon) { b.push_back(icon); });
		REQUIRE(a.size() == b.size());
		for (size_t i = 0; i < a.size(); ++i)
			CHECK(std::hypot(a[i].x - b[i].x, a[i].y - b[i].y) < 2);
	}

	TEST_CASE("A trained unit's level number rises and fades like a combat level-up")
	{
		CHECK(UnitEffect::levelNumber(0).alpha == 1);
		CHECK(UnitEffect::levelNumber(10).rise == 20);
		CHECK(UnitEffect::levelNumber(UnitEffect::LevelTicks).alpha == 0);
	}
}
