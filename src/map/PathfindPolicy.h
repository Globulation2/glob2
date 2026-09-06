// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Per-team pathfinder selection for A/B simulations. GLOB2_PATHFIND_ALT_TEAMS
// is a bitmask of team numbers that use the alternative pathfinder; all
// other teams use the baseline gradient pathfinder. Read once at first use.
// The mask is part of the run configuration, like the seed: two runs with
// the same seed and mask are deterministic, runs with different masks are not
// comparable replay-for-replay.

#pragma once

#include <cstdint>
#include <cstdlib>

namespace PathfindPolicy
{
	inline std::uint32_t altTeamMask()
	{
		static const std::uint32_t mask = []() {
			const char* env = std::getenv("GLOB2_PATHFIND_ALT_TEAMS");
			return env ? (std::uint32_t)std::strtoul(env, nullptr, 0) : 0u;
		}();
		return mask;
	}

	inline bool useAlternative(int teamNumber)
	{
		return (altTeamMask() >> teamNumber) & 1u;
	}
}
