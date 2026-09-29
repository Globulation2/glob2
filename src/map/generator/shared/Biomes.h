// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include "Ressource.h"
#include "Sketch.h"
#include <string>
#include <vector>
class Map;
struct GenerationContext;
namespace MapGeneration
{
// A kind of land as data: how much water it holds, whether stone rings it, how much of it is farmland,
// forest, outcrops and fruit. A map that gives colonies different land of equal worth, rather than the
// same land turned round, deals each colony a kit, sizes each colony's ground by the kit's worth
// (growTerritories' worth, Territories.h), sketches the terrain and furnishes it with the same two calls
// whatever the kit.
//
// The worth is an estimate for sharing out ground, not a measurement: it adds up what each layer is
// usually worth to a young colony per tile, from the start scorer's weights (StartQuality.h). Tune a
// map's kits with the fairness tournament (docs/map-generators/FAIRNESS_TOURNAMENT.md), and measure
// the finished starts with scoreStarts, before trusting it.

struct BiomeKit
{
	std::string name;
	int pondsPer10000 = 0;      // ponds per 10000 tiles of ground (at least one when above 0)
	int pondTiles = 0;          // each pond's size in tiles
	int wallThickness = 0;      // a ring of stone round the ground this thick; 0 for none
	int farmPerMille = 0;       // of the ground's fertile tiles, the share under wheat and wood
	int woodPercent = 33;       // of that farmland, the share that is wood
	int outcropsPer1000 = 0;    // one-tile stone outcrops per 1000 tiles
	int grovesPer1000 = 0;      // one-tile fruit groves per 1000 tiles
	// Finer counts, added to the two above: per 100000 tiles. A kit of 3 outcrops per 1000 scaled by a
	// resource amount has only whole numbers to land on (50% and 75% of 3 both come to 2), so its
	// control has dead steps; a generator that scales these itself sets them here instead (Hidden
	// Oasis). 0, the default, leaves every other kit as it was.
	int outcropsPer100000 = 0, grovesPer100000 = 0;
	int coverPercent = 0;       // of the open ground left, the share grown over with cover
	int coverResource = WOOD;   // what the cover is: WOOD for a forest, STONE for scree
	// Of the open ground where crops never regrow (no water within the growth probe's reach), the
	// share planted with finite wheat and wood all the same, split like the farmland. A kit on real
	// geography has dry ground for hundreds of tiles; a reserve there is the difference between a
	// colony that can expand into it and one that cannot.
	int dryFarmPerMille = 0;
	bool orchardIsland = false; // the first pond holds an island of all three fruits
};

/// The kits a map can start from.
BiomeKit fertilePlain();  // a few big ponds, wide farmland, open ground
BiomeKit stoneFortress(); // a thick stone ring and outcrops, poor water
BiomeKit orchardIsland(); // a lake with an island of all three fruits, little farmland
BiomeKit forest();        // most of the ground under wood, ponds to keep it growing
// Kinds of real land (WorldAtlas.h), furnished from whatever water the geography gives them: none
// dig ponds of their own, and each keeps a dry reserve.
BiomeKit farmland(); // temperate plains: wide wheat fields with woodlots
BiomeKit woodland(); // rainforest and taiga: wood cover with clearings, few fields
BiomeKit savanna();  // steppe and savanna: sparse crops, more outcrops than groves
BiomeKit barrens();  // tundra: a little wood, a few outcrops, no fruit
BiomeKit highland(); // a mountain range: stone scree with gaps to thread, thin fields between

/// The kit with its ambient layers scaled to the map's resource amounts (Pipeline.h): the farmland's
/// wheat and wood shares by theirs, outcrops by stone, groves by fruit, and the cover by what it is
/// made of. At 100 everywhere the kit is returned unchanged.
struct ResourceAmounts;
BiomeKit scaledBiome(const BiomeKit &, const ResourceAmounts &);

/// The estimated worth of one tile of ground dealt this kit, relative to a fertile plain's 1.
double biomeWorth(const BiomeKit &);

/// What sketchBiome laid, for furnishBiome and a validator.
struct BiomeTerrain
{
	std::vector<unsigned char> water;  // pond tiles
	std::vector<unsigned char> wall;   // the stone ring
	std::vector<unsigned char> island; // the orchard island
};

/// The kit's terrain in `region`: ponds grown round (growWater) from seeds at least four tiles inside
/// the region, one of them with an island in the middle when the kit has an orchard, and the stone
/// ring on the region's rim (the tiles within `wallThickness` of ground outside it) except on `doors`.
/// Water goes into the sketch; lay beaches afterwards. Draws from `stream`.
BiomeTerrain sketchBiome(TerrainSketch &, const Torus &, const std::vector<unsigned char> &region,
						 const std::vector<unsigned char> &doors, const BiomeKit &,
						 GenerationContext &, const std::string &stream);

/// The kit's deposits on a written map: stone on the ring, farmland on the most fertile open ground in
/// patches (furnishGround), outcrops and groves, all three fruits on the island, and cover over the
/// share of what is left open, all a tile in from the ground's edge. Nothing goes on `keepClear` (a
/// swarm's clearing, a road), and the cover alone also keeps off `noCover` when given: a clearing
/// round a home in a forest or on a range, where fields may still lie but the wood or scree may
/// not, so a colony has room to build before it cuts. Draws from streams named after `stream`.
void furnishBiome(Map &, const Torus &, GenerationContext &,
				  const std::vector<unsigned char> &region, const BiomeTerrain &, const BiomeKit &,
				  const std::vector<unsigned char> &keepClear, const std::string &stream,
				  const std::vector<unsigned char> *noCover = nullptr);
} // namespace MapGeneration
