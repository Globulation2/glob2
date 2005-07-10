 /*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  Copyright (C) 2005 Eli Dupree
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "AIWarrush.h"
#include "Building.h"
#include "Unit.h"
#include "Building.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Brush.h"

#define BUILDING_DELAY 20

void AIWarrush::init(Player *player)
{
	assert(player);
	
	this->player=player;
	this->team=player->team;
	this->game=player->game;
	this->map=player->map;
	buildingDelay = 0;
	
	assert(this->team);
	assert(this->game);
	assert(this->map);
}

AIWarrush::AIWarrush(Player *player)
{
	init(player);
}

AIWarrush::AIWarrush(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	init(player);
}

AIWarrush::~AIWarrush()
{
}

bool AIWarrush::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	init(player);
	return true;
}

void AIWarrush::save(GAGCore::OutputStream *stream)
{
}

/*
int AIWarrush::numberOfBuildingsOfType(const int buildingType)const
{
	Building **myBuildings=team->myBuildings;
	int count = 0;
	for (int i=0; i<1024; i++)
	{
		Building *b=myBuildings[i];
		if ((b) && (b->type->shortTypeNum==buildingType))
		{
			count++;
		}
	}
	return count;
}
*/

int AIWarrush::numberOfUnitsWithSkillGreaterThanValue(const int skill, const int value)const
{
	Unit **myUnits=team->myUnits;
	int count = 0;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if ((u)&&(u->performance[skill]>value))
		{
			count++;
		}
	}
	return count;
}

bool AIWarrush::isAnyUnitWithLessThanOneThirdFood()const
{
	Unit **myUnits=team->myUnits;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if ((u)&&(u->hungry<(Unit::HUNGRY_MAX/3)))
		{
			return true;
		}
	}
	return false;
}

Building *AIWarrush::getSwarmWithoutSettings(const int workerRatio, const int explorerRatio, const int warriorRatio)const
{
	Building **myBuildings=team->myBuildings;
	for (int i=0; i<1024; i++)
	{
		Building *b=myBuildings[i];
		if (	(b)
				&& (b->type->shortTypeNum==IntBuildingType::SWARM_BUILDING)
				&& ((b->ratio[0] != workerRatio) || (b->ratio[1] != explorerRatio) || (b->ratio[2] != warriorRatio))
					)
		{
			return b;
		}
	}
	return NULL;
}

Building *AIWarrush::getSwarmWithLeastProduction()const
{
	Building **myBuildings=team->myBuildings;
	int min = 10000;
	Building *min_swarm = NULL;
	for (int i=0; i<1024; i++)
	{
		Building *b=myBuildings[i];
		if ((b) && (b->type->shortTypeNum==IntBuildingType::SWARM_BUILDING))
		{
			if(b->maxUnitWorking < min)
			{
				min = b->maxUnitWorking;
				min_swarm = b;
			}
		}
	}
	return min_swarm;
}

Building *AIWarrush::getSwarmWithMostProduction()const
{
	Building **myBuildings=team->myBuildings;
	int max = -10000;
	Building *max_swarm = NULL;
	for (int i=0; i<1024; i++)
	{
		Building *b=myBuildings[i];
		if ((b) && (b->type->shortTypeNum==IntBuildingType::SWARM_BUILDING))
		{
			if(b->maxUnitWorking >= max)
			{
				max = b->maxUnitWorking;
				max_swarm = b;
			}
		}
	}
	return max_swarm;
}

int AIWarrush::numberOfJobsForWorkers()const
{
	Building **myBuildings=team->myBuildings;
	int count = 0;
	for (int i=0; i<1024; i++)
	{
		Building *b=myBuildings[i];
		if (b)
		{
			count+=b->maxUnitWorking;
		}
	}
	return count;
}

int AIWarrush::numberOfIdleLevel1Warriors()const
{
	Unit **myUnits=team->myUnits;
	int count = 0;
	for (int i=0; i<1024; i++)
	{
		Unit *u=myUnits[i];
		if ((u) && (u->performance[ATTACK_SPEED]==1) && (u->activity==Unit::ACT_RANDOM))
		{
			count++;
		}
	}
	return count;
}

bool AIWarrush::allBarracksAreCompletedAndFull()const
{
	Building **myBuildings=team->myBuildings;
	for (int i=0; i<1024; i++)
	{
		Building *b=myBuildings[i];
		if (
				(b)
				&& (b->type->shortTypeNum==IntBuildingType::ATTACK_BUILDING)
				&& (
						b->buildingState != Building::ALIVE
						|| b->unitsInside.size() < (size_t)b->maxUnitInside
							)
								)
		{
			return false;
		}
	}
	return true;
}

