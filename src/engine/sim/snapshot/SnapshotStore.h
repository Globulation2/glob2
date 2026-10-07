// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorldSnapshot.h"
#include "SnapshotStorage.h"
#include <chrono>
#include <cstdlib>
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
	// GLOB2_SNAPSHOT_VERIFY=1 turns capture verification on for every store.
	Store() { if (const char* value = std::getenv("GLOB2_SNAPSHOT_VERIFY")) storage.verify = *value && *value != '0'; }
	void reset() { const bool verify = storage.verify; latest.reset(); catalog.reset(); catalogConfigurationRevision = 0; storage = {}; storage.verify = verify; metrics = {}; memoryPeaks = {}; }
	void setVerification(bool on) { storage.verify = on; }
	Handle captureBoundary(const Game& game, Requirements required);
	MemoryMetrics memoryMetrics() const;
};
} // namespace SimulationSnapshot
