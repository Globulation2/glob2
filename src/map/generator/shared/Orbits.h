// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Geometry.h"
#include "Grid.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
class Game;
class Map;
struct GenerationContext;
namespace MapGeneration
{
// Fairness by symmetry: a map every colony sees the same way because a group of the torus's own
// symmetries carries each colony's ground exactly onto every other's. A wedge frame (Wedge.h) turns
// one design round the middle and rasterizes every copy separately, so copies agree to within a tile;
// a symmetry here maps tiles onto tiles, so copies agree exactly, deposits and all.
//
// Two kinds of group are offered. Point symmetries turn or mirror the map about its centre (a half
// turn for 2 colonies, quarter turns or mirrors for 4, all eight for 8; Symmetric arena). Translation
// symmetries slide the whole map by whole tiles onto itself: colonies on a lattice across the torus,
// no centre at all, every colony with the same neighbourhood in every direction, and a rectangular map
// served as well as a square one. Map sides are powers of two, so a translation group's order is too:
// 2, 4, 8, 16... colonies. Other counts have no exact group; latticeSites still spaces them evenly and
// the generator shares out the ground with growTerritories (Territories.h).

/// A symmetry as a signed permutation matrix acting on doubled centred coordinates, then a move by
/// (tx, ty) whole tiles. Tile (x, y) is (2x + 1 - W, 2y + 1 - H) and undermap corner (u, v) is
/// (2u - W, 2v - H). A tile's terrain comes from its four corners (Map::regenerateMap), and the same
/// matrix maps a tile's corners onto its image's corners, so the tile rotation (x, y) -> (W-1-y, x) is
/// the corner rotation (u, v) -> (W-v, u). A translation moves tiles and corners alike.
struct Isometry
{
	int a, b, c, d;
	int tx = 0, ty = 0;
};

/// A group of symmetries of a width x height torus. elements[0] is the identity, and colony k is
/// meant to start on elements[k]'s image of colony 0's home.
struct Symmetry
{
	int width = 0, height = 0;
	std::vector<Isometry> elements;

