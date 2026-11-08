// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include "Sketch.h"
#include "Terrain.h"
#include "TerrainType.h"
#include <string>
#include <vector>
class GenerationMap;
struct GenerationContext;
struct GenerationRequest;
namespace MapGeneration
{
// Terrain post-passes for the height-field generators (River, Swamp, Crater lakes): the field,
// its resources and its starts are laid exactly as generateHeightField lays them, then a pass
// redraws the finished terrain with the engine's full palette. The pass keeps two sketches, as the
// designed generators do (Sketch.h): a mechanic sketch holding the canonical type of every
// vertex's rule group (GRASS, SAND, WATER, DEEP_WATER, LOAM, SHALE, MUDFLAT, SCREE, RIDGE_ROCK),
// which every shared helper and check reads, and looks painted over it with composeLooks, which can
// never change a rule. All of these are tile-indexed vertex masks and fields: vertex (x, y) is the
// top-left corner of tile (x, y).

/// The vertex terrain generateHeightField paints for this request (water, beach, grass and desert,
/// with Map::layBeaches applied), rebuilt from the "heightmap" stream of a fresh context with no
/// resources and no starts: what a validator replans a post-pass from. `context` must not have drawn
/// from "heightmap" yet; `build` is the generator's own field builder. Given `deposits`, it also
/// receives every tile's resource type from the field's own bands (paintHeightFieldResources: algae,
/// stone, wheat and wood; NO_RES_TYPE elsewhere), before any start, kit or grove.
TerrainSketch heightFieldSketch(const GenerationRequest &, GenerationContext &, const HeightFieldOptions &,
								const HeightFieldBuilder &build, std::vector<int> *deposits = nullptr);

/// The current vertex terrain of a map, as a sketch.
TerrainSketch mapSketch(const GenerationMap &);

/// Vertices of the Water or DeepWater group: what "water" means to a check once deep cores exist
/// (Map::isWater and a sketch's == WATER see only plain water).
std::vector<unsigned char> wetVertices(const TerrainSketch &);

/// Eight-connected steps from the nearest wet vertex (0 on water, -1 everywhere when there is none):
/// the field every "near the water" and "beyond the water's reach" rule below reads. The engine's
/// growth probe reaches 15 tiles on each axis, so ground 16 steps out is never watered, and a
/// mirrored probe reaches about 30, so an inhibitor 22 or more steps out costs a crop almost nothing.
std::vector<int> stepsFromWet(const Torus &, const TerrainSketch &);

/// The distance from the nearest wet vertex as a straight line, rounded to whole tiles (-1 everywhere
/// when there is none): round contours for looks and far-ground edges, where stepsFromWet's
/// eight-connected steps draw squares round every pond. Never less than stepsFromWet, so a rule that
/// must hold in steps (the growth probe's square reach) still needs stepsFromWet as its floor.
std::vector<int> distanceFromWet(const Torus &, const TerrainSketch &);

/// Deep water with a shallow margin: plain WATER vertices whose straight-line distance to the nearest
/// non-water vertex is at least `reach` tiles (at least 2) and, where given, allowed by `eligible`, in
/// connected bodies of at least `least` vertices so no lone dark vertex reads as a hole, become
/// `deep` (DEEP_WATER or DARK_WATER). The straight line rounds the core's outline (eight-connected
/// steps drew it as a staircase of squares), and it is never nearer land in steps than reach / sqrt(2):
/// a reach of 4 leaves two whole tiles of shallows along every bank, so beaches, algae and swimming
/// access keep working. Returns the vertices deepened.
int deepenWater(TerrainSketch &, const Torus &, int reach, int least, TerrainType deep = DEEP_WATER,
				const std::vector<unsigned char> *eligible = nullptr);

/// The least `reach` of at least `least` for which deepenWater's core (plain WATER vertices that many
/// tiles from land in a straight line) holds no more than `permille` thousandths of the plain water: a
/// wide river keeps a shallow margin in proportion to its width, so its algae (the lowest sixth of a
/// height field's water, on the very ground a deep core takes) can be seated again in the shallows.
int deepReachForShare(const TerrainSketch &, const Torus &, int least, int permille);

/// The share of wet vertices, in thousandths, in the (2 * radius + 1) square round every vertex
/// (windowCount, Morphology.h): 500 on a straight bank, above it where the land juts into water (the
/// inside of a bend, a point bar) and below it where water bites into the land (the outside of a
/// bend, a cutbank). `wet` is any vertex mask: pass one body's vertices to read only its bends.
std::vector<int> wetShare(const Torus &, const std::vector<unsigned char> &wet, int radius);

/// The wet bodies (eight-connected) holding at least `share` thousandths of the largest one's
/// vertices: a long river and its repeats rather than the ponds beside it.
std::vector<unsigned char> majorWater(const Torus &, const std::vector<unsigned char> &wet, int share);

/// A levee: vertices of `from` (GRASS) between `nearest` and `farthest` steps from water (one more
/// where `widen`, 0 to 65535, passes `cut`; pass an empty vector for none), that `eligible` allows,
/// become `fertile` (LOAM): buildable crop land that waters the ground round it at 48/256. Keep
/// `nearest` at 2 or more so the beach stays. Returns the vertices laid.
int layLevee(TerrainSketch &, const std::vector<int> &stepsFromWet, const std::vector<unsigned char> &eligible,
			 int nearest, int farthest, const std::vector<int> &widen, int cut, TerrainType from = GRASS,
			 TerrainType fertile = LOAM);

/// Pavement only beyond the water's reach: vertices of `from` at least `reach` steps from water (and
/// up to `fray` steps further where the noise `jitter`, 0 to 65535, is low), that `eligible` allows,
/// become `to`. With `to` in the Barren group (SHALE) this is building room that holds nothing and
/// inhibits growth as sand does, so keep `reach` at 22 or more: never closer than `reach`, whatever
/// the noise. Returns the vertices paved.
int paveBeyondWater(TerrainSketch &, const std::vector<int> &stepsFromWet, const std::vector<unsigned char> &eligible,
					const std::vector<int> &jitter, int reach, int fray, TerrainType from, TerrainType to);

/// Looks for the grass group graded by distance from water: LUSH_GRASS within `lush` steps, DRY_GRASS
/// beyond `green`, each edge moved up to `shift` steps either way by `jitter` (0 to 65535), on GRASS
/// mechanics that `plain` does not mark (home gardens stay plain GRASS). Given `noDry`, vertices it
/// marks may be lush but never dry (a home's garden keeps its green). Writes only those vertices of
/// `looks`.
void gradeGrassLooks(TerrainSketch &looks, const TerrainSketch &mechanics, const std::vector<int> &stepsFromWet,
					 const std::vector<unsigned char> &plain, const std::vector<int> &jitter, int lush, int green,
					 int shift, const std::vector<unsigned char> *noDry = nullptr);

/// Every vertex within `radius` tiles (plus or minus `fray` where `jitter`, 0 to 65535, rises or
/// falls) of a swarm's middle (boot + 2 on each axis): home ground a post-pass leaves plain GRASS, so
/// placeStarts, the starting-crop guarantee and openStartsBuriedByResources find the ground they
/// expect.
std::vector<unsigned char> homeGardens(const Torus &, const std::vector<int> &bootX, const std::vector<int> &bootY,
									   int teams, double radius, const std::vector<int> &jitter, double fray);

/// What writing a post-pass took away: deposits on tiles whose new terrain no longer allows them.
struct DisplacedResources
{
	int wheat = 0, wood = 0, stone = 0, algae = 0, fruit = 0, other = 0;
	std::vector<int> algaeTiles; // where the algae stood, for a caller that seats them again
	int total() const { return wheat + wood + stone + algae + fruit + other; }
};
/// Writes composeLooks(mechanics, looks) to the map's vertices and clears every deposit its tile no
/// longer allows (rock holds nothing, deep water no algae, rough and barren ground nothing).
DisplacedResources writeLayeredTerrain(GenerationMap &, const TerrainSketch &mechanics, const TerrainSketch &looks);

/// The first group of blocking terrain (tiles that neither walk nor swim, eight-connected) on the
/// finished map that parts the walkable ground round it, so that the walkable tiles beside it fall in
/// more than one walkable component: a knot of rock that walled ground off. Returns a tile of that
/// group, or -1 when none does.
int partingBarrier(const GenerationMap &);
} // namespace MapGeneration
