// SPDX-License-Identifier: GPL-3.0-or-later
#include "RiceTerracesGenerator.h"
#include "Drawing.h"
#include "Farmland.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Orbits.h"
#include "Patterns.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Room.h"
#include "Settlements.h"
#include "Sketch.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Rice terraces: long terraced hillsides that run diagonally across the whole torus, the way a
// real terraced valley side does. Every hillside is one continuous stripe (Patterns.h's stripe
// field, at Rain shadow's slant, so with a slant every crest is one long spiral round the torus):
// a dry crest where the towns stand, then narrow contour strips of rice and irrigation water
// stepping down each slope, then a valley floor of shared ground with a river along its middle.
// The contours sway together along the hillside, in a few long waves and a fine grain, so the
// terraces meander rather than run ruled; sand stairs climb straight across them every
// `stair-spacing` tiles and carry on across the valley as roads that ford the river.
//
// WHY THIS MAP EXISTS (maintainer review 2026-09-16). The first Rice terraces drew concentric
// rings round point summits; lobing the rings answered "a centre-pivot farm" but never made them
// read as rice terraces, because terraces are not rings round a point at all: they are many narrow
// strips following the contour of a long slope, stacked, winding and joined into one landscape.
// That map is Hills now (HillsGenerator.cpp); this one starts from the landscape instead of from
// the contour-farm primitive.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Water blocks walking and
// crops block walking and building, so the terraces are a wall between the crest and the valley
// that only the stairs cross; every crop strip lies within a few tiles of water, so the terraces
// regrow and a colony's food is the slope below its town. Sand caps above and below the terraces,
// and a sand ring round every town, keep the crops off the towns and the commons however long the
// game runs. The valley is where colonies on facing slopes meet.
namespace
{
// The crest's half width beyond a town's radius: a lane of grass round every town along the crest.
constexpr double kCrestMargin = 3;
// Sand caps, in corners, above and below every slope's terraces and round every town that cuts
// into them: two corners make a full tile of sand no crop crosses on eight neighbours.
constexpr double kCap = 2;
// The narrowest valley floor: a river and its beaches with walking ground either side.
constexpr double kValleyMinimum = 16;
// One terrace at 100% band width: twelve corners of rice, six of water. layBeaches' eight-
// neighbour rule, the four-corner pureness test and the wave sway's own narrowing at a turn (below)
// each eat into a band's edges; the original five and two left both crop and water rows a single
// tile wide at the median and often none at all, too thin for wheat to grow or spread and too thin
// to read as an irrigation channel. These hold a plantable/pure width of at least two tiles
// everywhere across the band-width and waviness range and three or more typical
// (rice.crop-width.* and rice.water-width.* telemetry below). Every rice tile still lies within the
// engine's 15-tile growth probe of several channels.
constexpr double kCropCorners = 12, kWaterCorners = 6;
// Stairs are five corners across, as Hills' are; valley roads three, as sand lanes elsewhere.
constexpr double kStairHalf = 2.5, kRoadHalf = 1.5;
// The river along the valley's middle, in corners either side of it.
constexpr double kRiverHalf = 1.5;
// The contours' sway: three long waves along the hillside of about these lengths (whole numbers
// of waves per turn along, so the stripes still meet themselves across the seams), sharing the
// waviness control in these proportions, plus a fine fractal grain of kGrainPercent of a spacing.
// Every contour of a hillside moves by the same amount at a point along it, so the terraces keep
// their widths along the slope's normal and never fold; only steeper sway narrows them where the
// hillside turns. Lengthened 2026-09-17 (from 110, 60, 36) so a given waviness amplitude turns more
// gently: bigger, broader sweeps down the hillside instead of sharp doglegs, and less narrowing at
// every turn for the same visual sway.
constexpr std::array<double, 3> kWaveLengths{220, 120, 72}, kWaveShares{0.5, 0.3, 0.2};
constexpr int kGrainPeriod = 24, kGrainPercent = 3;
// The towns' starter supplies: two wheat patches on the first terrace below the town, one on either
// slope's side, and one of wood; a quarry tile in town; and a completed inn for the first meal
// (Hills' first AI games showed the first feeding deadline arriving before an inn is built and
// filled across a sand cap).
constexpr int kStarterWheat = 14, kStarterWood = 12;
constexpr int kMinimumRoom = 48;

struct Layout
{
	Torus t{1, 1};
	StripeStyle across, along;
	int hillsides = 0, terraces = 0, stairCount = 0;
	double spacing = 0, crestHalf = 0, homeRadius = 0;
	FarmRows rows{kCropCorners, kWaterCorners};
	std::vector<ShapePoint> homes;
	std::vector<int> homeOf, row; // row: 2k for the k-th rice strip down a slope, 2k+1 its channel
	std::vector<unsigned char> crest, stairs, roads, valley;
	std::vector<unsigned char> upper; // on the slope of rising phase from its crest
	TerrainSketch terrain;
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const RiceTerracesOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);
	L.rows = {kCropCorners * o.bandWidth / 100.0, kWaterCorners * o.bandWidth / 100.0};
	L.homeRadius = o.homeSize;
	L.crestHalf = L.homeRadius + kCrestMargin;

