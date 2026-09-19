// SPDX-License-Identifier: GPL-3.0-or-later
#include "TugGenerator.h"
#include "Contact.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Grid.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Regions.h"
#include "Resources.h"
#include "Rivers.h"
#include "Roads.h"
#include "Settlements.h"
#include "Solve.h"
#include "Sketch.h"
#include "Territories.h"
#include "Topology.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
using namespace MapGeneration;

// Tug (id "tug", legacy id 59): a country of home territories parted by a march of no-man's land,
// with a rope of prizes strung through it. Every prize is a fruit grove with a quarry beside it -
// the only fruit on the map and the only quarry worth marching for - and every prize is the same
// walk from every colony.
//
// WHAT THE GAME IS. Your territory feeds you and cannot win for you: it has crops, wood and a small
// quarry, and no fruit at all. The prizes are the rope. Taking one pulls your economy ahead and
// your opponent's back, and because the rope is level the only thing that decides which way it
// moves is play. There is no near end and no far end of this map.
//
// WHY THIS IS A SOLVED MAP AND NOT A DRAWN ONE. The shape of the country is drawn, entirely out of
// the shared toolkit: lakes from a noise field, homelands shared out by growTerritories (equal
// ground per colony by construction), a march opened between them with separateTerritories, and a
// swarm seated the same walk in from the march in every one of them (Homes.h's regionHome). None
// of that needs a search, and a search would be a poor way to get any of it.
//
// What cannot be drawn is the rope. On ground this asymmetric, "every prize is the same walk from
// every colony" is a property of the finished terrain that no amount of careful placement gets
// right: it is a constraint over an N-colony by K-prize matrix of walking costs, on a map with
// lakes and territory borders in the way. So that one decision - which of some hundreds of
// candidate sites in the march become the K prizes - is handed to a search, scored with the
// engine's own cost model (Contact.h) on the actual finished map. Everything else is drawn.
//
// That is the whole argument for aiming a solver at a map: not at the terrain, where it merely
// reinvents noise, but at a small set of discrete decisions whose quality is exactly measurable and
// whose right answer depends on all of the terrain at once. The search here is over a few hundred
// candidates and costs microseconds, because every colony's cost field is flooded once up front and
// a move is then a table lookup. The `levelling` slider turns it off, which is what makes its worth
// something a player - or a reviewer - can measure rather than take on trust.
//
// GAME RULES IT LEANS ON (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md):
// - Fruit is a weapon: it wins hungry enemy units over to your inns. Putting every fruit tree on
//   the rope is what makes the rope worth pulling.
// - Stone never runs out, so the prize quarries are permanent strategic ground, not a resource that
//   runs dry and stops mattering halfway through the game.
// - Resources block walking and building, so the prize groves are open clumps rather than a wall,
//   and the march is otherwise kept clear: it is ground to fight over, not ground to hack through.
// - Grass may not touch water, so layBeaches runs before any deposit is placed.
namespace
{

// The march's prizes: a fruit grove with a quarry beside it, the same at every prize, so the only
// thing that distinguishes one from another is where it is - which is the thing being solved. Both
// radii are what the resource sliders ask for at 100 per cent, scaled by area from there.
constexpr int kGroveRadius = 3;
constexpr int kQuarryRadius = 1;
constexpr int kQuarryOffset = 5;
// The grass clearing kept round every prize in the dry march: enough for the grove, the quarry
// beside it and room to fight over both.
constexpr int kPrizeClearing = 8;
// How far apart two prizes must stand. Prizes in a huddle are one prize, so the rope wants them
// spread - but the demand has to give way as more of them are asked for, or a long rope simply
// cannot be strung and the last few beads end up wherever there was room rather than where the
// search wanted them. Six prizes is the figure the share is quoted for; twice as many get half the
// separation, and it is never more than a quarter of the short side nor less than kLeastPrizeGap.
constexpr double kPrizeGapShare = 0.16;
constexpr int kPrizeGapPrizes = 6;
constexpr int kLeastPrizeGap = 20;
// Room a prize needs around it: its grove, its quarry, and ground to fight on. Measured as
// clearance, so this is half the width of the march at that point; the march control's own minimum
// is set to match, because a march too narrow to hold a prize is a map with no rope.
constexpr int kPrizeRoom = 4;
// How far in from the march a swarm sits, and the slack the search for that site is allowed.
constexpr int kHomeDepth = 16, kHomeDepthSpread = 28, kHomeRoom = 5;
// The nominal home radius the shared kit layout is scaled from (Homes.h); a territory is not round,
// so this is only the reach the kit's seeds are spread over.
constexpr double kKitRadius = 18;
// How the country's own crops are laid: the share of a territory's ground under each, and the
// starter kit every colony gets whatever the sliders say.
constexpr double kWoodOfFarmed = 0.75;
// How coarse the fields are. Finer than the 24 steps a colony's catchment is reckoned over, so that
// every colony has some of both crops within reach whatever its share of the homeland looks like.
constexpr int kFieldGrain = 12;
// The clear town round every swarm that the country's ambient fields keep out of. Wide enough for
// a colony to build in and no wider: a colony's catchment is reckoned 24 steps out, so a town of 17
// left the fields starting almost at the edge of it and what a colony had to farm came down to
// whatever happened to fall in a seven-tile ring. Wood then varied between colonies by more than it
// varied between generators.
constexpr double kTownRadius = 14;
constexpr int kKitWheat = 22, kKitWood = 18, kKitQuarry = 2;
// How far from its kit a colony's own quarry may be sought before the colony is given up on, and
// how near its swarm the home lake must end up.
constexpr int kQuarryReach = 26;
constexpr int kPondReach = 30;
// Every colony's private lake: its size in tiles, how clear of the homeland's edge and its swarm it
// keeps, and how far out from the swarm it may be sought. The size is the same for everyone, which
// is the whole point of it.
/// How far a colony is jostled off the spread that chose it, as a share of the shorter side.
///
/// There is deliberately no recentreSites pass here, though the toolkit has one and most maps want
/// it: walking a site to the middle of its own ground is the exact opposite of this jostle, and it
/// puts back the regular lattice the jostle exists to break.
constexpr double kSiteJostle = 0.11;
// How far a homeland border may bend to follow cheap ground, in growTerritories' units of a
// thousand to the step: four steps' worth.
constexpr int kBorderWander = 4000;
/// How much of its fair share of the map a colony keeps as its own homeland, as a percentage, drawn
/// per seed. The rest of the country is commons: expansion ground, battlefield and the rope. At the
/// low end the colonies are islands of farm in a wide shared country; at the high end they are
/// neighbours with a march between them.
constexpr double kHomelandLeast = 24, kHomelandMost = 66;
/// How densely a homeland is farmed, drawn per seed. This is also what tells a homeland apart from
/// the commons at a glance: the commons is planted to a fixed light share, so the further this is
/// drawn above it the more sharply a colony's own country reads against the open ground.
constexpr double kFarmedLeast = 13, kFarmedMost = 42;
/// How many steps' worth of the relief field the edge of a homeland may wander by, drawn per seed:
/// at the low end a compact country, at the high end one that follows the valleys.
constexpr double kRaggedLeast = 2.0, kRaggedMost = 16.0;
/// How coarse the fraying field is: a few tiles, so a homeland's edge breaks up along its length
/// rather than being shifted bodily.
constexpr int kFrayPeriod = 9;
/// The river, when a seed draws one (Rivers.h). How many beds are drawn and scored before one is
/// laid: the route is the part of a landform that construction cannot settle on its own, because
/// whether a bed is a good one depends on where the homelands and their lakes already are. Few
/// enough that every candidate is scored outright rather than annealed - a search over fourteen
/// things is a search that should have been a loop.
constexpr int kRiverBeds = 14;
/// The bed itself, in undermap corners: wide enough that no unit steps over it, even diagonally.
/// The wander is a share of the side the bed crosses, and it has to be read against that side: at
/// 0.17 over 256 tiles the beds came out as canals, which is the ruled line the head of Rivers.h
/// argues a positional constraint would give. A fourth harmonic wrinkles the long bends.
constexpr double kRiverHalfWidth = 2.6, kRiverWander = 0.30, kRiverSwell = 0.35;
/// Tiles between centre-line points, and how many harmonics the meander is built from.
constexpr double kBedPointStep = 2.0;
constexpr int kBedHarmonics = 4;
/// The sand of a ford, and how near the bed's radius it reaches across.
constexpr double kFordHalfWidth = 2.0, kFordReach = 2.5;
/// How far apart candidate fords stand, in centre-line points. A river crossed every few tiles is
/// not a river; this keeps the crossings far enough apart to be worth choosing between.
constexpr int kFordApart = 14;
/// How far past the bed a bank is read when looking for somewhere to ford.
constexpr double kBankReach = 2.0;
/// Crossings per colony. Rejoining the banks takes as few as one, and a river forded only that far
/// is a wall: measured at 512x512 it left one colony contesting five prizes and another one, which
/// is this map's own rope check failing. The rope wants the march to stay one fighting ground, so
/// the bed is crossed often enough to route a war rather than stop it.
constexpr int kFordsPerColony = 2;
/// What a bed is scored on. Flooding a homeland is the one thing a river here may not do - the
/// whole map rests on every colony having the same water and the same farm - so it outweighs the
/// rest together. Then the bed should lie in the commons, where it shapes the fight rather than
/// somebody's fields, and should put the colonies on both of its banks rather than run round the
/// outside of all of them.
/// A bed through a town is not a tradeable cost but a ruined colony, so it is priced out of reach
/// of the other terms rather than balanced against them: a candidate that touches one never wins.
constexpr double kBedTownWeight = 1000;
/// How much of a bed must lie in the commons for it to be cut at all.
///
/// This is the gate that decides whether a seed has a river, and it is a fact about the country
/// rather than a preference. A bed crossing the map has to pass between the homelands, and there is
/// only room for it to do that when the commons is wide enough to hold it: at 512x512 with four
/// colonies the homelands are so large that the best of fourteen beds still ran 42% of its length
/// through somebody's fields, which puts ambient water in one colony's larder and not another's -
/// the exact inequality the water rule exists to prevent - and left one colony contesting five
/// prizes to another's two. Wide country gets a river; crowded country does not. A landform is
/// character, so a seed is allowed to have none.
///
/// Where the line sits is measured. Over 40 seeds at 256x256 with four colonies, 16 cut a river and
/// none failed; the same sweep at 0.40 also passed, and the golden set - including the 512x512 case
/// a walled march once broke - passes at both. So the floor is not set by validity but by what a
/// river costs: with one, fairness averages 0.871 against 0.910 without. 0.45 buys a river on about
/// four seeds in ten for four points of a static score, which is the trade this map wants.
///
/// An earlier 0.60 was calibrated against a polluted measurement - the bed then carried a spurious
/// straight chord across the whole map (see riverWater in Rivers.h), whose tiles counted as commons
/// and flattered every candidate's share.
constexpr double kLeastCommonsShare = 0.45;
constexpr double kBedHomelandWeight = 40, kBedCommonsWeight = 6;
/// The dry collar between a homeland and the commons. Thin on purpose: it is there to stop a farm
/// creeping out into ground that should be taken rather than grown into, not to wall the map off.
constexpr int kCollarWidth = 6;
/// What the commons carries of its own, as a share of its ground: enough to be worth settling out
/// into, well short of what a homeland grows.
constexpr int kCommonsWheat = 9, kCommonsWood = 8;
// The least ground a colony's homeland needs: a town, a lake, a farm and a door onto the march.
constexpr int kLeastHomelandTiles = 2600;
// How long a map may be before it needs a ring of colonies rather than a line of them.
constexpr int kSquarishAspect = 2;
constexpr int kLeastColoniesOnALongMap = 6;
constexpr int kHomeLakeTiles = 110, kLakeGap = 3, kLakeFromSwarm = 7, kLakeReach = 22;
// How far from a swarm the march's own lakes are pushed back, so the private lake is the only water
// a colony farms beside.
constexpr double kHomeDryMargin = 26;
// What walking costs, for every measurement this map makes: open ground one, a crop three to cut
// through, and water, stone and buildings impassable. The solver and the validator share it, so the
// promise checked is the promise searched for.
constexpr StepCosts kWalk{1, 3, -1, -1, -1};
// What the finished world is held to: every prize a genuine front, and every colony a contender for
// the same number of prizes give or take kFrontTolerance.
//
// The second of those was first written as "every colony's mean walk to the whole rope is equal",
// and that was a mis-statement of the map rather than a hard target. With more than two colonies a
// colony is naturally near the prizes on its own fronts and far from the ones across the country,
// so the mean walk measures the shape of the map at least as much as the fairness of the rope: on a
// 4:1 map it came out 126 steps apart for reasons no arrangement of prizes could fix, and chasing
// it only moved the homelands about to no good end. What a tug actually needs is that everybody has
// the same number of prizes they can fight for on equal terms, which is what this asks for and what
// the search delivers on every shape.
//
// Contest is a tolerance rather than an equality because the cost field the search used is not
// quite the one the finished map has: the prizes themselves are standing on it by then.
// Both scale with the map, because walking costs do: a fixed margin of 14 steps that is generous on
// a 256-tile map is far tighter than the ground can meet on a 512-tile one, where the same journey
// costs twice as much. Held fixed, it left two colonies on a 256 by 512 map with no contested
// ground at all to hang a rope on.
constexpr int kLeastContestMargin = 14;
constexpr int kFrontTolerance = 2;
// The margin a candidate site must already meet before the search will even consider it, chosen
// well inside the tolerance above so that the drift between the cost field the search used and the
// finished map's cannot carry a chosen site past it.
/// How near the nearest colony's walk another colony's has to come for it to count as a contender
/// for that prize, on a map of this size, and what the finished world is then held to.
int contestMargin(const Torus &t) { return std::max(kLeastContestMargin, (t.w + t.h) / 32); }
int contestTolerance(const Torus &t) { return 2 * contestMargin(t); }
// How heavily an unevenly shared rope counts against a less contested one, and how heavily a rope
// whose beads are not spread over everybody's fronts counts against both.
//
// The front term is what makes an elongated map work. Sharing by walking cost alone leaves the
// search free to pile several prizes onto one front and make the sums come out by distance, which
// on a long map means one colony borders half the rope and another borders none of it; four
// colonies on a 4:1 map failed the finished-world share check on both orientations that way. This
// counts, for each colony, the prizes it is one of the two nearest to, and asks for that to come
// out level. Its unit is prizes rather than steps, hence the much larger weight.
constexpr int kShareWeight = 4;
constexpr int kFrontWeight = 30;
// How many placements the rope search tries. A move is a table lookup, so this is microseconds.
constexpr int kRopeMoves = 24000;
constexpr double kRopeHeat[2] = {12.0, 0.05};

struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<int> ownerOf;          // the colony whose homeland a tile is, or -1 for the march
	std::vector<unsigned char> march;  // the no-man's land the rope is strung through
	std::vector<unsigned char> ground; // the mainland: where everything happens
	std::vector<RegionHome> homes;
	double farmed = 0; // the share of a homeland under crops, drawn per seed
	bool hasRiver = false;
	River river;              // the bed this seed drew, when it drew one
	std::vector<int> fords;   // the centre-line points it is crossed at
	std::string failure;
};

