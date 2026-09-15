// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Contact.h"
#include "Grid.h"
#include "Sketch.h"
#include "Topology.h"
#include <climits>
#include <functional>
#include <queue>
#include <utility>
#include <vector>
class Map;
struct GenerationContext;
namespace MapGeneration
{
/// The cheapest walk on the torus by any step cost: from any source tile (at cost 0) to any
/// goal tile, where `stepCost(from, to, dx, dy)` is the cost of stepping from one tile onto its
/// neighbour, or -1 when that step cannot be taken. Returns the walk from the goal reached back
/// to its source, or nothing. Dijkstra, so ties go to the tile queued first.
template <typename StepCost>
std::vector<int> cheapestWalk(const Torus &t, GridNeighbors neighbours,
							  const std::vector<int> &sources,
							  const std::vector<unsigned char> &goal, StepCost stepCost)
{
	const int n = t.w * t.h;
	std::vector<int> cost(n, INT_MAX), from(n, -1);
	using Entry = std::pair<int, int>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
	for (int i : sources)
	{
		cost[i] = 0;
		heap.push({0, i});
	}
	int reached = -1;
	while (!heap.empty() && reached < 0)
	{
		const auto [c, i] = heap.top();
		heap.pop();
		if (c > cost[i])
			continue;
		if (goal[i])
		{
			reached = i;
			break;
		}
		const int x = i % t.w, y = i / t.w;
		const auto consider = [&](int dx, int dy)
		{
			const int m = t.at(x + dx, y + dy);
			const int step = stepCost(i, m, dx, dy);
			if (step < 0 || c + step >= cost[m])
				return;
			cost[m] = c + step;
			from[m] = i;
			heap.push({cost[m], m});
		};
		if (neighbours == GridNeighbors::Cardinal)
			for (const auto &step : kCardinalSteps)
				consider(step[0], step[1]);
		else
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					if (dx || dy)
						consider(dx, dy);
	}
	std::vector<int> route;
	for (int i = reached; i >= 0; i = from[i])
		route.push_back(i);
	return route;
}

/// The cheapest walk from any source tile to any goal tile, eight-connected on the torus:
/// stepping onto a `costly` tile costs one, any other open tile nothing, and `blocked` tiles
/// are impassable. Returns the tiles of the walk from the goal it reached back to its source, or
/// nothing when no goal can be reached. A 0-1 breadth-first search, so ties go to the walk found
/// first in neighbour order.
std::vector<int> cheapestRoute(const Torus &, const std::vector<int> &sources,
							   const std::vector<unsigned char> &goal,
							   const std::vector<unsigned char> &blocked,
							   const std::vector<unsigned char> &costly);

/// Reserve a permanent sand approach BEFORE placing resources/buildings. Finds a
/// cheapest path, widened by radius tiles (Chebyshev), between valid sources
/// and goal. No painted corner may be water or spoil a protected tile's terrain.
/// Returns the centreline, goal first; empty means no route and leaves sketch unchanged.
/// radius 0 is one pure-sand tile wide; radius 1 is three. Invalid masks/sources or
/// radius return empty. Source/goal cells that cannot fit the width are ignored.
/// Eight-neighbour routes use 10/14 cardinal/diagonal step lengths; cardinal-only
/// routes use unit lengths. Dilation keeps diagonal routes broad after rasterization.
/// Optional positive tileCosts steer the route along a terrain/noise field; each must
/// be at most INT_MAX / (tile count * maximum step length), bounding every simple path sum.
/// At radius 0 only, existingPassage may mark already walkable, crop-proof tiles
/// (for example mixed beach terrain). Such tiles need no conversion and are left
/// untouched. The caller must establish that semantic guarantee from the world;
/// this operation still excludes protectedTiles themselves. Other path tiles must
/// satisfy the ordinary corner protections and become pure sand. This allows an
/// existing beach bypass that cannot be repainted without spoiling adjacent stone.
/// This never creates a ford: callers must explicitly allow another operation for that.
std::vector<int> reserveSandRoute(TerrainSketch &, const Torus &, const std::vector<int> &sources,
								  const std::vector<unsigned char> &goal,
								  const std::vector<unsigned char> &protectedTiles, int radius = 0,
								  const std::vector<int> *tileCosts = nullptr,
								  GridNeighbors neighbours = GridNeighbors::Cardinal,
								  const std::vector<unsigned char> *existingPassage = nullptr);

/// Deposits may land anywhere, and a band of them could close a colony off from where it must
/// be able to walk. This keeps one way open: the cheapest walk from the sources to the goal
/// (deposits cost one, open ground nothing; water, buildings and `alsoBlocked` are impassable),
/// with only the deposits on it cleared. Almost always nothing is in the way and nothing is
/// cleared. False when no walk exists at all.
bool openRoad(Map &, const Torus &, const std::vector<int> &sources,
			  const std::vector<unsigned char> &goal,
			  const std::vector<unsigned char> *alsoBlocked = nullptr);
/// Every colony must be able to walk to colony 0 at the start, and the deposits are the last thing
/// laid: for each colony whose workers colony 0's cannot reach over walkable land (Grid.h), the
/// cheapest walk between them is opened with openRoad, clearing only the deposits on it, never
/// water, buildings or `alsoBlocked` (a designed wall). Almost always nothing is in the way.
/// Returns how many walks were opened, or -1 with `detail` set when a colony has no land route
/// at all, which a design that keeps its ground joined should never allow. Watershed's and
/// Braided river's last word on their deposits.
int connectColonies(Map &, int teams, const std::vector<unsigned char> *alsoBlocked,
					std::string &detail);
/// Clears every deposit within `radius` Chebyshev steps of the tiles of `route`, except tiles of
/// `keep` (a designed wall). A route one tile wide is a path a unit can follow but a column cannot,
/// and a single regrown crop closes it; radius 1 makes a three-wide lane. Returns how many deposits
/// it cleared.
int clearRoute(Map &, const Torus &, const std::vector<int> &route, int radius,
			   const std::vector<unsigned char> *keep = nullptr);

/// Every colony must be able to walk to colony 0 at the start. Where a map's growth, water or walls box
/// one in, the cheapest way from anything colony 0's doorstep reaches to that colony's doorstep (the
/// open ring round its swarm) is opened under `costs`, four-connected: deposits on it are cleared, and
/// water on it becomes a sand ford (one sand corner per water tile, clearing deposits on the four tiles
/// it spoils). The map may look odd there; it does not fail. Returns whether anything changed; the
/// terrain is rebuilt when a ford was laid. Everglades' backstop, and any map whose design can close.
///
/// With `radius` above 0 the deposits within that many steps of the route are cleared too
/// (clearRoute), so a pass cut through a range or a forest takes a column of units and does not
/// close on the first regrowth. Tiles of `keep` are never entered and never cleared: a designed wall
/// the route must go round, whatever `costs` says a deposit costs. The defaults are the original
/// behaviour, so existing maps are unchanged.
bool openColonyRoutes(Map &, const GenerationContext &, const Torus &, const StepCosts &costs,
					  int radius = 0, const std::vector<unsigned char> *keep = nullptr);
} // namespace MapGeneration
