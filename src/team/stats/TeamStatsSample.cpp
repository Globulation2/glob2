// SPDX-License-Identifier: GPL-3.0-or-later
#include "TeamStat.h"
#include "sim/snapshot/TeamRecords.h"
#include "Bullet.h"
#include "ai/observation/AIWorldView.h"
#include "ai/observation/ObservationQueries.h"
#include <algorithm>
#include <limits>
#include <tuple>
using AIEngine::AIWorldView;
using SimulationSnapshot::BuildingView;
using SimulationSnapshot::UnitView;
namespace
{
bool has(const UnitView &u, UnitRuntimeTraits::Flag f)
{
	return (u.capabilityFlags & f) != 0;
}
const BuildingType &type(const AIWorldView &w, const BuildingView &b)
{
	return w.catalog->at(b.typeNum).resolvedType;
}
int longLevel(const AIWorldView &w, const BuildingView &b)
{
	const auto &t = type(w, b);
	return 2 * t.level + 1 - t.isBuildingSite;
}
const BuildingView *resolve(const AIWorldView &w, BuildingRef r)
{
	return w.building(r);
}
const BuildingView *attached(const AIWorldView &w, const UnitView &u)
{
	return resolve(w, u.attached);
}
const BuildingView *target(const AIWorldView &w, const UnitView &u)
{
	return resolve(w, u.target);
}
bool hungry(const AIWorldView &w, const UnitView &u)
{
	return !w.rules.hungerDisabled && u.hungriness > 0 &&
		   u.hungry <= (u.carriedMaterial == -1 ? u.trigHungry : u.trigHungryCarrying);
}
void observeLabour(const AIWorldView &world, GameplayMeasurements &m, const UnitView *u)
{
	using M = GameplayMeasurements;
	if (!u || u->isDead || !has(*u, UnitRuntimeTraits::Transport))
		return;
	auto distanceTo = [&](const BuildingView *b)
	{
		return Uint64(AIEngine::ObservationQueries::distanceMax(world, u->posX, u->posY,
																b->posX - type(world, *b).decLeft,
																b->posY - type(world, *b).decTop));
	};
	const bool inside = u->displacement == UnitState::DIS_INSIDE ||
						u->displacement == UnitState::DIS_ENTERING_BUILDING ||
						u->displacement == UnitState::DIS_EXITING_BUILDING;
	if (u->medical != UnitState::MED_FREE)
	{
		const bool hungry = u->medical == UnitState::MED_HUNGRY;
		if (inside)
			++m.labour[hungry ? M::EAT_INSIDE : M::HEAL_INSIDE];
		else if (!target(world, *u))
			++m.labour[hungry ? M::EAT_NO_INN : M::HEAL_NO_HOSPITAL];
		else
		{
			++m.labour[hungry ? M::EAT_WALKING : M::HEAL_WALKING];
			if (hungry)
			{
				m.eatWalkDistance += distanceTo(target(world, *u));
				++m.eatWalkSamples;
			}
		}
		return;
	}
	switch (u->activity)
	{
	case UnitState::ACT_RANDOM:
		++m.labour[M::IDLE];
		return;
	case UnitState::ACT_UPGRADING:
		if (u->destinationPurpose == HEAL)
			++m.labour[inside ? M::HEAL_INSIDE : M::HEAL_WALKING];
		else
			++m.labour[inside ? M::TRAIN_INSIDE : M::TRAIN_WALKING];
		return;
	case UnitState::ACT_FLAG:
		++m.labour[M::FLAG_WORK];
		return;
	case UnitState::ACT_FILLING:
		if (const auto *b = attached(world, *u))
		{
			M::LabourJob job = M::OTHER_JOB;
			if (type(world, *b).isBuildingSite)
				job = M::SITE_JOB;
			else if (!type(world, *b).semantics.production.enabledUnits.empty())
				job = M::SWARM_JOB;
			else if (type(world, *b).canFeedUnit)
				job = M::INN_JOB;
			M::LabourPhase phase = M::OTHER_PHASE;
			if (u->displacement == UnitState::DIS_GOING_TO_RESOURCE)
				phase = M::TO_RESOURCE;
			else if (u->displacement == UnitState::DIS_HARVESTING)
			{
				phase = M::HARVESTING;
				m.harvestDistance[job] += distanceTo(b);
				++m.harvestSamples[job];
			}
			else if (u->displacement == UnitState::DIS_GOING_TO_BUILDING)
				phase = M::TO_BUILDING;
			++m.filling[job][phase];
			return;
		}
		break;
	default:
		break;
	}
	++m.labour[M::OTHER_ACTIVITY];
}

bool ground(const AIWorldView &w, int x, int y, bool swim, Uint32 mask, bool free)
{
	const auto i = w.tileIndex(x, y);
	const auto &o = w.occupancyAt(i);
	const auto &t = w.terrainPropertiesAt(i);
	return !MapState::resourceBlocksGround(w.state(), i) && o.building == 0xffff &&
		   (!free || o.groundUnit == 0xffff) && (t.walkable || (swim && t.swimmable)) &&
		   !(w.areasAt(i).forbidden & mask);
}
bool air(const AIWorldView &w, int x, int y)
{
	const auto i = w.tileIndex(x, y);
	return w.terrainPropertiesAt(i).flyable && !MapState::resourceBlocksAir(w.state(), i) &&
		   w.occupancyAt(i).airUnit == 0xffff;
}
template <class F> bool border(const AIWorldView &w, const BuildingView &b, F f)
{
	const auto &t = type(w, b);
	for (int y = b.posY - 1; y <= b.posY + t.height; ++y)
		for (int x = b.posX - 1; x <= b.posX + t.width; ++x)
			if ((x < b.posX || x >= b.posX + t.width || y < b.posY || y >= b.posY + t.height) &&
				f(x, y))
				return true;
	return false;
}
void periodic(const AIWorldView &w, unsigned teamId, TeamStatsSample &sample)
{
	using M = GameplayMeasurements;
	auto &m = sample.measurements;
	const auto &team = w.teams[teamId];
	const auto enemies = w.components().statisticsInputs->enemies[teamId];
	m.trappedTick = m.defenceTick = sample.tick;
	for (auto &v : m.variants)
		for (auto &row : v.trapped)
			std::fill(std::begin(row), std::end(row), 0);
	for (auto &row : m.trappedUnits)
		row.clear();
	for (auto &row : m.trappedBuildings)
		for (auto &swim : row)
			std::fill(std::begin(swim), std::end(swim), 0);
	for (auto &row : m.lowHP)
		row.clear();
	for (auto &row : m.lowFood)
		row.clear();
	std::fill(std::begin(m.warriors), std::end(m.warriors), 0);
	std::fill(std::begin(m.warriorLevels), std::end(m.warriorLevels), 0);
	m.warriorsHurt = m.warriorsFlagged = m.warriorsInside = m.intruders = m.intruderLevels = 0;
	for (const auto &b : w.buildings)
		if (b.team == int(teamId) && !type(w, b).isVirtual &&
			b.buildingState != BuildingStateRecord::DEAD)
			sample.coverage.push_back({b.posX, b.posY, type(w, b).width, type(w, b).height});
	std::sort(
		sample.coverage.begin(), sample.coverage.end(), [](const auto &a, const auto &b)
		{ return std::tie(a.x, a.y, a.width, a.height) < std::tie(b.x, b.y, b.width, b.height); });
	const auto place = [&](int x, int y)
	{
		Uint32 nearby = 0;
		for (unsigned t = 0; t < w.teams.size(); ++t)
		{
			const auto &anchors =
				t == teamId ? sample.coverage : w.components().statisticsInputs->coverage[t];
			for (const auto &b : anchors)
			{
				// Distance to a wrapped interval, without visiting footprint tiles.
				const auto axis = [](int d, int size, int period)
				{ return d < size ? 0 : std::min(d - size + 1, period - d); };
				const bool near = std::max(axis(w.normalizeX(x - b.x), b.width, w.width),
										   axis(w.normalizeY(y - b.y), b.height, w.height)) <=
								  GROWTH_COVERAGE_RADII[M::PLACE_BAND];
				if (near)
				{
					nearby |= w.teams[t].mask;
					break;
				}
			}
		}
		return nearby & team.mask ? M::HOME : (nearby & enemies ? M::AWAY : M::FIELD);
	};
	for (const auto &u : w.units)
		if (!u.isDead)
		{
			if (has(u, UnitRuntimeTraits::Melee))
			{
				const auto p = place(u.posX, u.posY);
				const auto levels = Uint64(u.level[ATTACK_SPEED] + u.level[ATTACK_STRENGTH]);
				if (u.team == int(teamId))
				{
					++m.warriors[p];
					m.warriorLevels[p] += levels;
					m.warriorsHurt += u.medical == UnitState::MED_DAMAGED;
					m.warriorsFlagged += bool(attached(w, u)) &&
										 u.activity == UnitState::ACT_FLAG &&
										 u.jobPurpose == UnitJobPurpose::Defend;
					m.warriorsInside += u.displacement == UnitState::DIS_INSIDE;
				}
				else if ((enemies & w.teams[u.team].mask) && p == M::HOME)
				{
					++m.intruders;
					m.intruderLevels += levels;
				}
			}
			if (u.team != int(teamId))
				continue;
			for (int band = 0; band < 3; ++band)
			{
				const int percent = (band + 1) * 25;
				if (Sint64(u.hp) * 100 <= Sint64(u.performance[HP]) * percent)
					++m.lowHP[band][u.typeNum];
				if (Sint64(u.hungry) * 100 <=
					Sint64(w.unitTraits(u.typeNum).foodCapacity) * percent)
					++m.lowFood[band][u.typeNum];
			}
			bool structural = false, current = false;
			const auto *b = attached(w, u);
			if (u.displacement == UnitState::DIS_INSIDE && b)
			{
				if (u.performance[FLY])
				{
					current = true;
					const auto &t = type(w, *b);
					for (int y = b->posY; y < b->posY + t.height && current; ++y)
						for (int x = b->posX; x < b->posX + t.width; ++x)
							if (air(w, x, y))
							{
								current = false;
								break;
							}
				}
				else
				{
					const auto exit = [&](int x, int y, bool free)
					{
						return (u.performance[WALK] ||
								(u.performance[SWIM] &&
								 w.terrainPropertiesAt(w.tileIndex(x, y)).swimmable)) &&
							   ground(w, x, y, u.performance[SWIM] > 0, team.mask, free);
					};
					structural = !border(w, *b, [&](int x, int y) { return exit(x, y, false); });
					current = !border(w, *b, [&](int x, int y) { return exit(x, y, true); });
				}
			}
			else if (u.displacement != UnitState::DIS_INSIDE)
			{
				bool hard = false, free = false;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
						if (dx || dy)
						{
							const int x = u.posX + dx, y = u.posY + dy;
							if (u.performance[FLY])
							{
								hard = true;
								free |= air(w, x, y);
							}
							else if (u.performance[WALK] ||
									 (u.performance[SWIM] &&
									  w.terrainPropertiesAt(w.tileIndex(x, y)).swimmable))
							{
								hard |= ground(w, x, y, u.performance[SWIM] > 0, team.mask, false);
								free |= ground(w, x, y, u.performance[SWIM] > 0, team.mask, true);
							}
						}
				structural = !hard;
				current = !free;
			}
			m.trappedUnits[0][u.typeNum] += structural;
			m.trappedUnits[1][u.typeNum] += current;
		}
	for (const auto &b : w.buildings)
		if (b.team == int(teamId) && !type(w, b).isVirtual &&
			b.buildingState != BuildingStateRecord::DEAD)
		{
			for (int swim = 0; swim < 2; ++swim)
			{
				const bool hard = border(w, b, [&](int x, int y)
										 { return ground(w, x, y, swim, team.mask, false); });
				const bool free = border(w, b, [&](int x, int y)
										 { return ground(w, x, y, swim, team.mask, true); });
				m.variants[b.typeNum].trapped[0][swim] += !hard;
				m.variants[b.typeNum].trapped[1][swim] += !free;
				const int family = type(w, b).shortTypeNum;
				if (family >= 0 && family < IntBuildingType::NB_BUILDING)
				{
					m.trappedBuildings[0][swim][family] += !hard;
					m.trappedBuildings[1][swim][family] += !free;
				}
			}
		}
}
} // namespace

