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
	std::vector<std::vector<int>> contours; // Q12 normalized patch displacements.
};
struct Backdrop
{
	std::string sprite;
	int firstFrame = 0, frames = 1, ticks = 1;
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
struct Material
{
	std::string key, sprite;
	std::vector<Variant> variants;
	unsigned totalWeight = 0;
	std::uint32_t salt = 0;
	unsigned profile = 0;
	bool ocean = false;
	Backdrop backdrop;
	int animationFrames = 1, animationTicks = 1, animationStride = 0;
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
	unsigned variantIndex(MaterialId material, int x, int y) const;
	int frame(MaterialId material, int x, int y, int time) const;
};
std::uint32_t hash(std::uint32_t x, std::uint32_t y, std::uint32_t salt = 0);
// Pure presentation adapter. Saved sprite numbers never become material handles.
std::array<unsigned, 4> legacyCorners(unsigned frame);
struct Recipe
{
	// Lattice samples at -8,8,24,40 logical pixels relative to a 32px cell.
	std::array<MaterialId, 16> samples{};
	// Canonical gameplay-cell coordinates and positive wrapped map dimensions.
	int x = 0, y = 0, width = 0, height = 0;
	bool operator==(const Recipe &) const = default;
};
struct Coverage
{
	std::array<MaterialId, 4> material{};
	std::array<unsigned, 4> weight{}; // Sum exactly 65536, including ocean.
	// Nearest other material and an estimate of the Q8 pixel distance to it,
	// for seam shading. Interior samples report 65535 and their own material.
	MaterialId neighbor = 0;
	unsigned margin = 65535;
};
// A tile's nine transition patches share immutable topology and contour choices
// across all native/HD samples. Catalog must outlive this prepared view.
class PreparedCoverage
{
  public:
	PreparedCoverage(const Catalog &, const Recipe &);
	// Q8 local pixel coordinates; resolution independent and toroidally periodic.
	Coverage at(int px, int py) const;

  private:
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
};
// Convenience for individual diagnostic samples. Bulk composition prepares once.
Coverage coverage(const Catalog &, const Recipe &, int px, int py);
} // namespace TerrainVisual
