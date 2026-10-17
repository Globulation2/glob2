// SPDX-License-Identifier: GPL-3.0-or-later
#include "Map.h"
#include "gradient/GradientRuntime.h"

void Map::resourceSeedChanged(size_t index, unsigned flags)
{
	gradientRuntime->resourceSeeds.changed(index, flags);
    if (flags & (ResourceSeedCache::Terrain | ResourceSeedCache::Forbidden))
        gradientRuntime->directFlight.valid = false;
	// Material availability is not a movement obstacle. Resource replacements
    // invalidate below only when their blocking properties actually change.
    gradientRuntime->safety.invalidate(
        flags & (ResourceSeedCache::Terrain | ResourceSeedCache::Building | ResourceSeedCache::Forbidden),
        flags & (ResourceSeedCache::Terrain | ResourceSeedCache::Forbidden));
}

void Map::invalidateResourceSeeds()
{
	gradientRuntime->resourceSeeds.invalidate();
    gradientRuntime->directFlight.valid = false;
	gradientRuntime->safety.invalidate(true, true);
}

void Map::replaceResource(size_t index, const Resource &resource, const std::array<Uint16, MaterialCount> *stocks)
{
	const bool blockedGround = resourceBlocksGround(index);
	const bool blockedAir = resourceBlocksAir(index);
	const auto before = resourceMaterialMaskAt(index);
	releaseResourceStock(index);
	resourceCells[index].resource = resource;
	markResource(index);
	initializeResourceStock(index, stocks);
	materialStockChanged(index, before);
    if (blockedAir != resourceBlocksAir(index)) gradientRuntime->directFlight.valid = false;
	gradientRuntime->safety.invalidate(blockedGround != resourceBlocksGround(index),
	                                   blockedAir != resourceBlocksAir(index));
}

void Map::replaceTile(size_t index, const Tile &tile)
{
	const Tile old = getTile(index);
	// Sprite changes do not replace a deposit's independently stored stocks.
	// Explicit replaceResource retains its reset-to-definition semantics.
	if (old.resource.type!=tile.resource.type || old.resource.amount!=tile.resource.amount)
		replaceResource(index,tile.resource);
	Resource resolved = resourceCells[index].resource;
	if (resolved.type!=NO_RES_TYPE)
	{
		resolved.variety=tile.resource.variety;
		resolved.animation=tile.resource.animation;
	}
	unsigned changes = 0;
	if (old.building != tile.building) changes |= ResourceSeedCache::Building;
	if (old.forbidden != tile.forbidden) changes |= ResourceSeedCache::Forbidden;
	resourceCells[index] = {resolved, tile.fertility, tile.canResourcesGrow};
	occupancyCells[index].building = tile.building;
	occupancyCells[index].groundUnit = tile.groundUnit;
	occupancyCells[index].airUnit = tile.airUnit;
	areaCells[index] = {tile.forbidden, tile.guardArea, tile.clearArea, tile.farmArea};
	scriptAreaCells[index] = tile.scriptAreas;
	markTerrain(index); markResource(index); markOccupancy(index); markArea(index);
	if (changes) resourceSeedChanged(index, changes);
}

void Map::setAreaMask(size_t index, Uint32 Tile::*field, Uint32 value)
{
	Uint32 MapState::AreaCell::*storedField;
	if (field == &Tile::forbidden) storedField = &MapState::AreaCell::forbidden;
	else if (field == &Tile::guardArea) storedField = &MapState::AreaCell::guard;
	else if (field == &Tile::clearArea) storedField = &MapState::AreaCell::clear;
	else { assert(field == &Tile::farmArea); storedField = &MapState::AreaCell::farm; }
	auto &stored = areaCells[index].*storedField;
	if (stored == value) return;
	stored = value;
	markArea(index);
	if (field == &Tile::forbidden) resourceSeedChanged(index, ResourceSeedCache::Forbidden);
}

void Map::addForbidden(int x, int y, Uint32 team)
{
	const size_t index = coordToIndex(x, y);
	const Uint32 mask = Team::teamNumberToMask(team);
	if ((areaCells[index].forbidden & mask) == mask) return;
	setAreaMask(index, &Tile::forbidden, areaCells[index].forbidden | mask);
	bumpTopologyGeneration();
}

void Map::removeForbidden(int x, int y, Uint32 team)
{
	const size_t index = coordToIndex(x, y);
	const Uint32 mask = Team::teamNumberToMask(team);
	if (!(areaCells[index].forbidden & mask)) return;
	setAreaMask(index, &Tile::forbidden, areaCells[index].forbidden & ~mask);
	bumpTopologyGeneration();
}

void Map::setBuilding(int x, int y, int width, int height, Uint16 building)
{
	for (int yi = y; yi < y + height; ++yi)
		for (int xi = x; xi < x + width; ++xi)
		{
			const size_t index = coordToIndex(xi, yi);
			if (occupancyCells[index].building != building)
			{
				occupancyCells[index].building = building;
				markOccupancy(index);
				resourceSeedChanged(index, ResourceSeedCache::Building);
			}
		}
	// Preserve the previous unconditional generation bump, including empty edits.
	bumpTopologyGeneration();
}

// Recompute only after a navigation constraint changes, not per flyer or tick.
// This cache is derived entirely from saved map state and is never serialized.
bool Map::hasAirRoutingConstraints(Uint32 teamMask) const
{
    if (hasAirTerrainConstraints()) return true;
    auto& cached = gradientRuntime->directFlight;
    if (!cached.valid) {
        cached.forbiddenTeams = 0;
        cached.resourceBlockers = false;
        for (size_t i = 0; i < size; ++i) {
            cached.forbiddenTeams |= areaCells[i].forbidden;
            cached.resourceBlockers |= resourceBlocksAir(i);
        }
        cached.valid = true;
    }
    return cached.resourceBlockers || (cached.forbiddenTeams & teamMask);
}
