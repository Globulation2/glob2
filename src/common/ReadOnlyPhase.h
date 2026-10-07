// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ComputeExecutor.h"
#include <array>
#include <stdexcept>

// A simulation-owner assembled observation batch with one completion barrier.
// "Read-only" refers to authoritative world state, not controller-owned state:
// tasks may write private results and explicitly synchronized derived caches.
// Callers must audit those caches; this helper cannot enforce constness through
// existing engine APIs. Orders and world mutations must wait for run() to return.
//
// Groups borrow lvalue callbacks, avoiding allocations/one callback per AI. Keep
// callbacks and their captured inputs alive until run() returns (also on error).
// ComputeExecutor waits for all parallel tasks before rethrowing an exception;
// serial execution stops on error, with no outstanding task to outlive the phase.
class ReadOnlyPhase
{
	struct Group {
		std::size_t count;
		void *context;
		void (*invoke)(void *, std::size_t);
	};
	std::array<Group, 8> groups{};
	std::size_t groupCount = 0, jobs = 0;
public:
	template<class Callback> void add(std::size_t count, Callback &callback)
	{
		if (!count) return;
		if (groupCount == groups.size()) throw std::logic_error("Too many read-only phase groups");
		groups[groupCount++] = {count, &callback, [](void *context, std::size_t index) {
			(*static_cast<Callback *>(context))(index);
		}};
		jobs += count;
	}
	void run(ComputeExecutor &executor) const
	{
		executor.run(jobs, [&](std::size_t index) {
			for (std::size_t group = 0; group < groupCount; ++group) {
				if (index < groups[group].count) {
					groups[group].invoke(groups[group].context, index);
					return;
				}
				index -= groups[group].count;
			}
		});
	}
};
