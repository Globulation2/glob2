// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UniformTraversal.h"

namespace field
{
// Ordered heap traversal for costs and parent tie rules owned by the caller.
// Unlike bucket paths this permits zero-cost edges and arbitrary payloads;
// the queue comparator, stale-entry test and admission rules remain explicit.
// Entries are copied and popped before callbacks; an early stop retains pending
// entries. Discovery receives raw coordinates and offers the complete stencil,
// just as traverse() does. A skipped entry never invokes discovery.
template <class Queue, class Stencil, class Index, class Visitor, class Discover>
void traversePriority(Queue &queue, const Grid &grid, const Stencil &stencil, Index indexOf,
					  Visitor visitor, Discover discover)
{
	while (!queue.empty())
	{
		const auto current = queue.top();
		queue.pop();
		const auto action = visitor(current);
		if (action == Visit::Stop)
			break;
		if (action == Visit::Skip)
			continue;
		grid.neighbors(indexOf(current), stencil, [&](int x, int y) { discover(current, x, y); });
	}
}
} // namespace field