	// FITTING. Hillsides are taken away one at a time until a slope holds the terraces asked for
	// beside a crest and the narrowest valley, as Rain shadow takes away ridges; then the terraces
	// are cut to what fits; and only a slope that holds no terrace refuses the map. The sway
	// narrows the terraces where the hillside turns, so the valley is budgeted a wave's worth wider.
	L.across.acrossX = o.slant;
	L.across.warpPeriod = kGrainPeriod;
	L.across.warpPercent = kGrainPercent;
	const auto slopeRoom = [&](int hillsides)
	{
		L.across.acrossY = hillsides;
		const double spacing = stripeSpacing(t, L.across);
		return (spacing - 2 * L.crestHalf - kValleyMinimum - o.waviness) / 2 - 2 * kCap;
	};
	// Every colony needs its own stretch of crest: the crests' total length (the map's area over the
	// spacing) must hold every town with a town's width of crest between it and the next, so a
	// crowded map keeps more hillsides than it asked for.
	const double townSpacing = 2 * (L.homeRadius + kCap + kCrestMargin) + 4;
	int fewest = 1;
	while (fewest < 12)
	{
		L.across.acrossY = fewest;
		if (t.size() / stripeSpacing(t, L.across) >= teams * townSpacing)
			break;
		++fewest;
	}
	// The control counts hillsides per 256 tiles of the map's longer side, so a 512 map keeps the
	// same slopes rather than one hillside with valleys twice as wide.
	const int asked = std::max(1, int(std::lround(o.hillsides * std::max(t.w, t.h) / 256.0)));
	for (L.hillsides = std::max(asked, fewest); L.hillsides > fewest; --L.hillsides)
		if (slopeRoom(L.hillsides) >= o.terraces * L.rows.period())
			break;
	const double room = slopeRoom(L.hillsides);
	L.terraces = std::min(o.terraces, int(std::floor(room / L.rows.period())));
	L.across.acrossY = L.hillsides;
	L.spacing = stripeSpacing(t, L.across);
	context.telemetry.measure("rice.hillsides.actual", L.hillsides);
	context.telemetry.measure("rice.terraces.per-slope", L.terraces);
	context.telemetry.measure("rice.hillsides.spacing", L.spacing);
	if (L.hillsides < asked)
		context.telemetry.fallback("rice.hillsides.reduced", "Fewer hillsides so the terraces fit");
	if (L.hillsides > asked)
		context.telemetry.fallback("rice.hillsides.added", "More hillsides so every town has crest");
	if (L.terraces < o.terraces)
		context.telemetry.fallback("rice.terraces.reduced", "Fewer terraces so a slope fits");
	if (L.terraces < 1)
	{
		L.failure = "A hillside has no room for a terrace; use a bigger map, fewer hillsides, "
					"smaller towns or narrower terraces.";
		return L;
	}

	// THE HILLSIDES. Phase 0 is a crest, half a turn a valley's middle. The long waves shift the
	// whole across phase by the same number of tiles at a point along the hillside.
	L.along = alongStripes(t, L.across);
	const double alongLength = stripeSpacing(t, L.along);
	std::vector<int> phase = stripePhase(t, L.across, context.stream("rice-hillsides"));
	const std::vector<int> along = stripePhase(t, L.along, context.stream("rice-hillsides"));
	std::array<int, 3> waves{};
	std::array<double, 3> wavePhase{};
	for (size_t j = 0; j < waves.size(); ++j)
	{
		waves[j] = std::max(1, int(std::lround(alongLength / kWaveLengths[j])));
		wavePhase[j] = context.bounded("rice-waves", 3600) * (2 * kPi / 3600);
	}
	for (int i = 0; i < n; ++i)
	{
		const double u = along[i] / 65536.0 * 2 * kPi;
		double shift = 0;
		for (size_t j = 0; j < waves.size(); ++j)
			shift += o.waviness * kWaveShares[j] * std::sin(waves[j] * u + wavePhase[j]);
		phase[i] = ((phase[i] + int(std::lround(shift / L.spacing * 65536))) % 65536 + 65536) % 65536;
	}
	const auto tilesFromCrest = [&](int i) { return stripeDistance(phase[i]) / 65536.0 * L.spacing; };

