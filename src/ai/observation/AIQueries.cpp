// SPDX-License-Identifier: GPL-3.0-or-later
#include "AIWorldView.h"
#include "Building.h"
#include "Map.h"
#include <algorithm>
#include <stdexcept>

namespace AIEngine
{
const BuildingView* AIWorldView::buildingAtSlot(Uint16 gid) const
{
	if (!lease.entities || gid >= lease.entities->buildingSlotIndices.size()) return nullptr;
	const auto index = lease.entities->buildingSlotIndices[gid];
	return index == SimulationSnapshot::Entities::NoRecord ? nullptr : &buildings[index];
}
const UnitView* AIWorldView::unitAtSlot(Uint16 gid) const
{
	if (!lease.entities || gid >= lease.entities->unitSlotIndices.size()) return nullptr;
	const auto index = lease.entities->unitSlotIndices[gid];
	return index == SimulationSnapshot::Entities::NoRecord ? nullptr : &units[index];
}
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

bool AIWorldView::isUpgradeAvailable(const BuildingView& building) const
{
	const int next = catalog->at(building.typeNum).next;
	return next >= 0 && std::size_t(next) < catalog->size() && catalog->at(next).available;
}
bool AIWorldView::isHardSpaceForBuildingSite(const BuildingView& building, bool upgrade) const
{
	const auto& kind = catalog->at(building.typeNum);
	if (upgrade && rules.upgradesDisabled) return false;
	if (!upgrade && !kind.semantics.repairable) return false;
	const int next = upgrade ? kind.next : kind.previous;
	if (next == BUILDING_LEVEL_NONE) return true;
	const auto& target = catalog->at(next);
	if (target.isVirtual) return true;
	const int x = building.posX + target.decLeft - kind.decLeft;
	const int y = building.posY + target.decTop - kind.decTop;
	for (int dy = 0; dy < target.height; ++dy) for (int dx = 0; dx < target.width; ++dx) {
		const auto index = tileIndex(x + dx, y + dy);
		if (resourceAt(index).resource.type != NO_RES_TYPE) return false;
		const auto occupant = occupancyAt(index).building;
		if (occupant != NOGBID && occupant != building.identity.gid) return false;
		if (!terrain->properties(terrainAt(index).type).buildable) return false;
	}
	return true;
}

int AIWorldView::distanceSquared(int x1, int y1, int x2, int y2) const
{
	int dx = normalizeX(x1) - normalizeX(x2), dy = normalizeY(y1) - normalizeY(y2);
	dx = std::min(std::abs(dx), width - std::abs(dx));
	dy = std::min(std::abs(dy), height - std::abs(dy));
	return dx * dx + dy * dy;
}
} // namespace AIEngine
