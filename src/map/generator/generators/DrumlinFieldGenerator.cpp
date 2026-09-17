// SPDX-License-Identifier: GPL-3.0-or-later
#include "DrumlinFieldGenerator.h"
#include "Drawing.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "GraphMaze.h"
#include "Grid.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Orbits.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Walls.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
using namespace MapGeneration;

// Drumlin field: a lake land. Where an ice sheet once flowed, the ground it left is a swarm of long
// oval hills, all pointing the way the ice went, with water in every hollow between them: the lake
// country of Finland, Ireland's drumlin belt, the Canadian shield. Every hill is a grass island
// with a blunt head upstream and a tapering tail downstream, and between the hills wind eskers, the
// sinuous sand ridges the ice's meltwater rivers left behind, which here are the causeways: the
// only dry way from one drumlin to the next until a colony learns to swim.
//
// Every colony starts on one of the biggest drumlins. Its blunt head is the town, open grass with
// room to build; a collar of sand across the drumlin's waist parts the town from the tail, which is
// the colony's farm: water lies within a few tiles of every part of every drumlin, so anything
// planted on the tail spreads until the tail is all crop, and the collar is what keeps the crops
// off the town. Every other drumlin is fertile the same way and starts under a scatter of crops;
// the biggest and the farthest from every home carry the prizes.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Water blocks walking until
// a colony has a pool, so the eskers are the map's doors: a line of sand a few tiles wide that a
// column of units can cross but nothing can be built on and no crop grows over, so whoever holds
// an esker's landing holds it, and nobody can wall it shut with buildings. The eskers form a tree that joins
// every drumlin, plus a few loops, so most drumlins have one or two ways in and a colony's choice
// is which esker to hold and which neighbour's drumlin to take. Everything is fertile, so a farm
// drumlin grows shut over the game; what stays open is the beach round every drumlin (the engine
// keeps sand between grass and water, and crops need grass), so the shoreline is always a road and
// an overgrown drumlin can still be harvested from its rim and walked round. Swimming pools rewrite
// the map: every lake becomes a road and the eskers stop mattering, which is the second half of the
// game.
//
// Nothing is turned round a centre: the homes sit on a lattice (Orbits.h), every drumlin is drawn
// to the same grain, and fairness is statistical - every colony gets a home drumlin of the same
// size and shape, and which other drumlins lie beside it and how many eskers reach it is where it
// fell (the lobby keeps the best of several seeds by the start scorer). A design decides the
// geometry; whether it is playable is measured on the finished map (see validateWorld) and in play.
namespace
{

// Every home's starting kit, unscaled whatever the amounts say: wheat and wood on the tail just
// past the collar, where the lake water regrows them for the whole game, and a quarry on the head.
// Wheat 14 and wood 12 are what Rain shadow's homes start with (a kit that lets a colony build its
// first buildings before its own farm has spread); the quarry is a clump of radius 2, about a dozen
// stones, because stone never runs out and one clump serves a colony for the whole game.
constexpr int kHomeWheat = 14, kHomeWood = 12, kHomeQuarry = 2;
// A home drumlin is `home-size` tiles across at its widest (half width `home-size`), and shrinks to
// fit between neighbouring homes down to this half width: the town head of an 8-wide-radius drumlin
// is about 16 by 14 tiles of pure grass once the beach is taken off, which seats a swarm, its
// workers and two or three buildings, the least a colony can start from (Polder's villages have the
// same floor). Below it the map is refused rather than shipped unplayable.
constexpr int kHomeSmallestHalfWidth = 8;
// The home's town, collar, swarm and kit: Homes.h's TeardropHome defaults, which were written for
// this map (the town is the head past its widest point, the collar two tiles, the swarm a quarter
// of the way back from the head, the kit's crops just past the collar and its quarry at the tip).
constexpr TeardropHome kHome{};
// Drumlins smaller than this half width are dropped from the packing (Points.h's packLandforms):
// a drumlin 8 tiles across keeps about 6 of pure grass once its beach is laid, enough for a grove
// or an outcrop, and anything narrower is all beach.
constexpr double kSmallestDrumlin = 4.0;
// A teardrop's widest point lies this share of its length back from the head, give or take the
// jitter, drawn per drumlin: 0.4 is the drumlin's classic profile, and a spread of 0.35 to 0.45
// makes some stubbier and some more tapered so the swarm does not read as stamped.
constexpr int kHeadSharePercent = 40, kHeadShareJitterPercent = 5;
// The water gap the `drumlin-spacing` control is calibrated at, and the least the sites' spacing
// is ever narrowed to by a gap below it (a spacing of 14 packed drumlins under 10 tiles wide, too
// thin to build on once the beach is laid).
constexpr int kReferenceGap = 6, kSmallestSpacing = 16;
// Dart throwing keeps a site at least this share of the spacing from every other (Points.h's
// default for spreadPoints), and then this many rounds of relaxation even the swarm out: three
// rounds move most sites under a tile on the third, and every round costs a labelling of the whole
// map. Without the relaxation the drumlins packed between the far pairs of a raw dart swarm covered
// a quarter of the map; with it, two fifths.
constexpr int kSiteMinimumPercent = 83, kRelaxRounds = 3;
// An esker is a wandering line of sand `kEskerHalfWidth` tiles either side of its centre: three
// undermap corners of sand, which is four walkable tiles across (a tile with any sand corner is
// land), so a column of units crosses it two abreast, while a defender's towers on the landing
// cover the whole of it (a level-1 tower reaches 5 tiles). It wanders by up to `kEskerWander`
// tiles from the straight line between its two drumlins, enough to read as a ridge the meltwater
// left rather than a ruled causeway, little enough that it does not stray into a third drumlin's
// water in the usual gap (`water-gap` is 6 by default); its width swells and narrows by a fifth.
constexpr double kEskerHalfWidth = 1.5, kEskerWander = 3.0, kEskerWidthJitter = 0.2;
// The near tree that joins the drumlins stretches each link's length by up to this much when
// ordering them (GraphMaze.h's carveNearTree), so the tree still favours the next drumlin over but
// differs from seed to seed even when the sites do not move much.
constexpr int kEskerJitterPercent = 30;
// Ambient farmland on the farm drumlins: this share of their fertile grass under wheat and this
// under wood at the start, in patches (Homes.h's furnishGround), so a drumlin is walkable and
// buildable at first and fills in as the crops spread. Wood spreads faster than wheat (the engine
// gates wheat by one in three, wood not at all), so it starts at less than half wheat's share.
constexpr int kFarmWheatPercent = 30, kFarmWoodPercent = 12;
// Stone outcrops (glacial erratics) and fruit groves scattered over the farm drumlins: one outcrop
// per this many tiles of farm ground and one grove per this many, before the amounts scale them.
constexpr int kTilesPerOutcrop = 2000, kTilesPerGrove = 1500;
// Orchards of all three fruits on the drumlins farthest from every home: half a colony's worth
// (rounded up) by default, each grove a one-tile clump this far from the drumlin's middle, on a
// drumlin at least this half width so the three groves and the room to build an inn beside them
// fit.
constexpr int kOrchardsPerTwoColonies = 1;
constexpr double kOrchardRadius = 3.0, kOrchardSmallestHalfWidth = 5.5;
// Algae: one clump per this many water tiles, on the half of the water where it regrows best
// (Planting.h's algaeGrowthChance wants sand within 30 tiles, which every beach and esker gives).
constexpr int kWaterPerAlgae = 50;

struct Drumlin
{
	Site site;      // its middle
	double radius;  // packed radius under the grain; below 0 when dropped
	Teardrop shape; // its outline, drawn to the grain's heading
};

struct Layout
{
	Torus t{1, 1};
	Grain grain;
	double heading = 0;    // the grain's heading: head to tail of every drumlin
	double homeRadius = 0; // every home drumlin's packed radius
	std::vector<Drumlin> drumlins;            // the first `teams` are the homes
	std::vector<int> drumlinOf;               // each tile's drumlin, or -1 for water
	std::vector<int> homeOf;                  // each tile's colony, on the town heads only, or -1
	std::vector<int> farmOf;                  // each tile's colony, on the home tails only, or -1
	std::vector<unsigned char> collar, esker; // sand: across each home's waist; the causeways
	std::vector<ShapePoint> homes;            // each colony's home drumlin's middle
	std::vector<std::array<int, 2>> eskers;   // the drumlins each esker joins
	std::vector<int> orchards;                // the drumlins carrying an orchard
	int treeEskers = 0;
	std::string failure;
};

// The homes and the grain: everything that can refuse a request, as a pure function of the request
// and the context's streams, and cheap, so the lobby's request check (designFailure<planHomes>)
// need not lay out the whole field to say no. `homeSites` receives the home sites.
Layout planHomes(const GenerationRequest &request, GenerationContext &context,
				 std::vector<Site> &homeSites)
{
	const DrumlinFieldOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int teams = std::max(1, request.nbTeams);

	// Homes: on a lattice, the roomiest the torus holds, dealt to the colonies at random.
	const LatticeSites lattice =
		latticeSites(t.w, t.h, teams, context.bounded("drumlin-layout", std::uint32_t(t.w)),
					 context.bounded("drumlin-layout", std::uint32_t(t.h)));
	homeSites.clear();
	for (const ShapePoint &p : lattice.sites)
		homeSites.push_back({t.x(int(p.x)), t.y(int(p.y))});
	dealStarts(context, homeSites); // which colony gets which site is a draw, not the order

	// The grain: the requested heading, or a random one unless it leaves the homes too close under
	// it (two homes end to end along the grain are as close as two side by side divided by the
	// stretch), in which case the heading that keeps them farthest apart is taken instead, so a
	// crowded map gets the grain that fits rather than a refusal. The homes' size under the grain
	// is `home-size`, or what fits between the nearest two homes with the water gap between their
	// drumlins and a tile for rasterization, whichever is less; and less again, down to the floor,
	// so the field between the homes keeps room for farm drumlins: the ground farthest from every
	// home on a square lattice is a cell's centre, the lattice spacing over root two away, and a
	// drumlin there needs two home radii and the gap to every home (a 128 map with four colonies
	// held nothing but the four homes at `home-size` 11; at 9 it holds a ring of farms).
	L.grain = grainForChoice(o.grain, o.drumlinLength, context, "drumlin-grain");
	const auto homeDistance = [&]
	{
		const std::vector<double> nearest = nearestSiteDistances(t, homeSites, L.grain);
		return *std::min_element(nearest.begin(), nearest.end());
	};
	double distance = homeDistance();
	if (o.grain == 0 && distance < 2 * kHomeSmallestHalfWidth + o.waterGap + 1)
	{
		L.grain = grainHeading(widestGrainHeading(t, homeSites, o.drumlinLength), o.drumlinLength);
		distance = homeDistance();
		context.telemetry.fallback("drumlin-field.grain.refit",
								   "Random grain left the homes too close; took the widest");
	}
	context.telemetry.choice("drumlin-field.grain",
							 std::to_string(L.grain.dx) + "," + std::to_string(L.grain.dy));
	L.heading = L.grain.heading();
	L.homeRadius = std::min<double>(o.homeSize, std::floor((distance - o.waterGap - 1) / 2));
	if (L.homeRadius < kHomeSmallestHalfWidth)
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	const double farmRoom = std::floor((distance / std::sqrt(2.0) - o.waterGap) / 2);
	if (farmRoom < L.homeRadius)
	{
		L.homeRadius = std::max<double>(kHomeSmallestHalfWidth, farmRoom);
		context.telemetry.fallback("drumlin-field.homes.farm-room",
								   "Homes shrank to leave room for farm drumlins between them");
	}
	context.telemetry.measure("drumlin-field.homes.distance-under-grain", distance);
	context.telemetry.measure("drumlin-field.homes.actual-radius", L.homeRadius);
	if (L.homeRadius < o.homeSize)
		context.telemetry.fallback("drumlin-field.homes.shrunk",
								   "Homes shrank to fit between their neighbours");
	return L;
}

// A request check that plans only the homes: the field's one other failure (a near tree that does
// not join every drumlin) cannot happen on a torus whose cells all touch, so the lobby's answer is
// the homes' answer, at a fraction of the cost.
std::string homesFailure(const GenerationRequest &request)
{
	GenerationContext probe(request);
	std::vector<Site> homeSites;
	return planHomes(request, probe, homeSites).failure;
}

// The whole layout as a pure function of the request and the context's streams: validateWorld
// builds it again and checks the finished map against it.
Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const DrumlinFieldOptions o(request);
	std::vector<Site> homeSites;
	Layout L = planHomes(request, context, homeSites);
	if (!L.failure.empty())
		return L;
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// The swarm of sites: the homes first, then darts under the grain, none nearer a home than two
	// home radii and the gap (so the drumlin packed round a dart never crowds a home's), evened out
	// by a few rounds of relaxation with the homes held still, then every site's packed radius. A
	// dropped site (too small for a drumlin) is water.
	const int fixedMinimum = int(std::ceil(2 * L.homeRadius + o.waterGap));
	// Sites are thrown `drumlin-spacing` apart at the default gap, and the gap's change from the
	// default is added to that spacing: a wider gap then widens the water rather than eating the
	// drumlins (at spacing 20 a gap of 12 packed drumlins 10 tiles wide, all beach; with the
	// spacing widened to 26 they keep their size and the lakes between them grow), and a narrower
	// one packs them closer.
	const int siteSpacing = std::max(kSmallestSpacing, o.drumlinSpacing + o.waterGap - kReferenceGap);
	context.telemetry.measure("drumlin-field.sites.spacing", siteSpacing);
	// How far a labelling searches for a tile's nearest site before trying every site: inside a
	// home's exclusion the home itself is within the exclusion, and outside it the relaxed swarm
	// leaves no tile more than about a spacing and a half from a site.
	const int labelReach = std::max(fixedMinimum, siteSpacing * 3 / 2);
	const std::vector<Site> sites =
		relaxPoints(t,
					spreadPoints(t, siteSpacing, L.grain, context, "drumlin-sites", homeSites,
								 fixedMinimum, kSiteMinimumPercent),
					L.grain, siteSpacing, kRelaxRounds, teams, labelReach);
	std::vector<double> fixed(sites.size(), -1);
	for (int k = 0; k < teams; ++k)
		fixed[k] = L.homeRadius;
	const std::vector<double> radius =
		packLandforms(t, sites, L.grain, fixed, o.waterGap, kSmallestDrumlin, L.homeRadius);
	const double stretch = o.drumlinLength / 100.0;
	int dropped = 0;
	for (size_t s = 0; s < sites.size(); ++s)
	{
		// One draw per site whether or not it is kept, so a dropped site never shifts the shapes
		// of those after it.
		const double headShare =
			(kHeadSharePercent - kHeadShareJitterPercent +
			 int(context.bounded("drumlin-shapes", 2 * kHeadShareJitterPercent + 1))) /
			100.0;
		dropped += radius[s] < 0;
		L.drumlins.push_back({sites[s], radius[s],
							  Teardrop::fitting(std::max(radius[s], 1.0), stretch, headShare)});
	}
	context.telemetry.measure("drumlin-field.sites.actual", sites.size());
	context.telemetry.measure("drumlin-field.drumlins.dropped", dropped);