/// What this country thinks of a bed laid across it, as named terms so a seed's river can be read
/// back as the brief it was chosen against (Objective, Solve.h).
Objective rateBed(const Layout &L, const Torus &t, int teams,
				  const std::vector<unsigned char> &towns, const std::vector<unsigned char> &bed)
{
	int bedTiles = 0, commonsTiles = 0, townTiles = 0;
	std::vector<double> perColony(teams, 0.0);
	for (int i = 0; i < t.size(); ++i)
	{
		if (!bed[i])
			continue;
		++bedTiles;
		townTiles += towns[i];
		if (L.ownerOf[i] < 0)
			++commonsTiles;
		else
			perColony[L.ownerOf[i]] += 1.0;
	}
	Objective score;
	if (bedTiles == 0)
		return score.add("town", kBedTownWeight, 1.0); // a bed with no water never wins

	// There is no term here for which bank a colony ends up on, and there was one until it was
	// measured. A river that leaves everybody on one side is a coastline rather than a front, so it
	// looked worth scoring - but cutting a torus along a single loop that goes all the way round
	// leaves it connected, the way slitting a bicycle tube along its circumference leaves a sheet.
	// It takes two parallel cuts to part a torus. So every colony is always on one bank, the term
	// was exactly 1.0 on all sixteen seeds that drew a river, and the flood fill per candidate that
	// worked it out was fourteen passes a seed to recompute a constant.
	//
	// A bed that touches nobody's farm is the best case, not the worst. imbalance() reads all-zero
	// shares as "every claimant has none of a thing it needs" and charges a whole point, which is
	// right for wheat and exactly inverted for water somebody did not want: measured, it made every
	// clean bed score 40 and handed the choice to the other terms. Only ask how evenly the water
	// fell once some of it has fallen.
	double touched = 0;
	for (const double share : perColony)
		touched += share;
	score.add("town", kBedTownWeight, double(townTiles));
	score.add("homeland", kBedHomelandWeight, touched > 0 ? imbalance(perColony) : 0.0);
	score.add("commons", kBedCommonsWeight, 1.0 - double(commonsTiles) / double(bedTiles));
	return score;
}

