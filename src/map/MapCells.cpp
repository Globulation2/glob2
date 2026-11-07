// SPDX-License-Identifier: GPL-3.0-or-later
#include "Map.h"
#include "gradient/MapRouting.h"
#include "gradient/GradientRuntime.h"
#include "gradient/GradientTargetCell.h"
#include <bit>

void Map::paintedAreaMaskChanged(AreaType kind, Uint32 before, Uint32 after)
{
    for (auto changed=before^after; changed; changed&=changed-1) {
        const auto team=std::countr_zero(changed);
        if (team>=Team::MAX_COUNT) continue; // Old masks retain 32 on-disk bits.
        auto& count=paintedAreaCounts[kind][team];
        if (after&(Uint32(1)<<team)) ++count;
        else { assert(count); --count; }
    }
}

void Map::rebuildPaintedAreaCounts()
{
    paintedAreaCounts={};
    for (const auto& area:areaCells) {
        paintedAreaMaskChanged(ClearingArea,0,area.clear);
        paintedAreaMaskChanged(ForbiddenArea,0,area.forbidden);
        paintedAreaMaskChanged(GuardArea,0,area.guard);
        paintedAreaMaskChanged(FarmArea,0,area.farm);
    }
}

void Map::resourceSeedChanged(size_t index, unsigned flags)
{
    // Revalidate endpoints after mutation. A pooled source can remain usable
    // behind forbidden paint or obstacles: workers approach its separate access
    // tile. Moving traffic never cancels the reservation.
    if (activeClaimsEnabled() && flags&(ResourceSeedCache::Terrain|ResourceSeedCache::Building|ResourceSeedCache::Forbidden)) {
        auto owners=materialClaims.ownersAtSource(Uint32(index));
        const auto access=materialClaims.ownersAtAccess(Uint32(index));
        owners.insert(owners.end(),access.begin(),access.end());
        revalidateMaterialClaimOwners(std::move(owners),false);
    }
	routing().gradientRuntime->resourceSeeds.changed(index, flags);
    if (flags & ResourceSeedCache::Terrain)
        routing().gradientRuntime->directFlight.valid = false;
	// Material availability is not a movement obstacle. Resource replacements
    // invalidate below only when their blocking properties actually change.
    routing().gradientRuntime->safety.invalidate(
        flags & (ResourceSeedCache::Terrain | ResourceSeedCache::Building | ResourceSeedCache::Forbidden),
        flags & ResourceSeedCache::Terrain);
}

void Map::invalidateResourceSeeds()
{
	routing().gradientRuntime->resourceSeeds.invalidate();
    routing().gradientRuntime->directFlight.valid = false;
	routing().gradientRuntime->safety.invalidate(true, true);
}

void Map::replaceResource(size_t index, const Resource &resource, const std::array<Uint16, MaterialCount> *stocks)
{
    auto claimOwners=farmMaterialClaimOwners(index);
    if (activeClaimsEnabled()) {
        const auto source=materialClaims.ownersAtSource(Uint32(index)),access=materialClaims.ownersAtAccess(Uint32(index));
        claimOwners.insert(claimOwners.end(),source.begin(),source.end());
        claimOwners.insert(claimOwners.end(),access.begin(),access.end());
    }
	const auto targetBefore=routing().gradientRuntime->adaptive.fields.empty() ? std::optional<GradientTargetCell>{}
		: std::optional{routing().captureGradientTargetCell(index)};
	const bool blockedGround = resourceBlocksGround(index);
	const bool blockedAir = resourceBlocksAir(index);
	const auto before = resourceMaterialMaskAt(index);
    const auto beforeAvailable=materialMaskAt(index);
	releaseResourceStock(index);
	resourceCells[index].resource = resource;
	markResource(index);
	initializeResourceStock(index, stocks);
	materialStockChanged(index, before, beforeAvailable, false);
    if (blockedAir != resourceBlocksAir(index)) routing().gradientRuntime->directFlight.valid = false;
	routing().gradientRuntime->safety.invalidate(blockedGround != resourceBlocksGround(index),
	                                   blockedAir != resourceBlocksAir(index));
	if(targetBefore) routing().gradientTargetCellChanged(index,*targetBefore);
    revalidateMaterialClaimOwners(std::move(claimOwners));
}

