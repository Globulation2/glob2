// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include <sstream>
#include <iostream>
#include <cstdlib>

#include <FormatableString.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <Stream.h>
#include <BinaryStream.h>
#include <stdexcept>
#include <cstddef>
#include <algorithm>
#include <tuple>

#include "Game.h"
#include "GlobalContainer.h"
#include "Team.h"
#include "TeamStat.h"
#include "FileFormatVersions.h"
#include "Unit.h"
#include "Bullet.h"
#include "Map.h"


namespace
{
void statValue(GAGCore::OutputStream* stream, const char* name, const int& value)
{
    stream->writeSint32(value, name);
}

void statValue(GAGCore::InputStream* stream, const char* name, int& value)
{
    value = stream->readSint32(name);
}

void statValue(GAGCore::OutputStream *s, const char *n, const Uint32 &v)
{
	s->writeUint32(v, n);
}
void statValue(GAGCore::InputStream *s, const char *n, Uint32 &v)
{
	v = s->readUint32(n);
}
void statValue(GAGCore::OutputStream *s, const char *n, const Uint64 &v)
{
	s->writeEnterSection(n);
	s->writeUint32(static_cast<Uint32>(v), "low");
	s->writeUint32(static_cast<Uint32>(v >> 32), "high");
	s->writeLeaveSection();
}
void statValue(GAGCore::InputStream *s, const char *n, Uint64 &v)
{
	s->readEnterSection(n);
	Uint64 low = s->readUint32("low");
	v = low | (Uint64(s->readUint32("high")) << 32);
	s->readLeaveSection();
}

template<class Name>
void enterStatSection(GAGCore::OutputStream* stream, Name name) { stream->writeEnterSection(name); }

template<class Name>
void enterStatSection(GAGCore::InputStream* stream, Name name) { stream->readEnterSection(name); }
void leaveStatSection(GAGCore::OutputStream* stream) { stream->writeLeaveSection(); }
void leaveStatSection(GAGCore::InputStream* stream) { stream->readLeaveSection(); }

template<class Stream, class T, std::size_t N>
void statValue(Stream* stream, const char* name, T (&values)[N])
{
    enterStatSection(stream, name);
    for (unsigned i = 0; i < N; ++i)
    {
        enterStatSection(stream, i);
        statValue(stream, "value", values[i]);
        leaveStatSection(stream);
    }
    leaveStatSection(stream);
}

template <class Stream, class Stat> void measurementFields(Stream *stream, Stat &stat, bool extended = true)
{
	statValue(stream, "tick", stat.tick);
	statValue(stream, "births", stat.births);
	statValue(stream, "deaths", stat.deaths);
	statValue(stream, "conversionsIn", stat.conversionsIn);
	statValue(stream, "conversionsOut", stat.conversionsOut);
	statValue(stream, "harvested", stat.harvested);
	statValue(stream, "cleared", stat.cleared);
	statValue(stream, "delivered", stat.delivered);
	statValue(stream, "withdrawn", stat.withdrawn);
	statValue(stream, "transferredIn", stat.transferredIn);
	statValue(stream, "transferredOut", stat.transferredOut);
	statValue(stream, "consumed", stat.consumed);
	statValue(stream, "repairDelivered", stat.repairDelivered);
	statValue(stream, "meals", stat.meals);
	statValue(stream, "healingVisits", stat.healingVisits);
	statValue(stream, "hpRestored", stat.hpRestored);
	statValue(stream, "damageDealt", stat.damageDealt);
	statValue(stream, "damageReceived", stat.damageReceived);
	statValue(stream, "shots", stat.shots);
	statValue(stream, "impacts", stat.impacts);
	statValue(stream, "completed", stat.completed);
	statValue(stream, "removed", stat.removed);
	statValue(stream, "trainingVisits", stat.trainingVisits);
	statValue(stream, "abilityGains", stat.abilityGains);
	statValue(stream, "stock", stat.stock);
	statValue(stream, "carried", stat.carried);
	statValue(stream, "buildings", stat.buildings);
	statValue(stream, "hungry", stat.hungry);
	statValue(stream, "critical", stat.critical);
	statValue(stream, "feeding", stat.feeding);
	statValue(stream, "healing", stat.healing);
	if (extended)
	{
		statValue(stream, "trappedUnits", stat.trappedUnits);
		statValue(stream, "trappedBuildings", stat.trappedBuildings);
		statValue(stream, "lowHP", stat.lowHP);
		statValue(stream, "lowFood", stat.lowFood);
		statValue(stream, "trappedTick", stat.trappedTick);
		statValue(stream, "growthTiles", stat.growthTiles);
		statValue(stream, "growthAmount", stat.growthAmount);
		statValue(stream, "growthReduction", stat.growthReduction);
		statValue(stream, "growthGlobal", stat.growthGlobal);
	}
}

template<class Stream, class Stat>
void liveStatFields(Stream* stream, Stat& stat)
{
    statValue(stream, "totalUnit", stat.totalUnit);
    statValue(stream, "numberUnitPerType", stat.numberUnitPerType);
    statValue(stream, "totalFree", stat.totalFree);
    statValue(stream, "isFree", stat.isFree);
    statValue(stream, "totalNeeded", stat.totalNeeded);
    statValue(stream, "totalNeededPerLevel", stat.totalNeededPerLevel);
    statValue(stream, "totalBuilding", stat.totalBuilding);
    statValue(stream, "numberBuildingPerType", stat.numberBuildingPerType);
    statValue(stream, "numberBuildingPerTypePerLevel", stat.numberBuildingPerTypePerLevel);
    statValue(stream, "needFoodCritical", stat.needFoodCritical);
    statValue(stream, "needFoodNoInns", stat.needFoodNoInns);
    statValue(stream, "needFood", stat.needFood);
    statValue(stream, "needHeal", stat.needHeal);
    statValue(stream, "needNothing", stat.needNothing);
    statValue(stream, "upgradeState", stat.upgradeState);
    statValue(stream, "upgradeStatePerType", stat.upgradeStatePerType);
    statValue(stream, "totalFood", stat.totalFood);
    statValue(stream, "totalFoodCapacity", stat.totalFoodCapacity);
    statValue(stream, "totalUnitFoodable", stat.totalUnitFoodable);
    statValue(stream, "totalUnitFooded", stat.totalUnitFooded);
    statValue(stream, "totalHP", stat.totalHP);
    statValue(stream, "totalAttackPower", stat.totalAttackPower);
    statValue(stream, "totalDefensePower", stat.totalDefensePower);
    statValue(stream, "happiness", stat.happiness);
}

template<class Stream, class Stat>
void smoothedStatFields(Stream* stream, Stat& stat)
{
    statValue(stream, "totalFree", stat.totalFree);
    statValue(stream, "isFree", stat.isFree);
    statValue(stream, "totalNeeded", stat.totalNeeded);
    statValue(stream, "totalNeededPerLevel", stat.totalNeededPerLevel);
}
}


EndOfGameStat::EndOfGameStat(int units, int buildings, int prestige, int hp, int attack, int defense)
{
	value[TYPE_UNITS]=units;
	value[TYPE_BUILDINGS]=buildings;
	value[TYPE_PRESTIGE]=prestige;
	value[TYPE_HP]=hp;
	value[TYPE_ATTACK]=attack;
	value[TYPE_DEFENSE]=defense;
}

TeamStat::TeamStat()
{
	reset();
}


void TeamStat::reset()
{
	totalUnit=0;
	for(int i=0; i<NB_UNIT_TYPE; ++i)
	{
		numberUnitPerType[i]=0;
		isFree[i]=0;
	}
	totalFree=0;
	totalNeeded=0;
	for(int i=0; i<NB_UNIT_LEVELS; ++i)
		totalNeededPerLevel[i]=0;
	totalBuilding=0;
	for(int i=0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		numberBuildingPerType[i]=0;
		for(int j=0; j<NB_BUILDING_LONG_LEVELS; ++j)
			numberBuildingPerTypePerLevel[i][j]=0;
	}
	needFoodCritical=0;
	needFood=0;
	needFoodNoInns=0;
	needHeal=0;
	needNothing=0;
	for(int i=0; i<NB_ABILITY; ++i)
		for(int j=0; j<NB_UNIT_LEVELS; ++j)
		{
			upgradeState[i][j]=0;
		}
	for(int k=0; k<NB_UNIT_TYPE; ++k)
	{
		for(int i=0; i<NB_ABILITY; ++i)
			for(int j=0; j<NB_UNIT_LEVELS; ++j)
				upgradeStatePerType[k][i][j]=0;
	}
	totalFood=0;
	totalFoodCapacity=0;
	totalUnitFoodable=0;
	totalUnitFooded=0;

	totalHP=0;
	totalAttackPower=0;
	totalDefensePower=0;

	for(int i=0; i<HAPPINESS_COUNT+1; ++i)
		happiness[i]=0;
}



TeamSmoothedStat::TeamSmoothedStat()
{
	reset();
}


