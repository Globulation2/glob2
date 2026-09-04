// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière


#include "AINumbi.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"

using std::shared_ptr;

int AINumbi::estimateFood(Building *building)
{
	int rx, ry, dist;
	bool found;
	if (map->ressourceAvailableUpdate(team->teamNumber, CORN, 0, building->posX-1, building->posY-1, &rx, &ry, &dist))
		found=true;
	else if (map->ressourceAvailableUpdate(team->teamNumber, CORN, 0, building->posX+building->type->width+1, building->posY-1, &rx, &ry, &dist))
		found=true;
	else if (map->ressourceAvailableUpdate(team->teamNumber, CORN, 0, building->posX+building->type->width+1, building->posY+building->type->height+1, &rx, &ry, &dist))
		found=true;
	else if (map->ressourceAvailableUpdate(team->teamNumber, CORN, 0, building->posX-1, building->posY+building->type->height+1, &rx, &ry, &dist))
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

		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx+i, ry, CORN)||map->isRessourceTakeable(rx+i, ry-1, CORN))
				w++;
			else if (hole--<0)
				break;
		rxr=rx+i;
		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx-i, ry, CORN)||map->isRessourceTakeable(rx-i, ry-1, CORN))
				w++;
			else if (hole--<0)
				break;
		rxl=rx-i;

		rx=((rxr+rxl)>>1);

		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx, ry+i, CORN)||map->isRessourceTakeable(rx-1, ry+i, CORN))
				h++;
			else if (hole--<0)
				break;
		ryb=ry+i;
		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx, ry-i, CORN)||map->isRessourceTakeable(rx-1, ry-i, CORN))
				h++;
			else if (hole--<0)
				break;
		ryt=ry-i;

		ry=((ryb+ryt)>>1);


		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx, ry+i, CORN)||map->isRessourceTakeable(rx+1, ry+i, CORN))
				h++;
			else if (hole--<0)
				break;
		ryb=ry+i;
		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx, ry-i, CORN)||map->isRessourceTakeable(rx+1, ry-i, CORN))
				h++;
			else if (hole--<0)
				break;
		ryt=ry-i;

		ry=((ryt+ryb)>>1);
		w=0;
		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx+i, ry, CORN)||map->isRessourceTakeable(rx+i, ry+1, CORN))
				w++;
			else if (hole--<0)
				break;
		hole=AI_NUMBI_CORN_SCAN_HOLE_TOLERANCE;
		for (i=0; i<AI_NUMBI_CORN_SCAN_MAX_RADIUS; i++)
			if (map->isRessourceTakeable(rx-i, ry, CORN)||map->isRessourceTakeable(rx-i, ry+1, CORN))
				w++;
			else if (hole--<0)
				break;

		//printf("r=(%d, %d), w=%d, h=%d, s=%d.\n", rx, ry, w, h, w*h);

		return (w*h);
	}
	else
		return 0;
}

std::shared_ptr<Order>AINumbi::swarmsForWorkers(const int minSwarmNumbers, const int nbWorkersFator, const int workers, const int explorers, const int warriors)
{
	std::list<Building *> swarms=team->swarms;
	int ss=swarms.size();
	Sint32 numberRequested=1+(nbWorkersFator/(ss+1));
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
			return shared_ptr<Order>(new OrderModifySwarm(b->gid, newRatio));
		}

		int f=estimateFood(b);
		int numberRequestedTemp=numberRequested;
		int numberRequestedLoca=b->maxUnitWorking;
		if (f<(nbu*AI_NUMBI_LOW_FOOD_PER_UNIT-1))
			numberRequestedTemp=0;
		else if (numberRequestedLoca==0)
			if (f<(nbu*AI_NUMBI_HIGH_FOOD_PER_UNIT+1))
				numberRequestedTemp=0;

		if (numberRequestedLoca!=numberRequestedTemp)
		{
			//printf("AI: (%d) numberRequested changed to (nrt=%d) (nrl=%d)(f=%d) (nbu=%d).\n", b->UID, numberRequestedTemp, numberRequestedLoca, f, nbu);
			return shared_ptr<Order>(new OrderModifyBuilding(b->gid, numberRequestedTemp));
		}
	}
	if (ss<minSwarmNumbers)
	{
		//printf("AI: not enough swarms (%d<%d).\n", ss, minSwarmNumbers);
		// TODO !
		// assert(false);
		/*int x, y;
		if (findNewEmplacement(IntBuildingType::SWARM_BUILDING, &x, &y))
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, true);
			int teamNumber=player->team->teamNumber;
			return shared_ptr<Order>(new OrderCreate(teamNumber, x, y, typeNum));
		}*/
	}
	return shared_ptr<Order>(new NullOrder);
}

std::shared_ptr<Order>AINumbi::adjustBuildings(const int numbers, const int numbersInc, const int workers, const int buildingType)
{
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
				return shared_ptr<Order>(new OrderModifyBuilding(b->gid, w));
		}
	}

	int wr=countUnits();

	if (buildingType==IntBuildingType::FOOD_BUILDING)
		wr+=AI_NUMBI_HUNGRY_INN_DEMAND_MULT*countUnits(Unit::MED_HUNGRY);
	else if (buildingType==IntBuildingType::HEAL_BUILDING)
		wr+=AI_NUMBI_DAMAGED_HEAL_DEMAND_MULT*countUnits(Unit::MED_DAMAGED);

	if (fb<((wr/numbers)+numbersInc))
	{
		//printf("AI: findNewEmplacement(%d), fb=%d, wr=%d, numbers=%d, numbersInc=%d, nn=%d.\n", buildingType, fb, wr, numbers, numbersInc, ((wr/numbers)+numbersInc));
		int x, y;
		if (findNewEmplacement(buildingType, &x, &y))
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::typeFromShortNumber(buildingType), 0, true);
			int teamNumber=team->teamNumber;
			return shared_ptr<Order>(new OrderCreate(teamNumber, x, y, typeNum, AI_NUMBI_BUILD_ORDER_UNITS_WORKING, AI_NUMBI_BUILD_ORDER_FLAG_RADIUS));
		}
		//printf("AI: findNewEmplacement(%d) failed.\n", buildingType);
		return shared_ptr<Order>(new NullOrder);
	}
	else
		return shared_ptr<Order>(new NullOrder);
}

std::shared_ptr<Order>AINumbi::checkoutExpands(const int numbers, const int workers)
{
	//std::list<Building *> swarms=team->swarms;
	//int ss=swarms.size();

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
			return shared_ptr<Order>(new OrderCreate(teamNumber, x, y, typeNum, AI_NUMBI_BUILD_ORDER_UNITS_WORKING, AI_NUMBI_BUILD_ORDER_FLAG_RADIUS));
		}
		return shared_ptr<Order>(new NullOrder);
	}
	else
		return shared_ptr<Order>(new NullOrder);
}
