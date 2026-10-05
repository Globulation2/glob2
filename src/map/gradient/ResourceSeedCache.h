// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

class Map;

// Derived preparation data only: neither propagated fields nor pending jobs live
// here. Discarding this cache must not change field age or publication deadlines.
class ResourceSeedCache
{
public:
	enum Change : unsigned
	{
		Resource = 1, Terrain = 2, Building = 4, Immobile = 8, Forbidden = 16,
		All = Resource | Terrain | Building | Immobile | Forbidden
	};
	ResourceSeedCache();
	~ResourceSeedCache();
	ResourceSeedCache(const ResourceSeedCache &) = delete;
	ResourceSeedCache &operator=(const ResourceSeedCache &) = delete;

	// Changes, invalidation and reset run between compute read phases, like the
	// map writes themselves. Notifications never allocate or lock; concurrent
	// readers serialize maintenance and copying in trySeed.
	void changed(std::size_t index, unsigned flags);
	void invalidate();
	void reset();
	// False requests the direct kernel; allocation failure is a normal fallback.
	// Optional per-building-ID seeds are compiled for this request (including
	// resource permissions, stock, pickup costs and consumer exclusions). They
	// are overlaid live and never retained by the terrain/resource template.
	bool trySeed(const Map &map, int team, int resource, int swim,
		std::uint16_t *output, const std::uint16_t *supplierSeeds);

private:
	struct Storage;
	std::unique_ptr<Storage> storage;
	std::mutex mutex;
	std::size_t cells = 0, dirtyLimit = 0, changes = 0, quietFields = 0;
	bool valid = false, allocationFailed = false;
	static constexpr std::size_t MinimumCells = 4096;
	static constexpr std::size_t MaximumBytes = 32 * 1024 * 1024;
	static constexpr std::size_t MaximumBytesPerCell = 16;
	static constexpr std::size_t DirtyDivisor = 64;
	static constexpr std::size_t RebuildQuietFields = 16;
	void invalidateLocked();
	void refresh(const Map &map, std::size_t index, unsigned flags);
	std::size_t allocatedBytes() const;
};
