// SPDX-License-Identifier: GPL-3.0-or-later
#include "EmojiGenerator.h"
#include "Drawing.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Room.h"
#include "Roads.h"
#include "Topology.h"
#include "Sketch.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
using namespace MapGeneration;

// An emoji lagoon or island. Colonies are seated on existing grass after the entire terrain
// is finished: no home pad, pond, beach or connecting road is painted for a player. Shore
// fertility and building clearance constrain the sites, then shared point dispersion spreads
// them out. Resource reservations protect the opening only; later clearing is part of play.
namespace
{
constexpr int kStartClearance = 10;
constexpr int kCloseClearance = 6;
constexpr int kCloseRange = 10;
constexpr int kCloseWheat = 24;
constexpr int kCloseWood = 16;
constexpr int kSiteRefinementRadius = 10;
constexpr int kFoodSearchRadius = 12;
constexpr int kMinimumStartSpacing = 28;
constexpr int kMinimumBuildingOrigins = 32;
constexpr int kStarterCrop = 48;
constexpr const char *kCharacters[] = {"smile",      "sad",        "wink",  "surprised",
									   "heart-eyes", "sunglasses", "heart", "star"};
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<unsigned char> roads;
	std::string failure;
};

// All artwork is native mask geometry, independent of installed fonts and emoji artwork.
std::vector<unsigned char> glyph(const Torus &t, int character, bool outline, double radius,
								 double thickness)
{
	std::vector<unsigned char> body(t.size(), 0), features(t.size(), 0);
	const double cx = t.w / 2.0, cy = t.h / 2.0;
	if (character < 6)
	{
		for (int i = 0; i < t.size(); ++i)
			body[i] = std::hypot(i % t.w - cx, i / t.w - cy) <= radius;
	}
	else
	{
		std::vector<SubtilePoint> polygon;
		const int points = character == 6 ? 120 : 10;
		for (int j = 0; j < points; ++j)
		{
			const double a = 2 * kPi * j / points;
			double x, y;
			if (character == 6)
			{
				x = std::pow(std::sin(a), 3);
				y = -(13 * std::cos(a) - 5 * std::cos(2 * a) - 2 * std::cos(3 * a) -
					  std::cos(4 * a)) /
					16;
			}
			else
			{
				const double reach = j % 2 ? 0.46 : 1.0;
				x = reach * std::cos(a - kPi / 2);
				y = reach * std::sin(a - kPi / 2);
			}
			polygon.push_back({std::llround((cx + x * radius) * kSubtile),
							   std::llround((cy + y * radius) * kSubtile)});
		}
		fillPolygon(body, t, polygon);
	}
	if (outline)
	{
		const auto inside = erode(t, body, int(std::ceil(thickness)));
		for (int i = 0; i < t.size(); ++i)
			body[i] = body[i] && !inside[i];
	}
	if (character >= 6)
		return body;
	const auto stroke = [&](std::vector<StrokePoint> path)
	{
		for (auto &p : path)
		{
			p.x = cx + p.x * radius;
			p.y = cy + p.y * radius;
			p.halfWidth *= radius;
		}
		strokePath(features, t, path);
	};
	const auto oval = [&](double x, double y, double rx, double ry)
	{
		for (int i = 0; i < t.size(); ++i)
		{
			const double dx = ((i % t.w - cx) / radius - x) / rx,
						 dy = ((i / t.w - cy) / radius - y) / ry;
			if (dx * dx + dy * dy <= 1)
				features[i] = 1;
		}
	};
	if (character == 4)
	{
		for (double x : {-0.36, 0.36})
		{
			oval(x - 0.08, -0.29, 0.12, 0.13);
			oval(x + 0.08, -0.29, 0.12, 0.13);
			stroke({{x - 0.14, -0.27, 0.08}, {x, -0.08, 0.06}, {x + 0.14, -0.27, 0.08}});
		}
	}
	else if (character == 5)
	{
		stroke({{-0.76, -0.29, 0.055}, {0.76, -0.29, 0.055}});
		stroke({{-0.53, -0.25, 0.15}, {-0.30, -0.25, 0.15}});
		stroke({{0.30, -0.25, 0.15}, {0.53, -0.25, 0.15}});
	}
	else
	{
		oval(-0.34, -0.28, 0.095, 0.14);
		if (character == 2)
			stroke({{0.20, -0.23, 0.045}, {0.34, -0.31, 0.045}, {0.48, -0.23, 0.045}});
		else
			oval(0.34, -0.28, 0.095, 0.14);
	}
	if (character == 3)
		oval(0, 0.37, 0.15, 0.21);
	else
	{
		std::vector<StrokePoint> mouth;
		for (int j = 0; j <= 32; ++j)
		{
			const double x = -0.49 + 0.98 * j / 32;
			const double y = character == 1 ? 0.23 + 0.85 * x * x : 0.52 - 0.85 * x * x;
			mouth.push_back({x, y, 0.05});
		}
		stroke(mouth);
	}
	// Features are negative space in a filled face, and ink in an outlined face.
	for (int i = 0; i < t.size(); ++i)
		body[i] = outline ? (body[i] || features[i]) : (body[i] && !features[i]);
	return body;
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const EmojiOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	if (t.w != t.h || t.w < 256 || request.nbTeams > 8)
	{
		L.failure = "Emoji needs a square map of at least 256 tiles and at most 8 colonies.";
		return L;
	}
	const double cx = t.w / 2.0, cy = t.h / 2.0, radius = 0.26 * t.w;
	const int character =
		o.character == 0 ? int(context.bounded("emoji-character", 8)) : o.character - 1;
	const bool outline = o.outline == 0 ? context.bounded("emoji-style", 2) != 0 : o.outline == 1;
	const bool inverse = o.inverse == 0 ? context.bounded("emoji-terrain", 2) != 0 : o.inverse == 2;
	// An inverse outline is the only land: a rim ribbon every colony's town, farm and front share.
	// Rotation tournaments found the rim start beside a facial-feature junction losing every game;
	// drawing the rim at a quarter of the radius instead (16 corners at 256) changed neither the
	// eliminations (36 against 32 in 96 colony-games on the same six seeds) nor the position bias,
	// so the width stays at what the drawing wants and the exposure is the variant's character.
	const double thickness = inverse ? std::max(14.0, 0.19 * radius) : std::max(7.0, 0.11 * radius);
	auto ink = glyph(t, character, outline, radius, thickness);
	L.terrain.resize(t.size());
	L.roads.assign(t.size(), 0);
	int inkCorners = 0;
	for (int i = 0; i < t.size(); ++i)
	{
		inkCorners += ink[i];
		L.terrain[i] = (bool(ink[i]) != inverse) ? WATER : GRASS;
	}
	// A narrow sand spine through a generous land bypass. No crops or buildings can close it.
	auto ring = arcPath(cx, cy, 0.30 * t.w, 0, 2 * kPi, 6);
	std::vector<unsigned char> land(t.size(), 0);
	strokePath(land, t, ring, 1, true);
	for (auto &p : ring)
		p.halfWidth = 1.5;
	strokePath(L.roads, t, ring, 1, true);
	const double phase = (context.bounded("emoji-crossings", 16) + 0.5) * kPi / 16;
	for (int j = 0; j < o.crossings; ++j)
	{
		const double a = phase + 2 * kPi * j / o.crossings;
		const auto from = polarPoint(cx, cy, 0.30 * t.w, a),
				   to = polarPoint(cx, cy, 0.55 * radius, a);
		std::vector<StrokePoint> bridge{{from.x, from.y, 4}, {to.x, to.y, 4}};
		strokePath(land, t, bridge);
		for (auto &p : bridge)
			p.halfWidth = 1.5;
		strokePath(L.roads, t, bridge);
	}
	for (int i = 0; i < t.size(); ++i)
		if (land[i])
			L.terrain[i] = L.roads[i] ? SAND : GRASS;
	layBeaches(L.terrain, t);
	context.telemetry.choice("emoji.character", kCharacters[character]);
	context.telemetry.choice("emoji.style", outline ? "outline" : "filled");
	context.telemetry.choice("emoji.terrain", inverse ? "grass-ink" : "water-ink");
	context.telemetry.measure("emoji.stroke.width-corners", thickness);
	context.telemetry.measure("emoji.ink.corners", inkCorners);
	context.telemetry.measure("emoji.crossings.requested", o.crossings);
	context.telemetry.measure("emoji.crossings.placed", o.crossings);
	context.telemetry.measure("emoji.crossings.phase-radians", phase);
	context.telemetry.measure("emoji.home.crop-target-tiles", kStarterCrop);
	return L;
}