Order *AIWarrush::getOrder(void)
{
	// reduce delay due to built building
	if (buildingDelay > 0)
		buildingDelay--;
	
	//C-style comments in the main code are remaining pseudocode.
	if(team->stats.getLatestStat()->numberBuildingPerType[IntBuildingType::FOOD_BUILDING] == 0)
	{
		return initialRush();
	}
	return maintain();
	
	//The complete pseudocode
	/*if(I have no Inn){
		return initialRush();
	}
	return maintain();*/
}

Order *AIWarrush::initialRush(void)
{
	if (buildingDelay == 0)
	{
		if(team->stats.getLatestStat()->numberBuildingPerType[IntBuildingType::SWARM_BUILDING] < 2)
		{
			return buildBuildingOfType(IntBuildingType::SWARM_BUILDING);
		}
		if(isAnyUnitWithLessThanOneThirdFood())
		{
			return buildBuildingOfType(IntBuildingType::FOOD_BUILDING);
		}
	}
	if(numberOfUnitsWithSkillGreaterThanValue(HARVEST,0) >= 6)
	{
		Building *out_of_date_swarm = getSwarmWithoutSettings(2,1,0);
		if(out_of_date_swarm)
		{
			Sint32 settings[3] = {2,1,0};
			return new OrderModifySwarm(out_of_date_swarm->gid, settings);
		}
	}
	if(numberOfJobsForWorkers() < numberOfUnitsWithSkillGreaterThanValue(HARVEST,0))
	{
		Building *low_swarm = getSwarmWithLeastProduction();
		return new OrderModifyBuilding(low_swarm->gid, low_swarm->maxUnitWorking + 1);
	}
	return new NullOrder();
	
	/*if(I have less than 2 swarms){
		find an available known location near wheat and workers;
		return (put a swarm there);
	}
	if(I have a worker with less than 125 nutrition){
		find an available known location near wheat, wood, and workers;
		return (put an inn there);
	}
	if(I have at least 6 workers){
		if(I have a swarm which is not making 1/3 explorers, 2/3 workers){
			return (make that swarm make 1/3 explorers, 2/3 workers);
		}
	}
	if(My swarms are not employing all my units){
		decide which has less production (probably workers/wheat-distance)
		return (increment that one's employment);
	}
	return (do nothing);*/
}

Order *AIWarrush::maintain(void)
{
	Building *out_of_date_swarm = getSwarmWithoutSettings(1,0,1);
	if(out_of_date_swarm)
	{
		Sint32 settings[3] = {1,0,1};
		return new OrderModifySwarm(out_of_date_swarm->gid, settings);
	}
	if (buildingDelay == 0)
	{
		if(
			team->stats.getLatestStat()->numberBuildingPerType[IntBuildingType::ATTACK_BUILDING] < 2
			|| (allBarracksAreCompletedAndFull() && numberOfIdleLevel1Warriors() >= 3)
				)
		{
			return buildBuildingOfType(IntBuildingType::ATTACK_BUILDING);
		}
		if(team->stats.getLatestStat()->numberBuildingPerType[IntBuildingType::FOOD_BUILDING] * 18
				<
				numberOfUnitsWithSkillGreaterThanValue(HARVEST,0))
		{
			return buildBuildingOfType(IntBuildingType::FOOD_BUILDING);
		}
	}
	const int jobs = numberOfJobsForWorkers();
	const int workers = numberOfUnitsWithSkillGreaterThanValue(HARVEST,0);
	if(jobs != workers)
	{
		Building *swarmToModify;
		if(jobs < workers)swarmToModify = getSwarmWithLeastProduction();
		else if(jobs > workers)swarmToModify = getSwarmWithMostProduction();
		else assert(false);
		return new OrderModifyBuilding(swarmToModify->gid,swarmToModify->maxUnitWorking+workers-jobs);
	}
	if(numberOfUnitsWithSkillGreaterThanValue(ATTACK_SPEED,1) >= 20)
	{
		return setupAttack();
	}
	return new NullOrder;
	/*if(I have a swarm which is not making 1/2 warriors, 1/2 workers){
		return (make that swarm make 1/2 warriors, 1/2 workers);
	}
	if(
		I have less than 2 barracks
		|| (all my Barracks are completed and full && I have at least 3 idle untrained warriors)
			) {
		find an available known location near wood and workers;
		return (put a barracks there);
	}
			//This is rather difficult to do and is reserved for later, that's why you don't
			//see it in the real part of the code. it also might be replaced with a 'farming'
			//algorithm.
				if(I am dangerously low on wheat){
					return (put Forbidden Area on all wheat immediately next to water/sand);
				}
	if(I have less than one inn per 18 units){
		find an available known location near wheat, wood, and workers;
		return (put an inn there);
	}
			//These are of dubious value, that's why you don't see them in the real part
			//of the code.
				if(I have an inn which is less than 1/4 full of wheat and has less than 2 workers on it){
					return (increase that inn's # of workers to 2);
				}
				if(I have an inn which is more than 3/4 full of wheat and has any workers on it){
					return (decrease that inn's # of workers to 0);
				}
	if(my employment is not equal to my # of available workers){
		decide which swarm is farther from (# workers available - # tasks other than swarms)/2;
		return (change that swarm's # of workers to (#workers available - # tasks other than that swarm));
	}
	if(I have at least 20 level 2 attack-speed warriors){
		return setupAttack();
	}
	return (do nothing);*/
}

