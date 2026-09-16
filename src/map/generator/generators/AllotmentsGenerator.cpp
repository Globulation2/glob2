// SPDX-License-Identifier: GPL-3.0-or-later
#include "AllotmentsGenerator.h"
#include "Contact.h"
#include "Drawing.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "LatticeNoise.h"
#include "Orbits.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Room.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Tessellation.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <cstdio>
using namespace MapGeneration;

// Allotments: villages among allotment gardens.
//
// WHAT IT LOOKS LIKE. Allotment gardens on the edge of town seen from the air: parcels of land cut
// by lanes, most of them divided into rows of narrow plots side by side, each tended differently -
// grain, fruit bushes, a thicket someone let go, a bare plot with a shed at the end of it - with a
// water ditch running across the rows where the gardeners fill their cans. Between the garden sites
// lie commons (meadow round a pond) and the odd woodlot. Every colony's village is an open green
// behind its own allotment block. (The first version tiled one square lot with a sand ring over the
// whole map: it read as graph paper and left room for 316 buildings on a whole 256 map.)
//
// HOW IT PLAYS. A plot is a strip of grass between two sand paths, so its crops never spread past
// it and every plot is worked from both long sides: a lot of frontage in a little ground, which is
// what the AIs harvest well. A village has a town's worth of open building ground and a block of
// plots fed by its own ditch. The garden sites between villages are the food the colonies grow
// into and fight over; the commons are where a colony builds out; the woodlots are its timber; the
// sheds on bare plots are small stones for its first upgrades. The lanes between parcels are sand,
// so every parcel stays reachable whatever grows.
//
// FAIRNESS. Villages stand on a lattice and are one stencil stamped by quarter turns, so every
// village, allotment block, ditch and planting pattern is identical. The parcels between them are a
// warped tiling whose kinds and plot mixes are drawn, which the lobby's best-of-five start scoring
// evens out.
namespace
{
// The village, in its own frame: a square of half-size kVillageHalf ringed by a lane, its front a
// green (kTownFront onwards) and its back an allotment block: plots `strip-width` wide across the
// frame, either side of a ditch kDitchCorners wide down the block's middle.
constexpr int kVillageHalf = 24, kTownFront = -5, kDitchCorners = 4, kVillageStrip = 9;
constexpr double kSwarmForward = 8;
// The village's plots, repeating down each side of the ditch: W wheat, D wood, F fruit, B bare.
// Village plots are kVillageStrip tiles wide whatever the Plot width control says: Numbi breeds
// only while the wheat block nearest its swarm is about three tiles per colonist (estimateFood
// measures one contiguous rectangle), and plots four wide held its villages at 13 colonists in a
// 45,000-tick tournament (2026-09-16).
constexpr const char *kVillagePattern = "WWDWWFWDW";
// Parcels: the tiling's cells. A site's plots are `strip-width` tiles wide (varied a tile either
// way per site), in bands of two rows of plots kLeastPlot to kMostPlot long either side of a ditch.
constexpr int kLeastPlot = 5, kMostPlot = 8;
// Parcel kinds by the Commons control: commons percent; woodlots take kWoodlotPercent; the rest
// are garden sites. Parcels with less interior than kLeastParcel are always commons.
constexpr int kCommonsPercent[3] = {12, 25, 40};
constexpr int kWoodlotPercent = 15, kLeastParcel = 150;
// Plot styles by the Plot mix control (Tended, Mixed, Overgrown): percent wheat, wood, fruit; the
// rest are bare, and a bare plot has a shed (a stone) with kShedPercent probability.
constexpr int kStyleShares[3][3] = {{58, 14, 8}, {46, 24, 7}, {30, 44, 5}};
constexpr int kShedPercent = 45;
// Cover: a wheat or wood plot is planted over this share of its ground; a fruit plot holds this
// many bushes; a woodlot is wooded over this share of its best (noisiest) ground.
constexpr int kPlotCoverPercent = 75, kFruitTiles = 6, kWoodlotCoverPercent = 60;
// A commons: a pond of radius kCommonsPond to kCommonsPond + 2, and a fruit clump or a stone clump.
constexpr double kCommonsPond = 2.5;

enum Kind : signed char
{
	kNone,
	kTown,
	kPlot,
	kCommons,
	kWoodlot
};
enum Style : signed char
{
	kWheat,
	kWood,
	kFruit,
	kBare
};

struct VillageStencil
{
	int extent = kVillageHalf + 1;
	std::vector<unsigned char> vertex;
	std::vector<signed char> kind;
	std::vector<int> plot;              // plot index in `styles`, or -1
	std::vector<signed char> styles;    // per plot
	int townSites = 0;
	int side() const { return 2 * extent + 1; }
	int index(int dx, int dy) const { return (dy + extent) * side() + dx + extent; }
};

VillageStencil buildVillage(int strip)
{
	VillageStencil s;
	const int side = s.side(), H = kVillageHalf;
	s.vertex.assign(size_t(side) * side, GRASS);
	s.kind.assign(size_t(side) * side, kNone);
	s.plot.assign(size_t(side) * side, -1);
	const int pitch = strip + 1;
	const int ditch = (-H + kTownFront) / 2 - kDitchCorners / 2;
	for (int dy = -s.extent; dy <= s.extent; ++dy)
		for (int dx = -s.extent; dx <= s.extent; ++dx)
		{
			const int at = s.index(dx, dy), ring = std::max(std::abs(dx), std::abs(dy));
			unsigned char v = GRASS;
			if (ring >= H)
				v = SAND;
			else if (dx == kTownFront)
				v = SAND;
			else if (dx < kTownFront)
			{
				if (dx >= ditch && dx < ditch + kDitchCorners)
					v = std::abs(dy) < H - 1 ? WATER : SAND;
				else if ((dy + H) % pitch == 0)
					v = SAND;
			}
			s.vertex[at] = v;
			// The tile whose top-left corner this is.
			if (ring >= H)
				continue;
			if (dx >= kTownFront)
				s.kind[at] = kTown;
			else if (dx < ditch - 1 || dx >= ditch + kDitchCorners)
			{
				s.kind[at] = kPlot;
				const int band = (dy + H) / pitch, west = dx < ditch;
				s.plot[at] = band * 2 + west;
			}
		}
	const int plots = 2 * ((2 * H) / pitch + 1);
	for (int p = 0; p < plots; ++p)
	{
		const char c = kVillagePattern[(p / 2) % 9];
		s.styles.push_back(c == 'W' ? kWheat : c == 'D' ? kWood : c == 'F' ? kFruit : kBare);
	}
	// The green's 4x4 build sites, all four corners of every tile grass.
	const auto pureTown = [&](int dx, int dy)
	{
		return s.kind[s.index(dx, dy)] == kTown && s.vertex[s.index(dx, dy)] == GRASS &&
			   s.vertex[s.index(dx + 1, dy)] == GRASS && s.vertex[s.index(dx, dy + 1)] == GRASS &&
			   s.vertex[s.index(dx + 1, dy + 1)] == GRASS;
	};
	for (int dy = -H; dy + 4 < H; ++dy)
		for (int dx = kTownFront; dx + 4 < H; ++dx)
		{
			bool fits = true;
			for (int y = 0; y < 4 && fits; ++y)
				for (int x = 0; x < 4 && fits; ++x)
					fits = pureTown(dx + x, dy + y);
			s.townSites += fits;
		}
	return s;
}

struct Parcel
{
	signed char kind;
	int orientation, strip, length;
};

struct Layout
{
	Torus t{1, 1};
	VillageStencil village;
	std::vector<ShapePoint> homes;
	std::vector<int> facings;
	Tessellation parcels;
	std::vector<Parcel> kinds;
	TerrainSketch sketch;
	std::vector<signed char> kind;
	std::vector<int> homeOf;   // colony of every village tile, else -1
	std::vector<int> cellOf;   // parcel of every tile outside the villages, else -1
	std::vector<int> plotOf;   // global plot id of every plot tile, else -1
	std::vector<signed char> styles; // per global plot id
	std::vector<unsigned char> lanes;
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const AllotmentsOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);
	const int H = kVillageHalf;

