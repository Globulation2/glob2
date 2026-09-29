// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include "Topology.h"
#include <string>
#include <vector>
class Map;
namespace MapGeneration
{
// How far colonies are from each other and from what a map promises them, by what it costs to get
// there rather than by steps alone. Walls.h's walkSpread counts plain walking steps to one target;
// here a cost model says what a step onto each kind of tile costs, so the same measure answers "how
// far to walk", "how much forest to chop through", or "how far once units can swim", and a map that is
// only fair under one of those (a forest map, a canal map) can prove it.

/// What stepping onto a tile costs, by what is on it; -1 means the step cannot be taken. Clearable
/// deposits are wheat, wood and algae (workers can clear them); eternal ones are stone and fruit.
/// Water is checked first, then buildings, then deposits.
struct StepCosts
{
	int open = 1;
	int clearable = -1;
	int eternal = -1;
	int water = -1;
	int building = -1;

	/// Units walking today: open ground only.
	static StepCosts walking() { return {1, -1, -1, -1, -1}; }
	/// Workers cutting their way through: a clearable deposit costs `clear` steps.
	static StepCosts chopping(int clear = 4) { return {1, clear, -1, -1, -1}; }
	/// Units that have learnt to swim: water costs `swim` steps.
	static StepCosts swimming(int swim = 1) { return {1, -1, -1, swim, -1}; }
};

/// The cost of stepping onto tile (x, y) of a finished map, -1 when it cannot be entered.
int stepCost(const Map &, int x, int y, const StepCosts &);

/// The cheapest cost from any source tile to every tile (0 at a source, -1 where nothing reaches),
/// eight-connected on the torus unless told otherwise. A source is reached whatever stands on it.
std::vector<int> costsFrom(const Map &, const Torus &, const std::vector<int> &sources,
						   const StepCosts &, GridNeighbors = GridNeighbors::Eight);

/// Every colony's cost to reach every other colony's units (row = from, column = to; 0 on the
/// diagonal, -1 when unreachable), measured from each colony's unit tiles.
struct ContactReport
{
	std::vector<std::vector<int>> cost;
	/// Each colony's cost to its nearest rival, -1 when none can be reached.
	std::vector<int> nearestRival() const;
	/// The largest minus the smallest nearest-rival cost; -1 when some colony reaches no rival.
	int spread() const;
};
ContactReport contactMatrix(const Map &, int teams, const StepCosts &);

/// Every colony's cost, from its units, to the nearest tile of `targets` (-1 when none is reached).
std::vector<int> costsToTarget(const Map &, int teams, const std::vector<unsigned char> &targets,
							   const StepCosts &);

/// The largest minus the smallest of a list of costs, -1 when any is -1 (unreachable), 0 when empty.
int costSpread(const std::vector<int> &costs);

/// A validator's check that every colony reaches `what` within `tolerance` of every other under a cost
/// model: empty when it does, otherwise which colony fell short and by how much.
std::string unevenCosts(const std::vector<int> &costs, int tolerance, const std::string &what);

/// One site per colony at the same cost from home: for each colony k (its row of `costs`, a costsFrom
/// field per colony), the tile `eligible` allows whose cost from k is nearest `target` and lower than
/// every other colony's cost to it (so it lies on k's side), within `tolerance` of the target; the lowest
/// index wins a tie. -1 for a colony with no such tile. Hidden groves in a forest at an equal chop from
/// every home (StepCosts::chopping), a prize equally deep in every colony's rock.
std::vector<int> equalCostSites(const std::vector<std::vector<int>> &costs,
								const std::vector<unsigned char> &eligible, int target,
								int tolerance);
} // namespace MapGeneration
