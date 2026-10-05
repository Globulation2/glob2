// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuildingGradientCapture.h"
#include "ForbiddenDirectionDecision.h"
#include "GradientRuntime.h"
#include "BuildingGradientSearch.h"
#include "Unit.h"
#include "Utilities.h"
using building_gradient::refreshDescription;
using building_gradient::refreshDestination;

Map::BuildingRefreshStatus Map::buildingRefreshStatus() const
{
	const auto &s = gradientRuntime->buildings;
	const auto &m = s.metrics;
	return {m.requests,   m.coalesced,           m.jobs,          m.published,
			m.discarded,  m.synchronousFallback, m.snapshotNs,    m.snapshotCpuNs,
			m.fallbackNs, m.fallbackCpuNs,       m.walkingFields, m.tripFields,
			m.buildNs,    m.buildCpuNs,          m.waitNs,        m.maxBytes,
			m.maxPending, s.pending.size(),      s.bytes(),       s.requests.size()};
}
void Map::configureBuildingGradientImpact(const std::string &prefix)
{
	gradientRuntime->impact = std::make_unique<BuildingGradientImpact>(prefix);
	gradientRuntime->impact->identity = [this](int gid, bool building) -> std::uint32_t
	{
		if (gid < 0)
			return 0;
		if (building)
		{
			auto *b = refreshDestination(game, gid);
			return b ? b->scriptIdentity : 0;
		}
		if (gid >= Unit::MAX_COUNT * Team::MAX_COUNT)
			return 0;
		const auto team = Unit::GIDtoTeam(gid);
		auto *u =
			team < game->teamsCount() ? game->teams[team]->myUnits[Unit::GIDtoID(gid)] : nullptr;
		return u ? u->scriptIdentity : 0;
	};
}

