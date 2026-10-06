// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "field/RuntimeTerrainGradient.h"
#include "BuildingGradientSearch.h"
#include "MapInternal.h"
#include "Ressource.h"
#include "UnitConsts.h"
#include "sim/snapshot/WorldSnapshot.h"
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
	std::vector<Cell> cells; // Standalone fixtures; live builds use projected components.
	SimulationSnapshot::Handle snapshot;
	std::size_t cellCount() const { return snapshot.terrain ? std::size_t(width) * height : cells.size(); }
	Cell cellAt(std::size_t i) const {
		if (!snapshot.terrain) return cells[i];
		const auto &occupant = snapshot.occupancy->cells[i];
		return {snapshot.areas->cells[i].forbidden, occupant.building,
			snapshot.resources->cells[i].resource.type, occupant.immobileUnit,
			Uint8(occupant.building == 0xffff ? 0 : occupant.building >> 10), (*snapshot.terrain->identity)[i]};
	}
	std::shared_ptr<const std::vector<TerrainType>> costs;
	std::shared_ptr<const TerrainRegistry> registry = TerrainRegistry::builtins();
	std::array<BuildingGradientInputs, SWIM_CLASS_COUNT> inputs;
	std::size_t index(int x, int y) const
	{
		return ((y & (height - 1)) * width) + (x & (width - 1));
	}
};
struct Destination
{
	int gid = 0, x = 0, y = 0, width = 0, height = 1, radius = 0, swim = 0;
	int route = 0;
	bool occupiesGround = true;
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
	BuildingGradientInputs inputs;
	bool modifiedCosts = false;
	Result() { tripCutoff.fill(-1); }
	void materialize()
	{
		auto finish = [&](auto &values, int &cutoff, int limit)
		{
			if (cutoff < 0) return;
			BuildingGradientSearch search;
			search.beginFrozen(width, height, values.data(), swim, inputs, limit, cutoff);
			search.finish("private_materialize");
			cutoff = -1;
		};
		finish(walking, walkingCutoff, gradient_kernel::COST_LIMIT);
		for (int r = 0; r < MAX_NB_RESOURCES; ++r) finish(trips[r], tripCutoff[r], tripLimit[r]);
		costs.reset();
		inputs={};
	}
};
template <class CellAt>
inline std::uint8_t paintGoals(const Terrain &map, const Destination &b, std::uint16_t *field,
							   CellAt cellAt)
{
	bool any = false;
	if (b.route != 0)
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
inline std::uint16_t seedCell(const Cell &c, const Destination &b, std::uint16_t initial, const TerrainRegistry &registry)
{
	if (c.building != 0xffff)
	{
		if (b.route == 0 && c.building == b.gid)
			return GRADIENT_AT_GOAL;
		if (b.route == 0 || !b.war || ((1u << c.team) & b.allies))
			return GRADIENT_FORBIDDEN;
		return initial == GRADIENT_AT_GOAL ? initial : GRADIENT_UNREACHABLE;
	}
	if ((c.forbidden & b.teamMask) ||
		(c.resource != NO_RES_TYPE && !(b.clearing && initial == GRADIENT_AT_GOAL)) ||
		c.immobile != IMMOBILE_UNIT_NONE ||
		(!registry.properties(c.terrain).walkable &&
		 !(b.swim && registry.properties(c.terrain).swimmable) &&
		 !(b.clearing && initial == GRADIENT_AT_GOAL)))
		return GRADIENT_FORBIDDEN;
	return initial;
}
inline bool isLocked(const Terrain &map, const Destination &b, const std::uint16_t *field)
{
	if (b.route != 0) return false;
 for(int x=-1;x<=b.width;++x) if(field[map.index(b.x+x,b.y-1)] || field[map.index(b.x+x,b.y+b.height)]) return false;
 for(int y=0;y<b.height;++y) if(field[map.index(b.x-1,b.y+y)] || field[map.index(b.x+b.width,b.y+y)]) return false;
	return true;
}
inline int seedTrip(const Terrain &map, const std::uint16_t *parent, const std::uint16_t *walking,
					std::uint16_t *trip, const std::vector<std::uint16_t> *supplierGoals = nullptr)
{
	std::uint16_t bestSeed = GRADIENT_UNREACHABLE;
	const auto cells = std::size_t(map.width) * map.height;
	for (std::size_t i = 0; i < cells; ++i)
	{
		const auto supplierPenalty = supplierGoals && !supplierGoals->empty() ? (*supplierGoals)[i] : 0;
		if (parent[i] != GRADIENT_AT_GOAL && !supplierPenalty)
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
		// A ground supplier replaces the natural seed. An overlay supplier adds
		// a competing seed, so a natural goal on the same tile remains usable.
		if (supplierPenalty && best > GRADIENT_UNREACHABLE &&
			(map.cellAt(i).building != std::uint16_t(0xffff) || parent[i] != GRADIENT_AT_GOAL))
			best = std::max<int>(GRADIENT_UNREACHABLE + 1, best - (supplierPenalty - 1));
		trip[i] = best;
		bestSeed = std::max(bestSeed, best);
	}
	return GRADIENT_AT_GOAL - bestSeed + 128 * GRADIENT_STEP;
}
// All inputs are immutable; this kernel has no Map, Building, RNG or telemetry access.
inline Result build(const Terrain &map, const Destination &b,
					const std::array<std::vector<std::uint16_t>, MAX_NB_RESOURCES> &resources,
					GradientWorkspace &scratch, const std::vector<std::size_t> *targets = nullptr,
 const std::array<std::vector<std::uint16_t>, MAX_NB_RESOURCES> *supplierGoals = nullptr)
{
	Result result;
	result.width = map.width; result.height = map.height; result.swim = b.swim;
	result.modifiedCosts = map.modifiedCosts; result.costs = targets ? map.costs : nullptr;
	result.inputs=map.inputs[b.swim];
	// Legacy standalone fixtures supply terrain without a prepared movement lease.
	if(!result.inputs.terrain && !result.inputs.profiles && !result.inputs.water) {
	 result.inputs.modified=map.modifiedCosts; result.inputs.registry=map.registry;
	 if(map.costs) result.inputs.terrain=map.costs;
	 else { auto costs=std::make_shared<std::vector<TerrainType>>(); costs->reserve(map.cellCount()); for(const auto &c:map.cells) costs->push_back(c.terrain); result.inputs.terrain=costs; }
	}
	auto &field = result.walking;
	field.assign(map.cellCount(), GRADIENT_UNREACHABLE);
	result.resourceState =
		paintGoals(map, b, field.data(), [&](std::size_t i) { return map.cellAt(i); });
	for (std::size_t i = 0; i < field.size(); ++i)
		field[i] = seedCell(map.cellAt(i), b, field[i], *map.registry);
	if(b.route==0 && !b.occupiesGround) for(int y=0;y<b.height;++y) for(int x=0;x<b.width;++x) field[map.index(b.x+x,b.y+y)]=GRADIENT_AT_GOAL;
	result.locked = isLocked(map, b, field.data());
	// Eager bundles use the same reusable bucket kernels as live fields. Partial
	// bundles retain a resumable search instead of allocating a full private queue.
	auto propagate = [&](std::uint16_t *values, int limit)
	{
		const auto &inputs = result.inputs;
		const field::Grid grid{map.width, map.height};
		if (inputs.modified && inputs.profiles)
			gradient_kernel::propagateTerrainProfiles(values, b.swim, limit, grid, scratch,
				inputs.profiles->data(), inputs.profiles->movement, inputs.buckets);
		else if (!inputs.modified && inputs.water && gradient_kernel::weightedClass(b.swim))
			gradient_kernel::propagateField(values, b.swim, limit, grid, scratch,
				[&](std::size_t i) { return (*inputs.water)[i] != 0; });
		else
			gradient_kernel::propagateTerrainField(values, b.swim, limit, grid, scratch,
				[&](std::size_t i) { return (*inputs.terrain)[i]; }, inputs.modified,
				*inputs.registry, inputs.buckets);
	};
	if (!result.locked)
	{
		if (targets)
		{
			BuildingGradientSearch search;
			search.beginFrozen(map.width, map.height, field.data(), b.swim, result.inputs, gradient_kernel::COST_LIMIT);
			bool children = false;
			for (const auto &parent : resources) children |= !parent.empty();
			if (children) search.finish("round_trip_parent");
			else for (auto cell : *targets) search.resolve(cell, "captured_demand");
			result.walkingCutoff = search.settledCost();
		}
		else propagate(field.data(), gradient_kernel::COST_LIMIT);
	}
	for (int r = 0; r < MAX_NB_RESOURCES; ++r)
		if (!resources[r].empty())
		{
			auto &trip = result.trips[r];
			trip.resize(field.size());
			const auto &parent = resources[r];
			const auto limit = seedTrip(map, parent.data(), field.data(), trip.data(), supplierGoals ? &(*supplierGoals)[r] : nullptr);
			result.tripLimit[r] = limit;
			if (targets)
			{
				BuildingGradientSearch search;
				search.beginFrozen(map.width, map.height, trip.data(), b.swim, result.inputs, limit);
				for (auto cell : *targets) search.resolve(cell, "captured_demand");
				result.tripCutoff[r] = search.settledCost();
			}
			else propagate(trip.data(), limit);
		}
	return result;
}
} // namespace building_gradient
