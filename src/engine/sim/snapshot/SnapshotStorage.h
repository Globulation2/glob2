// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorldSnapshot.h"
#include "BufferPool.h"
#include <array>

namespace SimulationSnapshot
{
// Counts describe component/plane buffer epochs, not whole-world generations.
// Leases include the latest owner snapshot and references from pooled components.
// Bytes include pool objects and enumerated vector capacities, deduplicating shared
// catalog/terrain payloads. Registry/configuration heaps, strings, map nodes,
// allocator overhead and shared_ptr control blocks are outside payload accounting.
struct MemoryMetrics
{
	Uint64 allocatedBuffers = 0, reusableBuffers = 0, leasedBuffers = 0;
	Uint64 retainedBytes = 0, capacityBytes = 0, leasedBytes = 0;
	Uint64 peakAllocatedBuffers = 0, peakReusableBuffers = 0, peakLeasedBuffers = 0;
	Uint64 peakRetainedBytes = 0, peakCapacityBytes = 0, peakLeasedBytes = 0;
};
struct Storage
{
	BufferPool<Catalogs> catalogs;
	BufferPool<Terrain> terrain;
	BufferPool<Session> session;
	BufferPool<Effects> effects;
	BufferPool<Statistics> statistics;
	BufferPool<History> history;
	BufferPool<Telemetry> telemetry;
	BufferPool<EntityDiagnostics> entityDiagnostics;
	BufferPool<Annotations> annotations;
	BufferPool<Resources> resources;
	BufferPool<Occupancy> occupancy;
	BufferPool<Areas> areas;
	BufferPool<Visibility> visibility;
	BufferPool<Entities> entities;
	BufferPool<Teams> teams;
	BufferPool<Rules> rules;
	BufferPool<ResourceFields> resourceFields;
	BufferPool<Fertility::GrowthCache> growth;
	BufferPool<BuildingAreaEffects::FertilitySnapshot> areaFertility;
	// Every live plane of every retained capture may be distinct.
	BufferPool<std::vector<Uint16>, std::size_t(17) * MapState::PlaneCount> resourcePlanes;
	Uint64 allocations = 0;
	Uint64 preparationNs = 0;
	//! Bytes of map arrays actually copied (changed chunks and full fills).
	Uint64 bytesCopied = 0;
	//! After each capture, compare every captured map array and entity list
	//! with the live state and throw on a mismatch (an unmarked write).
	bool verify = false;
	MemoryMetrics memoryMetrics() const;
};

} // namespace SimulationSnapshot