bool Map::buildingGradientImpactEnabled() const
{
	return bool(gradientRuntime->impact);
}
void Map::configureGradientCounterfactual(std::uint64_t tick, std::uint64_t event)
{
	if (!gradientRuntime->impact || !event)
		throw std::invalid_argument(
			"counterfactual requires impact telemetry and a positive event index");
	gradientRuntime->impact->counterfactualTick = tick;
	gradientRuntime->impact->counterfactualEvent = event;
}
void Map::beginGradientDecision(const char *kind, int gid, int uid)
{
	if (auto &impact = gradientRuntime->impact)
	{
		impact->begin(game->stepCounter, kind, gid, uid);
		auto rng = syncRandEngine();
		impact->tieRoll = rng();
		if (game->stepCounter == impact->counterfactualTick &&
			impact->index == impact->counterfactualEvent)
		{
			auto *b = refreshDestination(game, gid);
			if (!b)
				throw std::runtime_error("counterfactual destination disappeared");
			std::vector<Building *> targets{b};
			if (std::string(kind) == "resource")
				for (auto *market : b->owner->canExchange)
					if (market != b)
						targets.push_back(market);
			for (auto *target : targets)
				for (int sw = 0; sw < SWIM_CLASS_COUNT; ++sw)
					if (target->globalGradient[sw])
					{
						freshBuildingDecisionField(target, sw, -1);
						const auto &fresh = impact->oracle.at({target->gid, sw});
						recycleBuildingGradientSearch(std::move(target->globalGradientSearch[sw]));
						std::copy(fresh.walking.begin(), fresh.walking.end(),
								  target->globalGradient[sw]);
						target->locked[buildingAccessIndex(sw)] = fresh.locked;
						if (target->type->isVirtual && target->type->zonable[WORKER])
							target->anyResourceToClear[buildingAccessIndex(sw)] =
								fresh.resourceState;
						for (int r = 0; r < MAX_NB_RESOURCES; ++r)
							if (!fresh.trips[r].empty())
								std::copy(fresh.trips[r].begin(), fresh.trips[r].end(),
										  target->roundTripGradient[r][sw]);
					}
			impact->counterfactualApplied = true;
			impact->outcome(game->stepCounter, "counterfactual", gid, uid, -1, 0, 0, 0, false);
		}
	}
}
const Uint16 *Map::freshBuildingDecisionField(Building *b, int sw, int resource)
{
	auto &impact = *gradientRuntime->impact;
	const auto key = std::make_pair(int(b->gid), sw);
	if (!impact.oracle.count(key))
	{
		if (!impact.terrain)
		{
			impact.terrain = std::make_shared<building_gradient::Terrain>();
			auto &terrain = *impact.terrain;
			terrain.width = getW();
			terrain.height = getH();
			terrain.generation = topologyGeneration;
			terrain.modifiedCosts = hasTerrainMovementModifiers();
			terrain.cells.resize(size);
			for (std::size_t i = 0; i < size; ++i)
				terrain.cells[i] = {tiles[i].forbidden,
									tiles[i].building,
									tiles[i].resource.type,
									immobileUnits[i],
									Uint8(tiles[i].building == NOGBID
											  ? 0
											  : Building::GIDtoTeam(tiles[i].building)), terrainTypeAt(i)};
		}
		std::array<std::vector<Uint16>, MAX_NB_RESOURCES> parents;
		for (int r = 0; r < MAX_NB_RESOURCES; ++r)
			if (b->roundTripGradient[r][sw] && resourcesGradient[b->owner->teamNumber][r][sw])
			{
				const auto *p = resourcesGradient[b->owner->teamNumber][r][sw];
				parents[r].assign(p, p + size);
			}
		impact.oracle[key] = building_gradient::build(
			*impact.terrain, refreshDescription(*b, sw, 0), parents, impact.scratch);
	}
	const auto &result = impact.oracle.at(key);
	if (resource < 0)
		return result.locked ? nullptr : result.walking.data();
	return result.trips[resource].empty() ? nullptr : result.trips[resource].data();
}
int Map::freshBuildingClearingState(Building *b, int sw)
{
	freshBuildingDecisionField(b, sw, -1);
	return gradientRuntime->impact->oracle.at({b->gid, sw}).resourceState;
}
bool Map::buildingDecisionDistance(Building *b, int sw, int resource, int x, int y, int *distance,
								   bool fresh, bool publishedOnly)
{
	if (!fresh && !publishedOnly)
		return resource < 0 ? buildingAvailable(b, sw, x, y, distance)
							: roundTripDistance(b, resource, sw, x, y, distance);
	const auto *field =
		fresh ? freshBuildingDecisionField(b, sw, resource)
			  : (resource < 0 ? b->globalGradient[sw] : b->roundTripGradient[resource][sw]);
	if (!field || (resource < 0 && !fresh && b->locked[buildingAccessIndex(sw)]))
		return false;
	std::vector<Uint16> privatePublished;
	if (!fresh && resource < 0 && b->globalGradientSearch[sw] &&
		!b->globalGradientSearch[sw]->complete())
	{
		if (gradientRuntime->impact)
		{
			auto &snapshot = gradientRuntime->impact->publishedWalking[{b->gid, sw}];
			if (snapshot.empty())
				snapshot = b->globalGradientSearch[sw]->completePrivateSnapshot();
			field = snapshot.data();
		}
		else
		{
			privatePublished = b->globalGradientSearch[sw]->completePrivateSnapshot();
			field = privatePublished.data();
		}
	}
	if (resource >= 0)
	{
		const auto *parent = resourcesGradient[b->owner->teamNumber][resource][sw];
		if (!parent || parent[coordToIndex(x, y)] <= GRADIENT_UNREACHABLE)
			return false;
	}
	auto value = field[coordToIndex(x, y)];
	if (resource < 0)
		for (int d = 0; d < 8 && value <= GRADIENT_UNREACHABLE; ++d)
		{
			const auto cell = coordToIndex(x + tabClose[d][0], y + tabClose[d][1]);
			value = field[cell];
		}
	if (value <= GRADIENT_UNREACHABLE)
		return false;
	*distance = gradientTiles(value);
	return true;
}
void Map::recordGradientDecision(Building *b, int sw, int live, int fresh, int liveResource,
								 int freshResource, int liveScore, int freshScore, int harm)
{
	if (auto &impact = gradientRuntime->impact)
	{
		impact->record(live, fresh, liveResource, freshResource, liveScore, freshScore, harm,
					   b ? game->stepCounter - b->lastGlobalGradientUpdateStepCounter[sw] : 0,
					   b && gradientRuntime->buildings.pending.count({b->gid, sw}));
		if (impact->kind == "hiring" && live < 0 && fresh >= 0 && b)
		{
			auto *unit = game->teams[Unit::GIDtoTeam(fresh)]->myUnits[Unit::GIDtoID(fresh)];
			auto [episode, inserted] = impact->missedHires.try_emplace(
				{b->gid, fresh, true, unit->scriptIdentity, b->scriptIdentity, freshResource},
				BuildingGradientImpact::MissedHire{unit->scriptIdentity, b->scriptIdentity,
												   game->stepCounter, freshResource});
			episode->second.selected = true;
			episode->second.resource = freshResource;
		}
	}
}
void Map::gradientOutcome(const char *kind, Unit *unit, Building *b, int resource, unsigned elapsed,
						  unsigned distance, unsigned reversals, bool censored)
{
	if (auto &impact = gradientRuntime->impact)
	{
		const bool acquisition = std::string(kind) == "harvested" ||
			std::string(kind) == "market_acquired";
		const auto journeyIt = unit ? impact->journeys.find(unit->gid) : impact->journeys.end();
		const bool sameJourney = journeyIt != impact->journeys.end() &&
			journeyIt->second.identity == unit->scriptIdentity && b &&
			journeyIt->second.buildingIdentity == b->scriptIdentity;
		if (acquisition && sameJourney)
		{
			auto &journey = journeyIt->second;
			elapsed = game->stepCounter - journey.started;
			distance = journey.distance;
			reversals = journey.reversals;
			journey.carried = resource;
		}
		if (std::string(kind) == "delivered")
			++impact->deliveries;
		if (std::string(kind) == "delivered" && sameJourney)
		{
			auto journey = journeyIt->second;
			int dx = unit->posX - journey.x, dy = unit->posY - journey.y;
			if (dx > getW() / 2)
				dx -= getW();
			if (dx < -getW() / 2)
				dx += getW();
			if (dy > getH() / 2)
				dy -= getH();
			if (dy < -getH() / 2)
				dy += getH();
			journey.distance += std::max(std::abs(dx), std::abs(dy));
			if (dx * journey.dx + dy * journey.dy < 0)
				++journey.reversals;
			elapsed = game->stepCounter - journey.started;
			distance = journey.distance;
			reversals = journey.reversals;
			impact->journeys.erase(unit->gid);
		}
		const auto *source = unit && std::string(kind) == "market_acquired" ? unit->ownExchangeBuilding : nullptr;
		impact->outcome(game->stepCounter, kind, b ? b->gid : -1, unit ? unit->gid : -1, resource,
						elapsed, distance, reversals, censored, unit ? unit->posX : -1,
						unit ? unit->posY : -1, 0, 0, "", source ? source->gid : -1,
						source ? source->scriptIdentity : 0);
	}
}

