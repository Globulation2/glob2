// SPDX-License-Identifier: GPL-3.0-or-later
#include "FaultedCityGenerator.h"
#include "Contact.h"
#include "Farmland.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Growth.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Resources.h"
#include "Room.h"
#include "ScoredSettlements.h"
#include "Sketch.h"
#include "Tessellation.h"
#include <algorithm>
#include <array>
#include <numeric>
#include <string>
#include <vector>
using namespace MapGeneration;

// A planned city, sampled through a different integer translation in each fractured district.
// The same original streets and courtyard outlines continue at visibly different offsets across
// the sandy faults. Grass streets support construction; the faults cannot be built on and remain
// the permanent travel network. Colonies occupy found lawns beside existing city gardens, never
// stamped home compounds. Water and crops belong to sealed gardens, not to the streets.
namespace
{
constexpr int kPitch = 32;
enum BlockKind { Ruin, Square, Garden, Reservoir, Orchard };
struct Block { int kind, variant, facing; };
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<int> district, source, plotOf, crop, candidates, wheatPlot, woodPlot, wheatSteps, woodSteps, wheatEntry, woodEntry, plotCapacity;
	std::vector<unsigned char> street, fault, wall, lawn, junction, renewable;
	std::vector<std::vector<int>> plots;
	std::vector<int> gates;
	std::string failure;
};

// Source designs are integer tile drawings: turns preserve streets, seals and crop widths.
std::pair<int, int> localPoint(int x, int y, int facing)
{
	for (int turn = 0; turn < facing; ++turn)
	{
		const int old = x;
		x = y;
		y = kPitch - 1 - old;
	}
	return {x, y};
}

