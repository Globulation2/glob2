// SPDX-License-Identifier: GPL-3.0-or-later
#include "BastionKeysGenerator.h"
#include "Drawing.h"
#include "Farmland.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Grid.h"
#include "Growth.h"
#include "Orbits.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Room.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Walls.h"
#include "Topology.h"
#include <algorithm>
#include <cmath>
using namespace MapGeneration;

// Bastion Keys: protected harbour towns whose livelihood lies outside the ramparts. A short
// folded landing approach leads to two exposed plantations. Open sea separates the estates
// and neutral keys; swimming is required to expand or attack another colony.
// Stone is permanent terrain, not a granted defence. All production is outside the home walls.
namespace
{
struct Key
{
	ShapePoint centre;
	int radius, owner, kind; // owner -1: neutral; kind 0 plantation, 1 orchard, 2 outwork
	std::vector<int> crops[2];
	std::vector<int> court;
};
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<unsigned char> wall, gates, roads, reserved;
	std::vector<int> homeOf, cropOf, keyOf;
	std::vector<ShapePoint> homes;
	std::vector<Key> keys;
	std::vector<std::vector<int>> landingFields;
	int fortDesign = 0;
	bool transpose = false;
	std::string failure;
	ShapePoint point(int team, double u, double v) const
	{
		return {homes[team].x + (transpose ? v : u), homes[team].y + (transpose ? u : v)};
	}
	int at(int team, int u, int v) const
	{
		const auto p = point(team, u, v);
		return t.at(int(p.x), int(p.y));
	}
};

// Same facing and fort design for all homes. Only the natural island fringes differ.
void fort(Layout &L, int team, const BastionKeysOptions &o, GenerationContext &context)
{
	const int r = o.homeSize;
	const int cy = -12;
	const double phase = context.bounded("bastion-keys-coast", 65536) * 2 * kPi / 65536;
	for (int v = -58; v <= 45; ++v)
		for (int u = -46; u <= 46; ++u)
		{
			const int i = L.at(team, u, v);
			L.reserved[i] = (std::abs(u) <= r + 9 && v <= cy + r + 16) || (v >= 10 && v <= 45);
			const double a = std::atan2(v - cy, u);
			const double fringe = 3 + 1.3 * std::sin(3 * a + phase) + .7 * std::sin(5 * a - phase);
			if (std::max(std::abs(u), std::abs(v - cy)) <= r + fringe)
				L.terrain[i] = GRASS;
			if (std::abs(u) < r && std::abs(v - cy) < r)
				L.homeOf[i] = team;
			// Two tile thick ramparts. Variants change the corner traces, not the usable court.
			bool stone = std::abs(u) <= r && std::abs(v - cy) <= r &&
						 (std::abs(u) >= r - 1 || std::abs(v - cy) >= r - 1);
			const int cu = std::abs(u) - r, cv = std::abs(v - cy) - r;
			if (L.fortDesign == 0 && std::abs(cu) <= 3 && std::abs(cv) <= 3)
				stone = true;
			if (L.fortDesign == 1 && cu * cu + cv * cv <= 12)
				stone = true;
			if (L.fortDesign == 2 && std::abs(cu) + std::abs(cv) <= 5)
				stone = true;
			if (stone)
				L.wall[i] = 1;
		}
	// The gate corridor folds once, with a two-tile dividing wall. Its width leaves grass
	// shoulders for towers; a sand centre remains open after construction and crop growth.
	const int bottom = cy + r;
	for (int v = bottom - 2; v <= bottom + 12; ++v)
		for (int u = -4; u <= 10; ++u)
		{
			const int i = L.at(team, u, v);
			L.terrain[i] = GRASS;
			if ((u == -4 || u == 10 || v == bottom + 12) && v >= bottom)
				L.wall[i] = 1;
			if ((v == bottom + 5 || v == bottom + 6) && u >= 2)
				L.wall[i] = 1;
		}
	const auto lane = [&](int u0, int v0, int u1, int v1, bool gate)
	{
		const auto a = L.point(team, u0, v0), b = L.point(team, u1, v1);
		std::vector<unsigned char> mask(L.t.size());
		strokePath(mask, L.t, {{a.x, a.y, 1.5}, {b.x, b.y, 1.5}});
		const auto tiles = roadTiles(L.t, mask);
		for (int i = 0; i < L.t.size(); ++i)
		{
			if (mask[i])
			{
				L.terrain[i] = SAND;
				L.roads[i] = 1;
			}
			if (tiles[i])
			{
				L.wall[i] = 0;
				if (gate)
					L.gates[i] = 1;
			}
		}
	};
	lane(5, bottom - 3, 5, bottom + 2, true);
	lane(5, bottom + 2, -1, bottom + 2, false);
	lane(-1, bottom + 2, -1, bottom + 9, false);
	lane(-1, bottom + 9, 5, bottom + 9, false);
	lane(5, bottom + 9, 6, 20, false);
	// Harbour: open water between curved rocky arms. The landing court lies outside the wall,
	// so swimmers get a direct landing; walkers can also circle the longer outer beach.
	// Legal stone needs grass corners and cannot seal a beach against pure water.
	const int harbourY = cy - r - 11;
	for (int v = harbourY - 17; v <= cy - r - 3; ++v)
		for (int u = -22; u <= 22; ++u)
		{
			const double d = std::hypot(double(u), double(v - harbourY));
			const double a = std::atan2(v - harbourY, u);
			const double outer = 19 + std::sin(3 * a + phase);
			if (d >= 13 && d <= outer && (v > harbourY - 10 || std::abs(u) > 9))
				L.terrain[L.at(team, u, v)] = GRASS;
		}
	for (int v = cy - r - 8; v <= cy - r + 2; ++v)
		for (int u = -6; u <= 6; ++u)
			L.terrain[L.at(team, u, v)] = GRASS;
	lane(0, cy - r - 7, 0, cy - r + 3, true);
}

