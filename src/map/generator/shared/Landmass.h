// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include "Topology.h"
#include <vector>
namespace MapGeneration
{
// Land drawn from a picture or a noise field, made fit to play on. A coastline that is faithful at
// the source's scale becomes, on a small map, a fringe of one-tile islets, one-tile inlets and
// peninsulas two tiles wide: land the beach pass turns entirely to sand, on which nothing can be
// built, and specks of sea that are all beach. These steps drop what cannot be used and keep the
// outline that can, and find the one landmass the colonies share.

/// What cleanLandmass removes, in tiles.
struct CoastCleaning
{
	/// Islands smaller than this vanish: nothing fits on them and they clutter the sea. Twelve tiles
	/// is the least on which a beach leaves any grass at all.
	int minimumIslandTiles = 12;
	/// Inlets and pools smaller than this are filled: sea that would be nothing but beach.
	int minimumWaterTiles = 6;
	/// Land narrower than 2 * radius + 1 tiles goes (openMask): grass needs four grass corners, so a
	/// strip two tiles wide is beach on both sides and grass nowhere. 1 drops one- and two-tile
	/// strips; 0 keeps every strip.
	int sliverRadius = 1;
};

/// `land` with its slivers dropped, its specks of sea filled and its islets removed, in that order,
/// eight-connected across the wrap. Filling before dropping islets keeps an island that a pool
/// nearly cut in two whole.
std::vector<unsigned char> cleanLandmass(const Torus &, const std::vector<unsigned char> &land,
										 const CoastCleaning &);

/// The largest connected part of a mask (ties to the lowest label, which is the first in row
/// order), as a mask of its own: the mainland every colony must stand on when a map has no fords.
std::vector<unsigned char> largestRegion(const Torus &, const std::vector<unsigned char> &mask,
										 GridNeighbors = GridNeighbors::Eight);

/// For a tile of `filled` (ground just filled in, say), the most common value of `labels` among its
/// eight neighbours that are not themselves filled, the lowest on a tie, or `fallback` when none:
/// a filled inlet takes the land round it, so a pool in the desert fills with desert.
std::vector<unsigned char> inheritLabels(const Torus &, const std::vector<unsigned char> &labels,
										 const std::vector<unsigned char> &filled,
										 unsigned char fallback);
} // namespace MapGeneration