void Map::auditBuildingMovement(Unit *unit, Building *b, bool moved, int resource)
{
	if (!gradientRuntime->impact || !b)
		return;
	const int sw = unit->swimClass(), x = unit->posX, y = unit->posY;
	const auto here = coordToIndex(x, y);
	const auto *parent = resource >= 0
		? resourcesGradient[b->owner->teamNumber][resource][sw] : nullptr;
	const bool resourceStop = resource >= 0 &&
		(!parent || parent[here] == GRADIENT_UNREACHABLE || parent[here] == GRADIENT_AT_GOAL);
	const bool escape = resource >= 0 ? parent && parent[here] == GRADIENT_FORBIDDEN
		: (tiles[here].forbidden & unit->owner->me) != 0;
	const Uint16 *field = nullptr;
	GradientDirectionDecision choice;
	if (escape)
	{
		const auto *option = resource >= 0 ? parent :
			(b->globalGradient[sw] ? freshBuildingDecisionField(b, sw, -1) : nullptr);
		field = forbiddenGradient[unit->owner->teamNumber][sw];
		choice.best = evaluateForbiddenDirection(*this, field, option, sw, x, y);
	}
	else if (!resourceStop)
	{
		field = freshBuildingDecisionField(b, sw, resource);
		if (resource >= 0 && (!field || field[here] <= GRADIENT_UNREACHABLE))
			field = parent;
		if (field)
			choice = evaluateGradientDirection(unit->owner->me, sw, x, y, field, resource >= 0);
		if (resource >= 0 && !choice.available())
		{
			field = parent;
			choice = evaluateGradientDirection(unit->owner->me, sw, x, y, field, false);
		}
	}
	int fresh = choice.best;
	if (fresh < 0 && choice.count)
		fresh = choice.sidesteps[gradientRuntime->impact->tieRoll % choice.count];
	if (choice.atGoal)
		fresh = 8;
	int live = -1;
	if (moved)
	{
		live = 8;
		for (int d = 0; d < 8; ++d)
			if (unit->dx == tabClose[d][0] && unit->dy == tabClose[d][1])
				live = d;
	}
	int harm = 0, liveCost = -1, freshCost = -1;
	if (field && live >= 0 && fresh >= 0)
	{
		if (fresh == 8)
			freshCost = 0;
		else
		{
			const auto bestCell = coordToIndex(x + tabClose[fresh][0], y + tabClose[fresh][1]);
			freshCost = GRADIENT_AT_GOAL - field[bestCell] +
						stepCost(tabClose[fresh][0], tabClose[fresh][1], bestCell, sw);
		}
		const auto liveCell = coordToIndex(x + unit->dx, y + unit->dy);
		if (field[liveCell] > GRADIENT_UNREACHABLE)
			liveCost = GRADIENT_AT_GOAL - field[liveCell] +
					   (live == 8 ? 0 : stepCost(unit->dx, unit->dy, liveCell, sw));
		else
			liveCost = GRADIENT_AT_GOAL;
		harm = std::max(0, liveCost - freshCost);
	}
	recordGradientDecision(b, sw, live, fresh, resource, resource, liveCost, freshCost, harm);
}