Layout design(const GenerationRequest &r, GenerationContext &c)
{
	const FaultedCityOptions o(r);
	Layout L;
	L.t = {1 << r.wDec, 1 << r.hDec};
	const Torus &t = L.t;
	const int n = t.size(), columns = t.w / kPitch, rows = t.h / kPitch;
	std::vector<Block> blocks(columns * rows);
	std::array<int, 5> counts{};
	for (auto &b : blocks)
	{
		const int roll = c.bounded("faulted-city-blocks", 100);
		b.kind = roll < o.ruins ? Ruin :
			roll < o.ruins + (100 - o.ruins) * 25 / 65 ? Square :
			roll < o.ruins + (100 - o.ruins) * 50 / 65 ? Garden :
			roll < o.ruins + (100 - o.ruins) * 60 / 65 ? Reservoir : Orchard;
		b.variant = c.bounded("faulted-city-details", 3);
		b.facing = c.bounded("faulted-city-details", 4);
		++counts[b.kind];
	}
	// A coarse tiling describes the FINAL districts; inverse sampling has exactly one owner
	// per corner. No splat ordering, holes, doubled deposits or floating-point displacement.
	auto districts = squareTessellation(t.w, t.h, 96);
	std::vector<unsigned char> edges(districts.edges.size(), 1);
	warpCorners(districts, warpLimit(districts), edges, 30, 28, c, "faulted-city-faults");
	L.district = labelTiles(districts);
	if (L.district.empty()) { L.failure = "The blocks could not be labelled."; return L; }
	std::vector<std::pair<int, int>> shifts(districts.cellCount());
	for (int d = 0; d < districts.cellCount(); ++d)
	{
		auto &shift = shifts[d];
		shift.first = int(c.bounded("faulted-city-shifts", 2 * o.displacement + 1)) - o.displacement;
		shift.second = int(c.bounded("faulted-city-shifts", 2 * o.displacement + 1)) - o.displacement;
		// Force a different street phase at every previously assigned neighbour, deterministically.
		for (int tries = 0; tries < (2 * o.displacement + 1) * (2 * o.displacement + 1); ++tries)
		{
			bool distinct = true;
			for (int edge : districts.cells[d].edges)
			{
				const int other = districts.other(edge, d);
				if (other < d && std::max(std::abs(shifts[other].first - shift.first),
					std::abs(shifts[other].second - shift.second)) < std::max(2, o.displacement / 2)) distinct = false;
			}
			if (distinct) break;
			if (++shift.first > o.displacement)
			{ shift.first = -o.displacement; if (++shift.second > o.displacement) shift.second = -o.displacement; }
		}
		c.telemetry.measure("faulted-city.shift.x", shift.first, d);
		c.telemetry.measure("faulted-city.shift.y", shift.second, d);
	}
	const int phaseX = c.bounded("faulted-city-grid", kPitch);
	const int phaseY = c.bounded("faulted-city-grid", kPitch);
	// Put a two-block landmark across an actual fault, in the original city's frame.
	// Both sides sample this SAME civic drawing, so the displaced halves remain related.
	std::vector<int> landmarkEdges;
	for (int edge = 0; edge < int(districts.edges.size()); ++edge)
	{
		const auto e = districts.edgeEnds(edge, districts.edges[edge].cells[0]);
		const int x = t.x(int(subtileTile((e.first.x + e.second.x) / 2)));
		const int y = t.y(int(subtileTile((e.first.y + e.second.y) / 2)));
		if (x > 48 && x < t.w - 48 && y > 48 && y < t.h - 48) landmarkEdges.push_back(edge);
	}
	if (landmarkEdges.empty()) { L.failure = "The blocks could not be labelled."; return L; }
	const int landmarkEdge = landmarkEdges[c.bounded("faulted-city-landmark", landmarkEdges.size())];
	const int owner = districts.edges[landmarkEdge].cells[0];
	const auto ends = districts.edgeEnds(landmarkEdge, owner);
	const int mx = int(subtileTile((ends.first.x + ends.second.x) / 2));
	const int my = int(subtileTile((ends.first.y + ends.second.y) / 2));
	const bool verticalPlaza = std::abs(ends.second.x - ends.first.x) > std::abs(ends.second.y - ends.first.y);
	const int sourceMX = t.x(mx - shifts[owner].first + phaseX);
	const int sourceMY = t.y(my - shifts[owner].second + phaseY);
	const int plazaX = verticalPlaza ? (sourceMX / kPitch) * kPitch + 16 : (sourceMX / kPitch) * kPitch + 16;
	const int plazaY = verticalPlaza ? (sourceMY / kPitch) * kPitch + 16 : (sourceMY / kPitch) * kPitch + 16;
	const int plazaVariant = c.bounded("faulted-city-landmark", 3);
	std::vector<int> landmarkPieces(districts.cellCount(), 0), landmarkWallTiles;
	c.telemetry.choice("faulted-city.plaza-design", plazaVariant == 0 ? "arcade" : plazaVariant == 1 ? "twin-fountains" : "market");
	L.fault = rasterizeBoundaries(districts, edges, o.faultWidth / 2);
	const auto faultDistance = stepsFrom(t, L.fault);
	const auto slump = periodicNoise(t.w, t.h, 5, c.stream("faulted-city-slumps"));
	for (int i = 0; i < n; ++i)
		if (faultDistance[i] <= 3 && slump[i] > 22000 + faultDistance[i] * 8000) L.fault[i] = 1;
	const auto distance = stepsFrom(t, L.fault);
	L.terrain.assign(n, GRASS);
	L.source.resize(n);
	L.crop.assign(n, -1);
	L.plotOf.assign(n, -1);
	L.wall.assign(n, 0);
	L.street.assign(n, 0);
	L.lawn.assign(n, 0);
	// Draw the common source city through each district's coordinate frame. Every non-ruin
	// block has a lawn and an irrigated back garden; their size and furnishing vary by role.
	// Thus suitable starts exist in ordinary neighbourhoods before any colony is selected.
	for (int i = 0; i < n; ++i)
	{
		const auto shift = shifts[L.district[i]];
		const int sx = t.x(i % t.w - shift.first + phaseX);
		const int sy = t.y(i / t.w - shift.second + phaseY);
		const int block = (sy / kPitch) * columns + sx / kPitch;
		L.source[i] = block;
		const auto &b = blocks[block];
		const auto [x, y] = localPoint(sx % kPitch, sy % kPitch, b.facing);
		L.street[i] = x < 3 || x >= 29 || y < 3 || y >= 29;
		if (L.fault[i]) { L.terrain[i] = SAND; continue; }
		const bool reclaimed = b.kind == Ruin && b.variant != 0;
		const int civicKind = reclaimed ? Garden : b.kind;
		if (b.kind == Ruin && !reclaimed)
		{
			L.lawn[i] = x >= 4 && x <= 27 && y >= 4 && y <= 27;
			// Empty courtyard shell; the other ruin variants have reclaimed gardens below.
			const bool doorX = x >= 12 && x <= 19, doorY = y >= 12 && y <= 19;
			L.wall[i] = ((x == 5 || x == 6 || x == 25 || x == 26) && y >= 5 && y <= 26 && !doorY) ||
				((y == 5 || y == 6 || y == 25 || y == 26) && x >= 5 && x <= 26 && !doorX);
		}
		else
		{
			// Distinct civic spaces: a market's small corner garden, a deep productive garden,
			// a long reservoir with a bank field, and a formal orchard with side lawns.
			const int right = civicKind == Square ? 19 : 27;
			const int top = civicKind == Garden ? 14 : civicKind == Reservoir ? 10 : 15;
			const int divider = civicKind == Square ? 15 : 21;
			if (x >= 4 && x <= right && y >= top && y <= 27)
			{
				if (x == 4 || x == right || y == top || y == 27 || x == divider)
					L.terrain[i] = SAND;
				else if ((civicKind != Square && x >= 25 && y >= top + 4 && y <= 23) || (b.variant == 0 && x >= 7 && x <= divider - 3 && y >= (civicKind == Reservoir ? 20 : 23)) ||
					(b.variant == 1 && x >= 6 && x <= 10 && y >= top + 4 && y <= 25) ||
					(b.variant == 2 && ((x >= 6 && x <= 10) || (x >= divider - 6 && x <= divider - 2)) && y >= 22 && y <= 25))
					L.terrain[i] = WATER;
				else L.crop[i] = x < divider ? WHEAT : WOOD;
			}
			// A small sealed service court inside the large farm puts an inn beside the
			// grain without asking an AI to feed across the street and the plot's cap.
			if (civicKind == Reservoir && x >= 6 && x <= 13 && y >= 11 && y <= 18)
			{
				L.crop[i] = -1;
				L.terrain[i] = (x == 6 || x == 13 || y == 11 || y == 18) ? SAND : GRASS;
			}
			L.lawn[i] = x >= 4 && x <= 27 && y >= 4 &&
				(y < top - 1 || (civicKind == Square && x > right + 1));
			if (civicKind == Square)
				L.wall[i] = (y == 5 && (x == 5 || x == 11 || x == 25)) ||
					(x == 26 && y >= 6 && y <= 10 && b.variant == 2);
			if (civicKind == Orchard && x >= 8 && x <= 24 && x % 7 == 1 && (y == 6 || y == 11))
				L.crop[i] = CHERRY + block % 3;
			if (reclaimed)
			{
				// Retain substantial ruined hall/corner frontages around the reclaimed
				// garden, with broad gaps and an open lawn between the upper wings.
				const bool sideDoor = b.variant == 1 ? (y >= 12 && y <= 19) : (y >= 8 && y <= 15);
				const bool endDoor = b.variant == 1 ? (x >= 8 && x <= 15) : (x >= 16 && x <= 23);
				L.wall[i] = (((x >= 4 && x <= 5) || (x >= 26 && x <= 27)) && y >= 4 && y <= 12) ||
					(x == 28 && y >= 5 && y <= 26 && !sideDoor) ||
					(y == 28 && x >= 5 && x <= 26 && !endDoor);
			}

		}
		// Surviving facades make the old avenues visible. They are separate block outlines,
		// not a continuous fortification around a tectonic district.
		if (b.kind != Ruin && L.terrain[i] == GRASS && L.crop[i] < 0)
		{
			const bool opening = (x >= 12 && x <= 20) || (y >= 11 && y <= 19);
			if (!opening && ((x >= 4 && x <= 5 && y >= 4 && y <= 27) ||
				(x >= 26 && x <= 27 && y >= 4 && y <= 27) ||
				(b.kind != Garden && y >= 4 && y <= 5 && x >= 4 && x <= 27))) L.wall[i] = 1;
		}
		// The long square replaces whole source blocks, so it cannot open half a garden seal.
		const int pdx = t.offsetX(plazaX, sx), pdy = t.offsetY(plazaY, sy);
		const int u = verticalPlaza ? pdy : pdx, v = verticalPlaza ? pdx : pdy;
		if (u >= -48 && u < 48 && v >= -16 && v < 16)
		{
			L.terrain[i] = GRASS; L.crop[i] = -1; L.wall[i] = 0; L.lawn[i] = 0;
			L.street[i] = std::abs(v) >= 13 || u < -45 || u >= 45;
			if (std::abs(u) <= 41 && std::abs(v) <= 8) L.terrain[i] = SAND;
			if (std::abs(v) >= 10 && std::abs(v) <= 11 && std::abs(u) <= 42)
				L.wall[i] = plazaVariant == 0 ? (u + 48) % 10 < 6 : (u + 48) % 8 < 3;
			if (plazaVariant == 1 && std::abs(std::abs(u) - 30) <= 3 && std::abs(v) <= 3)
				L.terrain[i] = WATER;
			if (plazaVariant == 2 && std::abs(std::abs(u) - 28) <= 2 && std::abs(v) <= 4)
				L.terrain[i] = WATER;
			if (L.wall[i]) landmarkWallTiles.push_back(i);
		}

		// A narrow masonry lip confines access to deliberate junctions. Sand faults themselves
		// are untouched. The lip is cleared at selected street intersections below.
		if ((distance[i] == 2 || distance[i] == 3) && L.street[i]) L.wall[i] = 1;
	}
	// Find entrance candidates where a source avenue actually reaches the district lip.
	// Spread them round each district instead of punching a random wall near its centre.
	L.junction.assign(n, 0);
	auto &gateMask = L.junction;
	for (int d = 0; d < districts.cellCount(); ++d)
	{
		std::vector<int> eligible;
		for (int i = 0; i < n; ++i)
			if (L.district[i] == d && distance[i] == 3 && L.street[i]) eligible.push_back(i);
		std::vector<int> selected;
		for (int k = 0; k < 2 + o.junctions; ++k)
		{
			int best = -1, bestGap = -1;
			for (int i : eligible)
			{
				int gap = INT_MAX;
				for (int s : selected) gap = std::min(gap, t.dist2(i % t.w, i / t.w, s % t.w, s / t.w));
				if (gap > bestGap) { best = i; bestGap = gap; }
			}
			if (best < 0 || bestGap < 144) break;
			selected.push_back(best);
			L.gates.push_back(best);
			c.telemetry.measure("faulted-city.junction.x", best % t.w, L.gates.size() - 1);
			c.telemetry.measure("faulted-city.junction.y", best / t.w, L.gates.size() - 1);
			for (int dy = -4; dy <= 4; ++dy)
				for (int dx = -4; dx <= 4; ++dx)
					gateMask[t.at(best % t.w + dx, best / t.w + dy)] = 1;
		}
		if (selected.size() < 2) { L.failure = "The city has too few usable junctions."; return L; }
		c.telemetry.measure("faulted-city.junctions.actual", selected.size(), d);
	}
	for (int i = 0; i < n; ++i)
	{
		if (gateMask[i])
		{
			L.wall[i] = 0;
			// Keep the source surface: a gate never breaks a garden's sand containment.
		}
		if (distance[i] <= 5) L.lawn[i] = 0;
	}
	layBeaches(L.terrain, t);
	const auto grass = pureTiles(L.terrain, t, GRASS);
	// A translated plot crossing a fault becomes two independently sealed plots. Label the
	// final grass components rather than reusing source labels across a disconnected fragment.
	std::vector<unsigned char> cropGround(n, 0);
	for (int i = 0; i < n; ++i)
	{
		L.wall[i] = L.wall[i] && grass[i];
		cropGround[i] = grass[i] && (L.crop[i] == WHEAT || L.crop[i] == WOOD);
	}
	for (int i : landmarkWallTiles) if (L.wall[i]) ++landmarkPieces[L.district[i]];
	const int survivingPieces = std::count_if(landmarkPieces.begin(), landmarkPieces.end(),
		[](int count) { return count >= 6; });
	c.telemetry.measure("faulted-city.plaza.surviving-districts", survivingPieces);
	if (survivingPieces < 2)
	{ L.failure = "The fractured civic plaza did not survive."; return L; }

	L.plotOf = connectedRegions(cropGround, t.w, t.h, true, GridNeighbors::Eight);
	for (int i = 0; i < n; ++i)
		if (L.plotOf[i] >= 0)
		{
			if (L.plotOf[i] >= int(L.plots.size())) L.plots.resize(L.plotOf[i] + 1);
			L.plots[L.plotOf[i]].push_back(i);
		}
	// Propagate the identity of the nearest sustainable plot along real streets. A tiny
	// clipped garden cannot hide a useful full field behind it merely by being closer.
	const auto fertility = cropGrowthField(L.terrain, t);
	std::vector<unsigned char> open(n, 0);
	const auto water = pureTiles(L.terrain, t, WATER);
	L.renewable.assign(n, 0);
	L.plotCapacity.assign(L.plots.size(), 0);
	for (int i = 0; i < n; ++i)
	{
		open[i] = !water[i] && !L.wall[i] && !cropGround[i] && L.crop[i] < CHERRY;
		L.renewable[i] = cropGround[i] && fertility.at(i % t.w, i / t.w) > 0;
		if (L.renewable[i] && !L.wall[i]) ++L.plotCapacity[L.plotOf[i]];
	}
	const auto cropAccess = [&](int resource, std::vector<int> &owner, std::vector<int> &steps, std::vector<int> &entry)
	{
		owner.assign(n, -1); steps.assign(n, -1); entry.assign(n, -1);
		std::vector<int> queue;
		queue.reserve(n);
		for (int p = 0; p < int(L.plots.size()); ++p)
		{
			const auto &plot = L.plots[p];
			if (plot.empty() || L.crop[plot.front()] != resource) continue;
			std::uint64_t yield = 0;
			int capacity = 0;
			for (int i : plot) if (!L.wall[i] && fertility.at(i % t.w, i / t.w) > 0)
			{ yield += fertility.at(i % t.w, i / t.w); ++capacity; }
			if (resource == WHEAT) c.telemetry.measure("faulted-city.plot.food-yield", double(yield) / 65536, p);
			if (capacity < (resource == WHEAT ? 48 : 16) || (resource == WHEAT && yield < 3 * 65536ULL)) continue;
			for (int i : plot) if (!L.wall[i] && fertility.at(i % t.w, i / t.w) > 0)
			{ owner[i] = p; entry[i] = i; steps[i] = 0; queue.push_back(i); }
		}
		for (size_t q = 0; q < queue.size(); ++q)
		{
			const int i = queue[q];
			for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = t.at(i % t.w + dx, i / t.w + dy);
				if (steps[j] < 0 && open[j])
				{ steps[j] = steps[i] + 1; owner[j] = owner[i]; entry[j] = entry[i]; queue.push_back(j); }
			}
		}
	};
	cropAccess(WHEAT, L.wheatPlot, L.wheatSteps, L.wheatEntry);
	cropAccess(WOOD, L.woodPlot, L.woodSteps, L.woodEntry);
	// Candidate anchors remain wholly inside existing lawns, away from the fault and walls.
	std::vector<unsigned char> room(n, 0);
	for (int i = 0; i < n; ++i) room[i] = grass[i] && L.lawn[i] && !L.wall[i] && L.crop[i] < 0;
	const auto anchors = buildAnchors(t, room, 7);
	for (int i = 0; i < n; ++i)
		if (anchors[i] && i % 3 == 0) L.candidates.push_back(t.at(i % t.w + 2, i / t.w + 2));
	c.telemetry.measure("faulted-city.districts", districts.cellCount());
	c.telemetry.measure("faulted-city.starts.candidates", L.candidates.size());
	c.telemetry.measure("faulted-city.fault.corners", std::count(L.fault.begin(), L.fault.end(), 1));
	for (int k = 0; k < 5; ++k) c.telemetry.measure("faulted-city.blocks", counts[k], k);
	return L;
}

