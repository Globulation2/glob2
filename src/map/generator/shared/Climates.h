// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include "Sketch.h"
#include "TerrainGroup.h"
#include "TerrainGroupTable.h"
#include <functional>
#include <vector>
struct GenerationContext;
namespace MapGeneration
{
// Climates: one look for a whole map, drawn once per map, so every colony's ground looks alike.
// A climate is only a look. A generator designs its world with one terrain per rule (the
// "mechanic" terrains below), validates and measures that, and paints the climate last: every
// look shares the rules of the mechanic it replaces (TerrainGroup.h), checked when this compiles,
// so the economy, the walls and the walks are the same in every climate and one tuning pass
// covers them all. City states, Carousel, Switchbacks, Gauntlet and Amphitheatre share these.
//
// The mechanics a design draws with, and the climate role each becomes:
//   GRASS        farmGrass (farm rows), homeGrass (tended) or openGrass        Grass
//   SAND         beach                                                          Sand
//   CLIFFS       cliff: sea cliffs, borders, mountain mass                      Obstacles
//   RIDGE_ROCK   rock: walls towers shoot over                                  Ridges
//   TRAIL        town, street, road, deck or bridge (the generator says which)  Paths
//   DIRT         scrub, mottled with scrubAccent                                Barren
//   MUD          rough                                                          Rough
//   LOAM         fertile                                                        Fertile
//   WATER        stays water, or deep where the generator says                  Water / Deep water
//   LAVA         lava, mottled with molten pools                                Lava
struct ClimateLook
{
	const char *name;
	TerrainType cliff, rock;              // Obstacles; Ridges
	TerrainType homeGrass, openGrass;     // Grass
	TerrainType beach;                    // Sand
	TerrainType town, street, road, deck; // Paths
	TerrainType bridge;                   // Paths
	TerrainType scrub, scrubAccent;       // Barren
	TerrainType deep;                     // Deep water
	TerrainType rough;                    // Rough
	TerrainType fertile, fertileAccent;   // Fertile
	TerrainType rockAccent;               // Ridges: a second rock, for tiers and banding
	TerrainType farmGrass;                // Grass: farm rows' crop ground
};
// The order is part of every map that draws a climate: append, never reorder.
inline constexpr ClimateLook kClimateLooks[] = {
	{"temperate", CLIFFS, RIDGE_ROCK, LUSH_GRASS, GRASS, PALE_SAND, BRICK_PATH, BRICK_PATH,
	 OVERGROWN_ROAD, SLATE_ASHLAR, BOARDWALK, CLAY, HEATH, DEEP_WATER, MUDFLAT, LOAM, SPRING_MEADOW,
	 OUTCROP, LUSH_GRASS},
	{"arid", SANDSTONE_BENCHES, HOODOOS, GRASS, DRY_GRASS, DUNE_SAND, SANDSTONE, SANDSTONE, TRAIL,
	 SANDSTONE, BOARDWALK, LATERITE, WEATHERED_SANDSTONE, DEEP_WATER, HARDPAN, LOAM, LOAM,
	 TERMITE_MOUNDS, DRY_GRASS},
	{"highland", GRANITE_TORS, RIDGE_ROCK, GRASS, GRASS, SAND, SLATE_ASHLAR, SLATE_ASHLAR, TRAIL,
	 SLATE_ASHLAR, BOARDWALK, SHALE, TUNDRA, DEEP_WATER, SCREE, MOSS, LOAM, OUTCROP, GRASS},
	{"volcanic", OBSIDIAN_RIDGE, OUTCROP, GRASS, DRY_GRASS, SAND, SCORIA_BLOCKS, BASALT_CAUSEWAY,
	 BASALT_CAUSEWAY, BASALT_CAUSEWAY, BOARDWALK, VOLCANIC_ASH, DIRT, DARK_WATER, PUMICE, LOAM, MOSS,
	 PETRIFIED_STUMPS, DRY_GRASS},
};
inline constexpr int kClimateCount = int(sizeof(kClimateLooks) / sizeof(kClimateLooks[0]));
inline constexpr int kVolcanicClimate = 3;
static_assert(
	[]
	{
		for (const ClimateLook &c : kClimateLooks)
			if (terrainGroup(c.cliff) != TerrainGroup::Obstacles ||
				terrainGroup(c.rock) != TerrainGroup::Ridges ||
				terrainGroup(c.rockAccent) != TerrainGroup::Ridges ||
				terrainGroup(c.homeGrass) != TerrainGroup::Grass ||
				terrainGroup(c.farmGrass) != TerrainGroup::Grass ||
				terrainGroup(c.openGrass) != TerrainGroup::Grass ||
				terrainGroup(c.beach) != TerrainGroup::Sand ||
				terrainGroup(c.town) != TerrainGroup::Paths ||
				terrainGroup(c.street) != TerrainGroup::Paths ||
				terrainGroup(c.road) != TerrainGroup::Paths ||
				terrainGroup(c.deck) != TerrainGroup::Paths ||
				terrainGroup(c.bridge) != TerrainGroup::Paths ||
				terrainGroup(c.scrub) != TerrainGroup::Barren ||
				terrainGroup(c.scrubAccent) != TerrainGroup::Barren ||
				terrainGroup(c.deep) != TerrainGroup::DeepWater ||
				terrainGroup(c.rough) != TerrainGroup::Rough ||
				terrainGroup(c.fertile) != TerrainGroup::Fertile ||
				terrainGroup(c.fertileAccent) != TerrainGroup::Fertile)
				return false;
		return true;
	}(),
	"A climate may change only looks, never a terrain's rules");

/// One climate for the map, from its own stream.
int drawClimate(GenerationContext &, const char *stream);

/// Which Paths look a paved vertex takes.
enum class PathLook
{
	Town,
	Street,
	Road,
	Deck,
	Bridge
};

/// What the generator says about the vertices whose look depends on where they are. Each may be
/// left empty: no vertex is tended (all grass openGrass), every path is a road, no water is deep,
/// and no rock is the accent.
struct LookRoles
{
	std::function<bool(int)> tended;
	std::function<PathLook(int)> path;
	std::function<bool(int)> deep;
	std::function<bool(int)> rockAccent;
	std::function<bool(int)> farm; // grass of a farm's rows: the climate's farm grass
};

/// Paints the climate over a finished mechanic sketch (see the header). Barren, lava and fertile
/// ground are mottled by a fixed speckle of the vertex coordinates, not a random draw.
void paintClimate(TerrainSketch &, const Torus &, const ClimateLook &, const LookRoles &);

/// A colony's base drawn as barren ground (DIRT: buildable, and no crop ever grows on it), for maps
/// whose food grows elsewhere: every GRASS vertex `base` marks becomes DIRT, except those `keep`
/// marks (ground left for crops) and a verge of grass `verge` vertices wide at the foot of every
/// wall (CLIFFS or RIDGE_ROCK), where quarry stone can still stand. Returns the vertices changed.
int barrenBase(TerrainSketch &, const Torus &, const std::function<bool(int)> &base,
			   const std::function<bool(int)> &keep, int verge);

/// A paved core round (cx, cy): every GRASS or DIRT vertex within `radius` that `base` marks becomes
/// TRAIL. For the town square round a swarm.
void paveCore(TerrainSketch &, const Torus &, double cx, double cy, double radius,
			  const std::function<bool(int)> &base);

/// Open water at least `from` vertices from any land, among the vertices `sea` marks: where a
/// generator's deep water goes (slower swimming, no algae, which keeps to the shallows).
std::vector<unsigned char> deepSea(const TerrainSketch &, const Torus &,
								   const std::function<bool(int)> &sea, int from);
} // namespace MapGeneration
