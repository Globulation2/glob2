// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "field/GradientCosts.h"
#include "field/Grid.h"
#include <span>
#include <algorithm>
namespace gradient_kernel {
// Positive integer edge costs make this fixed point unique. Checking against the
// original seeds rejects transient/stale readback, including fabricated sources,
// without trusting the device's convergence flag. Partial results never publish.
template <class Value, class Costs>
bool exactGradientFixedPoint(std::span<const std::uint16_t> seeds, std::span<const Value> output,
							 field::Grid grid, int limit, Costs costs)
{
	if (limit < 0 || output.size() != seeds.size() || seeds.size() != grid.cells())
		return false;
	const int threshold = std::max(2, 65535 - std::min(limit, COST_LIMIT));
	for (std::size_t i = 0; i < seeds.size(); ++i)
	{
		if (output[i] > 65535 || (seeds[i] == 0 && output[i] != 0) || output[i] < seeds[i])
			return false;
		if (!seeds[i])
			continue;
		const auto ownCost = costs(i);
		if (!ownCost.cardinal || !ownCost.diagonal || ownCost.cardinal > 65535 ||
			ownCost.diagonal > 65535)
			return false;
		unsigned best = seeds[i];
		const int x = int(i % grid.width()), y = int(i / grid.width());
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (dx || dy)
				{
					const auto k =
						std::size_t(grid.wrapY(y + dy)) * grid.width() + grid.wrapX(x + dx);
					if (!seeds[k] || output[k] <= 1)
						continue;
					const auto step = costs(k);
					const auto edge = dx && dy ? step.diagonal : step.cardinal;
					if (!edge || edge > 65535)
						return false;
					const int candidate = int(output[k]) - int(edge);
					if (candidate >= threshold)
						best = std::max(best, unsigned(candidate));
				}
		if (output[i] != best)
			return false;
	}
	return true;
}
} // namespace gradient_kernel