void TeamSmoothedStat::reset()
{
	totalFree=0;
	for(int i=0; i<NB_UNIT_TYPE; ++i)
	{
		isFree[i]=0;
	}
	totalNeeded=0;
	for(int i=0; i<NB_UNIT_LEVELS; ++i)
		totalNeededPerLevel[i]=0;
}



TeamStats::TeamStats()
{
	statsIndex=0;
	smoothedIndex=0;
	haveSetMapSize=false;
}

TeamStats::~TeamStats()
{
	
}

// The worker time-use and combat-death counters are development diagnostics.
// They are read by nothing in the simulation, but they are not free either: the
// death scan walks every team's buildings. Gate them on the same switch as
// their only consumer, and cache it, so a shipped game pays nothing.
static bool diagnosticsEnabled()
{
	static const bool enabled = getenv("GLOB2_TEAM_TIMELINE") != nullptr;
	return enabled;
}

// Snapshot of the defensive picture: where this team's warriors are, how
// trained and how hurt, and the hostile warriors inside its colony.
void TeamStats::printDefenceSample(Team *team) const
{
	Game *game = team->game;
	const int w = team->map->getW(), h = team->map->getH();
	std::vector<std::pair<int,int>> own, enemy;
	int hospitalSeats = 0, hospitalInside = 0, towers = 0;
	for (int t = 0; t < game->mapHeader.getNumberOfTeams(); ++t)
	{
		Team *other = game->teams[t];
		if (!other) continue;
		for (int i = 0; i < Building::MAX_COUNT; ++i)
		{
			Building *b = other->myBuildings[i];
			if (!b || !b->type || b->type->isVirtual) continue;
			(other == team ? own : enemy).push_back(std::make_pair(b->getMidX(), b->getMidY()));
			if (other == team && !b->type->isBuildingSite)
			{
				if (b->type->shortTypeNum == IntBuildingType::HEAL_BUILDING)
				{ hospitalSeats += b->maxUnitInside; hospitalInside += int(b->unitsInside.size()); }
				if (b->type->shortTypeNum == IntBuildingType::DEFENSE_BUILDING) ++towers;
			}
		}
	}
	auto nearest = [&](const std::vector<std::pair<int,int>> &list, int x, int y) {
		int best = 1 << 30;
		for (const auto &p : list)
		{
			int dx = abs(x - p.first) % w, dy = abs(y - p.second) % h;
			dx = std::min(dx, w - dx); dy = std::min(dy, h - dy);
			best = std::min(best, std::max(dx, dy));
		}
		return best;
	};
	int place[3] = {0, 0, 0}, levels[3] = {0, 0, 0}, hurt = 0, flagged = 0, inside = 0;
	for (int i = 0; i < Unit::MAX_COUNT; ++i)
	{
		Unit *u = team->myUnits[i];
		if (!u || u->typeNum != WARRIOR || u->isDead) continue;
		const int o = nearest(own, u->posX, u->posY), e = nearest(enemy, u->posX, u->posY);
		const int p = o <= 12 && o <= e ? 0 : (e <= 12 ? 1 : 2);
		++place[p];
		levels[p] += u->level[ATTACK_SPEED] + u->level[ATTACK_STRENGTH];
		if (u->medical == Unit::MED_DAMAGED) ++hurt;
		if (u->attachedBuilding && u->attachedBuilding->type->shortTypeNum == IntBuildingType::WAR_FLAG) ++flagged;
		if (u->displacement == Unit::DIS_INSIDE) ++inside;
	}
	int intruders = 0, intruderLevels = 0;
	for (int t = 0; t < game->mapHeader.getNumberOfTeams(); ++t)
	{
		Team *other = game->teams[t];
		if (!other || other == team || (team->allies & other->me)) continue;
		for (int i = 0; i < Unit::MAX_COUNT; ++i)
		{
			Unit *u = other->myUnits[i];
			if (!u || u->typeNum != WARRIOR || u->isDead) continue;
			if (nearest(own, u->posX, u->posY) <= 12)
			{ ++intruders; intruderLevels += u->level[ATTACK_SPEED] + u->level[ATTACK_STRENGTH]; }
		}
	}
	std::cout << "GLOB2_DEFENCE team=" << team->teamNumber << " tick=" << measurements.tick
		<< " home=" << place[0] << " away=" << place[1] << " field=" << place[2]
		<< " home_levels=" << levels[0] << " away_levels=" << levels[1] << " field_levels=" << levels[2]
		<< " hurt=" << hurt << " flagged=" << flagged << " inside=" << inside
		<< " intruders=" << intruders << " intruder_levels=" << intruderLevels
		<< " hospital_seats=" << hospitalSeats << " hospital_inside=" << hospitalInside
		<< " towers=" << towers << std::endl;
}

void TeamStats::recordCombatDeath(Unit *u)
{
	if (!diagnosticsEnabled() || !u || u->typeNum < 0 || u->typeNum > 2)
		return;
	Game *game = u->owner->game;
	const int w = u->owner->map->getW(), h = u->owner->map->getH();
	int own = 1 << 30, enemy = 1 << 30;
	for (int t = 0; t < game->mapHeader.getNumberOfTeams(); ++t)
	{
		Team *team = game->teams[t];
		if (!team) continue;
		for (int i = 0; i < Building::MAX_COUNT; ++i)
		{
			Building *b = team->myBuildings[i];
			if (!b || !b->type || b->type->isVirtual) continue;
			int dx = abs(u->posX - b->getMidX()) % w, dy = abs(u->posY - b->getMidY()) % h;
			dx = std::min(dx, w - dx); dy = std::min(dy, h - dy);
			const int d = std::max(dx, dy);
			if (team == u->owner) own = std::min(own, d); else enemy = std::min(enemy, d);
		}
	}
	const int place = own <= 12 && own <= enemy ? 0 : (enemy <= 12 ? 1 : 2);
	++combatDeathPlace[u->typeNum][place];
	int job = 0;
	if (u->attachedBuilding)
	{
		const int type = u->attachedBuilding->type->shortTypeNum;
		job = type == IntBuildingType::WAR_FLAG ? 1 : type == IntBuildingType::CLEARING_FLAG ? 2
			: type == IntBuildingType::EXPLORATION_FLAG ? 3 : 4;
	}
	++combatDeathJob[u->typeNum][job];
}

// Buckets: 0 idle; 1 eat_walk 2 eat_inside 3 eat_no_inn; 4 heal_walk 5 heal_inside
// 6 heal_no_hospital; 7 train_walk 8 train_inside; 9+4*class+phase for filling jobs
// (class: 0 swarm 1 inn 2 site 3 other; phase: 0 to_resource 1 harvesting
// 2 to_building 3 other); 25 flag; 26 other; 27 total.
void TeamStats::observeLabour(Unit *u)
{
	if (!diagnosticsEnabled() || !u || u->typeNum != WORKER || u->isDead)
		return;
	++labourTicks[27];
	auto wrapDistance = [&](int x, int y, Building *b) {
		const int w = u->owner->map->getW(), h = u->owner->map->getH();
		int dx = abs(x - b->getMidX()) % w, dy = abs(y - b->getMidY()) % h;
		dx = std::min(dx, w - dx); dy = std::min(dy, h - dy);
		return std::max(dx, dy);
	};
	const bool inside = u->displacement == Unit::DIS_INSIDE
		|| u->displacement == Unit::DIS_ENTERING_BUILDING
		|| u->displacement == Unit::DIS_EXITING_BUILDING;
	if (u->medical != Unit::MED_FREE)
	{
		const int base = u->medical == Unit::MED_HUNGRY ? 1 : 4;
		if (inside) ++labourTicks[base + 1];
		else if (u->targetBuilding)
		{
			++labourTicks[base];
			if (base == 1)
			{
				labourWalkToEatDistance += wrapDistance(u->posX, u->posY, u->targetBuilding);
				++labourWalkToEatSamples;
			}
		}
		else ++labourTicks[base + 2];
		return;
	}
	switch (u->activity)
	{
		case Unit::ACT_RANDOM: ++labourTicks[0]; return;
		case Unit::ACT_UPGRADING:
			if (u->destinationPurpose == HEAL) ++labourTicks[inside ? 5 : 4];
			else ++labourTicks[inside ? 8 : 7];
			return;
		case Unit::ACT_FLAG: ++labourTicks[25]; return;
		case Unit::ACT_FILLING:
		{
			Building *b = u->attachedBuilding;
			if (!b) { ++labourTicks[26]; return; }
			int cls = 3;
			if (b->type->isBuildingSite) cls = 2;
			else if (b->type->shortTypeNum == IntBuildingType::SWARM_BUILDING) cls = 0;
			else if (b->type->shortTypeNum == IntBuildingType::FOOD_BUILDING) cls = 1;
			int phase = 3;
			if (u->displacement == Unit::DIS_GOING_TO_RESOURCE) phase = 0;
			else if (u->displacement == Unit::DIS_HARVESTING)
			{
				phase = 1;
				labourHarvestDistance[cls] += wrapDistance(u->posX, u->posY, b);
				++labourHarvestSamples[cls];
			}
			else if (u->displacement == Unit::DIS_GOING_TO_BUILDING) phase = 2;
			++labourTicks[9 + 4 * cls + phase];
			return;
		}
		default: ++labourTicks[26];
	}
}