TeamStatsSample computeTeamStatsSample(const SimulationSnapshot::Handle &snapshot, unsigned teamId,
									   bool aggregate, std::span<const TeamSmoothedStat> window, bool includeCurrent)
{
	AIWorldView world(snapshot);
	const auto ownUnits=SimulationSnapshot::teamRecords(world.units,int(teamId));
	const auto ownBuildings=SimulationSnapshot::teamRecords(world.buildings,int(teamId));
	TeamStatsSample sample;
	sample.tick = snapshot.tick;
	sample.prestige = world.teams[teamId].prestige;
	sample.aggregate = aggregate;
	sample.periodic = (sample.tick & END_OF_GAME_STAT_INTERVAL_MASK) == 0;
	auto &m = sample.measurements;
	snapshot.statisticsInputs->events[teamId].applyTo(m);
	m.tick = sample.tick;
	m.configureUnits(world.unitTypeCount());
	m.variants.resize(world.catalog->size());
	for (int r = 0; r < MaterialSlotCount; ++r)
		m.stock[r] = std::max(0, world.teams[teamId].materials[r]);
	auto &smooth = sample.smoothing;
	smooth.configureUnits(world.unitTypeCount());
	for (const auto &u : ownUnits)
	{
		if (!u.isDead)
		{
			if (!u.widePrimaryCargo && u.carriedMaterial >= 0 &&
				u.carriedMaterial < MaterialSlotCount)
				++m.carried[u.carriedMaterial];
			if (has(u, UnitRuntimeTraits::ExtendedCargo) || u.widePrimaryCargo)
				if (const auto *cargo = world.extraCargo(u))
					for (const auto &p : *cargo)
						++m.carried[p.material];
			if (hungry(world, u))
			{
				++m.hungry;
				m.critical += u.hp < u.performance[HP];
			}
			if (u.displacement == UnitState::DIS_INSIDE)
			{
				m.feeding += u.destinationPurpose == FEED;
				m.healing += u.destinationPurpose == HEAL;
			}
		}
		observeLabour(world, m, &u);
		if (u.medical == UnitState::MED_FREE && u.activity == UnitState::ACT_RANDOM)
		{
			++smooth.isFree[u.typeNum];
			++smooth.totalFree;
		}
	}
	for (const auto &b : ownBuildings)
	{
		const auto &t = type(world, b);
		if (!t.isVirtual && b.buildingState != BuildingStateRecord::DEAD)
		{
			++m.variants[b.typeNum].count;
			const int level = longLevel(world, b), family = t.shortTypeNum;
			if (family >= 0 && family < IntBuildingType::NB_BUILDING && level >= 0 &&
				level < NB_BUILDING_LONG_LEVELS)
				++m.buildings[family][level];
			if (!t.useTeamMaterials)
				for (int r = 0; r < MaterialSlotCount; ++r)
					m.stock[r] += std::max(0, b.localMaterials[r]);
		}
		if (t.foodable || t.fillable || t.attractsRole(0))
		{
			const int need = b.desiredMaxUnitWorking - int(b.working.count);
			smooth.totalNeeded += need;
			smooth.totalNeededPerLevel[t.semantics.requiredWorkerLevel] += need;
		}
	}
	if (sample.periodic)
		periodic(world, teamId, sample);
	if (!aggregate)
		return sample;
	auto &stat = sample.stat;
	stat.configureUnits(world.unitTypeCount());
	stat.buildingCountByVariant.resize(world.catalog->size());
	auto &eligible = sample.eligible;
	eligible.resize(world.unitTypeCount());
	for (const auto &unit : ownUnits)
	{
		const auto *u = &unit;
		stat.totalUnit++;
		stat.numberUnitPerType[(int)u->typeNum]++;
		stat.totalHP = int(std::clamp<Sint64>(Sint64(stat.totalHP) + u->hp,
											  std::numeric_limits<int>::min(),
											  std::numeric_limits<int>::max()));
		const bool mobile = u->performance[FLY] > 0 || u->performance[WALK] > 0 ||
							u->performance[SWIM] > 0;
		const bool carrier = mobile && has(*u, UnitRuntimeTraits::Transport) &&
							 u->performance[BUILD] > 0 && u->performance[HARVEST] > 0 &&
							 (!has(*u, UnitRuntimeTraits::ExtendedCargo) ||
							  world.unitTraits(u->typeNum).cargoCapacity > 0);
		const bool builder = carrier && has(*u, UnitRuntimeTraits::Construct);
		const int attackStrength =
			has(*u, UnitRuntimeTraits::Melee) ? u->realAttackStrength : 0;
		const bool melee = has(*u, UnitRuntimeTraits::Melee) &&
						   u->performance[ATTACK_SPEED] > 0 && attackStrength > 0;
		const bool ranged = u->performance[MAGIC_ATTACK_AIR] > 0 ||
							u->performance[MAGIC_ATTACK_GROUND] > 0;
		const bool defender =
			mobile && (melee || (has(*u, UnitRuntimeTraits::GuardIdle) && ranged));
		stat.carriers += carrier;
		stat.builders += builder;
		stat.meleeUnits += melee;
		stat.rangedUnits += ranged;
		stat.scouts += mobile && has(*u, UnitRuntimeTraits::Explore);
		if (builder)
			++stat.workersByConstructionLevel[u->constructionLevel];
		const bool idle =
			u->medical == UnitState::MED_FREE && u->activity == UnitState::ACT_RANDOM;
		auto &counts = eligible[u->typeNum];
		counts[0] += carrier;
		counts[1] += defender;
		counts[2] += carrier && idle;
		counts[3] += defender && idle && world.unitTraits(u->typeNum).recruits(2);

		if (hungry(world, *u))
		{
			if (attached(world, *u) && u->insideTimeout < 0 &&
				type(world, *attached(world, *u)).canFeedUnit)
				stat.needNothing++;
			else if (u->hp < u->performance[HP])
			{
				stat.needFoodCritical++;
			}
			else
				stat.needFood++;

			if (u->activity != UnitState::ACT_UPGRADING)
			{
				stat.needFoodNoInns++;
			}
		}
		else if (u->medical == UnitState::MED_DAMAGED)
		{
			if (attached(world, *u) && u->insideTimeout < 0 &&
				type(world, *attached(world, *u)).canHealUnit)
				stat.needNothing++;
			else
			{
				stat.needHeal++;
			}
		}
		else
		{
			stat.needNothing++;
			if (u->activity == UnitState::ACT_RANDOM)
			{
				stat.isFree[(int)u->typeNum]++;
				stat.totalFree++;
			}
		}
		for (int j = 0; j < NB_ABILITY; j++)
		{
			if (u->performance[j])
			{
				stat.upgradeState[j][u->level[j]]++;
				stat.upgradeStatePerType[(int)u->typeNum][j][u->level[j]]++;
			}
		}
		if (has(*u, UnitRuntimeTraits::Melee))
			stat.totalAttackPower = int(std::min<Sint64>(
				std::numeric_limits<int>::max(),
				Sint64(stat.totalAttackPower) +
					Sint64(u->performance[ATTACK_SPEED]) * attackStrength));

		stat.happiness[u->fruitCount]++;
	}

	for (const auto &building : ownBuildings)
	{
		const auto *b = &building;
		++stat.buildingCountByVariant[b->typeNum];
		// Historical histograms remain for authored SGSL and older dataset readers.
		const int family = type(world, *b).shortTypeNum, level = longLevel(world, *b);
		if (family >= 0 && family < IntBuildingType::NB_BUILDING && level >= 0 &&
			level < NB_BUILDING_LONG_LEVELS)
		{
			++stat.numberBuildingPerType[family];
			++stat.numberBuildingPerTypePerLevel[family][level];
		}
		stat.totalHP = int(std::clamp<Sint64>(Sint64(stat.totalHP) + b->hp,
											  std::numeric_limits<int>::min(),
											  std::numeric_limits<int>::max()));
		// The scalar defense estimate uses the strongest configured melee target;
		// projectile resolution retains each recipient's own damage.
		if (type(world, *b).shootingRange > 0)
			stat.totalDefensePower = int(
				std::min<Sint64>(std::numeric_limits<int>::max(),
								 Sint64(stat.totalDefensePower) +
									 ((Sint64(type(world, *b).runtimeDefenseDamage) *
									   type(world, *b).shootRhythm) >>
									  SHOOTING_COOLDOWN_MAGNITUDE)));
		if ((!type(world, *b).isBuildingSite) && (!type(world, *b).isVirtual))
			stat.totalBuilding++;
	}

    if (!window.empty()) {
        if (window.size()!=32) throw std::logic_error("Invalid smoothing window");
        TeamStats::finishAggregate(stat, eligible, window, *world.components().catalogs->units,
            includeCurrent ? &sample.smoothing : nullptr);
        sample.finalized=true;
    }
	return sample;
}