	// Stamp every drumlin, the homes with their town, collar and tail; a later drumlin never
	// overwrites an earlier one (the packing keeps them apart, so this only matters where
	// rasterization rounds).
	L.drumlinOf.assign(n, -1);
	L.homeOf.assign(n, -1);
	L.farmOf.assign(n, -1);
	L.collar.assign(n, 0);
	int grass = 0;
	for (size_t s = 0; s < L.drumlins.size(); ++s)
	{
		const Drumlin &d = L.drumlins[s];
		if (d.radius < 0)
			continue;
		const ShapePoint centre{d.site.x + 0.5, d.site.y + 0.5};
		const auto claim = [&](int i)
		{
			if (L.drumlinOf[i] >= 0)
				return false;
			L.drumlinOf[i] = int(s);
			++grass;
			return true;
		};
		if (int(s) >= teams)
			forEachTileInTeardrop(t, centre.x, centre.y, L.heading, d.shape,
								  [&](int i, double, double) { claim(i); });
		else
		{
			L.homes.push_back(centre);
			stampTeardropHome(t, centre, L.heading, d.shape, kHome,
							  [&](int i, int ground)
							  {
								  if (!claim(i))
									  return;
								  if (ground == 1)
									  L.collar[i] = 1;
								  else
									  (ground == 0 ? L.homeOf : L.farmOf)[i] = int(s);
							  });
		}
	}
	context.telemetry.measure("drumlin-field.grass.share-percent", grass * 100 / n);