	int order() const { return int(elements.size()); }
	/// The index of the image of tile (x, y) under element e.
	int tile(int e, int x, int y) const;
	/// The index of the image of undermap corner (u, v) under element e.
	int corner(int e, int u, int v) const;
	/// The top-left tile of the image of a w x h footprint anchored at (x, y): the image of a
	/// rectangle is a rectangle, so it is the image's lowest corner on each axis.
	std::pair<int, int> anchor(int e, int x, int y, int w, int h) const;
	/// The tile a symmetry carries (x, y) to with the lowest index: one name for a whole orbit.
	int orbitKey(int x, int y) const;
};

/// The point symmetry that gives every colony identical ground: a half turn for two colonies, a
/// quarter turn for four on a square map, the two mirrors for four on a rectangular map (where a
/// quarter turn doesn't map the map onto itself), and all eight symmetries of a square for eight.
/// Elements are listed in order round the centre, so neighbouring colonies are neighbouring team
/// numbers. Empty for colony counts no point symmetry serves.
Symmetry pointSymmetry(int width, int height, int teams);

/// The translation group of `teams` elements whose nearest two copies of a tile lie farthest apart:
/// colonies on the roomiest lattice the torus holds, square, staggered like hexagons, or sheared.
/// Elements are listed row by row of the lattice. Empty when no group of that order exists (the
/// order must divide the tile count, a power of two) or `teams` is below 2. `spacing2`, if given,
/// receives the squared distance between the nearest two copies.
Symmetry translationSymmetry(int width, int height, int teams, long long *spacing2 = nullptr);

/// The coarsest noise cell (in tiles) that every element of a translation group leaves unchanged: the
/// greatest common divisor of the map's sides and every element's move. Periodic noise on cells of this
/// size (LatticeNoise.h) is the same at every image of a tile, so a design that warps its borders with
/// it stays an exact copy of itself at every colony without summing anything over orbits (which would
/// average the noise away). For a point symmetry, or an empty group, the shorter side.
int latticePeriod(const Symmetry &);

/// Where `teams` colonies go on a lattice across the torus, one per site, starting from (x0, y0).
/// `exact` when translationSymmetry serves the count, and then site k is element k's image of site 0;
/// otherwise equal rows of evenly spaced sites, as many rows as brings the spacing between rows
/// nearest a hexagonal lattice's, each shifted about half a step from the last (by whole steps over all
/// the rows, so the wrap stays seamless): the ground between them is nearly even, and growTerritories
/// shares it out exactly.
struct LatticeSites
{
	std::vector<ShapePoint> sites;
	bool exact = false;
	int vacancies = 0; // extra lattice sites left empty, if a sparse lattice was requested
};
LatticeSites latticeSites(int width, int height, int teams, double x0, double y0);

/// Move sites by at most `radius` whole tiles on each axis, preserving `minimumSpacing`
/// measured on wrapped, truncated tile centres as nearestSiteDistance does. One proposal per
/// site, in input order; a rejected move leaves that site unchanged. Returns accepted moves.
/// This bounded operation cannot rescue an initially invalid lattice; validate spacing first.
int jitterSites(const Torus &, std::vector<ShapePoint> &, GenerationContext &,
				const std::string &stream, int radius, double minimumSpacing);
/// Roomier fallback for colony counts whose equal-row factorization is crowded (notably primes).
/// Tries at most `maxVacancies` extra lattice sites and leaves the extras empty. Each vacancy is
/// chosen to maximize the minimum wrapped whole-tile distance among the surviving sites; equal
/// scores retain the first choice. The original lattice wins unless spacing strictly improves.
/// This is opt-in: callers that need exact symmetry or historic layout output keep latticeSites.
LatticeSites roomyLatticeSites(int width, int height, int teams, double x0, double y0,
							   int maxVacancies = 4);

/// Colony 0's feature stamped onto every image of it: an entry is set when any image of its tile (or
/// corner) lies in the feature. A union doesn't depend on the order an orbit is visited in, so the
/// result is exactly symmetric.
std::vector<unsigned char> stampOrbits(const Symmetry &, const std::vector<unsigned char> &feature,
									   bool corners);

/// Any integer field summed over every orbit. Integers, so the sum is exact in any order and every
/// member of an orbit gets the same value.
std::vector<int> orbitSum(const Symmetry &, const std::vector<int> &raw, bool corners);

/// Plain noise (HeightMap::makePlain with `smoothing`) from a named stream, as 12-bit integers summed
/// over every orbit: a field every symmetry leaves unchanged.
std::vector<int> orbitNoise(GenerationContext &, const Symmetry &, const std::string &stream,
							float smoothing, bool corners);

/// The share (0 to 1) of eligible entries with the highest (or lowest) values, as a mask. Ties at the
/// cut-off are all kept, so the members of an orbit, which have equal values, are never split.
std::vector<unsigned char> topShare(const std::vector<int> &value,
									const std::vector<unsigned char> &eligible, double share,
									bool highest);

/// The engine draws each deposit's amount and look from the gameplay RNG (Map::setResource). Every
/// orbit takes its lowest-indexed tile's, so deposits are symmetric in size as well as type. Fails,
/// naming the tile in `detail`, when an orbit's deposits differ in type.
bool equaliseDeposits(Map &, const Symmetry &, std::string &detail);

/// Whether a finished world is exactly symmetric: every element maps undermap corners, tile graphics
/// (by class), deposits (type and amount), buildings and units onto themselves, with the colonies
/// permuted one to one, and those permutations carry colony 0 onto every other colony. Empty when it
/// is; otherwise what broke, and where. `permutations`, if given, receives each non-identity element's
/// colony permutation.
std::string orbitMismatch(const Game &, const Symmetry &, int teams,
						  std::vector<std::vector<int>> *permutations = nullptr);
/// The least distance between any two of `sites`, the short way round the wrap, from whole-tile
/// positions (each site truncated to its tile); the shorter side when there is only one. What a
/// design shrinks its homes to fit between.
inline double nearestSiteDistance(const Torus &t, const std::vector<ShapePoint> &sites)
{
	double nearest = std::min(t.w, t.h);
	for (size_t a = 0; a < sites.size(); ++a)
		for (size_t b = a + 1; b < sites.size(); ++b)
			nearest = std::min(nearest, std::hypot(t.offsetX(int(sites[a].x), int(sites[b].x)),
												   t.offsetY(int(sites[a].y), int(sites[b].y))));
	return nearest;
}
} // namespace MapGeneration