// A surviving avenue mouth is a broad passage, and occupied neighbourhoods must
// reach two mouths through their own streets before entering a fault corridor.
std::string colonyRoutes(const Map &map, const Layout &L, int teams)
{
	const auto walking = walkableTiles(map);
	for (int gate : L.gates)
		for (int dy = -2; dy <= 2; ++dy)
			for (int dx = -2; dx <= 2; ++dx)
				if (!walking[L.t.at(gate % L.t.w + dx, gate / L.t.w + dy)])
					return "The city has too few usable junctions.";
	auto interior = walking;
	for (int i = 0; i < L.t.size(); ++i) if (L.fault[i]) interior[i] = 0;
	const auto components = connectedRegions(interior, L.t.w, L.t.h, true, GridNeighbors::Eight);
	std::vector<int> mouths(L.t.size(), 0);
	for (int gate : L.gates) if (components[gate] >= 0) ++mouths[components[gate]];
	for (const auto &units : unitTilesByTeam(map, teams))
	{
		bool exits = false;
		for (int unit : units) if (components[unit] >= 0 && mouths[components[unit]] >= 2) exits = true;
		if (!exits) return "The city has too few usable junctions.";
	}
	return {};
}

// Prove actual non-overlapping construction room, not just many overlapping anchors.
std::string colonyRoom(const Map &map, const Layout &L, int teams)
{
	const Torus t(map);
	const auto walking = walkableTiles(map);
	auto building = potentialBuildingTiles(map);
	for (int i = 0; i < t.size(); ++i) if (L.renewable[i]) building[i] = 0;
	const auto units = unitTilesByTeam(map, teams);
	for (int k = 0; k < teams; ++k)
	{
		if (units[k].empty()) return "A city colony has no workers.";
		const int site = units[k].front(), x = site % t.w, y = site / t.w;
		const auto reach = reachFrom(t, units[k], walking, 24);
		auto local = tileMask(t, reach.tiles);
		for (int i : reach.tiles) local[i] = building[i];
		bool fits = false;
		// A city street is narrower than the packing grid's pitch. Check its alignment
		// instead of mistaking one arbitrary grid phase for the available building room.
		for (int oy = 0; oy < 6 && !fits; oy += 2)
			for (int ox = 0; ox < 6 && !fits; ox += 2)
			{
				const auto arrangement = arrangeBuildingGrid(t, local, walking,
					{{x - 24 + ox, y - 24 + oy, x + 25, y + 25}, 4, 4, 2, 1}, units[k]);
				fits = arrangement.failure.empty() && arrangement.footprints.size() >= 6;
			}
		if (!fits) return "A city neighbourhood lacks reachable crops or building room.";
	}
	return {};
}

