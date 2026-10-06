// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorldSnapshot.h"
#include <array>
#include <atomic>
#include <memory_resource>
#include <mutex>

namespace SimulationSnapshot
{
namespace Detail
{
// The allocator owns this state through control-block deallocation, which can
// happen after both the lease object and the Store have been destroyed.
struct LeaseMemory : std::pmr::memory_resource
{
	std::atomic<Uint64> allocations{0}, retainedBytes{0};
	void* do_allocate(std::size_t bytes, std::size_t alignment) override
	{
		void* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
		allocations.fetch_add(1, std::memory_order_relaxed);
		retainedBytes.fetch_add(bytes, std::memory_order_relaxed);
		return result;
	}
	void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
	{
		std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
		retainedBytes.fetch_sub(bytes, std::memory_order_relaxed);
	}
	bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};
struct LeaseState
{
	std::mutex mutex;
	LeaseMemory upstream;
	std::pmr::synchronized_pool_resource resource{&upstream};
};
template<class T> struct LeaseAllocator
{
	using value_type = T;
	std::shared_ptr<LeaseState> state;
	explicit LeaseAllocator(std::shared_ptr<LeaseState> state) : state(std::move(state)) {}
	template<class U> LeaseAllocator(const LeaseAllocator<U>& other) noexcept : state(other.state) {}
	T* allocate(std::size_t count) { return static_cast<T*>(state->resource.allocate(count * sizeof(T), alignof(T))); }
	void deallocate(T* pointer, std::size_t count) noexcept { state->resource.deallocate(pointer, count * sizeof(T), alignof(T)); }
	template<class U> bool operator==(const LeaseAllocator<U>& other) const noexcept { return state == other.state; }
};
template<class T> struct LeaseOwner
{
	std::shared_ptr<LeaseState> state;
	std::shared_ptr<T> source;
	LeaseOwner(std::shared_ptr<LeaseState> state, std::shared_ptr<T> source) noexcept
		: state(std::move(state)), source(std::move(source)) {}
	~LeaseOwner()
	{
		// All consumer accesses precede this unlock. The next acquisition locks
		// the same mutex before observing the sole pool reference and reusing it.
		std::lock_guard<std::mutex> lock(state->mutex);
		source.reset();
	}
};
}
// Seventeen slots cover the largest snapshot consumer horizon: delayed map
// gradients retain up to sixteen ticks, and AI decisions retain up to eight.
// A separate alias control block gives every acquired buffer a release barrier;
// use_count alone is a lifetime check, not synchronization for prior readers.
template<class T> class BufferPool
{
	std::array<std::shared_ptr<T>, 17> buffers;
	std::shared_ptr<Detail::LeaseState> state;
public:
	template<class Visit> void inspect(Visit&& visit) const
	{
		if (!state) return;
		std::lock_guard<std::mutex> lock(state->mutex);
		for (const auto& buffer : buffers) if (buffer) visit(*buffer, buffer.use_count() > 1);
	}
	Uint64 leaseUpstreamAllocations() const { return state ? state->upstream.allocations.load(std::memory_order_relaxed) : 0; }
	Uint64 leaseRetainedBytes() const { return state ? state->upstream.retainedBytes.load(std::memory_order_relaxed) : 0; }
	std::shared_ptr<T> acquire(Uint64& allocations)
	{
		if (!state) state = std::make_shared<Detail::LeaseState>();
		std::lock_guard<std::mutex> lock(state->mutex);
		for (auto& buffer : buffers) if (!buffer || buffer.use_count() == 1) {
			if (!buffer) { buffer = std::make_shared<T>(); ++allocations; }
			auto lease = std::allocate_shared<Detail::LeaseOwner<T>>(
				Detail::LeaseAllocator<Detail::LeaseOwner<T>>{state}, state, buffer);
			return std::shared_ptr<T>(std::move(lease), buffer.get());
		}
		throw std::logic_error("snapshot storage exceeded its bounded consumer horizon");
	}
};
// Counts describe component/plane buffer epochs, not whole-world generations.
// Leases include the latest owner snapshot and references from pooled components.
// Bytes include pool objects and enumerated vector capacities, deduplicating shared
// catalog/terrain payloads. Registry/configuration heaps, strings, map nodes,
// allocator overhead and shared_ptr control blocks are outside this accounting.
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
	BufferPool<Resources> resources;
	BufferPool<Occupancy> occupancy;
	BufferPool<Areas> areas;
	BufferPool<Visibility> visibility;
	BufferPool<Entities> entities;
	BufferPool<Teams> teams;
	BufferPool<Rules> rules;
	BufferPool<ResourceFields> resourceFields;
	BufferPool<Fertility::GrowthCache> growth;
	std::map<ResourceFieldKey, BufferPool<std::vector<Uint16>>> resourcePlanes;
	Uint64 allocations = 0;
	Uint64 preparationNs = 0;
	MemoryMetrics memoryMetrics() const;
};

