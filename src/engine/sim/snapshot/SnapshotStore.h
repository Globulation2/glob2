// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorldSnapshot.h"
#include "SnapshotStorage.h"
#include <chrono>
#include <cstdlib>
#include <optional>

namespace SimulationSnapshot
{
// Simulation-owner capture cache. Every boundary provides the complete core;
// named consumer views retain only their component leases. The store keeps the
// newest capture. Memory metrics and session peaks are computed only on query.
class Store
{
	std::shared_ptr<const std::vector<BuildingKindView>> catalog;
	Uint64 catalogConfigurationRevision = 0;
	std::optional<Handle> latest;
	Storage storage;
	mutable MemoryMetrics memoryPeaks;
	Uint64 observationRevision = 0;
	bool detailedMetrics = false;
	void accountCapture(const Handle& next, const Handle& previous, Uint64 copiedMapBytes);
public:
	struct Metrics
	{
		Uint64 captures = 0, captureNs = 0, bytesCopied = 0, reusedComponents = 0, allocations = 0;
		Uint64 preparationNs = 0, hiringFundingBytesCopied = 0;
		// Exact map copies are always counted; bytesCopied/reusedComponents cover
		// only detailedCaptures when optional diagnostics accounting is enabled.
		Uint64 mapBytesCopied = 0, detailedCaptures = 0;
	} metrics;
	// GLOB2_SNAPSHOT_VERIFY=1 turns capture verification on for every store.
	Store() { const auto enabled=[](const char* name) { const auto* value=std::getenv(name); return value && *value && *value!='0'; }; storage.profiling = enabled("GLOB2_STATS_PROFILE") || enabled("GLOB2_SNAPSHOT_PROFILE"); if (const char* value = std::getenv("GLOB2_SNAPSHOT_VERIFY")) storage.verify = *value && *value != '0'; }
	void reset() { const bool verify = storage.verify, profiling = storage.profiling; latest.reset(); catalog.reset(); catalogConfigurationRevision = 0; observationRevision = 0; storage = {}; storage.verify = verify; storage.profiling = profiling; metrics = {}; memoryPeaks = {}; }
	//! Owner announces a new observation boundary without advancing game time
	//! (paused edits, configuration changes). Existing handles remain immutable;
	//! unchanged map components can still be shared by the next capture.
	void invalidateBoundary()
	{
		if (observationRevision == std::numeric_limits<Uint64>::max())
			throw std::overflow_error("snapshot observation revision overflow");
		++observationRevision;
	}
    //! Catalog installation/reconfiguration is an explicit owner mutation.
    //! Existing leases retain their definitions; only the next publication rebuilds.
    void invalidateCatalog() { invalidateBoundary(); catalog.reset(); }
	void setVerification(bool on) { storage.verify = on; }
	//! Detailed byte/reuse accounting is opt-in (also enabled by compute telemetry).
	void setDetailedMetrics(bool on) { detailedMetrics = on; }
	//! Always captures the simulation preset; extra data may be added at the same boundary.
	Handle captureBoundary(const Game& game, CaptureOptions options = {});
	MemoryMetrics memoryMetrics() const;
};
} // namespace SimulationSnapshot
