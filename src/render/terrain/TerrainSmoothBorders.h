// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace TerrainVisual
{
using MaterialId = std::uint16_t;
class Catalog;
struct Recipe;
struct Coverage;
// Smooth contour geometry ("shape": "smooth"). Each corner material of a tile
// owns a field interpolated through the 4x4 vertex neighbourhood with
// Catmull-Rom splines; the dominant field wins. The splines interpolate their
// vertices, so every vertex keeps its own material, and they join with
// continuous slope across tile edges, so contours flow across the whole map
// instead of restarting in every cell. Meanders come from world-anchored value
// noise multiplied into each field: noise never reaches a field that is zero,
// so a material cannot appear where its vertices do not reach.
//
// The share of smooth geometry is decided per vertex: a vertex is smooth when
// every contact with its eight neighbours resolves to a smooth profile. Vertex
// shares interpolate bilinearly across the tile, so a smooth region fades into
// the patch and contextual geometry of its surroundings without tile seams.
class SmoothBorders
{
  public:
	void prepare(const Catalog &, const Recipe &);
	// No corner of the tile takes part in a smooth contact.
	bool inactive() const { return !anyShare; }
	// Every corner is smooth: the patch geometry is not needed.
	bool complete() const { return fullShare; }
	// Q12 share of the smooth geometry at a Q8 local pixel position.
	int share(int px, int py) const;
	Coverage at(int px, int py) const;

  private:
	struct NoiseLayer
	{
		int shift = 0, offsetX = 0, offsetY = 0;
		// At most five lattice vertices span a tile at the 8px finest spacing.
		float vertices[25]{};
		float at(int px, int py) const;
	};
	struct Field
	{
		MaterialId material = 0;
		std::array<NoiseLayer, 2> noise{};
		float feather = 1;   // Pixels; the ramp width shared with a competitor is the mean.
		float bias = 0;      // Saddle bias that joins diagonal lobes of this material.
	};
	std::array<Field, 4> fields{};
	unsigned count = 0;
	// Index into fields for each neighbourhood vertex, or -1 outside the palette.
	std::array<int, 16> owner{};
	std::array<int, 4> cornerShare{};     // Q12 per corner, TL TR BL BR.
	std::array<float, 4> cornerWiggle{};  // Multiplicative noise strength per corner.
	std::array<float, 4> cornerSquareness{}; // 0 round to 1 square, per corner.
	bool anyShare = false, fullShare = false;
	static float bilinear(const std::array<float, 4> &corner, float u, float v);
};
} // namespace TerrainVisual
