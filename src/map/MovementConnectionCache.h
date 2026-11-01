// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_stdinc.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace MapState
{
struct View;
using ConnectionMasks = std::shared_ptr<const std::vector<std::uint8_t>>;

// One terrain epoch. Outgoing masks retain legal escape edges from blocked
// sources; physical searches independently require traversable predecessors.
class TerrainConnectionCache
{
  public:
	ConnectionMasks outgoing(const View &, int movement) const;
	// The owner may clear only its unleased epoch, between map read phases.
	void clear();

  private:
	mutable std::mutex mutex;
	mutable std::array<ConnectionMasks, 3> masks;
};

// One physical-obstacle epoch, shared by live reads and immutable snapshots.
// Missing keys compile from the caller's view, never from a retained live map.
// Old snapshots keep their epoch and may populate new keys after map edits.
class MovementConnectionCache
{
  public:
	struct Stats
	{
		std::uint64_t hits = 0, builds = 0;
		// Mask payloads only; the separately capped key table is small.
		std::size_t retainedBytes = 0, entries = 0;
	};
	explicit MovementConnectionCache(std::size_t budget = 32 * 1024 * 1024) : budget(budget) {}
	ConnectionMasks incoming(const View &, int movement, Uint32 team,
							 bool interactionEndpoints) const;
	Stats stats() const;
	void clear();

  private:
	struct Entry
	{
		int profile;
		Uint32 team;
		bool interactions;
		ConnectionMasks masks;
		std::uint64_t used;
	};
	std::size_t budget;
	static constexpr std::size_t MaximumEntries = 128;
	mutable std::mutex mutex;
	mutable std::vector<Entry> entries;
	mutable Stats counters;
	mutable std::uint64_t clock = 0;
};

// Exact compilation also supports ad hoc views without cache ownership.
ConnectionMasks compileMovementConnections(const View &, int movement, Uint32 team,
										   bool interactionEndpoints);
ConnectionMasks terrainConnectionMasks(const View &, int movement);
} // namespace MapState
