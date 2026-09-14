// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include <cstdint>
#include <utility>
#include <vector>
class Map;
namespace MapGeneration
{
// Building room as something a design decides, not only something the start scorer finds afterwards.
// A swarm, an inn or a barracks needs a 4x4 footprint of pure grass with nothing on it
// (Map::isFreeForBuilding); StartQuality counts such footprints round each colony on the finished map.
// These count them on a design, so a map can make room scarce on purpose (chambers in rock, blocks in a
// town) and still give every colony exactly as much.

/// The size of the footprint building room is counted in: a swarm's.
constexpr int kBuildFootprint = 4;

/// Every tile a building can stand on today: pure grass, no deposit, no building, no unit.
std::vector<unsigned char> buildableTiles(const Map &);

/// The top-left tiles of every `size` x `size` footprint lying wholly on `buildable`, across the wrap.
std::vector<unsigned char> buildAnchors(const Torus &, const std::vector<unsigned char> &buildable,
										int size = kBuildFootprint);

/// How many `size` x `size` footprints lie wholly on tiles that are both `buildable` and in `region`.
int buildSites(const Torus &, const std::vector<unsigned char> &buildable,
			   const std::vector<unsigned char> &region, int size = kBuildFootprint);

/// Grows `region` until it holds `target` footprints of buildable ground: always taking the
/// four-connected frontier tile `eligible` allows with the lowest `key(tile)` (then the lowest index),
/// never more than `maximumTiles` added. A chamber dug out of rock until it fits exactly what the design
/// promised. Returns the footprints it holds when it stopped.
template <typename Key>
int growUntilSites(const Torus &t, std::vector<unsigned char> &region,
				   const std::vector<unsigned char> &buildable,
				   const std::vector<unsigned char> &eligible, int target, int maximumTiles,
				   Key key, int size = kBuildFootprint);
} // namespace MapGeneration

#include <queue>
template <typename Key>
int MapGeneration::growUntilSites(const Torus &t, std::vector<unsigned char> &region,
								  const std::vector<unsigned char> &buildable,
								  const std::vector<unsigned char> &eligible, int target,
								  int maximumTiles, Key key, int size)
{
	const auto usable = [&](int i) { return region[i] && buildable[i]; };
	// Whether the footprint anchored at (ax, ay) lies wholly on usable ground.
	const auto complete = [&](int ax, int ay)
	{
		for (int dy = 0; dy < size; ++dy)
			for (int dx = 0; dx < size; ++dx)
				if (!usable(t.at(ax + dx, ay + dy)))
					return false;
		return true;
	};
	int sites = buildSites(t, buildable, region, size);
	using Entry = std::pair<std::int64_t, int>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> frontier;
	std::vector<unsigned char> queued(region.size(), 0);
	const auto offer = [&](int i)
	{
		const int x = i % t.w, y = i / t.w;
		for (const auto &s : kCardinalSteps)
		{
			const int n = t.at(x + s[0], y + s[1]);
			if (!region[n] && !queued[n] && eligible[n])
			{
				queued[n] = 1;
				frontier.push({std::int64_t(key(n)), n});
			}
		}
	};
	for (int i = 0; i < t.size(); ++i)
		if (region[i])
			offer(i);
	for (int added = 0; sites < target && added < maximumTiles && !frontier.empty(); ++added)
	{
		const int i = frontier.top().second;
		frontier.pop();
		region[i] = 1;
		if (buildable[i])
		{
			const int x = i % t.w, y = i / t.w;
			for (int ay = y - size + 1; ay <= y; ++ay)
				for (int ax = x - size + 1; ax <= x; ++ax)
					sites += complete(ax, ay);
		}
		offer(i);
	}
	return sites;
}
