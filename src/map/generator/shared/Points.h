// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include <string>
#include <vector>
struct GenerationContext;
namespace MapGeneration
{
// Scattered sites on the torus and the cells they own: basins between ridges (Stone highlands),
// chambers in rock, plazas in a town, villages on a polder. Everything here is integer arithmetic on
// whole tiles and sixteenths of a tile, so the same seed draws the same cells on every platform.
// Tessellation.h is the regular counterpart (square and hexagon lattices); these are irregular.

struct Site
{
	int x, y;
};

/// Sites by dart throwing on the torus: a dart is kept when it lies at least `minimumPercent` of
/// `spacing` (and at least 4 tiles) from every site kept before it. `dartsPerSite` darts are thrown per
/// expected site (one per spacing squared, at least 2), enough to saturate the map, after which nearly
/// every dart lands too close. Draws come from the named stream, two per dart.
std::vector<Site> spreadPoints(const Torus &, int spacing, GenerationContext &,
							   const std::string &stream, int minimumPercent = 83,
							   int dartsPerSite = 60);

/// Every tile's nearest site, measured from a warped copy of the tile so cell borders wander instead of
/// running straight. The warp is two fractal noise fields (three octaves, `warpPeriodPercent` of the
/// spacing across, both drawn from `stream`) moving each tile by up to `warpPercent` of the spacing.
/// Distances are in sixteenths of a tile; a tie goes to the lower site index. `spacing` also sizes the
/// search buckets, so sites must be at least about `spacing` apart for the search to stay local (as
/// spreadPoints leaves them), and warpPercent 0 gives plain Voronoi cells.
std::vector<int> nearestSiteLabels(const Torus &, const std::vector<Site> &sites, int spacing,
								   GenerationContext &, const std::string &stream,
								   int warpPeriodPercent = 150, int warpPercent = 30);

/// The same labelling with the caller's own warp fields (two fields of 0..65535 per tile, as
/// periodicNoise or fractalNoise give them, or an orbitSum of one so a symmetric map gets symmetric
/// cells), each tile moved by up to `warpPercent` of the spacing.
std::vector<int> nearestSiteLabels(const Torus &, const std::vector<Site> &sites, int spacing,
								   const std::vector<int> &warpX, const std::vector<int> &warpY,
								   int warpPercent);

/// Lloyd relaxation on the torus: each site moves to the centroid of the tiles nearest it (measured the
/// short way round from the site), `iterations` times, so cells grow more even in size. Cheap enough for
/// a few dozen sites; the search is exhaustive.
std::vector<Site> relaxPoints(const Torus &, std::vector<Site> sites, int iterations);

/// The site graph of a labelling: two sites are neighbours when their cells touch (four-connected,
/// across the wrap), listed in ascending order.
std::vector<std::vector<int>> siteNeighbours(const Torus &, const std::vector<int> &labels,
											 int sites);
} // namespace MapGeneration
