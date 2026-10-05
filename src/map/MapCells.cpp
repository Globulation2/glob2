// SPDX-License-Identifier: GPL-3.0-or-later
#include "Map.h"
#include "gradient/GradientRuntime.h"

void Map::resourceSeedChanged(size_t index, unsigned flags)
{
	gradientRuntime->resourceSeeds.changed(index, flags);
}

void Map::invalidateResourceSeeds()
{
	gradientRuntime->resourceSeeds.invalidate();
}

void Map::replaceResource(size_t index, const Resource &resource)
{
	const bool changedType = tiles[index].resource.type != resource.type;
	tiles[index].resource = resource;
	if (changedType) resourceSeedChanged(index, ResourceSeedCache::Resource);
}

void Map::replaceTile(size_t index, const Tile &tile)
{
	const auto &old = tiles[index];
	unsigned changes = 0;
	if (old.resource.type != tile.resource.type) changes |= ResourceSeedCache::Resource;
	if (old.building != tile.building) changes |= ResourceSeedCache::Building;
	if (old.forbidden != tile.forbidden) changes |= ResourceSeedCache::Forbidden;
	tiles[index] = tile;
	if (changes) resourceSeedChanged(index, changes);
}

void Map::setAreaMask(size_t index, Uint32 Tile::*field, Uint32 value)
{
	if (tiles[index].*field == value) return;
	tiles[index].*field = value;
	if (field == &Tile::forbidden) resourceSeedChanged(index, ResourceSeedCache::Forbidden);
}

void Map::addForbidden(int x, int y, Uint32 team)
{
	const size_t index = coordToIndex(x, y);
	const Uint32 mask = Team::teamNumberToMask(team);
	if ((tiles[index].forbidden & mask) == mask) return;
	setAreaMask(index, &Tile::forbidden, tiles[index].forbidden | mask);
	bumpTopologyGeneration();
}

void Map::removeForbidden(int x, int y, Uint32 team)
{
	const size_t index = coordToIndex(x, y);
	const Uint32 mask = Team::teamNumberToMask(team);
	if (!(tiles[index].forbidden & mask)) return;
	setAreaMask(index, &Tile::forbidden, tiles[index].forbidden & ~mask);
	bumpTopologyGeneration();
}

void Map::setBuilding(int x, int y, int width, int height, Uint16 building)
{
	for (int yi = y; yi < y + height; ++yi)
		for (int xi = x; xi < x + width; ++xi)
		{
			const size_t index = coordToIndex(xi, yi);
			if (tiles[index].building != building)
			{
				tiles[index].building = building;
				resourceSeedChanged(index, ResourceSeedCache::Building);
			}
		}
	// Preserve the previous unconditional generation bump, including empty edits.
	bumpTopologyGeneration();
}
