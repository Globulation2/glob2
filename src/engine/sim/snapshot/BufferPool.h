// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace SimulationSnapshot
{
// A bounded pool of reusable component buffers. The owner thread acquires; a
// buffer is free again once every consumer has dropped its shared_ptr. The
// limit covers the longest consumer horizon plus the store's latest capture:
// delayed map gradients retain up to sixteen ticks, building gradients and AI
// decisions up to eight, all leasing the same per-tick captures, so
// max(16, 8, 8) + 1 = 17.
// Presentation reserves five additional epochs: active input, pending input,
// and three published/reusable PresentationFrame slots. Unused capacity is never allocated.
//
// Synchronization: a consumer's final release is an atomic release-decrement of
// the use count. The owner reads that count (a relaxed atomic load) and then
// issues an acquire fence, which synchronizes with the consumer's decrement, so
// every read the consumer made of the buffer happens-before the owner's reuse.
// (ThreadSanitizer does not model fences, so it cannot confirm this handoff.)
template<class T, std::size_t MaximumBuffers = 22> class BufferPool
{
	std::vector<std::shared_ptr<T>> buffers;
	std::size_t cursor = 0;
public:
	static constexpr std::size_t Limit = MaximumBuffers;
	std::size_t size() const { return buffers.size(); }
	template<class Visit> void inspect(Visit&& visit) const
	{
		for (const auto& buffer : buffers) visit(*buffer, buffer.use_count() > 1);
	}
	std::shared_ptr<T> acquire(std::uint64_t& allocations)
	{
		// Resume after the last hand-out so a large pool is not rescanned from
		// its busiest end on every acquisition.
		for (std::size_t scanned = 0; scanned < buffers.size(); ++scanned)
		{
			if (cursor >= buffers.size()) cursor = 0;
			const auto& buffer = buffers[cursor++];
			if (buffer.use_count() == 1)
			{
				std::atomic_thread_fence(std::memory_order_acquire);
				return buffer;
			}
		}
		return allocate(allocations);
	}
	// Prefer the free buffer with the highest score (its fill tick): the one
	// whose contents are closest to the live arrays, so a stamped copy moves the
	// fewest chunks.
	template<class Score> std::shared_ptr<T> acquire(std::uint64_t& allocations, Score&& score)
	{
		const std::shared_ptr<T>* best = nullptr;
		for (const auto& buffer : buffers)
			if (buffer.use_count() == 1 && (!best || score(*buffer) > score(**best))) best = &buffer;
		if (best)
		{
			std::atomic_thread_fence(std::memory_order_acquire);
			return *best;
		}
		return allocate(allocations);
	}
private:
	std::shared_ptr<T> allocate(std::uint64_t& allocations)
	{
		if (buffers.size() == Limit) throw std::logic_error("snapshot storage exceeded its bounded consumer horizon");
		++allocations;
		buffers.push_back(std::make_shared<T>());
		return buffers.back();
	}
};
} // namespace SimulationSnapshot