void plantation(Layout &L, Key &key, GenerationContext &context)
{
	const int x = int(key.centre.x), y = int(key.centre.y), r = key.radius;
	const auto existing = L.terrain;
	const double phase = context.bounded("bastion-keys-keys", 65536) * 2 * kPi / 65536;
	for (int dy = -r - 3; dy <= r + 3; ++dy)
		for (int dx = -r - 3; dx <= r + 3; ++dx)
		{
			const double angle = std::atan2(dy, dx);
			const double radius =
				r + 1.6 * std::sin(3 * angle + phase) + .8 * std::sin(5 * angle - phase);
			const double stretch =
				key.kind == 0 ? (L.fortDesign == 1 ? 1.12 : .95) : (key.kind == 1 ? 1.15 : .9);
			const double bend = key.kind == 1 ? 2 * std::sin(double(dy) / r * kPi) : 0;
			if ((dx - bend) * (dx - bend) / (stretch * stretch) + dy * dy * stretch * stretch <=
				radius * radius)
			{
				bool separated = true;
				if (key.owner >= 0)
				{
					const auto home = L.homes[key.owner];
					const int du = L.t.offsetX(int(home.x), x + dx),
							  dv = L.t.offsetY(int(home.y), y + dy);
					const int u = L.transpose ? dv : du, v = L.transpose ? du : dv;
					if (std::abs(u) <= 7 && v >= 14)
						separated = false;
					for (int ey = -2; ey <= 2 && separated; ++ey)
						for (int ex = -2; ex <= 2; ++ex)
							if (existing[L.t.at(x + dx + ex, y + dy + ey)] != WATER)
							{
								separated = false;
								break;
							}
				}
				if (separated)
				{
					const int i = L.t.at(x + dx, y + dy);
					L.terrain[i] = GRASS;
					L.keyOf[i] = int(L.keys.size());
				}
			}
		}
	// Each island has a service court; crops occupy two sealed coastal plots, never the court.
	// One-corner separators leave the nearest inn footprint beside harvestable grain.
	Farm dummy;
	dummy.water.assign(L.t.size(), 0);
	dummy.plot.assign(L.t.size(), 0);
	dummy.sand.assign(L.t.size(), 0);
	stampFarmPlot(L.terrain, L.t, dummy, x - 4, y - 4, {8, 8, 1});
	for (int dy = -4; dy < 4; ++dy)
		for (int dx = -4; dx < 4; ++dx)
			key.court.push_back(L.t.at(x + dx, y + dy));
	if (key.kind == 2)
	{
		// A small open-backed coastal battery, rather than a quarry pretending to be a fort.
		for (int d = -8; d <= 8; ++d)
		{
			L.wall[L.t.at(x + d, y - 8)] = 1;
			if (d <= 1)
				L.wall[L.t.at(x - 8, y + d)] = 1;
			if (d <= -2)
				L.wall[L.t.at(x + 8, y + d)] = 1;
		}
	}
	for (int d = -r - 1; d <= r + 1; ++d)
	{
		// A cross connects the clear court to every landing; the northern/southern lanes split
		// wheat from timber. The court overwrites their centres.
		if (std::abs(d) > 5)
		{
			L.terrain[L.t.at(x + d, y)] = SAND;
			L.terrain[L.t.at(x, y + d)] = SAND;
		}
	}
}

