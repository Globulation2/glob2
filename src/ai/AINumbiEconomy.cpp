// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "AITelemetryFields.h"
#include "AINumbi.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"

using std::shared_ptr;

int AINumbi::estimateFood(Building *building)
{
	PERF_SCOPE_TIME(AIObserve);
	telemetry.count(AITrace::AI1::AINumbi_estimateFood_calls);
	int rx, ry, dist;
	bool found;
	if (map->resourceAvailableUpdate(team->teamNumber, WHEAT, 0, building->posX-1, building->posY-1, &rx, &ry, &dist))
		found=true;
	else if (map->resourceAvailableUpdate(team->teamNumber, WHEAT, 0, building->posX+building->type->width+1, building->posY-1, &rx, &ry, &dist))
		found=true;
	else if (map->resourceAvailableUpdate(team->teamNumber, WHEAT, 0, building->posX+building->type->width+1, building->posY+building->type->height+1, &rx, &ry, &dist))
		found=true;
	else if (map->resourceAvailableUpdate(team->teamNumber, WHEAT, 0, building->posX-1, building->posY+building->type->height+1, &rx, &ry, &dist))
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
			if (map->isResourceTakeable(rx+i, ry, WHEAT)||map->isResourceTakeable(rx+i, ry-1, WHEAT))
				w++;
			else if (hole--<0)
				break;
		rxr=rx+i;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isResourceTakeable(rx-i, ry, WHEAT)||map->isResourceTakeable(rx-i, ry-1, WHEAT))
				w++;
			else if (hole--<0)
				break;
		rxl=rx-i;

		rx=((rxr+rxl)>>1);

		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isResourceTakeable(rx, ry+i, WHEAT)||map->isResourceTakeable(rx-1, ry+i, WHEAT))
				h++;
			else if (hole--<0)
				break;
		ryb=ry+i;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isResourceTakeable(rx, ry-i, WHEAT)||map->isResourceTakeable(rx-1, ry-i, WHEAT))
				h++;
			else if (hole--<0)
				break;
		ryt=ry-i;

		ry=((ryb+ryt)>>1);


		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isResourceTakeable(rx, ry+i, WHEAT)||map->isResourceTakeable(rx+1, ry+i, WHEAT))
				h++;
			else if (hole--<0)
				break;
		ryb=ry+i;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isResourceTakeable(rx, ry-i, WHEAT)||map->isResourceTakeable(rx+1, ry-i, WHEAT))
				h++;
			else if (hole--<0)
				break;
		ryt=ry-i;

		ry=((ryt+ryb)>>1);
		w=0;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isResourceTakeable(rx+i, ry, WHEAT)||map->isResourceTakeable(rx+i, ry+1, WHEAT))
				w++;
			else if (hole--<0)
				break;
		hole=AI_NUMBI_WHEAT_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_WHEAT_SCAN_MAX_RADIUS; i++)
			if (map->isResourceTakeable(rx-i, ry, WHEAT)||map->isResourceTakeable(rx-i, ry+1, WHEAT))
				w++;
			else if (hole--<0)
				break;

		//printf("r=(%d, %d), w=%d, h=%d, s=%d.\n", rx, ry, w, h, w*h);

		return telemetry.returnedInt(AITrace::AI1::AINumbi_estimateFood_result, (w * h));
	}
	else
		return telemetry.returnedInt(AITrace::AI1::AINumbi_estimateFood_result, 0);
}

