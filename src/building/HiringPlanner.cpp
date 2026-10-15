// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiringPlanner.h"
#include "HiringRanking.h"
#include "Map.h"
#include "Unit.h"
#include "Building.h"
#include "SharedBuildingGradient.h"
#include "ai/observation/AIWorldView.h"
#include "field/AirGradient.h"
#include <algorithm>
#include <climits>
#include <map>
#include <queue>
#include <tuple>

namespace HiringPlanner
{
namespace
{
using World = AIEngine::AIWorldView;
using B = SimulationSnapshot::BuildingView;
using U = SimulationSnapshot::UnitView;
// Hiring computations are private executor jobs. Reuse complete bounded air
// fields here without enrolling requests into the owner's publication queues.
class PrivateAirFields
{
    const World& world;
    std::vector<std::uint8_t> passable;
    std::vector<gradient_kernel::EntrySteps> costs;
    GradientWorkspace workspace;
public:
    explicit PrivateAirFields(const World& value):world(value) {}
    field::air::Traversal traversal()
    {
        if(passable.empty()) {
            GradientExecutionAudit::record(GradientExecutionAudit::Kind::AirTraversalPlane);
            const auto cells=std::size_t(world.width)*world.height;
            passable.resize(cells); costs.resize(cells);
            for(std::size_t cell=0;cell<cells;++cell) {
                passable[cell]=world.terrainPropertiesAt(cell).flyable;
                costs[cell]=field::air::compileSteps((*world.cellRules)[world.cellRuleAt(cell)].airCost);
            }
        }
        return {field::Grid(world.width,world.height),passable,costs};
    }
    std::vector<Uint16> build(int x,int y,field::air::Direction direction)
    {
        auto inputs=traversal();
        std::vector<Uint16> values(inputs.grid.cells());
        const field::air::Goal seed{Uint32(inputs.grid.index(x,y)),0,0};
        field::air::build(inputs,std::span(&seed,1),values,workspace,{},
            field::air::GoalPolicy::ApproachBlocked,direction);
        return values;
    }
    int tiles(std::span<const Uint16> values,int x,int y,bool approachBlocked)
    {
        auto inputs=traversal();
        const auto cell=inputs.grid.index(x,y);
        auto best=inputs.passable[cell] ? field::air::decode(values[cell]) : std::nullopt;
        if(!inputs.passable[cell] && approachBlocked)
            inputs.grid.neighborIndices(cell,field::Surrounding,[&](int neighbor) {
                if(const auto cost=field::air::decode(values[neighbor]);cost && (!best || *cost<*best)) best=cost;
            });
        return best ? int(field::air::ceilTiles(*best)) : INT_MAX;
    }
};
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
	if (u.carriedMaterial == material && !u.widePrimaryCargo() &&
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
		(u.carriedMaterial >= 0) + (cargo ? cargo->size() : 0) - (u.widePrimaryCargo() ? 1 : 0);
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
// Lower bounds ignore obstacles. They keep missing/stale route estimates
// recruitable, so normal live admission can establish or refresh shared fields.
int buildingEstimate(const World &world, const B &b, int x, int y, BuildingRoute route)
{
	if (route != BuildingRoute::Footprint)
		return std::max(0, warped(world, x, y, b.posX, b.posY) - int(b.unitStayRange));
	const auto &type = world.catalog->at(b.typeNum).resolvedType;
	const auto axis = [](int value, int origin, int extent, int size)
	{
		const int offset = (value - origin) & (size - 1);
		return offset < extent ? 0 : std::min(offset - extent + 1, size - offset);
	};
	return std::max(0, std::max(axis(x, b.posX, type.width, world.width),
		axis(y, b.posY, type.height, world.height)) - 1);
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
	World world(input.world);
    PrivateAirFields airFields(world);
	if (team >= world.teams.size())
		throw std::invalid_argument("Hiring team outside snapshot");
	Output output;
	output.tick = world.tick;
	output.team = team;
	const auto supplies = prepare(input, team);
	output.buildings.reserve(supplies.size());
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
		BuildingLists result{b.identity, b.typeNum, {}};
		std::vector<Key> jobs;
		for (unsigned r = 0; r < MaterialCount; ++r)
			if (type.maxMaterial[r] > 0 || b.wishedMaterials[r] > 0)
			{
				if (!supply->vacancies[r])
					continue;
				jobs.push_back({Job::Delivery, int(r)});
				jobs.push_back({Job::Fetch, int(r)});
			}
		std::array<std::vector<int>, 3> distances;
		std::vector<Uint16> airToBuilding;
		for (unsigned role = 0; role < 3; ++role)
			if (type.attractsRole(role))
				jobs.push_back({Job(unsigned(Job::Clear) + role), -1});
		if (!jobs.empty())
			indexTeam();
		for (auto key : jobs)
		{
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
			if (!any)
			{
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
				Candidate candidate = rankingBase(key.job, u, traits);
				if (!couldWin(candidate))
				{
					++output.metrics.distanceBoundSkips;
					continue;
				}
				const int swim = swimClass(u);
				const int budget = food(u);
				const bool immobile =
					!u.performance[FLY] && !u.performance[WALK] && !u.performance[SWIM];
				int resourceDistance = 0;
				if (key.job == Job::Fetch)
				{
					const auto modes = supply->modes[key.material];
					// Reject and bound with the existing immutable resource plane before
					// reading a shared building route. Special supplier/air
					// modes keep the optimistic estimate used by live admission.
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
					Candidate bound = candidate;
					// R + max(B, R) >= 2R for every nonnegative building distance.
					bound.value += std::int64_t(resourceDistance) * 512;
					if (!couldWin(bound))
					{
						++output.metrics.distanceBoundSkips;
						continue;
					}
				}
				int distance;

				auto &memo = distances[int(resolvedRoute)];
				if (memo.empty())
					memo.assign(Unit::MAX_COUNT, -1);
				auto &cached = memo[Unit::GIDtoID(u.gid)];
				if (cached != -1)
				{
					distance = cached;
				}
				else if (u.performance[FLY])
				{
                    if(world.airTerrainConstraints) {
                        if(airToBuilding.empty())
                            airToBuilding=airFields.build(b.posX,b.posY,field::air::Direction::ToDestination);
                        distance=airFields.tiles(airToBuilding,u.posX,u.posY,false);
                    } else distance=warped(world,u.posX,u.posY,b.posX,b.posY);
					if (key.job == Job::Clear)
					{
                        std::vector<Uint16> clearing;
                        if(world.airTerrainConstraints)
                            clearing=airFields.build(u.posX,u.posY,field::air::Direction::FromSource);
                        GradientExecutionAudit::record(GradientExecutionAudit::Kind::AirGoalEnumeration);
						distance = INT_MAX;
						const MaterialMask selected = b.clearingMaterialMask;
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
                                const int candidate=world.airTerrainConstraints
                                    ? airFields.tiles(clearing,x,y,true) : warped(world,u.posX,u.posY,x,y);
                                distance=std::min(distance,candidate);
							}
					}
				}
				else
				{
					if (!buildings[swim])
					{
						++output.metrics.missingBuildingFields;
						distance = buildingEstimate(world, b, u.posX, u.posY, resolvedRoute);
					}
					else
					{
						++output.metrics.sharedQueries;
						distance = buildings[swim]->distance(u.posX, u.posY);
					}
				}
				cached = distance;
				if (distance == INT_MAX)
					continue;

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
			result.jobs.push_back(shortlist(key, std::move(candidates), limit));
		}
		// Only an outstanding or imminent attraction round makes private offers.
		// These reduce repeated shortlist exclusion between peers; they reserve
		// nothing in the live simulation and use no gameplay randomness.
		if (unsigned(std::max(0, b.subscriptionWorkingTimer)) + Hiring::PublicationDelay +
				Hiring::RefreshInterval - 1 >= 32)
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
			}
		}
	}
	if (canCoordinate)
	{
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
			}
	}
	return output;
}
} // namespace HiringPlanner
