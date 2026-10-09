// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json_fwd.hpp>

namespace TerrainVisual
{
using MaterialId = std::uint16_t;
struct Variant
{
	int frame;
	unsigned weight;
};
struct Profile
{
	std::string key;
	int roughness = 192;  // Q8 multiplier; 256 represents 1.
	int feather = 256;    // Q8 native-pixel edge softness.
	int amplitude = 512;  // Q8 pixel limit on local contour displacement.
	int speckle = 0;      // Q8 pixel reach of detached pebbles beside an edge.
	int bridge = 0;       // Q8 pixel half-width of the neck joining diagonal lobes.
	int shearScale = 128; // Version 3 reads shear curves as pixel displacement (256).
	bool legacyEdges = false;
	bool contextual = false; // Neighbour-guided natural border geometry (version 3).
	std::vector<std::vector<int>> contours; // Q12 normalized patch displacements.
};
// Contact treatment where this material meets another. A higher material casts
// a shade onto lower neighbors within castWidth; a fringe tints any neighbor.
struct Seam
{
	int height = 0;                 // Stacking rank; equal ranks cast nothing.
	int cast = 0, castWidth = 0;    // Q8 darkening strength and Q8 pixel width.
	int fringe = 0, fringeWidth = 0; // Q8 tint strength and Q8 pixel width.
	std::array<unsigned char, 3> fringeColor{255, 255, 255};
};
// Raised objects drawn over a material's cells with the resources, in screen
// row order, so they overlap neighbouring cells (boulders, hedges, rock).
// `full` frames are for cells surrounded by the same material, `edge` frames
// (smaller, pulled toward the cell centre) for cells with an open neighbour.
struct Decor
{
	std::string sprite;
	std::vector<int> full, edge;
};
struct Material
{
	std::string key, sprite;
	std::vector<Variant> variants;
	unsigned totalWeight = 0;
	std::uint32_t salt = 0;
	unsigned profile = 0;
	int animationFrames = 1, animationTicks = 1, animationStride = 0;
	// Variants share one periodic edge band and join without the runtime's
	// border blend toward variant 0 ("edges": "periodic").
	bool periodicEdges = false;
	// Positional variants ("variant_grid"): the variants are one row-major
	// variantGrid x variantGrid block of cells repeating across the map, so a
	// periodic field larger than a cell continues across every cell edge.
	// Zero picks variants by hash.
	int variantGrid = 0;
	Decor decor;
	std::array<unsigned char, 3> preview{}, minimap{};
	Seam seam;
};
struct PairTreatment
{
	MaterialId a, b;
	unsigned profile;
};
class Catalog
{
  public:
	std::string compiledPack, serialized;
	int version = 2;
	// Q8 pixel amplitudes at 64px, 32px and 8px scales. Omitted in older packs.
	std::array<int, 3> boundaryWarp{};
	std::vector<Profile> profiles;
	std::vector<Material> materials;
	std::vector<PairTreatment> treatments;
	std::map<std::string, MaterialId> bindings;
	static Catalog parse(const nlohmann::json &document);
	MaterialId find(const std::string &key) const;
	unsigned profileFor(MaterialId a, MaterialId b) const;
	bool contextualFor(MaterialId a, MaterialId b) const;
	// seed is the map's terrain look seed (Recipe::seed); zero for diagnostics.
	unsigned variantIndex(MaterialId material, int x, int y, std::uint32_t seed = 0) const;
	int frame(MaterialId material, int x, int y, int time, std::uint32_t seed = 0) const;
	// Decor frame for a cell of this material, or -1 when it has no decor.
	int decorFrame(MaterialId material, int x, int y, bool edge, std::uint32_t seed = 0) const;
};
std::uint32_t hash(std::uint32_t x, std::uint32_t y, std::uint32_t salt = 0);
struct Recipe
{
	// Materials of the cell's corner vertices: top-left, top-right, bottom-left,
	// bottom-right. Only these materials may contribute to this cell.
	std::array<MaterialId, 4> corners{};
	// Canonical gameplay-cell coordinates and positive wrapped map dimensions.
	int x = 0, y = 0, width = 0, height = 0;
	// The map's terrain look seed: every hash of coordinates is salted with it,
	// so caches that compare recipes rebuild when it changes.
	std::uint32_t seed = 0;
	// Row-major vertices (-1,-1)..(2,2); optional for standalone diagnostics.
	std::array<MaterialId, 16> neighborhood{};
	bool hasNeighborhood = false;
	bool operator==(const Recipe &) const = default;
};
struct Coverage
{
	std::array<MaterialId, 4> material{};
	std::array<unsigned, 4> weight{}; // Sum exactly 65536.
	// Nearest other material and an estimate of the Q8 pixel distance to it,
	// for seam shading. Interior samples report 65535 and their own material.
	MaterialId neighbor = 0;
	unsigned margin = 65535;
};
// Contextual curves and the fallback's nine patches share immutable geometry
// across sampling resolutions. The fallback topology and contour choices agree
// across all native/HD samples. Patches are centred on the half-cell lattice at
// -8, 8, 24 and 40 logical pixels; each lattice point takes the nearest corner,
// so the outer patches repeat the tile's own corners and match the patches the
// neighbouring tiles prepare. Catalog must outlive this prepared view.
class PreparedCoverage
{
  public:
	PreparedCoverage(const Catalog &, const Recipe &);
	// Q8 local pixel coordinates; resolution independent and toroidally periodic.
	Coverage at(int px, int py) const;

