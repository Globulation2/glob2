// SPDX-License-Identifier: GPL-3.0-or-later
#include "Bases.h"
#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GlobalContainer.h"
#include "Ressource.h"
#include "Resources.h"
#include "Unit.h"
#include "UnitConsts.h"
#include <algorithm>
#include <cstring>
#include <string>
namespace MapGeneration
{
namespace
{
// The frame turned by a facing: Canals' block frame (CanalsGenerator.cpp localTile), so a design
// authored at facing 0 (along = +x, across = +y) lands turned a quarter turn per facing.
void turnOffset(int facing, int u, int v, int &dx, int &dy)
{
	switch (facing & 3)
	{
	case 1:
		dx = -v;
		dy = u;
		break;
	case 2:
		dx = -u;
		dy = -v;
		break;
	case 3:
		dx = v;
		dy = -u;
		break;
	default:
		dx = u;
		dy = v;
		break;
	}
}

// The building type a piece names, finished or as a level-0 site; nullptr when the game has no such
// type (a plan naming "market" at level 2, say), which a design treats as its own bug.
const BuildingType *pieceType(const BasePiece &piece)
{
	return globalContainer->buildingsTypes.getByType(piece.type, piece.finished ? piece.level : 0,
													 !piece.finished);
}

BaseFootprint pieceFootprint(const BaseSite &site, const BasePiece &piece, const BuildingType &type)
{
	return baseFootprint(site, piece.along, piece.across, type.width, type.height);
}

// Visits every tile of a footprint's one-tile ring, the eight-connected border round it: the ground
// a worker walks to reach the building's every side.
template <typename Visit> void forEachRingTile(const Torus &t, const BaseSite &site,
												const BaseFootprint &f, Visit visit)
{
	for (int dy = f.dy - 1; dy <= f.dy + f.h; ++dy)
		for (int dx = f.dx - 1; dx <= f.dx + f.w; ++dx)
		{
			const bool inside = dx >= f.dx && dx < f.dx + f.w && dy >= f.dy && dy < f.dy + f.h;
			if (!inside)
				visit(t.at(site.x + dx, site.y + dy));
		}
}
template <typename Visit> void forEachFootprintTile(const Torus &t, const BaseSite &site,
													 const BaseFootprint &f, Visit visit)
{
	for (int dy = 0; dy < f.h; ++dy)
		for (int dx = 0; dx < f.w; ++dx)
			visit(t.at(site.x + f.dx + dx, site.y + f.dy + dy));
}
template <typename Visit> void forEachDepotTile(const Torus &t, const BaseSite &site,
												 const BaseDepot &depot, Visit visit)
{
	for (int dv = -depot.radius; dv <= depot.radius; ++dv)
		for (int du = -depot.radius; du <= depot.radius; ++du)
			visit(baseTile(t, site, depot.along + du, depot.across + dv));
}

// The City layout, in frame tiles at facing 0 (top-left corners). Everything lies within Chebyshev
// 8 of the swarm's middle and every two footprints keep at least one tile between them, which
// basePlanFits enforces and the toolkit check proves at every facing:
//
//        u: -8 -7 -6 -5 -4 -3 -2 -1  0  1  2  3  4  5  6  7
//   v -6    T  T  .  .  .  .  I  I  .  I  I  .  .  .  T  T      towers, inns L1
//   v -5    T  T  .  .  .  .  I  I  .  I  I  .  .  .  T  T
//   v -2    B  B  B  B  .  .  S  S  S  S  .  R  R  R  R  .      barracks, swarm, racetrack
//   v  1    B  B  B  B  .  .  S  S  S  S  .  R  R  R  R  .
//   v  3    .  .  .  .  .  .  i  i  .  H  H  .  C  C  .  .      inn L0, hospital, school
//   v  5    Q  Q  Q  Q  Q  .  .  .  .  .  .  .  .  .  .  .      quarry depot (radius 2, v 3..7)
//
// The swarm is in the middle so the colonists spill out round it evenly; the food and care stand
// behind and before it, the two big 4x4s to its sides, the towers on the front corners where a
// Glacis compound's wall towers also want to be. Town and Hamlet drop pieces; nothing moves.
constexpr int kSwarmAlong = -2, kSwarmAcross = -2;
// Stock: a starting inn is full (30 wheat at level 2, 10 at level 1) and the swarm holds its 20, so
// the first minutes feed the colony while the fields come in; a tower starts with all its bullets,
// as placeTower stocks them.
constexpr int kFullStock = 100;

void addPiece(BasePlan &plan, const char *type, int level, int along, int across, bool finished,
			  int stock)
{
	plan.pieces.push_back({type, level, along, across, finished, stock});
}

// The plan's reach: the Chebyshev half-extent of every footprint's ring and every depot at facing
// 0, which is the same at every facing since a quarter turn preserves Chebyshev distance.
int planReach(const BasePlan &plan)
{
	int reach = 0;
	for (const BasePiece &piece : plan.pieces)
	{
		const BuildingType *type = pieceType(piece);
		if (!type)
			continue;
		reach = std::max({reach, std::abs(piece.along - 1), std::abs(piece.across - 1),
						  std::abs(piece.along + type->width), std::abs(piece.across + type->height)});
	}
	for (const BaseDepot &depot : plan.depots)
		reach = std::max({reach, std::abs(depot.along) + depot.radius,
						  std::abs(depot.across) + depot.radius});
	return reach;
}
} // namespace

BaseTier baseTier(int colonists)
{
	// Boundaries at 24 and 32 colonists: a Hamlet's one inn (7 inside at level 2) and swarm feed
	// about 20 units between meals, a Town's two some 28, a City's three all 48.
	if (colonists < 24)
		return BaseTier::Hamlet;
	if (colonists < 32)
		return BaseTier::Town;
	return BaseTier::City;
}

BasePlan standardBasePlan(BaseTier tier, BaseKind kind, int towers, bool quarry)
{
	BasePlan plan;
	const bool finished = kind == BaseKind::Finished;
	// The swarm is always finished and always stocked; a Sites base also keeps one finished inn,
	// or its colonists would starve before the first site was done.
	addPiece(plan, "swarm", 0, kSwarmAlong, kSwarmAcross, true, kFullStock);
	plan.swarm = 0;
	if (finished)
		addPiece(plan, "inn", 1, -2, -6, true, kFullStock);
	else
		addPiece(plan, "inn", 0, -2, -6, true, kFullStock);
	addPiece(plan, "hospital", 0, 1, 3, finished, 0);
	addPiece(plan, "barracks", 0, -8, -2, finished, 0);
	if (tier != BaseTier::Hamlet)
	{
		addPiece(plan, "inn", 0, -2, 3, finished, finished ? kFullStock : 0);
		addPiece(plan, "school", 0, 4, 3, finished, 0);
	}
	if (tier == BaseTier::City)
	{
		addPiece(plan, "inn", finished ? 1 : 0, 1, -6, finished, finished ? kFullStock : 0);
		addPiece(plan, "racetrack", 0, 3, -2, finished, 0);
	}
	// Towers: a Finished plan gets them complete and stocked when asked; a Sites plan of Town size
	// or better stakes them out as sites, since a city under construction plans its defences too.
	if (finished ? towers > 0 : tier != BaseTier::Hamlet)
	{
		addPiece(plan, "defencetower", 0, -8, -6, finished, finished ? kFullStock : 0);
		if (finished ? towers > 1 : tier == BaseTier::City)
			addPiece(plan, "defencetower", 0, 6, -6, finished, finished ? kFullStock : 0);
	}
	// Depots. A quarry behind the barracks, radius 2 (about thirteen tiles of stone: enough for
	// every upgrade a compound will ever build, since stone never runs out), one tile clear of the
	// barracks' walking ring. A Sites base gets a stack of wood by each of its four corners instead
	// - radius 1, five tiles - just outside the corner towers' and the back row's rings, so the
	// sites' wood is at hand without a clump standing in any building's walking ring.
	if (finished && quarry)
		plan.depots.push_back({STONE, -8, 5, 2});
	if (!finished)
		for (const int u : {-9, 8})
			for (const int v : {-9, 7})
				plan.depots.push_back({WOOD, u, v, 1});
	plan.reach = planReach(plan);
	return plan;
}

BaseGarrison baseGarrison(int colonists, bool garrison)
{
	BaseGarrison g;
	g.workers = colonists;
	if (garrison)
	{
		// Three eighths warriors and an eighth explorers: 12 and 4 at the default 32 colonists, 6
		// and 2 at 16, 18 and 6 at 48. Level-1 warriors (the barracks' first training) so the
		// garrison can hold a gate against level-0 raiders but cannot storm a stocked tower.
		g.warriors = colonists * 3 / 8;
		g.warriorLevel = 1;
		g.explorers = std::max(1, colonists / 8);
		g.explorerLevel = 0;
	}
	return g;
}

int baseTile(const Torus &t, const BaseSite &site, int u, int v)
{
	int dx, dy;
	turnOffset(site.facing, u, v, dx, dy);
	return t.at(site.x + dx, site.y + dy);
}

BaseFootprint baseFootprint(const BaseSite &site, int along, int across, int w, int h)
{
	// The image of a rectangle is a rectangle: its top-left is the least turned corner on each axis
	// (Symmetry::anchor's rule), and its sides swap at odd facings.
	int ax, ay, bx, by;
	turnOffset(site.facing, along, across, ax, ay);
	turnOffset(site.facing, along + w - 1, across + h - 1, bx, by);
	const bool odd = site.facing & 1;
	return {std::min(ax, bx), std::min(ay, by), odd ? h : w, odd ? w : h};
}

std::string basePlanMisfit(const Torus &t, const BasePlan &plan, const BaseSite &site,
						   const std::vector<unsigned char> &buildable,
						   const std::vector<unsigned char> &open)
{
	std::vector<unsigned char> occupied(t.size(), 0);
	std::vector<BaseFootprint> footprints;
	const auto where = [&](int i)
	{ return " at (" + std::to_string(i % t.w) + ", " + std::to_string(i / t.w) + ")"; };
	for (const BasePiece &piece : plan.pieces)
	{
		const BuildingType *type = pieceType(piece);
		if (!type)
			return std::string("no building type ") + piece.type;
		footprints.push_back(pieceFootprint(site, piece, *type));
		std::string misfit;
		forEachFootprintTile(t, site, footprints.back(),
							 [&](int i)
							 {
								 if (misfit.empty() && !buildable[i])
									 misfit = std::string(piece.type) + " off buildable ground" + where(i);
								 else if (misfit.empty() && occupied[i])
									 misfit = std::string(piece.type) + " on another footprint" + where(i);
								 occupied[i] = 1;
							 });
		if (!misfit.empty())
			return misfit;
	}
	for (size_t p = 0; p < footprints.size(); ++p)
	{
		std::string misfit;
		forEachRingTile(t, site, footprints[p],
						[&](int i)
						{
							if (misfit.empty() && (!open[i] || occupied[i]))
								misfit = std::string(plan.pieces[p].type) + "'s ring closed" + where(i);
						});
		if (!misfit.empty())
			return misfit;
	}
	for (const BaseDepot &depot : plan.depots)
	{
		std::string misfit;
		forEachDepotTile(t, site, depot,
						 [&](int i)
						 {
							 if (misfit.empty() && (!open[i] || occupied[i]))
								 misfit = "a depot with no room" + where(i);
						 });
		if (!misfit.empty())
			return misfit;
	}
	return "";
}

bool basePlanFits(const Torus &t, const BasePlan &plan, const BaseSite &site,
				  const std::vector<unsigned char> &buildable,
				  const std::vector<unsigned char> &open)
{
	return basePlanMisfit(t, plan, site, buildable, open).empty();
}

std::vector<unsigned char> baseFootprints(const Torus &t, const BasePlan &plan,
										  const std::vector<BaseSite> &sites)
{
	std::vector<unsigned char> mask(t.size(), 0);
	for (const BaseSite &site : sites)
		for (const BasePiece &piece : plan.pieces)
			if (const BuildingType *type = pieceType(piece))
				forEachFootprintTile(t, site, pieceFootprint(site, piece, *type),
									 [&](int i) { mask[i] = 1; });
	return mask;
}

std::vector<unsigned char> baseSurroundings(const Torus &t, const BasePlan &plan,
											const std::vector<BaseSite> &sites)
{
	std::vector<unsigned char> mask(t.size(), 0);
	for (const BaseSite &site : sites)
	{
		for (const BasePiece &piece : plan.pieces)
			if (const BuildingType *type = pieceType(piece))
			{
				const BaseFootprint f = pieceFootprint(site, piece, *type);
				forEachFootprintTile(t, site, f, [&](int i) { mask[i] = 1; });
				forEachRingTile(t, site, f, [&](int i) { mask[i] = 1; });
			}
		for (const BaseDepot &depot : plan.depots)
			forEachDepotTile(t, site, depot, [&](int i) { mask[i] = 1; });
	}
	return mask;
}

int garrison(Game &game, GenerationContext &context, int team, int swarmGid,
			 const BaseGarrison &units, const std::vector<int> *withinLabel, const char *stream)
{
	Map &map = game.map;
	const Torus t(map);
	const Building *swarm = game.teams[team]->myBuildings[Building::GIDtoID(Uint16(swarmGid))];
	if (!swarm)
		return -1;
	const int x0 = swarm->posX, y0 = swarm->posY, w = swarm->type->width, h = swarm->type->height;
	const auto allowed = [&](int i) { return !withinLabel || (*withinLabel)[i] == team; };
	// The tiles at exactly `ring` Chebyshev steps from the footprint, in row order.
	const auto ringTiles = [&](int ring)
	{
		std::vector<int> tiles;
		for (int dy = -ring; dy < h + ring; ++dy)
			for (int dx = -ring; dx < w + ring; ++dx)
				if (dx == -ring || dy == -ring || dx == w + ring - 1 || dy == h + ring - 1)
					tiles.push_back(t.at(x0 + dx, y0 + dy));
		return tiles;
	};
	int outermost = 0;
	// Ground units, ring by ring: workers first so they stand nearest the swarm and its stock, then
	// warriors. Within a ring the free tiles are dealt at random from the stream, so the same seed
	// seats the same colony and a ring only half used does not always fill from its top-left.
	const auto seatGround = [&](int count, int type, int level)
	{
		for (int ring = 1; count > 0 && ring <= kGarrisonReach; ++ring)
		{
			std::vector<int> free;
			for (int i : ringTiles(ring))
				if (allowed(i) &&
					map.isFreeForGroundUnit(i % t.w, i / t.w, false, Team::teamNumberToMask(team)))
					free.push_back(i);
			context.shuffle(free.begin(), free.end(), stream);
			for (size_t k = 0; k < free.size() && count > 0; ++k)
			{
				if (!game.addUnit(free[k] % t.w, free[k] / t.w, team, type, level, 0, 0, 0))
					return false;
				--count;
				outermost = std::max(outermost, ring);
			}
		}
		return count == 0;
	};
	if (!seatGround(units.workers, WORKER, 0))
		return -1;
	if (!seatGround(units.warriors, WARRIOR, units.warriorLevel))
		return -1;
	// Explorers fly: they hover over the swarm itself first (ring 0, the footprint), then outward.
	int explorers = units.explorers;
	for (int ring = 0; explorers > 0 && ring <= kGarrisonReach; ++ring)
	{
		std::vector<int> candidates;
		if (ring == 0)
			for (int dy = 0; dy < h; ++dy)
				for (int dx = 0; dx < w; ++dx)
					candidates.push_back(t.at(x0 + dx, y0 + dy));
		else
			candidates = ringTiles(ring);
		std::vector<int> free;
		for (int i : candidates)
			if (allowed(i) && map.isFreeForAirUnit(i % t.w, i / t.w))
				free.push_back(i);
		context.shuffle(free.begin(), free.end(), stream);
		for (size_t k = 0; k < free.size() && explorers > 0; ++k)
		{
			if (!game.addUnit(free[k] % t.w, free[k] / t.w, team, EXPLORER, units.explorerLevel, 0,
							  0, 0))
				return -1;
			--explorers;
			outermost = std::max(outermost, ring);
		}
	}
	return explorers == 0 ? outermost : -1;
}

bool raiseBase(Game &game, GenerationContext &context, int team, const BasePlan &plan,
			   const BaseSite &site, const BaseGarrison &units,
			   const std::vector<int> *withinLabel, const char *stream)
{
	context.stage = "base";
	const auto fail = [&](const std::string &detail)
	{
		context.detail = "Colony " + std::to_string(team) + ": " + detail;
		context.telemetry.fallback("bases.failure", detail, team);
		context.telemetry.choice("bases.outcome", "failed", team);
		return false;
	};
	if (team < 0 || team >= game.teamsCount() || !game.teams[team])
		return fail("no such colony");
	Map &map = game.map;
	const Torus t(map);
	Building *swarm = nullptr;
	int wheat = 0, sites = 0;
	for (size_t p = 0; p < plan.pieces.size(); ++p)
	{
		const BasePiece &piece = plan.pieces[p];
		const BuildingType *type = pieceType(piece);
		if (!type)
			return fail(std::string("no building type ") + piece.type);
		const int typeNum = globalContainer->buildingsTypes.getTypeNum(
			piece.type, piece.finished ? piece.level : 0, !piece.finished);
		const BaseFootprint f = pieceFootprint(site, piece, *type);
		const int x = t.x(site.x + f.dx), y = t.y(site.y + f.dy);
		if (!game.checkRoomForBuilding(x, y, type, team, false))
			return fail(std::string("no room for its ") + piece.type);
		// The editor's own choice of workers: one where the type takes any, none where it takes
		// none. Not persisted through the lobby's snapshot, so nothing depends on it.
		Building *building =
			game.addBuilding(x, y, typeNum, team, type->maxUnitWorking ? 1 : 0, 0);
		if (!building)
			return fail(std::string("could not raise its ") + piece.type);
		if (piece.stockPercent > 0)
		{
			// Stock is a share of the type's own cap, so a plan never has to know the numbers:
			// wheat for whatever eats it (swarm, inn), bullets for a tower.
			if (type->maxResource[WHEAT] > 0)
			{
				building->resources[WHEAT] =
					std::min(type->maxResource[WHEAT], type->maxResource[WHEAT] * piece.stockPercent / 100);
				wheat += building->resources[WHEAT];
			}
			if (type->maxBullets > 0)
				building->bullets = std::min(type->maxBullets, type->maxBullets * piece.stockPercent / 100);
		}
		if (!piece.finished)
			++sites;
		if (int(p) == plan.swarm)
			swarm = building;
	}
	if (!swarm)
		return fail("its plan has no swarm");
	const int rings = garrison(game, context, team, swarm->gid, units, withinLabel, stream);
	if (rings < 0)
		return fail("not every colonist found a tile to stand on");
	// The colony's lists, once, now that every building stands and every unit is placed: this is
	// what registers the inns to feed and the sites to be built (Building::update).
	game.teams[team]->createLists();
	game.teams[team]->startPosX = swarm->posX;
	game.teams[team]->startPosY = swarm->posY;
	game.teams[team]->startPosSet = Team::START_POS_FROM_SWARM;
	context.bootX[team] = swarm->posX;
	context.bootY[team] = swarm->posY;
	context.telemetry.measure("bases.pieces", int(plan.pieces.size()), team);
	context.telemetry.measure("bases.sites", sites, team);
	context.telemetry.measure("bases.stock.wheat", wheat, team);
	context.telemetry.measure("bases.workers.rings", rings, team);
	context.telemetry.measure("bases.garrison.warriors", units.warriors, team);
	context.telemetry.measure("bases.garrison.explorers", units.explorers, team);
	context.telemetry.choice("bases.outcome", "placed", team);
	return true;
}

bool raiseBases(Game &game, GenerationContext &context, const BasePlan &plan,
				const std::vector<BaseSite> &sites, const BaseGarrison &units,
				const std::vector<int> *withinLabel, const char *stream)
{
	context.telemetry.choice("bases.tier",
							 plan.pieces.size() <= 4 ? "hamlet" : plan.pieces.size() <= 6 ? "town" : "city");
	for (size_t k = 0; k < sites.size() && int(k) < context.request.nbTeams; ++k)
		if (!raiseBase(game, context, int(k), plan, sites[k], units, withinLabel, stream))
			return false;
	return true;
}

void plantBaseDepots(Map &map, GenerationContext &context, const Torus &t, const BasePlan &plan,
					 const BaseSite &site)
{
	for (const BaseDepot &depot : plan.depots)
	{
		const int i = baseTile(t, site, depot.along, depot.across);
		placeResourceClump(map, context, MapGeneratorPoint(i % t.w, i / t.w), depot.resource,
						   depot.radius);
	}
}

std::string validateBase(const Game &game, const Torus &t, int team, const BasePlan &plan,
						 const BaseSite &site, int workers)
{
	const std::string colony = "Colony " + std::to_string(team);
	if (team < 0 || team >= game.teamsCount() || !game.teams[team])
		return colony + " is missing.";
	const Team &owner = *game.teams[team];
	for (const BasePiece &piece : plan.pieces)
	{
		const BuildingType *type = pieceType(piece);
		if (!type)
			return colony + "'s plan names an unknown building.";
		const BaseFootprint f = pieceFootprint(site, piece, *type);
		const int x = t.x(site.x + f.dx), y = t.y(site.y + f.dy);
		const Uint16 gid = game.map.getBuilding(x, y);
		const Building *building =
			gid == NOGBID || Building::GIDtoTeam(gid) != team ? nullptr
															 : owner.myBuildings[Building::GIDtoID(gid)];
		if (!building || building->posX != x || building->posY != y)
			return colony + "'s " + piece.type + " is missing.";
		if (building->type->type != piece.type || building->type->level != (piece.finished ? piece.level : 0))
			return colony + "'s " + piece.type + " is not the building planned.";
		if ((building->type->isBuildingSite != 0) == piece.finished)
			return colony + "'s " + piece.type + (piece.finished ? " is not finished." : " is not a site.");
	}
	int counted = 0;
	for (int slot = 0; slot < Unit::MAX_COUNT; ++slot)
		if (const Unit *unit = owner.myUnits[slot]; unit && unit->typeNum == WORKER)
			++counted;
	if (counted != workers)
		return colony + " has " + std::to_string(counted) + " workers, not " + std::to_string(workers) + ".";
	return "";
}
} // namespace MapGeneration