Order *AIWarrush::setupAttack(void)
{
	BrushAccumulator acc;
	bool areaMap[map->w][map->h];
	for(int x=0;x<map->w;x++)
		for(int y=0;y<map->h;y++)
			areaMap[x][y]=false;

	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{
			if(map->isMapDiscovered(x,y,team->me))
			{
				//The below line is a major UGH. I don't know a simpler way to get a building from a GID.
				Building *b = game->teams[Building::GIDtoTeam(map->getBuilding(x,y))]->myBuildings[Building::GIDtoID(map->getBuilding(x,y))];
				if((b)&&(b->owner->teamNumber != team->teamNumber))
				{
					for(int sub_x = x - 1;sub_x <= x + 1;sub_x++)
					{
						for(int sub_y = y - 1;sub_y <= y + 1;sub_y++)
						{
							if(sub_x<0)sub_x+=map->w; if(sub_y<0)sub_y+=map->h;
							if(sub_x>map->w)sub_x-=map->w; if(sub_y>map->h)sub_y-=map->h;
							
							areaMap[sub_x][sub_y] = true;
						}
					}
				}
			}
		}
	}
	static bool adding = true;
	if (adding)
	{
		adding = false;
		for(int x=0;x<map->w;x++)
		{
			for(int y=0;y<map->h;y++)
			{
				if((areaMap[x][y])&&(!(map->getCase(x,y).guardArea & team->me)))
				{
					acc.applyBrush(map,BrushApplication(x,y,0));
				}
			}
		}
		return new OrderAlterateGuardArea(team->teamNumber,BrushTool::MODE_ADD,&acc);
	}
	else
	{
		adding = true;
		for(int x=0;x<map->w;x++)
		{
			for(int y=0;y<map->h;y++)
			{
				if((!areaMap[x][y])&&((map->getCase(x,y).guardArea & team->me)))
				{
					acc.applyBrush(map,BrushApplication(x,y,0));
				}
			}
		}
		return new OrderAlterateGuardArea(team->teamNumber,BrushTool::MODE_DEL,&acc);
	}
		
	/*static bool adding = true;
		find all KNOWN enemy buildings;
		find all locations adjacent to those buildings;
	if(adding){adding = false;
		return (put guard areas in all those locations);}
	else{adding = true;
		return (remove guard areas from all other locations);}*/
}

//ugh. such a large amount of code to work around something simple like "Unit8 gradient[map->w][map->h];"
struct DynamicGradientMapArray
{
public:
	typedef Uint8 element_type;
	DynamicGradientMapArray(std::size_t width, std::size_t height)
	: width_(width), height_(height), array_(new element_type[width*height]) {}
	
	~DynamicGradientMapArray() {delete[] array_;}
	
	//usage: gradient[x][y]
	element_type* operator[](std::size_t width_index) {return &array_[width_index*width_];}
	element_type const* operator[](std::size_t width_index) const {return &array_[width_index*width_];}
	
	element_type* c_array() {return array_;}
	
private:
	std::size_t width_;
	std::size_t height_;
	element_type* array_;
	
};