  private:
	struct Point { float x = 0, y = 0; };
	struct Border
	{
		std::array<Point, 65> points{};
		std::array<float, 33> ordinate{}, normal{};
		bool vertical = false;
		float direction = 1;
	};
	Border naturalBorder{};
	bool hasBorder = false;
	std::array<MaterialId, 2> borderMaterials{};
	float borderBlendScale = 65536;
	void prepareBorders(const Catalog &, const Recipe &);
	Coverage patchCoverage(int px, int py, const std::array<int, 2> *displacement = nullptr) const;
	struct WarpLayer
	{
		int shift = 0, offsetX = 0, offsetY = 0;
		// At most five noise-grid vertices span a 32px tile at the finest scale.
		int vertices[25][2]{};
		std::array<int, 2> at(int px, int py) const;
	};
	// Direct indexing avoids accessor calls in unoptimized coverage builds.
	WarpLayer warp[3]{};
	bool legacy = false; // Version-1 catalogs keep their original sampling.
	std::uint32_t seed = 0; // Mixed map seed applied to every hash salt.
	struct Curve
	{
		const Profile *profile = nullptr;
		// Resolve the immutable contour once, outside the per-pixel path.
		const int *points = nullptr;
		int segments = 0;
		bool mirror = false;
		int sign = 1;
		int anchor = 0; // Prepared displacement of the shared edge's single crossing.
		Curve() = default;
		Curve(const Profile *, unsigned motif);
		int wave(int t) const;
	};
	struct Patch
	{
		std::array<Curve, 4> edges{};    // top, bottom, left, right
		std::array<Curve, 2> contours{}; // horizontal and vertical interior shear
		std::array<MaterialId, 4> materials{};
		std::array<unsigned, 4> slots{};
		unsigned count = 0;
		std::array<unsigned, 4> feather{}, speckle{};
		std::array<int, 4> pebbles{-1, -1, -1, -1}; // Pebble field per slot.
		std::array<int, 2> bridge{-1, -1};           // Slots joined across the center.
		int bridgeScale = 0;
	};
	Patch patches[9]{};
	// Sparse world-space pebbles of one material on a four-pixel grid. Cells
	// -1..8 cover the tile and the neighborhood its samples can touch.
	struct Pebble
	{
		int x = 0, y = 0, radius = 0, strength = 0; // Q8 tile-relative pixels.
	};
	struct PebbleField
	{
		MaterialId material = 0;
		std::array<Pebble, 100> cells{};
		int at(int px, int py) const; // Q12 shape of the strongest covering pebble.
	};
	std::vector<PebbleField> pebbles;
	int pebbleField(const Catalog &, const Recipe &, MaterialId);
	std::uint32_t salted(std::uint32_t salt) const { return salt ^ seed; }
};
// Convenience for individual diagnostic samples. Bulk composition prepares once.
Coverage coverage(const Catalog &, const Recipe &, int px, int py);
} // namespace TerrainVisual
