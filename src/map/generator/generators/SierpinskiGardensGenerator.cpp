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
	// one shore. Three different diameters give 2, 4 or 6 distinct island approaches.
	std::vector<CrossingCandidate> candidates;
	const double shift = (context.bounded("garden-approaches", 3) - 1.0) * 0.12;
	int blockedApproaches = 0;
	const auto propose = [&](int k, double angle)
	{
		// Scale the APPROACH positions with the rectangle, not the causeway width.
		// Unscaled angles bunched every approach onto the short bank on 128×512,
		// making the second pair useless once seam routes were included.
		const double dx = std::cos(angle) * lake.width(), dy = std::sin(angle) * lake.height();
		const double reach = std::min((lake.width() / 2.0 + 5) / std::max(0.001, std::abs(dx)),
									  (lake.height() / 2.0 + 5) / std::max(0.001, std::abs(dy)));
		const ShapePoint bank{cx + dx * reach, cy + dy * reach};
		const ShapePoint opposite{2 * cx - bank.x, 2 * cy - bank.y};
		// Test the full opposing stroke against the protected economic footprint.
		// The half represented in the graph alone would miss damage on the far bank
		// (the original 128x256, seed 20001 regression).
		if (strokeIntersectsMask(t, {{bank.x, bank.y, 3.5}, {opposite.x, opposite.y, 3.5}},
								 crossingExclusion))
		{
			++blockedApproaches;
			return;
		}
		candidates.push_back({k,
							  0,
							  0,
							  0,
							  0,
							  int(reach * std::max(std::abs(dx), std::abs(dy))),
							  {cx + dx * reach, cy + dy * reach},
							  {double(cx), double(cy)}});
	};
	// Preserve the established three approaches when they fit. Only a constrained
	// shore needs the bounded five-degree search; no seed retry or home cutting.
	for (int k = 0; k < 3; ++k)
		propose(k, k * kPi / 3 + shift);
	if (int(candidates.size()) < o.pairs)
		for (int k = 0; k < 36; ++k)
			if (k % 12 != 0)
				propose(3 + k, k * kPi / 36 + shift);
	context.telemetry.measure("sierpinski.crossings.blocked-approaches", blockedApproaches);
	context.telemetry.measure("sierpinski.crossings.legal-candidates", candidates.size());
	// Garden pools in the meadows the recursion left empty. They go in before the causeway
	// graph is measured, so a pool can never appear under a selected approach.
	gardenBeds(L, context, 30, 13, 4);
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
			for (int signA : {-1, 1})
				for (int signB : {-1, 1})
					if (t.dist2(int(std::floor(cx + signA * (a.from.x - cx))),
								int(std::floor(cy + signA * (a.from.y - cy))),
								int(std::floor(cx + signB * (b.from.x - cx))),
								int(std::floor(cy + signB * (b.from.y - cy)))) < 100)
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
		crossing.to = {2 * cx - crossing.from.x, 2 * cy - crossing.from.y};
	stampCrossings(L, selected, context);
	int farms = 0;
	for (size_t k = 0; k < smallerLakes.size(); ++k)
	{
		const auto b = smallerLakes[k];
		const int y = (b.y0 + b.y1) / 2;
		// Banks alternate food and timber by seeded lake role; unsuccessful plots are
		// genuine omissions, never an excuse to fill part of the recursive lake.
		const bool timber =
			GenerationContext::deriveSeed(r.seed, "garden-farm-" + std::to_string(k)) % 3 == 0;
		const int x = (b.x0 + b.x1) / 2;
		// All four banks, not just the two sides: a garden lake with crops on one shore and
		// bare grass on the other three was most of what made this map read as empty.
		farms += bankFarm(L, {b.x0 - 18, y - 12, b.x0 - 5, y + 12}, timber);
		farms += bankFarm(L, {b.x1 + 5, y - 12, b.x1 + 18, y + 12}, !timber);
		farms += bankFarm(L, {x - 12, b.y0 - 18, x + 12, b.y0 - 5}, !timber);
		farms += bankFarm(L, {x - 12, b.y1 + 5, x + 12, b.y1 + 18}, timber);
	}
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
			2,
			false,
			controls,
			generate,
			true,
			validateRequest,
			validateWorld};
}