void TeamStats::step(Team *team, bool reloaded)
{
	PERF_SCOPE_TIME(Stats);
	if (!reloaded && needsMeasurementInitialization)
		initializeMeasurements(team->game->stepCounter);
	beginMeasurementSnapshot(team);
	// handle end of game stat step
	if (((team->game->stepCounter & END_OF_GAME_STAT_INTERVAL_MASK) == 0) && !reloaded)
	{
		endOfGameStats.push_back(EndOfGameStat(stats[statsIndex].totalUnit, stats[statsIndex].totalBuilding, team->prestige,
			stats[statsIndex].totalHP, stats[statsIndex].totalAttackPower, stats[statsIndex].totalDefensePower));

		// Optional rich per-team economy/food trace for AI debugging. Gated by
		// GLOB2_TEAM_TIMELINE (same flag as Engine::printTeamTimeline). Emitted
		// here so it captures the live food/worker state at each 512-tick sample
		// — data that the archived EndOfGameStat (6 scalars) cannot carry.
		if (getenv("GLOB2_TEAM_TIMELINE"))
		{
			const TeamStat &s = stats[statsIndex];
			std::cout << "GLOB2_ECON team=" << team->teamNumber
				<< " tick=" << team->game->stepCounter
				<< " workers=" << s.numberUnitPerType[WORKER]
				<< " warriors=" << s.numberUnitPerType[WARRIOR]
				<< " explorers=" << s.numberUnitPerType[EXPLORER]
				<< " food=" << s.totalFood << "/" << s.totalFoodCapacity
				<< " fooded=" << s.totalUnitFooded << "/" << s.totalUnitFoodable
				<< " foodCritical=" << s.needFoodCritical
				<< " needFood=" << s.needFood
				<< " swarm=" << s.numberBuildingPerType[IntBuildingType::SWARM_BUILDING]
				<< " inn=" << s.numberBuildingPerType[IntBuildingType::FOOD_BUILDING]
				<< " school=" << s.numberBuildingPerType[IntBuildingType::SCIENCE_BUILDING]
				<< " barracks=" << s.numberBuildingPerType[IntBuildingType::ATTACK_BUILDING]
				// Hospitals were the one building type this trace omitted, which
				// is the one whose whole job is surviving damage. Comparing two
				// AIs' ability to absorb losses was therefore impossible from the
				// trace alone: the gap between their building counts sat exactly
				// where the unmeasured type was.
				<< " hospital=" << s.numberBuildingPerType[IntBuildingType::HEAL_BUILDING]
				<< " racetrack=" << s.numberBuildingPerType[IntBuildingType::WALKSPEED_BUILDING]
				<< " pool=" << s.numberBuildingPerType[IntBuildingType::SWIMSPEED_BUILDING]
				<< " tower=" << s.numberBuildingPerType[IntBuildingType::DEFENSE_BUILDING]
				<< std::endl;
		}
	}
	
	// handle in game stat step
	TeamSmoothedStat &smoothedStat=smoothedStats[smoothedIndex];
	smoothedStat.reset();
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=team->myUnits[i];
		observeMeasurementUnit(u);
		if (!reloaded)
			observeLabour(u);
		if ((u)&&(u->medical==Unit::MED_FREE)&&(u->activity==Unit::ACT_RANDOM))
		{
			smoothedStat.isFree[(int)u->typeNum]++;
			smoothedStat.totalFree++;
		}
	}
	
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b = team->myBuildings[i];
		if (b)
		{
			observeMeasurementBuilding(b);
			if(b->type->foodable || b->type->fillable || b->type->zonable[WORKER])
            {
		        smoothedStat.totalNeeded+=b->desiredMaxUnitWorking-(int)b->unitsWorking.size();
		        smoothedStat.totalNeededPerLevel[b->type->level]+=b->desiredMaxUnitWorking-(int)b->unitsWorking.size();
            }
        }
    }

	if (!reloaded && !needsMeasurementInitialization &&
		(measurements.tick & END_OF_GAME_STAT_INTERVAL_MASK) == 0 &&
		(measurementHistory.empty() || measurementHistory.back().tick != measurements.tick))
	{
		sampleTraps(team);
		measurementHistory.push_back(measurements);
		AITelemetry::capture(team, true, getenv("GLOB2_TEAM_TIMELINE") != nullptr);
		if (getenv("GLOB2_TEAM_TIMELINE"))
		{
			printMeasurements(team->teamNumber);
			printDefenceSample(team);
			std::cout << "GLOB2_LABOUR team=" << team->teamNumber << " tick=" << measurements.tick;
			for (int i = 0; i < LABOUR_BUCKETS; ++i)
				std::cout << " b" << i << "=" << labourTicks[i];
			for (int i = 0; i < LABOUR_CLASSES; ++i)
				std::cout << " hd" << i << "=" << labourHarvestDistance[i] << " hn" << i << "=" << labourHarvestSamples[i];
			std::cout << " ed=" << labourWalkToEatDistance << " en=" << labourWalkToEatSamples;
			for (int t = 0; t < 3; ++t)
			{
				for (int i = 0; i < 3; ++i) std::cout << " cdp" << t << "_" << i << "=" << combatDeathPlace[t][i];
				for (int i = 0; i < 5; ++i) std::cout << " cdj" << t << "_" << i << "=" << combatDeathJob[t][i];
			}
			std::cout << std::endl;
		}
	}
	smoothedIndex++;
	smoothedIndex%=STATS_SMOOTH_SIZE;
	if (smoothedIndex)
		return;
	
	TeamSmoothedStat maxStat;
	for (int i=0; i<STATS_SMOOTH_SIZE; i++)
	{
		TeamSmoothedStat &smoothedStat=smoothedStats[i];

		if (smoothedStat.totalFree>maxStat.totalFree)
			maxStat.totalFree=smoothedStat.totalFree;
		for (int j=0; j<NB_UNIT_TYPE; j++)
			if (smoothedStat.isFree[j]>maxStat.isFree[j])
				maxStat.isFree[j]=smoothedStat.isFree[j];
		if (smoothedStat.totalNeeded>maxStat.totalNeeded)
		{
			maxStat.totalNeeded=smoothedStat.totalNeeded;
		}
		for(int k=0; k<NB_UNIT_LEVELS; ++k)
		{
			if (smoothedStat.totalNeededPerLevel[k]>maxStat.totalNeededPerLevel[k])
				maxStat.totalNeededPerLevel[k]=smoothedStat.totalNeededPerLevel[k];
		}
	}

	// We change current stats:
	statsIndex++;
	statsIndex%=STATS_SIZE;
	TeamStat &stat=stats[statsIndex];

	stat.reset();

	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=team->myUnits[i];
		if (u)
		{
			stat.totalUnit++;
			stat.numberUnitPerType[(int)u->typeNum]++;
			stat.totalHP+=u->hp;

			if (u->isUnitHungry())
			{
				if (u->attachedBuilding && u->insideTimeout<0 && u->attachedBuilding->type->canFeedUnit)
					stat.needNothing++;
				else if (u->hp<u->performance[HP])
				{
					stat.needFoodCritical++;
				}
				else
					stat.needFood++;
					
				if(u->activity != Unit::ACT_UPGRADING)
				{
					stat.needFoodNoInns++;
				}
			}
			else if (u->medical==Unit::MED_DAMAGED)
			{
				if (u->attachedBuilding && u->insideTimeout<0 && u->attachedBuilding->type->canHealUnit)
					stat.needNothing++;
				else
				{
					stat.needHeal++;
				}
			}
			else
			{
				stat.needNothing++;
				if (u->activity==Unit::ACT_RANDOM)
				{
					stat.isFree[(int)u->typeNum]++;
					stat.totalFree++;
				}
			}
			for (int j=0; j<NB_ABILITY; j++)
			{
				if (u->performance[j])
				{
					stat.upgradeState[j][u->level[j]]++;
					stat.upgradeStatePerType[(int)u->typeNum][j][u->level[j]]++;
				}
			}
			if (u->typeNum==WARRIOR)
				stat.totalAttackPower+=u->performance[ATTACK_SPEED]*u->getRealAttackStrength();
			
			stat.happiness[u->fruitCount]++;
		}
	}

	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b = team->myBuildings[i];
		if (b)
		{
			stat.numberBuildingPerType[b->type->shortTypeNum]++;
			int longLevel=b->getLongLevel();
			assert(longLevel>=0);
			assert(longLevel<=MAX_BUILDING_LONG_LEVEL);
			stat.numberBuildingPerTypePerLevel[b->type->shortTypeNum][longLevel]++;
			stat.totalHP += b->hp;
			stat.totalDefensePower += (b->type->shootDamage*b->type->shootRhythm) >> SHOOTING_COOLDOWN_MAGNITUDE;
			if ((!b->type->isBuildingSite) && (!b->type->isVirtual))
				stat.totalBuilding++;
		}
	}
	
	// We override unsmoothed stats:
	stat.totalFree=maxStat.totalFree;
	for (int j=0; j<NB_UNIT_TYPE; j++)
		stat.isFree[j]=maxStat.isFree[j];
	stat.totalNeeded=maxStat.totalNeeded;
	for(int k=0; k<NB_UNIT_LEVELS; ++k)
		stat.totalNeededPerLevel[k]=maxStat.totalNeededPerLevel[k];
}

