// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include <climits>
#include <string>
#include <vector>
class Map;
namespace MapGeneration
{
// Walls a generator designs and the checks that prove they hold. Stone can never be cleared, so a
// wall of it is permanent; but stone only stands on pure grass and grass may never touch water, so
// a wall is always a band of grass tiles given stone, never the water's edge itself. These are the
// pieces every walled map needs: which land a swimmer can stand on, a wall round a coast that keeps
// such a swimmer on the beach, a wall between labelled regions that no unit can cross even
// diagonally, and the walks and tower ranges that a validator checks the result against.

/// The land a unit landing from the sea can reach without crossing solid grass: every land tile
/// with a `sea` undermap vertex in the box its four corners touch, and every beach (land that is
/// not pure grass) joined to one. `sea` marks undermap vertices, so a generator decides which
/// water is sea and which (a home's lake) is not. `notBeach` marks sand that must not carry the
/// margin inland, such as a sand road.
std::vector<unsigned char> seaMargin(const Map &, const Torus &, const std::vector<unsigned char> &sea,
									 const std::vector<unsigned char> &notBeach);

/// The sea for seaMargin: every water vertex of the map except those of `lakes`, the inland water a
/// generator keeps apart from the sea.
std::vector<unsigned char> seaVertices(const Map &, const Torus &, const std::vector<unsigned char> &lakes);

/// Whether a sealed coast holds: the first tile of `inside` (land a swimmer must not get onto) that a
/// unit walking from the margin's walkable tiles reaches, or -1 when none is. Margin tiles themselves
/// never count.
int seaEntry(const Map &, const Torus &, const std::vector<unsigned char> &margin,
			 const std::vector<unsigned char> &inside);

/// The wall that seals a coast: stone on every pure-grass tile of `wallable` that touches the
/// margin, diagonals included. Every step off the beach then lands on stone, so a swimmer can land
/// and walk the sand lane outside it but never get in. Set these tiles to stone in index order.
std::vector<unsigned char> sealCoasts(const Map &, const Torus &,
									  const std::vector<unsigned char> &margin,
									  const std::vector<unsigned char> &wallable);

/// A wall between labelled regions: a tile is wall when any of its eight neighbours has a lower
/// label. A tile left off the wall then touches only its own label or wall, so no unit can step,
/// even diagonally, from one region into another, and the wall is one tile thick. Negative labels
/// (sea, or ground no region claimed) take no part: they never make a wall and are never walled.
std::vector<unsigned char> labelBorders(const Torus &, const std::vector<int> &labels);
/// The same wall `thickness` tiles thick (1 to 3): the second tile on the other region's side of the
/// border, the third back on the first's.
std::vector<unsigned char> labelBorders(const Torus &, const std::vector<int> &labels, int thickness);

/// labelBorders with doors: the border between a tile and its lower-labelled neighbour is walled
/// unless `open(tile, neighbour)` leaves it open, as where a home meets its own corridor. A tile is
/// wall when any such closed border touches it, so the wall still stands on the higher label's side.
template <typename Open>
std::vector<unsigned char> labelBorders(const Torus &t, const std::vector<int> &labels, Open open)
{
	std::vector<unsigned char> wall(t.size(), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x;
			if (labels[i] < 0)
				continue;
			for (int dy = -1; dy <= 1 && !wall[i]; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int j = t.at(x + dx, y + dy);
					if (labels[j] >= 0 && labels[j] < labels[i] && !open(i, j))
					{
						wall[i] = 1;
						break;
					}
				}
		}
	return wall;
}

/// A designed band of stone, once the terrain is laid: the tiles of `wall` that are pure grass and
/// so can take stone, and how many were not (the beach pass reached them, or the design put water or
/// sand there) - any such tile is a gap in the wall, which a design should refuse rather than ship.
struct DesignedStone
{
	std::vector<unsigned char> stone;
	int gaps = 0;
	int firstGap = -1;
};
DesignedStone designedStone(const Map &, const Torus &, const std::vector<unsigned char> &wall);

/// Steps from `from` over the land a ground unit can walk (walkableTiles: no water, deposit or
/// building) with the `shut` tiles closed as well: -1 wherever nothing was reached. The question
/// every sealing check asks - with the gates shut, can this ground reach that ground?
std::vector<int> reachesWithShut(const Map &, const Torus &, const std::vector<unsigned char> &from,
								 const std::vector<unsigned char> &shut);

/// The check that a map's parts stay apart with their doors shut: floods each part's walkable tiles
/// (a `piece` label of 0 or more) over land that is not stone, crops and buildings counted as the ground they
/// will leave, with the `shut` tiles closed, and returns the
/// first tile of a different part any flood reaches, or -1 when every part keeps to itself.
int pieceLeak(const Map &, const Torus &, const std::vector<int> &piece,
			  const std::vector<unsigned char> &shut);

/// The size of a defence tower's footprint, and the range of each of its three levels
/// (BuildingTypesDefence.cpp). A tower scans square rings round its footprint's top-left tile with
/// no line of sight (Building::findBestTarget, BuildingUtils::turretScanTile), reaching its range in
/// tiles beyond every side of its footprint, so a wall stops walking but not shooting.
constexpr int kTowerFootprint = 2;
constexpr int kTowerRange[3] = {5, 7, 9};

/// How close a tower built on `buildable` can come to shooting at `target`: the least Chebyshev
/// distance, across the wrap, from any tile of a 2x2 footprint wholly on `buildable` to any tile of
/// `target`. A tower of range r hits a target this far away or nearer. INT_MAX when no footprint fits
/// or there is no target.
int towerReach(const Torus &, const std::vector<unsigned char> &buildable,
			   const std::vector<unsigned char> &target);

/// Each colony's walk from its workers to a target tile of its own, over walkable land: the
/// check that every colony is as far from the place the map sends it as every other.
struct WalkSpread
{
	std::vector<int> steps; // per colony; -1 where the target was not reached
	int shortest = INT_MAX, longest = 0;
	int unreached = -1; // the first colony that could not reach its target
	/// Whether the walks differ by more than `allowed` steps or a third of the longest walk.
	bool tooUneven(int allowed) const
	{
		return longest - shortest > std::max(allowed, longest / 3);
	}
};
WalkSpread walkSpread(const Map &, const Torus &, const std::vector<std::vector<int>> &workers,
					  const std::vector<int> &targets);

/// Where one labelled region leaks into another it shouldn't: the first two neighbouring tiles,
/// diagonals included, both in `reached` (the ground units can actually walk to, such as a flood
/// from the colonies) whose labels differ and `allowed(a, b)` refuses. A design whose walls are
/// meant to leave only certain regions joined proves it with this. `tile` is -1 when nothing leaks.
struct RegionLeak
{
	int tile = -1, neighbour = -1;
};
template <typename Allowed>
RegionLeak firstRegionLeak(const Torus &t, const std::vector<unsigned char> &reached,
						   const std::vector<int> &labels, Allowed allowed)
{
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x;
			if (!reached[i] || labels[i] < 0)
				continue;
			for (int dy = 0; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					if (dy == 0 && dx <= 0)
						continue;
					const int j = t.at(x + dx, y + dy);
					if (reached[j] && labels[j] >= 0 && labels[j] != labels[i] &&
						!allowed(labels[i], labels[j]))
						return {i, j};
				}
		}
	return {};
}
} // namespace MapGeneration
