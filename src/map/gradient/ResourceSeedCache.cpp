// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceSeedCache.h"
#include "Map.h"
#include "MapInternal.h"
#include "GlobalContainer.h"
#include "Building.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstring>
#include <new>
#include <vector>

namespace
{
using Bits = std::vector<std::uint64_t>;

void setBit(Bits &bits, std::size_t index, bool value)
{
	const auto bit = std::uint64_t(1) << (index % 64);
	auto &word = bits[index / 64];
	word = (word & ~bit) | (value ? bit : 0);
}

template<class Function> void visit(const Bits &bits, Function function)
{
	for (std::size_t word = 0; word < bits.size(); ++word)
		for (auto remaining = bits[word]; remaining; remaining &= remaining - 1)
			function(word * 64 + std::countr_zero(remaining));
}
}

struct ResourceSeedCache::Storage
{
	std::array<std::vector<Uint16>, 2> base;
	std::array<Bits, MAX_RESOURCES> resources;
	Bits buildings;
	std::array<Bits, Team::MAX_COUNT> forbidden;
	// Effective natural goal type: occupied resource cells have no goal bit.
	std::vector<Uint8> resourceTypes, dirty;
	std::vector<Uint32> forbiddenMasks, queue;

	Storage(std::size_t cells, std::size_t dirtyLimit)
		: resourceTypes(cells, NO_RES_TYPE), dirty(cells), forbiddenMasks(cells)
	{
		for (auto &field : base) field.resize(cells);
		for (auto &bits : resources) bits.resize((cells + 63) / 64);
		buildings.resize((cells + 63) / 64);
		for (auto &bits : forbidden) bits.resize((cells + 63) / 64);
		queue.reserve(dirtyLimit);
	}

	std::size_t bytes() const
	{
		std::size_t bytes = sizeof(*this) + resourceTypes.capacity() + dirty.capacity();
		bytes += (queue.capacity() + forbiddenMasks.capacity()) * sizeof(Uint32);
		for (const auto &field : base) bytes += field.capacity() * sizeof(Uint16);
		for (const auto &bits : resources) bytes += bits.capacity() * sizeof(Uint64);
		for (const auto &bits : forbidden) bytes += bits.capacity() * sizeof(Uint64);
		return bytes + buildings.capacity() * sizeof(Uint64);
	}
};

ResourceSeedCache::ResourceSeedCache() = default;
ResourceSeedCache::~ResourceSeedCache() = default;

std::size_t ResourceSeedCache::allocatedBytes() const
{
	return sizeof(*this) + (storage ? storage->bytes() : 0);
}

void ResourceSeedCache::invalidateLocked()
{
	valid = false;
	quietFields = changes = 0;
	if (storage) storage->queue.clear();
}

void ResourceSeedCache::invalidate()
{
	std::lock_guard<std::mutex> lock(mutex);
	invalidateLocked();
}

void ResourceSeedCache::reset()
{
	std::lock_guard<std::mutex> lock(mutex);
	invalidateLocked();
	storage.reset();
	cells = dirtyLimit = 0;
	allocationFailed = false;
}

void ResourceSeedCache::changed(std::size_t index, unsigned flags)
{
	// Saturating counts are sufficient while direct preparation is selected.
	if (changes <= dirtyLimit) ++changes;
	if (!valid) return;
	assert(index < cells);
	auto &s = *storage;
	if (!s.dirty[index])
	{
		if (s.queue.size() == dirtyLimit)
		{
			valid = false;
			quietFields = 0;
			s.queue.clear();
			return;
		}
		s.queue.push_back(static_cast<Uint32>(index));
	}
	s.dirty[index] |= flags;
}

