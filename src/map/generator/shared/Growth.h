// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FertilityField.h"
#include "Grid.h"
#include "Sketch.h"
#include <cstdint>
#include <vector>
namespace MapGeneration
{
// Where wheat and wood will grow back, known while the map is still a sketch.
//
// A crop spreads when a probe up to 15 tiles away on each axis finds pure water and the probe mirrored
// through the tile finds no pure sand (Map::growResources; Fertility::Field computes the exact chance
// for every tile). Planting.h's algaeGrowthChance answers the same question for algae on a finished
// map; these answer it for crops on a TerrainSketch, before anything is written, so a design can size
// its farmland to its water, keep a forest from ever growing back, or prove a region stays dry.

/// How far, on each axis, the engine's crop growth probe reaches for water.
constexpr int kCropProbeReach = 15;

/// Every tile's crop growth chance on Fertility::kScale, from the sketch as the game will draw it
/// (pure water and pure sand tiles, pureTiles). Unlike Fertility::forMap it is not gated on deposits
/// being able to reach a tile: this is where crops would regrow if planted.
Fertility::Field cropGrowthField(const TerrainSketch &, const Torus &);

/// The share (0 to 1) of `region`'s tiles whose chance is at least `minimum`; 0 for an empty region.
double wateredShare(const Fertility::Field &, const std::vector<unsigned char> &region,
					std::uint32_t minimum = 1);

/// How many of `region`'s tiles have any chance at all: a design that promises a region stays dry
/// (a forest that never grows back) checks this is 0.
int wetTiles(const Fertility::Field &, const std::vector<unsigned char> &region);

/// The tiles where water would give some tile of `region` a chance to regrow: every tile within
/// kCropProbeReach on each axis. Keep water out of it and the region stays dry.
std::vector<unsigned char> dryZone(const Torus &, const std::vector<unsigned char> &region);

/// Turns every water corner of the sketch inside `zone` to grass, and returns how many it changed. Run
/// it before layBeaches, since beaches are drawn from the water that is left.
int drainWithin(TerrainSketch &, const std::vector<unsigned char> &zone);
} // namespace MapGeneration
