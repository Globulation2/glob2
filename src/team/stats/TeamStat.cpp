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
#include <PackedRecords.h>
#include <stdexcept>
#include <cstddef>
#include <algorithm>
#include <tuple>
#include <limits>

#include "Game.h"
#include "GlobalContainer.h"
#include "Team.h"
#include "TeamStat.h"
#include "FileFormatVersions.h"
#include "Version.h"
#include "Unit.h"
#include "Bullet.h"
#include "Map.h"
#include "ai/model/BuildingProjection.h"


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

template<class Stream, class Measurement>
void variantFields(Stream* stream, Measurement& value)
{
	statValue(stream,"count",value.count);
	statValue(stream,"completed",value.completed);
	statValue(stream,"removed",value.removed);
	statValue(stream,"trapped",value.trapped);
}
void statValue(GAGCore::OutputStream* stream,const char* name,const BuildingMeasurement& value)
{
	stream->writeEnterSection(name); variantFields(stream,value); stream->writeLeaveSection();
}
void statValue(GAGCore::InputStream* stream,const char* name,BuildingMeasurement& value)
{
	stream->readEnterSection(name); variantFields(stream,value); stream->readLeaveSection();
}
template<class T>
void statValue(GAGCore::OutputStream* stream,const char* name,const std::vector<T>& values)
{
	stream->writeEnterSection(name);
	stream->writeUint32(values.size(),"size");
	for (unsigned i=0; i<values.size(); ++i)
	{
		stream->writeEnterSection(i); statValue(stream,"value",values[i]); stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}
template<class T>
void statValue(GAGCore::InputStream* stream,const char* name,std::vector<T>& values)
{
	stream->readEnterSection(name);
	const auto count=stream->readUint32("size");
	if (count>4096) throw std::runtime_error("Invalid building statistics catalog size");
	values.resize(count);
	for (unsigned i=0; i<count; ++i)
	{
		stream->readEnterSection(i); statValue(stream,"value",values[i]); stream->readLeaveSection();
	}
	stream->readLeaveSection();
}

// Fields are grouped by the save format that introduced them; a stream written
// at versionMinor carries exactly the groups that existed then.
template <class Stream, class Stat> void measurementFields(Stream *stream, Stat &stat, int versionMinor = VERSION_MINOR)
{
	if (versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG) statValue(stream,"variants",stat.variants);
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
	enterStatSection(stream,"consumed");
	const int purposes = versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG ? GameplayMeasurements::PURPOSES : GameplayMeasurements::HEALING_COST;
	for (int i=0; i<purposes; ++i)
	{
		enterStatSection(stream,i); statValue(stream,"value",stat.consumed[i]); leaveStatSection(stream);
	}
	leaveStatSection(stream);
	statValue(stream, "repairDelivered", stat.repairDelivered);
	if (versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG) statValue(stream,"resourceSpillageEvents",stat.resourceSpillageEvents);
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
	if (versionMinor >= FILE_FORMAT_VERSION_EXTENDED_GAMEPLAY_STATS)
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
	if (versionMinor >= FILE_FORMAT_VERSION_LABOUR_STATS)
	{
		statValue(stream, "labour", stat.labour);
		statValue(stream, "filling", stat.filling);
		statValue(stream, "harvestDistance", stat.harvestDistance);
		statValue(stream, "harvestSamples", stat.harvestSamples);
		statValue(stream, "eatWalkDistance", stat.eatWalkDistance);
		statValue(stream, "eatWalkSamples", stat.eatWalkSamples);
		statValue(stream, "combatDeathPlace", stat.combatDeathPlace);
		statValue(stream, "combatDeathAssignment", stat.combatDeathAssignment);
		statValue(stream, "warriors", stat.warriors);
		statValue(stream, "warriorLevels", stat.warriorLevels);
		statValue(stream, "warriorsHurt", stat.warriorsHurt);
		statValue(stream, "warriorsFlagged", stat.warriorsFlagged);
		statValue(stream, "warriorsInside", stat.warriorsInside);
		statValue(stream, "intruders", stat.intruders);
		statValue(stream, "intruderLevels", stat.intruderLevels);
		statValue(stream, "defenceTick", stat.defenceTick);
	}
}

// Packed history rows have the fixed size of one record in the save's own format.
size_t measurementRecordBytes(int versionMinor, size_t catalogSize)
{
    GAGCore::BinaryOutputStream stream(new GAGCore::MemoryStreamBackend);
    GameplayMeasurements value{};
    value.variants.resize(catalogSize);
    measurementFields(&stream,value,versionMinor);
    return stream.getPosition();
}

template<class Stream, class Stat>
void liveStatFields(Stream* stream, Stat& stat, int versionMinor = VERSION_MINOR)
{
    if (versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG) statValue(stream,"buildingCountByVariant",stat.buildingCountByVariant);
    statValue(stream, "totalUnit", stat.totalUnit);
    statValue(stream, "numberUnitPerType", stat.numberUnitPerType);
    if (versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG) statValue(stream,"workersByConstructionLevel",stat.workersByConstructionLevel);
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
	std::fill(buildingCountByVariant.begin(),buildingCountByVariant.end(),0);
	std::fill_n(workersByConstructionLevel, NB_UNIT_LEVELS, 0);
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

void TeamStats::step(Team *team, bool reloaded)
{
	PERF_SCOPE_TIME(Stats);
	if (!reloaded && needsMeasurementInitialization)
		initializeMeasurements(team->game->stepCounter);
	if (reloaded) rebuildMeasurementCountReset();
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
            // Historical column names remain log/model aliases. Count each
            // physical variant once per applicable service, never by family ID.
            int production=0,feeding=0,healing=0,construction=0,combat=0,walking=0,swimming=0,projectiles=0;
            for(size_t id=0;id<s.buildingCountByVariant.size();++id)
            {
                const int count=s.buildingCountByVariant[id];if(!count)continue;
                const auto& type=ModelBuildingProjection::completed(team->game->buildingsTypes,*team->game->buildingsTypes.get(id));
                const auto& spec=type.semantics;
                auto trains=[&](int ability) {const auto& t=spec.training[ability];return type.maxUnitInside>0 && t.enabled && (t.unitMask&spec.admittedUnitMask);};
                production+=count*bool(spec.production.enabledUnitMask);
                feeding+=count*bool(type.maxUnitInside>0 && spec.feeding.enabled && (spec.feeding.unitMask&spec.admittedUnitMask));
                healing+=count*bool(type.maxUnitInside>0 && spec.healing.enabled && (spec.healing.unitMask&spec.admittedUnitMask));
                combat+=count*ModelBuildingProjection::trainsWarriorCombat(type);
                walking+=count*trains(WALK);swimming+=count*trains(SWIM);
                bool grantsConstruction=false;
                for(int ability=0;ability<NB_ABILITY;++ability)
                    grantsConstruction|=trains(ability) && spec.training[ability].constructionLevel>0 &&
                        (spec.training[ability].unitMask&spec.admittedUnitMask&(1u<<WORKER));
                construction+=count*grantsConstruction;
                projectiles+=count*bool(type.shootingRange>0 && type.shootRhythm>0 &&
                    std::any_of(spec.projectileDamage.begin(),spec.projectileDamage.end(),[](int n){return n>0;}));
            }
			std::cout << "GLOB2_ECON team=" << team->teamNumber
				<< " tick=" << team->game->stepCounter
				<< " workers=" << s.numberUnitPerType[WORKER]
				<< " warriors=" << s.numberUnitPerType[WARRIOR]
				<< " explorers=" << s.numberUnitPerType[EXPLORER]
				<< " food=" << s.totalFood << "/" << s.totalFoodCapacity
				<< " fooded=" << s.totalUnitFooded << "/" << s.totalUnitFoodable
				<< " foodCritical=" << s.needFoodCritical
				<< " needFood=" << s.needFood
				<< " swarm=" << production
				<< " inn=" << feeding
				<< " school=" << construction
				<< " barracks=" << combat
				<< " hospital=" << healing
				<< " racetrack=" << walking
				<< " pool=" << swimming
				<< " tower=" << projectiles;
            for(size_t id=0;id<s.buildingCountByVariant.size();++id)
                std::cout << " variant_" << id << '=' << s.buildingCountByVariant[id];
            std::cout << std::endl;
		}
	}
	
	// handle in game stat step
	TeamSmoothedStat &smoothedStat=smoothedStats[smoothedIndex];
	smoothedStat.reset();
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=team->myUnits[i];
		observeMeasurementUnit(u);
		// Filter here: most of the 1024 slots hold no worker.
		if (!reloaded && u && u->typeNum == WORKER)
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
		        smoothedStat.totalNeededPerLevel[b->type->semantics.requiredWorkerLevel]+=b->desiredMaxUnitWorking-(int)b->unitsWorking.size();
            }
        }
    }

	if (!reloaded && !needsMeasurementInitialization &&
		(measurements.tick & END_OF_GAME_STAT_INTERVAL_MASK) == 0 &&
		(measurementHistory.empty() || measurementHistory.back().tick != measurements.tick))
	{
		sampleTraps(team);
		sampleDefence(team);
		measurementHistory.push_back(measurements);
		AITelemetry::capture(team, true, getenv("GLOB2_TEAM_TIMELINE") != nullptr);
		if (getenv("GLOB2_TEAM_TIMELINE"))
			printMeasurements(team->teamNumber);
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
	stat.buildingCountByVariant.resize(team->game->buildingsTypes.size(),0);

	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=team->myUnits[i];
		if (u)
		{
			stat.totalUnit++;
			stat.numberUnitPerType[(int)u->typeNum]++;
			if (u->typeNum==WORKER) ++stat.workersByConstructionLevel[u->workerLevel()];
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
			++stat.buildingCountByVariant[b->typeNum];
			// Historical histograms remain for authored SGSL and older dataset readers.
			const int family=b->type->shortTypeNum, level=b->getLongLevel();
			if (family>=0 && family<IntBuildingType::NB_BUILDING && level>=0 && level<NB_BUILDING_LONG_LEVELS)
			{
				++stat.numberBuildingPerType[family];
				++stat.numberBuildingPerTypePerLevel[family][level];
			}
			stat.totalHP += b->hp;
			// The scalar model/UI convention is damage against warriors. Actual
            // projectile resolution keeps its independent per-unit damage.
            if (b->type->shootingRange > 0)
                stat.totalDefensePower = int(std::min<Sint64>(std::numeric_limits<int>::max(),
                    Sint64(stat.totalDefensePower) + ((Sint64(b->type->semantics.projectileDamage[WARRIOR]) *
                        b->type->shootRhythm) >> SHOOTING_COOLDOWN_MAGNITUDE)));
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

		// The same worst-to-best colours as the hunger and health charts after a match.
		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 12, 163, 12));
		gfx->drawString(textStartPos, startPoxY+104, font, Free);
		font->popStyle();

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 250, 178, 25));
		gfx->drawString(textStartPos+64, startPoxY+104, font, hungry);
		font->popStyle();

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 236, 131, 90));
		gfx->drawString(textStartPos, startPoxY+104+12, font, starving);
		font->popStyle();

		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 208, 59, 59));
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
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 120+12  +64-nbNeedHeal-nbNeedFoodCritical-nbNeedFood-nbOk, nbOk, 12, 163, 12);
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 120+12 +64-nbNeedHeal-nbNeedFoodCritical-nbNeedFood, nbNeedFood, 250, 178, 25);
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 120+12 +64-nbNeedHeal-nbNeedFoodCritical, nbNeedFoodCritical, 236, 131, 90);
		globalContainer->gfx->drawVertLine(posx+i, startPoxY+ 120+12 +64-nbNeedHeal, nbNeedHeal, 208, 59, 59);
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
	return (stats[statsIndex].workersByConstructionLevel[level]);
}

