// SPDX-License-Identifier: GPL-3.0-or-later
#include "AIWorldView.h"
#include <algorithm>
#include <stdexcept>

namespace AIEngine
{
namespace
{
template<class View> const View* findSlot(std::span<const View> values, Uint16 gid)
{
	const auto found = std::lower_bound(values.begin(), values.end(), gid,
		[](const View& value, Uint16 id) { return value.identity.gid < id; });
	return found != values.end() && found->identity.gid == gid ? &*found : nullptr;
}
}
const BuildingView* AIWorldView::buildingAtSlot(Uint16 gid) const { return findSlot(buildings, gid); }
const UnitView* AIWorldView::unitAtSlot(Uint16 gid) const { return findSlot(units, gid); }
const BuildingView* AIWorldView::building(BuildingRef identity) const
{
	const auto* value = buildingAtSlot(identity.gid);
	return value && value->identity == identity ? value : nullptr;
}
const UnitView* AIWorldView::unit(UnitRef identity) const
{
	const auto* value = unitAtSlot(identity.gid);
	return value && value->identity == identity ? value : nullptr;
}
TileView AIWorldView::tile(int x, int y) const
{
	return composeTile(tileIndex(x, y));
}
bool AIWorldView::canPaintFarmAt(std::size_t index) const
{
	if (!farmInputs) return false;
	const auto& cell = resourceCells[index];
	const auto& properties = terrain->properties(terrainCells[index]);
	return SimulationSnapshot::canPaintFarm(cell, properties, resourceShrinkable, *growth, index);
}
TileView AIWorldView::composeTile(std::size_t index) const
{
	TileView result;
	if (terrainCells) { result.terrain = terrainCells[index]; result.legacyTerrain = legacyTerrainCells[index]; }
	if (resourceCells) {
		const auto& cell = resourceCells[index];
		result.resource = cell.resource; result.fertility = cell.fertility; result.resourcesMayGrow = cell.mayGrow;
	}
	result.canPaintFarm = canPaintFarmAt(index);
	if (occupancyCells) {
		const auto& cell = occupancyCells[index];
		result.building = cell.building; result.groundUnit = cell.groundUnit;
		result.airUnit = cell.airUnit; result.immobileUnit = cell.immobileUnit;
	}
	if (areaCells) {
		const auto& cell = areaCells[index];
		result.forbidden = cell.forbidden; result.guard = cell.guard; result.clear = cell.clear; result.farm = cell.farm;
	}
	if (discoveredCells) { result.discovered = discoveredCells[index]; result.visible = visibleCells[index]; }
	return result;
}

int AIWorldView::distanceSquared(int x1, int y1, int x2, int y2) const
{
	int dx = normalizeX(x1) - normalizeX(x2), dy = normalizeY(y1) - normalizeY(y2);
	dx = std::min(std::abs(dx), width - std::abs(dx));
	dy = std::min(std::abs(dy), height - std::abs(dy));
	return dx * dx + dy * dy;
}
} // namespace AIEngine