void TeamStats::drawText(int posx, int posy)
{
	// local variable to speed up access
	GraphicContext *gfx=globalContainer->gfx;
	Font *font=globalContainer->littleFont;
	StringTable *strings=Toolkit::getStringTable();
	int textStartPosX=posx+4;
	int textStartPosY=posy;
	
	TeamStat &newStats=stats[statsIndex];
	
	// general
	textStartPosY -= 5;
	gfx->drawString(textStartPosX, textStartPosY+15, font, FormattableString("%0 %1").arg(newStats.totalUnit).arg(strings->getString("[Units]")).c_str());
	if (newStats.totalUnit)
	{
		// worker
		int free=newStats.isFree[WORKER]-newStats.totalNeeded;
		int seeking=newStats.totalNeeded;
		if (free<0)
		{
			free=0;
			seeking=newStats.isFree[WORKER];
		}
		gfx->drawString(textStartPosX, textStartPosY+30, font, FormattableString("%0 %1 (%2 %)").arg(newStats.numberUnitPerType[WORKER]).arg(strings->getString("[workers]")).arg(((float)newStats.numberUnitPerType[WORKER])*100.0f/((float)newStats.totalUnit), 0, 0).c_str());
		gfx->drawString(textStartPosX+5, textStartPosY+42, font, FormattableString("%0 %1 %2").arg(strings->getString("[of which]")).arg(free).arg(strings->getString("[free]")).c_str());
		gfx->drawString(textStartPosX+5, textStartPosY+54, font, FormattableString("%0 %1 %2").arg(strings->getString("[and]")).arg(seeking).arg(strings->getString("[seeking a job]")).c_str());

		// explorer
		gfx->drawString(textStartPosX, textStartPosY+69, font, FormattableString("%0 %1 (%2 %)").arg(newStats.numberUnitPerType[EXPLORER]).arg(strings->getString("[explorers]")).arg(((float)newStats.numberUnitPerType[EXPLORER])*100.0f/((float)newStats.totalUnit), 0, 0).c_str());
		gfx->drawString(textStartPosX+5, textStartPosY+81, font, FormattableString("%0 %1 %2").arg(strings->getString("[of which]")).arg(newStats.isFree[EXPLORER]).arg(strings->getString("[free]")).c_str());
		// warrior
		gfx->drawString(textStartPosX, textStartPosY+96, font, FormattableString("%0 %1 (%2 %)").arg(newStats.numberUnitPerType[WARRIOR]).arg(strings->getString("[warriors]")).arg(((float)newStats.numberUnitPerType[WARRIOR])*100.0f/((float)newStats.totalUnit), 0, 0).c_str());
		gfx->drawString(textStartPosX+5, textStartPosY+108, font, FormattableString("%0 %1 %2").arg(strings->getString("[of which]")).arg(newStats.isFree[WARRIOR]).arg(strings->getString("[free]")).c_str());

		// living state
		gfx->drawString(textStartPosX, textStartPosY+123, font, FormattableString("%0 %1 (%2 %)").arg(newStats.needNothing).arg(strings->getString("[are ok]")).arg(((float)newStats.needNothing)*100.0f/((float)newStats.totalUnit), 0, 0).c_str());
		gfx->drawString(textStartPosX, textStartPosY+135, font, FormattableString("%0 %1 (%2 %)").arg(newStats.needFood).arg(strings->getString("[are hungry]")).arg(((float)newStats.needFood)*100.0f/((float)newStats.totalUnit), 0, 0).c_str());
		gfx->drawString(textStartPosX, textStartPosY+147, font, FormattableString("%0 %1 (%2 %)").arg(newStats.needFoodCritical).arg(strings->getString("[are dying hungry]")).arg(((float)newStats.needFoodCritical)*100.0f/((float)newStats.totalUnit), 0, 0).c_str());
		gfx->drawString(textStartPosX, textStartPosY+159, font, FormattableString("%0 %1 (%2 %)").arg(newStats.needHeal).arg(strings->getString("[are wonded]")).arg(((float)newStats.needHeal)*100.0f/((float)newStats.totalUnit), 0, 0).c_str());

		// upgrade state
		gfx->drawString(textStartPosX,textStartPosY+174, globalContainer->littleFont, FormattableString("%0 %1/%2/%3/%4").arg(strings->getString("[Walk]")).arg(newStats.upgradeState[WALK][0]).arg(newStats.upgradeState[WALK][1]).arg(newStats.upgradeState[WALK][2]).arg(newStats.upgradeState[WALK][3]).c_str());
		gfx->drawString(textStartPosX, textStartPosY+186, globalContainer->littleFont, FormattableString("%0 %1/%2/%3").arg(strings->getString("[Swim]")).arg(newStats.upgradeState[SWIM][1]).arg(newStats.upgradeState[SWIM][2]).arg(newStats.upgradeState[SWIM][3]).c_str());
		gfx->drawString(textStartPosX, textStartPosY+198, globalContainer->littleFont, FormattableString("%0 %1/%2/%3/%4").arg(strings->getString("[Build]")).arg(newStats.upgradeState[BUILD][0]).arg(newStats.upgradeState[BUILD][1]).arg(newStats.upgradeState[BUILD][2]).arg(newStats.upgradeState[BUILD][3]).c_str());
		gfx->drawString(textStartPosX, textStartPosY+210, globalContainer->littleFont, FormattableString("%0 %1/%2/%3/%4").arg(strings->getString("[Harvest]")).arg(newStats.upgradeState[HARVEST][0]).arg(newStats.upgradeState[HARVEST][1]).arg(newStats.upgradeState[HARVEST][2]).arg(newStats.upgradeState[HARVEST][3]).c_str());
		gfx->drawString(textStartPosX, textStartPosY+222, globalContainer->littleFont, FormattableString("%0 %1/%2/%3/%4").arg(strings->getString("[At. speed]")).arg(newStats.upgradeState[ATTACK_SPEED][0]).arg(newStats.upgradeState[ATTACK_SPEED][1]).arg(newStats.upgradeState[ATTACK_SPEED][2]).arg(newStats.upgradeState[ATTACK_SPEED][3]).c_str());
		gfx->drawString(textStartPosX, textStartPosY+234, globalContainer->littleFont, FormattableString("%0 %1/%2/%3/%4").arg(strings->getString("[At. strength]")).arg(newStats.upgradeState[ATTACK_STRENGTH][0]).arg(newStats.upgradeState[ATTACK_STRENGTH][1]).arg(newStats.upgradeState[ATTACK_STRENGTH][2]).arg(newStats.upgradeState[ATTACK_STRENGTH][3]).c_str());
	
		// jobs
		gfx->drawString(textStartPosX, textStartPosY+249, globalContainer->littleFont, FormattableString("%0 1 %1: %2").arg(strings->getString("[level]")).arg(strings->getString("[jobs]")).arg(newStats.totalNeededPerLevel[0]).c_str());
		gfx->drawString(textStartPosX, textStartPosY+261, globalContainer->littleFont, FormattableString("%0 2 %1: %2").arg(strings->getString("[level]")).arg(strings->getString("[jobs]")).arg(newStats.totalNeededPerLevel[1]).c_str());
		gfx->drawString(textStartPosX, textStartPosY+273, globalContainer->littleFont, FormattableString("%0 3 %1: %2").arg(strings->getString("[level]")).arg(strings->getString("[jobs]")).arg(newStats.totalNeededPerLevel[2]).c_str());
	
		// happyness
		std::stringstream happyness;
		happyness << strings->getString("[Happyness]") << " " << newStats.happiness[0];
		for (int i=1; i<=HAPPINESS_COUNT; i++)
			happyness << '/' << newStats.happiness[i];
		gfx->drawString(textStartPosX, textStartPosY+288, globalContainer->littleFont, happyness.str().c_str());
	}
}

