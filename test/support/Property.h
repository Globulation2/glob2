// SPDX-License-Identifier: GPL-3.0-or-later
// Seeded property loops for randomized tests. forEachSeed(n, fn) calls fn(seed) for n
// consecutive seeds and attaches the seed to every assertion that fails inside fn, with
// the environment setting that replays it:
//
//   GLOB2_SEED=<n>             run only seed n (any value, not just a default seed)
//   GLOB2_PROPERTY_ITERS=<k>   run k times as many seeds as the test asks for
//
// Pass the historical seed of a converted fixed-seed loop as firstSeed, so the default
// run still covers the sequence the test always checked.
#pragma once

#include "Glob2Test.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>

namespace glob2test
{
	namespace property
	{
		// An unsigned 32-bit environment value, or nothing when the variable is unset
		// or empty. A malformed value fails the test rather than silently running the
		// default seeds.
		inline std::optional<std::uint32_t> environmentValue(const char* name)
		{
			const char* text = std::getenv(name);
			if (!text || !*text)
				return std::nullopt;
			errno = 0;
			char* end = nullptr;
			const unsigned long long value = std::strtoull(text, &end, 10);
			const bool valid = *text != '-' && end && *end == '\0' && errno == 0 && value <= UINT32_MAX;
			const std::string message = std::string(name) + "=" + text + " is not an unsigned 32-bit integer";
			REQUIRE_MESSAGE(valid, message);
			return static_cast<std::uint32_t>(value);
		}

		// The seed GLOB2_SEED selects, if any.
		inline std::optional<std::uint32_t> replaySeed() { return environmentValue("GLOB2_SEED"); }

		// The GLOB2_PROPERTY_ITERS multiplier, at least 1.
		inline std::uint32_t iterationScale()
		{
			const auto scale = environmentValue("GLOB2_PROPERTY_ITERS").value_or(1);
			REQUIRE_MESSAGE(scale >= 1, "GLOB2_PROPERTY_ITERS must be at least 1");
			return scale;
		}
	}

	// Calls fn(seed) for seeds firstSeed, firstSeed+1, ... (count of them, times
	// GLOB2_PROPERTY_ITERS), or only for GLOB2_SEED when that is set. fn takes a
	// std::uint32_t; seed its own generator from it so each seed replays alone.
	template <typename Fn>
	void forEachSeed(std::uint32_t count, Fn&& fn, std::uint32_t firstSeed = 1)
	{
		const auto run = [&](std::uint32_t seed)
		{
			const std::string replay = "property seed " + std::to_string(seed) +
				"; replay it alone with GLOB2_SEED=" + std::to_string(seed);
			INFO(replay);
			CAPTURE(seed);
			fn(seed);
		};
		if (const auto seed = property::replaySeed())
		{
			run(*seed);
			return;
		}
		const std::uint64_t total = std::uint64_t(count) * property::iterationScale();
		for (std::uint64_t i = 0; i < total; ++i)
			run(static_cast<std::uint32_t>(firstSeed + i));
	}
}
