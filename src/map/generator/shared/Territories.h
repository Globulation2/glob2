// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include "Sketch.h"
#include "Topology.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <queue>
#include <utility>
#include <vector>
namespace MapGeneration
{
/// Ground shared out between claimants: each tile's territory (-1 for ground none claimed) and each
/// territory's size in tiles.
struct Territories
{
	std::vector<int> labels;
	std::vector<int> areas;
	int smallest() const
	{
		int least = areas.empty() ? 0 : areas[0];
		for (int a : areas)
			least = std::min(least, a);
		return least;
	}
	int largest() const
	{
		int most = 0;
		for (int a : areas)
			most = std::max(most, a);
		return most;
	}
};

/// Shares the `eligible` ground out into territories of equal area, one per list of seed tiles,
/// grown together across the torus's wrap.
///
/// Every territory first takes its seeds (a seed already taken by an earlier territory stays
/// there). Then, one tile at a time, the territory that is smallest so far - the lowest-numbered
/// one on a tie - takes the cheapest eligible tile beside its ground, where a tile costs its steps
/// from that territory's seeds times 1000 plus `cost(tile)`. Taking only tiles beside its own ground
/// over the four cardinal neighbours keeps every territory in one piece; the step cost keeps it
/// compact, and a noise field in `cost` makes the borders between territories wander. A territory
/// with nothing left beside it stops growing and the others carry on, so the areas come out equal
/// to the tile unless some territory is boxed in. Eligible ground none could reach stays -1.
///
/// Growth draws nothing at random: the result is a function of the ground, the seeds and the cost
/// alone, with ties broken by tile index, so a validator can grow the same territories again.
///
/// Unlike wedges round the centre, which are equal only on a round design, this shares out a
/// square map's corners and the ground across its wrap as fairly as the middle, for any number of
/// claimants.
///
/// With `worth` (one value per claimant), territories are shared out by value instead of area: the
/// claimant whose area times worth is least takes the next tile, so a claimant whose ground is worth
/// less per tile ends with proportionally more of it.
template <typename Cost>
Territories growTerritories(const Torus &t, const std::vector<unsigned char> &eligible,
							const std::vector<std::vector<int>> &seeds, Cost cost,
							const std::vector<double> *worth = nullptr)
{
	const int n = t.w * t.h, claimants = int(seeds.size());
	Territories result;
	result.labels.assign(n, -1);
	result.areas.assign(claimants, 0);
	using Entry = std::pair<std::int64_t, int>;
	using Frontier = std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>>;
	std::vector<Frontier> frontier(claimants);
	std::vector<std::vector<int>> steps(claimants);
	std::vector<std::vector<unsigned char>> queued(claimants);
	const auto offer = [&](int k, int tile)
	{
		for (const auto &s : kCardinalSteps)
		{
			const int next = t.at(tile % t.w + s[0], tile / t.w + s[1]);
			if (eligible[next] && result.labels[next] < 0 && !queued[k][next] && steps[k][next] >= 0)
			{
				queued[k][next] = 1;
				frontier[k].push({std::int64_t(steps[k][next]) * 1000 + cost(next), next});
			}
		}
	};
	for (int k = 0; k < claimants; ++k)
	{
		steps[k] = stepsFrom(t, tileMask(t, seeds[k]), eligible);
		queued[k].assign(n, 0);
		for (int tile : seeds[k])
			if (eligible[tile] && result.labels[tile] < 0)
			{
				result.labels[tile] = k;
				++result.areas[k];
			}
	}
	for (int k = 0; k < claimants; ++k)
		for (int tile : seeds[k])
			if (result.labels[tile] == k)
				offer(k, tile);
	std::vector<unsigned char> growing(claimants, 1);
	for (;;)
	{
		int k = -1;
		for (int c = 0; c < claimants; ++c)
		{
			const double value = result.areas[c] * (worth ? (*worth)[c] : 1.0);
			if (growing[c] && (k < 0 || value < result.areas[k] * (worth ? (*worth)[k] : 1.0)))
				k = c;
		}
		if (k < 0)
			break;
		int taken = -1;
		while (!frontier[k].empty() && taken < 0)
		{
			const int tile = frontier[k].top().second;
			frontier[k].pop();
			if (result.labels[tile] < 0)
				taken = tile;
		}
		if (taken < 0)
		{
			growing[k] = 0;
			continue;
		}
		result.labels[taken] = k;
		++result.areas[k];
		offer(k, taken);
	}
	return result;
}
/// Smooths territories' borders, which a race for the cheapest tile leaves ragged: for `passes`
/// passes, every labelled tile whose neighbourhood - the square `radius` tiles round it - is more than
/// `share` another territory's takes that label, all at once per pass. A radius of 1 trims single-tile
/// spurs; a few passes at 3 or 4 straighten a wandering border into a smooth curve. Tiles of `keep` (a
/// territory's frontage) never change. Areas move a little, so a caller that needs them equal measures
/// them again afterwards.
inline void smoothLabels(const Torus &t, std::vector<int> &labels, int passes,
						 const std::vector<unsigned char> &keep, int radius = 1, double share = 0.7)
{
	std::vector<int> counts;
	for (int pass = 0; pass < passes; ++pass)
	{
		std::vector<int> smoothed = labels;
		int top = 0;
		for (int label : labels)
			top = std::max(top, label + 1);
		counts.assign(top, 0);
		const int window = (2 * radius + 1) * (2 * radius + 1) - 1;
		for (int i = 0; i < t.size(); ++i)
		{
			if (labels[i] < 0 || keep[i])
				continue;
			std::vector<int> touched;
			for (int dy = -radius; dy <= radius; ++dy)
				for (int dx = -radius; dx <= radius; ++dx)
				{
					const int label = labels[t.at(i % t.w + dx, i / t.w + dy)];
					if ((dx || dy) && label >= 0 && label != labels[i] && counts[label]++ == 0)
						touched.push_back(label);
				}
			int best = -1;
			for (int label : touched)
			{
				if (counts[label] > share * window && (best < 0 || counts[label] > counts[best]))
					best = label;
			}
			if (best >= 0)
				smoothed[i] = best;
			for (int label : touched)
				counts[label] = 0;
		}
		labels.swap(smoothed);
	}
}

/// Opens a gap between neighbouring territories: every tile within `gap` / 2 steps (rounded up) of a
/// tile of another territory loses its label, so any two territories end at least `gap` tiles apart
/// and can be separate islands, or separate fields behind their own walls.
inline void separateTerritories(const Torus &t, std::vector<int> &labels, int gap)
{
	std::vector<unsigned char> border(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		if (labels[i] < 0)
			continue;
		for (int dy = -1; dy <= 1 && !border[i]; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int other = labels[t.at(i % t.w + dx, i / t.w + dy)];
				if (other >= 0 && other != labels[i])
				{
					border[i] = 1;
					break;
				}
			}
	}
	const std::vector<int> fromBorder = stepsFrom(t, border);
	const int half = (gap + 1) / 2;
	for (int i = 0; i < t.size(); ++i)
		if (labels[i] >= 0 && fromBorder[i] >= 0 && fromBorder[i] < half)
			labels[i] = -1;
}

/// Closes the gaps between labelled regions: every `eligible` tile with no label, within `reach` steps
/// of a labelled tile over eligible ground, takes the label of the region nearest it (4-connected
/// steps, the lowest tile index first on a tie). A design that kept its pieces apart with water can
/// fill that water in and part them with a wall on the border instead (labelBorders). Returns the
/// tiles filled.
inline std::vector<unsigned char> fillToNearest(const Torus &t, std::vector<int> &labels,
												const std::vector<unsigned char> &eligible, int reach)
{
	std::vector<unsigned char> filled(t.size(), 0);
	std::vector<int> steps(t.size(), -1), frontier, next;
	for (int i = 0; i < t.size(); ++i)
		if (labels[i] >= 0)
		{
			steps[i] = 0;
			frontier.push_back(i);
		}
	for (int step = 1; step <= reach && !frontier.empty(); ++step)
	{
		next.clear();
		for (int i : frontier)
		{
			const int x = i % t.w, y = i / t.w;
			for (int j : {t.at(x, y - 1), t.at(x - 1, y), t.at(x + 1, y), t.at(x, y + 1)})
				if (steps[j] < 0 && eligible[j] && labels[j] < 0)
				{
					steps[j] = step;
					labels[j] = labels[i];
					filled[j] = 1;
					next.push_back(j);
				}
		}
		std::sort(next.begin(), next.end());
		frontier.swap(next);
	}
	return filled;
}

/// The ground `sources` cannot walk to: every tile of `ground` a flood from them over `ground` does not
/// reach. Land a lake or a wall has closed off, which a design usually fills rather than leaves as a
/// pocket nobody can use.
inline std::vector<unsigned char> strandedGround(const Torus &t, const std::vector<int> &sources,
												 const std::vector<unsigned char> &ground)
{
	const std::vector<int> reach = stepsFrom(t, tileMask(t, sources), ground);
	std::vector<unsigned char> stranded(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		stranded[i] = ground[i] && reach[i] < 0;
	return stranded;
}

/// A lake of exactly `target` tiles at the far end of a region: `depth` gives every tile of the
/// region its steps from the region's way in (-1 where not in it), and `room` its steps from anything
/// the lake must keep clear of; only tiles at least `gap` of room away may be water. The seed is deep
/// and roomy - the tile with the most depth plus twice its room, room counted only up to what a round
/// lake of the target needs - in a stretch of such tiles big enough for the whole lake, and the lake
/// grows from it by distance from the seed, pulled towards the far end by depth and roughened by
/// `noiseAt(tile)` (0 to 1). Every region given the same target gets a lake of the same size, which
/// is how a private lake stays fair when the regions are not the same shape. Adds the lake to
/// `water`; returns its size, 0 when no stretch of the region had room for it.
template <typename NoiseAt>
int growFarLake(const Torus &t, std::vector<unsigned char> &water, const std::vector<int> &depth,
				const std::vector<int> &room, int gap, int target, NoiseAt noiseAt,
				std::vector<int> &queued, int stamp)
{
	const int n = t.size();
	std::vector<unsigned char> roomy(n, 0);
	for (int i = 0; i < n; ++i)
		roomy[i] = depth[i] >= 0 && room[i] >= gap && !water[i];
	const std::vector<int> stretch = connectedRegions(roomy, t.w, t.h, true);
	std::vector<int> stretchSize;
	for (int label : stretch)
		if (label >= 0)
		{
			if (label >= int(stretchSize.size()))
				stretchSize.resize(label + 1, 0);
			++stretchSize[label];
		}
	const int enough = gap + int(std::ceil(std::sqrt(target / 3.14159265358979)));
	const auto score = [&](int i) { return depth[i] + 2 * std::min(room[i], enough); };
	int seed = -1;
	for (int i = 0; i < n; ++i)
		if (roomy[i] && stretchSize[stretch[i]] >= target && (seed < 0 || score(i) > score(seed)))
			seed = i;
	if (seed < 0)
		return 0;
	const int far = depth[seed];
	return growWater(
		t, water, seed, target, [&](int i) { return roomy[i] != 0; },
		[&](int i)
		{
			const double d = std::sqrt(double(t.dist2(seed % t.w, seed / t.w, i % t.w, i / t.w)));
			return std::int64_t(d * 1000) + (far - depth[i]) * 250LL + std::int64_t(noiseAt(i) * 2500);
		},
		queued, stamp);
}

/// A lake of exactly `target` tiles beside a site, on one side of it: `depth` gives the region as in
/// growFarLake (-1 outside), `room` every tile's steps from what the lake must keep clear of, and only
/// tiles with at least `gap` of room, and at least `siteGap` from the site, may be water. The seed lies
/// on the `side` (+1 left, -1 right) of the line through the site along `heading` (radians, from the
/// region's way in towards the site), and the whole lake keeps to that side of the line, so it never
/// comes between the site and the way in. The seed lies between `siteGap` and `reach` tiles from it: the roomiest such
/// tile, room counted only up to what a round lake of the target needs, then the nearest to half way
/// out, in a stretch of roomy tiles big enough for the whole lake. Two calls, one each side, give a
/// home a lake on either flank, the same size for everyone. Adds the lake to `water`; returns its size,
/// 0 when that side has no room.
template <typename NoiseAt>
int growLakeBeside(const Torus &t, std::vector<unsigned char> &water, const std::vector<int> &depth,
				   const std::vector<int> &room, int gap, int target, int site, double heading, int side,
				   int siteGap, int reach, NoiseAt noiseAt, std::vector<int> &queued, int stamp)
{
	const int n = t.size(), sx = site % t.w, sy = site / t.w;
	const auto away = [&](int i)
	{ return std::sqrt(double(t.dist2(sx, sy, i % t.w, i / t.w))); };
	const double hx = std::cos(heading), hy = std::sin(heading);
	// How far a tile lies to the left of the line through the site along the heading (right is
	// negative). The whole lake keeps to its side, clear of the line by half the site gap, so it never
	// comes between the site and the way in.
	const auto lateral = [&](int i)
	{
		const double dx = t.offsetX(sx, i % t.w), dy = t.offsetY(sy, i / t.w);
		return hx * dy - hy * dx;
	};
	std::vector<unsigned char> roomy(n, 0);
	for (int i = 0; i < n; ++i)
		roomy[i] = depth[i] >= 0 && room[i] >= gap && !water[i] && away(i) >= siteGap &&
				   lateral(i) * side >= siteGap;
	const std::vector<int> stretch = connectedRegions(roomy, t.w, t.h, true);
	std::vector<int> stretchSize;
	for (int label : stretch)
		if (label >= 0)
		{
			if (label >= int(stretchSize.size()))
				stretchSize.resize(label + 1, 0);
			++stretchSize[label];
		}
	const int enough = gap + int(std::ceil(std::sqrt(target / 3.14159265358979)));
	const double ideal = (siteGap + reach) / 2.0;
	int seed = -1;
	double seedScore = 0;
	for (int i = 0; i < n; ++i)
	{
		if (!roomy[i] || stretchSize[stretch[i]] < target || away(i) > reach)
			continue;
		const double score = 4 * std::min(room[i], enough) - std::abs(away(i) - ideal);
		if (seed < 0 || score > seedScore)
		{
			seed = i;
			seedScore = score;
		}
	}
	if (seed < 0)
		return 0;
	return growWater(
		t, water, seed, target, [&](int i) { return roomy[i] != 0; },
		[&](int i)
		{
			const double d = std::sqrt(double(t.dist2(seed % t.w, seed / t.w, i % t.w, i / t.w)));
			return std::int64_t(d * 1000) + std::int64_t(noiseAt(i) * 2500);
		},
		queued, stamp);
}

/// The site in a region the same number of steps from its way in as every other region's: among the
/// tiles whose `depth` is within `spread` of `target` (trying 0, then 1, up to `spread`), the one with
/// the most `room` (steps from anything a site must keep clear of), at least `minimumRoom`; ties go to
/// the first in row order. -1 when no tile qualifies. A swarm placed this way walks as far to the way
/// out as every other colony's, whatever shape its region is.
inline int siteAtDepth(const std::vector<int> &depth, const std::vector<int> &room, int target,
					   int minimumRoom, int spread)
{
	for (int within = 0; within <= spread; ++within)
	{
		int best = -1;
		for (size_t i = 0; i < depth.size(); ++i)
			if (depth[i] >= 0 && std::abs(depth[i] - target) <= within && room[i] >= minimumRoom &&
				(best < 0 || room[i] > room[best]))
				best = int(i);
		if (best >= 0)
			return best;
	}
	return -1;
}
} // namespace MapGeneration