std::string colonyFood(const Map &map, const Layout &L, int teams, const std::vector<int> *sites = nullptr)
{
	const auto open = walkableTiles(map);
	const auto units = unitTilesByTeam(map, teams);
	const auto fertility = Fertility::forMap(map, false);
	for (int k = 0; k < teams; ++k)
	{
		const auto reach = reachFrom(L.t, units[k], open, 24);
		std::vector<unsigned char> seen(L.t.size(), 0), plots(L.plots.size(), 0);
		int close = 0, total = 0;
		bool wood = false;
		for (size_t p = 0; p < reach.tiles.size(); ++p)
		{
			const int i = reach.tiles[p];
			for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = L.t.at(i % L.t.w + dx, i / L.t.w + dy);
				const int resource = map.getResource(j % L.t.w, j / L.t.w).type;
				if (seen[j] || (resource != WHEAT && resource != WOOD) || reach.steps[p] + 1 > 24) continue;
				seen[j] = 1;
				if (resource == WOOD)
				{
					if (!sites || L.plotOf[j] == L.woodPlot[(*sites)[k]]) wood = true;
					continue;
				}
				// Starter guarantees must work on their own at zero ambient abundance.
				if (sites && L.plotOf[j] != L.wheatPlot[(*sites)[k]]) continue;
				++total;
				if (reach.steps[p] + 1 <= 12) ++close;
				if (L.plotOf[j] >= 0) plots[L.plotOf[j]] = 1;
			}
		}
		std::uint64_t yield = 0;
		for (size_t p = 0; p < plots.size(); ++p) if (plots[p])
			for (int i : L.plots[p]) if (!L.wall[i]) yield += fertility.at(i % L.t.w, i / L.t.w);
		if (!wood || close < 12 || total < 24 || yield < 3 * 65536ULL)
		return "A city neighbourhood lacks reachable crops or building room.";
	}
	return {};
}