	const int offsetX = int(context.bounded("allotments-layout", std::uint32_t(t.w)));
	const int offsetY = int(context.bounded("allotments-layout", std::uint32_t(t.h)));
	L.homes = latticeSites(t.w, t.h, teams, offsetX, offsetY).sites;
	for (ShapePoint &h : L.homes)
		h = {double(int(std::lround(h.x)) % t.w), double(int(std::lround(h.y)) % t.h)};
	dealStarts(context, L.homes);
	// One facing for every colony, drawn once per map. A home stencil turned by different quarter
	// turns covers identical tiles, but the AIs scan along the map's axes: with a facing per colony,
	// Numbi colonies on The Glacis grew to 60 in one facing and 20 to 30 in the others (rotation
	// tournaments, 2026-09-16). The same facing makes every home an exact translation of the others.
	L.facings.assign(teams, int(context.bounded("allotments-facing", 4)));
	if (nearestSiteDistance(t, L.homes) < 2 * H + 10 || std::min(t.w, t.h) < 2 * H + 10)
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	L.village = buildVillage(kVillageStrip);
	const VillageStencil &v = L.village;
	context.telemetry.measure("allotments.village.town-sites", v.townSites);

	L.sketch.assign(n, GRASS);
	L.kind.assign(n, kNone);
	L.homeOf.assign(n, -1);
	L.cellOf.assign(n, -1);
	L.plotOf.assign(n, -1);
	L.lanes.assign(n, 0);
	std::vector<unsigned char> villages(n, 0);
	for (int k = 0; k < teams; ++k)
	{
		const int cx = int(L.homes[k].x), cy = int(L.homes[k].y);
		for (int dy = -v.extent - 2; dy <= v.extent + 2; ++dy)
			for (int dx = -v.extent - 2; dx <= v.extent + 2; ++dx)
				villages[t.at(cx + dx, cy + dy)] = 1;
	}

