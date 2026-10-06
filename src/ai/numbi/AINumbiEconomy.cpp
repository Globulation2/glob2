#include "AIRuleOrders.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "AITelemetryFields.h"
#include "AINumbi.h"
#include "Game.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"
#include <algorithm>
#include <limits>

using std::shared_ptr;

int AINumbi::selectBuilding(Intent intent)
{
	const auto& index = game->buildingCapabilities();
	const auto& candidates = index.placements(intent);
	unsigned eligible = 0;
	for (const auto& candidate : candidates)
		if (index.available(candidate, intent, game->gameHeader)) ++eligible;
	if (!eligible) return -1;
	unsigned chosen = eligible == 1 ? 0 : random() % eligible;
	for (const auto& candidate : candidates)
		if (index.available(candidate, intent, game->gameHeader) && chosen-- == 0)
			return candidate.placementType;
	return -1;
}

bool AINumbi::provides(const Building& building, Intent intent) const
{
	const int complete = building.type->isBuildingSite ? building.type->nextLevel : building.typeNum;
	return game->buildingCapabilities().matches(complete, intent);
}

int AINumbi::estimateFood(Building *building)
{
	PERF_SCOPE_TIME(AIObserve);
	telemetry.count(AITrace::AI1::AINumbi_estimateFood_calls);
	int resource = -1;
	for (int r = 0; r < MaterialSlotCount && resource < 0; ++r)
		for (int unit = 0; unit < NB_UNIT_TYPE; ++unit)
			if (building->type->semantics.production.recipes[unit].enabled
				&& building->type->semantics.production.recipes[unit].cost[r] > 0)
				{ resource = r; break; }
	if (resource < 0) return std::numeric_limits<int>::max();
	int rx, ry, dist;
	bool found;
	if (map->materialAvailableUpdateSlot(team->teamNumber, resource, 0, building->posX-1, building->posY-1, &rx, &ry, &dist))
		found=true;
	else if (map->materialAvailableUpdateSlot(team->teamNumber, resource, 0, building->posX+building->type->width+1, building->posY-1, &rx, &ry, &dist))
		found=true;
	else if (map->materialAvailableUpdateSlot(team->teamNumber, resource, 0, building->posX+building->type->width+1, building->posY+building->type->height+1, &rx, &ry, &dist))
		found=true;
	else if (map->materialAvailableUpdateSlot(team->teamNumber, resource, 0, building->posX-1, building->posY+building->type->height+1, &rx, &ry, &dist))
		found=true;
	else
		found=false;

	if (found)
	{
		rx+=map->getW();
		ry+=map->getH();

		int w=0;
		int h=0;
		int i;
		int rxl, rxr, ryt, ryb;
		int hole;

		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isMaterialTakeableSlot(rx+i, ry, resource)||map->isMaterialTakeableSlot(rx+i, ry-1, resource))
				w++;
			else if (hole--<0)
				break;
		rxr=rx+i;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isMaterialTakeableSlot(rx-i, ry, resource)||map->isMaterialTakeableSlot(rx-i, ry-1, resource))
				w++;
			else if (hole--<0)
				break;
		rxl=rx-i;

		rx=((rxr+rxl)>>1);

		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isMaterialTakeableSlot(rx, ry+i, resource)||map->isMaterialTakeableSlot(rx-1, ry+i, resource))
				h++;
			else if (hole--<0)
				break;
		ryb=ry+i;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isMaterialTakeableSlot(rx, ry-i, resource)||map->isMaterialTakeableSlot(rx-1, ry-i, resource))
				h++;
			else if (hole--<0)
				break;
		ryt=ry-i;

		ry=((ryb+ryt)>>1);


		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isMaterialTakeableSlot(rx, ry+i, resource)||map->isMaterialTakeableSlot(rx+1, ry+i, resource))
				h++;
			else if (hole--<0)
				break;
		ryb=ry+i;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isMaterialTakeableSlot(rx, ry-i, resource)||map->isMaterialTakeableSlot(rx+1, ry-i, resource))
				h++;
			else if (hole--<0)
				break;
		ryt=ry-i;

		ry=((ryt+ryb)>>1);
		w=0;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isMaterialTakeableSlot(rx+i, ry, resource)||map->isMaterialTakeableSlot(rx+i, ry+1, resource))
				w++;
			else if (hole--<0)
				break;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isMaterialTakeableSlot(rx-i, ry, resource)||map->isMaterialTakeableSlot(rx-i, ry+1, resource))
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
	const int warriors=game->gameHeader.isPeacefulModeEnabled() ? 0 : requestedWarriors;
	telemetry.set(AITrace::AI1::AINumbi_swarmsForWorkers_input_warriors, warriors);
	telemetry.set(AITrace::AI1::AINumbi_swarmsForWorkers_input_explorers, explorers);
	telemetry.set(AITrace::AI1::AINumbi_swarmsForWorkers_input_workers, workers);
	telemetry.set(AITrace::AI1::AINumbi_swarmsForWorkers_input_nbWorkersFactor, nbWorkersFactor);
	telemetry.set(AITrace::AI1::AINumbi_swarmsForWorkers_input_minSwarmNumbers, minSwarmNumbers);
	telemetry.count(AITrace::AI1::AINumbi_swarmsForWorkers_calls);
	std::list<Building *> swarms=team->swarms;
	int ss=swarms.size();
	Sint32 numberRequested=1+(nbWorkersFactor/(ss+1));
	int nbu=countUnits();
	if(auto order=AIPlanning::missingProductionOrder(*game,*team,{workers,explorers,warriors},numberRequested,numberRequested)) return order;

	for (std::list<Building *>::iterator it=swarms.begin(); it!=swarms.end(); ++it)
	{
		Building *b=*it;
		const Sint32 desired[NB_UNIT_TYPE] = {workers, explorers, warriors};
		Sint32 newRatio[NB_UNIT_TYPE]{};
		bool changed = false;
		for (int unit = 0; unit < NB_UNIT_TYPE; ++unit)
		{
			newRatio[unit] = b->type->semantics.production.recipes[unit].enabled ? desired[unit] : 0;
			changed |= b->ratio[unit] != newRatio[unit];
		}
		if (changed)
			return telemetry.returnedOrder(AITrace::AI1::AINumbi_swarmsForWorkers_result,
				std::make_shared<OrderModifySwarm>(b->gid, newRatio));

		int f=estimateFood(b);
		int numberRequestedTemp=std::min<int>(numberRequested, b->type->semantics.assignmentLimit);
		int numberRequestedLocal=b->maxUnitWorking;
		if (f<(nbu*AI_NUMBI_LOW_FOOD_PER_UNIT-1))
			numberRequestedTemp=0;
		else if (numberRequestedLocal==0)
			if (f<(nbu*AI_NUMBI_HIGH_FOOD_PER_UNIT+1))
				numberRequestedTemp=0;

		if (b->type->semantics.feeding.enabled || b->type->semantics.healing.enabled)
			numberRequestedTemp=std::max(numberRequestedTemp, numberRequestedLocal);
		if (numberRequestedLocal!=numberRequestedTemp)
		{
			return telemetry.returnedOrder(
				AITrace::AI1::AINumbi_swarmsForWorkers_result,
				shared_ptr<Order>(new OrderModifyBuilding(b->gid, numberRequestedTemp)));
		}
	}
	return telemetry.returnedOrder(AITrace::AI1::AINumbi_swarmsForWorkers_result,
								   shared_ptr<Order>(new NullOrder));
}

