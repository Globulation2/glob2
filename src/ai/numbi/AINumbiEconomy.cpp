#include "AIRuleOrders.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "AITelemetryFields.h"
#include "AINumbi.h"
#include "NumbiQueries.h"
#include "ai/engine/AIDecision.h"
#include "Game.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"
#include <algorithm>
#include <limits>

using std::shared_ptr;

int AINumbi::selectBuilding(Intent intent)
{
	const auto& candidates = queries->placements(intent);
	unsigned eligible = 0;
	for (const auto& candidate : candidates)
		if (queries->available(candidate, intent)) ++eligible;
	if (!eligible) return -1;
	unsigned chosen = eligible == 1 ? 0 : random() % eligible;
	for (const auto& candidate : candidates)
		if (queries->available(candidate, intent) && chosen-- == 0)
			return candidate.placementType;
	return -1;
}

bool AINumbi::provides(const AIEngine::BuildingView& building, Intent intent) const
{
 return queries->provides(building,intent);
}

int AINumbi::estimateFood(const AIEngine::BuildingView *building)
{
	PERF_SCOPE_TIME(AIObserve);
	telemetry.count(AITrace::AI1::AINumbi_estimateFood_calls);
	int resource = -1;
	for (int r = 0; r < MAX_NB_RESOURCES && resource < 0; ++r)
		for (int unit = 0; unit < NB_UNIT_TYPE; ++unit)
			if (queries->kind(*building).semantics.production.recipes[unit].enabled
				&& queries->kind(*building).semantics.production.recipes[unit].cost[r] > 0)
				{ resource = r; break; }
	if (resource < 0) return std::numeric_limits<int>::max();
	int rx, ry, dist;
	bool found;
	if (queries->resourceAvailableUpdate(teamNumber, resource, 0, building->posX-1, building->posY-1, &rx, &ry, &dist))
		found=true;
	else if (queries->resourceAvailableUpdate(teamNumber, resource, 0, building->posX+queries->kind(*building).width+1, building->posY-1, &rx, &ry, &dist))
		found=true;
	else if (queries->resourceAvailableUpdate(teamNumber, resource, 0, building->posX+queries->kind(*building).width+1, building->posY+queries->kind(*building).height+1, &rx, &ry, &dist))
		found=true;
	else if (queries->resourceAvailableUpdate(teamNumber, resource, 0, building->posX-1, building->posY+queries->kind(*building).height+1, &rx, &ry, &dist))
		found=true;
	else
		found=false;

	if (found)
	{
		rx+=observation->width;
		ry+=observation->height;

		int w=0;
		int h=0;
		int i;
		int rxl, rxr, ryt, ryb;
		int hole;

		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (queries->isResourceTakeable(rx+i, ry, resource)||queries->isResourceTakeable(rx+i, ry-1, resource))
				w++;
			else if (hole--<0)
				break;
		rxr=rx+i;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (queries->isResourceTakeable(rx-i, ry, resource)||queries->isResourceTakeable(rx-i, ry-1, resource))
				w++;
			else if (hole--<0)
				break;
		rxl=rx-i;

		rx=((rxr+rxl)>>1);

		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (queries->isResourceTakeable(rx, ry+i, resource)||queries->isResourceTakeable(rx-1, ry+i, resource))
				h++;
			else if (hole--<0)
				break;
		ryb=ry+i;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (queries->isResourceTakeable(rx, ry-i, resource)||queries->isResourceTakeable(rx-1, ry-i, resource))
				h++;
			else if (hole--<0)
				break;
		ryt=ry-i;

		ry=((ryb+ryt)>>1);


		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (queries->isResourceTakeable(rx, ry+i, resource)||queries->isResourceTakeable(rx+1, ry+i, resource))
				h++;
			else if (hole--<0)
				break;
		ryb=ry+i;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (queries->isResourceTakeable(rx, ry-i, resource)||queries->isResourceTakeable(rx+1, ry-i, resource))
				h++;
			else if (hole--<0)
				break;
		ryt=ry-i;

		ry=((ryt+ryb)>>1);
		w=0;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (queries->isResourceTakeable(rx+i, ry, resource)||queries->isResourceTakeable(rx+i, ry+1, resource))
				w++;
			else if (hole--<0)
				break;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (queries->isResourceTakeable(rx-i, ry, resource)||queries->isResourceTakeable(rx-i, ry+1, resource))
				w++;
			else if (hole--<0)
				break;

		//printf("r=(%d, %d), w=%d, h=%d, s=%d.\n", rx, ry, w, h, w*h);

		return telemetry.returnedInt(AITrace::AI1::AINumbi_estimateFood_result, (w * h));
	}
	else
		return telemetry.returnedInt(AITrace::AI1::AINumbi_estimateFood_result, 0);
}

