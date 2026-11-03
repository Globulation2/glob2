// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the swarm spawn effect's timing (SpawnEffects.h). The properties
// of interest: the unit starts invisible and ends drawn normally, its silhouette
// settles monotonically, the swarm glow and dust are over within the record's
// lifetime, and a spawn's particles are the same from frame to frame, so they
// move smoothly rather than jumping.

#include "Glob2Test.h"

#include "SpawnEffects.h"

#include <cmath>
#include <vector>

namespace
{
std::vector<SpawnEffect::Sparkle> motes(std::uint32_t seed, float age)
{
	std::vector<SpawnEffect::Sparkle> result;
	SpawnEffect::forEachDustMote(seed, age, [&](const SpawnEffect::Sparkle &mote) { result.push_back(mote); });
	return result;
}
}

TEST_SUITE("SpawnEffect")
{
	TEST_CASE("A produced unit fades in and its silhouette settles")
	{
		const auto first = SpawnEffect::unitLook(0);
		CHECK(first.opacity == 0);
		CHECK(first.silhouette == doctest::Approx(0.95f));
		CHECK(first.slide > 0);
		float previous = first.silhouette;
		for (float age = 0.25f; age <= SpawnEffect::ShimmerTicks; age += 0.25f)
		{
			const auto look = SpawnEffect::unitLook(age);
			CHECK(look.silhouette <= previous + 1e-6f);
			previous = look.silhouette;
		}
		const auto settled = SpawnEffect::unitLook(SpawnEffect::ShimmerTicks);
		CHECK(settled.opacity == 1);
		CHECK(settled.slide == 0);
		CHECK(settled.silhouette == 0);
		CHECK(SpawnEffect::unitLook(SpawnEffect::EmergeTicks).opacity == 1);
	}

	TEST_CASE("The swarm glow peaks once and is over within the lifetime")
	{
		CHECK(SpawnEffect::swarmGlow(-1) == 0);
		CHECK(SpawnEffect::swarmGlow(0) == 0);
		CHECK(SpawnEffect::swarmGlow(SpawnEffect::PulseRiseTicks) ==
			doctest::Approx(SpawnEffect::SwarmGlow * SpawnEffect::PulseStrength));
		for (float age = 0; age < SpawnEffect::PulseRiseTicks + SpawnEffect::PulseFallTicks; age += 0.5f)
			CHECK(SpawnEffect::swarmGlow(age) <= SpawnEffect::SwarmGlow * SpawnEffect::PulseStrength + 1e-6f);
		CHECK(SpawnEffect::swarmGlow(SpawnEffect::PulseRiseTicks + SpawnEffect::PulseFallTicks) == 0);
		CHECK(SpawnEffect::swarmGlow(SpawnEffect::LifetimeTicks) == 0);
	}

	TEST_CASE("Dust appears with the unit and is gone by its duration")
	{
		CHECK(motes(7, -0.5f).empty());
		CHECK(motes(7, SpawnEffect::DustTicks).empty());
		const auto early = motes(7, 2);
		CHECK(early.size() == SpawnEffect::DustMotes);
		for (const auto &mote : early)
		{
			CHECK(mote.alpha >= 0);
			CHECK(mote.alpha <= SpawnEffect::DustIntensity);
			CHECK(mote.glint <= SpawnEffect::DustIntensity * SpawnEffect::DustGlint);
		}
		int glows = 0;
		SpawnEffect::forEachDustGlow(SpawnEffect::DustTicks, [&](const SpawnEffect::Glow &) { ++glows; });
		CHECK(glows == 0);
		CHECK(SpawnEffect::DustTicks <= SpawnEffect::LifetimeTicks);
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
		CHECK(SpawnEffect::seedOf(3, 4, 10) != SpawnEffect::seedOf(4, 3, 10));
	}

	TEST_CASE("Shimmer twinkles finish within the lifetime")
	{
		int count = 0;
		SpawnEffect::forEachShimmerTwinkle(5, SpawnEffect::TwinklesEnd, [&](const SpawnEffect::Sparkle &) { ++count; });
		CHECK(count == 0);
		SpawnEffect::forEachShimmerTwinkle(5, SpawnEffect::ShimmerTicks / 2, [&](const SpawnEffect::Sparkle &) { ++count; });
		CHECK(count > 0);
	}
}
