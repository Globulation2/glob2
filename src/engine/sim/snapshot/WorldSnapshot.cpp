// SPDX-License-Identifier: GPL-3.0-or-later
#include "WorldSnapshot.h"
#include "TerrainResourceProperties.h"
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
bool Handle::canPaintFarmAt(std::size_t index) const
{
	checkTileIndex(index);
	if (!resources || !terrain || !catalogs || !growth) return false;
	const auto& cell = resources->cells.at(index);
	const auto& properties = terrain->registry->properties(terrain->identity->at(index));
	const auto type = cell.resource.type;
	const int crop = properties.farmCrop;
	return cell.mayGrow && properties.resourcesGrow
		&& (type == NO_RES_TYPE || type == WHEAT || type == WOOD || type == ALGA)
		&& crop >= 0 && crop < MAX_NB_RESOURCES
		&& terrainSupportsResource(properties, crop, catalogs->shrinkable[crop])
		&& growth->rate(index, crop) != 0;
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
	if (visibility) { const auto& c = visibility->cells.at(index); result.discovered = c.discovered; result.visible = c.visible; }
	return result;
}
} // namespace SimulationSnapshot