	// The eskers: the drumlins' cells under the grain give the neighbour graph (two drumlins are
	// neighbours when only water parts them), a near tree joins every drumlin to the next one
	// over, and `esker-loops` percent more links open second ways round. Each link is a wandering
	// line of sand from one drumlin's middle to the other's, laid only over water: across a drumlin
	// it is nothing (its beach joins the esker to the grass), and where it clips a third drumlin's
	// water it lands there too, which is the odd extra door the design accepts.
	std::vector<Site> live;
	std::vector<ShapePoint> middles;
	std::vector<int> liveIndex;
	for (size_t s = 0; s < L.drumlins.size(); ++s)
		if (L.drumlins[s].radius >= 0)
		{
			live.push_back(L.drumlins[s].site);
			middles.push_back({L.drumlins[s].site.x + 0.5, L.drumlins[s].site.y + 0.5});
			liveIndex.push_back(int(s));
		}
	const std::vector<int> cells = nearestSiteLabels(t, live, siteSpacing, L.grain, labelReach);
	const CellGraph graph = cellGraph(t, live, siteNeighbours(t, cells, int(live.size())));
	std::vector<unsigned char> blocked(live.size(), 0), open(graph.edgeCells.size(), 0);
	if (!carveNearTree(graph, context, "drumlin-eskers", blocked, kEskerJitterPercent, open))
	{
		L.failure = "The eskers could not join every drumlin; use a smaller water gap or a "
					"bigger map.";
		return L;
	}
	L.treeEskers = int(std::count(open.begin(), open.end(), 1));
	openLoops(graph, context, "drumlin-eskers", blocked, o.eskerLoops, open);
	L.esker.assign(n, 0);
	for (const std::array<int, 2> &edge :
		 carveOpenEdges(L.esker, t, graph, middles, open, kEskerHalfWidth, kEskerWander,
						kEskerWidthJitter, context.stream("drumlin-esker-paths"),
						[&](int i) { return L.drumlinOf[i] < 0; }))
		L.eskers.push_back({liveIndex[edge[0]], liveIndex[edge[1]]});
	context.telemetry.measure("drumlin-field.drumlins.actual", live.size());
	context.telemetry.measure("drumlin-field.eskers.tree", L.treeEskers);
	context.telemetry.measure("drumlin-field.eskers.actual", L.eskers.size());

