// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include "Topology.h"
#include <cstdint>
#include <vector>
namespace MapGeneration
{
// Mask arithmetic on the torus: growing and shrinking regions, closing gaps, dropping specks, and
// measuring how wide ground is. Every function wraps, works on whole tiles and uses integers only, so
// the same mask comes out on every platform.
//
// A design uses these to keep what it drew playable: a tunnel no narrower than a unit column, a
// corridor a tower can't close, no scrap of grass too small to build on. Widths here are in tiles
// across a passage: a tile with clearance c (Chebyshev steps to the nearest tile outside the mask)
// sits in ground at least 2c - 1 tiles across.

/// Every tile within `radius` Chebyshev steps (a square) of a mask tile. Radius 0 copies the mask.
std::vector<unsigned char> dilate(const Torus &, const std::vector<unsigned char> &mask,
								  int radius);

/// Every mask tile whose whole square of `radius` lies in the mask.
std::vector<unsigned char> erode(const Torus &, const std::vector<unsigned char> &mask, int radius);

/// Erode then dilate: drops parts of the mask narrower than 2 * radius + 1, keeps the rest's outline.
std::vector<unsigned char> openMask(const Torus &, const std::vector<unsigned char> &mask,
									int radius);

/// Dilate then erode: fills gaps and notches narrower than 2 * radius + 1.
std::vector<unsigned char> closeMask(const Torus &, const std::vector<unsigned char> &mask,
									 int radius);

/// Squared Euclidean distance from every tile to the nearest mask tile, the short way round; 0 on the
/// mask, and -1 everywhere when the mask is empty. Exact (lower envelopes of parabolas per axis).
std::vector<std::int64_t> distanceSquaredTo(const Torus &, const std::vector<unsigned char> &mask);

/// Every tile within `radius` tiles (a disc, Euclidean) of a mask tile.
std::vector<unsigned char> dilateRound(const Torus &, const std::vector<unsigned char> &mask,
									   double radius);

/// Chebyshev steps from every mask tile to the nearest tile outside it (1 on the rim); 0 off the mask.
/// A mask with nothing outside it gets the half of the longer side everywhere.
std::vector<int> clearance(const Torus &, const std::vector<unsigned char> &mask);

/// The mask with every connected part (with the given neighbours, across the wrap) smaller than
/// `minimumTiles` removed. Pass the inverted mask to fill pockets instead.
std::vector<unsigned char> dropSmallRegions(const Torus &, const std::vector<unsigned char> &mask,
											int minimumTiles,
											GridNeighbors neighbours = GridNeighbors::Eight);

/// The widest walk from any source to any goal through the mask, eight-connected: the largest
/// clearance c such that some walk keeps to tiles of clearance at least c, so its narrowest point is at
/// least 2c - 1 tiles across. 0 when no walk exists. Sources and goals must be mask tiles to count.
int widestWalkClearance(const Torus &, const std::vector<unsigned char> &mask,
						const std::vector<int> &sources, const std::vector<unsigned char> &goal);

/// widestWalkClearance as a passage width in tiles (2c - 1), 0 when no walk exists: the check a
/// validator makes that a designed tunnel, lane or street still takes a column of units.
int narrowestPassage(const Torus &, const std::vector<unsigned char> &mask,
					 const std::vector<int> &sources, const std::vector<unsigned char> &goal);

/// Every mask tile lying in ground narrower than an eroded square of `minimumClearance` survives:
/// the tiles openMask(mask, minimumClearance - 1) removes, the slivers a design should widen or give
/// up.
std::vector<unsigned char> slivers(const Torus &, const std::vector<unsigned char> &mask,
								   int minimumClearance);

/// The mask with every diagonal-only contact joined: where two mask tiles touch only at a corner and
/// neither tile beside that corner is in the mask, the one of those two with the lower index joins
/// it. A line of tiles that must hold water or wall against eight-connected movement (a river, a
/// moat's core) leaks at every diagonal step otherwise; after this the mask is four-connected
/// wherever it was eight-connected. One pass suffices: a bridging tile touches its two neighbours
/// orthogonally and creates no new diagonal-only contact with them.
std::vector<unsigned char> bridgeDiagonals(const Torus &, const std::vector<unsigned char> &mask);

/// How many mask tiles lie within `radius` Chebyshev steps (a square) of every tile, across the
/// wrap: running sums along rows then columns, so the cost does not grow with the radius. A cheap
/// measure of how much of a kind of ground a site has round it (buildable grass, farmable land).
std::vector<int> windowCount(const Torus &, const std::vector<unsigned char> &mask, int radius);

/// The least value of an integer field within `radius` Chebyshev steps (a square) of every tile,
/// across the wrap: a running minimum along rows then columns, so the cost does not grow with the
/// radius. A tile equal to its window's minimum is a local minimum with nothing lower within the
/// radius: pools at the bottoms of a field's troughs, spaced at least the radius apart.
std::vector<int> windowMinimum(const Torus &, const std::vector<int> &field, int radius);
} // namespace MapGeneration
