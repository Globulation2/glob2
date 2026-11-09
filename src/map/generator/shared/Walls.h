// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationWork.h"
#include "Grid.h"
#include "Sketch.h"
#include "TerrainGroup.h"
#include "TerrainGroupTable.h"
#include <functional>
#include <array>
#include <climits>
#include <string>
#include <vector>
class GenerationMap;
namespace MapGeneration
{
// Walls a generator designs and the checks that prove they hold. Stone can never be cleared, so a
// wall of it is permanent; but stone only stands on pure grass and grass may never touch water, so
// a wall is always a band of grass tiles given stone, never the water's edge itself. These are the
// pieces every walled map needs: which land a swimmer can stand on, a wall round a coast that keeps
// such a swimmer on the beach, a wall between labelled regions that no unit can cross even
// diagonally, and the walks and tower ranges that a validator checks the result against.

/// The land a unit landing from the sea can reach without crossing solid grass: every land tile
/// with a `sea` terrain vertex in the box its four corners touch, and every beach (land that is
/// not pure grass) joined to one. `sea` marks terrain vertices, so a generator decides which
/// water is sea and which (a home's lake) is not. `notBeach` marks sand that must not carry the
/// margin inland, such as a sand road.
std::vector<unsigned char> seaMargin(const GenerationMap &, const Torus &,
									 const std::vector<unsigned char> &sea,
									 const std::vector<unsigned char> &notBeach);

/// The sea for seaMargin: every water vertex of the map except those of `lakes`, the inland water a
/// generator keeps apart from the sea.
std::vector<unsigned char> seaVertices(const GenerationMap &, const Torus &,
									   const std::vector<unsigned char> &lakes);

/// Whether a sealed coast holds: the first tile of `inside` (land a swimmer must not get onto) that a
/// unit walking from the margin's walkable tiles reaches, or -1 when none is. Margin tiles themselves
/// never count.
int seaEntry(const GenerationMap &, const Torus &, const std::vector<unsigned char> &margin,
			 const std::vector<unsigned char> &inside);

/// The wall that seals a coast: stone on every pure-grass tile of `wallable` that touches the
/// margin, diagonals included. Every step off the beach then lands on stone, so a swimmer can land
/// and walk the sand lane outside it but never get in. Set these tiles to stone in index order.
std::vector<unsigned char> sealCoasts(const GenerationMap &, const Torus &,
									  const std::vector<unsigned char> &margin,
									  const std::vector<unsigned char> &wallable);

/// The sea's margin for a design of walled land in the sea with inland ponds and sand roads (Carousel,
/// Switchbacks): the sea is every water vertex but the `ponds`', and neither a road's spoiled tiles
/// (`roadTile`) nor those of any other designed sand line (`otherSand`, as vertices, such as a farm plot's
/// ring) count as beach, so none of them carries the margin inland.
std::vector<unsigned char> islandSeaMargin(const GenerationMap &, const Torus &,
										   const std::vector<unsigned char> &ponds,
										   const std::vector<unsigned char> &roadTile,
										   const std::vector<unsigned char> &otherSand);

/// Such a design's stone once the terrain is laid: every coast of `land` on the margin sealed
/// (sealCoasts), and every tile of the `designed` walls that the beaches left grass (designedStone).
std::vector<unsigned char> sealedIslandStone(const GenerationMap &, const Torus &,
											 const std::vector<unsigned char> &margin,
											 const std::vector<unsigned char> &land,
											 const std::vector<unsigned char> &designed);

/// A wall between labelled regions: a tile is wall when any of its eight neighbours has a lower
/// label. A tile left off the wall then touches only its own label or wall, so no unit can step,
/// even diagonally, from one region into another, and the wall is one tile thick. Negative labels
/// (sea, or ground no region claimed) take no part: they never make a wall and are never walled.
std::vector<unsigned char> labelBorders(const Torus &, const std::vector<int> &labels);
/// The same wall `thickness` tiles thick (1 to 3): the second tile on the other region's side of the
/// border, the third back on the first's.
std::vector<unsigned char> labelBorders(const Torus &, const std::vector<int> &labels,
										int thickness);

/// labelBorders with doors: the border between a tile and its lower-labelled neighbour is walled
/// unless `open(tile, neighbour)` leaves it open, as where a home meets its own corridor. A tile is
/// wall when any such closed border touches it, so the wall still stands on the higher label's side.
template <typename Open>
std::vector<unsigned char> labelBorders(const Torus &t, const std::vector<int> &labels, Open open)
{
	std::vector<unsigned char> wall(t.size(), 0);
	for (int y = 0; y < t.h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const int i = y * t.w + x;
			if (labels.at(i) < 0)
				continue;
			for (int dy = -1; dy <= 1 && !wall.at(i); ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = -1; dx <= 1; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					const int j = t.at(x + dx, y + dy);
					if (labels.at(j) >= 0 && labels.at(j) < labels.at(i) && !open(i, j))
					{
						wall.at(i) = 1;
						break;
					}
				}
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
DesignedStone designedStone(const GenerationMap &, const Torus &,
							const std::vector<unsigned char> &wall);

/// Steps from `from` over the land a ground unit can walk (walkableTiles: no water, deposit or
/// building) with the `shut` tiles closed as well: -1 wherever nothing was reached. The question
/// every sealing check asks - with the gates shut, can this ground reach that ground?
std::vector<int> reachesWithShut(const GenerationMap &, const Torus &,
								 const std::vector<unsigned char> &from,
								 const std::vector<unsigned char> &shut);

/// The check that a map's parts stay apart with their doors shut: floods each part's walkable tiles
/// (a `piece` label of 0 or more) over land that is not stone, crops and buildings counted as the ground they
/// will leave, with the `shut` tiles closed, and returns the
/// first tile of a different part any flood reaches, or -1 when every part keeps to itself.
int pieceLeak(const GenerationMap &, const Torus &, const std::vector<int> &piece,
			  const std::vector<unsigned char> &shut);

/// A raster gate connecting two region labels. The tiles are the complete plug cut
/// from a barrier, not its approach road. Separate entries preserve parallel gates
/// between the same regions. A plug may currently be blocked by clearable material.
struct TileGate
{
	std::array<int, 2> regions;
	std::vector<int> tiles;
};

struct GatePartitionCheck
{
	int leakTile = -1; // sealed walkable component touches different region labels
	int badGate = -1;  // empty/disconnected plug, missing side, or contact with a third region
};

/// Seal every gate in passable ground, then prove that connected components remain
/// inside one region and each gate is one connected plug touching exactly its two intended
/// regions. Uses eight neighbors and wraps both axes, matching ground-unit movement.
/// Unlabelled ground (-1) may connect labelled ground and therefore must not be
/// removed from the passable mask. Gates are tested independently of their current
/// passability, so the same check covers open doors and future clearable crossings.
/// Masks must cover the torus; gate tiles must be valid, unique indices per gate.
GatePartitionCheck checkGatePartition(const Torus &, const std::vector<unsigned char> &passable,
									  const std::vector<int> &labels,
									  const std::vector<TileGate> &gates);
/// The first two colonies that can walk to each other with the `shut` tiles closed, over land that
/// is not stone, crops and buildings counted as the ground they will leave (only water, stone and
/// the shut tiles part two colonies for good): {-1, -1} when every colony keeps to itself. The
/// check for a map whose causeways, bridges or gates are meant to be the only ways between
/// colonies: shut them and nobody should reach anybody. A shut sand line is terrain vertices, so
/// pass the tiles it makes walkable (Sketch.h's roadTiles), not the corners alone.
std::array<int, 2> colonyLeak(const GenerationMap &, const Torus &, int teams,
							  const std::vector<unsigned char> &shut);

/// The size of a defence tower's footprint, and the range of each of its three levels
/// (BuildingTypesDefence.cpp). A tower scans square rings round its footprint's top-left tile with
/// no line of sight (TurretTargeting::plan, BuildingUtils::turretScanTile), reaching its range in
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
WalkSpread walkSpread(const GenerationMap &, const Torus &,
					  const std::vector<std::vector<int>> &workers,
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
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const int i = y * t.w + x;
			if (!reached.at(i) || labels.at(i) < 0)
				continue;
			for (int dy = 0; dy <= 1; ++dy)
			{
				::MapGeneration::generationCheckpoint();
				for (int dx = -1; dx <= 1; ++dx)
				{
					::MapGeneration::generationCheckpoint();
					if (dy == 0 && dx <= 0)
						continue;
					const int j = t.at(x + dx, y + dy);
					if (reached.at(j) && labels.at(j) >= 0 && labels.at(j) != labels.at(i) &&
						!allowed(labels.at(i), labels.at(j)))
						return {i, j};
				}
			}
		}
	}
	return {};
}

/// A validator's proof that a designed wall stands: the first tile of `wall` that holds no stone on
/// the finished map, or of `doors` that holds stone, as a message naming `what` and the tile; ""
/// when every tile is as designed. A wall tile the beaches spoiled (no longer grass) counts as
/// broken, since the design promised stone there.
std::string wallStanding(const GenerationMap &, const Torus &,
						 const std::vector<unsigned char> &wall,
						 const std::vector<unsigned char> &doors, const char *what);

// Terrain walls. The stone walls above were the only permanent barrier when the engine had grass,
// sand and water alone; the terrain catalogue now has walls of their own (TerrainGroup.h):
// Obstacles stop units and arrows, Ridges stop units while towers shoot over. They are drawn on a
// TerrainSketch, on vertices, and a tile is blocked once two of its corners are.

/// Seals a coast in cliffs: every `wallable` vertex within `reach` vertices (Chebyshev, on the
/// torus) of a `sea` vertex becomes `cliff`, water included, so there is no beach to land on and no
/// shore to swim from. Two vertices of reach leave no walkable tile between the sea and the land
/// behind. `sea` must not mark a wallable vertex. Returns the vertices turned to cliff.
int cliffCoast(TerrainSketch &, const Torus &, const std::function<bool(int)> &wallable,
			   const std::function<bool(int)> &sea, int reach, TerrainType cliff = CLIFFS);

/// The vertices that wall off a tile mask (a labelBorders wall, a ring): the top-left corner of
/// every wall tile, which blocks it and the tile above or to its left (two corners each), plus
/// one more corner wherever two wall tiles touch only diagonally, so the wall has no diagonal gap.
/// With `keepOpen`, a wall tile touching a keepOpen tile (a lane the wall must not narrow) is walled
/// on every corner it does not share with one, so the wall leans away from the lane.
/// A wall tile left with fewer than two blocked corners gets back the corners the fewest open tiles
/// share, but never a corner of a `strict` tile (a door, which must keep every tile): a wall's end
/// beside a door is then left open, which only widens the door.
std::vector<unsigned char> wallVertices(const Torus &, const std::vector<unsigned char> &tiles,
										const std::vector<unsigned char> *keepOpen = nullptr,
										const std::vector<unsigned char> *strict = nullptr);
/// Thickens every Ridges wall (RIDGE_ROCK vertices) to at least three vertices: each rock vertex
/// ends up in a full 3x3 block of rock, so the wall has tiles whose four corners are all rock, the
/// only tiles a ridge look draws its peaks on (a two-vertex band, above all a diagonal or curved
/// one, draws as bare ground). For each vertex outside such a block the pass completes the block
/// round it that needs the fewest new vertices, all of which `canGrow` must allow (never a door,
/// a lane, water or paving), preferring the block with the most rock round it. Returns the vertices
/// added; `unresolved`, when given, counts the rock vertices no allowed block could complete.
int thickenRidges(TerrainSketch &, const Torus &, const std::function<bool(int)> &canGrow,
				  int *unresolved = nullptr);
/// Whether vertex i is a corner of some `tiles` tile.
bool cornerOf(const Torus &, const std::vector<unsigned char> &tiles, int i);

/// Paves the four corners of every `tiles` tile that is not water: `throatTerrain` where `throat`
/// marks the tile or the corner already holds it (sand, or rough ground, where nothing can be built
/// in a gate), TRAIL elsewhere. Marks every paved corner in `paved` (which must be the sketch's
/// size). Returns the corners paved.
int paveTiles(TerrainSketch &, const Torus &, const std::vector<unsigned char> &tiles,
			  const std::vector<unsigned char> &throat, std::vector<unsigned char> &paved,
			  TerrainType throatTerrain = SAND);

/// The flood a sealed world is checked with: from `units`' tiles over the terrain a ground unit
/// can walk (and, with `swim`, swim), deposits and buildings counted as cleared since they can be,
/// with the `shut` tiles closed. The tile outside `inside` the flood reaches first, -1 when it
/// reaches none: where the seal leaks.
int leakWithShut(const GenerationMap &, const Torus &, const std::vector<int> &units,
				 const std::vector<unsigned char> &shut, bool swim,
				 const std::function<bool(int)> &inside);

/// The first vertex of `vertices` whose terrain is not of `group`, or -1: a designed terrain wall
/// checked on the finished world.
int wallTerrainMismatch(const GenerationMap &, const Torus &, const std::vector<unsigned char> &vertices,
						TerrainGroup group);
} // namespace MapGeneration
