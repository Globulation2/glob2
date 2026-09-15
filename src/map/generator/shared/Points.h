// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
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

/// The two closest distinct site entries to a tile, ordered by squared toroidal
/// distance and then input index. Missing sites/distances are -1. Keeping squared
/// distances preserves exact ties and lets callers avoid roots when only comparing.
struct NearestSites
{
	int first = -1, second = -1;
	int firstDistanceSquared = -1, secondDistanceSquared = -1;
};

/// Exhaustive unwarped query for small site sets, with no spacing requirement.
/// Useful for a region's owner and its boundary strength (difference of the two
/// distances), e.g. rivers between hills or ridges between basins. Duplicate site
/// coordinates remain distinct entries. Empty and singleton sets are supported.
NearestSites nearestTwoSites(const Torus &, const std::vector<Site> &, int x, int y);

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

/// A grain: a direction across the map, as whole steps so the same seed lands the same way on every
/// platform, and how much farther things reach along it than across it. Under a grain, distance is
/// measured in a frame squashed along the direction by `stretchPercent`: two sites a stretch apart
/// along the grain are as "close" as two sites one tile apart across it. Everything laid out under
/// a grain - sites, the landforms round them, the gaps between them - comes out elongated the same
/// way, which is what a field of drumlins, dunes or roches moutonnées looks like: a swarm all
/// pointing one way. A stretch of 100 is plain distance.
struct Grain
{
	int dx = 1, dy = 0;       // the direction, as whole steps (any pair but 0, 0)
	int stretchPercent = 100; // reach along the direction relative to across it, 100 or more
	/// The offset's part along the direction and across it (to its left), each scaled by the
	/// direction's length so no square root is needed: whole-number arithmetic.
	std::int64_t along(int offX, int offY) const
	{
		return std::int64_t(offX) * dx + std::int64_t(offY) * dy;
	}
	std::int64_t across(int offX, int offY) const
	{
		return -std::int64_t(offX) * dy + std::int64_t(offY) * dx;
	}
	/// Squared length of the direction, the scale the two parts carry.
	std::int64_t length2() const { return std::int64_t(dx) * dx + std::int64_t(dy) * dy; }
	/// The offset's squared distance under the grain, in tiles squared times length2() times
	/// stretchPercent squared: compare two of these directly, or with `metric2(tiles)` of a
	/// distance.
	std::int64_t distance2(int offX, int offY) const
	{
		const std::int64_t a = along(offX, offY), c = across(offX, offY);
		return a * a * 10000 + c * c * stretchPercent * stretchPercent;
	}
	/// A distance in tiles across the grain, in distance2's units.
	std::int64_t metric2(std::int64_t tiles) const
	{
		return tiles * tiles * length2() * stretchPercent * stretchPercent;
	}
	/// The heading of the direction, radians.
	double heading() const { return std::atan2(double(dy), double(dx)); }
	/// distance2 as a plain distance in tiles across the grain.
	double distance(int offX, int offY) const
	{
		return std::sqrt(double(distance2(offX, offY)) /
						 (double(length2()) * stretchPercent * stretchPercent));
	}
	/// The squared distance under the grain between two tiles of a torus, the short way round
	/// under the grain: on a torus a tile has four nearest images, and the one nearest in map tiles
	/// need not be the one nearest under the grain (two tiles half a map apart along the width may
	/// be closer through the wrap along the grain than across it), so all four are tried.
	std::int64_t distance2(const Torus &t, int ax, int ay, int bx, int by) const
	{
		const int dx = t.offsetX(ax, bx), dy = t.offsetY(ay, by);
		std::int64_t best = distance2(dx, dy);
		// A far image is at least as long in map tiles as its wrapped offset on that axis, and a
		// distance under the grain is at least the map distance over the stretch, so an image whose
		// wrapped offset alone exceeds the near image's map distance times the stretch can never
		// win: for a tile a spacing from its site on a big map, no far image is ever measured.
		const std::int64_t reach2 = (std::int64_t(dx) * dx + std::int64_t(dy) * dy) *
									stretchPercent * stretchPercent / 10000 + 1;
		const int otherX = dx > 0 ? dx - t.w : dx + t.w, otherY = dy > 0 ? dy - t.h : dy + t.h;
		const bool tryX = std::int64_t(otherX) * otherX <= reach2,
				   tryY = std::int64_t(otherY) * otherY <= reach2;
		if (tryX)
			best = std::min(best, distance2(otherX, dy));
		if (tryY)
			best = std::min(best, distance2(dx, otherY));
		if (tryX && tryY)
			best = std::min(best, distance2(otherX, otherY));
		return best;
	}
	double distance(const Torus &t, int ax, int ay, int bx, int by) const
	{
		return std::sqrt(double(distance2(t, ax, ay, bx, by)) /
						 (double(length2()) * stretchPercent * stretchPercent));
	}
};