bool populate(Game &game, GenerationContext &c, const Layout &L, const std::vector<int> &sites)
{
	const FaultedCityOptions o(c.request);
	const Torus &t = L.t;
	Map &map = game.map;
	writeUndermap(map, L.terrain);
	for (int k = 0; k < c.request.nbTeams; ++k) game.addTeam();
	for (int i = 0; i < t.size(); ++i)
		if (L.wall[i]) map.setResource(i % t.w, i / t.w, STONE, 1);
	const auto fertility = Fertility::forMap(map, false);
	std::vector<int> starterOwner(L.plots.size(), -1);
	for (int site : sites)
	{
		if (L.wheatPlot[site] >= 0)
		{
			if (starterOwner[L.wheatPlot[site]] >= 0) return false;
			starterOwner[L.wheatPlot[site]] = site;
		}
		if (L.woodPlot[site] >= 0) starterOwner[L.woodPlot[site]] = site;
	}
	int planted[2]{};
	for (const auto &plot : L.plots)
	{
		if (plot.empty()) continue;
		const int resource = L.crop[plot.front()];
		const bool starter = starterOwner[L.plotOf[plot.front()]] >= 0;
		const int amount = resource == WHEAT ? o.wheat : o.wood;
		const int capacity = L.plotCapacity[L.plotOf[plot.front()]];
		const int minimum = starter ? capacity : 0;
		// 100% seeds 55% of renewable capacity; the upper half of the slider fills
		// the remaining space gradually, reaching capacity only at 300%.
		const int fill = amount <= 100 ? amount * 110 : 11000 + (amount - 100) * 45;
		const int wanted = std::max(minimum, capacity * fill / 20000);
		int guaranteed = 0;
		if (starter)
		{
			const int label = L.plotOf[plot.front()];
			const int owner = starterOwner[label];
			const int seed = (resource == WHEAT ? L.wheatEntry[owner] : L.woodEntry[owner]);
			const auto eligible = [&](int i)
			{ return L.plotOf[i] == label && fertility.at(i % t.w, i / t.w) > 0 && clearGround(map, i % t.w, i / t.w); };
			// Put the first grain on the town-facing edge, not only at the deepest water.
			// Inns need several grains within their own working radius from the opening.
			if (seed >= 0) guaranteed = growPatch(map, t, seed, resource, minimum, eligible);
		}
		planted[resource == WOOD] += guaranteed + plantContainedPlot(map, t, plot, fertility, resource, std::max(0, wanted - guaranteed));
	}
	for (int k = 0; k < c.request.nbTeams; ++k)
	{
		const int site = sites[k];
		std::vector<unsigned char> home(t.size(), 0);
		for (int dy = -3; dy <= 6; ++dy)
			for (int dx = -3; dx <= 6; ++dx)
			{
				const int i = t.at(site % t.w + dx, site / t.w + dy);
				home[i] = L.lawn[i] && !L.wall[i] && L.crop[i] < 0;
			}
		if (!placeSettlement(game, c, k, home, {site % t.w, site / t.w}, "faulted-city-starts")) return false;
	}
	// Optional stone/fruit furnish ruin interiors, off the streets and colony lawns.
	int stones = 0, fruits = 0;
	for (int i = 0; i < t.size(); ++i)
		if (!L.street[i] && !L.lawn[i] && !L.fault[i] && !L.junction[i] && L.crop[i] < 0 &&
			!L.wall[i] && clearGround(map, i % t.w, i / t.w))
		{
			const auto draw = c.bounded("faulted-city-ambient", 12000);
			if (draw < unsigned(o.stone)) { map.setResource(i % t.w, i / t.w, STONE, 1); ++stones; }

		}
	for (int i = 0; i < t.size(); ++i)
		if (L.crop[i] >= CHERRY && !L.junction[i] && clearGround(map, i % t.w, i / t.w) &&
			c.bounded("faulted-city-fruit", 300) < unsigned(o.fruit))
		{ map.setResource(i % t.w, i / t.w, L.crop[i], 1); ++fruits; }
	seedAlgae(map, c, t, "faulted-city-algae", o.algae, AlgaeBand::anyWater());
	if (const auto routes = colonyRoutes(map, L, c.request.nbTeams); !routes.empty())
	{ c.telemetry.choice("faulted-city.rejected-check", "routes"); c.detail = routes; return false; }
	if (const auto food = colonyFood(map, L, c.request.nbTeams, &sites); !food.empty())
	{ c.telemetry.choice("faulted-city.rejected-check", "food"); c.detail = food; return false; }
	if (const auto room = colonyRoom(map, L, c.request.nbTeams); !room.empty())
	{ c.telemetry.choice("faulted-city.rejected-check", "room"); c.detail = room; return false; }
	c.telemetry.measure("faulted-city.resources.wheat", planted[0]);
	c.telemetry.measure("faulted-city.resources.wood", planted[1]);
	c.telemetry.measure("faulted-city.resources.stone-extra", stones);
	c.telemetry.measure("faulted-city.resources.fruit", fruits);
	return true;
}