bool Map::resourceDecisionDistance(int team, int resource, int sw, int x, int y, int *distance,
								   bool fresh)
{
	if (!fresh)
		return resourceAvailable(team, resource, sw, x, y, distance);
	const Uint16 *field = resourcesGradient[team][resource][sw];
	if (!field)
	{
		auto &impact = *gradientRuntime->impact;
		const auto key = std::make_tuple(team, resource, sw);
		auto &values = impact.resourceOracle[key];
		if (values.empty())
		{
			values.resize(size);
			seedResourcesGradient(team, resource, sw, values.data());
			const auto terrain = frozenTerrainSnapshot();
			gradient_kernel::propagateTerrainField(values.data(), sw, GRADIENT_COST_LIMIT,
											{getW(), getH()}, impact.scratch,
											[&](std::size_t i) { return (*terrain)[i]; }, hasTerrainMovementModifiers());
		}
		field = values.data();
	}
	const auto value = field[coordToIndex(x, y)];
	if (value <= GRADIENT_UNREACHABLE)
		return false;
	*distance = gradientTiles(value);
	return true;
}

void Map::auditResourceDestination(Unit *unit, int liveResource, Building *liveMarket,
								   int freshResource, Building *freshMarket)
{
	if (!gradientRuntime->impact || !unit->attachedBuilding)
		return;
	auto *b = unit->attachedBuilding;
	const int sw = unit->swimClass();
	auto destination = [&](int resource, Building *market, bool fresh)
	{
		if (resource < 0)
			return -1;
		if (market)
			return int(coordToIndex(market->getMidX(), market->getMidY()));
		const Uint16 *field = fresh ? freshBuildingDecisionField(b, sw, resource)
									: b->roundTripGradient[resource][sw];
		if (!field || field[coordToIndex(unit->posX, unit->posY)] <= GRADIENT_UNREACHABLE)
			field = resourcesGradient[b->owner->teamNumber][resource][sw];
		if (!field)
			return -1;
		Sint32 x = unit->posX, y = unit->posY;
		getGlobalGradientDestination(field, x, y, &x, &y);
		return int(coordToIndex(x, y));
	};
	gradientRuntime->impact->kind = "resource_site";
	recordGradientDecision(b, sw, destination(liveResource, liveMarket, false),
						   destination(freshResource, freshMarket, true), liveResource,
						   freshResource, 0, 0);
}

