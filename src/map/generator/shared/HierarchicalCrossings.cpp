#include "GenerationWork.h"
#include "GenerationNumeric.h"
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
		::MapGeneration::generationCheckpoint();
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
	{
		::MapGeneration::generationCheckpoint();
		if (!std::isfinite(fraction) || fraction <= 0 || fraction >= 1)
		{
			out.failure = "Crossing fractions must lie strictly inside each segment.";
			return out;
		}
	}
	// Validate the whole input before returning proposals: invalid input is not a
	// usable partial design. Ordering is sample slot, then the caller's segment order.
	for (size_t slot = 0; slot < fractions.size(); ++slot)
	{
		::MapGeneration::generationCheckpoint();
		for (const auto &s : segments)
		{
			::MapGeneration::generationCheckpoint();
			const double dx = s.to.x - s.from.x, dy = s.to.y - s.from.y;
			const double length = ::MapGeneration::Numeric::hypot(dx, dy),
						 fraction = fractions.at(slot);
			if (length * std::min(fraction, 1 - fraction) < endClearance)
				continue;
			const double x = s.from.x + fraction * dx, y = s.from.y + fraction * dy;
			const double nx = -dy / length * halfSpan, ny = dx / length * halfSpan;
			out.candidates.push_back({s.id + int(slot) * stride,
									  0,
									  0,
									  s.level,
									  s.parentRegion,
									  int(::MapGeneration::Numeric::ceil(2 * halfSpan)),
									  {x + nx, y + ny},
									  {x - nx, y - ny}});
		}
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
	{
		::MapGeneration::generationCheckpoint();
		for (ShapePoint p : {c.from, c.to})
		{
			::MapGeneration::generationCheckpoint();
			if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1048576 ||
				std::abs(p.y) > 1048576)
			{
				result.failure = "Invalid unwrapped crossing endpoint.";
				return result;
			}
			const int at = t.at(int(::MapGeneration::Numeric::floor(p.x)),
								int(::MapGeneration::Numeric::floor(p.y)));
			if (!passable.at(at))
			{
				result.failure = "A crossing endpoint is outside passable terrain.";
				return result;
			}
			endpoints.push_back(at);
		}
	}
	result.regions = int(endpoints.size());
	for (size_t i = 0; i < candidates.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		candidates.at(i).fromRegion = int(2 * i);
		candidates.at(i).toRegion = int(2 * i + 1);
	}
	// These floods are the graph measurement, not telemetry work. Duplicate endpoint
	// tiles intentionally get zero-cost edges: multiple approaches can share an island.
	for (size_t a = 0; a < endpoints.size(); ++a)
	{
		::MapGeneration::generationCheckpoint();
		const auto distances = stepsFrom(t, tileMask(t, {endpoints.at(a)}), passable);
		for (size_t b = a + 1; b < endpoints.size(); ++b)
		{
			::MapGeneration::generationCheckpoint();
			if (distances.at(endpoints.at(b)) >= 0)
				result.edges.push_back({int(a), int(b), distances.at(endpoints.at(b))});
		}
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
	{
		::MapGeneration::generationCheckpoint();
		distance.at(i).at(i) = 0;
	}
	const auto valid = [&](int i) { return i >= 0 && i < regions; };
	for (int node : required)
	{
		::MapGeneration::generationCheckpoint();
		if (!valid(node))
		{
			out.failure = "Invalid required crossing region.";
			return out;
		}
	}
	for (const auto &e : edges)
	{
		::MapGeneration::generationCheckpoint();
		if (!valid(e.from) || !valid(e.to) || e.length < 0 || e.length >= kUnreachable)
		{
			out.failure = "Invalid region edge.";
			return out;
		}
		distance.at(e.from).at(e.to) = distance.at(e.to).at(e.from) =
			std::min(distance.at(e.from).at(e.to), (long long)e.length);
	}
	std::set<int> ids, parents;
	for (const auto &c : candidates)
	{
		::MapGeneration::generationCheckpoint();
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
	{
		::MapGeneration::generationCheckpoint();
		for (int i = 0; i < regions; ++i)
		{
			::MapGeneration::generationCheckpoint();
			for (int j = 0; j < regions; ++j)
			{
				::MapGeneration::generationCheckpoint();
				distance.at(i).at(j) =
					std::min(distance.at(i).at(j), distance.at(i).at(k) + distance.at(k).at(j));
			}
		}
	}
	std::vector<bool> used(candidates.size(), false);
	std::map<int, int> localCounts;
	const auto connected = [&]()
	{
		for (int a : required)
		{
			::MapGeneration::generationCheckpoint();
			for (int b : required)
			{
				::MapGeneration::generationCheckpoint();
				if (distance.at(a).at(b) == kUnreachable)
					return false;
			}
		}
		return true;
	};
	// Quantize with floor so translating an unwrapped negative midpoint by a
	// whole map width preserves its tile. Widen the requested squared distance
	// before multiplying; an impossible large separation is still a valid budget.
	const auto separated = [&](const CrossingCandidate &c)
	{
		for (const auto &s : out.selected)
		{
			::MapGeneration::generationCheckpoint();
			if ((compatible && !compatible(c, s)) ||
				t.dist2(int(::MapGeneration::Numeric::floor((c.from.x + c.to.x) / 2)),
						int(::MapGeneration::Numeric::floor((c.from.y + c.to.y) / 2)),
						int(::MapGeneration::Numeric::floor((s.from.x + s.to.x) / 2)),
						int(::MapGeneration::Numeric::floor((s.from.y + s.to.y) / 2))) <
					int64_t(separation) * separation)
				return false;
		}
		return true;
	};
	for (;;)
	{
		::MapGeneration::generationCheckpoint();
		const bool mandatory = !connected();
		int best = -1;
		long long bestBenefit = -1;
		for (size_t k = 0; k < candidates.size(); ++k)
		{
			::MapGeneration::generationCheckpoint();
			const auto &c = candidates.at(k);
			if (used.at(k) || !separated(c))
				continue;
			if (mandatory)
			{
				if (distance.at(c.fromRegion).at(c.toRegion) != kUnreachable)
					continue;
				// A connecting edge can enter an intermediate non-required region. Restrict it to
				// a component carrying a required node, avoiding unrelated islands consuming space.
				bool touches = false;
				for (int r : required)
				{
					::MapGeneration::generationCheckpoint();
					touches |= distance.at(r).at(c.fromRegion) < kUnreachable ||
							   distance.at(r).at(c.toRegion) < kUnreachable;
				}
				if (!touches)
					continue;
			}
			else if (c.level == 0 ? out.major >= majorBudget
								  : localCounts[c.parentRegion] >= localPerParent)
				continue;
			long long benefit = 0;
			for (int i = 0; i < regions; ++i)
			{
				::MapGeneration::generationCheckpoint();
				for (int j = i + 1; j < regions; ++j)
				{
					::MapGeneration::generationCheckpoint();
					const long long via = std::min(
						distance.at(i).at(c.fromRegion) + c.length + distance.at(c.toRegion).at(j),
						distance.at(i).at(c.toRegion) + c.length + distance.at(c.fromRegion).at(j));
					benefit += distance.at(i).at(j) - std::min(distance.at(i).at(j), via);
				}
			}
			if (!mandatory && benefit == 0)
				continue; // no ornamental bridge to fill a counter
			if (best < 0 || benefit > bestBenefit ||
				(benefit == bestBenefit &&
				 std::make_pair(tieKey(seed, c.id), c.id) <
					 std::make_pair(tieKey(seed, candidates.at(best).id), candidates.at(best).id)))
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
		const auto &c = candidates.at(best);
		used.at(best) = true;
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
		{
			::MapGeneration::generationCheckpoint();
			for (int j = 0; j < regions; ++j)
			{
				::MapGeneration::generationCheckpoint();
				distance.at(i).at(j) =
					std::min({old.at(i).at(j),
							  old.at(i).at(c.fromRegion) + c.length + old.at(c.toRegion).at(j),
							  old.at(i).at(c.toRegion) + c.length + old.at(c.fromRegion).at(j)});
			}
		}
	}
	out.majorShortfall = majorBudget - out.major;
	out.localShortfall = int(parents.size()) * localPerParent - out.local;
	return out;
}
} // namespace MapGeneration