	// THE STAIRS, every stair-spacing tiles along each turn of the hillside, the two slopes of a
	// valley staggered by half a spacing so no stair lines up with the one facing it.
	L.stairCount = std::max(1, int(std::lround(alongLength / o.stairSpacing)));
	const int stairPeriod = 65536 / L.stairCount;
	const auto alongTiles = [&](double units) { return units / 65536.0 * alongLength; };
	const auto stairOffset = [&](int i)
	{
		const int shift = phase[i] < 32768 ? stairPeriod / 2 : 0;
		const int offset = ((along[i] + shift) % stairPeriod + stairPeriod) % stairPeriod;
		return alongTiles(std::min(offset, stairPeriod - offset));
	};

	const double terracesStart = L.crestHalf + kCap;
	const double terracesEnd = terracesStart + L.terraces * L.rows.period();
	const double valleyStart = terracesEnd + kCap;
	L.terrain.assign(n, GRASS);
	L.row.assign(n, -1);
	L.crest.assign(n, 0);
	L.stairs.assign(n, 0);
	L.roads.assign(n, 0);
	L.valley.assign(n, 0);
	L.upper.assign(n, 0);
	int riverCorners = 0;
	for (int i = 0; i < n; ++i)
	{
		const double d = tilesFromCrest(i);
		const double stair = stairOffset(i);
		L.upper[i] = phase[i] < 32768;
		if (d < L.crestHalf)
		{
			L.crest[i] = 1;
			continue;
		}
		if (d < valleyStart)
		{
			if (stair < kStairHalf)
			{
				L.stairs[i] = 1;
				L.terrain[i] = SAND;
				continue;
			}
			if (d < terracesStart || d >= terracesEnd)
			{
				L.terrain[i] = SAND;
				continue;
			}
			const double e = d - terracesStart;
			const int k = int(std::floor(e / L.rows.period()));
			const bool wet = e - k * L.rows.period() >= L.rows.crops;
			L.row[i] = 2 * k + wet;
			L.terrain[i] = wet ? WATER : GRASS;
			continue;
		}
		L.valley[i] = 1;
		// The stairs carry on across the valley as roads, to its middle and a little past it, so a
		// road fords the river and meets the facing slope's own road on the far side.
		if (stair < kRoadHalf)
		{
			L.roads[i] = 1;
			L.terrain[i] = SAND;
			continue;
		}
		if (o.river && std::abs(d - L.spacing / 2) < kRiverHalf)
		{
			L.terrain[i] = WATER;
			++riverCorners;
		}
	}