void TeamStats::drawStat(int posx, int posy)
{
	assert(STATS_SIZE==128);// We have graphical constraints
	
	// local variable to speed up access
	GraphicContext *gfx=globalContainer->gfx;
	Font *font=globalContainer->littleFont;
	StringTable *strings=Toolkit::getStringTable();
	int textStartPos=posx+4;
	int startPoxY=posy;
	
	int maxWorker=0;
	for (int i=0; i<STATS_SIZE; i++)
		if (stats[i].numberUnitPerType[WORKER]>maxWorker)
			maxWorker=stats[i].numberUnitPerType[WORKER];

	if (maxWorker==0)
		return;

	// captions
	{
		startPoxY -= 10;
		
		int dec=0;
		std::string Total=strings->getString("[Total]");
		std::string free=strings->getString("[free]");
		std::string seeking=strings->getString("[seeking]");
		std::string slash="/";
		int sLen=font->getStringWidth(slash);

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 34, 66, 163));
		gfx->drawString(textStartPos, startPoxY+20, font, Total);
		font->popStyle();

		dec+=font->getStringWidth(Total);
		gfx->drawString(textStartPos+dec, startPoxY+20, font, "/");
		dec+=sLen;

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 22, 229, 40));
		gfx->drawString(textStartPos+dec, startPoxY+20, font, free);
		font->popStyle();

		dec+=font->getStringWidth(free);
		gfx->drawString(textStartPos+dec, startPoxY+20, font, "/");
		dec+=sLen;

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 150, 50, 50));
		gfx->drawString(textStartPos+dec, startPoxY+20, font, seeking);
		font->popStyle();

		dec=0;
		std::string Free=strings->getString("[Free]");
		std::string hungry=strings->getString("[hungry]");
		std::string starving=strings->getString("[starving]");
		std::string wounded=strings->getString("[wounded]");

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 22, 229, 40));
		gfx->drawString(textStartPos, startPoxY+104, font, Free);
		font->popStyle();

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 224, 210, 17));
		gfx->drawString(textStartPos+64, startPoxY+104, font, hungry);
		font->popStyle();

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 249, 167, 14));
		gfx->drawString(textStartPos, startPoxY+104+12, font, starving);
		font->popStyle();

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 250, 25, 25));
		gfx->drawString(textStartPos+64, startPoxY+104+12, font, wounded);
		font->popStyle();
	}

	// graph
	for (int i=0; i<STATS_SIZE; i++)
	{
		int index=(statsIndex+i+1)&(STATS_SIZE - 1);

		int free=stats[index].isFree[WORKER]-stats[index].totalNeeded;
		int seeking=stats[index].totalNeeded;
		if (free<0)
		{
			free=0;
			seeking=stats[index].isFree[WORKER];
		}
		
		int nbFree=(free*64)/maxWorker;
		int nbSeeking=(seeking*64)/maxWorker;
		int nbTotal=(stats[index].numberUnitPerType[WORKER]*64)/maxWorker;
		
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 36 +64-nbTotal, nbTotal-nbFree-nbSeeking, 34, 66, 163);
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 36 +64-nbFree-nbSeeking, nbFree, 22, 229, 40);
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 36 +64-nbSeeking, nbSeeking, 150, 50, 50);

		int nbOk, nbNeedFood, nbNeedFoodCritical, nbNeedHeal;
		if (stats[index].totalUnit)
		{
			// to avoid some round-off errors
			if (stats[index].needNothing>0)
			{
				nbNeedHeal=(stats[index].needHeal*64)/stats[index].totalUnit;
				nbNeedFood=(stats[index].needFood*64)/stats[index].totalUnit;
				nbNeedFoodCritical=(stats[index].needFoodCritical*64)/stats[index].totalUnit;
				nbOk=64-(nbNeedHeal+nbNeedFood+nbNeedFoodCritical);
			}
			else if (stats[index].needFood>0)
			{
				nbNeedHeal=(stats[index].needHeal*64)/stats[index].totalUnit;
				nbNeedFood=(stats[index].needFood*64)/stats[index].totalUnit;
				nbNeedFoodCritical=(stats[index].needFoodCritical*64)/stats[index].totalUnit;
				nbNeedFood=64-(nbNeedHeal+nbNeedFoodCritical);
				nbOk=0;
			}
			else if (stats[index].needFoodCritical>0)
			{
				nbNeedHeal=(stats[index].needHeal*64)/stats[index].totalUnit;
				nbNeedFood=0;
				nbNeedFoodCritical=64-nbNeedHeal;
				nbOk=0;
			}
			else
			{
				nbOk=nbNeedFood=nbNeedFoodCritical=0;
				nbNeedHeal=64;
			}
		}
		else
		{
			nbOk=nbNeedFood=nbNeedHeal=nbNeedFoodCritical=0;
		}
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 120+12  +64-nbNeedHeal-nbNeedFoodCritical-nbNeedFood-nbOk, nbOk, 22, 229, 40);
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 120+12 +64-nbNeedHeal-nbNeedFoodCritical-nbNeedFood, nbNeedFood, 224, 210, 17);
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 120+12 +64-nbNeedHeal-nbNeedFoodCritical, nbNeedFoodCritical, 249, 167, 14);
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 120+12 +64-nbNeedHeal, nbNeedHeal, 250, 25, 25);
	}
}

int TeamStats::getFreeUnits(int type)
{
	return (stats[statsIndex].isFree[type]);
}

int TeamStats::getTotalUnits(int type)
{
	return (stats[statsIndex].numberUnitPerType[type]);
}

int TeamStats::getWorkersNeeded()
{
	return (stats[statsIndex].totalNeeded);
}

int TeamStats::getWorkersBalance()
{
	return (stats[statsIndex].isFree[WORKER]-stats[statsIndex].totalNeeded);
}

int TeamStats::getWorkersLevel(int level)
{
	return (stats[statsIndex].upgradeState[BUILD][level]);
}

int TeamStats::getStarvingUnits()
{
	return (stats[statsIndex].needFoodCritical);
}

bool TeamStats::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	stream->readEnterSection("TeamStats");
	Uint32 size=0;
	size=stream->readUint32("size");

	bool stop=false;
	
	for (unsigned int i=0; i<size; i++)
	{
		stream->readEnterSection(i);
		Sint32 units = stream->readSint32("EndOfGameStat::TYPE_UNITS");
		Sint32 buildings = stream->readSint32("EndOfGameStat::TYPE_BUILDINGS");
		Sint32 prestige = stream->readSint32("EndOfGameStat::TYPE_PRESTIGE");
		Sint32 hp = 0;
		Sint32 attack = 0;
		Sint32 defense = 0;
		hp = stream->readSint32("EndOfGameStat::TYPE_HP");
		attack = stream->readSint32("EndOfGameStat::TYPE_ATTACK");
		defense = stream->readSint32("EndOfGameStat::TYPE_DEFENSE");
		if(!stop)
			endOfGameStats.push_back(EndOfGameStat(units, buildings, prestige, hp, attack, defense));
		stream->readLeaveSection();
	}
    if (versionMinor >= FILE_FORMAT_VERSION_LIVE_TEAM_STATS)
    {
        GAGCore::BinaryInputStream::CheckedReads checkedReads(stream);
        statsIndex = stream->readSint32("statsIndex");
        smoothedIndex = stream->readSint32("smoothedIndex");
        if (statsIndex < 0 || statsIndex >= STATS_SIZE ||
            smoothedIndex < 0 || smoothedIndex >= STATS_SMOOTH_SIZE)
        {
            stream->readLeaveSection();
            throw std::runtime_error("Invalid team statistics sampling index");
        }
        stream->readEnterSection("liveStats");
        for (unsigned i = 0; i < STATS_SIZE; ++i)
        {
            stream->readEnterSection(i);
            liveStatFields(stream, stats[i]);
            stream->readLeaveSection();
        }
        stream->readLeaveSection();
        stream->readEnterSection("smoothedStats");
        for (unsigned i = 0; i < STATS_SMOOTH_SIZE; ++i)
        {
            stream->readEnterSection(i);
            smoothedStatFields(stream, smoothedStats[i]);
            stream->readLeaveSection();
        }
        stream->readLeaveSection();
    }

	if (versionMinor >= FILE_FORMAT_VERSION_GAMEPLAY_STATS)
	{
		GAGCore::BinaryInputStream::CheckedReads checked(stream);
		coverageStartTick = stream->readUint32("coverageStartTick");
		measurementFields(stream, measurements, versionMinor >= FILE_FORMAT_VERSION_EXTENDED_GAMEPLAY_STATS);
		const Uint32 count = stream->readUint32("measurementCount");
		if (measurements.tick < coverageStartTick || count > Uint64(measurements.tick) / 512 + 1)
			throw std::runtime_error("Invalid gameplay statistics coverage");
		measurementHistory.clear();
		for (Uint32 i = 0; i < count; ++i)
		{
			stream->readEnterSection(i);
			GameplayMeasurements sample;
			measurementFields(stream, sample, versionMinor >= FILE_FORMAT_VERSION_EXTENDED_GAMEPLAY_STATS);
			stream->readLeaveSection();
			if (sample.tick < coverageStartTick || sample.tick > measurements.tick ||
				(sample.tick & 511) ||
				(!measurementHistory.empty() && sample.tick <= measurementHistory.back().tick))
				throw std::runtime_error("Invalid gameplay statistics timestamp");
			measurementHistory.push_back(sample);
		}
		needsMeasurementInitialization = false;
		extendedCoverageStartTick = versionMinor >= FILE_FORMAT_VERSION_EXTENDED_GAMEPLAY_STATS
			? stream->readUint32("extendedCoverageStartTick") : measurements.tick;
		if (extendedCoverageStartTick < coverageStartTick || extendedCoverageStartTick > measurements.tick)
			throw std::runtime_error("Invalid extended gameplay coverage");
		if (versionMinor >= FILE_FORMAT_VERSION_EXTENDED_GAMEPLAY_STATS)
		{
			coverageBuildingTick = stream->readUint32("coverageBuildingTick");
			coverageBuildingGeneration = stream->readUint32("coverageBuildingGeneration");
			const Uint32 buildings = stream->readUint32("coverageBuildingCount");
			if (buildings > Building::MAX_COUNT || coverageBuildingTick > measurements.tick)
				throw std::runtime_error("Invalid gameplay coverage anchors");
			coverageBuildings.clear();
			coverageBuildings.reserve(buildings);
			for (Uint32 i = 0; i < buildings; ++i)
			{
				CoverageBuilding b{stream->readSint32("coverageX"),stream->readSint32("coverageY"),
					stream->readSint32("coverageW"),stream->readSint32("coverageH")};
				if (b.width <= 0 || b.height <= 0 || b.width > 32 || b.height > 32 ||
					b.x < -(1 << 20) || b.x > (1 << 20) ||
					b.y < -(1 << 20) || b.y > (1 << 20))
					throw std::runtime_error("Invalid gameplay coverage footprint");
				coverageBuildings.push_back(b);
			}
			std::sort(coverageBuildings.begin(), coverageBuildings.end(), [](const CoverageBuilding &a, const CoverageBuilding &b) {
				return std::tie(a.x,a.y,a.width,a.height) < std::tie(b.x,b.y,b.width,b.height);
			});
		}
		else
		{
			coverageBuildings.clear();
			coverageBuildingTick = coverageBuildingGeneration = 0;
		}
	}
	else
	{
		needsMeasurementInitialization = true;
		extendedCoverageStartTick = measurements.tick;
	}

	if (versionMinor >= FILE_FORMAT_VERSION_AI_TELEMETRY)
		AITelemetry::load(stream, aiTelemetry);
	else
		aiTelemetry.clear();
	stream->readLeaveSection();
	return true;
}

