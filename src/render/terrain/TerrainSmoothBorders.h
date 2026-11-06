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
// Version-4 catalogs draw every contact with this geometry; a profile only
// sets its parameters (meander, fine detail, squareness, feather, bridge), and
// these interpolate between vertices, so contours keep one algorithm and one
// distance measure across every tile edge whatever terrains meet. Earlier
// catalogs keep patch and contextual geometry: for them a vertex is smooth when
// every contact with its eight neighbours names a smooth profile, and vertex
// shares fade one geometry into the other.
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
		// At most ten lattice vertices span a tile at the 4px finest spacing,
		// shifted by half a spacing (see JaggedPhase).
		static constexpr int Stride = 10;
		int shift = 0, offsetX = 0, offsetY = 0;
		// Linear interpolation leaves creases between lattice points.
		bool linear = false;
		// Ridged layers fold the noise at zero (1 - 2|n|) into sharp creases.
		bool ridged = false;
		float weight = 1;
		float vertices[Stride * Stride]{};
		// Value and its gradient per Q8 pixel.
		float at(int px, int py, float &dx, float &dy) const;
	};
	struct Field
	{
		MaterialId material = 0;
		std::array<NoiseLayer, 2> noise{}; // Broad meander: the period and half of it.
		NoiseLayer detail{};               // Fine edge roughness at DetailPeriod.
		// Fractal grit at 32, 16, 8 and 4 px, each 0.6 of the last; the finest
		// two ridged. Irregular at every scale, like a natural coastline.
		std::array<NoiseLayer, 4> grit{};
		float feather = 1;   // Pixels; the ramp width shared with a competitor is the mean.
		float bias = 0;      // Saddle bias that joins diagonal lobes of this material.
	};
	// The tile's corner materials first: only they take coverage. Halo
	// materials (Recipe::halo) follow and only give seams and surf a distance.
	std::array<Field, 6> fields{};
	unsigned count = 0, weighted = 0;
	// Bit g of bands[f]: fields f and g draw a seam or surf band where they
	// meet (Catalog::bandsBetween). Seams and surf measure only to those.
	std::array<std::uint8_t, 6> bands{};
	// Index into fields for each neighbourhood vertex, or -1 outside the palette.
	std::array<int, 16> owner{};
	std::array<int, 4> cornerShare{};     // Q12 per corner, TL TR BL BR.
	std::array<float, 4> cornerWiggle{};  // Multiplicative noise strength per corner.
	std::array<float, 4> cornerDetail{};  // Fine roughness strength per corner.
	std::array<float, 4> cornerJagged{};  // Angular grit strength per corner.
	std::array<float, 4> cornerSpeckle{}; // Pixels pebbles reach across a border, per corner.
	std::array<float, 4> cornerSquareness{}; // 0 round to 1 square, per corner.
	bool anyShare = false, fullShare = false;
	// The tile's world origin in logical pixels and the look seed, for pebbles.
	float originX = 0, originY = 0;
	std::uint32_t seed = 0;
	// Each material's pebbles: the share of one at a Q8 local position, 0 to 1.
	float pebble(unsigned field, int px, int py) const;
	std::array<std::uint32_t, 6> pebbleSalt{};
	int width = 0, height = 0; // Map size in cells, for wrapping pebbles.
	static float bilinear(const std::array<float, 4> &corner, float u, float v);
};
} // namespace TerrainVisual