inline MemoryMetrics Storage::memoryMetrics() const
{
	MemoryMetrics result;
	const auto vectorBytes = []<class T>(const std::vector<T>& values) { return Uint64(values.capacity()) * sizeof(T); };
	struct SharedPayload { const void* identity = nullptr; Uint64 object = 0, capacity = 0; bool leased = false; };
	std::array<SharedPayload, 34> shared{};
	std::size_t sharedCount = 0;
	const auto remember = [&](const auto& owner, bool leased) {
		if (!owner) return;
		for (std::size_t i = 0; i < sharedCount; ++i) if (shared[i].identity == owner.get()) { shared[i].leased |= leased; return; }
		shared.at(sharedCount++) = {owner.get(), sizeof(*owner), vectorBytes(*owner), leased};
	};
	const auto account = [&]<class T>(const BufferPool<T>& pool, auto payload) {
		pool.inspect([&](const T& buffer, bool leased) {
			++result.allocatedBuffers;
			if (leased) ++result.leasedBuffers; else ++result.reusableBuffers;
			const Uint64 capacity = payload(buffer, leased);
			result.capacityBytes += capacity;
			result.retainedBytes += sizeof(T) + capacity;
			if (leased) result.leasedBytes += sizeof(T) + capacity;
		});
	};
	account(catalogs, [&](const Catalogs& value, bool leased) { remember(value.buildings, leased); return Uint64(0); });
	account(terrain, [&](const Terrain& value, bool leased) { remember(value.identity, leased); return vectorBytes(value.legacy); });
	const auto cells = [&](const auto& value, bool) { return vectorBytes(value.cells); };
	account(resources, cells); account(occupancy, cells); account(areas, cells); account(visibility, cells);
	account(entities, [&](const Entities& value, bool) {
		return vectorBytes(value.buildings) + vectorBytes(value.units) + vectorBytes(value.relationships) + vectorBytes(value.projects);
	});
	account(teams, [&](const Teams& value, bool) {
		Uint64 bytes = vectorBytes(value.values);
		for (const auto& team : value.values) bytes += vectorBytes(team.statistics.buildingCountByVariant)
			+ vectorBytes(team.virtualBuildings) + vectorBytes(team.swarms);
		return bytes;
	});
	account(rules, [&](const Rules& value, bool) { return vectorBytes(value.named) + vectorBytes(value.experiments); });
	account(resourceFields, [](const ResourceFields&, bool) { return Uint64(0); });
	account(growth, [](const Fertility::GrowthCache& value, bool) {
		const auto capacities = value.storageCapacities();
		return Uint64(capacities[0] + capacities[1]) * sizeof(Uint32) + Uint64(capacities[2] + capacities[3]) * sizeof(Uint16);
	});
	for (const auto& [key, pool] : resourcePlanes) account(pool, [&](const auto& value, bool) { return vectorBytes(value); });
	for (std::size_t i = 0; i < sharedCount; ++i) {
		result.capacityBytes += shared[i].capacity;
		result.retainedBytes += shared[i].object + shared[i].capacity;
		if (shared[i].leased) result.leasedBytes += shared[i].object + shared[i].capacity;
	}
	return result;
}
} // namespace SimulationSnapshot