void ResourceSeedCache::refresh(const Map &map, std::size_t index, unsigned flags)
{
	auto &s = *storage;
	const auto &cell = map.tiles[index];
	if (flags & (Resource | Immobile))
	{
		const Uint8 old = s.resourceTypes[index];
		const Uint8 next = map.immobileUnits[index] == IMMOBILE_UNIT_NONE
			? cell.resource.type : NO_RES_TYPE;
		if (old != next)
		{
			if (old != NO_RES_TYPE) setBit(s.resources[old], index, false);
			if (next != NO_RES_TYPE) setBit(s.resources[next], index, true);
			s.resourceTypes[index] = next;
		}
	}
	if (flags & (Resource | Terrain | Building | Immobile))
	{
		const auto &terrain = map.terrainPropertiesAt(index);
		const bool open = cell.resource.type == NO_RES_TYPE && cell.building == NOGBID &&
			map.immobileUnits[index] == IMMOBILE_UNIT_NONE;
		s.base[0][index] = open && terrain.walkable ? GRADIENT_UNREACHABLE : GRADIENT_FORBIDDEN;
		s.base[1][index] = open && (terrain.walkable || terrain.swimmable)
			? GRADIENT_UNREACHABLE : GRADIENT_FORBIDDEN;
	}
	if (flags & Building) setBit(s.buildings, index, cell.building != NOGBID);
	if (flags & Forbidden)
	{
		for (auto changed = s.forbiddenMasks[index] ^ cell.forbidden; changed; changed &= changed - 1)
		{
			const unsigned team = std::countr_zero(changed);
			if (team < Team::MAX_COUNT)
				setBit(s.forbidden[team], index, cell.forbidden & Team::teamNumberToMask(team));
		}
		s.forbiddenMasks[index] = cell.forbidden;
	}
}

bool ResourceSeedCache::trySeed(const Map &map, int team, int resource, int swim,
	Uint16 *output, const Uint16 *supplierSeeds)
{
	// Avoid allocation and locking altogether on small maps and over budget.
	if (map.size <= MinimumCells || map.size > MaximumBytes / MaximumBytesPerCell)
		return false;
	std::lock_guard<std::mutex> lock(mutex);
	assert(!cells || cells == map.size); // Map::clear resets storage before resizing.
	cells = map.size;
	dirtyLimit = cells / DirtyDivisor;
	if (!valid && !allocationFailed)
	{
		quietFields = changes > dirtyLimit ? 0 : quietFields + 1;
		if (quietFields >= RebuildQuietFields)
		{
			try
			{
				if (!storage) storage = std::make_unique<Storage>(cells, dirtyLimit);
				// Include actual vector capacities and fixed state in both limits.
				if (allocatedBytes() > MaximumBytes || allocatedBytes() > cells * MaximumBytesPerCell)
					throw std::bad_alloc();
				std::fill(storage->dirty.begin(), storage->dirty.end(), 0);
				storage->queue.clear();
				for (std::size_t i = 0; i < cells; ++i) refresh(map, i, All);
				valid = true;
			}
			catch (const std::bad_alloc &)
			{
				storage.reset();
				allocationFailed = true;
			}
		}
	}
	changes = 0;
	if (!valid) return false;
	auto &s = *storage;
	for (const auto index : s.queue)
	{
		refresh(map, index, s.dirty[index]);
		s.dirty[index] = 0;
	}
	s.queue.clear();
	std::memcpy(output, s.base[swim > 0].data(), cells * sizeof(*output));
	const Uint32 mask = Team::teamNumberToMask(team);
	const bool hideFogged = globalContainer->resourcesTypes.get(resource)->visibleToBeCollected;
	// Goals override terrain/buildings, but not immobile units or forbidden paint.
	// Fog, market stock and resource policy are live overlays, never cached.
	visit(s.resources[resource], [&](std::size_t index) {
		if (!hideFogged || (map.fogOfWar[index] & mask))
			output[index] = GRADIENT_AT_GOAL;
	});
	if (supplierSeeds)
	{
		const unsigned teamBuildingBase=unsigned(team)*Building::MAX_COUNT;
		visit(s.buildings, [&](std::size_t index) {
			const auto &cell = map.tiles[index];
			const unsigned localId=unsigned(cell.building)-teamBuildingBase;
			if (cell.resource.type == NO_RES_TYPE && map.immobileUnits[index] == IMMOBILE_UNIT_NONE &&
				localId<Building::MAX_COUNT)
				output[index] = supplierSeeds[localId];
		});
	}
	visit(s.forbidden[team], [&](std::size_t index) { output[index] = GRADIENT_FORBIDDEN; });
	return true;
}
