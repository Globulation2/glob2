// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "field/TerrainGradient.h"
#include "BuildingGradientSearch.h"
#include "MapInternal.h"
#include "Ressource.h"
#include <array>
#include <memory>
#include <vector>

namespace building_gradient
{
struct Cell
{
	std::uint32_t forbidden;
	std::uint16_t building;
	std::uint8_t resource, immobile, team;
	TerrainType terrain = GRASS;
};
struct Terrain
{
	int width = 0, height = 0;
	std::uint32_t generation = 0;
	bool modifiedCosts = false;
	std::vector<Cell> cells;
	std::shared_ptr<const std::vector<TerrainType>> costs;
	std::size_t index(int x, int y) const
	{
		return ((y & (height - 1)) * width) + (x & (width - 1));
	}
};
struct Destination
{
	int gid = 0, x = 0, y = 0, width = 0, radius = 0, swim = 0;
	std::uint32_t identity = 0, epoch = 0, teamMask = 0, allies = 0;
	bool virtualBuilding = false, clearing = false, war = false;
	std::array<bool, BASIC_COUNT> clearingResources{};
};
struct Result
{
	std::vector<std::uint16_t> walking;
	std::array<std::vector<std::uint16_t>, MAX_NB_RESOURCES> trips;
	bool locked = false;
	std::uint8_t resourceState = 0;
	// A cutoff is the first unsettled cost layer, -1 denotes a complete field.
	int walkingCutoff = -1, width = 0, height = 0, swim = 0;
	std::array<int, MAX_NB_RESOURCES> tripCutoff{}, tripLimit{};
	std::shared_ptr<const std::vector<TerrainType>> costs;
	bool modifiedCosts = false;
	Result() { tripCutoff.fill(-1); }
	void materialize()
	{
		auto finish = [&](auto &values, int &cutoff, int limit)
		{
			if (cutoff < 0) return;
			BuildingGradientSearch search;
			search.beginFrozen(width, height, values.data(), swim, costs, modifiedCosts, limit, cutoff);
			search.finish("private_materialize");
			cutoff = -1;
		};
		finish(walking, walkingCutoff, gradient_kernel::COST_LIMIT);
		for (int r = 0; r < MAX_NB_RESOURCES; ++r) finish(trips[r], tripCutoff[r], tripLimit[r]);
		costs.reset();
	}
};
template <class CellAt>
inline std::uint8_t paintGoals(const Terrain &map, const Destination &b, std::uint16_t *field,
							   CellAt cellAt)
{
	bool any = false;
	if (b.virtualBuilding)
		for (int y = -b.radius; y <= b.radius; ++y)
			for (int x = -b.radius; x <= b.radius; ++x)
				if (x * x + y * y <= b.radius * b.radius)
				{
					auto i = map.index(b.x + x, b.y + y);
					const auto c = cellAt(i);
					if (!b.clearing ||
						(c.resource < BASIC_COUNT && b.clearingResources[c.resource]))
					{
						field[i] = GRADIENT_AT_GOAL;
						any = true;
					}
				}
	return b.clearing ? (any ? 1 : 2) : 0;
}
inline std::uint16_t seedCell(const Cell &c, const Destination &b, std::uint16_t initial)
{
	if (c.building != 0xffff)
	{
		if (c.building == b.gid)
			return GRADIENT_AT_GOAL;
		if (!b.virtualBuilding || !b.war || ((1u << c.team) & b.allies))
			return GRADIENT_FORBIDDEN;
		return initial;
	}
	if ((c.forbidden & b.teamMask) ||
		(c.resource != NO_RES_TYPE && !(b.clearing && initial == GRADIENT_AT_GOAL)) ||
		c.immobile != IMMOBILE_UNIT_NONE ||
		(!terrainProperties(c.terrain).walkable &&
		 !(b.swim && terrainProperties(c.terrain).swimmable) &&
		 !(b.clearing && initial == GRADIENT_AT_GOAL)))
		return GRADIENT_FORBIDDEN;
	return initial;
}
inline bool isLocked(const Terrain &map, const Destination &b, const std::uint16_t *field)
{
	if (b.virtualBuilding)
		return false;
	int x = b.x - 1, y = b.y - 1;
	constexpr int dx[4] = {1, 0, -1, 0}, dy[4] = {0, 1, 0, -1};
	for (int d = 0; d < 4; ++d)
		for (int i = 0; i < b.width + 1; ++i)
		{
			if (field[map.index(x, y)])
				return false;
			x += dx[d];
			y += dy[d];
		}
	return true;
}
inline int seedTrip(const Terrain &map, const std::uint16_t *parent, const std::uint16_t *walking,
					std::uint16_t *trip)
{
	std::uint16_t bestSeed = GRADIENT_UNREACHABLE;
	const auto cells = std::size_t(map.width) * map.height;
	for (std::size_t i = 0; i < cells; ++i)
	{
		if (parent[i] != GRADIENT_AT_GOAL)
		{
			trip[i] = parent[i] == GRADIENT_FORBIDDEN ? GRADIENT_FORBIDDEN : GRADIENT_UNREACHABLE;
			continue;
		}
		std::uint16_t best = GRADIENT_UNREACHABLE;
		for (int d = 0; d < 8; ++d)
		{
			auto n =
				map.index(int(i % map.width) + tabClose[d][0], int(i / map.width) + tabClose[d][1]);
			if (parent[n] > GRADIENT_UNREACHABLE && walking[n] > best)
				best = walking[n];
		}
		trip[i] = best;
		bestSeed = std::max(bestSeed, best);
	}
	return GRADIENT_AT_GOAL - bestSeed + 128 * GRADIENT_STEP;
}
// All inputs are immutable; this kernel has no Map, Building, RNG or telemetry access.
inline Result build(const Terrain &map, const Destination &b,
					const std::array<std::vector<std::uint16_t>, MAX_NB_RESOURCES> &resources,
					GradientWorkspace &scratch, const std::vector<std::size_t> *targets = nullptr)
{
	Result result;
	result.width = map.width; result.height = map.height; result.swim = b.swim;
	result.modifiedCosts = map.modifiedCosts; result.costs = targets ? map.costs : nullptr;
	auto &field = result.walking;
	field.assign(map.cells.size(), GRADIENT_UNREACHABLE);
	result.resourceState =
		paintGoals(map, b, field.data(), [&](std::size_t i) { return map.cells[i]; });
	for (std::size_t i = 0; i < field.size(); ++i)
		field[i] = seedCell(map.cells[i], b, field[i]);
	result.locked = isLocked(map, b, field.data());
	const field::Grid geometry{map.width, map.height};
	auto terrainAt = [&](std::size_t i) { return map.cells[i].terrain; };
	if (!result.locked)
	{
		if (targets)
		{
			BuildingGradientSearch search;
			search.beginFrozen(map.width, map.height, field.data(), b.swim, map.costs,
				map.modifiedCosts, gradient_kernel::COST_LIMIT);
			bool children = false;
			for (const auto &parent : resources) children |= !parent.empty();
			if (children) search.finish("round_trip_parent");
			else for (auto cell : *targets) search.resolve(cell, "captured_demand");
			result.walkingCutoff = search.settledCost();
		}
		else gradient_kernel::propagateTerrainField(field.data(), b.swim, gradient_kernel::COST_LIMIT,
			geometry, scratch, terrainAt, map.modifiedCosts);
	}
	for (int r = 0; r < MAX_NB_RESOURCES; ++r)
		if (!resources[r].empty())
		{
			auto &trip = result.trips[r];
			trip.resize(field.size());
			const auto &parent = resources[r];
			const auto limit = seedTrip(map, parent.data(), field.data(), trip.data());
			result.tripLimit[r] = limit;
			if (targets)
			{
				BuildingGradientSearch search;
				search.beginFrozen(map.width, map.height, trip.data(), b.swim, map.costs,
					map.modifiedCosts, limit);
				for (auto cell : *targets) search.resolve(cell, "captured_demand");
				result.tripCutoff[r] = search.settledCost();
			}
			else gradient_kernel::propagateTerrainField(trip.data(), b.swim, limit, geometry,
				scratch, terrainAt, map.modifiedCosts);
		}
	return result;
}
} // namespace building_gradient
