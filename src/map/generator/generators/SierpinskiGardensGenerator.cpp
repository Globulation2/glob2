// SPDX-License-Identifier: GPL-3.0-or-later
#include "SierpinskiGardensGenerator.h"
#include "FractalMapSupport.h"
#include "Game.h"
#include "Drawing.h"
#include <algorithm>
#include <cmath>
using namespace FractalMaps;
namespace
{
struct GardensOptions
{
	int nesting, minimumSide, lakePercent, pairs;
	explicit GardensOptions(const GenerationRequest &r)
		: nesting(r.option("maximum-nesting")), minimumSide(r.option("minimum-district-side")),
		  lakePercent(r.option("lake-size")), pairs(r.option("major-crossing-pairs"))
	{
	}
};
RegionBounds lakeIn(RegionBounds b, int percent)
{
	// Central-third openings use the parent's exact integer boundaries; the size slider
	// expands about that opening's centre. Rounding is shared by terrain and reservations.
	const int x0 = b.x0 + b.width() / 3, x1 = b.x0 + b.width() * 2 / 3;
	const int y0 = b.y0 + b.height() / 3, y1 = b.y0 + b.height() * 2 / 3;
	const int width = (x1 - x0) * percent / 100, height = (y1 - y0) * percent / 100;
	return {(x0 + x1 - width) / 2, (y0 + y1 - height) / 2, (x0 + x1 - width) / 2 + width,
			(y0 + y1 - height) / 2 + height};
}
Layout design(const GenerationRequest &r, GenerationContext &context)
{
	const GardensOptions o(r);
	Layout L;
	initialize(L, r);
	if (!L.failure.empty())
		return L;
	const auto &t = L.t;
	const RegionBounds root{0, 0, t.w, t.h}, lake = lakeIn(root, o.lakePercent);
	fillRectangle(L.terrain, t, lake, WATER);
	// The central lake's outer shore gets wheat spots when the resources go down.
	fillRectangle(L.wheatShore, t, lake, 1);
	// Reserve homes against the largest, unavoidable lake FIRST. Smaller lakes are only
	// allowed in unreserved districts. No late "make room" pass may flatten the nesting.
	std::vector<Home> districts;
	// At 256 and above, put a home wholly inside its first-level district. Letting a
	// module straddle a third boundary reserved two or four districts and suppressed
	// every smaller lake in the first probe. Narrow maps instead use the seam-aware
	// general search: their first-level districts cannot contain a whole module.
	if (std::min(t.w, t.h) >= 256)
		for (int y = 0; y < 3; ++y)
			for (int x = 0; x < 3; ++x)
				if (x != 1 || y != 1)
					districts.push_back({(2 * x + 1) * t.w / 6, (2 * y + 1) * t.h / 6});
	if (!reserveHomes(L, context, districts))
		return L;
	const RegionTree tree = partitionRegions(
		root, 3, o.nesting, o.minimumSide,
		[&](const RecursiveRegion &region)
		{
			if (region.depth == 0)
				return RegionStop::None;
			const auto &parent = region.bounds;
			// The middle child is the parent's lake, not another land district. Its whole
			// region remains in the hierarchy but never receives another recursive lake.
			const int cx = (parent.x0 + parent.x1) / 2, cy = (parent.y0 + parent.y1) / 2;
			if ((region.id - 1) % 9 == 4 || L.terrain[t.at(cx, cy)] == WATER)
				return RegionStop::Caller;
			if (overlapsHome(L, parent))
				return RegionStop::Home;
			// A small seeded fraction of otherwise eligible districts stays as expansion
			// meadow. The stable region identity drives this choice, independently of teams.
			if (region.depth > 1 &&
				GenerationContext::deriveSeed(r.seed, "garden-stop-" + std::to_string(region.id)) %
						5 ==
					0)
				return RegionStop::Caller;
			return RegionStop::None;
		});
	if (!tree.failure.empty())
	{
		L.failure = tree.failure;
		return L;
	}
	std::vector<RegionBounds> smallerLakes;
	int lakes = 1, homeStops = 0, sizeStops = 0, actualNesting = 1;
	for (const auto &region : tree.regions)
	{
		homeStops += region.stop == RegionStop::Home;
		sizeStops += region.stop == RegionStop::Size;
		if (region.depth == 0 || region.terminal())
			continue;
		const auto opening = lakeIn(region.bounds, o.lakePercent);
		// Enlarged lake corners must leave at least a beach and a broad walking rim in
		// their own district. Reject this geometry instead of reducing the requested lake.
		if (std::min(opening.x0 - region.bounds.x0, opening.y0 - region.bounds.y0) < 8 ||
			overlapsHome(L, opening, 3))
		{
			L.failure = "Nested lake leaves insufficient land rim; reduce lake size or nesting.";
			return L;
		}
		fillRectangle(L.terrain, t, opening, WATER);
		++lakes;
		smallerLakes.push_back(opening);
		actualNesting = std::max(actualNesting, region.depth + 1);
	}
	// The orchard island is large enough for several building courts at 256. On a 128
	// map it is an orchard objective, not an extra promised home. The outer land remains
	// a continuous toroidal route whatever causeway count the player chooses.
	const int cx = (lake.x0 + lake.x1) / 2, cy = (lake.y0 + lake.y1) / 2;
	const int ix = std::max(8, lake.width() / 4), iy = std::max(8, lake.height() / 4);
	fillRectangle(L.terrain, t, {cx - ix, cy - iy, cx + ix + 1, cy + iy + 1}, GRASS);
	fillRectangle(L.objectives, t, {cx - ix + 3, cy - iy + 3, cx + ix - 2, cy + iy - 2}, 1);
	// Finish home irrigation before measuring bridge travel. Protect its actual
	// water, crop corners, and building courts; empty external lanes may still meet
	// a causeway. Protecting an entire bounding square would reject safe narrow-map
	// approaches, while stamping farms afterwards could erase the selected road.
	layHomeEconomies(L);
	auto protectedPlots = L.wheat;
	for (int i = 0; i < t.size(); ++i)
		protectedPlots[i] |= L.wood[i];
	auto crossingExclusion = tileCorners(t, protectedPlots);
	const auto serviceCorners = tileCorners(t, L.growthRestricted);
	for (int i = 0; i < t.size(); ++i)
		crossingExclusion[i] |= serviceCorners[i];
	for (int i = 0; i < t.size(); ++i)
		crossingExclusion[i] |= L.reserved[i] && L.terrain[i] == WATER;
	for (Home h : L.homes)
		fillRectangle(crossingExclusion, t, {h.x - 12, h.y - 10, h.x + 13, h.y + 15});
	// Each candidate is a complete opposing pair of causeways, represented as one path
	// across the island. Selecting halves independently could leave all approaches on
	// one shore.
	//
	// Every causeway is square to the lake: horizontal or vertical, never at an angle. This is
	// a formal garden, and a causeway struck across a square lake at sixty degrees reads as a
	// diagram of access rather than a garden path (2026-09-16). A pair is the causeway and its
	// mirror image across the lake along its own axis, so a pair off the centre line is two
	// parallel causeways rather than one diagonal. The centre lines come first; the parallels,
	// still landing on the orchard island, are the third pair and the fallback when a home's
	// ground blocks a centre line.
	std::vector<CrossingCandidate> candidates;
	int blockedApproaches = 0;
	// How each candidate's far half mirrors its near one, recorded when it is proposed rather
	// than guessed from geometry afterwards: a shallow angled causeway can sit within a
	// parallel's offset of the centre line. Square ones mirror along their own axis, angled
	// fallbacks through the centre, as the original design did.
	enum class Mirror { Horizontal, Vertical, Centre };
	std::vector<Mirror> mirrors;
	const auto mirrorPoint = [&](ShapePoint p, Mirror m)
	{
		switch (m)
		{
		case Mirror::Horizontal:
			return ShapePoint{2.0 * cx - p.x, p.y};
		case Mirror::Vertical:
			return ShapePoint{p.x, 2.0 * cy - p.y};
		default:
			return ShapePoint{2.0 * cx - p.x, 2.0 * cy - p.y};
		}
	};
	const auto mirror = [&](const CrossingCandidate &c)
	{ return mirrorPoint(c.from, mirrors[size_t(c.id)]); };
	const auto propose = [&](int k, bool horizontal, double offset)
	{
		// Scale the APPROACH positions with the rectangle, not the causeway width.
		// Unscaled angles bunched every approach onto the short bank on 128×512,
		// making the second pair useless once seam routes were included.
		const ShapePoint bank = horizontal ? ShapePoint{lake.x0 - 5.0, cy + offset}
										   : ShapePoint{cx + offset, lake.y0 - 5.0};
		const ShapePoint opposite = mirrorPoint(bank, horizontal ? Mirror::Horizontal : Mirror::Vertical);
		// Test the full opposing stroke against the protected economic footprint.
		// The half represented in the graph alone would miss damage on the far bank
		// (the original 128x256, seed 20001 regression).
		if (strokeIntersectsMask(t, {{bank.x, bank.y, 3.5}, {opposite.x, opposite.y, 3.5}},
								 crossingExclusion))
		{
			++blockedApproaches;
			return;
		}
		const ShapePoint landing = horizontal ? ShapePoint{double(cx), cy + offset}
											  : ShapePoint{cx + offset, double(cy)};
		// Ids index `mirrors`, so every proposal records one whether or not it is kept.
		if (mirrors.size() <= size_t(k))
			mirrors.resize(size_t(k) + 1, Mirror::Centre);
		mirrors[size_t(k)] = horizontal ? Mirror::Horizontal : Mirror::Vertical;
		candidates.push_back({k,
							  0,
							  0,
							  0,
							  0,
							  int(horizontal ? lake.width() / 2.0 + 5 : lake.height() / 2.0 + 5),
							  bank,
							  landing});
	};
	// Centre lines, then parallels that still land on the island. The seeded choice of which
	// axis leads is the variety: the drawing stays square either way.
	const bool horizontalFirst = context.bounded("garden-approaches", 2) == 0;
	propose(0, horizontalFirst, 0);
	propose(1, !horizontalFirst, 0);
	int k = 2;
	for (const double fraction : {0.55, 0.3, 0.8})
		for (const bool horizontal : {horizontalFirst, !horizontalFirst})
			for (const double sign : {1.0, -1.0})
			{
				const double half = horizontal ? iy : ix;
				propose(k++, horizontal, sign * fraction * half);
			}
	// Square is the rule, not a precondition. On a 128 map a home's court can block a whole
	// axis — its centre line and every parallel share the same reach — and refusing the map
	// there would trade the smallest supported size for a drawing. Only then, and only for the
	// missing axis, the causeway may leave the square: the angled search the map used before.
	// It is the one place a diagonal survives, and telemetry records every time it is used.
	int squareAxes = 0;
	for (const Mirror axis : {Mirror::Horizontal, Mirror::Vertical})
		for (const auto &c : candidates)
			if (mirrors[size_t(c.id)] == axis)
			{
				++squareAxes;
				break;
			}
	const bool angledFallback = squareAxes < std::min(o.pairs, 2);
	context.telemetry.measure("sierpinski.crossings.square-axes", squareAxes);
	context.telemetry.measure("sierpinski.crossings.angled-fallback", angledFallback);
	if (angledFallback)
		for (int step = 1; step < 36; ++step)
			if (step % 9 != 0)
			{
				const double angle = step * kPi / 36;
				const double dx = std::cos(angle) * lake.width(), dy = std::sin(angle) * lake.height();
				const double reach =
					std::min((lake.width() / 2.0 + 5) / std::max(0.001, std::abs(dx)),
							 (lake.height() / 2.0 + 5) / std::max(0.001, std::abs(dy)));
				const ShapePoint bank{cx + dx * reach, cy + dy * reach};
				const ShapePoint opposite{2 * cx - bank.x, 2 * cy - bank.y};
				if (strokeIntersectsMask(t, {{bank.x, bank.y, 3.5}, {opposite.x, opposite.y, 3.5}},
										 crossingExclusion))
				{
					++blockedApproaches;
					continue;
				}
				if (mirrors.size() <= size_t(k))
					mirrors.resize(size_t(k) + 1, Mirror::Centre);
				mirrors[size_t(k)] = Mirror::Centre;
				candidates.push_back({k++, 0, 0, 0, 0,
									  int(reach * std::max(std::abs(dx), std::abs(dy))), bank,
									  {double(cx), double(cy)}});
			}
	context.telemetry.measure("sierpinski.crossings.blocked-approaches", blockedApproaches);
	context.telemetry.measure("sierpinski.crossings.legal-candidates", candidates.size());
	// Garden pools in the meadows the recursion left empty. They go in before the causeway
	// graph is measured, so a pool can never appear under a selected approach.
	gardenBeds(L, context, 30, 13, 4);
	// A provisional shoreline for the causeway graph below, which measures walking over the
	// preliminary land mask. The real one is laid at the end, once the bank plots are down.
	layBeaches(L.terrain, t);
	// Graph travel uses the preliminary land mask. Finished-world validators later
	// account for deposits, settlements, and actual engine movement predicates.
	auto passable = pureTiles(L.terrain, t, WATER);
	for (auto &tile : passable)
		tile = !tile;
	const auto graph = crossingEndpointGraph(t, passable, candidates);
	if (!graph.failure.empty())
	{
		L.failure = graph.failure;
		return L;
	}
	auto edges = graph.edges;
	std::vector<int> required;
	for (int i = 0; i < int(candidates.size()) * 2; ++i)
		required.push_back(i);
	// Include the orchard island in the required graph. Comparing only outer-bank
	// detours incorrectly called island approaches useless on narrow rectangular tori:
	// the seam is a short route between banks, but it cannot reach the island at all.
	// The first approach is mandatory connectivity; subsequent approaches are ranked
	// by access benefit. The opposite half of each pair is a fixed tactical counterpart
	// and is not credited in this coarse graph estimate.
	// Pair midpoints deliberately meet on the same court, so separation is zero here;
	// approach separation is enforced on both banks by the compatibility predicate.
	auto selected = selectCrossings(
		t, int(candidates.size()) * 2, edges, required, candidates, 0, o.pairs - 1, 0, r.seed,
		[&](const CrossingCandidate &a, const CrossingCandidate &b)
		{
			// A candidate represents BOTH banks. Keep at least
			// 10 tiles between their approach centres: seven
			// corners of road plus a visible shoulder gap.
			for (const ShapePoint pa : {a.from, mirror(a)})
				for (const ShapePoint pb : {b.from, mirror(b)})
					if (t.dist2(int(std::floor(pa.x)), int(std::floor(pa.y)), int(std::floor(pb.x)),
								int(std::floor(pb.y))) < 100)
						return false;
			return true;
		});
	if (int(selected.selected.size()) != o.pairs)
	{
		L.failure = "The requested causeway pairs do not provide distinct useful approaches; "
					"enlarge the central lake.";
		return L;
	}
	for (auto &crossing : selected.selected)
		crossing.to = mirror(crossing);
	stampCrossings(L, selected, context);
	int farms = 0;
	for (size_t k = 0; k < smallerLakes.size(); ++k)
	{
		const auto b = smallerLakes[k];
		const int y = (b.y0 + b.y1) / 2;
		// Banks alternate food and timber by seeded lake role; unsuccessful plots are
		// genuine omissions, never an excuse to fill part of the recursive lake.
		// Three banks of every lake grow wheat and exactly one grows timber, the seed choosing
		// which: a garden lake is somewhere a colony feeds itself, with one stand of wood
		// beside it. Alternating the kinds round the lake gave two of each (2026-09-16).
		const int timberSide =
			int(GenerationContext::deriveSeed(r.seed, "garden-farm-" + std::to_string(k)) % 4);
		const int x = (b.x0 + b.x1) / 2;
		// All four banks, not just the two sides: a garden lake with crops on one shore and
		// bare grass on the other three was most of what made this map read as empty. Each box
		// runs up to the lake's own edge, so the plot shares the shore's sand and its crops
		// stand in the water's growth range instead of a few tiles inland of it.
		farms += bankFarm(L, {b.x0 - 15, y - 12, b.x0 + 1, y + 12}, timberSide == 0);
		farms += bankFarm(L, {b.x1 - 1, y - 12, b.x1 + 15, y + 12}, timberSide == 1);
		farms += bankFarm(L, {x - 12, b.y0 - 15, x + 12, b.y0 + 1}, timberSide == 2);
		farms += bankFarm(L, {x - 12, b.y1 - 1, x + 12, b.y1 + 15}, timberSide == 3);
	}
	// The lakes are features too, so paths lead to their shores as well as to the plots.
	L.features.push_back({lake.x0 - 2, lake.y0 - 2, lake.x1 + 2, lake.y1 + 2});
	for (const auto &b : smallerLakes)
		L.features.push_back({b.x0 - 2, b.y0 - 2, b.x1 + 2, b.y1 + 2});
	gardenPaths(L, context);
	// Every plot and path that just went in may meet a lake: give the whole map its shoreline
	// again now that nothing further will cut terrain. Grass may never touch water.
	layBeaches(L.terrain, t);
	context.telemetry.measure("sierpinski.bank-farms.proposed", smallerLakes.size() * 4);
	context.telemetry.measure("sierpinski.bank-farms.placed", farms);
	context.telemetry.measure("sierpinski.depth.requested", o.nesting);
	context.telemetry.measure("sierpinski.depth.achieved", actualNesting);
	context.telemetry.measure("sierpinski.regions.home-stops", homeStops);
	context.telemetry.measure("sierpinski.regions.size-stops", sizeStops);
	context.telemetry.measure("sierpinski.lakes", lakes);
	context.telemetry.measure("sierpinski.crossing-pairs.requested", o.pairs);
	return L;
}
bool generate(Game &game, GenerationContext &context)
{
	context.stage = "Sierpiński Gardens design";
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	return furnishAndSettle(game, context, L);
}
std::string validateRequest(const GenerationRequest &r)
{
	GenerationContext context(r);
	return design(r, context).failure;
}
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	return validate(game, context, design(context.request, replay));
}
} // namespace
GeneratorDefinition sierpinskiGardensDefinition()
{
	std::vector<GeneratorControl> controls{
		{"maximum-nesting", "Maximum nesting", 1, 4, 1, 3, ControlGroup::Layout},
		{"minimum-district-side", "Minimum terminal district side", 24, 48, 4, 24,
		 ControlGroup::Layout},
		{"lake-size", "Lake size", 75, 125, 5, 100, ControlGroup::Terrain},
		{"major-crossing-pairs", "Major crossing pairs", 1, 3, 1, 2, ControlGroup::Layout}};
	const auto resources = resourceControls();
	controls.insert(controls.end(), resources.begin(), resources.end());
	return {"sierpinski-gardens",
			49,
			"Sierpiński Gardens",
			5,
			false,
			controls,
			generate,
			true,
			validateRequest,
			validateWorld};
}
