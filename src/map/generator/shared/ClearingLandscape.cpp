#include "GenerationWork.h"
#include "GenerationNumeric.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClearingLandscape.h"
#include "GenerationContext.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Orbits.h"
#include "Pipeline.h"
#include <algorithm>
#include <cmath>
namespace MapGeneration
{
namespace
{
// Optional pools evenly spaced around each home.
constexpr double kPoolRadius = 2.5, kPoolRingShare = 0.6;
// The forest starts this far beyond a home's rough disc, so the clearing's edge is open ground to
// build against and the pond's beach never meets a tree; then a ring of sand this wide keeps the
// forest from spreading into the clearing. Two tiles: crops spread only onto
// an adjacent grass tile, so a single tile of sand corners already stops them, and the second tile
// keeps the beach arithmetic from ever leaving a grass corner between forest and clearing.
constexpr double kClearingMargin = 3.0;
constexpr int kSandRing = 2;
// Lakes keep this far from every clearing and from each other's shores, so a lake's regrowth never
// reaches a home's forest edge (the growth probe reaches 15 tiles) and two lakes read as two.
constexpr int kLakeGap = 20;
} // namespace
ClearingLandscape clearingLandscape(const GenerationRequest &request, GenerationContext &context,
									const ClearingLandscapeOptions &o)
{
	ClearingLandscape L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// Homes on the roomiest lattice, shrunk to leave a stretch of forest between neighbours at least
	// as wide as a clearing, so no two clearings ever touch.
	L.homes = latticeSites(t.w, t.h, teams, context.bounded("growth-layout", std::uint32_t(t.w)),
						   context.bounded("growth-layout", std::uint32_t(t.h)))
				  .sites;
	L.spacing = nearestSiteDistance(t, L.homes);
	L.homeRadius = std::min<double>(
		o.homeSize, ::MapGeneration::Numeric::floor(L.spacing / 3 - kClearingMargin - kSandRing));
	// Only a failing layout gets the vacancy search. This keeps successful maps, their
	// seed streams, and every Old Growth layout byte-for-byte stable. A composite lattice
	// with empty sites can rescue awkward prime counts on rectangles; it cannot force a
	// genuinely overcrowded shape to fit. The original first site's coordinates anchor
	// all alternatives without another random draw.
	if (o.repairCrowdedLattice && !homeHasRoom(L.homeRadius))
	{
		const LatticeSites roomy =
			roomyLatticeSites(t.w, t.h, teams, L.homes.front().x, L.homes.front().y);
		const double spacing = nearestSiteDistance(t, roomy.sites);
		const double radius = std::min<double>(
			o.homeSize, ::MapGeneration::Numeric::floor(spacing / 3 - kClearingMargin - kSandRing));
		if (roomy.vacancies && homeHasRoom(radius))
		{
			L.homes = roomy.sites;
			L.spacing = spacing;
			L.homeRadius = radius;
			context.telemetry.fallback(std::string(o.telemetryPrefix) + ".homes.vacancies",
									   "Left " + std::to_string(roomy.vacancies) +
										   " lattice sites empty to fit the colonies");
			context.telemetry.measure(std::string(o.telemetryPrefix) + ".homes.vacancies",
									  roomy.vacancies);
		}
	}
	dealStarts(context, L.homes); // which colony gets which site is a draw, not the order
	context.telemetry.measure(std::string(o.telemetryPrefix) + ".homes.spacing", L.spacing);
	context.telemetry.measure(std::string(o.telemetryPrefix) + ".homes.actual-radius",
							  L.homeRadius);
	if (L.homeRadius < o.homeSize)
		context.telemetry.fallback(std::string(o.telemetryPrefix) + ".homes.shrunk",
								   "Homes shrank to preserve intervening forest");
	if (!homeHasRoom(L.homeRadius))
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	L.clearing.assign(n, 0);
	L.sand.assign(n, 0);
	for (const ShapePoint &home : L.homes)
	{
		::MapGeneration::generationCheckpoint();
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			const double d = ::MapGeneration::Numeric::hypot(t.offsetX(int(home.x), t.remainderX(i)),
															 t.offsetY(int(home.y), i / t.w));
			if (d <= L.homeRadius + kClearingMargin)
				L.clearing.at(i) = 1;
			else if (d <= L.homeRadius + kClearingMargin + kSandRing)
				L.sand.at(i) = 1;
		}
	}
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (L.clearing.at(i))
			L.sand.at(i) = 0;
	}
	L.homeOf.assign(n, -1);
	L.water.assign(n, 0);
	const RadialShape home(L.homeRadius, 0.15, context, "growth-home");
	const RadialShape pond(homePondRadius(L.homeRadius), 0.3, context, "growth-pond");
	L.kits = stampRoundHomes(t, L.homes, 0.0, home, L.homeRadius, 1, &pond, L.water, L.homeOf);
	// The ring of pools, evenly spaced from a random phase per home.
	const RadialShape pool(kPoolRadius, 0.3, context, "growth-pools");
	for (const ShapePoint &home : L.homes)
	{
		::MapGeneration::generationCheckpoint();
		std::vector<ShapePoint> ring;
		const double phase = context.bounded("growth-pools", 3600) / 3600.0 * 2 * kPi;
		for (int p = 0; p < o.homePools; ++p)
		{
			::MapGeneration::generationCheckpoint();
			const ShapePoint at = polarPoint(home.x, home.y, kPoolRingShare * L.homeRadius,
											 phase + 2 * kPi * p / o.homePools);
			ring.push_back(at);
			fillShape(L.water, t, at.x, at.y, pool, 0.0);
		}
		L.pools.push_back(ring);
	}

	// Lakes: each at the tile farthest from every clearing and every lake so far (the exact
	// distance transform, Morphology.h), grown to `lakeSize` tiles by distance from its seed with a
	// little noise in the key so the outline is a lake's, not a disc's. `lakes` is a count per
	// 128x128 of map, so a 256 map gets four times as many as a 128.
	L.lake.assign(n, 0);
	// Area scaling makes the control mean the same density on squares and rectangles;
	// round to the nearest whole lake, rather than adding fractional or undersized lakes.
	const int lakes = int(::MapGeneration::Numeric::lround(o.lakes * double(n) / (128.0 * 128.0)));
	std::vector<unsigned char> settled(n, 0);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		settled.at(i) = L.clearing.at(i) || L.sand.at(i);
	}
	std::vector<unsigned char> keepClear = dilate(t, settled, kLakeGap);
	const std::vector<int> ripple = periodicNoise(t.w, t.h, 6, context.stream("growth-lakes"));
	std::vector<int> queued(n, 0);
	for (int lake = 0; lake < lakes; ++lake)
	{
		::MapGeneration::generationCheckpoint();
		const std::vector<std::int64_t> clearance = distanceSquaredTo(t, keepClear);
		int seed = -1;
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (!keepClear.at(i) && (seed < 0 || clearance.at(i) > clearance.at(seed)))
				seed = i;
		}
		if (seed < 0)
			break;
		const int grown = growWater(
			t, L.water, seed, o.lakeSize, [&](int i) { return !keepClear.at(i); },
			[&](int i)
			{
				const double d = ::MapGeneration::Numeric::sqrt(
					double(t.dist2(t.remainderX(seed), seed / t.w, t.remainderX(i), i / t.w)));
				// Integer priority: distance in thousandths of a tile plus up to 2.5 tiles
				// of periodic-noise perturbation (noise spans 0..65535). This roughens the
				// shore without letting noise defeat the distance-led compact lake shape.
				return std::int64_t(d * 1000) + std::int64_t(ripple.at(i)) * 2500 / 65536;
			},
			queued, lake + 1);
		if (grown <= 0)
			break;
		L.lakeCentres.push_back(seed);
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (L.water.at(i) && !L.clearing.at(i))
				L.lake.at(i) = 1;
		}
		keepClear = dilate(t, settled, kLakeGap);
		const std::vector<unsigned char> shores = dilate(t, L.lake, kLakeGap);
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			keepClear.at(i) = keepClear.at(i) || shores.at(i);
		}
	}
	context.telemetry.measure(std::string(o.telemetryPrefix) + ".lakes.requested", lakes);
	context.telemetry.measure(std::string(o.telemetryPrefix) + ".lakes.actual",
							  L.lakeCentres.size());
	if (int(L.lakeCentres.size()) < lakes)
		context.telemetry.fallback(std::string(o.telemetryPrefix) + ".lakes.omitted",
								   "No further lake fit with clearing and shore clearance");
	// The forest: everything that is not a clearing or water.
	L.forest.assign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		L.forest.at(i) = !L.clearing.at(i) && !L.sand.at(i) && !L.water.at(i);
	}
	return L;
}

} // namespace MapGeneration