std::shared_ptr<Order>AINumbi::swarmsForWorkers(const int minSwarmNumbers, const int nbWorkersFactor, const int workers, const int explorers, const int warriors)
{
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

	for (std::list<Building *>::iterator it=swarms.begin(); it!=swarms.end(); ++it)
	{
		Building *b=*it;
		if ((b->ratio[WORKER]!=workers)||(b->ratio[EXPLORER]!=explorers)||(b->ratio[WARRIOR]!=warriors))
		{
			// Stack buffer for the order payload — the per-viewer GUI shadow
			// that used to back this lives in BuildingGuiState now and is
			// off-limits to AI code.
			Sint32 newRatio[NB_UNIT_TYPE];
			newRatio[WORKER]=workers;
			newRatio[EXPLORER]=explorers;
			newRatio[WARRIOR]=warriors;
			return telemetry.returnedOrder(
				AITrace::AI1::AINumbi_swarmsForWorkers_result,
				shared_ptr<Order>(new OrderModifySwarm(b->gid, newRatio)));
		}

		int f=estimateFood(b);
		int numberRequestedTemp=numberRequested;
		int numberRequestedLocal=b->maxUnitWorking;
		if (f<(nbu*AI_NUMBI_LOW_FOOD_PER_UNIT-1))
			numberRequestedTemp=0;
		else if (numberRequestedLocal==0)
			if (f<(nbu*AI_NUMBI_HIGH_FOOD_PER_UNIT+1))
				numberRequestedTemp=0;

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

std::shared_ptr<Order>AINumbi::adjustBuildings(const int numbers, const int numbersInc, const int workers, const int buildingType)
{
	telemetry.set(AITrace::AI1::AINumbi_adjustBuildings_input_buildingType, buildingType);
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
		if ((b)&&(b->type->shortTypeNum==buildingType))
		{
			fb++;
			int w=workers;
			if ((b->maxUnitWorking!=w)&&(b->type->maxUnitWorking))
				return telemetry.returnedOrder(
					AITrace::AI1::AINumbi_adjustBuildings_result,
					shared_ptr<Order>(new OrderModifyBuilding(b->gid, w)));
		}
	}

	int wr=countUnits();

	if (buildingType==IntBuildingType::FOOD_BUILDING)
		wr+=AI_NUMBI_HUNGRY_INN_DEMAND_MULT*countUnits(Unit::MED_HUNGRY);
	else if (buildingType==IntBuildingType::HEAL_BUILDING)
		wr+=AI_NUMBI_DAMAGED_HEAL_DEMAND_MULT*countUnits(Unit::MED_DAMAGED);

	if (fb<((wr/numbers)+numbersInc))
	{
		int x, y;
		if (findNewEmplacement(buildingType, &x, &y))
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::typeFromShortNumber(buildingType), 0, true);
			int teamNumber=team->teamNumber;
			return telemetry.returnedOrder(
				AITrace::AI1::AINumbi_adjustBuildings_result,
				shared_ptr<Order>(new OrderCreate(teamNumber, x, y, typeNum,
												  AI_NUMBI_BUILD_ORDER_UNITS_WORKING,
												  AI_NUMBI_BUILD_ORDER_FLAG_RADIUS)));
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
		if ((b)&&(b->type->shortTypeNum==0))
			ss++;
	}

	int wr=countUnits();

	if (ss<=(wr/numbers))
	{
		//printf("AI: checkoutExpands(%d<%d=(%d/%d)).\n", ss, (wr/numbers), wr, numbers);
		int x, y;
		if (findNewEmplacement(IntBuildingType::SWARM_BUILDING, &x, &y))
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, true);
			int teamNumber=team->teamNumber;
			return telemetry.returnedOrder(
				AITrace::AI1::AINumbi_checkoutExpands_result,
				shared_ptr<Order>(new OrderCreate(teamNumber, x, y, typeNum,
												  AI_NUMBI_BUILD_ORDER_UNITS_WORKING,
												  AI_NUMBI_BUILD_ORDER_FLAG_RADIUS)));
		}
		return telemetry.returnedOrder(AITrace::AI1::AINumbi_checkoutExpands_result,
									   shared_ptr<Order>(new NullOrder));
	}
	else
		return telemetry.returnedOrder(AITrace::AI1::AINumbi_checkoutExpands_result,
									   shared_ptr<Order>(new NullOrder));
}
