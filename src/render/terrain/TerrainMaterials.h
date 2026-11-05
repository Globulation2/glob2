// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include "TerrainSmoothBorders.h"

namespace TerrainVisual
{
using MaterialId = std::uint16_t;
struct Variant
{
	int frame;
	unsigned weight;
};
// Contour geometry of a boundary profile. Patch blends the nine prepared
// patches with authored contour curves; contextual follows marching-square
// curves through two-material cells; smooth follows a spline field through the
// vertex neighbourhood (see TerrainSmoothBorders.h).
enum class BoundaryShape { Patch, Contextual, Smooth };
struct Profile
{
	std::string key;
	BoundaryShape shape = BoundaryShape::Patch;
	// Version 4: rank among the edges of one border class; the higher one
	// shapes a contact between two materials of that class.
	int precedence = 0;
	// Version 4: profile this edge draws against a shoreline class, or -1.
	int coast = -1;
	int wiggle = 0;        // Smooth: Q8 pixels of meander along a straight contour.
	int wigglePeriod = 64; // Smooth: logical pixels between the broadest meanders.
	int squareness = 0;    // Smooth: Q8 from round (0) to square (256) corners.
	int roughness = 192;  // Q8 multiplier; 256 represents 1.
	int feather = 256;    // Q8 native-pixel edge softness.
	int amplitude = 512;  // Q8 pixel limit on local contour displacement.
	int speckle = 0;      // Q8 pixel reach of detached pebbles beside an edge.
	int bridge = 0;       // Q8 pixel half-width of the neck joining diagonal lobes.
	int shearScale = 128; // Version 3 reads shear curves as pixel displacement (256).
	bool legacyEdges = false;
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
// Animated shoreline where a liquid ("surf") material meets any material
// without surf: lighter shallows on the liquid side, a darker wet band on the
// land side, and a foam line that washes up the land and back. Widths and reach
// are Q8 logical pixels from the coverage margin, which is exact only within
// eight pixels of the edge, so every band must end inside that distance.
struct Surf
{
	int period = 0;                  // Animation ticks per wash; zero disables surf.
	int ticks = 1;                   // Animation ticks per surf update (and page re-blend).
	int reach = 0;                   // Q8 pixels the wash climbs onto the land.
	int shallow = 0, shallowWidth = 0; // Q8 tint strength and Q8 pixel width.
	int wet = 0, wetWidth = 0;         // Q8 darkening strength and Q8 pixel width.
	int foam = 0, foamWidth = 0;       // Q8 strength and Q8 pixel half-width.
	std::array<unsigned char, 3> shallowColor{}, foamColor{255, 255, 255};
};
// Applies surf to one composed sample. value holds the composed channels and
// is updated in place. surfSide is true when the sample's dominant material is
// the surf one. wx and wy are the sample's non-negative Q8 world pixel
// coordinates. The integer arithmetic is mirrored exactly by
// libs/gag/shaders/terrain-compose-fragment.glsl.
void applySurf(const Surf &, bool surfSide, int margin, int wx, int wy, unsigned time,
			   unsigned value[3]);
// Share of each wash (of 1024) spent rushing up the beach; the rest drains back
// more slowly. Mirrored as washIn in libs/gag/shaders/terrain-compose-fragment.glsl.
constexpr int SurfWashInQ10 = 224;
// Shore calm: animated surf liquids fade from their full waves to a quiet
// shimmer toward land, so the surf is the only motion at the beach. The fade
// ends ShoreCalmReachQ8 (Q8 cells) from the shoreline, which lies halfway
// between a land and a liquid vertex; ShoreCalmContrastQ8 is the share of the
// wave contrast kept at the shoreline itself.
constexpr int ShoreCalmReachQ8 = 512; // Two tiles.
constexpr int ShoreCalmContrastQ8 = 64;
// Q8 calm of a liquid vertex whose nearest non-surf vertex is at squared
// vertex distance d2 (zero means none within reach).
unsigned shoreCalmAt(int d2);
// An offset beyond this integer radius already has zero calm, even on
// an axis. The half-cell term matches the shoreline offset in shoreCalmAt.
constexpr int ShoreCalmSearch = (ShoreCalmReachQ8 + 128) / 256;
// Calm at a sample: corner calm bilinearly interpolated at Q8 cell position
// fx, fy (0..256). The result is continuous across cells.
unsigned shoreCalm(const std::array<std::uint8_t, 4> &corners, unsigned fx, unsigned fy);
// Pulls rgb toward mean by calm (Q8), keeping `strength` of the reduction;
// mirrored exactly by libs/gag/shaders/terrain-compose-fragment.glsl.
void applyShoreCalm(unsigned calm, unsigned strength, const std::array<unsigned char, 3> &mean,
					unsigned rgb[3]);
// Raised objects drawn over a material's cells with the resources, in screen
// row order, so they overlap neighbouring cells (boulders, hedges, rock).
// `full` frames are for cells surrounded by the same material, `edge` frames
// (smaller, pulled toward the cell centre) for cells with an open neighbour.
// Connected foliage instead selects its surface using the painted corner mask.
struct Decor
{
	std::string sprite;
	std::vector<int> full, edge;
	// Optional mask-major frames: TL=1, TR=2, BL=4, BR=8. Equal numbers
	// of variants for each of the 16 masks; mask zero is never drawn.
	std::vector<int> connected;
	// Corner tiles ("connected_corners"): each mask holds 16 variants indexed by
	// the colour bits of the cell's four vertices (see cornerColours), so a
	// texture continues across every cell edge without repeating periodically.
	bool connectedCorners = false;
	// Optional extra filled-mask frames ("connected_interior"), colour-major with
	// the same count per colour: alternative interiors picked by a cell hash.
	std::vector<int> connectedInterior;
	// Draw the corner-mask frames against open ground too ("connected_open"):
	// cells with undecorated corners show this material's edge, as for a cliff
	// whose rim and walls stand on the grass, instead of no raised decor.
	bool connectedOpen = false;
};
// Decor frames may be up to MaximumDecorFrame native pixels on each side. A
// frame up to 64 px tall is centred on its 32 px cell; a taller frame keeps a
// 64 px frame's bottom edge, 16 px below the cell, and grows upward, so tall
// obstacles stand on their own cell rather than spilling into the row south.
// Every frame is centred horizontally. Offsets are subtracted from the cell's
// top-left corner.
constexpr int MaximumDecorFrame = 128;
constexpr int decorOffsetX(int width) { return (width - 32) >> 1; }
constexpr int decorOffsetY(int height) { return std::max((height - 32) >> 1, height - 48); }
// Native pixels a decor frame extends beyond its cell on each side.
struct DecorReach
{
	int up = 0, down = 0, side = 0;
	void include(int width, int height)
	{
		up = std::max(up, decorOffsetY(height));
		down = std::max(down, height - 32 - decorOffsetY(height));
		side = std::max(side, decorOffsetX(width));
	}
};
// Version 4 border class: what kind of surface a material is (cover, cut,
// built, flow, solid, pool). Classes are listed strongest first; the stronger
// side of a contact shapes it. A shoreline class takes the shape of its basin,
// so the land against it draws its coast profile.
struct BorderClass
{
	std::string key;
	bool shoreline = false;
};
// A flat animated overlay drawn over the ground under any decor ("overlay"),
// such as the glints that mark fertile ground. Each cycle lists one frame per
// step, -1 for a step that draws nothing; a cell keeps one cycle, starts it at
// its own hashed step and advances one step every `ticks` scene ticks. Only
// densityQ8/256 of the cells draw at all, chosen by a presentation hash.
struct Overlay
{
	std::string sprite;
	std::vector<std::vector<int>> cycles;
	int ticks = 1;
	int densityQ8 = 256;
};
struct Material
{
	std::string key, sprite;
	unsigned borderClass = 0; // Version 4.
	std::vector<Variant> variants;
	unsigned totalWeight = 0;
	std::uint32_t salt = 0;
	std::uint32_t blockSalt = 0;
	unsigned profile = 0;
	int animationFrames = 1, animationTicks = 1, animationStride = 0;
	// Crossfade weights per phase ("animation_blend_steps"); zero blends on
	// every tick. Fewer steps recompose animated pages less often.
	int animationBlendSteps = 0;
	// Variants share one periodic edge band and join without the runtime's
	// border blend toward variant 0 ("edges": "periodic").
	bool periodicEdges = false;
	// Positional variants ("variant_grid"): the variants are one row-major
	// variantGrid x variantGrid block of cells repeating across the map, so a
	// periodic field larger than a cell continues across every cell edge.
	// Zero picks variants by hash.
	int variantGrid = 0;
	// Interchangeable blocks share boundary geometry; choose one per map block.
	int variantBlocks = 1;
	Decor decor;
	Overlay overlay;
	std::array<unsigned char, 3> preview{}, minimap{};
	Seam seam;
	Surf surf;
	// Set by the compositor for animated surf liquids: their mean native colour,
	// toward which shore calm pulls the waves.
	bool shoreCalm = false;
	std::array<unsigned char, 3> calmMean{};
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
	std::vector<BorderClass> borderClasses; // Version 4, strongest first.
	std::vector<Profile> profiles;
	std::vector<Material> materials;
	std::vector<PairTreatment> treatments;
	std::map<std::string, MaterialId> bindings;
	static Catalog parse(const nlohmann::json &document);
	MaterialId find(const std::string &key) const;
	// Boundary profile drawing the contact of two different materials; see
	// TerrainBorderRules.cpp.
	unsigned profileFor(MaterialId a, MaterialId b) const;
	bool contextualFor(MaterialId a, MaterialId b) const;
	// Version 4: a's border class is stronger, or the same class with a higher
	// edge precedence. Diagonal smooth contacts join the dominant material.
	bool dominates(MaterialId a, MaterialId b) const;
	// seed is the map's terrain look seed (Recipe::seed); zero for diagnostics.
	unsigned variantIndex(MaterialId material, int x, int y, std::uint32_t seed = 0) const;
	int frame(MaterialId material, int x, int y, int time, std::uint32_t seed = 0) const;
	// Decor frame for a cell of this material, or -1 when it has no decor.
	// cornerColours selects the variant of corner-tiled connected decor.
	int decorFrame(MaterialId material, int x, int y, bool edge, std::uint32_t seed = 0,
		unsigned cornerMask = 15, unsigned cornerColours = 0) const;
	// Overlay frame for a cell of this material at a presentation time (the
	// clock `frame` uses for animated materials), or -1 when the material has
	// no overlay, the cell is not chosen or its step is empty.
	int overlayFrame(MaterialId material, int x, int y, std::uint32_t time, std::uint32_t seed = 0, bool *animated = nullptr) const;
};
std::uint32_t hash(std::uint32_t x, std::uint32_t y, std::uint32_t salt = 0);
std::uint32_t mapSeedSalt(std::uint32_t seed);
// One seeded bit per map vertex, packed for a cell in corner-mask order. It is
// shared by all materials, so adjacent connected foliage joins the same shapes.
unsigned cornerColours(int x, int y, unsigned maskW, unsigned maskH, std::uint32_t seed);
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
	// Q8 shore calm of each corner (see shoreCalm), zero away from land.
	std::array<std::uint8_t, 4> calm{};
	bool calmed() const { return calm != std::array<std::uint8_t, 4>{}; }
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
	enum class Detail { Surface, Weights };
	PreparedCoverage(const Catalog &, const Recipe &, Detail = Detail::Surface);
	// Q8 local pixel coordinates; resolution independent and toroidally periodic.
	Coverage at(int px, int py) const;

  private:
	SmoothBorders smooth;
	Coverage roughCoverage(int px, int py) const;
	bool edgeMetadata;
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
	// Nearest contour segment (1-based, 0 when beyond the seam reach) at two-pixel
	// lattice points from -4 to 36 logical pixels. Samples search only the
	// segments their lattice corners name, not the whole curve.
	static constexpr int NearestLattice = 21;
	std::array<std::uint8_t, NearestLattice * NearestLattice> nearestSegment{};
	void prepareBorders(const Catalog &, const Recipe &);
	void prepareNearestSegments();
	unsigned borderMargin(Point sample) const;
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