	// The parcels: a warped square tiling, its edges sand lanes.
	L.parcels = squareTessellation(t.w, t.h, o.siteSize);
	Tessellation &g = L.parcels;
	const std::vector<unsigned char> allEdges(g.edges.size(), 1);
	warpCorners(g, warpLimit(g), allEdges, 8, o.siteSize / 4, context, "allotments-warp");
	const std::vector<int> labels = labelTiles(g);
	if (labels.empty())
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	const std::vector<unsigned char> lanes = rasterizeBoundaries(g, allEdges, 0);
	for (int i = 0; i < n; ++i)
		if (!villages[i])
		{
			L.cellOf[i] = labels[i];
			if (lanes[i])
			{
				L.lanes[i] = 1;
				L.sketch[i] = SAND;
			}
		}
	std::vector<int> interior(g.cellCount(), 0);
	for (int i = 0; i < n; ++i)
		if (L.cellOf[i] >= 0 && !L.lanes[i])
			++interior[L.cellOf[i]];
	const int commons = kCommonsPercent[std::clamp(o.commons, 0, 2)];
	std::array<int, 3> counts{};
	for (int c = 0; c < g.cellCount(); ++c)
	{
		Parcel parcel{kPlot, 0, o.stripWidth, kLeastPlot};
		const int roll = int(context.bounded("allotments-parcels", 100));
		parcel.orientation = int(context.bounded("allotments-parcels", 2));
		parcel.strip = std::max(2, o.stripWidth - 1 + int(context.bounded("allotments-parcels", 3)));
		parcel.length = kLeastPlot + int(context.bounded("allotments-parcels", kMostPlot - kLeastPlot + 1));
		if (interior[c] < kLeastParcel || roll < commons)
			parcel.kind = kCommons;
		else if (roll < commons + kWoodlotPercent)
			parcel.kind = kWoodlot;
		L.kinds.push_back(parcel);
		++counts[parcel.kind == kPlot ? 0 : parcel.kind == kCommons ? 1 : 2];
	}
	context.telemetry.measure("allotments.parcels.gardens", counts[0]);
	context.telemetry.measure("allotments.parcels.commons", counts[1]);
	context.telemetry.measure("allotments.parcels.woodlots", counts[2]);

