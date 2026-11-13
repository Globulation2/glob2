// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Building-independent part of every building walking field's seed, kept
// current cell by cell so a cold field starts from a copy instead of a full
// map scan (SeedCells.h buildingCells). Derived data only: never saved, and
// discarding it changes no simulation result, only who pays for seeding.

#include "BuildingSeedPlanes.h"
#include "MapChangeTracking.h"
#include "MapStateView.h"

#include <cstddef>
#include <cstdint>

//! Owner-thread maintenance of the live planes, held by GradientRuntime and
//! reached through MapRouting. Map edits notify one cell at a time through
//! Map::resourceSeedChanged; bulk edits invalidate, and the next ensure()
//! rebuilds from the live map (the raw writes of a bulk edit may still be in
//! progress when it invalidates).
//!
//! Every template change accompanies a write that also marks one of the map's
//! own tracked arrays, so a snapshot boundary the Store reuses because those
//! are unchanged also has unchanged templates.
class BuildingSeedTemplates
{
public:
	//! One cell changed. teamCells: its building or forbidden paint may have
	//! changed, so the per-team bitsets need updating too.
	void changed(const MapState::View& view, std::size_t index, bool teamCells);
	void invalidate() { stale = true; }
	void reset();
	//! Rebuild if stale or resized; the returned planes match the view.
	const MapState::BuildingSeedPlanes& ensure(const MapState::View& view);
	//! Bring a captured copy up to date with the ensured live planes,
	//! copying only chunks changed since that copy was filled.
	void capture(MapState::BuildingSeedSnapshot& out, std::uint64_t world, std::uint64_t& bytesCopied) const;
	//! True when out already equals the live planes of this world. A stale set
	//! is never current, and asking does not rebuild it.
	bool current(const MapState::BuildingSeedSnapshot& out, std::uint64_t world) const
	{
		return !stale && out.world == world && out.epoch == epoch && out.generation == changes.generation
			&& out.planes.cells == data.cells;
	}
	//! Full rebuilds so far; per-cell edits never add one.
	std::uint64_t rebuilds() const { return epoch; }
	//! Tests: request templates at every snapshot boundary, not only when
	//! building field requests are queued (MapRouting::buildingSeedDemand).
	void setCaptureEveryBoundary(bool enabled) { everyBoundary = enabled; }
	bool captureEveryBoundary() const { return everyBoundary; }

private:
	MapState::BuildingSeedPlanes data;
	MapState::ChunkGeometry geometry;
	MapState::ChangeTracker changes;
	bool stale = true;
	bool everyBoundary = false;
	//! Counts rebuilds; a captured copy from another epoch is copied whole.
	std::uint64_t epoch = 0;
	bool refresh(const MapState::View& view, std::size_t index, bool teamCells);
};