bool AIWarrush::locationIsAvailableForBuilding(int x, int y, int width, int height)
{
	if(map->isHardSpaceForBuilding(x,y,width,height))
	{
		if(		map->isMapDiscovered(x,			y,			team->me)
			||	map->isMapDiscovered(x+width-1,	y,			team->me)
			||	map->isMapDiscovered(x+width-1,	y+height-1,	team->me)
			||	map->isMapDiscovered(x,			y+height-1,	team->me)
				)
		{
			return true;
		}
	}
	return false;
}

Order *AIWarrush::buildBuildingOfType(Sint32 typeNum)
{
	DynamicGradientMapArray gradient(map->w,map->h);
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{
			Case c=map->getCase(x,y);
			if (
					(c.ressource.type==CORN)
					&& (
						(typeNum==IntBuildingType::FOOD_BUILDING)
						|| (typeNum==IntBuildingType::SWARM_BUILDING)
							)
								)
			{
				gradient[x][y] = 255;
			}
			else if (
					(c.ressource.type==WOOD)
					&& (
						(typeNum==IntBuildingType::FOOD_BUILDING)
						|| (typeNum==IntBuildingType::ATTACK_BUILDING)
							)
								)
			{
				gradient[x][y] = 255;
			}
			else if (c.ressource.type!=NO_RES_TYPE)
			{
				gradient[x][y] = 0;
			}
			else if (c.building!=NOGBID)
			{
				gradient[x][y] = 0;
			}
			else if (map->isWater(x,y))
			{
				gradient[x][y] = 0;
			}
			else
			{ //has to be desert or grass with no buildings or resources, at this point.
				gradient[x][y] = 1;
			}
		}
	}
	
	map->updateGlobalGradient(gradient.c_array());
	
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{
			if (gradient[x][y] == 255)
				gradient[x][y] = 0;
			else
				gradient[x][y]++;
		}
	}
	
	BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
	DynamicGradientMapArray availability_gradient(map->w,map->h);
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{
			Case c=map->getCase(x,y);
			if (c.ressource.type!=NO_RES_TYPE)
			{
				availability_gradient[x][y] = 0;
			}
			else if (c.building!=NOGBID)
			{
				availability_gradient[x][y] = 0;
			}
			else if (map->isWater(x,y))
			{
				availability_gradient[x][y] = 0;
			}
			else if(locationIsAvailableForBuilding(x,y,bt->width,bt->height))
			{
				availability_gradient[x][y] = 255;
			}
			else availability_gradient[x][y] = 1;
		}
	}
	
	map->updateGlobalGradient(availability_gradient.c_array());
	
	Building *swarm = getSwarmWithMostProduction();
	Sint32 destination_x,destination_y;
	{
		Sint32 x,y;
		map->dumpGradient(gradient.c_array(), "gradient.pgm");
		map->dumpGradient(availability_gradient.c_array(), "availability_gradient.pgm");
		map->getGlobalGradientDestination(gradient.c_array(), swarm->posX, swarm->posY, &x, &y);
		bool res = map->getGlobalGradientDestination(availability_gradient.c_array(), x, y, &destination_x, &destination_y);
		
		std::cout << "Trying to build " << typeNum << " at " << destination_x << "," << destination_y << " from " << x << "," << y << " swarm is " << swarm->posX << "," << swarm->posY << ", found = " << res << std::endl;
	}
	
	// set delay
	buildingDelay = BUILDING_DELAY;
	
	// creatre and return order
	return new OrderCreate(team->teamNumber,destination_x,destination_y,typeNum);
	
	/*Code that I decided not to use -- NOT pseudocode!
	
	int x,y;
	int max_value = -10000;
	int max_x, max_y;
	BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
	int width = bt->width;
	int height = bt->height;
	for(x=0;x<map->w;x++){
		for(y=0;y<map->h;y++){
			if(map->isHardSpaceForBuilding(x,y,width,height)){
				if(		isMapDiscovered(x,			y,			team->me)
					||	isMapDiscovered(x+width,	y,			team->me)
					||	isMapDiscovered(x+width,	y+height,	team->me)
					||	isMapDiscovered(x,			y+height,	team->me)
						) {
					int value;
					if(typeNum == IntBuildingType::FOOD_BUILDING){
						value += map->getGradient(team->teamNumber,CORN,false,x,y)
						value += map->getGradient(team->teamNumber,WOOD,false,x,y)
					}
					//max value thingy y'kno..
				}
			}
		}
	}*/
}