	// THE TOWNS, on the middle line of the crests, spread as far apart along them as they go: the
	// first at a seeded crest tile, every next one the crest tile farthest from the towns before it.
	// Each is a clearing that the terraces bend round inside a sand ring.
	std::vector<int> ridgeline;
	for (int i = 0; i < n; ++i)
		if (stripeDistance(phase[i]) / 65536.0 * L.spacing < 0.5)
			ridgeline.push_back(i);
	if (ridgeline.empty())
	{
		L.failure = "The crests have no middle line.";
		return L;
	}
	std::vector<std::int64_t> nearestTown(ridgeline.size(), INT64_MAX);
	int next = int(context.bounded("rice-layout", std::uint32_t(ridgeline.size())));
	for (int k = 0; k < teams; ++k)
	{
		const int at = ridgeline[size_t(next)];
		L.homes.push_back({at % t.w + 0.5, at / t.w + 0.5});
		int farthest = 0;
		for (size_t c = 0; c < ridgeline.size(); ++c)
		{
			const int i = ridgeline[c];
			nearestTown[c] = std::min<std::int64_t>(nearestTown[c],
													t.dist2(at % t.w, at / t.w, i % t.w, i / t.w));
			if (nearestTown[c] > nearestTown[size_t(farthest)])
				farthest = int(c);
		}
		next = farthest;
	}
	dealStarts(context, L.homes);
	if (teams > 1 && nearestSiteDistance(t, L.homes) < 2 * (L.homeRadius + kCap + kCrestMargin))
	{
		L.failure = "Too many colonies for these crests; use a bigger map or fewer colonies.";
		return L;
	}
	L.homeOf.assign(n, -1);
	for (int k = 0; k < int(L.homes.size()); ++k)
	{
		const int reach = int(std::ceil(L.homeRadius + kCap)) + 1;
		const int hx = int(std::lround(L.homes[k].x)), hy = int(std::lround(L.homes[k].y));
		for (int dy = -reach; dy <= reach; ++dy)
			for (int dx = -reach; dx <= reach; ++dx)
			{
				const int i = t.at(hx + dx, hy + dy);
				const double r = std::hypot(dx, dy);
				if (r < L.homeRadius)
				{
					L.homeOf[i] = k;
					L.terrain[i] = GRASS;
					L.row[i] = -1;
					L.stairs[i] = L.roads[i] = L.valley[i] = 0;
					L.crest[i] = 1;
				}
				else if (r < L.homeRadius + kCap && !L.crest[i] && L.terrain[i] != SAND)
				{
					L.terrain[i] = SAND;
					L.row[i] = -1;
				}
			}
	}
	layBeaches(L.terrain, t);
	context.telemetry.measure("rice.stairs.per-turn", L.stairCount);
	context.telemetry.measure("rice.river.water-corners", riverCorners);
	context.telemetry.measure("rice.waves.first", waves[0]);
	{
		const auto grassPure = pureTiles(L.terrain, t, GRASS);
		const auto waterPure = pureTiles(L.terrain, t, WATER);
		const double heading = stripeNormal(t, L.across);
		const int sx = int(std::lround(std::cos(heading))), sy = int(std::lround(std::sin(heading)));
		const auto measure = [&](const std::vector<unsigned char> &pure, bool wet, const char *prefix)
		{
			std::vector<int> widths;
			for (int i = 0; i < n; ++i)
			{
				if (!(L.row[i] >= 0 && bool(L.row[i] % 2) == wet && pure[i]))
					continue;
				int width = 1;
				for (int dir : {-1, 1})
				{
					int x = i % t.w, y = i / t.w;
					for (;;)
					{
						x += dir * sx;
						y += dir * sy;
						const int j = t.at(x, y);
						if (pure[j] && L.row[j] == L.row[i])
							++width;
						else
							break;
					}
				}
				widths.push_back(width);
			}
			if (!widths.empty())
			{
				std::sort(widths.begin(), widths.end());
				context.telemetry.measure((std::string(prefix) + ".min").c_str(), widths.front());
				context.telemetry.measure((std::string(prefix) + ".p10").c_str(),
										  widths[widths.size() / 10]);
				context.telemetry.measure((std::string(prefix) + ".median").c_str(),
										  widths[widths.size() / 2]);
			}
		};
		measure(grassPure, false, "rice.crop-width");
		measure(waterPure, true, "rice.water-width");
	}
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "rice terraces layout";
	const RiceTerracesOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("rice.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	Map &map = game.map;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();
	writeUndermap(map, L.terrain);

	context.stage = "rice terraces towns";
	if (!settleRoundColonies(game, context, "rice-starts", L.homeOf, L.homes, L.homeRadius))
		return false;
	const auto free = [&](int i) { return clearGround(map, i % t.w, i / t.w); };
	const auto home = [&](int k)
	{
		std::vector<unsigned char> mask(n, 0);
		for (int i = 0; i < n; ++i)
			mask[i] = L.homeOf[i] == k && map.isGrass(i % t.w, i / t.w);
		return mask;
	};
	const double normal = stripeNormal(t, L.across);
	for (int k = 0; k < teams; ++k)
	{
		const ShapePoint h = L.homes[k];
		if (placeStartingBuilding(game, k, "inn", 0, h.x + 6 * std::cos(normal),
								  h.y + 6 * std::sin(normal), 4, home(k), {WHEAT}) < 0)
		{
			context.detail = "A town has no room for its starter inn.";
			return false;
		}
		const int quarry = seedNear(t, int(h.x - 7 * std::cos(normal)), int(h.y - 7 * std::sin(normal)),
									4, [&](int i) { return L.homeOf[i] == k && free(i); });
		if (quarry < 0)
		{
			context.detail = "A town has no room for its quarry.";
			return false;
		}
		map.setResource(quarry % t.w, quarry / t.w, STONE, 1);
	}

	context.stage = "rice terraces crops";
	// Starter crops on the first rice strip below each town, down both slopes of its crest: wheat
	// from the strip tile nearest the town on each slope, wood from the upper slope's nearest tile
	// at least a patch's width from that slope's wheat.
	const auto firstStrip = [&](int i) { return L.row[i] == 0 && free(i); };
	for (int k = 0; k < teams; ++k)
	{
		const int hx = int(L.homes[k].x), hy = int(L.homes[k].y);
		const int reach = int(L.homeRadius + kCap) + 14;
		// Whether a patch of `count` fits in the strip pocket round a tile: a stair or the town's
		// ring can cut a strip into pieces too short for one.
		const auto roomFor = [&](int seed, int count)
		{
			std::vector<int> queue{seed};
			std::vector<int> seen{seed};
			for (size_t q = 0; q < queue.size() && int(queue.size()) < count; ++q)
			{
				const int x = queue[q] % t.w, y = queue[q] / t.w;
				for (const auto &[ox, oy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
				{
					const int j = t.at(x + ox, y + oy);
					if (firstStrip(j) && std::find(seen.begin(), seen.end(), j) == seen.end())
					{
						seen.push_back(j);
						queue.push_back(j);
					}
				}
			}
			return int(queue.size()) >= count;
		};
		const auto nearest = [&](bool upper, int awayFrom, int count)
		{
			int best = -1;
			std::int64_t bestDistance = 0;
			for (int dy = -reach; dy <= reach; ++dy)
				for (int dx = -reach; dx <= reach; ++dx)
				{
					const int i = t.at(hx + dx, hy + dy);
					if (!firstStrip(i) || bool(L.upper[i]) != upper ||
						(awayFrom >= 0 &&
						 t.dist2(i % t.w, i / t.w, awayFrom % t.w, awayFrom / t.w) < 100))
						continue;
					const std::int64_t distance = std::int64_t(dx) * dx + std::int64_t(dy) * dy;
					if ((best < 0 || distance < bestDistance) && roomFor(i, count))
					{
						best = i;
						bestDistance = distance;
					}
				}
			return best;
		};
		int wheat = 0, upperWheat = -1;
		for (bool upper : {false, true})
			if (const int seed = nearest(upper, -1, kStarterWheat); seed >= 0)
			{
				wheat += growPatch(map, t, seed, WHEAT, kStarterWheat, firstStrip);
				if (upper)
					upperWheat = seed;
			}
		const int woodSeed = nearest(true, upperWheat, kStarterWood);
		const int wood = woodSeed >= 0 ? growPatch(map, t, woodSeed, WOOD, kStarterWood, firstStrip) : 0;
		context.telemetry.measure("rice.starter.wheat-tiles", wheat, k);
		context.telemetry.measure("rice.starter.wood-tiles", wood, k);
		if (wheat < kStarterWheat || wood < kStarterWood / 2)
		{
			context.detail = "A town's starter terraces do not fit (" + std::to_string(wheat) + " wheat, " + std::to_string(wood) + " wood).";
			return false;
		}
	}
	// The terraces: rice (wheat) over most of the strips and a little timber, in patches so the
	// strips can be harvested from their ends; the valley keeps only fruit and outcrops.
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 6, context.stream("rice-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 5, context.stream("rice-split"));
	const auto strip = [&](int i) { return L.row[i] >= 0 && L.row[i] % 2 == 0 && free(i); };
	int stripArea = 0;
	for (int i = 0; i < n; ++i)
		stripArea += strip(i) && fertility.at(i % t.w, i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, strip, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int)
		{
			return GroundAmounts{int(scaledCount(stripArea * 50 / 100, o.wheat)),
								 int(scaledCount(stripArea * 6 / 100, o.wood)), 0, 0};
		},
		"rice-stone", "rice-fruit");
	std::vector<int> commons;
	for (int i = 0; i < n; ++i)
		if (L.valley[i] && !L.roads[i] && free(i))
			commons.push_back(i);
	for (int type : {STONE, CHERRY})
	{
		const bool quarry = type == STONE;
		const int wanted =
			int(scaledCount(int(commons.size()) / (quarry ? 1500 : 700) + (quarry ? 1 : 3),
							quarry ? o.stone : o.fruit));
		const char *stream = quarry ? "rice-stone" : "rice-fruit";
		const int actual = scatterClumps(
			context, t, commons, wanted, stream, free,
			[&](MapGeneratorPoint p)
			{
				placeResourceClump(map, context, p,
								   quarry ? STONE : CHERRY + int(context.bounded(stream, 3)), 1);
			});
		context.telemetry.measure(quarry ? "rice.quarries.placed" : "rice.groves.placed", actual);
	}
	seedAlgae(map, context, t, "rice-algae", o.algae, AlgaeBand::anyWater(60));
	// The generic first-crop rescue may only plant on a rice strip, never in a town or the valley.
	std::vector<unsigned char> topup(n, 0);
	for (int i = 0; i < n; ++i)
		topup[i] = L.row[i] >= 0 && L.row[i] % 2 == 0;
	secureStartingCrops(game, context, t, 24, 32, 0, nullptr, &topup);
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (const std::string mismatch = designMismatch(L, game.map, "rice terraces"); !mismatch.empty())
		return mismatch;
	const Map &map = game.map;
	const Torus &t = L.t;
	const auto open = walkableTiles(map);
	const auto sand = pureTiles(L.terrain, t, SAND);
	for (int i = 0; i < t.size(); ++i)
		if ((L.stairs[i] || L.roads[i]) && sand[i] && !open[i])
			return "A terrace stair or valley road is blocked.";
	// Crop containment as a future footprint: no pure grass reachable from a town, on eight
	// neighbours, may be a rice strip, whatever grows or is harvested later.
	const auto grass = pureTiles(L.terrain, t, GRASS);
	const auto buildable = buildableTiles(map);
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		std::vector<unsigned char> home(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			home[i] = L.homeOf[i] == k;
		const auto fromHome = stepsFrom(t, home, grass);
		for (int i = 0; i < t.size(); ++i)
			if (fromHome[i] >= 0 && L.row[i] >= 0)
				return "Terrace crops could grow into a town.";
		if (buildSites(t, buildable, home) < kMinimumRoom)
			return "A town lacks building room.";
	}
	return walkFromFirstColony(map, context.request.nbTeams, "the terraces",
							   "along the crests, stairs and valley roads")
		.error;
}
} // namespace

RiceTerracesOptions::RiceTerracesOptions(const GenerationRequest &r)
	: hillsides(r.option("hillsides")), slant(r.option("slant")), terraces(r.option("terraces")),
	  bandWidth(r.option("band-width")), waviness(r.option("waviness")),
	  stairSpacing(r.option("stair-spacing")), homeSize(r.option("home-size")),
	  river(r.option("valley-river") != 0), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition riceTerracesDefinition()
{
	return {
			"rice-terraces",
			52,
			"Rice terraces",
			3,
			false,
			// One hillside at a slant of one is a single terraced slope spiralling round the torus: on
			// a 256 map it crosses twice. Terraces should be most of the map (a first render with two
			// hillsides of four terraces was mostly grass); the `terraces` control asks for a count per
			// slope but the room a slope actually holds, at a workable band width, wins (see
			// rice.terraces.reduced telemetry) — a 256 map with one hillside fits about three at the
			// default band width. Waviness is the long sway's amplitude in tiles.
			{{"hillsides", "Hillsides", 1, 6, 1, 1, ControlGroup::Terrain},
			 {"slant", "Slant", 0, 3, 1, 1, ControlGroup::Terrain},
			 {"terraces", "Terraces per slope", 2, 12, 1, 8, ControlGroup::Terrain},
			 {"band-width", "Contour band width", 90, 140, 10, 100, ControlGroup::Terrain},
			 {"waviness", "Waviness", 0, 20, 2, 16, ControlGroup::Terrain},
			 {"stair-spacing", "Stair spacing", 24, 96, 8, 48, ControlGroup::Layout},
			 {"home-size", "Home size", 10, 18, 1, 12, ControlGroup::Layout},
			 GeneratorControl::toggle("valley-river", "Valley river", true, ControlGroup::Terrain),
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld,
			{"terrain:natural", "feature:terraces", "feature:river", "style:tight-building"}};
}