void Map::observeGradientImpact()
{
	auto &impact = *gradientRuntime->impact;
	std::set<int> present;
	unsigned units = 0, buildings = 0, slots = 0, hungry = 0, completed = 0;
	std::uint64_t dead = 0;
	for (int t = 0; t < game->teamsCount(); ++t)
	{
		auto *team = game->teams[t];
		for (const auto &row : team->stats.measurements.deaths)
			for (auto count : row)
				dead += count;
		for (int n = 0; n < Building::MAX_COUNT; ++n)
			if (auto *b = team->myBuildings[n])
			{
				++buildings;
				slots += std::max(0, b->desiredMaxUnitWorking - int(b->unitsWorking.size()));
				auto old = impact.buildings.find(b->gid);
				if (old != impact.buildings.end() && old->second.first == b->scriptIdentity &&
					old->second.second && !b->type->isBuildingSite)
				{
					++completed;
					impact.outcome(game->stepCounter, "construction_complete", b->gid, -1, -1, 0, 0,
								   0, false, b->posX, b->posY);
				}
				impact.buildings[b->gid] = {b->scriptIdentity, b->type->isBuildingSite};
			}
		for (int n = 0; n < Unit::MAX_COUNT; ++n)
			if (auto *u = team->myUnits[n])
			{
				++units;
				if (u->hungry <= u->trigHungry)
					++hungry;
				present.insert(u->gid);
				auto found = impact.journeys.find(u->gid);
				if (found != impact.journeys.end())
				{
					auto &j = found->second;
					if (j.identity != u->scriptIdentity || u->isDead || !u->attachedBuilding ||
						j.building != u->attachedBuilding->gid ||
						j.buildingIdentity != u->attachedBuilding->scriptIdentity)
					{
						impact.outcome(game->stepCounter, "abandoned", j.building, u->gid,
									   j.resource, game->stepCounter - j.started, j.distance,
									   j.reversals, true, u->posX, u->posY, j.identity,
									   j.buildingIdentity);
						impact.journeys.erase(found);
						found = impact.journeys.end();
					}
					else
					{
						int dx = u->posX - j.x, dy = u->posY - j.y;
						if (dx > getW() / 2)
							dx -= getW();
						if (dx < -getW() / 2)
							dx += getW();
						if (dy > getH() / 2)
							dy -= getH();
						if (dy < -getH() / 2)
							dy += getH();
						if (dx || dy)
						{
							j.distance += std::max(std::abs(dx), std::abs(dy));
							if (dx * j.dx + dy * j.dy < 0)
								++j.reversals;
							j.dx = dx;
							j.dy = dy;
						}
						j.x = u->posX;
						j.y = u->posY;
						j.carried = u->carriedResource;
					}
				}
				if (found == impact.journeys.end() && !u->isDead && u->attachedBuilding &&
					u->activity == Unit::ACT_FILLING &&
					(u->displacement == Unit::DIS_GOING_TO_RESOURCE ||
					 u->displacement == Unit::DIS_GOING_TO_BUILDING ||
					 u->displacement == Unit::DIS_HARVESTING ||
					 u->displacement == Unit::DIS_FILLING_BUILDING))
				{
					impact.journeys[u->gid] = {u->scriptIdentity,
											   u->attachedBuilding->scriptIdentity,
											   game->stepCounter,
											   u->attachedBuilding->gid,
											   u->destinationPurpose,
											   u->posX,
											   u->posY,
											   0,
											   0,
											   u->carriedResource,
											   0,
											   0};
				}
			}
	}
	for (auto it = impact.journeys.begin(); it != impact.journeys.end();)
	{
		if (!present.count(it->first))
		{
			auto &j = it->second;
			impact.outcome(game->stepCounter, "removed", j.building, it->first, j.resource,
						   game->stepCounter - j.started, j.distance, j.reversals, true, j.x, j.y,
						   j.identity, j.buildingIdentity);
			it = impact.journeys.erase(it);
		}
		else
			++it;
	}
	for (auto it = impact.missedHires.begin(); it != impact.missedHires.end();)
	{
		const auto &key = it->first;
		const auto &m = it->second;
		auto *b = refreshDestination(game, std::get<0>(key));
		const auto team = Unit::GIDtoTeam(std::get<1>(key));
		auto *u = team < game->teamsCount()
					  ? game->teams[team]->myUnits[Unit::GIDtoID(std::get<1>(key))]
					  : nullptr;
		const bool hired = u && b && u->scriptIdentity == m.unitIdentity &&
						   b->scriptIdentity == m.buildingIdentity && u->attachedBuilding == b &&
						   (u->activity == Unit::ACT_FILLING || u->activity == Unit::ACT_FLAG);
		const char *censorReason = "";
		if (!b || b->scriptIdentity != m.buildingIdentity)
			censorReason = "destination_removed";
		else if (!u || u->scriptIdentity != m.unitIdentity || u->isDead ||
				 u->activity != Unit::ACT_RANDOM || u->medical != Unit::MED_FREE)
			censorReason = "candidate_unavailable";
		else if (m.resource >= 0 && b->neededResource(m.resource) <= 0)
			censorReason = "demand_disappeared";
		else if (int(b->unitsWorking.size()) >= b->desiredMaxUnitWorking)
			censorReason = "staffing_demand_filled";
		if (!hired && !*censorReason)
		{
			beginGradientDecision("hiring_episode", b->gid, u->gid);
			if (!b->freshHiringEligibility(u, m.resource))
				censorReason = "candidate_no_longer_eligible";
		}
		const bool unavailable = *censorReason;

		if (hired || unavailable)
		{
			impact.outcome(game->stepCounter, m.selected ? "missed_hire" : "missed_hire_candidate",
						   std::get<0>(key), std::get<1>(key), m.resource,
						   game->stepCounter - m.started, 0, 0, !hired, u ? u->posX : -1,
						   u ? u->posY : -1, m.unitIdentity, m.buildingIdentity,
						   hired ? "" : censorReason);
			it = impact.missedHires.erase(it);
		}
		else
			++it;
	}
	impact.ticks << game->stepCounter << ','
				 << BuildingGradientDiagnostics::now() - impact.lastTickNs << ',' << buildings
				 << ',' << units << ',' << slots << ',' << impact.deliveries << ',' << completed
				 << ',' << hungry << ',' << dead << '\n';
}
void Map::finishGradientImpact()
{
	if (!gradientRuntime->impact)
		return;
	auto &impact = *gradientRuntime->impact;
	if (impact.counterfactualEvent && !impact.counterfactualApplied)
		throw std::runtime_error("requested counterfactual decision was not reached");
	for (const auto &[uid, j] : impact.journeys)
		impact.outcome(game->stepCounter, "unfinished_trip", j.building, uid, j.resource,
					   game->stepCounter - j.started, j.distance, j.reversals, true, j.x, j.y,
					   j.identity, j.buildingIdentity, "horizon");
	for (const auto &[key, m] : impact.missedHires)
		impact.outcome(game->stepCounter, m.selected ? "missed_hire" : "missed_hire_candidate",
					   std::get<0>(key), std::get<1>(key), m.resource,
					   game->stepCounter - m.started, 0, 0, true, -1, -1, m.unitIdentity,
					   m.buildingIdentity);
	impact.journeys.clear();
	impact.missedHires.clear();
	impact.decisions.flush();
	impact.outcomes.flush();
	impact.ticks.flush();
}

void Map::recordHiringCandidate(Building *b, Unit *unit, int resource, int liveReason,
								int freshReason, int liveScore, int freshScore)
{
	if (auto &impact = gradientRuntime->impact)
	{
		const auto kind = impact->kind;
		const auto uid = impact->unit;
		impact->kind = "hiring_candidate";
		impact->unit = unit->gid;
		impact->record(liveReason < 0 ? unit->gid : -1, freshReason < 0 ? unit->gid : -1, resource,
					   resource, liveScore, freshScore, 0,
					   game->stepCounter -
						   b->lastGlobalGradientUpdateStepCounter[unit->swimClass()],
					   gradientRuntime->buildings.pending.count({b->gid, unit->swimClass()}),
					   liveReason, freshReason);
		if (liveReason >= 0 && freshReason < 0 &&
			int(b->unitsWorking.size()) < b->desiredMaxUnitWorking)
			impact->missedHires.try_emplace(
				{b->gid, unit->gid, false, unit->scriptIdentity, b->scriptIdentity, resource},
				BuildingGradientImpact::MissedHire{unit->scriptIdentity, b->scriptIdentity,
												   game->stepCounter, resource, false});
		impact->kind = kind;
		impact->unit = uid;
	}
}