std::shared_ptr<Order>AINumbi::adjustBuildings(const int numbers, const int numbersInc, const int workers, Intent intent)
{
	if (!AIPlanning::BuildingCapabilityIndex::allowed(intent, game->gameHeader))
		return std::make_shared<NullOrder>();
	telemetry.set(AITrace::AI1::AINumbi_adjustBuildings_input_buildingType, static_cast<int>(intent));
	telemetry.set(AITrace::AI1::AINumbi_adjustBuildings_input_workers, workers);
	telemetry.set(AITrace::AI1::AINumbi_adjustBuildings_input_numbersInc, numbersInc);
	telemetry.set(AITrace::AI1::AINumbi_adjustBuildings_input_numbers, numbers);
	telemetry.count(AITrace::AI1::AINumbi_adjustBuildings_calls);
	Building **myBuildings=team->myBuildings;
	//Unit **myUnits=player->team->myUnits;
	int fb=0;

	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b=myBuildings[i];
		if ((b)&&provides(*b, intent))
		{
			fb++;
			int w=std::min<int>(workers, b->type->semantics.assignmentLimit);
			const auto& semantic = b->type->semantics;
			const bool mixed = (semantic.feeding.enabled && intent != Intent::Feed)
				|| (semantic.healing.enabled && intent != Intent::Heal)
				|| std::any_of(semantic.production.recipes.begin(), semantic.production.recipes.end(), [](const auto& r) { return r.enabled; });
			if (mixed) w=std::max(w, b->maxUnitWorking);
			if ((b->maxUnitWorking != w)&&(b->type->maxUnitWorking))
				return telemetry.returnedOrder(
					AITrace::AI1::AINumbi_adjustBuildings_result,
					shared_ptr<Order>(new OrderModifyBuilding(b->gid, w)));
		}
	}

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
			int teamNumber=team->teamNumber;
			return telemetry.returnedOrder(
				AITrace::AI1::AINumbi_adjustBuildings_result,
				AIRules::createOrder(*game, teamNumber, x, y, typeNum,
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

	Building **myBuildings=team->myBuildings;
	int ss=0;
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b=myBuildings[i];
		if ((b)&&provides(*b, Intent::ProduceWorker))
			ss++;
	}

	int wr=countUnits();

	if (ss<=(wr/numbers))
	{
		//printf("AI: checkoutExpands(%d<%d=(%d/%d)).\n", ss, (wr/numbers), wr, numbers);
		int x, y;
		const int typeNum = selectBuilding(Intent::ProduceWorker);
		if (typeNum >= 0 && findNewEmplacement(Intent::ProduceWorker, typeNum, &x, &y))
		{
			int teamNumber=team->teamNumber;
			return telemetry.returnedOrder(
				AITrace::AI1::AINumbi_checkoutExpands_result,
				AIRules::createOrder(*game, teamNumber, x, y, typeNum,
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