	// Orchards on the drumlins farthest from every home and from each other (farthestSites), by
	// distance in tiles rather than under the grain, since it is the walk that makes them remote:
	// the prize is always the far end of somebody's expansion.
	const int orchards = int(scaledCount((teams * kOrchardsPerTwoColonies + 1) / 2, o.fruit));
	std::vector<int> homeIndex(teams);
	std::vector<unsigned char> roomy(L.drumlins.size(), 0);
	for (int k = 0; k < teams; ++k)
		homeIndex[k] = k;
	for (size_t s = teams; s < L.drumlins.size(); ++s)
		roomy[s] = L.drumlins[s].radius >= kOrchardSmallestHalfWidth;
	std::vector<Site> allSites;
	for (const Drumlin &d : L.drumlins)
		allSites.push_back(d.site);
	L.orchards = farthestSites(t, allSites, homeIndex, roomy, orchards);
	context.telemetry.measure("drumlin-field.orchards.requested", orchards);
	context.telemetry.measure("drumlin-field.orchards.actual", L.orchards.size());
	if (int(L.orchards.size()) < orchards)
		context.telemetry.fallback("drumlin-field.orchards.short",
								   "Too few drumlins big enough for every orchard");
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "drumlin field layout";
	const DrumlinFieldOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("drumlin-field.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	// Terrain: water everywhere but the drumlins; the collars and the eskers are sand. The beach
	// pass then puts sand between every drumlin's grass and the water, which is the rim that stays
	// walkable however the crops spread.
	context.stage = "drumlin field terrain";
	TerrainSketch terrain(n, WATER);
	for (int i = 0; i < n; ++i)
		terrain[i] = L.esker[i] || L.collar[i] ? SAND : L.drumlinOf[i] >= 0 ? GRASS : WATER;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	// Colonies: each swarm on its town head, as near the designed spot as the head's grass allows.
	context.stage = "drumlin field colonies";
	if (!settleColonies(
			game, context, "drumlin-starts",
			[&](int team) { return homeGrassMask(map, t, L.homeOf, team); },
			[&](int team)
			{ return teardropHomeSwarm(L.homes[team], L.heading, L.drumlins[team].shape, kHome); }))
		return false;

	context.stage = "drumlin field resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	const auto clear = [&](int i) { return !reserved[i] && clearGround(map, i % t.w, i / t.w); };
	// Kits: the crops on the tail (past the collar, so they spread over the tail and never into
	// the town) and the quarry on the head, each seed searching its own side of the collar only.
	for (int k = 0; k < teams; ++k)
		plantSplitKit(
			map, t, context,
			teardropHomeKit(L.homes[k], L.heading, L.drumlins[k].shape, kHome, kHomeWheat,
							kHomeWood, kHomeQuarry),
			[&](int i) { return L.farmOf[i] == k && clear(i); },
			[&](int i) { return L.homeOf[i] == k && clear(i); });
	// Orchards: cherries, oranges and prunes a third of a turn apart round the middle of each prize
	// drumlin, before the ambient crops so they take the middle. An inn stocked with all three is
	// the strongest prize the game has (GAME_RULES_FOR_MAP_DESIGN.md).
	for (int s : L.orchards)
		plantOrchard(map, t, context, L.drumlins[s].site.x + 0.5, L.drumlins[s].site.y + 0.5,
					 kOrchardRadius, {L.heading}, 2 * kPi * kOrchardRadius / 3, 3, 1,
					 [&](int i) { return L.drumlinOf[i] == s && clear(i); });
	// The farmland: every farm drumlin and every home's tail (the tail is the colony's own farm,
	// and a colony whose farm starts nearly bare scores as a poor start and plays as one: the kit
	// alone left the start scorer's resource depth at a tenth of its reference). Every tile of it
	// is fertile (water within a few tiles all round), so the shares below are of nearly all its
	// grass, laid in patches from a noise field so the ground starts walkable. Outcrops and groves
	// are counted over the whole farmland, though only the farm drumlins get them: a home's tail
	// keeps its quarry on the head and its fruit for the prizes.
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 8, context.stream("drumlin-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 5, context.stream("drumlin-split"));
	const auto farmGround = [&](int i)
	{ return (L.drumlinOf[i] >= teams || L.farmOf[i] >= 0) && clear(i); };
	int fertile = 0;
	for (int i = 0; i < n; ++i)
		fertile += farmGround(i) && fertility.at(i % t.w, i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, farmGround, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int area)
		{
			return GroundAmounts{int(scaledCount(fertile * kFarmWheatPercent / 100, o.wheat)),
								 int(scaledCount(fertile * kFarmWoodPercent / 100, o.wood)),
								 int(scaledCount(area / kTilesPerOutcrop, o.stone)),
								 int(scaledCount(area / kTilesPerGrove, o.fruit))};
		},
		"drumlin-stone", "drumlin-fruit", [&](int i) { return L.drumlinOf[i] >= teams; });
	seedAlgae(map, context, t, "drumlin-algae", o.algae,
			  AlgaeBand::anyWater(kWaterPerAlgae).thriving(0.5));
	secureStartingCrops(game, context, t);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit});
	// The eskers and the beaches join everything; a crop patch could still close a landing, so the
	// cheapest way through crops is opened, never through water: an esker that does not reach is
	// a design failure validateWorld reports, not something to ford.
	context.stage = "drumlin field routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "drumlin field"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int teams = context.request.nbTeams;
	// The promise the map makes: with the eskers shut, no colony can walk to another. An esker is
	// sand on undermap corners and a tile with any sand corner is walkable, so the tiles to shut
	// are every tile touching an esker corner (roadTiles), not the corners alone.
	if (const std::array<int, 2> leak = colonyLeak(map, t, teams, roadTiles(t, L.esker));
		leak[0] >= 0)
		return "Colony " + std::to_string(leak[0]) + " can walk to colony " +
			   std::to_string(leak[1]) + " without an esker: the drumlins touch.";
	// Every collar still parts its town from its tail: no pure grass of a town touches pure grass
	// of a tail (labels 2k for colony k's town and 2k + 1 for its tail; grass is what crops spread
	// over), or the crops would spread across into the town.
	std::vector<int> ground(t.size(), -1);
	std::vector<unsigned char> grass(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		ground[i] = L.homeOf[i] >= 0 ? 2 * L.homeOf[i] : L.farmOf[i] >= 0 ? 2 * L.farmOf[i] + 1 : -1;
		grass[i] = map.isGrass(i % t.w, i / t.w);
	}
	if (const RegionLeak leak = firstRegionLeak(t, grass, ground, [](int, int) { return false; });
		leak.tile >= 0)
		return "Colony " + std::to_string(ground[leak.tile] / 2) + "'s collar has a gap at (" +
			   std::to_string(leak.tile % t.w) + ", " + std::to_string(leak.tile / t.w) + ").";
	// And with the eskers open, every colony can walk to every other.
	return walkFromFirstColony(map, teams, "the drumlins", "along the eskers").error;
}
} // namespace