std::shared_ptr<Order>AINumbi::swarmsForWorkers(const int minSwarmNumbers, const int nbWorkersFactor, const int workers, const int explorers, const int requestedWarriors)
{
	const int warriors=observation->rules.peaceful ? 0 : requestedWarriors;
	telemetry.set(AITrace::AI1::AINumbi_swarmsForWorkers_input_warriors, warriors);
	telemetry.set(AITrace::AI1::AINumbi_swarmsForWorkers_input_explorers, explorers);
	telemetry.set(AITrace::AI1::AINumbi_swarmsForWorkers_input_workers, workers);
	telemetry.set(AITrace::AI1::AINumbi_swarmsForWorkers_input_nbWorkersFactor, nbWorkersFactor);
	telemetry.set(AITrace::AI1::AINumbi_swarmsForWorkers_input_minSwarmNumbers, minSwarmNumbers);
	telemetry.count(AITrace::AI1::AINumbi_swarmsForWorkers_calls);
	std::vector<const AIEngine::BuildingView*> swarms;
 for(const auto identity:observation->teams[teamNumber].swarms)
  if(const auto* building=observation->building(identity)) swarms.push_back(building);
	int ss=swarms.size();
	Sint32 numberRequested=1+(nbWorkersFactor/(ss+1));
	int nbu=countUnits();
	if(auto order=queries->missingProductionOrder({workers,explorers,warriors},numberRequested,numberRequested)) return order;

	for (std::vector<const AIEngine::BuildingView*>::iterator it=swarms.begin(); it!=swarms.end(); ++it)
	{
		const AIEngine::BuildingView *b=*it;
		const Sint32 desired[NB_UNIT_TYPE] = {workers, explorers, warriors};
		Sint32 newRatio[NB_UNIT_TYPE]{};
		bool changed = false;
		for (int unit = 0; unit < NB_UNIT_TYPE; ++unit)
		{
			newRatio[unit] = queries->kind(*b).semantics.production.recipes[unit].enabled ? desired[unit] : 0;
			changed |= requestedRatio(*b,unit) != newRatio[unit];
		}
		if (changed)
			return telemetry.returnedOrder(AITrace::AI1::AINumbi_swarmsForWorkers_result,
				std::make_shared<OrderModifySwarm>(b->identity.gid, newRatio));

		int f=estimateFood(b);
		int numberRequestedTemp=std::min<int>(numberRequested, queries->kind(*b).semantics.assignmentLimit);
		int numberRequestedLocal=requestedWorkers(*b);
		if (f<(nbu*AI_NUMBI_LOW_FOOD_PER_UNIT-1))
			numberRequestedTemp=0;
		else if (numberRequestedLocal==0)
			if (f<(nbu*AI_NUMBI_HIGH_FOOD_PER_UNIT+1))
				numberRequestedTemp=0;

		if (queries->kind(*b).semantics.feeding.enabled || queries->kind(*b).semantics.healing.enabled)
			numberRequestedTemp=std::max(numberRequestedTemp, numberRequestedLocal);
		if (numberRequestedLocal!=numberRequestedTemp)
		{
			return telemetry.returnedOrder(
				AITrace::AI1::AINumbi_swarmsForWorkers_result,
				shared_ptr<Order>(new OrderModifyBuilding(b->identity.gid, numberRequestedTemp)));
		}
	}
	return telemetry.returnedOrder(AITrace::AI1::AINumbi_swarmsForWorkers_result,
								   shared_ptr<Order>(new NullOrder));
}