	// Garden sites: bands of plots either side of a ditch, the plots divided by sand paths.
	// A corner takes its parcel's pattern when the tile it is the top-left corner of lies in the
	// parcel off the lanes (the lanes' own corners stay sand).
	const auto cornerInside = [&](int i, int c) { return L.cellOf[i] == c && !L.lanes[i]; };
	const auto styleOf = [&](int mix)
	{
		const int roll = int(context.bounded("allotments-plots", 100));
		const auto &share = kStyleShares[std::clamp(mix, 0, 2)];
		return static_cast<signed char>(roll < share[0]                         ? kWheat
										: roll < share[0] + share[1]            ? kWood
										: roll < share[0] + share[1] + share[2] ? kFruit
																				: kBare);
	};
	std::vector<std::vector<int>> cellTiles(g.cellCount());
	for (int i = 0; i < n; ++i)
		if (L.cellOf[i] >= 0)
			cellTiles[L.cellOf[i]].push_back(i);
	for (int c = 0; c < g.cellCount(); ++c)
	{
		const Parcel &parcel = L.kinds[c];
		if (parcel.kind != kPlot)
			continue;
		const int band = 2 * parcel.length + kDitchCorners + 2, pitch = parcel.strip + 1;
		const int cx = g.centreTileX(c), cy = g.centreTileY(c);
		const int bandOffset = int(context.bounded("allotments-plots", std::uint32_t(band)));
		const int stripOffset = int(context.bounded("allotments-plots", std::uint32_t(pitch)));
		for (int i : cellTiles[c])
		{
			if (!cornerInside(i, c))
				continue;
			const int dx = t.offsetX(cx, i % t.w), dy = t.offsetY(cy, i / t.w);
			const int u = (parcel.orientation ? dy : dx) + 1024 * band + bandOffset;
			const int w = (parcel.orientation ? dx : dy) + 1024 * pitch + stripOffset;
			// Along a band: a path, a row of plots, the ditch, a row of plots; across it, a path
			// between every two plots.
			const int inBand = u % band;
			if (inBand == 0)
				L.sketch[i] = SAND;
			else if (inBand > parcel.length && inBand <= parcel.length + kDitchCorners)
				L.sketch[i] = WATER;
			else if (w % pitch == 0)
				L.sketch[i] = SAND;
		}
	}
	// Commons: a pond in the middle of the parcel.
	for (int c = 0; c < g.cellCount(); ++c)
	{
		if (L.kinds[c].kind == kPlot)
			continue;
		for (int i : cellTiles[c])
			if (!L.lanes[i])
				L.kind[i] = L.kinds[c].kind;
		if (L.kinds[c].kind != kCommons)
			continue;
		const double radius = kCommonsPond + context.bounded("allotments-commons", 3);
		const RadialShape pond(radius, 0.25, context, "allotments-commons");
		const int cx = g.centreTileX(c), cy = g.centreTileY(c);
		forEachTileInShape(t, cx, cy, pond, 0,
						   [&](int i, double, double)
						   {
							   if (cornerInside(i, c) && L.cellOf[i] == c)
								   L.sketch[i] = WATER;
						   });
	}

