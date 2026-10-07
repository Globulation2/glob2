// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorldSnapshot.h"
#include "SnapshotStorage.h"
#include <chrono>
#include <optional>

namespace SimulationSnapshot
{
// Simulation-owner capture cache. Consumers receive projections; the store keeps
// only the newest generation, while component leases retain older inputs. Memory
// metrics and their session peaks are computed on query, never during capture.
class Store
{
	std::shared_ptr<const std::vector<BuildingKindView>> catalog;
	Uint64 catalogConfigurationRevision = 0;
	std::optional<Handle> latest;
	Storage storage;
	mutable MemoryMetrics memoryPeaks;
public:
	struct Metrics
	{
		Uint64 captures = 0, captureNs = 0, bytesCopied = 0, reusedComponents = 0, allocations = 0;
		Uint64 preparationNs = 0;
	} metrics;
	void reset() { latest.reset(); catalog.reset(); catalogConfigurationRevision = 0; storage = {}; metrics = {}; memoryPeaks = {}; }
	Handle captureBoundary(const Game& game, Requirements required);
	MemoryMetrics memoryMetrics() const;
};
} // namespace SimulationSnapshot