std::shared_ptr<Order>AINumbi::adjustBuildings(const int numbers, const int numbersInc, const int workers, Intent intent)
{
	if (!queries->allowed(intent))
		return std::make_shared<NullOrder>();
	telemetry.set(AITrace::AI1::AINumbi_adjustBuildings_input_buildingType, static_cast<int>(intent));
	telemetry.set(AITrace::AI1::AINumbi_adjustBuildings_input_workers, workers);
	telemetry.set(AITrace::AI1::AINumbi_adjustBuildings_input_numbersInc, numbersInc);
	telemetry.set(AITrace::AI1::AINumbi_adjustBuildings_input_numbers, numbers);
	telemetry.count(AITrace::AI1::AINumbi_adjustBuildings_calls);
	const auto myBuildings=observation->buildingSlots(teamNumber);
	//Unit **myUnits=player->team->myUnits;
	int fb=0;

	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		const AIEngine::BuildingView *b=myBuildings[i];
		if ((b)&&provides(*b, intent))
		{
			fb++;
			int w=std::min<int>(workers, queries->kind(*b).semantics.assignmentLimit);
			const auto& semantic = queries->kind(*b).semantics;
			const bool mixed = (semantic.feeding.enabled && intent != Intent::Feed)
				|| (semantic.healing.enabled && intent != Intent::Heal)
				|| std::any_of(semantic.production.recipes.begin(), semantic.production.recipes.end(), [](const auto& r) { return r.enabled; });
			if (mixed) w=std::max(w, requestedWorkers(*b));
			if ((requestedWorkers(*b) != w)&&(queries->kind(*b).maximumWorkers))
				return telemetry.returnedOrder(
					AITrace::AI1::AINumbi_adjustBuildings_result,
					shared_ptr<Order>(new OrderModifyBuilding(b->identity.gid, w)));
		}
	}

	fb+=pendingBuildings(intent);
	int wr=countUnits();

	if (intent==Intent::Feed)
		wr+=AI_NUMBI_HUNGRY_INN_DEMAND_MULT*countUnits(Unit::MED_HUNGRY);
	else if (intent==Intent::Heal)
		wr+=AI_NUMBI_DAMAGED_HEAL_DEMAND_MULT*countUnits(Unit::MED_DAMAGED);

	if (fb<((wr/numbers)+numbersInc))
	{
		int x, y;
		const int typeNum = selectBuilding(intent);
		if (typeNum >= 0 && findNewEmplacement(intent, typeNum, &x, &y))
		{

			return telemetry.returnedOrder(
				AITrace::AI1::AINumbi_adjustBuildings_result,
				queries->createOrder( teamNumber, x, y, typeNum,
												  AI_NUMBI_BUILD_ORDER_UNITS_WORKING,
												  AI_NUMBI_BUILD_ORDER_FLAG_RADIUS));
		}
		return telemetry.returnedOrder(AITrace::AI1::AINumbi_adjustBuildings_result,
									   shared_ptr<Order>(new NullOrder));
	}
	else
		return telemetry.returnedOrder(AITrace::AI1::AINumbi_adjustBuildings_result,
									   shared_ptr<Order>(new NullOrder));
}

std::shared_ptr<Order>AINumbi::checkoutExpands(const int numbers, const int workers)
{
	telemetry.set(AITrace::AI1::AINumbi_checkoutExpands_input_workers, workers);
	telemetry.set(AITrace::AI1::AINumbi_checkoutExpands_input_numbers, numbers);
	telemetry.count(AITrace::AI1::AINumbi_checkoutExpands_calls);

	const auto myBuildings=observation->buildingSlots(teamNumber);
	int ss=0;
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		const AIEngine::BuildingView *b=myBuildings[i];
		if ((b)&&provides(*b, Intent::ProduceWorker))
			ss++;
	}

	ss+=pendingBuildings(Intent::ProduceWorker);
	int wr=countUnits();

	if (ss<=(wr/numbers))
	{
		//printf("AI: checkoutExpands(%d<%d=(%d/%d)).\n", ss, (wr/numbers), wr, numbers);
		int x, y;
		const int typeNum = selectBuilding(Intent::ProduceWorker);
		if (typeNum >= 0 && findNewEmplacement(Intent::ProduceWorker, typeNum, &x, &y))
		{

			return telemetry.returnedOrder(
				AITrace::AI1::AINumbi_checkoutExpands_result,
				queries->createOrder( teamNumber, x, y, typeNum,
												  AI_NUMBI_BUILD_ORDER_UNITS_WORKING,
												  AI_NUMBI_BUILD_ORDER_FLAG_RADIUS));
		}
		return telemetry.returnedOrder(AITrace::AI1::AINumbi_checkoutExpands_result,
									   shared_ptr<Order>(new NullOrder));
	}
	else
		return telemetry.returnedOrder(AITrace::AI1::AINumbi_checkoutExpands_result,
									   shared_ptr<Order>(new NullOrder));
}
