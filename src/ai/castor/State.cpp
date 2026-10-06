// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "AITelemetryFields.h"
#include "AICastor.h"
#include "ai/observation/WorldQueries.h"
#include "Game.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"
#include <algorithm>

#define AI_FILE_MIN_VERSION 1
#define AI_FILE_VERSION 2

using std::shared_ptr;

bool AICastor::enoughFreeWorkers()
{
	telemetry.count(AITrace::AI2::AICastor_enoughFreeWorkers_calls);
	int totalWorkers=observedTeam->view->statistics.numberUnitPerType[WORKER];
	int workersBalance=observedTeam->view->workerBalance;
	int partFree=(totalWorkers/strategy.isFreePart);
	int minBalance;
	if (buildsAmount<=0)
		minBalance=-partFree;
	else if (buildsAmount<=AI_CASTOR_BUILDS_LOW)
		minBalance=0;
	else if (buildsAmount<=AI_CASTOR_BUILDS_MID)
		minBalance=partFree;
	else
		minBalance=(partFree<<AI_CASTOR_BALANCE_LATE_SHIFT);
	if (foodLock)
		minBalance+=AI_CASTOR_FOODLOCK_BALANCE_BIAS;
	int minOverWorkers=minBalance+partFree;

	bool enough=(workersBalance>minBalance);
	overWorkers=(workersBalance>minOverWorkers);

	return telemetry.returnedBool(AITrace::AI2::AICastor_enoughFreeWorkers_result,
								  AITrace::AI2::AICastor_enoughFreeWorkers_true, enough);
}

void AICastor::computeCanSwim()
{
	telemetry.count(AITrace::AI2::AICastor_computeCanSwim_calls);
	//printf("computeCanSwim()...\n");
	// If our population has more healthy-working-units able to swim than healthy-working-units
	// unable to swim then we choose to be able to go through water:
	const auto& myUnits=observedTeam->myUnits;
	int sumCanSwim=0;
	int sumCantSwim=0;
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		const AIEngine::UnitView *u=myUnits[i];
		if (u && u->type==WORKER && u->medical==0)
		{
			if (u->performance[SWIM]>0)
				sumCanSwim++;
			else
				sumCantSwim++;
		}
	}
	
	canSwim=(sumCanSwim>sumCantSwim);
	//printf("...computeCanSwim() done\n");
}

void AICastor::computeNeedSwim()
{
	telemetry.count(AITrace::AI2::AICastor_computeNeedSwim_calls);
	int w=observation->width;
	int h=observation->height;
	size_t size=w*h;
	
	canSwim=false;
	computeObstacleUnitMap();
	computeWorkRangeMap();
	
	Sint32 baseCount=0;
	for (size_t i=0; i<size; i++)
		if (workRangeMap[i]!=0)
			baseCount++;
	
	canSwim=true;
	computeObstacleUnitMap();
	computeWorkRangeMap();
	
	Sint32 extendedCount=0;
	for (size_t i=0; i<size; i++)
		if (workRangeMap[i]!=0)
			extendedCount++;
	
	needSwim=((baseCount<<AI_CASTOR_SWIM_GAIN_NUMER_SHIFT)>(AI_CASTOR_SWIM_GAIN_DENOM*extendedCount));

	computeCanSwim();
}

void AICastor::computeBuildingSum()
{
	telemetry.count(AITrace::AI2::AICastor_computeBuildingSum_calls);
	for (int bi=0; bi<AICastor::DemandCount; bi++)
		for (int si=0; si<2; si++)
			for (int li=0; li<NB_UNIT_LEVELS; li++)
				buildingLevels[bi][si][li]=0;
	
	const auto& capabilities=*queries;
	const auto& myBuildings=observedTeam->myBuildings;
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		const AIEngine::BuildingView *b=myBuildings[i];
		if (b)
		{
   const bool upgrading = b->state==Building::WAITING_FOR_CONSTRUCTION && b->construction==Building::UPGRADE;
   int completed = queries->kind(*b).resolvedType.isBuildingSite ? queries->kind(*b).resolvedType.nextLevel : b->type;
   if (upgrading && queries->kind(*b).resolvedType.nextLevel >= 0) {
    const auto* next = (&queries->kind(queries->kind(*b).resolvedType.nextLevel).resolvedType);
    completed = next->isBuildingSite ? next->nextLevel : queries->kind(*b).resolvedType.nextLevel;
   }
   if (completed < 0) continue;
   const int stage=std::clamp(capabilities.lineagePosition(completed)-1,0,NB_UNIT_LEVELS-1);
   const auto mask=capabilities.rawIntentMask(completed);
   for (int demand=0; demand<DemandCount; ++demand)
    if (mask & (std::uint64_t{1} << static_cast<unsigned>(demandIntents[demand])))
     buildingLevels[demand][upgrading || queries->kind(*b).resolvedType.isBuildingSite][stage]++;

		}
	}
 for(const auto& intent:pendingCreates) {
  const auto& kind=queries->kind(intent.type);
  const int completed=kind.site?kind.next:intent.type;
  const int stage=std::clamp(queries->lineagePosition(completed)-1,0,NB_UNIT_LEVELS-1);
  const auto mask=queries->rawIntentMask(completed);
  for(int demand=0;demand<DemandCount;++demand) if(mask&(Uint64(1)<<unsigned(demandIntents[demand]))) buildingLevels[demand][kind.site][stage]++;
 }
	for (int bi=0; bi<AICastor::DemandCount; bi++)
		for (int si=0; si<2; si++)
		{
			int sum=0;
			for (int li=0; li<NB_UNIT_LEVELS; li++)
				sum+=buildingLevels[bi][si][li];
			buildingSum[bi][si]=sum;
		}

	for (int bi=0; bi<AICastor::DemandCount; bi++)
		for (int si=0; si<2; si++)
			for (int li=0; li<NB_UNIT_LEVELS; li++)
				if (buildingLevels[bi][si][li]>0)
					if ((timer&AI_CASTOR_VERBOSE_LOG_INTERVAL_MASK)==0)
						if (verbose)
							bufferedDiagnostics.push_back({"", "", "buildingLevels[" + std::to_string(bi) + "][" + std::to_string(si) + "][" + std::to_string(li) + "]=" + std::to_string(buildingLevels[bi][si][li]) + "\n"});
}