/// This seed's river, if it drew one and the country has room for it.
///
/// The division of labour the whole map argues for, applied to scenery rather than to prizes. The
/// bed is not something a solver could find - see the head of Rivers.h for why a target on where
/// the water goes buys a blob or a ruled line and never a river - so it is drawn, closed and
/// winding and the right width, for the cost of some trigonometry. What construction cannot settle
/// is which bed to cut, because that depends on homelands and lakes already on the ground; that is
/// a handful of discrete options, exactly measurable, so it is scored outright.
///
/// A country with no room simply gets no river. Cutting the best of a bad set anyway would put
/// water through somebody's town or their fields, and a landform is character: a seed may have
/// none, unlike the things that make the map playable at all.
void cutRiver(Layout &L, const Torus &t, int teams, GenerationContext &context, const Brief &brief,
			  std::vector<unsigned char> &water)
{
	if (!brief.on("river"))
		return;

	std::vector<unsigned char> swarms(t.size(), 0);
	for (const RegionHome &home : L.homes)
		swarms[home.site] = 1;
	const std::vector<unsigned char> towns = dilateRound(t, swarms, kTownRadius);
	const RiverStyle style{kRiverHalfWidth, kRiverWander, kRiverSwell, kBedPointStep, kBedHarmonics};
	const RiverChoice chosen = bestRiverAcross(
		t, context, "tug-river", context.bounded("tug-river", 2) != 0, kRiverBeds, style,
		[&](const River &, const std::vector<unsigned char> &bed)
		{ return rateBed(L, t, teams, towns, bed); });

	// The winner carries every measurement that chose it, so the two things a bed must clear are
	// read back off its own score rather than tracked alongside the loop.
	const double commonsShare = 1.0 - chosen.score.residual("commons");
	if (!chosen.found || chosen.score.residual("town") != 0 || commonsShare < kLeastCommonsShare)
	{
		context.telemetry.measure("tug.river.best-commons-share",
								  chosen.found ? commonsShare : 0.0);
		context.telemetry.fallback("tug.river.no-room",
								   "This country had no bed a river could take.");
		return;
	}
	L.hasRiver = true;
	L.river = chosen.bed;
	reportObjective(context.telemetry, "tug.river.bed", chosen.score);
	int bedTiles = 0;
	for (int i = 0; i < t.size(); ++i)
		if (chosen.water[i])
		{
			water[i] = 1;
			++bedTiles;
		}
	context.telemetry.measure("tug.river.tiles", bedTiles);
}