int TeamStats::getStarvingUnits()
{
	return (stats[statsIndex].needFoodCritical);
}

bool TeamStats::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	stream->readEnterSection("TeamStats");
	Uint32 size=0;
	size=stream->readCount("size");

	bool stop=false;
	
    const auto readEnd=[&](GAGCore::InputStream* stream,size_t i)
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
    };
    if(versionMinor>=FILE_FORMAT_VERSION_COMPACT_STATE && GAGCore::PackedArray::binary(stream))
        GAGCore::PackedRecords::read(stream,size,24,readEnd);
    else for(unsigned i=0;i<size;++i) readEnd(stream,i);
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
            liveStatFields(stream, stats[i], versionMinor);
            if (versionMinor < FILE_FORMAT_VERSION_BUILDING_CATALOG)
                std::copy_n(stats[i].upgradeStatePerType[WORKER][BUILD], NB_UNIT_LEVELS, stats[i].workersByConstructionLevel);
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
		measurementFields(stream, measurements, versionMinor);
		const Uint32 count = stream->readUint32("measurementCount");
		if (measurements.tick < coverageStartTick || count > Uint64(measurements.tick) / 512 + 1)
			throw std::runtime_error("Invalid gameplay statistics coverage");
		measurementHistory.clear();
        const auto readMeasurement=[&](GAGCore::InputStream* stream,size_t i)
        {
			stream->readEnterSection(i);
			GameplayMeasurements sample;
			measurementFields(stream, sample, versionMinor);
			stream->readLeaveSection();
			if (sample.variants.size() != measurements.variants.size() || sample.tick < coverageStartTick || sample.tick > measurements.tick ||
				(sample.tick & 511) ||
				(!measurementHistory.empty() && sample.tick <= measurementHistory.back().tick))
				throw std::runtime_error("Invalid gameplay statistics timestamp");
			measurementHistory.push_back(sample);
        };
        if(versionMinor>=FILE_FORMAT_VERSION_COMPACT_STATE && GAGCore::PackedArray::binary(stream))
            GAGCore::PackedRecords::read(stream,count,measurementRecordBytes(versionMinor, measurements.variants.size()),readMeasurement);
        else for(Uint32 i=0;i<count;++i) readMeasurement(stream,i);
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
		labourCoverageStartTick = versionMinor >= FILE_FORMAT_VERSION_LABOUR_STATS
			? stream->readUint32("labourCoverageStartTick") : measurements.tick;
		if (labourCoverageStartTick < coverageStartTick || labourCoverageStartTick > measurements.tick)
			throw std::runtime_error("Invalid labour statistics coverage");
	}
	else
	{
		needsMeasurementInitialization = true;
		extendedCoverageStartTick = labourCoverageStartTick = measurements.tick;
	}

	if (versionMinor >= FILE_FORMAT_VERSION_AI_TELEMETRY)
		AITelemetry::load(stream, aiTelemetry, versionMinor);
	else
		aiTelemetry.clear();
	stream->readLeaveSection();
    if (versionMinor < FILE_FORMAT_VERSION_BUILDING_CATALOG)
    {
        // Legacy saves always load the legacy catalog. Preserve sampled history,
        // rather than reconstructing past counts from today's live buildings.
        BuildingsTypes catalog;
        catalog.initLegacy();
        for (auto& stat : stats) stat.buildingCountByVariant.assign(catalog.size(), 0);
        const auto importMeasurement = [&](GameplayMeasurements& sample)
        {
            sample.variants.assign(catalog.size(), {});
            bool importedTraps[IntBuildingType::NB_BUILDING]{};
            for (size_t id=0; id<catalog.size(); ++id)
            {
                const auto& type = *catalog.get(id);
                const int family=type.shortTypeNum, longLevel=2*type.level+(type.isBuildingSite ? 0 : 1);
                if (family<0 || family>=IntBuildingType::NB_BUILDING || longLevel<0 || longLevel>=NB_BUILDING_LONG_LEVELS) continue;
                auto& variant=sample.variants[id];
                variant.count=sample.buildings[family][longLevel];
                for (int kind=0; kind<GameplayMeasurements::REMOVALS; ++kind)
                    variant.removed[kind]=sample.removed[kind][family][longLevel];
                if (!type.isBuildingSite && type.level<NB_UNIT_LEVELS)
                    for (int kind=0; kind<GameplayMeasurements::COMPLETIONS; ++kind)
                        variant.completed[kind]=sample.completed[kind][family][type.level];
                // Old blockage samples did not retain levels. Keep the family
                // aggregate once; it is used only for the total blockage metric.
                if (!type.isBuildingSite && !importedTraps[family])
                {
                    for (int blocked=0; blocked<2; ++blocked) for (int swim=0; swim<2; ++swim)
                        variant.trapped[blocked][swim]=sample.trappedBuildings[blocked][swim][family];
                    importedTraps[family]=true;
                }
            }
        };
        for (size_t id=0; id<catalog.size(); ++id)
        {
            const auto& type=*catalog.get(id);
            const int family=type.shortTypeNum, longLevel=2*type.level+(type.isBuildingSite ? 0 : 1);
            if (family<0 || family>=IntBuildingType::NB_BUILDING || longLevel<0 || longLevel>=NB_BUILDING_LONG_LEVELS) continue;
            for (auto& stat : stats) stat.buildingCountByVariant[id]=stat.numberBuildingPerTypePerLevel[family][longLevel];
        }
        importMeasurement(measurements);
        for (auto& sample : measurementHistory) importMeasurement(sample);
    }
	rebuildMeasurementCountReset();
	return true;
}

