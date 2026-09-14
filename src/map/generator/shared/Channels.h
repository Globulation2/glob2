// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Drawing.h"
#include "Grid.h"
#include "Sketch.h"
#include <vector>
class Map;
namespace MapGeneration
{
// Water between two pieces of land, sized for what it is meant to do: a canal towers shoot across
// from the first minute, a moat no tower can reach over, a strait with a few sand bridges. Every
// generator that parts land with water has done the same beach arithmetic by hand; it lives here once.
//
// The arithmetic. A straight channel `w` undermap corners wide gets a sand corner either side from
// layBeaches, and a tile is pure grass only when its four corners are, so the channel spoils w + 3
// tiles of grass across (kChannelSpoiledTiles), of which w - 1 are pure water. A tower's footprint on
// the last grass tile of one bank is w + 4 tiles (Chebyshev) from the first grass tile of the other,
// and a tower of range r hits what is r tiles away or nearer (towerReach, Walls.h).

/// The tiles of grass a channel spoils beyond its water corners.
constexpr int kChannelSpoiledTiles = 3;

/// The distance from a tower on one bank's last grass tile to the other bank's first grass tile, for a
/// straight channel `waterCorners` wide.
inline int bankToBank(int waterCorners)
{
	return waterCorners + kChannelSpoiledTiles + 1;
}

/// The widest channel (in water corners) a tower of `level` (1 to 3) on one bank still shoots across,
/// reaching `depth` tiles of grass into the far bank (1 = its first tile). 0 when even a single water
/// corner is too wide.
int widestChannelTowersCross(int level, int depth = 1);

/// The narrowest channel (in water corners) a tower of `level` cannot shoot across at all.
int narrowestChannelTowersMiss(int level);

/// A sketch's beach as the game will draw it: every tile that is neither pure grass nor pure water,
/// the walkable, unbuildable rim round water. Towers stand against it to cover a canal
/// (TowerRequest::against).
std::vector<unsigned char> beachTiles(const TerrainSketch &, const Torus &);

/// The same on a finished map.
std::vector<unsigned char> beachTiles(const Map &, const Torus &);

/// A sand bridge laid straight across water from `from` to `to`: every water corner within
/// `halfWidth` of the line becomes sand (strokePath over corners). A tile is water only when all four
/// of its corners are, so a bridge one corner wide already carries units on the two tiles either side
/// of it. Run after layBeaches (a bridge's own sand needs no beach). Returns the corners changed.
int bridgeAcross(TerrainSketch &, const Torus &, ShapePoint from, ShapePoint to, double halfWidth);

/// How many separate bridges (eight-connected pieces of `bridges`) touch each label's ground, for the
/// labels 0 to `labels` - 1: the check that every colony got as many ways across as every other.
std::vector<int> crossingsPerLabel(const Torus &, const std::vector<unsigned char> &bridges,
								   const std::vector<int> &labelled, int labels);
} // namespace MapGeneration
