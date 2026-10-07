// SPDX-License-Identifier: GPL-3.0-or-later
#include "WorldSnapshot.h"
#include "TerrainResourceProperties.h"
#include <bit>
#include <algorithm>
namespace SimulationSnapshot
{
Handle Handle::project(Requirements requested) const
{
	if ((requested & requirements) != requested) throw std::invalid_argument("snapshot component was not captured");
	Handle result;
	result.tick = tick; result.width = width; result.height = height; result.requirements = requested;
	result.configurationRevision = configurationRevision; result.worldIdentity = worldIdentity; result.mapGenerations = mapGenerations;
	if (needs(requested, Component::Catalogs)) result.catalogs = catalogs;
	if (needs(requested, Component::Terrain)) result.terrain = terrain;
	if (needs(requested, Component::Resources)) result.resources = resources;
	if (needs(requested, Component::Occupancy)) result.occupancy = occupancy;
	if (needs(requested, Component::Areas)) result.areas = areas;
	if (needs(requested, Component::Visibility)) result.visibility = visibility;
	if (needs(requested, Component::Entities)) result.entities = entities;
	if (needs(requested, Component::Teams)) result.teams = teams;
	if (needs(requested, Component::Rules)) result.rules = rules;
	if (needs(requested, Component::Growth)) result.growth = growth;
	if (needs(requested, Component::ResourceFields)) result.resourceFields = resourceFields;
	return result;
}
MapState::View Handle::view() const
{
	MapState::View v;
	v.width = width; v.height = height;
	v.wDec = width > 0 ? unsigned(std::countr_zero(unsigned(width))) : 0;
	v.wMask = Uint32(width - 1); v.hMask = Uint32(height - 1);
	if (resources) { v.resources = resources->cells; v.stockIndices = resources->stockIndices; v.stocks = resources->stocks; v.materialSourceCounts = resources->materialSourceCounts; }
	if (occupancy) v.occupancy = occupancy->cells;
	if (areas) v.areas = areas->cells;
	if (terrain) { if (terrain->identity) v.terrainIds = *terrain->identity; v.legacyTerrain = terrain->legacy; v.terrainRegistry = terrain->registry.get(); }
	if (catalogs) { v.resourceRegistry = catalogs->resources.get(); v.habitats = catalogs->habitats.get(); }
	v.growth = growth.get();
	if (rules) { v.resourceGrowthDisabled = rules->values.resourceGrowthDisabled; v.resourceScarcityLevel = rules->configuration ? int(rules->configuration->getResourceScarcityLevel()) : 0; }
	return v;
}
bool Handle::canPaintFarmAt(std::size_t index) const
{
	checkTileIndex(index);
	if (!resources || !terrain || !catalogs || !catalogs->resources || !catalogs->habitats || !growth || !rules) return false;
	const auto v = view();
	return MapState::canPaintFarmArea(v, int(index & v.wMask), int(index >> v.wDec));
}
TileView Handle::tileAt(std::size_t index) const
{
	if (index >= std::size_t(width) * height) throw std::out_of_range("snapshot tile index");
	TileView result;
	if (terrain) { result.terrain = terrain->identity->at(index); result.legacyTerrain = terrain->legacy.at(index); }
	if (resources) { const auto& c = resources->cells.at(index); result.resource = c.resource; result.fertility = c.fertility; result.resourcesMayGrow = c.mayGrow; }
	// Derived ecology is read from frozen inputs on demand. Extraction never
	// invokes a growth-cache query (or takes its lock) once per map cell.
	result.canPaintFarm = canPaintFarmAt(index);
	if (occupancy) { const auto& c = occupancy->cells.at(index); result.building = c.building; result.groundUnit = c.groundUnit; result.airUnit = c.airUnit; result.immobileUnit = c.immobileUnit; }
	if (areas) { const auto& c = areas->cells.at(index); result.forbidden = c.forbidden; result.guard = c.guard; result.clear = c.clear; result.farm = c.farm; }
	if (visibility) { result.discovered = visibility->discovered.at(index); result.visible = visibility->visible.at(index); }
	return result;
}
} // namespace SimulationSnapshot