DrumlinFieldOptions::DrumlinFieldOptions(const GenerationRequest &r)
	: grain(r.option("grain")), drumlinSpacing(r.option("drumlin-spacing")),
	  drumlinLength(r.option("drumlin-length")), waterGap(r.option("water-gap")),
	  homeSize(r.option("home-size")), eskerLoops(r.option("esker-loops")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition drumlinFieldDefinition()
{
	return {
			"drumlin-field",
			43,
			"Drumlin field",
			1,
			false,
			// Sites 20 apart across the grain and drumlins two and a half times as long as wide give a
		// 256 map some fifty drumlins of about 16 by 40 tiles round four homes of 22 by 55, with
		// two fifths of the sketch land (the sweep: 28% pure grass, 16% buildable, once the beaches
		// are laid). The water gap is in undermap corners between any two drumlins: 6 corners is
		// five tiles of pure water, which no unit steps over and no tower shoots across
		// (Channels.h: the banks' grass is 10 tiles apart, past a top tower's 9), while the
		// narrowest, 4, lets a top-level tower on one drumlin shell the next; the spacing widens
		// with the gap so the drumlins keep their size. A quarter more eskers than the tree needs
		// gives most drumlins a second way in without making the field an open plain.
		{GeneratorControl::choice("grain", "Grain", {"Random", "Horizontal", "Vertical", "Diagonal"},
								  0, ControlGroup::Terrain),
		 {"drumlin-spacing", "Drumlin spacing", 16, 32, 2, 20, ControlGroup::Terrain},
		 {"drumlin-length", "Drumlin length", 150, 400, 25, 250, ControlGroup::Terrain},
		 {"water-gap", "Water gap", 4, 12, 1, 6, ControlGroup::Terrain},
		 {"home-size", "Home size", 8, 16, 1, 11, ControlGroup::Layout},
		 {"esker-loops", "Esker loops", 0, 100, 5, 25, ControlGroup::Layout},
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			homesFailure,
			validateWorld,
			{"terrain:natural", "feature:lakes", "style:tight-building"}};
}