void TeamStats::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("TeamStats");
	stream->writeUint32(endOfGameStats.size(), "size");
    if(GAGCore::PackedArray::binary(stream))
        GAGCore::PackedRecords::write(stream,endOfGameStats.size(),24,[&](GAGCore::OutputStream* rows,size_t i) {
            for(int k=0;k<6;++k) rows->writeSint32(endOfGameStats[i].value[k],"value");
        });
    else
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
    if(GAGCore::PackedArray::binary(stream))
        GAGCore::PackedRecords::write(stream,measurementHistory.size(),measurementRecordBytes(VERSION_MINOR, measurements.variants.size()),
            [&](GAGCore::OutputStream* rows,size_t i){
				auto sample=measurementHistory[i]; sample.variants.resize(measurements.variants.size());
				measurementFields(rows,sample);
			});
    else
	for (unsigned i = 0; i < measurementHistory.size(); ++i)
	{
		stream->writeEnterSection(i);
		auto sample=measurementHistory[i]; sample.variants.resize(measurements.variants.size());
		measurementFields(stream, sample);
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
	stream->writeUint32(labourCoverageStartTick, "labourCoverageStartTick");

	AITelemetry::save(stream, aiTelemetry);
	stream->writeLeaveSection();
}

void TeamStats::initializeMeasurements(Uint32 tick)
{
	measurements = GameplayMeasurements{};
	measurementCountTouched.clear();
	measurementCountCatalogSize = 0;
	measurements.tick = coverageStartTick = tick;
	extendedCoverageStartTick = labourCoverageStartTick = tick;
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
			  << " extended_coverage_start=" << extendedCoverageStartTick
			  << " labour_coverage_start=" << labourCoverageStartTick << " final=" << isFinal;
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
	for (size_t i=0; i<measurements.variants.size(); ++i)
	{
		const auto key="variant_"+std::to_string(i);
		const auto& value=measurements.variants[i];
		printMeasurement(key+"_count",value.count);
		printMeasurement(key+"_completed",value.completed);
		printMeasurement(key+"_removed",value.removed);
		printMeasurement(key+"_trapped",value.trapped);
	}
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
	printMeasurement("labour", measurements.labour);
	printMeasurement("filling", measurements.filling);
	printMeasurement("harvestDistance", measurements.harvestDistance);
	printMeasurement("harvestSamples", measurements.harvestSamples);
	printMeasurement("eatWalkDistance", measurements.eatWalkDistance);
	printMeasurement("eatWalkSamples", measurements.eatWalkSamples);
	printMeasurement("combatDeathPlace", measurements.combatDeathPlace);
	printMeasurement("combatDeathAssignment", measurements.combatDeathAssignment);
	printMeasurement("warriors", measurements.warriors);
	printMeasurement("warriorLevels", measurements.warriorLevels);
	printMeasurement("warriorsHurt", measurements.warriorsHurt);
	printMeasurement("warriorsFlagged", measurements.warriorsFlagged);
	printMeasurement("warriorsInside", measurements.warriorsInside);
	printMeasurement("intruders", measurements.intruders);
	printMeasurement("intruderLevels", measurements.intruderLevels);
	printMeasurement("defenceTick", measurements.defenceTick);
	std::cout << '\n';
	};
	if (final)
		for (const auto& sample : measurementHistory) emit(sample, "GLOB2_MEASURE_HISTORY", false);
	emit(measurements, "GLOB2_MEASURE", final);
}