void AICastor::computeWarLevel()
{
	telemetry.count(AITrace::AI2::AICastor_computeWarLevel_calls);
	if (timer>strategy.warTimeTrigger)
	{
		warTimeTriggerLevel++;
		strategy.warTimeTrigger=strategy.warTimeTrigger+((AI_CASTOR_WARTIME_TRIGGER_GROWTH_BIAS+strategy.warTimeTrigger)>>AI_CASTOR_WARTIME_TRIGGER_GROWTH_SHIFT);
	}
	int warTimeTriggerLevelUse=warTimeTriggerLevel;
	if (warTimeTriggerLevelUse>AI_CASTOR_WARTIME_LEVEL_CAP)
		warTimeTriggerLevelUse=AI_CASTOR_WARTIME_LEVEL_CAP;

	int sum=0;
	for (int si=0; si<2; si++)
		for (int li=strategy.warLevelTrigger; li<NB_UNIT_LEVELS; li++)
			sum+=buildingLevels[AICastor::TrainAttack][si][li];
	if (sum>AI_CASTOR_WARLEVEL_BUILDINGS_HIGH)
		warLevelTriggerLevel=AI_CASTOR_WAR_LEVEL_HIGH;
	else if (sum>0)
		warLevelTriggerLevel=AI_CASTOR_WAR_LEVEL_MID;
	else
		warLevelTriggerLevel=0;

	if (buildsAmount>strategy.warAmountTrigger)
		warAmountTriggerLevel=AI_CASTOR_WAR_LEVEL_HIGH;
	else if (buildsAmount>=strategy.warAmountTrigger)
		warAmountTriggerLevel=AI_CASTOR_WAR_LEVEL_MID;
	else
		warAmountTriggerLevel=0;
	warLevel=warTimeTriggerLevelUse+warLevelTriggerLevel+warAmountTriggerLevel;

	if (warLevel==0)
		return;

	int warPowerSum=0;
	const auto& myUnits=observedTeam->myUnits;
	// Custom-game "glass cannon" rule: scale the same way
	// Unit::getRealAttackStrength() does, so this self-assessment of army
	// strength doesn't ignore a rule that's actively changing how hard these
	// units actually hit. (experienceLevel is deliberately left out, matching
	// getRealAttackStrength() -- this is scoped to the new rule, not a
	// broader change to how this heuristic already approximates strength.)
	const int glassCannonScale = observation->configuration->getGlassCannonScale();
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		const AIEngine::UnitView *u=myUnits[i];
		if (u && u->medical==Unit::MED_FREE && u->type==WARRIOR)
			warPowerSum+=u->performance[ATTACK_SPEED]*u->performance[ATTACK_STRENGTH]*glassCannonScale;
	}
	if (warPowerSum<strategy.strikeWarPowerTriggerDown)
	{
		if (onStrike)
		{
			strikeTeamSelected=false;
			onStrike=false;

			strikeTimeTrigger=timer+strategy.strikeTimeTrigger;
			strategy.strikeWarPowerTriggerUp=strategy.strikeWarPowerTriggerUp+strategy.strikeWarPowerTriggerUp/AI_CASTOR_STRIKE_TRIGGER_GROWTH_DIV;
		}
	}
	else if (timer>strikeTimeTrigger || warPowerSum>strategy.strikeWarPowerTriggerUp)
	{
		onStrike=true;
	}
}