// Choose from the finished terrain. Converting the eligible mask to an area grid lets the
// existing whole-region dispersion search do the spacing without modifying the landscape.
std::vector<MapGeneratorPoint> existingStarts(Map &map, const Layout &L,
											  const Fertility::Field &fertility,
											  GenerationContext &context)
{
	const Torus &t = L.t;
	const auto grass = pureTiles(L.terrain, t, GRASS);
	const auto roomy = buildAnchors(t, grass, 8);
	const auto open = walkableTiles(map);
	const auto labels = connectedRegions(open, t.w, t.h, true, GridNeighbors::Eight);
	std::vector<int> sizes(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		if (open[i] && labels[i] >= 0)
			++sizes[labels[i]];
	const int main = int(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
	const auto water = pureTiles(L.terrain, t, WATER);
	const auto shore = stepsFrom(t, water);
	std::vector<int> eligible(t.size(), 1);
	int candidates = 0;
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = t.at(x, y), centre = t.at(x + 2, y + 2);
			if (!roomy[t.at(x - 2, y - 2)] || labels[i] != main || shore[centre] > 14)
				continue;
			// Require actual fertile grass around the town, not merely a nearby sand shoreline.
			std::uint64_t food = 0;
			for (int dy = -10; dy <= 13; dy += 3)
				for (int dx = -10; dx <= 13; dx += 3)
				{
					const int j = t.at(x + dx, y + dy);
					if (grass[j])
						food += fertility.at(j % t.w, j / t.w);
				}
			if (food < 12000)
				continue;
			eligible[i] = 0;
			++candidates;
		}
	context.telemetry.measure("emoji.starts.eligible", candidates);
	if (candidates < context.request.nbTeams)
	{
		context.detail = "The unchanged emoji has too few roomy shoreline sites.";
		return {};
	}
	std::vector<MapGeneratorPoint> sites(context.request.nbTeams, MapGeneratorPoint(0, 0));
	std::vector<int> weights(sites.size(), 1);
	const int spacing =
		splitUpPoints(map, context, eligible, 0, sites, weights, PointSearch::WholeRegion, 64);
	context.telemetry.measure("emoji.starts.spacing-tiles", spacing);
	if (spacing < kMinimumStartSpacing)
	{
		context.detail = "The unchanged emoji cannot separate this many colonies; use fewer "
						 "colonies or a larger map.";
		return {};
	}
	// Refine dispersed sites using the best nearby growing area. A distant fertile shore
	// passed the old geometric check but left some colonies unable to feed and reproduce.
	// This is a terrain heuristic; actual crop access and AI openings still need validation.
	const auto anchors = buildAnchors(t, grass);
	for (int k = 0; k < int(sites.size()); ++k)
	{
		const auto original = sites[k];
		MapGeneratorPoint selected = original;
		int bestScore = -1, bestShift = 100000;
		for (int oy = -kSiteRefinementRadius; oy <= kSiteRefinementRadius; ++oy)
			for (int ox = -kSiteRefinementRadius; ox <= kSiteRefinementRadius; ++ox)
			{
				const int x = t.x(original.x + ox), y = t.y(original.y + oy);
				if (eligible[t.at(x, y)] != 0)
					continue;
				bool separated = true;
				for (int j = 0; j < int(sites.size()); ++j)
					if (j != k && t.dist2(x, y, sites[j].x, sites[j].y) <
									  kMinimumStartSpacing * kMinimumStartSpacing)
						separated = false;
				if (!separated)
					continue;
				std::vector<int> growing;
				int room = 0;
				for (int dy = -kFoodSearchRadius; dy <= kFoodSearchRadius; ++dy)
					for (int dx = -kFoodSearchRadius; dx <= kFoodSearchRadius; ++dx)
					{
						const int i = t.at(x + 2 + dx, y + 2 + dy);
						room += anchors[i];
						if (dx * dx + dy * dy > kCloseClearance * kCloseClearance && grass[i])
							growing.push_back(fertility.at(i % t.w, i / t.w));
					}
				std::sort(growing.begin(), growing.end(), std::greater<int>());
				int supply = 0;
				for (int i = 0; i < std::min(kStarterCrop, int(growing.size())); ++i)
					supply += growing[i];
				// Reward a complete local crop budget and some redundant building room.
				// Capping fertility made unequal shoreline sites indistinguishable in playtests.
				const int score = supply + 500 * std::min(room, 200);
				const int shift = ox * ox + oy * oy;
				if (score > bestScore || (score == bestScore && shift < bestShift))
				{
					bestScore = score;
					bestShift = shift;
					selected = MapGeneratorPoint(x, y);
				}
			}
		sites[k] = selected;
		context.telemetry.measure("emoji.starts.refinement-score", bestScore, k);
	}
	dealStarts(context, sites, "emoji-start-deal");
	context.telemetry.measure("emoji.starts.placed", sites.size());
	return sites;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "emoji layout";
	const EmojiOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	Map &map = game.map;
	writeUndermap(map, L.terrain);
	const auto fertility = Fertility::forMap(map, false);
	context.stage = "emoji existing-land starts";
	const auto sites = existingStarts(map, L, fertility, context);
	if (sites.empty())
		return false;
	for (int k = 0; k < context.request.nbTeams; ++k)
		game.addTeam();
	const auto grass = pureTiles(L.terrain, t, GRASS);
	if (!settleColonies(
			game, context, "emoji-starts", [&](int) { return grass; },
			[&](int k) { return sites[k]; }))
		return false;
	// Reserve existing grass for initial construction; this mask never changes terrain or growth flags.
	std::vector<unsigned char> reserved(t.size(), 0), starterReserved(t.size(), 0);
	for (const auto &p : sites)
		for (int dy = -kStartClearance; dy <= kStartClearance; ++dy)
			for (int dx = -kStartClearance; dx <= kStartClearance; ++dx)
				if (dx * dx + dy * dy <= kStartClearance * kStartClearance)
				{
					reserved[t.at(p.x + 2 + dx, p.y + 2 + dy)] = 1;
					if (dx * dx + dy * dy <= kCloseClearance * kCloseClearance)
						starterReserved[t.at(p.x + 2 + dx, p.y + 2 + dy)] = 1;
				}
	const auto units = unitTilesByTeam(map, context.request.nbTeams);
	const auto bare = walkableTiles(map);
	context.stage = "emoji shoreline starter crops";
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		const auto walk = stepsFrom(t, tileMask(t, units[k]), bare);
		int first = -1;
		for (int type : {WHEAT, WOOD})
		{
			const auto eligible = [&](int i)
			{
				return walk[i] >= 0 && walk[i] <= 24 && !reserved[i] &&
					   fertility.at(i % t.w, i / t.w) > 0 && clearGround(map, i % t.w, i / t.w) &&
					   (first < 0 || t.dist2(first % t.w, first / t.w, i % t.w, i / t.w) >= 64);
			};
			int placed = 0, firstSeed = -1;
			// Follow the natural shore in several patches when a bay or narrow strip
			// ends a patch. Never enlarge it by changing the terrain.
			while (placed < kStarterCrop)
			{
				int seed = -1;
				double best = -1;
				for (int i = 0; i < t.size(); ++i)
					if (eligible(i))
					{
						const double score = double(fertility.at(i % t.w, i / t.w)) / (8 + walk[i]);
						if (score > best)
						{
							best = score;
							seed = i;
						}
					}
				if (seed < 0)
					break;
				if (firstSeed < 0)
					firstSeed = seed;
				const int added = growPatch(map, t, seed, type, kStarterCrop - placed, eligible);
				if (!added)
					break;
				placed += added;
			}
			context.telemetry.measure(
				type == WHEAT ? "emoji.home.wheat-placed" : "emoji.home.wood-placed", placed, k);
			if (placed < 24)
			{
				context.detail = "The unchanged shoreline cannot fit sufficient starter crops.";
				return false;
			}
			if (placed < kStarterCrop)
				context.telemetry.fallback("emoji.starts.crop-patch-limited",
										   "Shoreline bounds the starter patch", k);
			if (type == WHEAT)
				first = firstSeed;
		}
	}
	// A short supply line supplements the renewable shore farm. It does not replace it.
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		const auto walk = stepsFrom(t, tileMask(t, units[k]), bare);
		for (int type : {WHEAT, WOOD})
		{
			const auto eligible = [&](int i)
			{
				return walk[i] >= 0 && walk[i] <= kCloseRange && !starterReserved[i] &&
					   fertility.at(i % t.w, i / t.w) > 0 && clearGround(map, i % t.w, i / t.w);
			};
			const int target = type == WHEAT ? kCloseWheat : kCloseWood;
			int placed = 0;
			while (placed < target)
			{
				int seed = -1;
				double best = -1;
				for (int i = 0; i < t.size(); ++i)
					if (eligible(i))
					{
						const double score = double(fertility.at(i % t.w, i / t.w)) / (4 + walk[i]);
						if (score > best)
						{
							best = score;
							seed = i;
						}
					}
				if (seed < 0)
					break;
				const int added = growPatch(map, t, seed, type, target - placed, eligible);
				if (!added)
					break;
				placed += added;
			}

			context.telemetry.measure(
				type == WHEAT ? "emoji.home.close-wheat-placed" : "emoji.home.close-wood-placed",
				placed, k);
		}
	}
	const auto patch = periodicNoise(t.w, t.h, 10, context.stream("emoji-fields"));
	const auto split = periodicNoise(t.w, t.h, 6, context.stream("emoji-crops"));
	furnishGround(
		map, t, context, fertility,
		[&](int i) { return !reserved[i] && !L.roads[i] && clearGround(map, i % t.w, i / t.w); },
		[&](int i) { return float(patch[i]); }, [&](int i) { return split[i]; },
		[&](int area)
		{
			return GroundAmounts{
				int(scaledCount(area / 16, o.wheat)), int(scaledCount(area / 24, o.wood)),
				int(scaledCount(area / 900, o.stone)), int(scaledCount(area / 1100, o.fruit))};
		},
		"emoji-quarries", "emoji-orchards");
	seedAlgae(map, context, t, "emoji-algae", o.algae, AlgaeBand::shallows(1, 6, 60));
	// Only deposits may be cleared. openRoad cannot paint a ford or edit any terrain corner.
	for (int k = 1; k < context.request.nbTeams; ++k)
		if (!openRoad(map, t, units[0], tileMask(t, units[k])))
		{
			context.detail = "Colonies cannot connect over the unchanged emoji terrain.";
			return false;
		}
	secureStartingCrops(game, context, t);
	if (o.wheat != 100 || o.wood != 100 || o.stone != 100 || o.algae != 100 || o.fruit != 100)
	{
		// Dense optional deposits can consume the remaining building origins. Clear only
		// resources, with headroom for the subsequent crop guarantee; terrain stays intact.
		openCrampedStarts(game, context, kMinimumBuildingOrigins + 16, 24);
		secureStartingCrops(game, context, t);
	}
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (auto error = designMismatch(L, game.map, "emoji"); !error.empty())
		return error;
	// Stronger than checking the silhouette: every single undermap corner must survive settlement.
	for (int i = 0; i < L.t.size(); ++i)
		if (game.map.getUMTerrain(i % L.t.w, i / L.t.w) != L.terrain[i])
			return "Emoji terrain changed during colony placement.";
	const auto anchors = buildAnchors(L.t, buildableTiles(game.map));
	const auto units = unitTilesByTeam(game.map, context.request.nbTeams);
	const auto open = walkableTiles(game.map);
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		const auto walk = floodFrom(L.t, tileMask(L.t, units[k]), open, 24);
		int room = 0;
		bool wheat = false, wood = false;
		for (int i : walk.visited)
		{
			room += anchors[i];
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int type =
						game.map.getResource(L.t.x(i % L.t.w + dx), L.t.y(i / L.t.w + dy)).type;
					wheat |= type == WHEAT;
					wood |= type == WOOD;
				}
		}
		if (room < kMinimumBuildingOrigins)
			return "Emoji start has insufficient existing construction room.";
		if (!wheat || !wood)
			return "Emoji start cannot reach both crops on the unchanged terrain.";
	}
	return walkFromFirstColony(game.map, context.request.nbTeams, "emoji",
							   "along the existing land")
		.error;
}
} // namespace
EmojiOptions::EmojiOptions(const GenerationRequest &r)
	: character(r.option("character")), outline(r.option("outline")), inverse(r.option("inverse")),
	  crossings(r.option("crossings")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition emojiDefinition()
{
	return {
		"emoji",
		34,
		"Emoji",
		9,
		false,
		{GeneratorControl::choice("character", "Emoji character",
								  {"Random", "Smiley", "Sad face", "Winking face", "Surprised face",
								   "Heart eyes", "Sunglasses", "Heart", "Star"},
								  0, ControlGroup::Terrain),
		 GeneratorControl::choice("outline", "Emoji style", {"Random", "Outline", "Filled"}, 0,
								  ControlGroup::Terrain),
		 GeneratorControl::choice("inverse", "Emoji terrain", {"Random", "Regular", "Inverse"}, 0,
								  ControlGroup::Terrain),
		 {"crossings", "Crossings", 2, 8, 2, 4, ControlGroup::Layout},
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