std::string qualityFailure(const StartQualityReport &q)
{
	for (const auto &s : q.colonies)
		if (s.wheatDistance < 0 || s.wheatDistance > 12 || s.woodDistance < 0 || s.woodDistance > 24 ||
			s.resources[STONE].nearestDistance < 0 || s.resources[STONE].nearestDistance > 32 || s.buildSites < 48 ||
			s.distanceBands[0].depositTiles[WHEAT] < 12 || s.resources[WHEAT].catchmentDeposits < 24)
		return "A city neighbourhood lacks reachable crops or building room.";
	if (q.fairness < 0.80) return "The city neighbourhoods are too unequal; try another seed.";
	return {};
}

constexpr int kLayoutAttempts = 8;
GenerationRequest layoutRequest(const GenerationRequest &request, int attempt)
{
	auto r = request;
	if (attempt) r.seed = GenerationContext::deriveSeed(request.seed,
		"faulted-city-layout-" + std::to_string(attempt));
	return r;
}

bool generate(Game &game, GenerationContext &c)
{
	// Search whole, unmodified city layouts when fractures leave too few healthy
	// neighbourhoods. Never patch a failed site into a prefabricated home compound.
	for (int attempt = 0; attempt < kLayoutAttempts; ++attempt)
	{
		const auto request = layoutRequest(c.request, attempt);
		auto canonical = request;
		for (const char *key : {"wheat-amount", "wood-amount", "stone-amount", "algae-amount", "fruit-amount"})
			canonical.options[key] = 300;
		GenerationContext trial(canonical, c.telemetry.enabled());
		c.stage = "faulted city layout";
		const auto L = design(request, trial);
		if (!L.failure.empty()) { c.detail = L.failure; continue; }
		std::vector<RankedSite> candidates;
		for (int i : L.candidates)
			if (L.wheatSteps[i] >= 0 && L.wheatSteps[i] <= 10 && L.woodSteps[i] >= 0 && L.woodSteps[i] <= 20)
				candidates.push_back({i, 1.0 / (1.0 + L.wheatSteps[i] / 12.0)});
		std::vector<std::vector<int>> proposals;
		for (int p = 0; p < 6 && !candidates.empty(); ++p)
		{
			auto sites = spreadRankedSites(L.t, candidates, request.nbTeams, 32,
				trial.bounded("faulted-city-proposals", candidates.size()));
			dealStarts(trial, sites, "faulted-city-deal");
			proposals.push_back(std::move(sites));
		}
		trial.telemetry.measure("faulted-city.starts.viable-candidates", candidates.size());
		c.stage = "faulted city settlements";
		const auto build = [&](Game &g, GenerationContext &probe, const std::vector<int> &sites)
		{ return populate(g, probe, L, sites); };
		const auto choice = chooseScoredSettlements(trial, proposals, build, qualityFailure);
		if (choice.selected < 0)
		{
			c.detail = choice.failure;
			c.telemetry.fallback("faulted-city.layout.rejected", choice.failure, attempt);
			continue;
		}
		// Resource controls furnish the selected city; they never choose a different
		// terrain merely because an abundance changes a placement score.
		GenerationContext actual(request, c.telemetry.enabled());
		if (!build(game, actual, choice.sites))
		{ c.telemetry.replay(actual.telemetry); c.detail = actual.detail; return false; }
		c.bootX = actual.bootX; c.bootY = actual.bootY;
		c.telemetry.replay(trial.telemetry);
		c.telemetry.replay(actual.telemetry);
		c.telemetry.measure("faulted-city.layout.attempts", attempt + 1);
		c.detail.clear();
		return true;
	}
	return false;
}