	// The plots are what the terrain makes of the pattern: every eight-connected patch of pure grass
	// in a garden parcel, once the ditches' beaches are laid, is one plot with a style of its own.
	// (Deriving them from the pattern's arithmetic instead missed the patches a warped lane cuts
	// short, which then joined two plots or a plot to open ground.)
	{
		TerrainSketch beached = L.sketch;
		layBeaches(beached, t);
		const std::vector<unsigned char> pure = pureTiles(beached, t, GRASS);
		for (int c = 0; c < g.cellCount(); ++c)
		{
			if (L.kinds[c].kind != kPlot)
				continue;
			for (int start : cellTiles[c])
			{
				if (!pure[start] || L.lanes[start] || L.plotOf[start] >= 0)
					continue;
				const int id = int(L.styles.size());
				std::vector<int> patch{start};
				L.plotOf[start] = id;
				for (size_t q = 0; q < patch.size(); ++q)
				{
					const int x = patch[q] % t.w, y = patch[q] / t.w;
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							const int j = t.at(x + dx, y + dy);
							if (pure[j] && L.cellOf[j] == c && !L.lanes[j] && L.plotOf[j] < 0)
							{
								L.plotOf[j] = id;
								patch.push_back(j);
							}
						}
				}
				for (int j : patch)
					L.kind[j] = kPlot;
				// A sliver a lane cut off is left bare.
				L.styles.push_back(patch.size() < 6 ? static_cast<signed char>(kBare) : styleOf(o.plotMix));
			}
		}
	}

	// The villages, over whatever the parcels drew there.
	for (int k = 0; k < teams; ++k)
	{
		const int cx = int(L.homes[k].x), cy = int(L.homes[k].y);
		const int base = int(L.styles.size());
		L.styles.insert(L.styles.end(), v.styles.begin(), v.styles.end());
		for (int dy = -v.extent - 2; dy <= v.extent + 2; ++dy)
			for (int dx = -v.extent - 2; dx <= v.extent + 2; ++dx)
			{
				const int i = t.at(cx + dx, cy + dy);
				// A lane round the village's margin closes off the parcels the village cut into.
				const int ring = std::max(std::abs(dx), std::abs(dy));
				L.sketch[i] = ring == v.extent + 2 ? SAND : ring > v.extent ? GRASS : L.sketch[i];
				L.kind[i] = kNone;
				L.plotOf[i] = -1;
			}
		for (int dy = -v.extent; dy <= v.extent; ++dy)
			for (int dx = -v.extent; dx <= v.extent; ++dx)
			{
				const int at = v.index(dx, dy);
				const auto [vx, vy] = turnStencilVertex(L.facings[k], dx, dy);
				L.sketch[t.at(cx + vx, cy + vy)] = TerrainType(v.vertex[at]);
				const auto [tx, ty] = turnStencilTile(L.facings[k], dx, dy);
				const int i = t.at(cx + tx, cy + ty);
				if (v.kind[at] != kNone)
				{
					L.kind[i] = v.kind[at];
					L.homeOf[i] = k;
				}
				if (v.plot[at] >= 0)
					L.plotOf[i] = base + v.plot[at];
			}
	}
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "allotments layout";
	const AllotmentsOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("allotments.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "allotments terrain";
	TerrainSketch terrain = L.sketch;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	context.stage = "allotments colonies";
	std::vector<int> townOf(n, -1);
	for (int i = 0; i < n; ++i)
		if (L.kind[i] == kTown)
			townOf[i] = L.homeOf[i];
	if (!settleColonies(
			game, context, "allotments-starts",
			[&](int k) { return homeGrassMask(map, t, townOf, k); },
			[&](int k)
			{
				const ShapePoint p = turnStencilPoint(L.facings[k], {kSwarmForward, 0});
				return MapGeneratorPoint(int(L.homes[k].x + p.x) - 2, int(L.homes[k].y + p.y) - 2);
			}))
		return false;
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);

	context.stage = "allotments plots";
	std::vector<std::vector<int>> plotTiles(L.styles.size());
	for (int i = 0; i < n; ++i)
		if (L.plotOf[i] >= 0 && map.isGrass(i % t.w, i / t.w) && !reserved[i] &&
			clearGround(map, i % t.w, i / t.w))
			plotTiles[L.plotOf[i]].push_back(i);
	std::array<int, 4> planted{};
	std::vector<unsigned char> topup(n, 0);
	for (size_t p = 0; p < plotTiles.size(); ++p)
	{
		const std::vector<int> &tiles = plotTiles[p];
		if (tiles.empty())
			continue;
		const bool home = L.homeOf[tiles.front()] >= 0;
		for (int i : tiles)
			topup[i] = home;
		const Style style = Style(L.styles[p]);
		if (style == kWheat || style == kWood)
		{
			// A village's plots are its opening and are unscaled; the gardens' scale.
			const int share = int(tiles.size()) * kPlotCoverPercent / 100;
			const int count = std::min(int(tiles.size()),
									   home ? share : int(scaledCount(share, style == kWheat ? o.wheat : o.wood)));
			for (int j = 0; j < count; ++j)
				map.setResource(tiles[j] % t.w, tiles[j] / t.w, style == kWheat ? WHEAT : WOOD, 1);
			planted[style] += count;
		}
		else if (style == kFruit)
		{
			const int count = std::min(int(tiles.size()), home ? kFruitTiles : int(scaledCount(kFruitTiles, o.fruit)));
			const int fruit = CHERRY + int(p % 3);
			for (int j = 0; j < count; ++j)
				map.setResource(tiles[j] % t.w, tiles[j] / t.w, fruit, 1);
			planted[kFruit] += count;
		}
		else if (int(context.bounded("allotments-sheds", 100)) < kShedPercent && (home || scaledCount(1, o.stone) > 0))
		{
			map.setResource(tiles.back() % t.w, tiles.back() / t.w, STONE, 1);
			++planted[kBare];
		}
	}
	context.telemetry.measure("allotments.plots.wheat-tiles", planted[kWheat]);
	context.telemetry.measure("allotments.plots.wood-tiles", planted[kWood]);
	context.telemetry.measure("allotments.plots.fruit-tiles", planted[kFruit]);
	context.telemetry.measure("allotments.plots.sheds", planted[kBare]);

	context.stage = "allotments parcels";
	const std::vector<int> noise = fractalNoise(t.w, t.h, 16, 2, context.stream("allotments-woods"));
	for (int c = 0; c < L.parcels.cellCount(); ++c)
	{
		const Parcel &parcel = L.kinds[c];
		std::vector<int> ground;
		for (int i = 0; i < n; ++i)
			if (L.cellOf[i] == c && L.kind[i] == parcel.kind && L.homeOf[i] < 0 &&
				map.isGrass(i % t.w, i / t.w) && clearGround(map, i % t.w, i / t.w) && !reserved[i])
				ground.push_back(i);
		if (ground.empty())
			continue;
		if (parcel.kind == kWoodlot)
		{
			std::stable_sort(ground.begin(), ground.end(),
							 [&](int a, int b) { return noise[a] > noise[b]; });
			const int count = std::min(int(ground.size()),
									   int(scaledCount(int(ground.size()) * kWoodlotCoverPercent / 100, o.wood)));
			for (int j = 0; j < count; ++j)
				map.setResource(ground[j] % t.w, ground[j] / t.w, WOOD, 1);
		}
		else if (parcel.kind == kCommons)
		{
			// One grove or one small quarry on every commons.
			const int seed = ground[context.bounded("allotments-commons", std::uint32_t(ground.size()))];
			if (context.bounded("allotments-commons", 2))
			{
				if (scaledCount(1, o.fruit) > 0)
					growPatch(map, t, seed, CHERRY + int(context.bounded("allotments-commons", 3)),
							  int(scaledCount(5, o.fruit)),
							  [&](int i) { return L.cellOf[i] == c && L.kind[i] == kCommons && clearGround(map, i % t.w, i / t.w); });
			}
			else if (scaledCount(1, o.stone) > 0)
				placeResourceClump(map, context, MapGeneratorPoint(seed % t.w, seed / t.w), STONE, 1);
		}
	}
	seedAlgae(map, context, t, "allotments-algae", o.algae, AlgaeBand::anyWater(40));

	secureStartingCrops(game, context, t, 24, 32, 0, nullptr, &topup);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0);
	context.stage = "allotments routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	if (const std::string mismatch = designMismatch(L, map, "allotments"); !mismatch.empty())
		return mismatch;
	// Every plot keeps its crops to itself: flooding every plot's grass at once, labelled by plot,
	// never reaches grass of another plot or outside the plots.
	std::vector<int> label(n, -1);
	std::vector<int> queue;
	const auto open = [&](int i)
	{
		const int x = i % t.w, y = i / t.w;
		return map.isGrass(x, y) && !(map.isResource(x, y) && map.getResource(x, y).type == STONE);
	};
	for (int i = 0; i < n; ++i)
		if (L.plotOf[i] >= 0 && open(i))
		{
			label[i] = L.plotOf[i];
			queue.push_back(i);
		}
	for (size_t q = 0; q < queue.size(); ++q)
	{
		const int i = queue[q], x = i % t.w, y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = t.at(x + dx, y + dy);
				if (j == i || !open(j))
					continue;
				if (label[j] < 0)
				{
					if (L.plotOf[j] != label[i])
					{
						std::fprintf(stderr, "LEAK from %d,%d plot %d kind %d cell %d home %d -> %d,%d plot %d kind %d cell %d home %d lane %d\n", x, y, label[i], L.kind[i], L.cellOf[i], L.homeOf[i], j % t.w, j / t.w, L.plotOf[j], L.kind[j], L.cellOf[j], L.homeOf[j], L.lanes[j]);
						return "A plot's crops can spread out of it.";
					}
					label[j] = label[i];
					queue.push_back(j);
				}
				else if (label[j] != label[i])
					return "Two plots' crops can spread into each other.";
			}
	}
	if (const ColonyWalk walk = walkFromFirstColony(map, teams, "the lanes", "along the lanes");
		!walk.error.empty())
		return walk.error;
	return startingAccessFailure(map, teams, {{WHEAT, 24, "wheat"}, {WOOD, 32, "wood"}}, 16, 24);
}
} // namespace

AllotmentsOptions::AllotmentsOptions(const GenerationRequest &r)
	: siteSize(r.option("site-size")), stripWidth(r.option("strip-width")),
	  plotMix(r.option("plot-mix")), commons(r.option("commons")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition allotmentsDefinition()
{
	return {"allotments",
			40,
			"Allotments",
			2,
			false,
			// Site size is the pitch of the parcel tiling: 36 gives a 256 map seven parcels a side.
			// Plots four tiles wide are narrow enough to read as strips and wide enough to work.
			{{"site-size", "Site size", 28, 48, 4, 36, ControlGroup::Layout},
			 {"strip-width", "Plot width", 3, 5, 1, 4, ControlGroup::Layout},
			 GeneratorControl::choice("plot-mix", "Plot mix", {"Tended", "Mixed", "Overgrown"}, 1),
			 GeneratorControl::choice("commons", "Commons", {"Few", "Some", "Many"}, 1),
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld};
}