void TeamStats::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("TeamStats");
	stream->writeUint32(endOfGameStats.size(), "size");
	for (unsigned int i=0; i<endOfGameStats.size(); i++)
	{
		stream->writeEnterSection(i);
		stream->writeSint32(endOfGameStats[i].value[EndOfGameStat::TYPE_UNITS], "EndOfGameStat::TYPE_UNITS");
		stream->writeSint32(endOfGameStats[i].value[EndOfGameStat::TYPE_BUILDINGS], "EndOfGameStat::TYPE_BUILDINGS");
		stream->writeSint32(endOfGameStats[i].value[EndOfGameStat::TYPE_PRESTIGE], "EndOfGameStat::TYPE_PRESTIGE");
		stream->writeSint32(endOfGameStats[i].value[EndOfGameStat::TYPE_HP], "EndOfGameStat::TYPE_HP");
		stream->writeSint32(endOfGameStats[i].value[EndOfGameStat::TYPE_ATTACK], "EndOfGameStat::TYPE_ATTACK");
		stream->writeSint32(endOfGameStats[i].value[EndOfGameStat::TYPE_DEFENSE], "EndOfGameStat::TYPE_DEFENSE");
		stream->writeLeaveSection();
	}
    stream->writeSint32(statsIndex, "statsIndex");
    stream->writeSint32(smoothedIndex, "smoothedIndex");
    stream->writeEnterSection("liveStats");
    for (unsigned i = 0; i < STATS_SIZE; ++i)
    {
        stream->writeEnterSection(i);
        liveStatFields(stream, stats[i]);
        stream->writeLeaveSection();
    }
    stream->writeLeaveSection();
    stream->writeEnterSection("smoothedStats");
    for (unsigned i = 0; i < STATS_SMOOTH_SIZE; ++i)
    {
        stream->writeEnterSection(i);
        smoothedStatFields(stream, smoothedStats[i]);
        stream->writeLeaveSection();
    }
    stream->writeLeaveSection();

	stream->writeUint32(coverageStartTick, "coverageStartTick");
	measurementFields(stream, measurements);
	stream->writeUint32(measurementHistory.size(), "measurementCount");
	for (unsigned i = 0; i < measurementHistory.size(); ++i)
	{
		stream->writeEnterSection(i);
		measurementFields(stream, measurementHistory[i]);
		stream->writeLeaveSection();
	}
	stream->writeUint32(extendedCoverageStartTick, "extendedCoverageStartTick");
	stream->writeUint32(coverageBuildingTick, "coverageBuildingTick");
	stream->writeUint32(coverageBuildingGeneration, "coverageBuildingGeneration");
	stream->writeUint32(coverageBuildings.size(), "coverageBuildingCount");
	for (const auto &b : coverageBuildings)
	{
		stream->writeSint32(b.x, "coverageX");
		stream->writeSint32(b.y, "coverageY");
		stream->writeSint32(b.width, "coverageW");
		stream->writeSint32(b.height, "coverageH");
	}

	AITelemetry::save(stream, aiTelemetry);
	stream->writeLeaveSection();
}

void TeamStats::initializeMeasurements(Uint32 tick)
{
	measurements = GameplayMeasurements{};
	measurements.tick = coverageStartTick = tick;
	extendedCoverageStartTick = tick;
	coverageBuildingTick = 0;
	coverageBuildingGeneration = 0;
	coverageBuildings.clear();
	measurementHistory.clear();
	needsMeasurementInitialization = false;
}

void TeamStats::recordDamage(Team *source, Team *target, int kind, int targetKind, int hp,
							 int damage)
{
	const Uint64 removed = std::max(0, std::min(hp, damage));
	target->stats.measurements.damageReceived[kind][targetKind] += removed;
	if (source)
	{
		source->stats.measurements.damageDealt[kind][targetKind] += removed;
		if (removed)
			++source->stats.measurements.impacts[kind][targetKind];
	}
}

namespace
{
template <class T> void printMeasurement(const std::string &name, const T &value)
{
	std::cout << ' ' << name << '=' << value;
}
template <class T, size_t N> void printMeasurement(const std::string &name, const T (&values)[N])
{
	for (size_t i = 0; i < N; ++i)
		printMeasurement(name + "_" + std::to_string(i), values[i]);
}
} // namespace
void TeamStats::printMeasurements(int team, bool final) const
{
	auto emit = [&](const GameplayMeasurements& measurements, const char* prefix, bool isFinal)
	{
	std::cout << prefix << " team=" << team << " tick=" << measurements.tick
			  << " coverage_start=" << coverageStartTick
			  << " extended_coverage_start=" << extendedCoverageStartTick << " final=" << isFinal;
	printMeasurement("births", measurements.births);
	printMeasurement("deaths", measurements.deaths);
	printMeasurement("conversionsIn", measurements.conversionsIn);
	printMeasurement("conversionsOut", measurements.conversionsOut);
	printMeasurement("harvested", measurements.harvested);
	printMeasurement("cleared", measurements.cleared);
	printMeasurement("delivered", measurements.delivered);
	printMeasurement("withdrawn", measurements.withdrawn);
	printMeasurement("transferredIn", measurements.transferredIn);
	printMeasurement("transferredOut", measurements.transferredOut);
	printMeasurement("consumed", measurements.consumed);
	printMeasurement("repairDelivered", measurements.repairDelivered);
	printMeasurement("meals", measurements.meals);
	printMeasurement("healingVisits", measurements.healingVisits);
	printMeasurement("hpRestored", measurements.hpRestored);
	printMeasurement("damageDealt", measurements.damageDealt);
	printMeasurement("damageReceived", measurements.damageReceived);
	printMeasurement("shots", measurements.shots);
	printMeasurement("impacts", measurements.impacts);
	printMeasurement("completed", measurements.completed);
	printMeasurement("removed", measurements.removed);
	printMeasurement("trainingVisits", measurements.trainingVisits);
	printMeasurement("abilityGains", measurements.abilityGains);
	printMeasurement("stock", measurements.stock);
	printMeasurement("carried", measurements.carried);
	printMeasurement("buildings", measurements.buildings);
	printMeasurement("hungry", measurements.hungry);
	printMeasurement("critical", measurements.critical);
	printMeasurement("feeding", measurements.feeding);
	printMeasurement("healing", measurements.healing);
	printMeasurement("trappedUnits", measurements.trappedUnits);
	printMeasurement("trappedBuildings", measurements.trappedBuildings);
	printMeasurement("lowHP", measurements.lowHP);
	printMeasurement("lowFood", measurements.lowFood);
	printMeasurement("trappedTick", measurements.trappedTick);
	printMeasurement("growthTiles", measurements.growthTiles);
	printMeasurement("growthAmount", measurements.growthAmount);
	printMeasurement("growthReduction", measurements.growthReduction);
	printMeasurement("growthGlobal", measurements.growthGlobal);
	std::cout << '\n';
	};
	if (final)
		for (const auto& sample : measurementHistory) emit(sample, "GLOB2_MEASURE_HISTORY", false);
	emit(measurements, "GLOB2_MEASURE", final);
}

