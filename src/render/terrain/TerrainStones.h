// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "TerrainMaterials.h"

namespace TerrainVisual
{
// Whole-stone path edges. A laid stone path ("stones" in its catalog block)
// keeps or drops each stone as a unit where it borders other ground, so its
// edge runs along the joints instead of cutting a stone in half.
//
// The decision is made once per stone, at the stone's centre, from the same
// smooth-border coverage the rest of the cell uses: a stone is kept when its
// material is the largest there. Compositor::describe records the decisions
// for a cell and its eight neighbours in the Recipe (Recipe::StoneLayer), so cached
// pages that compare recipes already rebuild when any of them changes, and
// every pixel then needs only a stone-map lookup and a bit test.
//
// The stone map (tools/artwork/terrain_stones.py) covers one variant block of
// tiles x tiles cells at four texels per logical pixel, the same in every
// layout. Each texel names its stone and the two nearest other stones, each as
// the stone's number among the stones centred in one cell and that cell's
// offset from the texel's cell, with the distance to each joint, and the
// texel's signed distance from its own stone's face (negative in the joint).
// Knowing two neighbours tells a texel where three stones meet whether any of
// them was dropped; a kept stone beside a dropped one then ends at its face.
class StoneMap
{
  public:
	static constexpr int TileTexels = 128;          // Four per logical pixel.
	static constexpr unsigned MaxStonesPerTile = 64; // One decision bit each.
	// Two size x size RGBA8 blocks side by side (rows of `pitch` bytes), size a
	// multiple of TileTexels: the stones and nearest neighbours, then the
	// second neighbours. Throws std::runtime_error on an inconsistent map.
	StoneMap(const std::uint8_t *rgba, int size, int pitch);
	int tiles() const { return tileCount; }
	struct Texel
	{
		std::uint8_t own, ownTile, across, acrossTile; // Tile: (dx+1) + 3(dy+1).
		unsigned joint;                                 // Q8 logical pixels.
		std::uint8_t second, secondTile;                // The second-nearest stone.
		unsigned secondJoint;
		int face; // Q8 logical pixels from the stone's face, negative in the joint.
	};
	// The texel at Q8 logical position (px, py) of block cell (tx, ty).
	Texel at(int tx, int ty, int px, int py) const;
	// The Q8 logical centre of each stone centred in block cell (tx, ty), by number.
	const std::vector<std::array<int, 2>> &centres(int tx, int ty) const
	{
		return tileCentres[std::size_t(ty) * std::size_t(tileCount) + std::size_t(tx)];
	}
	// The stones with texels in block cell (tx, ty), one bit per stone for each
	// cell offset (dx+1) + 3(dy+1) of the cell holding its centre.
	const std::array<std::uint64_t, 9> &reaching(int tx, int ty) const
	{
		return tileReach[std::size_t(ty) * std::size_t(tileCount) + std::size_t(tx)];
	}

  private:
	int size = 0, tileCount = 0;
	std::vector<std::uint32_t> texels, seconds;
	std::vector<std::vector<std::array<int, 2>>> tileCentres;
	std::vector<std::array<std::uint64_t, 9>> tileReach;
};
// The stone maps of a catalog's laid paths, one per material that names one.
// A decoded map takes four bytes per texel, so each is read on first use (by
// any thread) and kept; maps without stone paths never load one.
class StoneMaps
{
  public:
	// Checks that every named stone map can be opened.
	explicit StoneMaps(const Catalog &);
	bool has(MaterialId id) const { return id < slots.size() && slots[id]; }
	// The material's stone map, or null when it has none. Throws
	// std::runtime_error when the map cannot be read or is invalid.
	const StoneMap *get(MaterialId) const;

  private:
	struct Slot
	{
		std::string path;
		mutable std::once_flag once;
		mutable std::unique_ptr<StoneMap> map;
	};
	std::vector<std::unique_ptr<Slot>> slots;
};
// Vertices beyond a cell's corners that its stone decisions read: the 4x4
// windows of the eight neighbouring cells whose stones can reach it.
constexpr int StoneSearch = 2;
// Q8 logical pixels over which a kept stone's exposed face fades into the
// ground beside it, half on either side of the face.
constexpr unsigned StoneRimQ8 = 256;
// Replaces the stone material's share of a coverage sample at Q8 logical
// position (px, py) of the recipe's cell with whole stones, and points the
// seam distance at the stone edge so contact shading follows the stones.
// Applies one stone layer (Recipe::stoneLayers[layer]) with its own stone map;
// callers apply the layers from the last to the first, so the dominant path's
// kept stones lie over the other's.
void applyStones(const Recipe &, unsigned layer, const StoneMap &, int px, int py, Coverage &);
} // namespace TerrainVisual
