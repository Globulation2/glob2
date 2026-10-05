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
	int roughness = 192; // Q8 multiplier; 256 represents 1.
	int feather = 256;   // Q8 native-pixel edge softness.
	bool legacyEdges = false;
	std::array<std::vector<int>, 4> contours{}; // Q12 normalized patch displacements.
};
struct Backdrop
{
	std::string sprite;
	int firstFrame = 0, frames = 1, ticks = 1;
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
		std::array<std::array<int, 2>, 25> vertices{};
		std::array<int, 2> at(int px, int py) const;
	};
	std::array<WarpLayer, 3> warp{};
	int localDisplacementLimit = 512;
	struct Curve
	{
		const Profile *profile = nullptr;
		unsigned motif = 0;
		int anchor = 0; // Prepared displacement of the shared edge's single crossing.
	};
	struct Patch
	{
		std::array<Curve, 4> edges{};    // top, bottom, left, right
		std::array<Curve, 2> contours{}; // horizontal and vertical interior shear
		std::array<MaterialId, 4> materials{};
		std::array<unsigned, 4> slots{};
		unsigned count = 0;
		std::array<unsigned, 4> feather{};
	};
	std::array<Patch, 9> patches{};
};
// Convenience for individual diagnostic samples. Bulk composition prepares once.
Coverage coverage(const Catalog &, const Recipe &, int px, int py);
} // namespace TerrainVisual
