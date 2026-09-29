// SPDX-License-Identifier: GPL-3.0-or-later
#include "HierarchicalCrossings.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
namespace MapGeneration
{
namespace
{
constexpr long long kUnreachable = 1000000000;
// Fixed integer mixing, keyed by stable identity: candidate input order and standard-library
// shuffle implementations cannot change ties. This consumes no generator or simulation RNG.
std::uint32_t tieKey(std::uint32_t seed, int id)
{
	std::uint32_t x = seed ^ std::uint32_t(id);
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	return x ^ (x >> 16);
}
} // namespace
CrossingProposals transverseCrossings(const std::vector<RecursiveSegment> &segments,
									  const std::vector<double> &fractions, double halfSpan,
									  double endClearance)
{
	CrossingProposals out;
	if (segments.empty() || segments.size() > 128 || fractions.empty() || fractions.size() > 16 ||
		!std::isfinite(halfSpan) || halfSpan <= 0 || halfSpan > 1048576 ||
		!std::isfinite(endClearance) || endClearance < 0 || endClearance > 1048576)
	{
		out.failure = "Invalid transverse crossing span, clearance or sample count.";
		return out;
	}
	std::set<int> ids;
	int stride = 1;
	for (const auto &s : segments)
	{
		if (s.id < 0 || s.id > 1000000 || !ids.insert(s.id).second || s.level < 0 ||
			!std::isfinite(s.from.x) || !std::isfinite(s.from.y) || !std::isfinite(s.to.x) ||
			!std::isfinite(s.to.y) || std::abs(s.from.x) > 1048576 ||
			std::abs(s.from.y) > 1048576 || std::abs(s.to.x) > 1048576 ||
			std::abs(s.to.y) > 1048576 || (s.from.x == s.to.x && s.from.y == s.to.y))
		{
			out.failure = "Invalid transverse crossing source segment.";
			return out;
		}
		stride = std::max(stride, s.id + 1);
	}
	for (double fraction : fractions)
		if (!std::isfinite(fraction) || fraction <= 0 || fraction >= 1)
		{
			out.failure = "Crossing fractions must lie strictly inside each segment.";
			return out;
		}
	// Validate the whole input before returning proposals: invalid input is not a
	// usable partial design. Ordering is sample slot, then the caller's segment order.
	for (size_t slot = 0; slot < fractions.size(); ++slot)
		for (const auto &s : segments)
		{
			const double dx = s.to.x - s.from.x, dy = s.to.y - s.from.y;
			const double length = std::hypot(dx, dy), fraction = fractions[slot];
			if (length * std::min(fraction, 1 - fraction) < endClearance)
				continue;
			const double x = s.from.x + fraction * dx, y = s.from.y + fraction * dy;
			const double nx = -dy / length * halfSpan, ny = dx / length * halfSpan;
			out.candidates.push_back({s.id + int(slot) * stride,
									  0,
									  0,
									  s.level,
									  s.parentRegion,
									  int(std::ceil(2 * halfSpan)),
									  {x + nx, y + ny},
									  {x - nx, y - ny}});
		}
	return out;
}

CrossingGraph crossingEndpointGraph(const Torus &t, const std::vector<unsigned char> &passable,
									std::vector<CrossingCandidate> &candidates)
{
	CrossingGraph result;
	if (candidates.empty() || candidates.size() > 128 || passable.size() != size_t(t.size()))
	{
		result.failure = "Invalid endpoint graph mask or candidate count.";
		return result;
	}
	std::vector<int> endpoints;
	for (const auto &c : candidates)
		for (ShapePoint p : {c.from, c.to})
		{
			if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1048576 ||
				std::abs(p.y) > 1048576)
			{
				result.failure = "Invalid unwrapped crossing endpoint.";
				return result;
			}
			const int at = t.at(int(std::floor(p.x)), int(std::floor(p.y)));
			if (!passable[at])
			{
				result.failure = "A crossing endpoint is outside passable terrain.";
				return result;
			}
			endpoints.push_back(at);
		}
	result.regions = int(endpoints.size());
	for (size_t i = 0; i < candidates.size(); ++i)
	{
		candidates[i].fromRegion = int(2 * i);
		candidates[i].toRegion = int(2 * i + 1);
	}
	// These floods are the graph measurement, not telemetry work. Duplicate endpoint
	// tiles intentionally get zero-cost edges: multiple approaches can share an island.
	for (size_t a = 0; a < endpoints.size(); ++a)
	{
		const auto distances = stepsFrom(t, tileMask(t, {endpoints[a]}), passable);
		for (size_t b = a + 1; b < endpoints.size(); ++b)
			if (distances[endpoints[b]] >= 0)
				result.edges.push_back({int(a), int(b), distances[endpoints[b]]});
	}
	return result;
}