namespace
{
template <class T> Uint64 measurementSum(const T &value)
{
	return value;
}
template <class T, size_t N> Uint64 measurementSum(const T (&values)[N])
{
	Uint64 total = 0;
	for (const auto &v : values)
		total += measurementSum(v);
	return total;
}
} // namespace
const char *TeamStats::measurementLabel(int metric)
{
	static const char *labels[] = {
		"[Stats births]",        "[Stats deaths]",          "[Stats starvation]",
		"[Stats wheat stock]",   "[Stats wheat loads]",     "[Stats meals]",
		"[Stats new buildings]", "[Stats upgrades]",        "[Stats training]",
		"[Stats damage dealt]",  "[Stats damage received]", "[Stats HP restored]",
		"[Stats combat deaths]", "[Stats clearing deaths]", "[Stats trapped deaths]",
		"[Stats unknown deaths]", "[Stats current blocked units]", "[Stats current blocked buildings]",
		"[Stats growth global]", "[Stats growth near 8]", "[Stats growth near 16]",
		"[Stats growth near 32]", "[Stats low HP 25]", "[Stats low food 25]",
		"[Stats low HP 50]", "[Stats low HP 75]", "[Stats low food 50]",
		"[Stats low food 75]", "[Stats structural units]", "[Stats structural buildings]"};
	assert(metric >= 0 && metric < 30);
	return labels[metric];
}
Uint64 TeamStats::graphValue(const GameplayMeasurements &m, int metric)
{
	switch (metric)
	{
	case 0:
		return measurementSum(m.births);
	case 1:
		return measurementSum(m.deaths);
	case 2:
	{
		Uint64 total = 0;
		for (const auto &row : m.deaths)
			total += row[GameplayMeasurements::STARVATION];
		return total;
	}
	case 3:
		return m.stock[WHEAT];
	case 4:
		return m.harvested[WHEAT];
	case 5:
		return m.meals;
	case 6:
		return measurementSum(m.completed[GameplayMeasurements::NEW_BUILDING]);
	case 7:
		return measurementSum(m.completed[GameplayMeasurements::UPGRADED]);
	case 8:
		return measurementSum(m.trainingVisits);
	case 9:
		return measurementSum(m.damageDealt);
	case 10:
		return measurementSum(m.damageReceived);
	case 11:
		return m.hpRestored;
	case 12: case 13: case 14: case 15:
	{
		const int cause[] = {GameplayMeasurements::COMBAT, GameplayMeasurements::CLEARING,
			GameplayMeasurements::TRAPPED, GameplayMeasurements::UNKNOWN};
		Uint64 total = 0;
		for (const auto &row : m.deaths) total += row[cause[metric-12]];
		return total;
	}
	case 16: return measurementSum(m.trappedUnits[1]);
	case 17: return measurementSum(m.trappedBuildings[1][0]);
	case 18: return measurementSum(m.growthGlobal[1]);
	case 19: case 20: case 21: return measurementSum(m.growthAmount[metric-19]);
	case 22: return measurementSum(m.lowHP[0]);
	case 23: return measurementSum(m.lowFood[0]);
	case 24: return measurementSum(m.lowHP[1]);
	case 25: return measurementSum(m.lowHP[2]);
	case 26: return measurementSum(m.lowFood[1]);
	case 27: return measurementSum(m.lowFood[2]);
	case 28: return measurementSum(m.trappedUnits[0]);
	case 29: return measurementSum(m.trappedBuildings[0][0]);
	default:
		return 0;
	}
}
void TeamStats::drawMeasurements(int x, int y)
{
	auto *strings = Toolkit::getStringTable();
	auto compact = [](Uint64 value)
	{
		if (value < 1000000) return std::to_string(value);
		std::ostringstream text;
		text.setf(std::ios::scientific); text.precision(2);
		text << static_cast<long double>(value);
		return text.str();
	};
	auto fit = [](std::string text, int width)
	{
		while (!text.empty() && globalContainer->littleFont->getStringWidth(text) > width)
		{
			size_t last = text.size()-1;
			while (last>0 && (static_cast<unsigned char>(text[last]) & 0xc0)==0x80) --last;
			text.resize(last);
		}
		return text;
	};
	auto line = [&](const char *label, const std::string &value)
	{
		const int valueWidth=globalContainer->littleFont->getStringWidth(value);
		globalContainer->gfx->drawString(x+4,y,globalContainer->littleFont,fit(strings->getString(label),124-valueWidth));
		globalContainer->gfx->drawString(x+132-valueWidth,y,globalContainer->littleFont,value);
		y += 12;
	};
	auto count = [&](const char *label, Uint64 value) { line(label, compact(value)); };
	count("[Stats since tick]", coverageStartTick);
	count("[Stats births]", measurementSum(measurements.births));
	count("[Stats deaths]", measurementSum(measurements.deaths));
	const char *causes[] = {"[Stats combat deaths]", "[Stats starvation]",
							"[Stats clearing deaths]", "[Stats trapped deaths]",
							"[Stats unknown deaths]"};
	for (int c = 0; c < GameplayMeasurements::DEATH_CAUSES; ++c)
	{
		Uint64 n = 0;
		for (auto &row : measurements.deaths)
			n += row[c];
		count(causes[c], n);
	}
	line("[Stats conversions]", compact(measurementSum(measurements.conversionsIn)) + " / " +
									compact(measurementSum(measurements.conversionsOut)));
	for (int r = 0; r < MAX_RESOURCES; ++r)
	{
		const std::string amount = compact(measurements.stock[r]);
		const std::string name = fit(getResourceName(r), 60-globalContainer->littleFont->getStringWidth(amount));
		globalContainer->gfx->drawString(x + 4 + (r % 2) * 66, y, globalContainer->littleFont,
										 name + " " + amount);
		if (r % 2)
			y += 12;
	}
	auto rate = [&](int kind)
	{
		if (measurementHistory.size() < 2)
			return std::string(strings->getString("[Stats unavailable]"));
		const auto &a = measurementHistory[measurementHistory.size() - 2];
		const auto &b = measurementHistory.back();
		const Uint64 difference = kind == 0 ? b.harvested[WHEAT] - a.harvested[WHEAT]
			: b.consumed[GameplayMeasurements::MEAL][WHEAT] -
				a.consumed[GameplayMeasurements::MEAL][WHEAT];
		std::ostringstream text;
		text.setf(std::ios::fixed);
		text.precision(1);
		text << static_cast<long double>(difference) * 1500 / (b.tick - a.tick);
		return text.str();
	};
	line("[Stats wheat per minute]", rate(0));
	line("[Stats meals per minute]", rate(1));
	line("[Stats occupancy]",
		 std::to_string(measurements.feeding) + " / " + std::to_string(measurements.healing));
	count("[Stats HP restored]", measurements.hpRestored);
	count("[Stats new buildings]", graphValue(measurements, 6));
	count("[Stats upgrades]", graphValue(measurements, 7));
	count("[Stats training]", graphValue(measurements, 8));
}

void TeamStats::drawExpandedMeasurements(int x, int y)
{
	auto *strings = Toolkit::getStringTable();
	auto compact = [](Uint64 value)
	{
		if (value < 1000000) return std::to_string(value);
		std::ostringstream text;
		text.setf(std::ios::scientific); text.precision(2);
		text << static_cast<long double>(value);
		return text.str();
	};
	auto line = [&](const char *label, const std::string &value)
	{
		const int valueWidth = globalContainer->littleFont->getStringWidth(value);
		std::string name = strings->getString(label);
		while (!name.empty() && globalContainer->littleFont->getStringWidth(name) > 124-valueWidth)
		{
			size_t last = name.size()-1;
			while (last>0 && (static_cast<unsigned char>(name[last]) & 0xc0)==0x80) --last;
			name.resize(last);
		}
		globalContainer->gfx->drawString(x+4,y,globalContainer->littleFont,name);
		globalContainer->gfx->drawString(x+132-valueWidth,y,globalContainer->littleFont,value);
		y += 12;
	};
	auto count = [&](const char *label, Uint64 value) { line(label,compact(value)); };
	count("[Stats since tick]", extendedCoverageStartTick);
	if (measurements.trappedTick < extendedCoverageStartTick)
	{
		for (const char *key : {"[Stats blocked units]", "[Stats blocked buildings]",
			"[Stats blocked buildings swim]", "[Stats low HP ranges]", "[Stats low food ranges]"})
			line(key,strings->getString("[Stats unavailable]"));
	}
	else
	{
		line("[Stats blocked units]",compact(measurementSum(measurements.trappedUnits[0]))+" / "+
			compact(measurementSum(measurements.trappedUnits[1])));
		line("[Stats blocked buildings]",compact(measurementSum(measurements.trappedBuildings[0][0]))+" / "+
			compact(measurementSum(measurements.trappedBuildings[1][0])));
		line("[Stats blocked buildings swim]",compact(measurementSum(measurements.trappedBuildings[0][1]))+" / "+
			compact(measurementSum(measurements.trappedBuildings[1][1])));
		line("[Stats low HP ranges]",compact(measurementSum(measurements.lowHP[0]))+"/"+
			compact(measurementSum(measurements.lowHP[1]))+"/"+
			compact(measurementSum(measurements.lowHP[2])));
		line("[Stats low food ranges]",compact(measurementSum(measurements.lowFood[0]))+"/"+
			compact(measurementSum(measurements.lowFood[1]))+"/"+
			compact(measurementSum(measurements.lowFood[2])));
	}
	count("[Stats growth new tiles]",measurementSum(measurements.growthGlobal[0]));
	count("[Stats growth global]",measurementSum(measurements.growthGlobal[1]));
	count("[Stats growth reductions]",measurementSum(measurements.growthGlobal[2]));
	for (int r = 0; r < MAX_RESOURCES; ++r)
	{
		const std::string amount = compact(measurements.growthGlobal[1][r]);
		std::string name = getResourceName(r);
		while (!name.empty() && globalContainer->littleFont->getStringWidth(name+" "+amount) > 60)
		{
			size_t last = name.size()-1;
			while (last>0 && (static_cast<unsigned char>(name[last]) & 0xc0)==0x80) --last;
			name.resize(last);
		}
		globalContainer->gfx->drawString(x+4+(r%2)*66,y,globalContainer->littleFont,name+" "+amount);
		if (r%2) y += 12;
	}
	count("[Stats growth near 8]",measurementSum(measurements.growthAmount[0]));
	count("[Stats growth near 16]",measurementSum(measurements.growthAmount[1]));
	count("[Stats growth near 32]",measurementSum(measurements.growthAmount[2]));
	if (measurementHistory.size() < 2)
		line("[Stats growth per minute]",strings->getString("[Stats unavailable]"));
	else
	{
		const auto &a = measurementHistory[measurementHistory.size()-2];
		const auto &b = measurementHistory.back();
		const Uint64 delta = b.growthGlobal[1][WHEAT]-a.growthGlobal[1][WHEAT];
		std::ostringstream rate;
		rate.setf(std::ios::fixed); rate.precision(1);
		rate << static_cast<long double>(delta)*1500/(b.tick-a.tick);
		line("[Stats growth per minute]",rate.str());
	}
}

