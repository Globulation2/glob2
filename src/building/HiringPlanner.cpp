// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiringPlanner.h"
#include "HiringRanking.h"
#include "Map.h"
#include "Unit.h"
#include "Building.h"
#include "SharedBuildingGradient.h"
#include "ai/observation/AIWorldView.h"
#include "field/AirPathfind.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <map>
#include <queue>
#include <tuple>

namespace HiringPlanner
{
namespace
{
using Clock = std::chrono::steady_clock;
std::uint64_t elapsed(Clock::time_point start)
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
using World = AIEngine::AIWorldView;
using B = SimulationSnapshot::BuildingView;
using U = SimulationSnapshot::UnitView;
int swimClass(const U &u)
{
	return Map::swimClass(u.performance[WALK], u.performance[SWIM]);
}
int food(const U &u)
{
	if (u.hungriness <= 0)
		return INT_MAX;
	return int(std::clamp<std::int64_t>((std::int64_t(u.hungry) - u.trigHungry) / u.hungriness,
										INT_MIN, INT_MAX));
}
// Distance zero is an optimistic score: every actual route adds nonnegative
// cost. A bounded shortlist can reject a candidate only if this cannot win.
Candidate rankingBase(Job job, const U &u, const UnitRuntimeTraits &traits)
{
	Candidate candidate{u.identity, 0, 0};
	if (job == Job::Delivery || job == Job::Fetch)
	{
		candidate.level = HiringRanking::materialLevel(u.constructionLevel, u.level[WALK]);
		candidate.value = job == Job::Delivery ? -(food(u) >> 1) : (u.carriedMaterial >= 0 ? 5 * 256 : 0);
		return candidate;
	}
	const unsigned role = unsigned(job) - unsigned(Job::Clear);
	std::int64_t time = u.hungriness > 0
		? (std::int64_t(u.hungry) - (role == 0 ? u.trigHungry : 0)) / u.hungriness : INT_MAX;
	std::int64_t hp = std::int64_t(u.hp) * 16 / std::max(1, traits.flagRankingHealth);
	if (role == 1)
	{
		time = std::clamp<std::int64_t>(time, -1000000000, 1000000000);
		time *= time;
		hp = std::clamp<std::int64_t>(hp, -1000000000, 1000000000);
		hp *= hp;
	}
	candidate.level = role == 0 ? u.constructionLevel : role == 1 ? u.level[MAGIC_ATTACK_GROUND]
		: std::int64_t(u.performance[ATTACK_SPEED]) * u.realAttackStrength;
	candidate.value = -(role == 0 ? 1 : 2) * (time + hp);
	return candidate;
}
bool hasCargo(const Inputs &input, const U &u, int material)
{
	if (u.carriedMaterial == material)
		return true;
	if (const auto *cargo = input.world.entities->unitCargo.find(u.gid))
		for (const auto &entry : *cargo)
			if (entry.material == material)
				return true;
	return false;
}
bool hasDeliverableCargo(const Inputs &input, const U &u, int material, Uint64 multiplier)
{
	if (u.carriedMaterial == material && !u.widePrimaryCargo &&
		packetCanDeliver({u.carriedPacket.numerator, u.carriedPacket.denominator}, multiplier))
		return true;
	if (const auto *cargo = input.world.entities->unitCargo.find(u.gid))
		for (const auto &entry : *cargo)
			if (entry.material == material && packetCanDeliver(entry.packet, multiplier))
				return true;
	return false;
}
bool canCarry(const Inputs &input, const World &world, const U &u, int material)
{
	const auto &traits = world.unitTraits(u.typeNum);
	if (!traits.has(UnitRuntimeTraits::ExtendedCargo))
		return true;
	const auto *cargo = input.world.entities->unitCargo.find(u.gid);
	const unsigned count =
		(u.carriedMaterial >= 0) + (cargo ? cargo->size() : 0) - (u.widePrimaryCargo ? 1 : 0);
	if (count >= unsigned(traits.cargoCapacity))
		return false;
	if (u.carriedMaterial < 0 || hasCargo(input, u, material))
		return true;
	unsigned kinds = 1;
	MaterialMask mask = MaterialMask(1u << u.carriedMaterial);
	if (cargo)
		for (const auto &entry : *cargo)
			if (!(mask & (1u << entry.material)))
			{
				mask |= 1u << entry.material;
				++kinds;
			}
	return kinds < unsigned(traits.cargoKinds);
}
int warped(const World &world, int a, int b, int c, int d)
{
	int dx = std::abs(a - c), dy = std::abs(b - d);
	return std::max(std::min(dx, world.width - dx), std::min(dy, world.height - dy));
}
int resourceDistance(const World &world, std::span<const Uint16> values, int x, int y)
{
	if (values.size() != std::size_t(world.width) * world.height)
		throw std::invalid_argument("Resource plane dimensions differ from snapshot");
	auto value = values[world.tileIndex(x, y)];
	constexpr int neighbors[8][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0},
		{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
	for (unsigned d = 0; d < 8 && value <= GRADIENT_UNREACHABLE; ++d)
		value = values[world.tileIndex(x + neighbors[d][0], y + neighbors[d][1])];
	return value > GRADIENT_UNREACHABLE ? gradientTiles(value) : INT_MAX;
}
} // namespace
bool needsCandidates(const SimulationSnapshot::BuildingView &b)
{
	// Match the owner's maintained staffing demand, without advancing clocks
	// or recomputing demand through a live building on the worker.
	return b.buildingState == B::ALIVE && b.desiredMaxUnitWorking > 0 &&
		   b.working.count < unsigned(b.desiredMaxUnitWorking);
}
std::vector<Supply> prepare(const Inputs &inputs, unsigned team)
{
	const World world(inputs.world);
	unsigned stock = 0, direct = 0, extraDirect = 0;
	bool hasStock = false, hasDirect = false;
	for (const auto &kind : *world.catalog)
	{
		const auto &t = kind.resolvedType;
		stock |= t.runtimeSuppliesStockMask;
		direct |= t.runtimeSuppliesDirectStockMask;
		extraDirect |= t.runtimeSuppliesDirectStockMask & ~t.runtimeSuppliesStockMask;
	}
	hasStock = world.teams[team].supplierPresence & 1;
	hasDirect = world.teams[team].supplierPresence & 2;
	std::vector<Supply> supplies;
	std::vector<const B *> ordered;
	for (const auto *b : world.buildingSlots(team))
		if (b && needsCandidates(*b))
		{
			const auto &type = world.catalog->at(b->typeNum).resolvedType;
			Supply supply;
			supply.building = b->identity;
			supply.seats = unsigned(b->desiredMaxUnitWorking) - b->working.count;
			const auto resources = world.buildingResources(*b);
			static const SimulationSnapshot::FundingState unfunded{};
			const auto &funding = b->fundingOffset == SimulationSnapshot::Entities::NoRecord
									  ? unfunded
									  : inputs.world.entities->funding.at(b->fundingOffset);
			for (unsigned material = 0; material < MaterialCount; ++material)
			{
				const unsigned bit = 1u << material;
				const bool construction =
					type.isBuildingSite && (funding.constructionBudget[material] ||
											type.semantics.constructionCost[material]);
				// Replenishment is a catalog capability; use the same compiled traits.
				const bool replenish = type.semantics.replenishMaterialMask & bit;
				const int deliveryTarget = construction ? funding.constructionBudget[material]
										   : replenish  ? type.maxMaterial[material]
														: 0;
				const int reserved = type.useTeamMaterials
										 ? world.teams[team].reservedMaterials[material]
										 : funding.reservedMaterials[material];
				const int uncommitted = b->constructionResultState == B::REPAIR
											? 0
											: std::max(0, resources[material] - reserved);
				const int missing =
					construction
						? std::max(0, std::max(0, funding.constructionBudget[material] -
													  funding.constructionReserved[material]) -
										  uncommitted)
					: replenish ? std::max(0, type.maxMaterial[material] - resources[material])
								: 0;
				supply.materialDemand |= missing > 0;
				const int multiplier = type.materialMultiplier[material];
				const int target = (deliveryTarget + multiplier - 1) / multiplier;
				int served = target - (missing + multiplier - 1) / multiplier;
				for (const auto ref : world.workers(*b))
				{
					const auto *u = world.unit(ref);
					if (!u)
						throw std::logic_error("Hiring observed relationship lost unit");
					served += u->destinationPurpose == int(material);
				}
				if (missing > 0 && served < target)
					supply.vacancies[material] = std::min(supply.seats, unsigned(target - served));
				unsigned modes =
					supply.vacancies[material]
						? ((type.runtimeFetchesStockMask & stock & bit) && hasStock ? 1u : 0u) |
							  ((type.runtimeFetchesDirectStockMask & direct & bit) && hasDirect
								   ? 2u
								   : 0u)
						: 0;
				if (modes == 3 && !(extraDirect & bit))
					modes = 1;
				supply.modes[material] = modes;
				const auto supplied = ((modes & 1) ? type.runtimeSuppliesStockMask : 0) |
									  ((modes & 2) ? type.runtimeSuppliesDirectStockMask : 0);
				supply.consumerSpecific[material] = type.useTeamMaterials || (supplied & bit);
			}
			supplies.push_back(supply);
			ordered.push_back(b);
		}
	std::sort(ordered.begin(), ordered.end(),
			  [&](const B *a, const B *b)
			  {
				  if (a->priority != b->priority)
					  return a->priority > b->priority;
				  const auto &at = world.catalog->at(a->typeNum).resolvedType;
				  const auto &bt = world.catalog->at(b->typeNum).resolvedType;
				  if (at.semantics.workPriorityBias != bt.semantics.workPriorityBias)
					  return at.semantics.workPriorityBias > bt.semantics.workPriorityBias;
				  const int ar =
								(a->maxUnitWorking - int(a->working.count)) * int(b->working.count),
							br =
								(b->maxUnitWorking - int(b->working.count)) * int(a->working.count);
				  if (ar != br)
					  return ar > br;
				  int aw = 0, bw = 0;
				  for (unsigned m = 0; m < MaterialSlotCount; ++m)
				  {
					  aw += a->wishedMaterials[m];
					  bw += b->wishedMaterials[m];
				  }
				  return aw != bw ? aw > bw : a->gid < b->gid;
			  });
	for (unsigned rank = 0; rank < ordered.size(); ++rank)
		for (auto &supply : supplies)
			if (supply.building == ordered[rank]->identity)
				supply.visitRank = rank;
	return supplies;
}

namespace
{
// Ordering direction is constant for a job. Keep the small comparison inline
// in partition/sort instead of passing the job through every comparison.
struct CandidateOrder
{
	Job job;
	explicit CandidateOrder(Job value) : job(value) {}
	bool operator()(const Candidate &a, const Candidate &b) const
	{
		if (a.level != b.level || a.value != b.value)
			return job <= Job::Fetch
					   ? HiringRanking::materialBetter(a.level, a.value, b.level, b.value)
					   : HiringRanking::roleBetter(int(job) - int(Job::Clear), a.level, a.value,
												   b.level, b.value);
		if (a.unit.gid != b.unit.gid)
			return a.unit.gid < b.unit.gid;
		return a.unit.generation < b.unit.generation;
	}
};
} // namespace
bool better(const Candidate &a, const Candidate &b, Job job)
{
	return CandidateOrder(job)(a, b);
}
List shortlist(Key key, std::vector<Candidate> candidates, unsigned capacity)
{
	if (capacity < 1 || capacity > 128)
		throw std::invalid_argument("Hiring capacity must be 1..128");
	const CandidateOrder compare(key.job);
	const auto count = std::min<std::size_t>(capacity, candidates.size());
	if (count < candidates.size())
		std::nth_element(candidates.begin(), candidates.begin() + count, candidates.end(), compare);
	std::sort(candidates.begin(), candidates.begin() + count, compare);
	List result{key, {}};
	result.candidates.assign(candidates.begin(), candidates.begin() + count);
	return result;
}
Output Planner::rank(const Inputs &input, unsigned team, unsigned capacity)
{
	if (capacity < 1 || capacity > 128)
		throw std::invalid_argument("Hiring capacity must be 1..128");
	const auto start = std::chrono::steady_clock::now();
	World world(input.world);
	if (team >= world.teams.size())
		throw std::invalid_argument("Hiring team outside snapshot");
	Output output;
	output.tick = world.tick;
	output.team = team;
	const auto prepareStart = Clock::now();
	const auto supplies = prepare(input, team);
	output.buildings.reserve(supplies.size());
	output.metrics.preparationNs += elapsed(prepareStart);
	unsigned teamSeats = 0;
	bool canCoordinate = true;
	for (const auto &supply : supplies)
		if (Building::GIDtoTeam(supply.building.gid) == team)
		{
			teamSeats += supply.seats;
			const auto *b = world.buildingAtSlot(supply.building.gid);
			if ((b && (world.catalog->at(b->typeNum).resolvedType.attractsRole(0) ||
					   world.catalog->at(b->typeNum).resolvedType.attractsRole(1) ||
					   world.catalog->at(b->typeNum).resolvedType.attractsRole(2))))
				canCoordinate = false;
		}
	canCoordinate &= teamSeats > 0 && teamSeats + 2u <= capacity;
	++output.metrics.refreshes;
	output.metrics.setupNs += elapsed(start);
	// Qualification/cargo indexes are immutable and shared across this team's
	// building refreshes. Keep original slot order for deterministic traversal.
	std::vector<const U *> transport;
	std::array<std::vector<const U *>, 3> roles;
	std::array<bool, MaterialCount> carrying{};
	bool indexedTeam = false;
	const auto indexTeam = [&]
	{
		if (indexedTeam)
			return;
		indexedTeam = true;
		for (const auto *unit : world.unitSlots(team))
			if (unit && !unit->isDead && unit->activity == U::ACT_RANDOM &&
				unit->medical == U::MED_FREE)
			{
				const auto &traits = world.unitTraits(unit->typeNum);
				if (unit->activity == U::ACT_RANDOM && unit->medical == U::MED_FREE &&
					traits.has(UnitRuntimeTraits::Transport) && unit->performance[BUILD] > 0)
				{
					transport.push_back(unit);
					for (unsigned r = 0; r < MaterialCount; ++r)
						carrying[r] |= hasCargo(input, *unit, r);
				}
				for (unsigned role = 0; role < 3; ++role)
					if (traits.recruitmentMask & (1u << role))
						roles[role].push_back(unit);
			}
	};
	std::vector<const B *> orderedBuildings;
	for (const auto *building : world.buildingSlots(team))
		if (building)
		{
			++output.metrics.buildingsExamined;
			if (needsCandidates(*building))
				orderedBuildings.push_back(building);
			else
				++output.metrics.buildingsSkippedNoDemand;
		}
	const auto needOf = [&](const B &b) -> const Supply &
	{
		return *std::find_if(supplies.begin(), supplies.end(),
							 [&](const auto &s) { return s.building == b.identity; });
	};
	std::sort(orderedBuildings.begin(), orderedBuildings.end(),
			  [&](const B *a, const B *b) { return needOf(*a).visitRank < needOf(*b).visitRank; });
	std::array<bool, Unit::MAX_COUNT> roleOffers{};
	for (const auto *building : orderedBuildings)
	{
		if (!building)
			continue;
		++output.metrics.buildingsNeedingHiring;
		const auto &b = *building;
		const auto &type = world.catalog->at(b.typeNum).resolvedType;
		const auto supply = std::find_if(supplies.begin(), supplies.end(), [&](const auto &value)
										 { return value.building == b.identity; });
		if (supply == supplies.end())
			throw std::logic_error("Hiring input missing supplier modes");
		BuildingLists result{b.identity, b.typeNum, configuration(b), {}};
		std::vector<Key> jobs;
		for (unsigned r = 0; r < MaterialCount; ++r)
			if (type.maxMaterial[r] > 0 || b.wishedMaterials[r] > 0)
			{
				if (!supply->vacancies[r])
				{
					output.metrics.jobsSkippedNoQuota += 2;
					continue;
				}
				jobs.push_back({Job::Delivery, int(r)});
				jobs.push_back({Job::Fetch, int(r)});
			}
		std::array<std::vector<int>, 3> distances;
		const auto flyable = [&](int x, int y)
		{ return world.terrainPropertiesAt(world.tileIndex(x, y)).flyable; };
		const auto airCost = [&](int x, int y)
		{ return (*world.cellRules)[world.cellRuleAt(world.tileIndex(x, y))].airCost; };
		using AirField = field::AirDistanceField<decltype(flyable), decltype(airCost)>;
		std::unique_ptr<AirField> airToBuilding;
		for (unsigned role = 0; role < 3; ++role)
			if (type.attractsRole(role))
				jobs.push_back({Job(unsigned(Job::Clear) + role), -1});
		if (!jobs.empty())
			indexTeam();
		for (auto key : jobs)
		{
			++output.metrics.jobs;
			const auto precheckStart = Clock::now();
			List list{key, {}};
			std::vector<Candidate> candidates;
			bool any = true;
			if (key.job == Job::Delivery)
			{
				any = carrying[key.material];
			}
			else if (key.job == Job::Fetch &&
					 input.world.resources->materialSourceCounts[key.material] == 0)
			{
				any = false;
				const auto modes = supply->modes[key.material];
				if (modes)
					for (const auto &supplier : world.buildings)
					{
						const auto &st = world.catalog->at(supplier.typeNum).resolvedType;
						const auto supplied = ((modes & 1) ? st.runtimeSuppliesStockMask : 0) |
											  ((modes & 2) ? st.runtimeSuppliesDirectStockMask : 0);
						if (supplier.team == int(team) && supplier.identity != b.identity &&
							supplier.buildingState == B::ALIVE &&
							!(supplier.usesTeamResources && b.usesTeamResources) &&
							(supplied & supplier.availableSupplyMask & (1u << key.material)))
						{
							any = true;
							break;
						}
					}
			}
			output.metrics.precheckNs += elapsed(precheckStart);
			if (!any)
			{
				++output.metrics.skippedJobs;
				result.jobs.push_back(std::move(list));
				continue;
			}
			std::array<std::shared_ptr<SharedBuildingGradient>, SWIM_CLASS_COUNT> buildings{};
			// Read only shared published resource planes. Missing or consumer-specific
			// planes use a zero resource-distance estimate; live admission performs
			// the normal destination/reachability checks and initializes shared fields.
			const auto route = key.job == Job::Delivery || key.job == Job::Fetch
								   ? BuildingRoute::Footprint
							   : key.job == Job::Clear ? BuildingRoute::Clearing
													   : BuildingRoute::Combat;
			// Exploration and defense share the live combat access contract.
			const auto resolvedRoute = route;
			for (const auto &captured : *input.routes)
				if (captured.building == b.identity &&
					captured.slot / SWIM_CLASS_COUNT == int(resolvedRoute))
					buildings[captured.slot % SWIM_CLASS_COUNT] = captured.field;
			const unsigned vacancies =
				key.material >= 0 ? supply->vacancies[key.material] : supply->seats;
			const unsigned limit = std::min(capacity, (canCoordinate ? teamSeats : vacancies) + 2u);
			const auto evaluationStart = Clock::now();
			const auto &indexed = (key.job == Job::Delivery || key.job == Job::Fetch)
									  ? transport
									  : roles[unsigned(key.job) - unsigned(Job::Clear)];
			candidates.reserve(std::min<std::size_t>(limit, indexed.size()));
			const CandidateOrder compare(key.job);
			const auto couldWin = [&](const Candidate &bound)
			{
				return !pruneCandidates || candidates.size() < limit || compare(bound, candidates.front());
			};
			for (const auto *unit : indexed)
			{
				++output.candidateVisits;
				const auto &u = *unit;
				const auto &traits = world.unitTraits(u.typeNum);
				const bool transport = key.job == Job::Delivery || key.job == Job::Fetch;
				if (!transport && roleOffers[Unit::GIDtoID(u.gid)])
					continue;
				if (transport)
				{
					if (!traits.has(UnitRuntimeTraits::Transport) || u.performance[BUILD] <= 0 ||
						(type.isBuildingSite && !traits.has(UnitRuntimeTraits::Construct)) ||
						u.constructionLevel < type.semantics.requiredWorkerLevel)
						continue;
					if (key.job == Job::Delivery &&
						!hasDeliverableCargo(input, u, key.material,
											 type.materialMultiplier[key.material]))
						continue;
					if (key.job == Job::Fetch &&
						(u.performance[HARVEST] <= 0 || hasCargo(input, u, key.material) ||
						 !canCarry(input, world, u, key.material)))
						continue;
				}
				else
				{
					const unsigned role = unsigned(key.job) - unsigned(Job::Clear);
					if (u.attached == b.identity ||
						(role == 2 && u.movement == Unit::MOV_ATTACKING_TARGET))
						continue;
					constexpr unsigned types[3] = {WORKER, EXPLORER, WARRIOR};
					const auto &selection = type.semantics.attractionUnits[role];
					const bool selected = selection.resolved.empty()
						? selection.matches(u.typeNum, type.zonable[types[role]] ? 1u << types[role] : 0)
						: unsigned(u.typeNum) < selection.resolved.size() && selection.resolved[u.typeNum];
					if (!(traits.recruitmentMask & (1u << role)) || !selected)
						continue;
					if ((role == 0 && u.constructionLevel < b.minWorkerLevelToFlag) ||
						(role == 1 && b.explorersRequireBombing && !u.level[MAGIC_ATTACK_GROUND]) ||
						(role == 2 && std::min(u.level[ATTACK_SPEED], u.level[ATTACK_STRENGTH]) <
										  b.minLevelToFlag))
						continue;
				}
				++output.metrics.availableEvaluations;
				Candidate candidate = rankingBase(key.job, u, traits);
				if (!couldWin(candidate))
				{
					++output.metrics.distanceBoundSkips;
					continue;
				}
				const int swim = swimClass(u);
				int distance;

				auto &memo = distances[int(resolvedRoute)];
				if (memo.empty())
					memo.assign(Unit::MAX_COUNT, -1);
				auto &cached = memo[Unit::GIDtoID(u.gid)];
				if (cached != -1)
				{
					distance = cached;
					++output.metrics.distanceReuses;
				}
				else if (u.performance[FLY])
				{
					++output.metrics.airQueries;
					if (!airToBuilding)
						airToBuilding = std::make_unique<AirField>(
							world.width, world.height, b.posX, b.posY, flyable, airCost,
							world.airTerrainConstraints,
							field::AirDistanceDirection::ToDestination);
					const auto cost =
						airToBuilding->enabled()
							? airToBuilding->costTo(u.posX, u.posY)
							: unsigned(warped(world, u.posX, u.posY, b.posX, b.posY)) *
								  GRADIENT_STEP;
					distance = cost == AirField::unreachable
								   ? INT_MAX
								   : int((cost + GRADIENT_STEP - 1) / GRADIENT_STEP);
					if (key.job == Job::Clear)
					{
						field::AirDistanceField clearing(
							world.width, world.height, u.posX, u.posY, [&](int x, int y)
							{ return world.terrainPropertiesAt(world.tileIndex(x, y)).flyable; },
							[&](int x, int y)
							{
								return (*world.cellRules)[world.cellRuleAt(world.tileIndex(x, y))]
									.airCost;
							},
							world.airTerrainConstraints);
						distance = INT_MAX;
						MaterialMask selected = 0;
						for (unsigned r = 0; r < MaterialCount; ++r)
							if (b.clearingMaterials[r])
								selected |= MaterialMask(1u << r);
						for (int y = 0; y < world.height; ++y)
							for (int x = 0; x < world.width; ++x)
							{
								const auto i = world.tileIndex(x, y);
								const auto resource = world.resourceAt(i).resource;
								if (world.distanceSquared(x, y, b.posX, b.posY) >
										b.unitStayRange * b.unitStayRange ||
									(world.areasAt(i).forbidden & world.teams[team].mask) ||
									resource.type == NO_RES_TYPE ||
									!world.resourceRegistry
										 ->properties(static_cast<ResourceId>(resource.type))
										 .clearable ||
									!(MapState::materialMaskAt(world.state(), i) & selected))
									continue;
								const auto c = clearing.enabled()
												   ? clearing.costTo(x, y)
												   : unsigned(warped(world, u.posX, u.posY, x, y)) *
														 GRADIENT_STEP;
								if (c != decltype(clearing)::unreachable)
									distance = std::min(
										distance, int((c + GRADIENT_STEP - 1) / GRADIENT_STEP));
							}
					}
				}
				else
				{
					if (!buildings[swim])
					{
						++output.metrics.missingBuildingFields;
						continue;
					}
					++output.metrics.sharedQueries;
					distance = buildings[swim]->distance(
						u.posX, u.posY, output.metrics.sharedWaitNs, output.metrics.sharedQueryNs,
						output.metrics.sharedExpansions);
				}
				cached = distance;
				if (distance == INT_MAX)
					continue;
				const int budget = food(u);
				const bool immobile =
					!u.performance[FLY] && !u.performance[WALK] && !u.performance[SWIM];
				const bool tooFar =
					key.job == Job::Explore && u.performance[FLY] && !world.airTerrainConstraints
						? budget < 0 || world.distanceSquared(u.posX, u.posY, b.posX, b.posY) >
											std::int64_t(budget) * budget
						: (key.job == Job::Explore ? distance > budget : distance >= budget);
				if ((immobile && distance > (transport ? 1 : 0)) || tooFar)
					continue;
				if (transport)
				{
					if (key.job == Job::Delivery)
						candidate.value += distance;
					else
					{
						Candidate bound = candidate;
						bound.value += std::int64_t(distance) * 256;
						if (!couldWin(bound))
						{
							++output.metrics.distanceBoundSkips;
							continue;
						}
						int resourceDistance = 0;
						const auto modes = supply->modes[key.material];
						// Ground planes cannot represent air routes or consumer exclusions.
						const auto plane = !u.performance[FLY] && !(modes & 2) &&
							!supply->consumerSpecific[key.material]
							? world.resourceGradient(team, key.material, swim, modes & 1)
							: std::span<const Uint16>{};
						if (!plane.empty())
						{
							++output.metrics.resourcePlaneReads;
							resourceDistance = HiringPlanner::resourceDistance(world, plane, u.posX, u.posY);
						}
						else ++output.metrics.resourceEstimateFallbacks;
						if (resourceDistance == INT_MAX || resourceDistance >= budget ||
							(immobile && resourceDistance > 1))
							continue;
						candidate.value += (std::int64_t(resourceDistance) + std::max(distance, resourceDistance)) * 256;
					}
				}
				else
				{
					const unsigned role = unsigned(key.job) - unsigned(Job::Clear);
					if (role == 1)
					{
						distance = u.performance[FLY] && !world.airTerrainConstraints
									   ? world.distanceSquared(u.posX, u.posY, b.posX, b.posY)
									   : int(std::min<std::int64_t>(
											 INT_MAX, std::int64_t(distance) * distance));
					}
					candidate.value += distance;
				}
				++output.metrics.rankedCandidates;
				if (candidates.size() < limit)
				{
					candidates.push_back(candidate);
					std::push_heap(candidates.begin(), candidates.end(), compare);
				}
				else if (compare(candidate, candidates.front()))
				{
					std::pop_heap(candidates.begin(), candidates.end(), compare);
					candidates.back() = candidate;
					std::push_heap(candidates.begin(), candidates.end(), compare);
				}
			}
			output.metrics.evaluationNs += elapsed(evaluationStart);
			const auto sortStart = Clock::now();
			list = shortlist(key, std::move(candidates), limit);
			output.metrics.sortOutputNs += elapsed(sortStart);
			output.retainedBytes += list.candidates.capacity() * sizeof(Candidate);
			result.jobs.push_back(std::move(list));
		}
		// Only an outstanding or imminent attraction round makes private offers.
		// These reduce repeated shortlist exclusion between peers; they reserve
		// nothing in the live simulation and use no gameplay randomness.
		const bool imminent =
			b.hiringRecruitmentDeadline >= world.tick && b.hiringRecruitmentDeadline != 0;
		if (imminent || unsigned(std::max(0, b.subscriptionWorkingTimer)) +
								Hiring::PublicationDelay + Hiring::RefreshInterval - 1 >=
							32)
		{
			std::array<unsigned, 3> assigned{};
			unsigned deliveries = 0;
			const auto targets = Hiring::roleTargets(
				unsigned(b.desiredMaxUnitWorking), supply->materialDemand,
				{type.attractsRole(0), type.attractsRole(1), type.attractsRole(2)});
			for (unsigned i = 0; i < b.working.count; ++i)
			{
				const auto ref = input.world.entities->relationships.at(b.working.offset + i);
				const auto *u = world.unitAtSlot(ref.gid);
				if (u && u->identity == ref)
					deliveries += u->activity == U::ACT_FILLING;
				if (u && u->identity == ref && u->activity == U::ACT_FLAG)
				{
					const int role = int(u->jobPurpose) - int(UnitJobPurpose::Clear);
					if (role >= 0 && role < 3)
						++assigned[role];
				}
			}
			const unsigned deliveryTarget = targets[0];
			const unsigned deliveryVacancies =
				deliveryTarget > deliveries ? deliveryTarget - deliveries : 0;
			const unsigned roleSeats = supply->seats - std::min(supply->seats, deliveryVacancies);
			for (unsigned seat = 0; seat < roleSeats; ++seat)
			{
				const Candidate *offer = nullptr;
				int selectedRole = -1;
				for (unsigned pass = 0; pass < 2 && !offer; ++pass)
					for (const auto &list : result.jobs)
						if (list.key.job >= Job::Clear)
						{
							const unsigned role = unsigned(list.key.job) - unsigned(Job::Clear);
							if ((!pass && assigned[role] >= targets[role + 1]) ||
								(selectedRole >= 0 && assigned[role] >= assigned[selectedRole]))
								continue;
							for (const auto &entry : list.candidates)
								if (!roleOffers[Unit::GIDtoID(entry.unit.gid)])
								{
									offer = &entry;
									selectedRole = role;
									break;
								}
						}
				if (!offer)
					break;
				roleOffers[Unit::GIDtoID(offer->unit.gid)] = true;
				++assigned[selectedRole];
			}
		}
		output.buildings.push_back(std::move(result));
	}
	std::sort(output.buildings.begin(), output.buildings.end(),
			  [](const auto &a, const auto &b) { return a.building.gid < b.building.gid; });
	if (canCoordinate)
		for (const auto &building : output.buildings)
		{
			int material = -1;
			for (const auto &list : building.jobs)
				if (!list.candidates.empty())
				{
					if (material != -1 && material != list.key.material)
						canCoordinate = false;
					material = list.key.material;
				}
			if (material < 0)
				canCoordinate = false;
		}
	if (!canCoordinate)
	{
		++output.metrics.coordinationSkipped;
		output.retainedBytes = 0;
		for (auto &building : output.buildings)
		{
			const auto &need = *std::find_if(supplies.begin(), supplies.end(), [&](const auto &x)
											 { return x.building == building.building; });
			for (auto &list : building.jobs)
			{
				const auto n = std::min(
					capacity,
					(list.key.material >= 0 ? need.vacancies[list.key.material] : need.seats) + 2u);
				if (list.candidates.size() > n)
					std::vector<Candidate>(list.candidates.begin(), list.candidates.begin() + n)
						.swap(list.candidates);
				output.retainedBytes += list.candidates.capacity() * sizeof(Candidate);
			}
		}
	}
	if (canCoordinate)
	{
		const auto coordinationStart = Clock::now();
		++output.metrics.coordinatedTeams;
		std::vector<unsigned> order;
		std::vector<const Supply *> needs;
		for (unsigned i = 0; i < output.buildings.size(); ++i)
		{
			const auto id = output.buildings[i].building;
			needs.push_back(&*std::find_if(supplies.begin(), supplies.end(),
										   [&](const auto &x) { return x.building == id; }));
			order.push_back(i);
		}
		std::sort(order.begin(), order.end(), [&](unsigned a, unsigned b)
				  { return needs[a]->visitRank < needs[b]->visitRank; });
		// Every candidate belongs to this team and this immutable snapshot,
		// which has one incarnation per slot. Membership needs no tree lookup.
		std::array<bool, Unit::MAX_COUNT> chosen{};
		struct Planned
		{
			std::vector<Candidate> entries;
			unsigned cursor = 0;
		};
		std::vector<std::vector<Planned>> primary(output.buildings.size());
		std::vector<unsigned> assigned(output.buildings.size());
		std::vector<std::array<unsigned, MaterialCount>> materialAssigned(output.buildings.size());
		for (unsigned bi = 0; bi < output.buildings.size(); ++bi)
			primary[bi].resize(output.buildings[bi].jobs.size());
		// Respect priority buckets and visit peers once per hiring round.
		for (unsigned begin = 0; begin < order.size();)
		{
			unsigned end = begin + 1;
			const auto priority =
				world.buildingAtSlot(output.buildings[order[begin]].building.gid)->priority;
			while (end < order.size() &&
				   world.buildingAtSlot(output.buildings[order[end]].building.gid)->priority ==
					   priority)
				++end;
			bool progress = true;
			while (progress)
			{
				progress = false;
				for (unsigned pos = begin; pos < end; ++pos)
				{
					const auto bi = order[pos];
					auto &building = output.buildings[bi];
					if (assigned[bi] >= needs[bi]->seats)
						continue;
					const Candidate *best = nullptr;
					unsigned jobIndex = 0;
					for (auto wanted : {Job::Delivery, Job::Fetch})
					{
						for (unsigned j = 0; j < building.jobs.size(); ++j)
							if (building.jobs[j].key.job == wanted &&
								materialAssigned[bi][building.jobs[j].key.material] <
									needs[bi]->vacancies[building.jobs[j].key.material])
							{
								const auto &entries = building.jobs[j].candidates;
								auto &cursor = primary[bi][j].cursor;
								// Prefixes are sorted; chosen membership only grows.
								// The first remaining entry is this job's best offer.
								while (cursor < entries.size() &&
									   chosen[Unit::GIDtoID(entries[cursor].unit.gid)])
									++cursor;
								if (cursor < entries.size())
								{
									const auto &c = entries[cursor];
									if (!best || better(c, *best, wanted))
									{
										best = &c;
										jobIndex = j;
									}
								}
							}
						if (best)
							break;
					}
					if (best)
					{
						chosen[Unit::GIDtoID(best->unit.gid)] = true;
						primary[bi][jobIndex].entries.push_back(*best);
						++materialAssigned[bi][building.jobs[jobIndex].key.material];
						++assigned[bi];
						progress = true;
					}
				}
			}
			begin = end;
		}
		output.retainedBytes = 0;
		for (unsigned bi = 0; bi < output.buildings.size(); ++bi)
			for (unsigned j = 0; j < output.buildings[bi].jobs.size(); ++j)
			{
				auto &list = output.buildings[bi].jobs[j];
				const unsigned vacancies = list.key.material >= 0
											   ? needs[bi]->vacancies[list.key.material]
											   : needs[bi]->seats;
				const auto limit = std::min(capacity, vacancies + 2u);
				std::vector<Candidate> published;
				if (!list.candidates.empty())
					published.reserve(limit);
				for (const auto &c : primary[bi][j].entries)
					if (published.size() < limit)
						published.push_back(c);
				for (const auto &c : list.candidates)
					if (published.size() < limit && !chosen[Unit::GIDtoID(c.unit.gid)])
						published.push_back(c);
				list.candidates = std::move(published);
				output.retainedBytes += list.candidates.capacity() * sizeof(Candidate);
			}
		output.metrics.coordinationNs += elapsed(coordinationStart);
	}
	output.retainedBytes += sizeof(Output) + output.buildings.capacity() * sizeof(BuildingLists);
	for (const auto &building : output.buildings)
		output.retainedBytes += building.jobs.capacity() * sizeof(List);
	output.ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - start)
					.count();
	return output;
}
} // namespace HiringPlanner