void TeamStats::rebuildMeasurementCountReset()
{
	measurementCountTouched.clear();
	measurementCountCatalogSize = measurements.variants.size();
	measurementCountTouched.reserve(measurementCountCatalogSize);
	for (size_t id=0; id<measurementCountCatalogSize; ++id)
		if (measurements.variants[id].count) measurementCountTouched.push_back(id);
}

void TeamStats::beginMeasurementSnapshot(Team *team)
{
	measurements.tick = team->game->stepCounter;
	measurements.variants.resize(team->game->buildingsTypes.size());
	if (measurementCountCatalogSize != measurements.variants.size())
		rebuildMeasurementCountReset();
	for (const auto id : measurementCountTouched) measurements.variants[id].count=0;
	measurementCountTouched.clear();
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
		auto& count=measurements.variants[b->typeNum].count;
		if (count == 0) measurementCountTouched.push_back(size_t(b->typeNum));
		++count;
		if (b->type->shortTypeNum>=0 && b->type->shortTypeNum<IntBuildingType::NB_BUILDING && b->getLongLevel()<NB_BUILDING_LONG_LEVELS)
			++measurements.buildings[b->type->shortTypeNum][b->getLongLevel()];
		if (!b->type->useTeamResources)
			for (int r = 0; r < MAX_NB_RESOURCES; ++r)
				measurements.stock[r] += std::max(0, b->resources[r]);
	}
}
GameplayMeasurements::Place TeamStats::placeOf(const Team *team, int x, int y)
{
	// One read of the growth-coverage tile masks. Callers refresh them first.
	const Uint32 nearbyTeams = team->map->teamsWithBuildingsNear(x, y, GameplayMeasurements::PLACE_BAND);
	if (nearbyTeams & team->me)
		return GameplayMeasurements::HOME;
	return (nearbyTeams & team->enemies) ? GameplayMeasurements::AWAY : GameplayMeasurements::FIELD;
}