void TeamStats::beginMeasurementSnapshot(Team *team)
{
	measurements.tick = team->game->stepCounter;
	std::fill(std::begin(measurements.stock), std::end(measurements.stock), 0);
	std::fill(std::begin(measurements.carried), std::end(measurements.carried), 0);
	for (auto &row : measurements.buildings)
		std::fill(std::begin(row), std::end(row), 0);
	measurements.hungry = measurements.critical = measurements.feeding = measurements.healing = 0;
	for (int r = 0; r < MAX_NB_RESOURCES; ++r)
		measurements.stock[r] = std::max(0, team->teamResources[r]);
}
void TeamStats::observeMeasurementUnit(Unit *u)
{
	if (u && !u->isDead)
	{
		if (u->carriedResource >= 0 && u->carriedResource < MAX_NB_RESOURCES)
			++measurements.carried[u->carriedResource];
		if (u->isUnitHungry())
		{
			++measurements.hungry;
			if (u->hp < u->performance[HP])
				++measurements.critical;
		}
		if (u->displacement == Unit::DIS_INSIDE)
		{
			if (u->destinationPurpose == FEED)
				++measurements.feeding;
			if (u->destinationPurpose == HEAL)
				++measurements.healing;
		}
	}
}
void TeamStats::observeMeasurementBuilding(Building *b)
{
	if (b && !b->type->isVirtual && b->buildingState != Building::DEAD)
	{
		++measurements.buildings[b->type->shortTypeNum][b->getLongLevel()];
		if (!b->type->useTeamResources)
			for (int r = 0; r < MAX_NB_RESOURCES; ++r)
				measurements.stock[r] += std::max(0, b->resources[r]);
	}
}
void TeamStats::refreshMeasurements(Team *team)
{
	beginMeasurementSnapshot(team);
	for (int i = 0; i < Unit::MAX_COUNT; ++i)
		observeMeasurementUnit(team->myUnits[i]);
	for (int i = 0; i < Building::MAX_COUNT; ++i)
		observeMeasurementBuilding(team->myBuildings[i]);
	sampleTraps(team);
}

void TeamStats::sampleTraps(Team *team)
{
	auto &m = measurements;
	m.trappedTick = team->game->stepCounter;
	for (auto &row : m.trappedUnits) std::fill(std::begin(row), std::end(row), 0);
	for (auto &row : m.trappedBuildings)
		for (auto &swim : row) std::fill(std::begin(swim), std::end(swim), 0);
	for (auto &row : m.lowHP) std::fill(std::begin(row), std::end(row), 0);
	for (auto &row : m.lowFood) std::fill(std::begin(row), std::end(row), 0);
	static constexpr int offsets[8][2] = {{-1,-1},{0,-1},{1,-1},{-1,0},
		{1,0},{-1,1},{0,1},{1,1}};
	Map *map = team->map;
	for (int i = 0; i < Unit::MAX_COUNT; ++i)
	{
		Unit *u = team->myUnits[i];
		if (!u || u->isDead) continue;
		for (int band = 0; band < 3; ++band)
		{
			const int percent = (band + 1) * 25;
			if (Sint64(u->hp) * 100 <= Sint64(u->performance[HP]) * percent)
				++m.lowHP[band][u->typeNum];
			if (Sint64(u->hungry) * 100 <= Sint64(Unit::HUNGRY_MAX) * percent)
				++m.lowFood[band][u->typeNum];
		}
		bool structural = false, current = false;
		if (u->displacement == Unit::DIS_INSIDE && u->attachedBuilding)
		{
			Building *b = u->attachedBuilding;
			if (u->performance[FLY])
			{
				int x,y,dx,dy;
				current = !b->findAirExit(&x,&y,&dx,&dy);
			}
			else
			{
				bool hardExit = false;
				for (int y = b->posY - 1; y <= b->posY + b->type->height && !hardExit; ++y)
					for (int x = b->posX - 1; x <= b->posX + b->type->width; ++x)
						if (x < b->posX || x >= b->posX + b->type->width ||
							y < b->posY || y >= b->posY + b->type->height)
							hardExit |= map->isHardSpaceForGroundUnit(x,y,u->performance[SWIM] > 0,team->me);
				structural = !hardExit;
				int x,y,dx,dy;
				current = !b->findGroundExit(&x,&y,&dx,&dy,u->performance[SWIM] > 0);
			}
		}
		else if (u->displacement != Unit::DIS_INSIDE)
		{
			bool hardExit = false, freeExit = false;
			for (const auto &d : offsets)
			{
				const int x = u->posX + d[0], y = u->posY + d[1];
				if (u->performance[FLY])
				{
					hardExit = true;
					freeExit |= map->isFreeForAirUnit(x,y);
				}
				else
				{
					hardExit |= map->isHardSpaceForGroundUnit(x,y,u->performance[SWIM] > 0,team->me);
					freeExit |= map->isFreeForGroundUnit(x,y,u->performance[SWIM] > 0,team->me);
				}
				if (hardExit && freeExit) break;
			}
			structural = !hardExit;
			current = !freeExit;
		}
		if (structural) ++m.trappedUnits[0][u->typeNum];
		if (current) ++m.trappedUnits[1][u->typeNum];
	}
	for (int i = 0; i < Building::MAX_COUNT; ++i)
	{
		Building *b = team->myBuildings[i];
		if (!b || b->type->isVirtual || b->buildingState == Building::DEAD) continue;
		for (int swim = 0; swim < 2; ++swim)
		{
			bool hardExit = false;
			for (int y = b->posY - 1; y <= b->posY + b->type->height && !hardExit; ++y)
				for (int x = b->posX - 1; x <= b->posX + b->type->width; ++x)
					if (x < b->posX || x >= b->posX + b->type->width ||
						y < b->posY || y >= b->posY + b->type->height)
						hardExit |= map->isHardSpaceForGroundUnit(x,y,swim != 0,team->me);
			int x,y,dx,dy;
			const bool freeExit = b->findGroundExit(&x,&y,&dx,&dy,swim != 0);
			if (!hardExit) ++m.trappedBuildings[0][swim][b->type->shortTypeNum];
			if (!freeExit) ++m.trappedBuildings[1][swim][b->type->shortTypeNum];
		}
	}
	if ((m.tick & END_OF_GAME_STAT_INTERVAL_MASK) == 0 &&
		(coverageBuildingTick != m.tick || (m.tick == 0 && coverageBuildings.empty())))
	{
		std::vector<CoverageBuilding> latest;
		for (int i = 0; i < Building::MAX_COUNT; ++i)
		{
			Building *b = team->myBuildings[i];
			if (b && !b->type->isVirtual && b->buildingState != Building::DEAD)
				latest.push_back({b->posX,b->posY,b->type->width,b->type->height});
		}
		std::sort(latest.begin(), latest.end(), [](const CoverageBuilding &a, const CoverageBuilding &b) {
			return std::tie(a.x,a.y,a.width,a.height) < std::tie(b.x,b.y,b.width,b.height);
		});
		if (latest != coverageBuildings)
		{
			coverageBuildings.swap(latest);
			++coverageBuildingGeneration;
		}
		coverageBuildingTick = m.tick;
	}
}
