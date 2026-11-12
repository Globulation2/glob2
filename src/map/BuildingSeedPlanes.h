// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Data of the building seed templates (gradient/BuildingSeedTemplates.h),
// shared by the live map and engine snapshots. Derived data only: never saved.

#include "TeamCapacity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace MapState
{
//! The four passability kinds a building seed distinguishes. Swim ratios only
//! change propagation costs, so amphibious classes 1..6 share one template.
enum class SeedPassability : unsigned char { Walk, WalkSwim, WaterOnly, Air };
inline constexpr std::size_t SeedPassabilityCount = 4;

struct BuildingSeedPlanes
{
	using Bits = std::vector<std::uint64_t>;
	//! GRADIENT_FORBIDDEN where any building, blocking resource, immobile unit
	//! or closed terrain stops this kind; GRADIENT_UNREACHABLE elsewhere. Air
	//! ignores buildings and immobile units, as the seeder does.
	std::array<std::vector<std::uint16_t>, SeedPassabilityCount> planes;
	//! Per team: cells painted forbidden for that team.
	std::array<Bits, TEAM_MAX_COUNT> forbidden;
	//! Per team: cells occupied by one of that team's buildings.
	std::array<Bits, TEAM_MAX_COUNT> buildings;
	std::size_t cells = 0;

	const std::uint16_t* plane(SeedPassability kind) const { return planes[std::size_t(kind)].data(); }
	bool empty() const { return cells == 0; }
	std::size_t capacityBytes() const
	{
		std::size_t bytes = 0;
		for (const auto& plane : planes) bytes += plane.capacity() * sizeof(std::uint16_t);
		for (const auto& bits : forbidden) bytes += bits.capacity() * sizeof(std::uint64_t);
		for (const auto& bits : buildings) bytes += bits.capacity() * sizeof(std::uint64_t);
		return bytes;
	}
};

//! A captured copy. Chunks whose stamp equals the live stamp are identical to
//! the live planes; (world, epoch) name the live set the stamps belong to.
struct BuildingSeedSnapshot
{
	BuildingSeedPlanes planes;
	std::vector<std::uint64_t> stamps;
	std::uint64_t world = 0, epoch = 0, generation = 0;
};
} // namespace MapState