CrossingSelection selectCrossings(
	const Torus &t, int regions, const std::vector<RegionEdge> &edges,
	const std::vector<int> &required, const std::vector<CrossingCandidate> &candidates,
	int localPerParent, int majorBudget, int separation, std::uint32_t seed,
	const std::function<bool(const CrossingCandidate &, const CrossingCandidate &)> &compatible)
{
	CrossingSelection out;
	// Floyd-Warshall is deliberately bounded to a coarse region graph, not a tile graph.
	// The generator must coarsen a more detailed world before asking for this heuristic.
	if (regions < 1 || regions > 256 || candidates.size() > 2048 || localPerParent < 0 ||
		majorBudget < 0 || localPerParent > 2048 || majorBudget > 2048 || separation < 0)
	{
		out.failure = "Crossing graph or budget exceeds supported bounds.";
		return out;
	}
	std::vector<std::vector<long long>> distance(regions,
												 std::vector<long long>(regions, kUnreachable));
	for (int i = 0; i < regions; ++i)
		distance[i][i] = 0;
	const auto valid = [&](int i) { return i >= 0 && i < regions; };
	for (int node : required)
		if (!valid(node))
		{
			out.failure = "Invalid required crossing region.";
			return out;
		}
	for (const auto &e : edges)
	{
		if (!valid(e.from) || !valid(e.to) || e.length < 0 || e.length >= kUnreachable)
		{
			out.failure = "Invalid region edge.";
			return out;
		}
		distance[e.from][e.to] = distance[e.to][e.from] =
			std::min(distance[e.from][e.to], (long long)e.length);
	}
	std::set<int> ids, parents;
	for (const auto &c : candidates)
	{
		if (!valid(c.fromRegion) || !valid(c.toRegion) || c.length < 0 ||
			c.length >= kUnreachable || c.level < 0 || !std::isfinite(c.from.x) ||
			!std::isfinite(c.from.y) || !std::isfinite(c.to.x) || !std::isfinite(c.to.y) ||
			std::abs(c.from.x) > 1048576 || std::abs(c.from.y) > 1048576 ||
			std::abs(c.to.x) > 1048576 || std::abs(c.to.y) > 1048576 || !ids.insert(c.id).second)
		{
			out.failure = "Invalid crossing candidate or duplicate stable identity.";
			return out;
		}
		if (c.level > 0)
			parents.insert(c.parentRegion);
	}
	for (int k = 0; k < regions; ++k)
		for (int i = 0; i < regions; ++i)
			for (int j = 0; j < regions; ++j)
				distance[i][j] = std::min(distance[i][j], distance[i][k] + distance[k][j]);
	std::vector<bool> used(candidates.size(), false);
	std::map<int, int> localCounts;
	const auto connected = [&]()
	{
		for (int a : required)
			for (int b : required)
				if (distance[a][b] == kUnreachable)
					return false;
		return true;
	};
	// Quantize with floor so translating an unwrapped negative midpoint by a
	// whole map width preserves its tile. Widen the requested squared distance
	// before multiplying; an impossible large separation is still a valid budget.
	const auto separated = [&](const CrossingCandidate &c)
	{
		for (const auto &s : out.selected)
			if ((compatible && !compatible(c, s)) ||
				t.dist2(int(std::floor((c.from.x + c.to.x) / 2)),
						int(std::floor((c.from.y + c.to.y) / 2)),
						int(std::floor((s.from.x + s.to.x) / 2)),
						int(std::floor((s.from.y + s.to.y) / 2))) <
					int64_t(separation) * separation)
				return false;
		return true;
	};
	for (;;)
	{
		const bool mandatory = !connected();
		int best = -1;
		long long bestBenefit = -1;
		for (size_t k = 0; k < candidates.size(); ++k)
		{
			const auto &c = candidates[k];
			if (used[k] || !separated(c))
				continue;
			if (mandatory)
			{
				if (distance[c.fromRegion][c.toRegion] != kUnreachable)
					continue;
				// A connecting edge can enter an intermediate non-required region. Restrict it to
				// a component carrying a required node, avoiding unrelated islands consuming space.
				bool touches = false;
				for (int r : required)
					touches |= distance[r][c.fromRegion] < kUnreachable ||
							   distance[r][c.toRegion] < kUnreachable;
				if (!touches)
					continue;
			}
			else if (c.level == 0 ? out.major >= majorBudget
								  : localCounts[c.parentRegion] >= localPerParent)
				continue;
			long long benefit = 0;
			for (int i = 0; i < regions; ++i)
				for (int j = i + 1; j < regions; ++j)
				{
					const long long via =
						std::min(distance[i][c.fromRegion] + c.length + distance[c.toRegion][j],
								 distance[i][c.toRegion] + c.length + distance[c.fromRegion][j]);
					benefit += distance[i][j] - std::min(distance[i][j], via);
				}
			if (!mandatory && benefit == 0)
				continue; // no ornamental bridge to fill a counter
			if (best < 0 || benefit > bestBenefit ||
				(benefit == bestBenefit &&
				 std::make_pair(tieKey(seed, c.id), c.id) <
					 std::make_pair(tieKey(seed, candidates[best].id), candidates[best].id)))
			{
				best = int(k);
				bestBenefit = benefit;
			}
		}
		if (best < 0)
		{
			if (mandatory)
				out.failure = "Required regions cannot be connected with the legal, separated "
							  "crossing candidates.";
			break;
		}
		const auto &c = candidates[best];
		used[best] = true;
		out.selected.push_back(c);
		out.benefits.push_back(bestBenefit);
		if (mandatory)
			++out.mandatory;
		else if (c.level == 0)
			++out.major;
		else
		{
			++out.local;
			++localCounts[c.parentRegion];
		}
		// A single undirected new edge updates all-pairs shortest paths in O(V^2). Snapshot
		// endpoints first so this iteration cannot accidentally use a partially updated row.
		const auto old = distance;
		for (int i = 0; i < regions; ++i)
			for (int j = 0; j < regions; ++j)
				distance[i][j] =
					std::min({old[i][j], old[i][c.fromRegion] + c.length + old[c.toRegion][j],
							  old[i][c.toRegion] + c.length + old[c.fromRegion][j]});
	}
	out.majorShortfall = majorBudget - out.major;
	out.localShortfall = int(parents.size()) * localPerParent - out.local;
	return out;
}
} // namespace MapGeneration