/// The country: lakes, homelands shared out equally, and a march opened between them. Every step of
/// this is a shared primitive doing the job it was written for; none of it is searched.
Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const TugOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int teams = request.nbTeams;

	// What this seed was asked for, in one place.
	//
	// Drawn, not fixed: with constant targets every seed of a solved map comes out with the same
	// character however different its layout, because a search is very good at finding the same
	// answer to the same question. The homeland share and the farmed share are also what tells a
	// colony's own country from the commons at a glance, and the ragged draw is how far its edge
	// follows the lie of the land. A reader looking at an odd seed should be able to see what it
	// was asked for before wondering whether the search failed.
	Brief brief(context, "tug");
	const double homelandShare = brief.target("homeland-share", kHomelandLeast, kHomelandMost);
	L.farmed = brief.target("farmed-share", kFarmedLeast, kFarmedMost);
	const std::int64_t ragged =
		std::int64_t(brief.target("ragged-steps", kRaggedLeast, kRaggedMost) * 64);
	brief.choose({"river"}, 0, 1);

	// The country's water, from a smooth field: the first thing that makes one seed's march
	// different from the next's, and the reason the walk to a prize is never the straight line.
	std::mt19937 &rng = context.stream("tug-terrain");
	const std::vector<int> relief =
		fractalNoise(t.w, t.h, std::max(16, std::min(t.w, t.h) / 6), 4, rng);
	std::vector<unsigned char> water(t.size(), 0);
	const int shore = percentile(relief, std::clamp(o.lakes, 0, 100));
	for (int i = 0; i < t.size(); ++i)
		water[i] = relief[i] < shore;
	// Puddles are not lakes and only cost building room; drop them before anything is measured.
	water = dropSmallRegions(t, water, 24);

	// The homes are spread over the one landmass, so no colony is stranded on an island.
	std::vector<unsigned char> land(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		land[i] = !water[i];
	const std::vector<int> parts = connectedRegions(land, t.w, t.h, true, GridNeighbors::Eight);
	std::vector<int> partSize;
	for (const int part : parts)
		if (part >= 0)
		{
			if (part >= int(partSize.size()))
				partSize.resize(part + 1, 0);
			++partSize[part];
		}
	const int mainland =
		int(std::max_element(partSize.begin(), partSize.end()) - partSize.begin());
	std::vector<unsigned char> mainlandMask(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		mainlandMask[i] = parts[i] == mainland;

	// The homes, as far apart as the mainland's own walking distances allow, then dealt to the
	// colonies so a team number never lands on the same ground map after map.
	std::vector<int> sites =
		farthestSites(t, mainlandMask, mainlandMask, teams, context, "tug-homes");
	if (int(sites.size()) < teams)
	{
		L.failure = "This map has no room to spread the colonies out; use a bigger map or fewer "
					"colonies.";
		return L;
	}
	dealStarts(context, sites, "tug-homes-deal");
	// Jostled off the spread that chose them, which is the whole reason this map stopped looking
	// like a country and started looking like city blocks.
	//
	// farthestSites spreads colonies as far apart as it can, and on a torus the farthest-apart
	// arrangement of four points is a regular 2x2 lattice. growTerritories then shares the ground
	// out in equal areas, and the equal-area partition of a torus from a regular lattice of seeds is
	// exactly a grid of straight lines - which separateTerritories widens into straight streets. No
	// amount of wander in the border cost fixes that, because moving a border anywhere but locally
	// would unbalance the areas the partition exists to keep equal: raising the wander sixty-fold
	// moved about five hundred tiles of march and changed nothing anyone would notice. The premise
	// to break is the regular lattice, not the straightness of the borders it implies.
	const int jostle = std::max(4, int(std::min(t.w, t.h) * kSiteJostle));
	for (int &site : sites)
	{
		const int dx = int(context.bounded("tug-homes", 2 * jostle + 1)) - jostle;
		const int dy = int(context.bounded("tug-homes", 2 * jostle + 1)) - jostle;
		const int moved = t.at(site % t.w + dx, site / t.w + dy);
		if (mainlandMask[moved])
			site = moved;
	}

	// Equal ground per colony, with borders that wander because the noise field makes some tiles
	// cheaper to take than others. This is growTerritories doing exactly what it was written for.
	// It shares out the whole torus, water included, so that the water rule below can move a lake
	// off a homeland without the tile it vacates falling out of that homeland.
	const std::vector<unsigned char> everywhere(t.size(), 1);
	// growTerritories prices a tile at its steps from the seed times a thousand, plus this. So a
	// wander that only spans 0..999 can never buy a border even one tile of detour - it breaks ties
	// within a step and nothing more, and the borders come out as near-exact distance contours.
	// With colonies on the near-regular lattice that farthestSites and recentreSites leave, that
	// read as a grid of city blocks with right-angle crossroads in almost every seed. Spanning
	// several steps' worth lets a border follow the lie of the land instead.
	const auto wander = [&](int i) { return std::int64_t(relief[i]) * kBorderWander / 65535; };
	const auto share = [&](const std::vector<int> &at)
	{
		std::vector<std::vector<int>> seeds;
		for (const int site : at)
			seeds.push_back({site});
		return growTerritories(t, everywhere, seeds, wander);
	};
	const Territories shared = share(sites);
	L.ownerOf = shared.labels;
	// Deliberately not smoothed. smoothLabels straightens a border by pulling every tile towards
	// whatever its neighbourhood mostly is, which is exactly the meander the wander above buys, and
	// running it here undid that: the marches came out as a grid of right-angle streets. A border
	// grown by a race for the cheapest tile is a little ragged, and on this map that is the point -
	// it is the difference between country and city blocks.
	// The march: the ground between the homelands, opened by pushing every border back.

	// A homeland is a core, not a share of everything.
	//
	// Sharing the whole map out between the colonies leaves a map with no neutral ground on it: every
	// tile belongs to somebody, the only unowned strip is the border, and since that strip is what
	// keeps the farms apart there is nowhere at all to expand into. A map wants commons - ground to
	// settle out into, ground to fight over, ground that is nobody's until somebody takes it. So each
	// colony keeps only the nearest kHomelandShare of its fair share and the rest goes back to
	// nobody. Taking the nearest tiles by walk means the homelands come out the same size as each
	// other whatever shape the territory around them was, which is the fairness that actually
	// matters, and it leaves better than half the country neutral.
	const std::int64_t homeland = std::int64_t(std::int64_t(t.size()) * homelandShare) / (100 * teams);
	// A field of its own, and a fine one. Bending the ranking with `relief` did almost nothing
	// because relief undulates about every forty tiles and a homeland is only sixty or so across:
	// over that span it is nearly a gradient, so it slid the square sideways instead of breaking up
	// its edge. An edge frays at the scale of the fraying, not at the scale of the landscape.
	const std::vector<int> fray = fractalNoise(t.w, t.h, kFrayPeriod, 3, rng);
	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> own(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			own[i] = L.ownerOf[i] == k;
		const std::vector<int> walk = stepsFrom(t, tileMask(t, {sites[k]}), own);
		// Ranked by the walk bent with the lie of the land, not by the walk alone.
		//
		// The walk here is eight-connected, and eight-connected distance is the Chebyshev metric,
		// whose balls are squares - so taking a colony's nearest N tiles by walk carved it a square
		// homeland, and the collar dilated round that came out as the square sand outline these maps
		// all wore. The collar was never the problem. Adding a few steps' worth of the relief field
		// to the ranking lets the boundary wander into the cheap ground and out of the dear, which is
		// what makes a country's edge look like a country's edge. The swing is drawn per seed, so
		// one seed's homelands are compact and the next's sprawl along the valleys.
		std::vector<std::pair<std::int64_t, int>> byWalk;
		for (int i = 0; i < t.size(); ++i)
			if (own[i] && walk[i] >= 0)
				byWalk.push_back(
					{std::int64_t(walk[i]) * 64 + std::int64_t(fray[i]) * ragged / 65535, i});
		std::sort(byWalk.begin(), byWalk.end());
		for (size_t n = size_t(homeland); n < byWalk.size(); ++n)
			L.ownerOf[byWalk[n].second] = -1;
		// Ground the walk could not reach is not part of the homeland either.
		for (int i = 0; i < t.size(); ++i)
			if (own[i] && walk[i] < 0)
				L.ownerOf[i] = -1;
	}

	// The minimum gap the march control asks for, applied once the cores are settled. Bounding the
	// homelands already parts them on most maps; this is what still honours the control when the
	// colonies are packed tightly enough that their cores would otherwise touch. It runs after the
	// bounding, never before: on a crowded map it can strip a whole territory, seed and all, and a
	// colony bounded out of an already-stripped territory ends up with no homeland at all.
	separateTerritories(t, L.ownerOf, std::max(4, o.march));

	// The water rule, and the reason this map does not simply take the lakes the noise gave it.
	//
	// Crops regrow from the water beside them, so water in a homeland is not scenery: it is that
	// colony's larder refilling. Noise lakes land where they land, and measured that way one colony
	// had four times another's fertile ground and a third had none - a difference no amount of
	// levelling the rope can make up for, because it compounds over the whole game.
	//
	// So: no ambient water in anybody's homeland, one private lake of exactly the same size beside
	// every swarm, and every other lake out in the march where it belongs to nobody and shapes the
	// routes the rope is strung along. Equal by construction, which is always better than equal by
	// search when construction can reach it.
	for (int i = 0; i < t.size(); ++i)
		if (L.ownerOf[i] >= 0)
			water[i] = 0;
	L.march.assign(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		L.march[i] = L.ownerOf[i] < 0 && !water[i];

	// Every swarm the same walk in from its own doorstep onto the march, so no colony starts nearer
	// the rope than another. regionHome is the toolkit's answer to "a home in ground of any shape".
	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> home(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			home[i] = L.ownerOf[i] == k;
		std::vector<int> door;
		for (int i = 0; i < t.size(); ++i)
		{
			if (!home[i])
				continue;
			const int x = i % t.w, y = i / t.w;
			// The rim of the homeland, whatever lies beyond it. Asking specifically for a tile
			// touching the commons fails a homeland whose whole edge happens to be shore, which
			// bounding the homelands to a core made reachable: they are small enough now to sit
			// wholly against their own lake.
			for (const auto &step : kCardinalSteps)
				if (L.ownerOf[t.at(x + step[0], y + step[1])] != k)
				{
					door.push_back(i);
					break;
				}
		}
		if (door.empty())
		{
			L.failure = "A colony's homeland has no way out onto the march; use a bigger map or a "
						"narrower march.";
			return L;
		}
		const RegionHome home_ = regionHome(t, home, door, kHomeDepth, kHomeDepthSpread, kHomeRoom);
		if (home_.site < 0)
		{
			L.failure = "A colony's homeland has no room for a swarm; use a bigger map, fewer "
						"colonies or a narrower march.";
			return L;
		}
		L.homes.push_back(home_);
	}

	// The march's own lakes are pushed back off the towns before the private ones are cut. A swarm
	// sits about kHomeDepth from the march, so a lake lying against the border is still inside the
	// neighbourhood a colony farms, and one colony drawing a march lake next door while another
	// draws none puts the fertility back out of step - which is the very thing the private lakes
	// are here to fix.
	std::vector<unsigned char> townSeeds(t.size(), 0);
	for (const RegionHome &home : L.homes)
		townSeeds[home.site] = 1;
	const std::vector<unsigned char> dry = dilateRound(t, townSeeds, kHomeDryMargin);
	for (int i = 0; i < t.size(); ++i)
		if (dry[i])
			water[i] = 0;

	// Every colony's private lake, beside its swarm and exactly the same size as every other's.
	// growLakeBeside exists for this: it takes a target in tiles and grows to it, so homelands of
	// different shapes still end up with the same water. The flank it sits on is drawn per colony,
	// so the homes are not copies of each other.
	std::vector<int> queued(t.size(), 0);
	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> home(t.size(), 0), blocked(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
		{
			home[i] = L.ownerOf[i] == k;
			blocked[i] = L.ownerOf[i] != k;
		}
		const int sx = L.homes[k].site % t.w, sy = L.homes[k].site / t.w;
		for (int dy = -kHomeRoom; dy <= kHomeRoom; ++dy)
			for (int dx = -kHomeRoom; dx <= kHomeRoom; ++dx)
				blocked[t.at(sx + dx, sy + dy)] = 1;
		std::vector<int> door;
		for (int i = 0; i < t.size(); ++i)
			if (home[i] && L.march[i])
				door.push_back(i);
		const std::vector<int> depth = stepsFrom(t, tileMask(t, {L.homes[k].site}), home);
		const std::vector<int> room = stepsFrom(t, blocked);
		const int side = context.bounded("tug-lakes", 2) ? 1 : -1;
		const int grown = growLakeBeside(
			t, water, depth, room, kLakeGap, kHomeLakeTiles, L.homes[k].site, L.homes[k].axis, side,
			kLakeFromSwarm, kLakeReach, [&](int i) { return relief[i] / 65535.0; }, queued, k + 1);
		context.telemetry.measure("tug.home.lake-tiles", grown, k);
		if (grown < kHomeLakeTiles)
			context.telemetry.fallback("tug.home.lake-short",
									   "A homeland had no room for its whole lake.", k);
	}

	cutRiver(L, t, teams, context, brief, water);

	L.ground.assign(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		L.ground[i] = !water[i];

	// The crossings. That the country stays in one piece is an invariant, not character, so it is
	// construction that guarantees it: fords are opened greedily until every bank is joined again
	// (fordsToRejoin). Where the rest of the crossings fall would be a thing worth searching; there
	// are none yet, and a search over the empty set is not worth writing.
	if (L.hasRiver)
	{
		const std::vector<int> banks =
			connectedRegions(L.ground, t.w, t.h, true, GridNeighbors::Eight);
		int pieces = 0;
		for (const int bank : banks)
			pieces = std::max(pieces, bank + 1);
		const std::vector<RiverFord> sites =
			fordSites(t, L.river, L.ground, banks, kBankReach, kFordApart);
		const std::vector<int> taken =
			fordsSpreadAlong(sites, fordsToRejoin(sites, pieces), teams * kFordsPerColony,
							 int(L.river.line.size()));
		for (const int site : taken)
			L.fords.push_back(sites[site].index);
		context.telemetry.measure("tug.river.ford-sites", int(sites.size()));
		context.telemetry.measure("tug.river.fords", int(L.fords.size()));
	}

	L.terrain.assign(t.size(), GRASS);
	for (int i = 0; i < t.size(); ++i)
		if (water[i])
			L.terrain[i] = WATER;
	layBeaches(L.terrain, t);
	// Fords are sand laid over the bed's water, so they go on after the beaches, whose own sand they
	// leave alone. The ground and the march are then re-read from the terrain, because a ford is
	// walkable and the prizes may be strung along one.
	for (const int ford : L.fords)
		layFord(L.terrain, t, L.river, ford, kFordHalfWidth, kFordReach);
	if (L.hasRiver)
		for (int i = 0; i < t.size(); ++i)
		{
			L.ground[i] = L.terrain[i] != WATER;
			L.march[i] = L.ownerOf[i] < 0 && L.ground[i];
		}

	context.telemetry.measure("tug.ground.mainland-tiles", partSize[mainland]);
	// What growTerritories dealt out, which is the equal-ground guarantee the map's fairness rests
	// on. It is named for the territory and not the homeland because that is what it is: it is read
	// before the march is opened, so it does not move when the march control widens the gap. The
	// homeland a colony actually keeps is counted below, from the finished ground.
	context.telemetry.measure("tug.territory.smallest-tiles", shared.smallest());
	context.telemetry.measure("tug.territory.largest-tiles", shared.largest());
	int marchTiles = 0;
	std::vector<int> homelandTiles(teams, 0);
	for (int i = 0; i < t.size(); ++i)
	{
		marchTiles += L.march[i];
		if (L.ownerOf[i] >= 0 && L.ownerOf[i] < teams && L.ground[i])
			++homelandTiles[L.ownerOf[i]];
	}
	context.telemetry.measure("tug.march.tiles", marchTiles);
	// The homelands as they finish: what the march control takes away, and what the lakes and the
	// river leave. The territory above is equal by construction; this is not promised to be, and the
	// gap between the two is the part of a colony's ground that the water took.
	const auto [least, most] = std::minmax_element(homelandTiles.begin(), homelandTiles.end());
	context.telemetry.measure("tug.homeland.smallest-tiles", *least);
	context.telemetry.measure("tug.homeland.largest-tiles", *most);
	return L;
}

// ---------------------------------------------------------------------------
// The one thing that is searched for: a level rope
// ---------------------------------------------------------------------------

/// What a colony's walk costs when there is no walk: more than any real one, so the search treats
/// an unreachable prize as the failure it is without any special case.
constexpr int kNoWalk = 1 << 20;

/// What the rope is scored on. Two properties, because either one alone describes a map nobody
/// would want to play.
///
/// The first formulation tried here asked for one property - every prize the same walk from every
/// colony - and it was the wrong property twice over. It is over-constrained: a site equidistant
/// from all of four colonies lies near one place on the map, so asking for six such sites that are
/// also well separated asks for something the ground does not contain, and the search stalled at a
/// spread of 31 steps having started at 72. And it was not even what a tug of war wants, because a
/// rope with no near end for anybody is a rope nobody can start pulling.
///
/// What a tug wants is that each prize sits on a front between the two colonies contending for it,
/// and that no colony is near more of the rope than any other. Both are satisfiable at once, and
/// together they say the real thing: every prize is a fight, and the fights are shared out evenly.
struct RopeScore
{
	int contest = 0; // mean over prizes of how much nearer the closest colony is than the next
	int share = 0;   // how unevenly the colonies' walks to the whole rope are shared, per prize
	int fronts = 0;  // how unevenly the prizes are spread over the colonies' fronts
	/// Contest is already guaranteed by the candidate filter, so what the search is really spending
	/// its moves on is the other two; contest stays in to break ties towards the more contested of
	/// two otherwise equal ropes, but it cannot be allowed to outvote what is still at stake.
	int total() const { return contest + kShareWeight * share + kFrontWeight * fronts; }
};

RopeScore scoreRope(const std::vector<int> &prize, const std::vector<std::vector<int>> &cost,
					int margin)
{
	RopeScore score;
	const int teams = int(cost.size()), prizes = int(prize.size());
	if (prizes == 0 || teams == 0)
		return score;
	std::vector<std::int64_t> share(teams, 0);
	std::vector<int> onFront(teams, 0), walk(teams);
	std::int64_t contest = 0;
	for (const int site : prize)
	{
		int nearest = kNoWalk, next = kNoWalk;
		for (int k = 0; k < teams; ++k)
		{
			walk[k] = cost[k][site] < 0 ? kNoWalk : cost[k][site];
			share[k] += walk[k];
			if (walk[k] < nearest)
			{
				next = nearest;
				nearest = walk[k];
			}
			else if (walk[k] < next)
				next = walk[k];
		}
		contest += teams > 1 ? next - nearest : 0;
		// Who is a contender for this prize: everyone whose walk is within the contest margin of
		// the nearest colony's, not simply the two nearest. At a prize that is doing its job those
		// walks are nearly tied, so "the two nearest" flips between colonies on a step or two of
		// difference and neither the search nor the validator can agree with itself from one
		// measurement to the next. A band is stable, and it is also the truer question: can this
		// colony fight for this prize on equal terms?
		for (int k = 0; k < teams; ++k)
			onFront[k] += walk[k] - nearest <= margin;
	}
	const auto [least, most] = std::minmax_element(share.begin(), share.end());
	const auto [fewest, mostFronts] = std::minmax_element(onFront.begin(), onFront.end());
	score.contest = int(contest / prizes);
	score.share = int((*most - *least) / prizes);
	score.fronts = *mostFronts - *fewest;
	return score;
}

struct Rope
{
	std::vector<int> prize; // the chosen sites, as tiles
	RopeScore dealt, solved;
	SolveReport run;
	int candidates = 0;
};

/// Chooses which of the march's candidate sites carry the prizes: every prize on a front between
/// the two colonies contending for it, the rope shared evenly between all of them, and no two
/// prizes in a huddle.
///
/// This is the only search in the generator. It is cheap because the expensive part - what it costs
/// each colony to walk anywhere - was flooded once before it started, so scoring a whole rope is a
/// few dozen table lookups. Annealing rather than a greedy pick because the prizes are emphatically
/// not independent: the share term couples every prize to every other, and the separation rule
/// couples them again, so the greedy choice for an early prize routinely leaves no good site for a
/// later one.
Rope levelRope(const std::vector<int> &candidates, const std::vector<std::vector<int>> &cost,
			   const Torus &t, int wanted, int gap, int levelling, GenerationContext &context)
{
	Rope rope;
	rope.candidates = int(candidates.size());
	if (candidates.empty() || wanted <= 0)
		return rope;
	const auto apart = [&](int site, int ignore)
	{
		for (size_t p = 0; p < rope.prize.size(); ++p)
			if (int(p) != ignore &&
				t.dist2(site % t.w, site / t.w, rope.prize[p] % t.w, rope.prize[p] / t.w) < gap * gap)
				return false;
		return true;
	};
	// A first rope of separated sites, taken at random: this is what an ordinary generator would
	// place, and it is exactly what the search is measured against.
	for (int k = 0; k < wanted; ++k)
	{
		int chosen = -1;
		for (int attempt = 0; attempt < 256 && chosen < 0; ++attempt)
		{
			const int site = candidates[context.bounded("tug-rope", std::uint32_t(candidates.size()))];
			if (apart(site, -1))
				chosen = site;
		}
		if (chosen < 0)
			break;
		rope.prize.push_back(chosen);
	}
	rope.dealt = rope.solved = scoreRope(rope.prize, cost, contestMargin(t));
	// Levelling buys search: the slider is how many proposals the solver gets. At zero the rope stays
	// where chance put it, which is the baseline the map's telemetry reports beside the solved figure.
	const Anneal schedule{int(std::int64_t(kRopeMoves) * std::clamp(levelling, 0, 100) / 100),
						  kRopeHeat[0], kRopeHeat[1], "tug-rope"};
	int slot = -1, was = -1;
	std::vector<int> best = rope.prize;
	rope.run = anneal(
		schedule, context,
		[&]
		{
			if (rope.prize.empty())
				return false;
			slot = int(context.bounded("tug-rope", std::uint32_t(rope.prize.size())));
			const int site =
				candidates[context.bounded("tug-rope", std::uint32_t(candidates.size()))];
			if (!apart(site, slot))
				return false;
			was = rope.prize[slot];
			rope.prize[slot] = site;
			return true;
		},
		[&]
		{
			rope.solved = scoreRope(rope.prize, cost, contestMargin(t));
			return double(rope.solved.total());
		},
		[&] { rope.prize[slot] = was; }, [&] { best = rope.prize; },
		[&] { rope.prize = best; });
	// anneal leaves `solved` holding whatever the last proposal scored, accepted or not; the state
	// the run actually ended on is the one to report.
	rope.solved = scoreRope(rope.prize, cost, contestMargin(t));
	return rope;
}

/// Every colony's cost to walk anywhere on the finished map, under the map's one cost model.
std::vector<std::vector<int>> colonyCosts(const Map &map, const Torus &t, int teams)
{
	std::vector<std::vector<int>> cost;
	for (const auto &units : unitTilesByTeam(map, teams))
		cost.push_back(costsFrom(map, t, units, kWalk));
	return cost;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "tug country";
	const TugOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "tug terrain";
	writeUndermap(map, L.terrain);

	context.stage = "tug colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> home(size_t(t.size()), 0);
		for (int i = 0; i < t.size(); ++i)
			home[i] = L.ownerOf[i] == team && map.isGrass(i % t.w, i / t.w);
		return home;
	};
	const auto anchor = [&](int team) { return L.homes[team].swarm; };
	if (!settleColonies(game, context, "tug-starts", homeMask, anchor))
		return false;

	context.stage = "tug homelands";
	// What a homeland carries: a starter kit at the swarm, then fields and woods over its own
	// ground. No fruit, and no quarry worth marching for: those are on the rope.
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	// Wheat and wood get a field each, and both fields are finer than a colony's own neighbourhood.
	// Laying wood on the wheat field inverted looks like a tidy way to keep the two crops in
	// separate patches, and it is a trap: the two are then perfectly anti-correlated, and since the
	// share is taken over a whole homeland while a colony farms only the part of it within reach, a
	// colony can land wholly on one side of the field. Measured that way one colony had 531 wheat
	// and 46 wood in its catchment while another had 43 wheat and 401 wood on the same map.
	std::mt19937 &fields = context.stream("tug-fields");
	const std::vector<int> wheatGrain = fractalNoise(t.w, t.h, kFieldGrain, 3, fields);
	const std::vector<int> woodGrain = fractalNoise(t.w, t.h, kFieldGrain, 3, fields);
	// A town round every swarm that the country's fields stay out of. The kit's own crops go inside
	// it, placed where the design wants them; everything ambient starts beyond it. Without this the
	// fields simply cover whatever the noise points them at, and how much building room a colony
	// ends up with is a matter of luck: one colony measured 298 four-by-four sites in its
	// neighbourhood against another's 1284 on the same map.
	std::vector<unsigned char> centres(t.size(), 0);
	for (const RegionHome &home : L.homes)
		centres[home.site] = 1;
	const std::vector<unsigned char> towns = dilateRound(t, centres, kTownRadius);
	for (int k = 0; k < teams; ++k)
	{
		const auto ownGround = [&](int i)
		{ return L.ownerOf[i] == k && !reserved[i] && clearGround(map, i % t.w, i / t.w); };
		const auto openCountry = [&](int i) { return ownGround(i) && !towns[i]; };
		// The kit's crops round the pond. Its quarry is placed separately and with a far wider
		// search than the kit frame's own: on ground this irregular the frame's ten-tile box can
		// land wholly on water or on another colony's side of a border, and a colony that starts
		// with no stone at all cannot build a tower. Seed 7 left one colony quarryless that way.
		plantOpenHomeKit(map, t, context, L.homes[k].kitCentre, L.homes[k].axis, kKitRadius,
						 kKitWheat, kKitWood, -1, ownGround);
		if (const int quarry = seedNear(t, int(std::lround(L.homes[k].kitCentre.x)),
										int(std::lround(L.homes[k].kitCentre.y)), kQuarryReach,
										ownGround);
			quarry >= 0)
			placeResourceClump(map, context, MapGeneratorPoint(quarry % t.w, quarry / t.w), STONE,
							   kKitQuarry);
		std::vector<int> ground;
		for (int i = 0; i < t.size(); ++i)
			if (openCountry(i))
				ground.push_back(i);
		plantCoverShare(map, t, ground, WHEAT, int(scaledCount(std::int64_t(L.farmed), o.wheat)),
						[&](int i) { return wheatGrain[i]; });
		ground.clear();
		for (int i = 0; i < t.size(); ++i)
			if (openCountry(i))
				ground.push_back(i);
		plantCoverShare(map, t, ground, WOOD,
						int(scaledCount(std::int64_t(L.farmed * kWoodOfFarmed), o.wood)),
						[&](int i) { return woodGrain[i]; });
	}

	context.stage = "tug rope";
	// The march's candidate sites: open ground out in the no-man's land with room for a prize and
	// the fighting round it.
	const std::vector<std::vector<int>> cost = colonyCosts(map, t, teams);
	std::vector<unsigned char> open(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		open[i] = L.march[i] && map.isGrass(i % t.w, i / t.w) && clearGround(map, i % t.w, i / t.w);
	const std::vector<int> room = clearance(t, open);
	// Only genuinely contested ground is a candidate at all. A prize is required to sit on a front,
	// so a site that no search could ever make contested has no business being proposed: filtering
	// it out up front turns the map's promise from something the search hopes to reach into
	// something the candidate set guarantees. It matters most where the march is widest - few
	// colonies on a big map - where most of the no-man's land is plainly nearer one colony than any
	// other and a search over all of it spends its moves in places that can never qualify. Before
	// this filter, three colonies on a 512-tile map produced a valid rope on four seeds in eight.
	std::vector<int> candidates, walk(teams);
	for (int i = 0; i < t.size(); ++i)
	{
		if (!open[i] || room[i] < kPrizeRoom)
			continue;
		bool reachable = true;
		for (int k = 0; k < teams; ++k)
		{
			walk[k] = cost[k][i];
			reachable = reachable && walk[k] >= 0;
		}
		if (!reachable)
			continue;
		std::sort(walk.begin(), walk.end());
		if (teams < 2 || walk[1] - walk[0] <= contestMargin(t))
			candidates.push_back(i);
	}
	context.telemetry.measure("tug.rope.contested-candidates", int(candidates.size()));
	// Too little contested ground for the whole rope is not a failure: a shorter rope is still a
	// rope, and it is a far better answer than refusing the map. Two colonies have only one front
	// between them, so a long rope asked for on a cramped march simply cannot be strung, and the
	// fallback below records how many beads it actually managed.
	if (candidates.size() < 2)
	{
		context.detail = "The march has no contested ground to hang a rope on; use a wider march, "
						 "fewer colonies or a bigger map.";
		return false;
	}
	const int side = std::min(t.w, t.h);
	const int gap = std::clamp(int(side * kPrizeGapShare * kPrizeGapPrizes / std::max(2, o.prizes)),
							   kLeastPrizeGap, side / 4);
	const Rope rope = levelRope(candidates, cost, t, o.prizes, gap, o.levelling, context);
	if (rope.prize.empty())
	{
		context.detail = "The march has no room for a prize; use a bigger map, fewer colonies or a "
						 "wider march.";
		return false;
	}
	context.telemetry.measure("tug.rope.candidates", rope.candidates);
	context.telemetry.measure("tug.rope.prizes-wanted", o.prizes);
	context.telemetry.measure("tug.rope.prizes-strung", int(rope.prize.size()));
	reportSolve(context.telemetry, "tug.rope", rope.run);
	context.telemetry.measure("tug.rope.contest-dealt", rope.dealt.contest);
	context.telemetry.measure("tug.rope.contest-solved", rope.solved.contest);
	context.telemetry.measure("tug.rope.share-dealt", rope.dealt.share);
	context.telemetry.measure("tug.rope.share-solved", rope.solved.share);
	if (int(rope.prize.size()) < o.prizes)
		context.telemetry.fallback("tug.rope.short",
								   "The march had room for fewer prizes than were asked for.");

	// The march goes dry, now that the rope is decided and before a single prize is placed.
	//
	// This is what the map is called after. Sand carries no building and no deposit and grows
	// nothing, so the march is not ground anyone can expand into - it is only ground to cross and to
	// hold. The homelands are therefore fixed for the whole game and the only thing that can move is
	// the rope. It also makes the map legible, which it was not before: as plain grass the march was
	// invisible between two farmed countries, and a player could not see the front they were
	// standing on.
	//
	// The prizes keep a clearing of grass around them, because a deposit needs grass to stand on:
	// each one reads as an oasis in the waste, which is exactly what it is worth.
	TerrainSketch dry = L.terrain;
	std::vector<unsigned char> clearing(t.size(), 0);
	for (const int site : rope.prize)
	{
		const int sx = site % t.w, sy = site / t.w;
		for (int dy = -kPrizeClearing; dy <= kPrizeClearing; ++dy)
			for (int dx = -kPrizeClearing; dx <= kPrizeClearing; ++dx)
				if (dx * dx + dy * dy <= kPrizeClearing * kPrizeClearing)
					clearing[t.at(sx + dx, sy + dy)] = 1;
	}
	// Only a collar of it goes dry, not the whole commons. Sand carries no building and grows
	// nothing, so sanding every unowned tile was the same as saying "there is nowhere to expand" -
	// the map had a homeland each and a no-man's land nobody could ever use. A thin dry ring round
	// each homeland does the job the sand was actually for: a farm cannot creep out of its own
	// country. Past the collar the commons is living ground, to be settled if you can hold it.
	std::vector<unsigned char> owned(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		owned[i] = L.ownerOf[i] >= 0;
	const std::vector<unsigned char> collar = dilateRound(t, owned, kCollarWidth);
	int sanded = 0;
	for (int i = 0; i < t.size(); ++i)
		if (L.march[i] && collar[i] && !clearing[i] && dry[i] == GRASS)
		{
			dry[i] = SAND;
			++sanded;
		}
	layBeaches(dry, t);
	writeUndermap(map, dry);
	context.telemetry.measure("tug.march.sand-tiles", sanded);

	// Every prize is the same prize: a grove of one fruit with a quarry beside it, on the one patch
	// of living ground for a long way in any direction. Identical value is what leaves position as
	// the only thing that distinguishes one prize from another, which is the thing the search just
	// levelled.
	for (const int site : rope.prize)
	{
		// The grove is planted open, every other tile, not as a solid clump. Fruit can never be
		// cleared, so a solid one would be a permanent wall standing exactly where the fighting is
		// meant to happen - and worse than that, a wall whose two sides are not the same walk from
		// everywhere, which quietly undoes the levelling the search just did. Planted solid at this
		// radius it moved one colony 43 steps out of step on seed 21.
		const int fruit = CHERRY + int(context.bounded("tug-prizes", 3));
		const int sx = site % t.w, sy = site / t.w;
		// The grove answers to the fruit slider, like every other resource on the map answers to
		// its own. The radius is scaled by area rather than directly, so the slider delivers the
		// proportion of fruit it says; scaled directly, 300 per cent would ring each prize with a
		// nine-times grove, and fruit cannot be cleared, so that is a permanent wall planted exactly
		// where the fighting is supposed to happen.
		const int groveRadius = scaledRadius(kGroveRadius, o.fruit);
		for (int dy = -groveRadius; dy <= groveRadius; ++dy)
			for (int dx = -groveRadius; dx <= groveRadius; ++dx)
			{
				const int i = t.at(sx + dx, sy + dy);
				if (dx * dx + dy * dy <= groveRadius * groveRadius && (dx + dy) % 2 == 0 &&
					clearGround(map, i % t.w, i / t.w) &&
					map.isResourceAllowed(i % t.w, i / t.w, fruit))
					map.setResource(i % t.w, i / t.w, fruit, 1);
			}
		const double angle = context.bounded("tug-prizes", 360) * kPi / 180.0;
		const int qx = t.x(site % t.w + int(std::lround(kQuarryOffset * std::cos(angle))));
		const int qy = t.y(site / t.w + int(std::lround(kQuarryOffset * std::sin(angle))));
		if (const int seed = seedNear(t, qx, qy, 4, [&](int i)
									  { return clearGround(map, i % t.w, i / t.w); });
			seed >= 0)
			placeResourceClump(map, context, MapGeneratorPoint(seed % t.w, seed / t.w), STONE,
							   scaledRadius(kQuarryRadius, o.stone));
	}

	// What the commons carries: enough crop and wood to be worth settling out into, thinner than a
	// homeland's so that leaving home is a decision and not an obvious upgrade. This is the
	// expansion ground the map was missing.
	std::vector<int> open2;
	for (int i = 0; i < t.size(); ++i)
		if (L.march[i] && !reserved[i] && clearGround(map, i % t.w, i / t.w) &&
			map.isGrass(i % t.w, i / t.w))
			open2.push_back(i);
	context.telemetry.measure(
		"tug.commons.wheat-tiles",
		plantCoverShare(map, t, open2, WHEAT, int(scaledCount(kCommonsWheat, o.wheat)),
						[&](int i) { return wheatGrain[i]; }));
	open2.clear();
	for (int i = 0; i < t.size(); ++i)
		if (L.march[i] && !reserved[i] && clearGround(map, i % t.w, i / t.w) &&
			map.isGrass(i % t.w, i / t.w))
			open2.push_back(i);
	context.telemetry.measure(
		"tug.commons.wood-tiles",
		plantCoverShare(map, t, open2, WOOD, int(scaledCount(kCommonsWood, o.wood)),
						[&](int i) { return woodGrain[i]; }));

	context.stage = "tug openings";
	secureStartingCrops(game, context, t);
	openColonyRoutes(map, context, t, kWalk);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, 100, o.fruit});
	return true;
}

/// Why a request cannot be a tug, or "". A rope needs two ends: with a single colony
/// separateTerritories has nothing to part, there is no march, and the map has nothing to be about.
std::string requestFailure(const GenerationRequest &request)
{
	if (request.nbTeams < 2)
		return "A tug needs two sides; use at least two colonies.";
	// Each homeland has to hold a town, a lake, a farm and a way out onto the march. Below about
	// this much ground per colony it holds none of them and every seed fails late instead of the
	// request failing early.
	const std::int64_t width = 1 << request.wDec, height = 1 << request.hDec;
	if (width * height / request.nbTeams < kLeastHomelandTiles)
		return "Too many colonies for this map to give each a homeland; use a bigger map or fewer "
			   "colonies.";
	// A long map needs enough colonies to ring it. With only a few, the homelands string out along
	// the length and the fronts between them come out lopsided: some prizes end up with one obvious
	// owner and the rope cannot be shared out however the search places it. Three, four and five
	// colonies on a 4:1 map failed the finished-world check on every seed tried; six and more are
	// fine, and so is any count once the map is squarer than 2:1.
	const std::int64_t longer = std::max(width, height), shorter = std::min(width, height);
	if (longer > kSquarishAspect * shorter && request.nbTeams < kLeastColoniesOnALongMap)
		return "A map this long needs more colonies to ring it; use a squarer map or at least six "
			   "colonies.";
	return "";
}

/// The finished world against the promise. The rope's levelness is checked directly, from the
/// fruit standing on the map: fruit grows nowhere else on this map, so each connected grove is a
/// prize, and every colony's walk to it must be within kLevelTolerance of every other's. This is
/// the same measurement the search minimised, taken again on the finished world with the prizes
/// themselves now standing in the way, which is why it is a tolerance and not an equality.
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	const int teams = context.request.nbTeams;
	const Torus t(game.map);
	if (const std::string cut = walkFromFirstColony(game.map, teams, "the country", "").error;
		!cut.empty())
		return cut;
	if (const std::string hungry = startingAccessFailure(
			game.map, teams, {{WHEAT, 24, "wheat"}, {WOOD, 32, "wood"}, {STONE, 40, "stone"}});
		!hungry.empty())
		return hungry;
	// Every colony's own water. The engine regrows crops from the water beside them, so a homeland
	// with none has a larder that empties and never refills, however much wheat it started with.
	std::vector<unsigned char> wet(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		wet[i] = game.map.isWater(i % t.w, i / t.w);
	const std::vector<std::int64_t> toWater = distanceSquaredTo(t, wet);
	const std::vector<std::vector<int>> units = unitTilesByTeam(game.map, teams);
	for (int k = 0; k < teams; ++k)
	{
		std::int64_t nearest = -1;
		for (const int tile : units[k])
			if (nearest < 0 || toWater[tile] < nearest)
				nearest = toWater[tile];
		if (nearest < 0 || nearest > std::int64_t(kPondReach) * kPondReach)
			return "Colony " + std::to_string(k) + " has no water near its homeland to grow on.";
	}

	std::vector<unsigned char> fruit(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		const int type = game.map.getResource(i % t.w, i / t.w).type;
		fruit[i] = type >= CHERRY && type < CHERRY + 3;
	}
	const std::vector<int> grove = connectedRegions(fruit, t.w, t.h, true, GridNeighbors::Eight);
	int groves = 0;
	for (const int label : grove)
		groves = std::max(groves, label + 1);
	if (groves == 0)
		return "The march carries no prize.";
	// What the rope is held to depends on what was asked of it. Levelling at zero is a request for a
	// rope dealt by chance, and a map that was never promised a level rope is not a broken one; the
	// telemetry still reports what the rope came out at, so the difference is on the record either
	// way. Every other promise on this map holds at every setting.
	//
	// The distinction is not academic. Measured over 16 seeds at 256x256 with four colonies, a rope
	// dealt and left alone misses the contest tolerance on every single seed - 16 out of 16 - with a
	// median contest of 30 steps against a tolerance of 28 and no seed close to it. The search is
	// not polish on this map; it is the thing that makes the map possible at all.
	if (context.request.option("levelling") <= 0)
		return "";

	const std::vector<std::vector<int>> cost = colonyCosts(game.map, t, teams);
	// A worker harvests from beside a deposit, never from on it, so the walk that matters to a
	// grove is the walk to the ground around it.
	std::vector<int> contenders(teams, 0);
	for (int g = 0; g < groves; ++g)
	{
		std::vector<int> reach(teams, -1);
		for (int i = 0; i < t.size(); ++i)
		{
			if (grove[i] != g)
				continue;
			const int x = i % t.w, y = i / t.w;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int next = t.at(x + dx, y + dy);
					for (int k = 0; k < teams; ++k)
						if (cost[k][next] >= 0 && (reach[k] < 0 || cost[k][next] < reach[k]))
							reach[k] = cost[k][next];
				}
		}
		for (int k = 0; k < teams; ++k)
			if (reach[k] < 0)
				return "A colony cannot reach a prize on the rope.";
		// Every prize is a front: the colony nearest it is barely nearer than the next one along,
		// and both of them count as contenders for it.
		std::vector<int> order(reach);
		std::sort(order.begin(), order.end());
		for (int k = 0; k < teams; ++k)
			if (reach[k] - order[0] <= contestTolerance(t))
				++contenders[k];
		if (teams > 1 && order[1] - order[0] > contestTolerance(t))
			return "A prize on the rope is not contested: colony " +
				   std::to_string(std::find(reach.begin(), reach.end(), order[0]) - reach.begin()) +
				   " reaches it " + std::to_string(order[1] - order[0]) +
				   " steps before anyone else (tolerance " + std::to_string(contestTolerance(t)) +
				   ").";
	}
	// And every colony is a contender for the same number of prizes: the stake each one has in the
	// rope, which is the thing a tug of war has to share out.
	int fewest = contenders[0], most = contenders[0];
	for (int k = 0; k < teams; ++k)
	{
		fewest = std::min(fewest, contenders[k]);
		most = std::max(most, contenders[k]);
	}
	if (most - fewest > kFrontTolerance)
		return "The rope is not shared out: one colony is a contender for " + std::to_string(most) +
			   " of its prizes and another for " + std::to_string(fewest) + " (tolerance " +
			   std::to_string(kFrontTolerance) + ").";
	return "";
}
} // namespace

TugOptions::TugOptions(const GenerationRequest &r)
	: prizes(r.option("prizes")), march(r.option("march")), levelling(r.option("levelling")),
	  lakes(r.option("lakes")), wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition tugDefinition()
{
	return {"tug",
			59,
			"Tug",
			// 2: some seeds now cut a river through the commons, which moves their terrain.
			2,
			false,
			// Levelling is the map's own argument: at 0 the prizes are merely spread out, the way
			// any generator would place them, and at 100 they are spread out and the same walk from
			// everywhere. The map's telemetry reports both figures for every seed.
			{{"prizes", "Prizes on the rope", 2, 12, 1, 6, ControlGroup::Layout},
			 {"march", "March width", 8, 40, 4, 16, ControlGroup::Layout},
			 {"levelling", "Levelling", 0, 100, 10, 100, ControlGroup::Layout},
			 {"lakes", "Lakes", 0, 40, 5, 10, ControlGroup::Terrain},
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			requestFailure,
			validateWorld,
			{"terrain:natural", "feature:lakes", "style:contested-center", "fairness:solved-rope"}};
}