std::string requestFailure(const GenerationRequest &r)
{
	// A home is an estate, not a tiny standalone island. Never compress away its harbour or farms.
	if ((1 << r.wDec) < 128 || (1 << r.hDec) < 128)
		return "Bastion Keys need at least 128 tiles on each side.";
	const int w = 1 << r.wDec, h = 1 << r.hDec;
	if (r.nbTeams > (w / 128) * (h / 128))
		return "These island estates need more room; use a larger map or fewer colonies.";
	return {};
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	L.failure = requestFailure(request);
	if (!L.failure.empty())
		return L;
	const BastionKeysOptions o(request);
	const Torus &t = L.t;
	L.terrain.assign(t.size(), WATER);
	L.wall.assign(t.size(), 0);
	L.gates.assign(t.size(), 0);
	L.roads.assign(t.size(), 0);
	L.reserved.assign(t.size(), 0);
	L.homeOf.assign(t.size(), -1);
	L.cropOf.assign(t.size(), -1);
	L.keyOf.assign(t.size(), -1);
	L.fortDesign = context.bounded("bastion-keys-fort", 3);
	L.transpose = context.bounded("bastion-keys-facing", 2);
	const int shiftX = context.bounded("bastion-keys-sites", t.w);
	const int shiftY = context.bounded("bastion-keys-sites", t.h);
	// Rectangular slots reserve an honest envelope. Empty slots become open archipelago;
	// shuffle all slots before selecting, so odd counts do not occupy only one map edge.
	std::vector<ShapePoint> slots;
	for (int y = 0; y < t.h / 128; ++y)
		for (int x = 0; x < t.w / 128; ++x)
			slots.push_back({double(t.x(shiftX + x * 128)), double(t.y(shiftY + y * 128))});
	// Select spread sites greedily; ties use a seeded deal, then assign sites to teams independently.
	dealStarts(context, slots, "bastion-keys-slots");
	L.homes.push_back(slots.back());
	slots.pop_back();
	while (int(L.homes.size()) < request.nbTeams)
	{
		int best = 0, bestDistance = -1;
		for (int k = 0; k < int(slots.size()); ++k)
		{
			int distance = INT_MAX;
			for (const auto &h : L.homes)
				distance = std::min(distance,
									t.dist2(int(h.x), int(h.y), int(slots[k].x), int(slots[k].y)));
			if (distance > bestDistance)
			{
				best = k;
				bestDistance = distance;
			}
		}
		L.homes.push_back(slots[best]);
		slots.erase(slots.begin() + best);
	}
	// Small translations break the visual lattice without risking adjacent estate envelopes.
	for (auto &h : L.homes)
	{
		const int jx = int(context.bounded("bastion-keys-jitter", 17)) - 8;
		const int jy = int(context.bounded("bastion-keys-jitter", 17)) - 8;
		h.x = t.x(int(h.x) + jx);
		h.y = t.y(int(h.y) + jy);
	}
	dealStarts(context, L.homes, "bastion-keys-starts");
	for (int team = 0; team < request.nbTeams; ++team)
	{
		fort(L, team, o, context);
		for (int sign : {-1, 1})
		{
			Key key{L.point(team, sign * 22, 24), o.plantationSize, team, 0, {}, {}};
			plantation(L, key, context);
			L.keys.push_back(std::move(key));
		}
		const int bottom = -12 + o.homeSize;
		const auto inner = L.point(team, 5, bottom + 2);
		for (int sign : {-1, 1})
		{
			const auto shoulder = L.point(team, sign * 24, bottom + 2);
			const auto end = L.point(team, sign * 22, 24);
			std::vector<unsigned char> pier(t.size());
			strokePath(
				pier, t,
				{{inner.x, inner.y, 1.0}, {shoulder.x, shoulder.y, 1.0}, {end.x, end.y, 1.0}});
			const auto fieldFrom = L.point(team, sign * 14, bottom + 2);
			const auto fieldLanding = L.point(team, sign * 14, bottom + 7);
			strokePath(pier, t,
					   {{fieldFrom.x, fieldFrom.y, 1.0}, {fieldLanding.x, fieldLanding.y, 1.0}});
			const auto pierTiles = roadTiles(t, pier);
			for (int i = 0; i < t.size(); ++i)
			{
				if (pier[i])
					L.roads[i] = 1;
				if (pierTiles[i])
					L.wall[i] = 0;
			}
		}
	}
	// Outlying keys remain separate islands. Their spacing protects open-water passages
	// even after beaches are laid and every harvestable resource has been cleared.
	const int wanted = o.outerIslands * request.nbTeams;
	int placed = 0;
	for (int attempt = 0; placed < wanted && attempt < wanted * 160; ++attempt)
	{
		const int x = context.bounded("bastion-keys-expansion", t.w);
		const int y = context.bounded("bastion-keys-expansion", t.h);
		const int kind = placed % 3, radius = kind == 0 ? 14 : 10;
		bool fits = true;
		for (int dy = -radius - 7; dy <= radius + 7 && fits; ++dy)
			for (int dx = -radius - 7; dx <= radius + 7; ++dx)
				if (L.reserved[t.at(x + dx, y + dy)])
				{
					fits = false;
					break;
				}
		for (const auto &key : L.keys)
			if (t.dist2(x, y, int(key.centre.x), int(key.centre.y)) <
				(radius + key.radius + 8) * (radius + key.radius + 8))
				fits = false;
		if (!fits)
			continue;
		Key key{{double(x), double(y)}, radius, -1, kind, {}, {}};
		plantation(L, key, context);
		L.keys.push_back(std::move(key));
		++placed;
	}
	// Sand lanes are confined to each estate's folded landing and supply piers.
	for (int i = 0; i < t.size(); ++i)
		if (L.roads[i])
			L.terrain[i] = SAND;
	for (const auto &key : L.keys)
		for (int i : key.court)
			for (int dy = 0; dy <= 1; ++dy)
				for (int dx = 0; dx <= 1; ++dx)
					L.terrain[t.at(i % t.w + dx, i / t.w + dy)] = GRASS;
	// Walls own their four grass corners. This keeps beaches a full tile outside the wall.
	for (int i = 0; i < t.size(); ++i)
		if (L.wall[i])
			for (int dy = -1; dy <= 2; ++dy)
				for (int dx = -1; dx <= 2; ++dx)
				{
					const int j = t.at(i % t.w + dx, i / t.w + dy);
					// Preserve sand circulation/containment in the apron. Only the four
					// actual wall corners must overwrite terrain; roadTiles removed clashes.
					if ((dx >= 0 && dx <= 1 && dy >= 0 && dy <= 1) || L.terrain[j] == WATER)
						L.terrain[j] = GRASS;
				}
	// Small exposed landing fields make the first grain haul short enough to establish an
	// inn early in the opening. Their unseeded half is production
	// ground where an inn may be built beside grain; permanent courts remain on the larger keys.
	for (int team = 0; team < request.nbTeams; ++team)
		for (int sign : {-1, 1})
		{
			std::vector<int> corners;
			const int bottom = -12 + o.homeSize;
			for (int v = bottom + 9; v <= bottom + 14; ++v)
				for (int u = 14; u <= 23; ++u)
					corners.push_back(L.at(team, sign * u, v));
			auto field = stampContainedPlot(L.terrain, t, corners);
			const auto gate = L.point(team, 5, bottom);
			std::stable_sort(field.begin(), field.end(),
							 [&](int a, int b)
							 {
								 return t.dist2(a % t.w, a / t.w, int(gate.x), int(gate.y)) <
										t.dist2(b % t.w, b / t.w, int(gate.x), int(gate.y));
							 });
			L.landingFields.push_back(std::move(field));
		}
	layBeaches(L.terrain, t);
	const auto grass = pureTiles(L.terrain, t, GRASS);
	std::vector<unsigned char> landing(t.size());
	for (const auto &field : L.landingFields)
		for (int i : field)
			landing[i] = 1;
	for (size_t k = 0; k < L.keys.size(); ++k)
	{
		auto &key = L.keys[k];
		for (int dy = -key.radius - 2; dy <= key.radius + 2; ++dy)
			for (int dx = -key.radius - 2; dx <= key.radius + 2; ++dx)
			{
				const int i = t.at(int(key.centre.x) + dx, int(key.centre.y) + dy);
				bool ownCorners = true;
				for (int ey = 0; ey <= 1; ++ey)
					for (int ex = 0; ex <= 1; ++ex)
						ownCorners =
							ownCorners && L.keyOf[t.at(i % t.w + ex, i / t.w + ey)] == int(k);
				if (landing[i] || !ownCorners || !grass[i] || L.wall[i] || L.homeOf[i] >= 0 ||
					(std::abs(dx) <= 5 && std::abs(dy) <= 5))
					continue;
				// Wheat on three quadrants, wood on one. Terrain lanes part them for good.
				int timber = dx > 0 && dy < 0;
				if (key.owner >= 0)
				{
					const auto home = L.homes[key.owner];
					const int side = L.transpose ? t.offsetY(int(home.y), int(key.centre.y))
												 : t.offsetX(int(home.x), int(key.centre.x));
					const int u = L.transpose ? dy : dx, v = L.transpose ? dx : dy;
					timber = u * (side < 0 ? -1 : 1) > 0 && v < 0;
				}
				key.crops[timber].push_back(i);
				L.cropOf[i] = int(k) * 2 + timber;
			}
	}
	context.telemetry.choice("bastion-keys.fort.design",
							 std::vector<const char *>{"square-bastions", "round-bastions",
													   "diamond-bastions"}[L.fortDesign]);
	context.telemetry.measure("bastion-keys.fort.transposed", L.transpose);
	context.telemetry.measure("bastion-keys.fort.half-width", o.homeSize);
	context.telemetry.measure("bastion-keys.plantation.radius", o.plantationSize);
	context.telemetry.measure("bastion-keys.expansion.requested", wanted);
	context.telemetry.measure("bastion-keys.expansion.placed", placed);
	if (placed < wanted)
		context.telemetry.fallback("bastion-keys.expansion.capacity",
								   "Open sea could not fit every requested key");
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "bastion keys design";
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	const BastionKeysOptions o(context.request);
	const Torus &t = L.t;
	for (int k = 0; k < context.request.nbTeams; ++k)
		game.addTeam();
	writeUndermap(game.map, L.terrain);
	context.stage = "bastion keys walls";
	const auto walls = designedStone(game.map, t, L.wall);
	if (walls.gaps)
	{
		context.detail = "A bastion wall met a beach at " + std::to_string(walls.firstGap);
		return false;
	}
	for (int i = 0; i < t.size(); ++i)
		if (L.wall[i])
			game.map.setResource(i % t.w, i / t.w, STONE, 1);
	context.stage = "bastion keys colonies";
	for (int team = 0; team < context.request.nbTeams; ++team)
	{
		const auto home = homeGrassMask(game.map, t, L.homeOf, team);
		const auto pool = L.point(team, -7, -15);
		if (placeBuilding(game, team, "swimmingpool", 0, pool.x, pool.y, 2, home) < 0)
		{
			context.detail = "The harbour town has no room for its swimming pool.";
			return false;
		}
		const auto swarm = L.point(team, 3, -4);
		if (!placeSettlement(game, context, team, home,
							 MapGeneratorPoint(int(swarm.x) - 2, int(swarm.y) - 2),
							 "bastion-keys-settle"))
			return false;
	}
	context.stage = "bastion keys plantations";
	const auto fertility = Fertility::forMap(game.map, false);
	for (size_t f = 0; f < L.landingFields.size(); ++f)
	{
		int planted = 0;
		for (int i : L.landingFields[f])
			if (planted < 24 && fertility.values()[i] > 0 && game.map.isGrass(i % t.w, i / t.w))
			{
				game.map.setResource(i % t.w, i / t.w, WHEAT, 1);
				++planted;
			}
		context.telemetry.measure("bastion-keys.landing.wheat", planted, int(f));
		if (planted < 24)
		{
			context.detail = "A landing field cannot sustain its opening grain.";
			return false;
		}
	}
	for (size_t k = 0; k < L.keys.size(); ++k)
	{
		const auto &key = L.keys[k];
		for (int kind = 0; kind < 2; ++kind)
		{
			const int amount = kind ? o.wood : o.wheat;
			const int guarantee = key.owner >= 0 ? (kind ? 16 : 36) : 0;
			const int capacity =
				int(std::count_if(key.crops[kind].begin(), key.crops[kind].end(),
								  [&](int i) { return fertility.values()[i] > 0; }));
			// Keep the established default density where possible, but reserve a third of
			// the surplus for the upper control range. 0% retains the kit; 300% fills the
			// fertile plot. Interpolate before rounding so every endpoint is exact.
			const int surplus = std::max(0, capacity - guarantee);
			const int normal = std::min(int(key.crops[kind].size()) / 2, surplus * 2 / 3);
			const int wanted =
				guarantee + (amount <= 100 ? normal * amount / 100
										   : normal + (surplus - normal) * (amount - 100) / 200);
			int planted = 0;
			if (key.kind == 0)
			{
				const int resource = kind ? WOOD : WHEAT;
				if (key.owner >= 0)
				{
					const auto gate = L.point(key.owner, 6, 20);
					const auto eligible = [&](int i)
					{ return L.cropOf[i] == int(k) * 2 + kind && fertility.values()[i] > 0; };
					planted = growPatchesNear(game.map, t, int(gate.x), int(gate.y), 56, resource,
											  guarantee, eligible)
								  .tiles;
				}
				planted += plantContainedPlot(game.map, t, key.crops[kind], fertility, resource,
											  wanted - planted);
			}
			context.telemetry.measure(
				kind ? "bastion-keys.wood.target" : "bastion-keys.wheat.target", wanted, int(k));
			context.telemetry.measure(
				kind ? "bastion-keys.wood.planted" : "bastion-keys.wheat.planted", planted, int(k));
			if (key.owner >= 0 && planted < guarantee)
			{
				context.detail = "An exposed plantation cannot sustain its starter crops.";
				return false;
			}
		}
		if (key.kind != 0)
		{
			int planted = 0;
			const int wanted =
				scaledCount(key.kind == 1 ? 15 : 12, key.kind == 1 ? o.fruit : o.stone);
			for (int i : key.crops[0])
			{
				if (planted >= wanted)
					break;
				game.map.setResource(i % t.w, i / t.w, key.kind == 1 ? CHERRY + planted % 3 : STONE,
									 1);
				++planted;
			}
		}
	}
	seedAlgae(game.map, context, t, "bastion-keys-algae", o.algae, AlgaeBand::shallows(1, 4));
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (const auto error = designMismatch(L, game.map, "bastion keys"); !error.empty())
		return error;
	const Torus &t = L.t;
	auto growthGround = pureTiles(game.map, GRASS);
	std::vector<unsigned char> cropSeeds(t.size());
	for (int i = 0; i < t.size(); ++i)
	{
		const int resource = game.map.getResource(i).type;
		cropSeeds[i] = resource == WHEAT || resource == WOOD;
		// Eternal stone is a physical growth barrier; ignore buildings and harvestable crops.
		if (resource == STONE)
			growthGround[i] = 0;
	}
	const auto spread = floodFrom(t, cropSeeds, growthGround);
	for (int i : spread.visited)
		if (L.homeOf[i] >= 0)
			return "Crops can spread into a harbour town.";
	for (const auto &key : L.keys)
		for (int i : key.court)
			if (spread.steps[i] >= 0)
				return "Crops can spread into an island service court.";
	if (const auto error = wallStanding(game.map, t, L.wall, L.gates, "bastion ramparts");
		!error.empty())
		return error;
	if (const auto error = startingAccessFailure(
			game.map, context.request.nbTeams,
			{{WHEAT, 28, "plantation wheat"}, {WOOD, 36, "plantation timber"}}, 64, 24);
		!error.empty())
		return error;
	// Even cleared estates must be islands: crops and buildings cannot disguise a land
	// bridge. Eight-neighbour components include diagonal beach contacts and torus seams.
	std::vector<unsigned char> clearedGround(t.size());
	for (int i = 0; i < t.size(); ++i)
		clearedGround[i] =
			!game.map.isWater(i % t.w, i / t.w) && game.map.getResource(i).type != STONE;
	const auto regions = connectedRegions(clearedGround, t.w, t.h, true, GridNeighbors::Eight);
	const auto ownership = labelComponents(regions, L.homeOf);
	if (ownership.conflictTile >= 0)
		return "Two harbour estates have a walking connection.";
	for (const auto &key : L.keys)
		for (int i : key.court)
			if (regions[i] < 0 || ownership.owners[regions[i]] != key.owner)
				return "An island service court has the wrong walking connection.";
	const auto workers = unitTilesByTeam(game.map, context.request.nbTeams);
	const auto swimming =
		stepsFrom(t, tileMask(t, workers.front()), groundUnitTiles(game.map, true));
	if (firstColonyCutOff(swimming, workers) >= 0)
		return "A harbour estate cannot be reached by swimming.";
	for (const auto &key : L.keys)
		if (std::none_of(key.court.begin(), key.court.end(),
						 [&](int i) { return swimming[i] >= 0; }))
			return "An outlying island cannot be reached by swimming.";
	for (const auto &field : L.landingFields)
		if (std::count_if(field.begin(), field.end(),
						  [&](int i) { return game.map.getResource(i).type == WHEAT; }) < 24)
			return "An exposed landing field lost its opening grain.";
	// Closing the two gate plugs must isolate the courtyard even for swimmers. Ignore buildings
	// and clearable crops: neither may hide a defect in a permanent enclosure.
	std::vector<unsigned char> swim(t.size());
	for (int i = 0; i < t.size(); ++i)
		swim[i] = !(game.map.getResource(i).type == STONE) && !L.gates[i];
	for (int team = 0; team < context.request.nbTeams; ++team)
	{
		if (countBuildings(game, team, "swimmingpool") != 1)
			return "A harbour town lost its swimming pool.";
		const auto reach = reachFrom(t, {L.at(team, 0, -12)}, swim, t.size());
		for (int i : reach.tiles)
			if (L.homeOf[i] != team)
				return "A swimmer can bypass a harbour fort's gates.";
		int wheat = 0, wood = 0;
		for (int i = 0; i < t.size(); ++i)
			if (L.homeOf[i] == team &&
				((game.map.getResource(i).type == WHEAT) || (game.map.getResource(i).type == WOOD)))
				return "Production escaped into a harbour town.";
		for (const auto &key : L.keys)
			if (key.owner == team)
			{
				for (int i : key.crops[0])
					wheat += (game.map.getResource(i).type == WHEAT);
				for (int i : key.crops[1])
					wood += (game.map.getResource(i).type == WOOD);
			}
		if (wheat < 72 || wood < 32)
			return "The external starter plantations lost their crops.";
	}
	return {};
}
} // namespace
BastionKeysOptions::BastionKeysOptions(const GenerationRequest &r)
	: homeSize(r.option("home-size")), plantationSize(r.option("plantation-size")),
	  outerIslands(r.option("outer-islands")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition bastionKeysDefinition()
{
	return {"bastion-keys",
			67,
			"Bastion Keys",
			1,
			false,
			{{"home-size", "Home size", 13, 15, 1, 14, ControlGroup::Layout},
			 {"plantation-size", "Plantation size", 14, 18, 2, 14, ControlGroup::Layout},
			 {"outer-islands", "Outlying islands", 1, 5, 1, 3, ControlGroup::Layout},
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			requestFailure,
			validateWorld,
			{"terrain:stronghold", "feature:islands", "feature:ocean", "feature:stone-walls",
			 "style:siege", "style:fortified"}};
}
