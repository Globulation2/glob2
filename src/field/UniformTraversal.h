// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include "Frontier.h"
#include <cstddef>
#include <type_traits>

namespace field
{
// The visitor runs before expansion: Skip suppresses neighbours, Stop ends the
// search immediately. Neither action undoes discovery or payload writes.
enum class Visit
{
	Expand,
	Skip,
	Stop
};

// FIFO over an append-only indexed sequence (usually a vector). Entries remain
// in the sequence after traversal, including entries beyond an early stop; this
// preserves discovery history for callers that consume it later. The caller owns
// clearing and seeding. Callbacks may append, but must not erase or reorder entries.
// Returning Stop from expansion ends the search after that callback returns.
template <class Queue, class Visitor, class Expand>
void breadthFirst(Queue &queue, Visitor visitor, Expand expand)
{
	for (std::size_t head = 0; head < queue.size(); ++head)
	{
		const auto current = queue[head];
		const auto action = visitor(current);
		if (action == Visit::Stop)
			break;
		if (action == Visit::Skip)
			continue;
		if (expand(current) == Visit::Stop)
			break;
	}
}

// Ring FIFO consumes the current entry before callbacks and retains only pending
// entries on an early stop. A completed search drains it. Unlike the indexed
// overload above, it does not retain discovery history. The caller owns seeding.
template <class Visitor, class Expand>
void breadthFirst(Frontier &queue, Visitor visitor, Expand expand)
{
	while (!queue.empty())
	{
		const int current = queue.front();
		queue.pop_front();
		const auto action = visitor(current);
		if (action == Visit::Stop)
			break;
		if (action == Visit::Expand && expand(current) == Visit::Stop)
			break;
	}
}

// LIFO over caller-owned stack storage. Copy and pop before callbacks, which may
// push new entries. Seed and push order determine visitation order; an early stop
// retains pending entries, as in the ring FIFO overload.
template <class Stack, class Visitor, class Expand>
void depthFirst(Stack &stack, Visitor visitor, Expand expand)
{
	while (!stack.empty())
	{
		const auto current = stack.back();
		stack.pop_back();
		const auto action = visitor(current);
		if (action == Visit::Stop)
			break;
		if (action == Visit::Skip)
			continue;
		if (expand(current) == Visit::Stop)
			break;
	}
}

// Caller-seeded FIFO; discovery owns admission, payloads and enqueueing. Entries
// are copied before callbacks, since discovery may reallocate queue storage.
// Coordinates passed to discover are intentionally unwrapped: a domain boundary
// predicate can reject them before normalizing. Seed and stencil order are exact.
// discover returns void: all neighbours of an expanded entry are offered before
// the next visitor can stop. Use breadthFirst directly to stop during expansion.
template <class Queue, class Stencil, class Index, class Visitor, class Discover>
void traverse(Queue &queue, const Grid &grid, const Stencil &stencil, Index indexOf,
			  Visitor visitor, Discover discover)
{
	breadthFirst(queue, visitor,
				 [&](const auto &current)
				 {
					 grid.neighbors(indexOf(current), stencil,
									[&](int x, int y) { discover(current, x, y); });
					 return Visit::Expand;
				 });
}

template <class Queue, class Stencil, class Visitor, class Discover>
void traverse(Queue &queue, const Grid &grid, const Stencil &stencil, Visitor visitor,
			  Discover discover)
{
	traverse(queue, grid, stencil, [](int index) { return index; }, visitor, discover);
}

// Builds directly in the caller's encoding (including compact proxy references).
// Only unvisited cells are written; obstacles and seeded values remain untouched.
// Seed the FIFO with valid flat indices in the required order. Seeds must have
// the same distance for shortest-distance results; mixed seed distances require
// a priority search. The encoding must represent every reached distance without
// colliding with unvisited. Admission runs only for still-unvisited neighbours.
// Grid wrapping and stencil order apply, including aliases on thin grids.
template <class Values, class Queue, class Stencil, class Value, class Admit>
void expandDistances(Values &values, Queue &queue, const Grid &grid, const Stencil &stencil,
					 Value unvisited, Admit admit)
{
	// Select addressing once per field, outside the hot frontier loop.
	const int width = grid.width(), maskX = grid.maskX();
	const int cellMask = int(grid.cells()) - 1;
	const auto run = [&](auto masked)
	{
		breadthFirst(
			queue, [](int) { return Visit::Expand; },
			[&](int current)
			{
				const auto nextValue = values[current] + 1;
				const auto discover = [&](int next)
				{
					if (values[next] == unvisited && admit(next))
					{
						values[next] = nextValue;
						queue.push_back(next);
					}
				};
				if constexpr (decltype(masked)::value)
				{
					const int x = current & maskX, row = current & ~maskX;
					for (const auto d : stencil)
						discover(((row + d.y * width) & cellMask) + ((x + d.x) & maskX));
				}
				else
					grid.neighborIndices(current, stencil, discover);
				return Visit::Expand;
			});
	};
	if (grid.powerOfTwo())
		run(std::true_type{});
	else
		run(std::false_type{});
}

template <class Values, class Queue, class Stencil, class Value>
void expandDistances(Values &values, Queue &queue, const Grid &grid, const Stencil &stencil,
					 Value unvisited)
{
	expandDistances(values, queue, grid, stencil, unvisited, [](int) { return true; });
}
} // namespace field