/// The eight whole-step headings a grain is drawn from, about 22 degrees apart round half a turn
/// (the other half is the same grain the other way): index 0 runs along the width, 4 down the
/// height, 2 on the diagonal. `grainHeading` gives the grain of that index with a stretch.
constexpr int kGrainHeadings = 8;
Grain grainHeading(int index, int stretchPercent);

/// A grain from a control's choice: 0 a random heading drawn from `stream`, 1 along the width, 2
/// down the height, 3 the diagonal. What a "Grain" choice (Random, Horizontal, Vertical, Diagonal)
/// means to every map with a grain.
Grain grainForChoice(int choice, int stretchPercent, GenerationContext &,
					 const std::string &stream);

/// The heading (0 to kGrainHeadings - 1) under which `sites` lie farthest apart (the least distance
/// between any two, under that grain, is greatest; the lowest index on a tie): the grain that
/// leaves the most room round a design's fixed sites, when the drawn one leaves too little.
int widestGrainHeading(const Torus &, const std::vector<Site> &, int stretchPercent);

/// spreadPoints under a grain: darts are kept when they lie at least `minimumPercent` of `spacing`
/// (and at least 4 tiles) from every site kept before them, measured under the grain, so sites can
/// stand `stretch` times closer together along the grain than across it. `fixed` sites are kept
/// first and never moved, and a dart within `fixedMinimum` tiles (under the grain) of one of them
/// is refused: a design seats its colonies where it likes and lets the swarm fill in round them,
/// each colony keeping room for a landform of its own. Darts per expected site and draws as
/// spreadPoints.
std::vector<Site> spreadPoints(const Torus &, int spacing, const Grain &, GenerationContext &,
							   const std::string &stream, const std::vector<Site> &fixed,
							   int fixedMinimum, int minimumPercent = 83, int dartsPerSite = 60);

/// Each site's distance (under the grain, in tiles across it) to the nearest other site, the short
/// way round; the shorter map side when there is only one. The room a landform round each site has.
std::vector<double> nearestSiteDistances(const Torus &, const std::vector<Site> &, const Grain &);

/// Every tile's nearest site under the grain, a tie going to the lower index: the cells of a swarm
/// of aligned landforms, elongated with the grain, so two landforms end to end are neighbours as
/// readily as two side by side. `spacing` sizes the search buckets as nearestSiteLabels, and
/// `reach` (tiles under the grain; twice the spacing when 0) how far the search looks: the window
/// round a tile is the bounding box of the ellipse `reach` under the grain, so a site within reach
/// is always found and is then the nearest; a tile with no site within reach (a sparse corner) is
/// labelled by a search of every site. Pass a reach no tile's nearest site is beyond (a spacing
/// or two for a relaxed swarm, a fixed site's exclusion where one keeps the rest away).
std::vector<int> nearestSiteLabels(const Torus &, const std::vector<Site> &, int spacing,
								   const Grain &, int reach = 0);

/// Lloyd relaxation under a grain: each site moves to the centroid of the tiles nearest it under
/// the grain (nearestSiteLabels), `iterations` times, so the cells grow even in size and the swarm
/// packs as tightly as its count allows; the first `fixedCount` sites never move (a design's
/// colonies). Dart throwing alone leaves the cells uneven (some sites nearly a spacing apart, some
/// twice that), and landforms packed round uneven sites waste the room between the far pairs.
std::vector<Site> relaxPoints(const Torus &, std::vector<Site> sites, const Grain &, int spacing,
							  int iterations, int fixedCount = 0, int reach = 0);

