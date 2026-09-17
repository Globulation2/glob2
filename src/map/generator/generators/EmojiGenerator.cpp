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
// Append new characters: an explicit `character` option stores the position in this list.
enum Character
{
	Smile, Sad, Wink, Surprised, HeartEyes, Sunglasses, Heart, Star, Neutral, Sleeping, Grin, Kissing,
	Angry, TongueOut, Skull, Flower, Clover, Teardrop, Cloud, Gem, Apple, SpeechBubble, Shield,
	CharacterCount
};
constexpr const char *kCharacters[CharacterCount] = {
	"smile",   "sad",        "wink",       "surprised", "heart-eyes", "sunglasses",
	"heart",   "star",       "neutral",    "sleeping",  "grin",       "kissing",
	"angry",   "tongue-out", "skull",      "flower",    "clover",     "teardrop",
	"cloud",   "gem",        "apple",      "speech-bubble", "shield"};
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<unsigned char> roads;
	std::string failure;
};

// All artwork is native mask geometry, independent of installed fonts and emoji artwork.
// Shapes are drawn in units of the radius about the map centre, with y pointing down.
std::vector<unsigned char> glyph(const Torus &t, int character, bool outline, double radius,
								 double thickness)
{
	std::vector<unsigned char> body(t.size(), 0), features(t.size(), 0);
	const double cx = t.w / 2.0, cy = t.h / 2.0;
	const auto fill = [&](std::vector<unsigned char> &mask,
						  const std::function<bool(double, double)> &inside)
	{
		for (int i = 0; i < t.size(); ++i)
			if (inside((i % t.w - cx) / radius, (i / t.w - cy) / radius))
				mask[i] = 1;
	};
	const auto polygon = [&](std::vector<unsigned char> &mask,
							 const std::vector<std::pair<double, double>> &points)
	{
		std::vector<SubtilePoint> outline;
		for (const auto &[x, y] : points)
			outline.push_back({std::llround((cx + x * radius) * kSubtile),
							   std::llround((cy + y * radius) * kSubtile)});
		fillPolygon(mask, t, outline);
	};
	const auto stroke = [&](std::vector<unsigned char> &mask, std::vector<StrokePoint> path)
	{
		for (auto &p : path)
		{
			p.x = cx + p.x * radius;
			p.y = cy + p.y * radius;
			p.halfWidth *= radius;
		}
		strokePath(mask, t, path);
	};
	const auto disc = [](double u, double v, double x, double y, double r)
	{ return (u - x) * (u - x) + (v - y) * (v - y) <= r * r; };
	const auto roundedBox = [](double u, double v, double x, double y, double hw, double hh, double r)
	{
		const double dx = std::max(std::abs(u - x) - (hw - r), 0.0),
					 dy = std::max(std::abs(v - y) - (hh - r), 0.0);
		return std::abs(u - x) <= hw && std::abs(v - y) <= hh && dx * dx + dy * dy <= r * r;
	};
	switch (character)
	{
	case Skull:
		fill(body, [&](double u, double v)
			 { return disc(u, v, 0, -0.12, 0.86) || roundedBox(u, v, 0, 0.45, 0.50, 0.42, 0.15); });
		break;
	case Heart:
	case Star:
	{
		std::vector<std::pair<double, double>> points;
		const int count = character == Heart ? 120 : 10;
		for (int j = 0; j < count; ++j)
		{
			const double a = 2 * kPi * j / count;
			if (character == Heart)
				points.push_back({std::pow(std::sin(a), 3), -(13 * std::cos(a) - 5 * std::cos(2 * a) -
															  2 * std::cos(3 * a) - std::cos(4 * a)) /
																16});
			else
			{
				const double reach = j % 2 ? 0.46 : 1.0;
				points.push_back({reach * std::cos(a - kPi / 2), reach * std::sin(a - kPi / 2)});
			}
		}
		polygon(body, points);
		break;
	}
	case Flower:
	{
		// Five petals that meet well outside the centre, so every petal stays one piece of land.
		std::vector<std::pair<double, double>> points;
		for (int j = 0; j < 180; ++j)
		{
			const double a = 2 * kPi * j / 180, reach = 0.62 + 0.38 * std::abs(std::cos(2.5 * a));
			points.push_back({reach * std::cos(a - kPi / 2), reach * std::sin(a - kPi / 2)});
		}
		polygon(body, points);
		break;
	}
	case Clover:
		fill(body, [&](double u, double v)
			 {
				 return disc(u, v, 0, -0.08, 0.30) || disc(u, v, -0.34, -0.42, 0.38) ||
						disc(u, v, 0.34, -0.42, 0.38) || disc(u, v, -0.34, 0.26, 0.38) ||
						disc(u, v, 0.34, 0.26, 0.38);
			 });
		stroke(body, {{0, 0.42, 0.09}, {0.10, 0.72, 0.08}, {0.26, 0.94, 0.07}});
		break;
	case Teardrop:
	{
		std::vector<std::pair<double, double>> points;
		for (int j = 0; j < 120; ++j)
		{
			const double a = 2 * kPi * j / 120;
			points.push_back({std::sin(a) * std::sin(a / 2), -0.98 * std::cos(a)});
		}
		polygon(body, points);
		break;
	}
	case Cloud:
		fill(body, [&](double u, double v)
			 {
				 return disc(u, v, -0.55, 0.24, 0.34) || disc(u, v, -0.22, -0.02, 0.40) ||
						disc(u, v, 0.25, -0.10, 0.48) || disc(u, v, 0.60, 0.24, 0.34) ||
						(u >= -0.55 && u <= 0.60 && v >= 0.10 && v <= 0.58);
			 });
		break;
	case Gem:
		polygon(body, {{-0.45, -0.65}, {0.45, -0.65}, {0.95, -0.18}, {0, 0.92}, {-0.95, -0.18}});
		break;
	case Apple:
		fill(body, [&](double u, double v)
			 {
				 // The leaf is an ellipse tilted up and away from the stem.
				 const double c = std::cos(-0.44), s = std::sin(-0.44), lu = u - 0.30, lv = v + 0.68;
				 const double along = (lu * c + lv * s) / 0.25, across = (-lu * s + lv * c) / 0.11;
				 return disc(u, v, -0.33, 0.10, 0.60) || disc(u, v, 0.33, 0.10, 0.60) ||
						along * along + across * across <= 1;
			 });
		stroke(body, {{0, -0.32, 0.06}, {0.06, -0.80, 0.05}});
		break;
	case SpeechBubble:
		fill(body, [&](double u, double v) { return roundedBox(u, v, 0, -0.15, 0.95, 0.58, 0.32); });
		polygon(body, {{-0.55, 0.30}, {-0.10, 0.30}, {-0.65, 0.95}});
		break;
	case Shield:
	{
		// Straight sides curving to a point: a quadratic Bezier on each side.
		std::vector<std::pair<double, double>> points{{-0.82, -0.92}, {0.82, -0.92}};
		for (int j = 0; j <= 24; ++j)
		{
			const double s = j / 24.0;
			points.push_back({0.82 * (1 - s * s), -0.07 + 1.24 * s - 0.24 * s * s});
		}
		for (int j = 23; j >= 0; --j)
		{
			const double s = j / 24.0;
			points.push_back({-0.82 * (1 - s * s), -0.07 + 1.24 * s - 0.24 * s * s});
		}
		polygon(body, points);
		break;
	}
	default:
		for (int i = 0; i < t.size(); ++i)
			body[i] = std::hypot(i % t.w - cx, i / t.w - cy) <= radius;
	}
	if (outline)
	{
		const auto inside = erode(t, body, int(std::ceil(thickness)));
		for (int i = 0; i < t.size(); ++i)
			body[i] = body[i] && !inside[i];
	}
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
	const auto mouth = [&](bool frown, double lift = 0)
	{
		std::vector<StrokePoint> path;
		for (int j = 0; j <= 32; ++j)
		{
			const double x = -0.49 + 0.98 * j / 32;
			path.push_back({x, (frown ? 0.23 + 0.85 * x * x : 0.52 - 0.85 * x * x) - lift, 0.05});
		}
		stroke(features, path);
	};
	// Keep feature land within about 0.55 of the radius: crossings stop short of that, so a larger
	// eye, tongue or facet island would get a causeway and a cramped colony of its own.
	const auto eyes = [&] { oval(-0.34, -0.28, 0.095, 0.14), oval(0.34, -0.28, 0.095, 0.14); };
	switch (character)
	{
	case Smile:
	case Sad:
	case Surprised:
		eyes();
		break;
	case Wink:
		oval(-0.34, -0.28, 0.095, 0.14);
		stroke(features, {{0.20, -0.23, 0.045}, {0.34, -0.31, 0.045}, {0.48, -0.23, 0.045}});
		break;
	case HeartEyes:
		for (double x : {-0.36, 0.36})
		{
			oval(x - 0.08, -0.29, 0.12, 0.13);
			oval(x + 0.08, -0.29, 0.12, 0.13);
			stroke(features, {{x - 0.14, -0.27, 0.08}, {x, -0.08, 0.06}, {x + 0.14, -0.27, 0.08}});
		}
		break;
	case Sunglasses:
		stroke(features, {{-0.76, -0.29, 0.055}, {0.76, -0.29, 0.055}});
		stroke(features, {{-0.53, -0.25, 0.15}, {-0.30, -0.25, 0.15}});
		stroke(features, {{0.30, -0.25, 0.15}, {0.53, -0.25, 0.15}});
		break;
	case Neutral:
		eyes();
		stroke(features, {{-0.38, 0.40, 0.05}, {0.38, 0.40, 0.05}});
		break;
	case Sleeping:
		for (double x : {-0.30, 0.30})
			stroke(features, {{x - 0.12, -0.29, 0.045}, {x, -0.22, 0.045}, {x + 0.12, -0.29, 0.045}});
		oval(0, 0.45, 0.09, 0.07);
		break;
	case Grin:
		for (double x : {-0.30, 0.30})
			stroke(features, {{x - 0.12, -0.22, 0.045}, {x, -0.33, 0.045}, {x + 0.12, -0.22, 0.045}});
		// A wide open D-shaped mouth: the lower half of an ellipse.
		fill(features, [](double u, double v)
			 {
				 const double du = u / 0.50, dv = (v - 0.16) / 0.40;
				 return v >= 0.16 && du * du + dv * dv <= 1;
			 });
		break;
	case Kissing:
		eyes();
		oval(0.08, 0.42, 0.08, 0.11);
		break;
	case Angry:
		eyes();
		stroke(features, {{-0.58, -0.62, 0.05}, {-0.16, -0.50, 0.05}});
		stroke(features, {{0.16, -0.50, 0.05}, {0.58, -0.62, 0.05}});
		mouth(true);
		break;
	case TongueOut:
		eyes();
		mouth(false, 0.16);
		oval(0.12, 0.42, 0.09, 0.08);
		break;
	case Skull:
		oval(-0.33, -0.15, 0.19, 0.21);
		oval(0.33, -0.15, 0.19, 0.21);
		polygon(features, {{0, 0.12}, {0.09, 0.30}, {-0.09, 0.30}});
		stroke(features, {{-0.36, 0.58, 0.035}, {0.36, 0.58, 0.035}});
		for (double x : {-0.24, -0.08, 0.08, 0.24})
			stroke(features, {{x, 0.48, 0.035}, {x, 0.70, 0.035}});
		break;
	case Flower:
		oval(0, 0, 0.20, 0.20);
		break;
	case Gem:
		// Facet lines stop short of each other and the edge, so they never cut the gem apart.
		stroke(features, {{-0.46, -0.18, 0.04}, {0.46, -0.18, 0.04}});
		stroke(features, {{-0.30, -0.04, 0.04}, {-0.04, 0.40, 0.04}});
		stroke(features, {{0.30, -0.04, 0.04}, {0.04, 0.40, 0.04}});
		break;
	case SpeechBubble:
		for (double x : {-0.42, 0.0, 0.42})
			oval(x, -0.15, 0.12, 0.12);
		break;
	case Shield:
		stroke(features, {{0, -0.62, 0.07}, {0, 0.50, 0.07}});
		stroke(features, {{-0.50, -0.28, 0.07}, {0.50, -0.28, 0.07}});
		break;
	default:
		return body;
	}
	if (character == Smile || character == Wink || character == HeartEyes ||
		character == Sunglasses)
		mouth(false);
	else if (character == Sad)
		mouth(true);
	else if (character == Surprised)
		oval(0, 0.37, 0.15, 0.21);
	// Features are negative space in a filled shape, and ink in an outlined one.
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
		o.character == 0 ? int(context.bounded("emoji-character", CharacterCount)) : o.character - 1;
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
	int placed = 0;
	for (int j = 0; j < o.crossings; ++j)
	{
		const double a = phase + 2 * kPi * j / o.crossings;
		const auto from = polarPoint(cx, cy, 0.30 * t.w, a);
		// A crossing joins the bypass to the drawing: it runs inward to the first land beyond the
		// first water and stops there. One with no water to cross is not needed, and one that
		// finds no land before 0.55 of the radius only leads into a lake, where it would
		// hand a single eye or mouth island to whichever colony stands nearest (maintainer review
		// 2026-09-16: "they make one of the eyeballs connected but the other ones not"), so it
		// is not laid at all.
		bool wet = false, landed = false;
		ShapePoint to = from;
		for (double r = 0.30 * t.w; r >= 0.55 * radius && !landed; r -= 0.5)
		{
			to = polarPoint(cx, cy, r, a);
			const bool water =
				L.terrain[t.at(int(std::lround(to.x)), int(std::lround(to.y)))] == WATER;
			landed = wet && !water;
			wet |= water;
		}
		if (!landed)
			continue;
		++placed;
		std::vector<StrokePoint> bridge{{from.x, from.y, 4}, {to.x, to.y, 4}};
		strokePath(land, t, bridge);
		for (auto &p : bridge)
			p.halfWidth = 1.5;
		strokePath(L.roads, t, bridge);
	}
	// Crossings are causeways: their land and sand spine are laid only over water, and their spine
	// over the country outside the drawing, where it joins the bypass. Over the drawing's own land
	// they draw nothing, so the face is not scored with sand lines; that land is walkable already
	// (maintainer review 2026-09-16: bridges "ruining the nice appearance of the emoji itself").
	for (int i = 0; i < t.size(); ++i)
	{
		const bool outside =
			std::hypot(t.offsetX(int(cx), i % t.w), t.offsetY(int(cy), i / t.w)) > radius + 1;
		if (L.roads[i] && !outside && L.terrain[i] != WATER)
			L.roads[i] = 0;
		if (land[i])
			L.terrain[i] = L.roads[i] ? SAND : GRASS;
	}
	layBeaches(L.terrain, t);
	context.telemetry.choice("emoji.character", kCharacters[character]);
	context.telemetry.choice("emoji.style", outline ? "outline" : "filled");
	context.telemetry.choice("emoji.terrain", inverse ? "grass-ink" : "water-ink");
	context.telemetry.measure("emoji.stroke.width-corners", thickness);
	context.telemetry.measure("emoji.ink.corners", inkCorners);
	context.telemetry.measure("emoji.crossings.requested", o.crossings);
	context.telemetry.measure("emoji.crossings.placed", placed);
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
		11,
		false,
		{GeneratorControl::choice("character", "Emoji character",
								  {"Random", "Smiley", "Sad face", "Winking face", "Surprised face",
								   "Heart eyes", "Sunglasses", "Heart", "Star", "Neutral face",
								   "Sleeping face", "Grinning face", "Kissing face", "Angry face",
								   "Tongue out", "Skull", "Flower", "Four-leaf clover", "Teardrop",
								   "Cloud", "Gem", "Apple", "Speech bubble", "Shield"},
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
