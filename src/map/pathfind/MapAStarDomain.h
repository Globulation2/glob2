// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// World adapters of the point-to-point search (field/astar/AStar.h). Each reads
// the authoritative cell records through MapState::View, which the live Map and
// every engine snapshot expose alike, and answers one cell at a time: the
// search samples a cell when it first touches it.
#include "MapStateView.h"
#include "field/TerrainMovementCosts.h"
#include "field/astar/AStar.h"

namespace MapAStar
{
inline unsigned packedSteps(const CellRule &rule, int movement)
{
	const auto steps = rule.ground[movement];
	return steps.cardinal | steps.diagonal << 8;
}

inline astar::Floor floorOf(gradient_kernel::EntrySteps minima)
{
	return astar::consistentFloor(minima.cardinal, minima.diagonal);
}

// Unit movement on the live map, with Map::isFreeForMovement's rules. A goal
// cell may be entered whenever its terrain admits the class, so attackers can
// path onto an occupied target.
template <bool Air> struct UnitDomain
{
	const MapState::View &view;
	int movement;
	Uint32 teamMask;

	unsigned entry(std::uint32_t i, bool goal) const
	{
		const auto &rule = (*view.rules)[view.cellRules[i]];
		const auto &p = rule.properties;
		const auto &tile = view.occupancy[i];
		if constexpr (Air)
		{
			if (!p.flyable || MapState::resourceBlocksAir(view, i) || tile.airUnit != 0xffff)
				return 0;
		}
		else
		{
			const bool free = (movement != WATER_ONLY_CLASS || p.swimmable) && !MapState::resourceBlocksGround(view, i) &&
							  tile.building == 0xffff && tile.groundUnit == 0xffff &&
							  (p.walkable || (movement > 0 && p.swimmable)) && !(view.areas[i].forbidden & teamMask);
			if (!free && !(goal && gradient_kernel::terrainAllowsGround(p, movement)))
				return 0;
		}
		return packedSteps(rule, movement);
	}
};
} // namespace MapAStar