/// Radii (under a grain, in tiles across it) for a landform round every site, packed so that any
/// two keep at least `gap` tiles of open ground between them in every direction: the gap is in map
/// tiles whichever way the pair lies, so two landforms end to end along the grain sit as close as
/// two side by side, not the stretch times farther (under the grain a pair's distance is its map
/// distance divided by up to the stretch, so the gap the pair needs under the grain is scaled the
/// same way). A site whose radius is given (`fixed[s]` at or above 0) keeps it, and every other
/// starts at half its nearest neighbour's distance less that pair's gap and then, in index order,
/// grows into whatever slack its neighbours leave (a neighbour that is small because of a third
/// site on its far side leaves room on this side), up to `maximum`, over a few rounds. A landform
/// that fits inside its site's radius under the grain (Teardrop::fitting; an ellipse `radius`
/// across and `radius` times the stretch long) then never comes within `gap` map tiles of another.
/// Sites left with a radius under `minimum` get -1: too small to be worth a landform, and the
/// ground goes to the water instead. Rounds are index order, so the result is the same on every
/// platform.
std::vector<double> packLandforms(const Torus &, const std::vector<Site> &, const Grain &,
								  const std::vector<double> &fixed, double gap, double minimum,
								  double maximum);

/// Farthest-point spreading over sites: `count` of the sites `eligible` allows, each in turn the
/// one farthest (in map tiles, the short way round) from every `seed` site and every site chosen
/// before it, the lowest index on a tie; a seed is never chosen. Prizes on the ground farthest from
/// every home, in the order a generator should hand them out.
std::vector<int> farthestSites(const Torus &, const std::vector<Site> &,
							   const std::vector<int> &seeds,
							   const std::vector<unsigned char> &eligible, int count);

/// The site graph of a labelling: two sites are neighbours when their cells touch (four-connected,
/// across the wrap), listed in ascending order.
std::vector<std::vector<int>> siteNeighbours(const Torus &, const std::vector<int> &labels,
											 int sites);

/// `count` sites spread over uneven ground by walking distance: the first is a random candidate,
/// and each next site is the candidate farthest, in steps over `walkable` (eight-connected, across
/// the wrap), from every site so far, so no two sites are near each other by any route a unit can
/// take. A coast, a lake or a range between two sites counts for what it is: the way round it.
/// Straight-line spreading (spreadPoints, relaxPoints) puts two sites a bay apart side by side;
/// this puts them a bay's walk apart. `trials` spreads are run from different first sites (one draw
/// from `stream` each) and the one whose closest pair is farthest apart is kept, so a first site on
/// a cape does not cramp the rest. Candidates a walk from the first site cannot reach are never
/// chosen (they are on another landmass). Fewer than `count` sites come back only when the
/// candidates reachable from the first run out. Tiles as indices, in the order chosen.
///
/// With `prefer`, each pick is the farthest candidate `prefer(tile)` accepts, tested farthest
/// first so few are tested; when none is accepted the farthest of all is taken, and `rejected`
/// (when given) counts the picks that fell back. A test that floods from the candidate (the room
/// a unit can reach from it, say) is affordable here where it is not for every candidate.
std::vector<int> farthestSites(const Torus &, const std::vector<unsigned char> &candidates,
							   const std::vector<unsigned char> &walkable, int count,
							   GenerationContext &, const std::string &stream, int trials = 6,
							   const std::function<bool(int)> *prefer = nullptr,
							   int *rejected = nullptr);

/// Each site moved to the candidate nearest the middle of its own ground: for site k, the tile of
/// `candidates` labelled k in `labels` (a territory of Territories.h, say) nearest, the short way
/// round, to the mean position of every tile labelled k, measured as offsets from the site so a
/// territory across the wrap has a sensible middle. A site with no labelled candidate stays. Run
/// after farthestSites and growTerritories, then grow the territories again: a site chosen for
/// being far from the others sits at the edge of its ground, and a round or two of this walks it
/// to where its colony has room on every side. Deterministic; ties go to the lowest tile index.
std::vector<int> recentreSites(const Torus &, const std::vector<int> &labels,
							   const std::vector<unsigned char> &candidates,
							   const std::vector<int> &sites);
} // namespace MapGeneration
