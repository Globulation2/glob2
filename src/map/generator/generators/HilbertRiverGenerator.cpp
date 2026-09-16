// SPDX-License-Identifier: GPL-3.0-or-later
#include "HilbertRiverGenerator.h"
#include "FractalMapSupport.h"
#include "Drawing.h"
#include "Game.h"
#include <algorithm>
#include <cmath>
using namespace FractalMaps;
namespace
{
struct RiverOptions
{
	int depth, width, spacing, local, major;
	explicit RiverOptions(const GenerationRequest &r)
		: depth(r.option("maximum-fold-depth")), width(r.option("river-width")),
		  spacing(r.option("minimum-land-spacing")), local(r.option("local-crossings")),
		  major(r.option("major-shortcuts"))
	{
	}
};
Layout design(const GenerationRequest &r, GenerationContext &context)
{
	const RiverOptions o(r);
	Layout L;
	initialize(L, r);
	if (!L.failure.empty())
		return L;
	const auto &t = L.t;
	// Only legal global Hilbert orientations are varied. Recursive entry/exit orientations
	// come from the shared decoder; independently rotating children would break the river.
	const int orientation = context.bounded("hilbert-orientation", 8);
	HilbertPath path;
	int homeLimited = 0;
	for (int maximum = o.depth; maximum >= 1; --maximum)
	{
		path = hilbertPath({0, 0, t.w, t.h}, maximum, o.width + o.spacing, orientation);
		if (!path.failure.empty())
		{
			L.failure = path.failure;
			return L;
		}
		L.terrain.assign(t.size(), GRASS);
		std::vector<StrokePoint> stroke;
		for (const auto &p : path.points)
			stroke.push_back({p.x, p.y, o.width / 2.0});
		strokePath(L.terrain, t, stroke, WATER);
		// The river stops at the first/last cell centre. Leaving those ends inside the map
		// preserves a deliberate long walking alternative and useful swimming shortcuts.
		// If homes cannot fit, lower the UNIFORM order, not selected pieces of the river.
		// This preserves recognizable self-similarity and every entry/exit relationship.
		GenerationContext homes(r);
		if (reserveHomes(L, homes, {}, &context.telemetry))
		{
			L.failure.clear();
			break;
		}
		maximum = path.actualOrder;
		++homeLimited;
		if (maximum == 1)
			return L;
	}
	// The river is the shore that gets wheat spots when the resources go down.
	{
		std::vector<StrokePoint> stroke;
		for (const auto &p : path.points)
			stroke.push_back({p.x, p.y, o.width / 2.0});
		strokePath(L.wheatShore, t, stroke);
	}
	// Include home irrigation in the graph's real end-around and seam distances.
	layHomeEconomies(L);
	std::vector<CrossingCandidate> candidates;
	const double reach = o.width / 2.0 + 5;
	const auto propose = [&](const std::vector<double> &fractions)
	{
		const auto samples = transverseCrossings(path.segments, fractions, reach, reach + 4);
		if (!samples.failure.empty())
		{
			L.failure = samples.failure;
			return;
		}
		for (const auto &c : samples.candidates)
		{
			const double mx = (c.from.x + c.to.x) / 2, my = (c.from.y + c.to.y) / 2;
			// Reserve a court wider than the bridge, including its beach shoulders.
			// The shared sampler keeps the full court clear of rounded river bends.
			if (!overlapsHome(L, {int(mx - reach - 4), int(my - reach - 4), int(mx + reach + 5),
								  int(my + reach + 5)}))
				candidates.push_back(c);
		}
	};
	propose({0.5});
	const bool extraSamples = candidates.empty();
	// A first-order U has just three midpoints. Homes can occupy all three while
	// long stretches of bank remain empty (seed 20002, depth 1). Try fixed interior
	// samples only in that case; never relocate homes or erase a fold to force a road.
	// Slot zero remains the midpoint, preserving stable IDs across both passes.
	if (extraSamples)
		propose({0.5, 0.25, 0.75, 0.125, 0.375, 0.625, 0.875});
	context.telemetry.measure("hilbert.crossings.extra-samples", extraSamples);
	context.telemetry.measure("hilbert.crossings.legal-candidates", candidates.size());
	if (!L.failure.empty())
		return L;
	if (candidates.empty())
	{
		L.failure = "No crossing court fits outside the reserved homes.";
		return L;
	}
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
	// On a torus an open-ended river need not disconnect land. "Mandatory" here means
	// one guaranteed direct bank connection, even with both optional sliders at zero.
	// Pick the greatest real end-around detour reduction, then include it in the graph
	// before buying optional bridges; their benefit must be incremental, not double-counted.
	auto mandatoryCandidates = candidates;
	for (auto &c : mandatoryCandidates)
		c.level = 0;
	auto mandatory = selectCrossings(t, int(candidates.size()) * 2, edges, {}, mandatoryCandidates,
									 0, 1, 12, r.seed);
	if (mandatory.selected.empty())
	{
		L.failure = "No useful mandatory river crossing fits.";
		return L;
	}
	mandatory.mandatory = 1;
	mandatory.major = 0;
	const auto bridge = mandatory.selected.front();
	edges.push_back({bridge.fromRegion, bridge.toRegion, bridge.length});
	std::vector<CrossingCandidate> optional;
	for (const auto &c : candidates)
		if (c.id != bridge.id && t.dist2(int((c.from.x + c.to.x) / 2), int((c.from.y + c.to.y) / 2),
										 int((bridge.from.x + bridge.to.x) / 2),
										 int((bridge.from.y + bridge.to.y) / 2)) >= 144)
			optional.push_back(c);
	auto selected = selectCrossings(t, int(candidates.size()) * 2, edges, {}, optional, o.local,
									o.major, 12, r.seed);
	if (!selected.failure.empty())
	{
		L.failure = selected.failure;
		return L;
	}
	stampCrossings(L, mandatory, context);
	stampCrossings(L, selected, context);
	// Fruit courts flank the selected regional crossings. Their gathering lanes and the
	// permanently sandy crossings stay open even when the fruit/stone controls are high.
	for (const auto &c : selected.selected)
		if (c.level == 0)
			for (ShapePoint p : {c.from, c.to})
				fillRectangle(L.objectives, t,
							  {int(p.x) - 10, int(p.y) - 10, int(p.x) + 11, int(p.y) + 11}, 1);
	for (ShapePoint p : {bridge.from, bridge.to})
		fillRectangle(L.objectives, t, {int(p.x) - 10, int(p.y) - 10, int(p.x) + 11, int(p.y) + 11},
					  1);
	// Pools in the pockets between the folds, before the bank plots, so the river's own
	// banks keep first claim on the ground beside them.
	gardenBeds(L, context, (o.width + o.spacing) / 2, (o.spacing - 8) / 2, 4);
	int farms = 0;
	for (const auto &segment : path.segments)
	{
		const double dx = segment.to.x - segment.from.x, dy = segment.to.y - segment.from.y;
		const double length = std::hypot(dx, dy);
		// One plot on each bank of every segment, at its midpoint, sliding along the bank
		// when the first position is taken. Plot orientation follows the bank, so
		// rectangular scaling never widens the river.
		for (int side : {-1, 1})
			for (const double along : {0.5})
			{
				// Offset by half the river plus half the plot: the plot's inner edge lands on
				// the bank itself, so its crops share the river's sand and stand in its
				// growth range. Laid a dozen tiles inland they regrew too slowly to be worth
				// the walk (2026-09-16).
				const int x =
					int(segment.from.x + dx * along - side * dy / length * (o.width / 2 + 8));
				const int y =
					int(segment.from.y + dy * along + side * dx / length * (o.width / 2 + 8));
				// Square-ish and large: a plot's sand rim is fixed by its perimeter, so a few
				// big plots cost far less sand per crop tile than many small ones. At four
				// narrow plots a segment the rims alone covered a tenth of the map.
				const int hx = dx == 0 ? 8 : 11, hy = dx == 0 ? 11 : 8;
				// One bank in four carries timber, the rest food: the ambient copses are the
				// map's wood, and a river bank is worth more as somewhere a colony can feed
				// itself.
				farms += bankFarm(L, {x - hx, y - hy, x + hx, y + hy},
								  (segment.id + side + int(along * 4)) % 4 == 0);
			}
	}
	gardenPaths(L, context);
	// The beds, plots and paths just laid may meet the river: give the whole map its
	// shoreline again now that nothing further will cut terrain. Grass may never touch water.
	layBeaches(L.terrain, t);
	context.telemetry.measure("hilbert.bank-farms.proposed", path.segments.size() * 2);
	context.telemetry.measure("hilbert.bank-farms.placed", farms);
	context.telemetry.measure("hilbert.depth.requested", o.depth);
	context.telemetry.measure("hilbert.depth.achieved", path.actualOrder);
	context.telemetry.measure("hilbert.depth.home-reductions", homeLimited);
	context.telemetry.measure("hilbert.depth.spacing-limited", path.stop == RegionStop::Spacing);
	context.telemetry.measure("hilbert.orientation", orientation);
	context.telemetry.measure("hilbert.crossings.local-per-parent-requested", o.local);
	context.telemetry.measure("hilbert.crossings.major-requested", o.major);
	return L;
}
bool generate(Game &game, GenerationContext &context)
{
	context.stage = "Hilbert River design";
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
GeneratorDefinition hilbertRiverDefinition()
{
	std::vector<GeneratorControl> controls{
		{"maximum-fold-depth", "Maximum fold depth", 1, 4, 1, 3, ControlGroup::Layout},
		{"river-width", "River width", 4, 10, 1, 6, ControlGroup::Terrain},
		{"minimum-land-spacing", "Minimum land spacing", 24, 40, 2, 28, ControlGroup::Terrain},
		{"local-crossings", "Optional local crossings per parent", 0, 2, 1, 1,
		 ControlGroup::Layout},
		{"major-shortcuts", "Optional major shortcuts", 0, 4, 1, 2, ControlGroup::Layout}};
	const auto resources = resourceControls();
	controls.insert(controls.end(), resources.begin(), resources.end());
	return {"hilbert-river", 50,           "Hilbert River", 5, false, controls, generate, true,
			validateRequest, validateWorld};
}