std::string validateRequest(const GenerationRequest &r)
{
	if (r.wDec < 8 || r.hDec < 8 || r.wDec > 9 || r.hDec > 9 || r.nbTeams < 1 || r.nbTeams > 12)
		return "The Faulted City needs 256 or 512 tiles per side and one to twelve colonies.";
	return {};
}

std::string validateWorld(const Game &game, const GenerationContext &c)
{
	Layout L;
	bool matched = false;
	for (int attempt = 0; attempt < kLayoutAttempts && !matched; ++attempt)
	{
		const auto request = layoutRequest(c.request, attempt);
		GenerationContext replay(request);
		auto candidate = design(request, replay);
		if (!designMismatch(candidate, game.map, "faulted city").empty()) continue;
		matched = true;
		for (int i = 0; i < candidate.t.size() && matched; ++i)
			matched = candidate.terrain[i] == game.map.getUMTerrain(i % candidate.t.w, i / candidate.t.w);
		if (matched) L = std::move(candidate);
	}
	if (!matched) return "The city terrain no longer matches its design.";
	if (const auto plots = containedPlotsMismatch(game.map, L.t, L.plotOf); !plots.empty()) return plots;
	for (int i = 0; i < L.t.size(); ++i)
	{
		if (L.wall[i] && game.map.getResource(i % L.t.w, i / L.t.w).type != STONE)
			return "The city has lost part of its masonry.";
		if (L.fault[i] && (game.map.getUMTerrain(i % L.t.w, i / L.t.w) != SAND ||
			game.map.isResource(i % L.t.w, i / L.t.w)))
			return "A city fault is obstructed.";
	}
	if (const auto access = startingAccessFailure(game.map, c.request.nbTeams,
		{{WHEAT, 12, "wheat"}, {WOOD, 24, "wood"}, {STONE, 32, "stone"}}, 48); !access.empty()) return access;
	if (const auto routes = colonyRoutes(game.map, L, c.request.nbTeams); !routes.empty()) return routes;
	if (const auto food = colonyFood(game.map, L, c.request.nbTeams); !food.empty()) return food;
	if (const auto room = colonyRoom(game.map, L, c.request.nbTeams); !room.empty()) return room;
	const auto walk = walkFromFirstColony(game.map, c.request.nbTeams, "the city", "through its junctions");
	if (!walk.error.empty()) return walk.error;
	for (int gate : L.gates)
		if (walk.steps[gate] < 0) return "The city has an unreachable junction.";
	return {};
}
} // namespace

FaultedCityOptions::FaultedCityOptions(const GenerationRequest &r)
	: displacement(r.option("fault-displacement")), faultWidth(r.option("fault-width")),
	  ruins(r.option("ruin-density")), junctions(r.option("surviving-junctions")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount")) {}
GeneratorDefinition faultedCityDefinition()
{
	return {"faulted-city", 62, "The Faulted City", 1, false,
		{{"fault-displacement", "Fault displacement", 4, 14, 1, 10, ControlGroup::Terrain},
		 {"fault-width", "Fault width", 8, 16, 2, 12, ControlGroup::Terrain},
		 {"ruin-density", "Ruin density", 20, 60, 5, 35, ControlGroup::Layout},
		 GeneratorControl::choice("surviving-junctions", "Surviving junctions", {"Few", "Normal", "Many"}, 1, ControlGroup::Layout),
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate, true, validateRequest, validateWorld,
		{"terrain:urban", "feature:stone-walls", "feature:lakes", "style:expansion"}};
}