void Map::replaceTile(size_t index, const Tile &tile)
{
	const Tile old = getTile(index);
    auto claimOwners=(old.farmArea&~tile.farmArea) ? farmMaterialClaimOwners(index,old.farmArea&~tile.farmArea) : std::vector<ResourceClaims::Owner>{};
	// Sprite changes do not replace a deposit's independently stored stocks.
	// Explicit replaceResource retains its reset-to-definition semantics.
	if (old.resource.type!=tile.resource.type || old.resource.amount!=tile.resource.amount)
		replaceResource(index,tile.resource);
	// Resource replacement above already accounts for its own membership delta.
	const auto targetBefore=routing().gradientRuntime->adaptive.fields.empty() ? std::optional<GradientTargetCell>{}
		: std::optional{routing().captureGradientTargetCell(index)};
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
	paintedAreaMaskChanged(ForbiddenArea,old.forbidden,tile.forbidden);
	paintedAreaMaskChanged(GuardArea,old.guardArea,tile.guardArea);
	paintedAreaMaskChanged(ClearingArea,old.clearArea,tile.clearArea);
	paintedAreaMaskChanged(FarmArea,old.farmArea,tile.farmArea);
	areaCells[index] = {tile.forbidden, tile.guardArea, tile.clearArea, tile.farmArea};
	scriptAreaCells[index] = tile.scriptAreas;
	markTerrain(index); markResource(index); markOccupancy(index); markArea(index);
	if (changes) resourceSeedChanged(index, changes);
	if(targetBefore) routing().gradientTargetCellChanged(index,*targetBefore);
    revalidateMaterialClaimOwners(std::move(claimOwners));
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
    auto claimOwners=field==&Tile::farmArea && (stored&~value) ? farmMaterialClaimOwners(index,stored&~value) : std::vector<ResourceClaims::Owner>{};
	const auto targetBefore=routing().gradientRuntime->adaptive.fields.empty() ? std::optional<GradientTargetCell>{}
		: std::optional{routing().captureGradientTargetCell(index)};
	const auto kind=field==&Tile::forbidden ? ForbiddenArea : field==&Tile::guardArea ? GuardArea
		: field==&Tile::clearArea ? ClearingArea : FarmArea;
	paintedAreaMaskChanged(kind,stored,value);
	stored = value;
	markArea(index);
	if (field == &Tile::forbidden) resourceSeedChanged(index, ResourceSeedCache::Forbidden);
	if(targetBefore) routing().gradientTargetCellChanged(index,*targetBefore);
    revalidateMaterialClaimOwners(std::move(claimOwners));
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
				const auto targetBefore=routing().gradientRuntime->adaptive.fields.empty() ? std::optional<GradientTargetCell>{}
					: std::optional{routing().captureGradientTargetCell(index)};
				occupancyCells[index].building = building;
				markOccupancy(index);
				resourceSeedChanged(index, ResourceSeedCache::Building);
				if(targetBefore) routing().gradientTargetCellChanged(index,*targetBefore);
			}
		}
	// Preserve the previous unconditional generation bump, including empty edits.
	bumpTopologyGeneration();
}

// Recompute only after a navigation constraint changes, not per flyer or tick.
// This cache is derived entirely from saved map state and is never serialized.
bool Map::hasAirRoutingConstraints() const
{
    if (hasAirTerrainConstraints()) return true;
    auto& cached = routing().gradientRuntime->directFlight;
    if (!cached.valid) {
        cached.resourceBlockers = false;
        for (size_t i = 0; i < size; ++i) {
            cached.resourceBlockers |= resourceBlocksAir(i);
        }
        cached.valid = true;
    }
    return cached.resourceBlockers;
}