void TeamStats::observeLabour(Unit *u)
{
	using M = GameplayMeasurements;
	if (!u || u->isDead || u->typeNum != WORKER)
		return;
	auto &m = measurements;
	auto distanceTo = [u](Building *b)
	{
		return Uint64(u->owner->map->warpDistMax(u->posX, u->posY, b->getMidX(), b->getMidY()));
	};
	const bool inside = u->displacement == Unit::DIS_INSIDE ||
		u->displacement == Unit::DIS_ENTERING_BUILDING || u->displacement == Unit::DIS_EXITING_BUILDING;
	if (u->medical != Unit::MED_FREE)
	{
		const bool hungry = u->medical == Unit::MED_HUNGRY;
		if (inside)
			++m.labour[hungry ? M::EAT_INSIDE : M::HEAL_INSIDE];
		else if (!u->targetBuilding)
			++m.labour[hungry ? M::EAT_NO_INN : M::HEAL_NO_HOSPITAL];
		else
		{
			++m.labour[hungry ? M::EAT_WALKING : M::HEAL_WALKING];
			if (hungry)
			{
				m.eatWalkDistance += distanceTo(u->targetBuilding);
				++m.eatWalkSamples;
			}
		}
		return;
	}
	switch (u->activity)
	{
	case Unit::ACT_RANDOM:
		++m.labour[M::IDLE];
		return;
	case Unit::ACT_UPGRADING:
		if (u->destinationPurpose == HEAL)
			++m.labour[inside ? M::HEAL_INSIDE : M::HEAL_WALKING];
		else
			++m.labour[inside ? M::TRAIN_INSIDE : M::TRAIN_WALKING];
		return;
	case Unit::ACT_FLAG:
		++m.labour[M::FLAG_WORK];
		return;
	case Unit::ACT_FILLING:
		if (Building *b = u->attachedBuilding)
		{
			M::LabourJob job = M::OTHER_JOB;
			if (b->type->isBuildingSite)
				job = M::SITE_JOB;
			else if (b->type->semantics.production.enabledUnitMask)
				job = M::SWARM_JOB;
			else if (b->type->canFeedUnit)
				job = M::INN_JOB;
			M::LabourPhase phase = M::OTHER_PHASE;
			if (u->displacement == Unit::DIS_GOING_TO_RESOURCE)
				phase = M::TO_RESOURCE;
			else if (u->displacement == Unit::DIS_HARVESTING)
			{
				phase = M::HARVESTING;
				m.harvestDistance[job] += distanceTo(b);
				++m.harvestSamples[job];
			}
			else if (u->displacement == Unit::DIS_GOING_TO_BUILDING)
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

void TeamStats::recordCombatDeath(Unit *u)
{
	using M = GameplayMeasurements;
	if (u->typeNum < 0 || u->typeNum >= NB_UNIT_TYPE)
		return;
	u->owner->map->rebuildGrowthCoverage();
	++measurements.combatDeathPlace[u->typeNum][placeOf(u->owner, u->posX, u->posY)];
	M::Assignment assignment = M::UNASSIGNED;
	if (u->attachedBuilding)
	{
		assignment=M::OTHER_BUILDING;
		if (u->activity==Unit::ACT_FLAG)
		{
			if (u->typeNum==WARRIOR) assignment=M::WAR_FLAG;
			else if (u->typeNum==WORKER) assignment=M::CLEARING_FLAG;
			else if (u->typeNum==EXPLORER) assignment=M::EXPLORATION_FLAG;
		}
	}
	++measurements.combatDeathAssignment[u->typeNum][assignment];
}

void TeamStats::sampleDefence(Team *team)
{
	auto &m = measurements;
	m.defenceTick = team->game->stepCounter;
	team->map->rebuildGrowthCoverage();
	std::fill(std::begin(m.warriors), std::end(m.warriors), 0);
	std::fill(std::begin(m.warriorLevels), std::end(m.warriorLevels), 0);
	m.warriorsHurt = m.warriorsFlagged = m.warriorsInside = m.intruders = m.intruderLevels = 0;
	auto attackLevels = [](const Unit *u) { return Uint64(u->level[ATTACK_SPEED] + u->level[ATTACK_STRENGTH]); };
	for (int i = 0; i < Unit::MAX_COUNT; ++i)
	{
		Unit *u = team->myUnits[i];
		if (!u || u->isDead || u->typeNum != WARRIOR)
			continue;
		const auto place = placeOf(team, u->posX, u->posY);
		++m.warriors[place];
		m.warriorLevels[place] += attackLevels(u);
		m.warriorsHurt += u->medical == Unit::MED_DAMAGED;
		m.warriorsFlagged += u->attachedBuilding &&
			u->activity == Unit::ACT_FLAG && u->attachedBuilding->type->zonable[WARRIOR];
		m.warriorsInside += u->displacement == Unit::DIS_INSIDE;
	}
	for (int t = 0; t < team->game->teamsCount(); ++t)
	{
		const Team *other = team->game->teams[t];
		if (!other || !(team->enemies & other->me))
			continue;
		for (int i = 0; i < Unit::MAX_COUNT; ++i)
		{
			const Unit *u = other->myUnits[i];
			if (u && !u->isDead && u->typeNum == WARRIOR &&
				placeOf(team, u->posX, u->posY) == GameplayMeasurements::HOME)
			{
				++m.intruders;
				m.intruderLevels += attackLevels(u);
			}
		}
	}
}

void TeamStats::refreshMeasurements(Team *team)
{
	// Explicit cold refresh also accepts caller-supplied diagnostic counts.
	rebuildMeasurementCountReset();
	beginMeasurementSnapshot(team);
	for (int i = 0; i < Unit::MAX_COUNT; ++i)
		observeMeasurementUnit(team->myUnits[i]);
	for (int i = 0; i < Building::MAX_COUNT; ++i)
		observeMeasurementBuilding(team->myBuildings[i]);
	sampleTraps(team);
	sampleDefence(team);
}

void TeamStats::sampleTraps(Team *team)
{
	auto &m = measurements;
	m.trappedTick = team->game->stepCounter;
	m.variants.resize(team->game->buildingsTypes.size());
	for (auto& variant : m.variants) for (auto& row : variant.trapped) std::fill(std::begin(row),std::end(row),0);
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
			if (!hardExit) ++m.variants[b->typeNum].trapped[0][swim];
			if (!freeExit) ++m.variants[b->typeNum].trapped[1][swim];
			if (b->type->shortTypeNum>=0 && b->type->shortTypeNum<IntBuildingType::NB_BUILDING)
			{
				if (!hardExit) ++m.trappedBuildings[0][swim][b->type->shortTypeNum];
				if (!freeExit) ++m.trappedBuildings[1][swim][b->type->shortTypeNum];
			}
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
