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
#include <valarray>
#include <algorithm>

#define BUILDING_DELAY 30

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

int AIWarrush::numberOfBuildingsOfType(Sint32 shortTypeNum)const
{
	Building **myBuildings=team->myBuildings;
	int count = 0;
	for (int i=0; i<1024; i++)
	{
		Building *b=myBuildings[i];
		if ((b)&&(b->type->shortTypeNum==shortTypeNum))
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
	
	//This is basically just a way of splitting the AI into two phases, one of which comes before
	//the Inn is placed, the other of which comes after. It would probably be better to store the
	//state, e.g. for maps where you start with an inn it would do very poorly.
	// Not using: if(team->stats.getLatestStat()->numberBuildingPerType[IntBuildingType::FOOD_BUILDING] == 0)
	if(numberOfBuildingsOfType(IntBuildingType::FOOD_BUILDING) == 0)
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
	//IF it hasn't been too soon since we last built something, we may build stuff...
	if (buildingDelay == 0)
	{
		//Build up two swarms. This usually means to build one swarm at the beginning
		//of the game. This is so that the AI can produce units faster, as it needs to
		//in order to 'rush'. :)
		//Not using: if(team->stats.getLatestStat()->numberBuildingPerType[IntBuildingType::SWARM_BUILDING] < 2)
		if(numberOfBuildingsOfType(IntBuildingType::SWARM_BUILDING) < 2)
		{
			return buildBuildingOfType(IntBuildingType::SWARM_BUILDING);
		}
		//This triggers the end of the first phase. The first phase lasts until you
		//actually *need* the inn before your units get hungry.
		if(isAnyUnitWithLessThanOneThirdFood())
		{
			return buildBuildingOfType(IntBuildingType::FOOD_BUILDING);
		}
	}
	//If we have enough workers, we can switch to making some explorers to find the enemies.
	if(numberOfUnitsWithSkillGreaterThanValue(HARVEST,0) >= 6)
	{
		//This is basically a way to change all the swarms without bothering to remember
		//anything. (It can only issue one order per tick, so it has to do it over several
		//ticks and calculate the orders seperately.)
		Building *out_of_date_swarm = getSwarmWithoutSettings(2,1,0);
		if(out_of_date_swarm)
		{
			Sint32 settings[3] = {2,1,0};
			return new OrderModifySwarm(out_of_date_swarm->gid, settings);
		}
	}
	//Obviously we have no reason for unemployment.
	//Currently AIWarrush::getSwarmWithLeastProduction() returns the swarm with fewest
	//workers on it, but it might be good to also base it on distance to wheat (so that
	//the swarms get relatively equal amounts of wheat, rather than workers.)
	if(numberOfJobsForWorkers() < numberOfUnitsWithSkillGreaterThanValue(HARVEST,0))
	{
		Building *low_swarm = getSwarmWithLeastProduction();
		if (low_swarm)
			return new OrderModifyBuilding(low_swarm->gid, std::min(low_swarm->maxUnitWorking + 1, 20));
		else
			return new NullOrder;
	}
	//Conversely we have no need for overemployment.
	if(numberOfJobsForWorkers() > numberOfUnitsWithSkillGreaterThanValue(HARVEST,0))
	{
		Building *high_swarm = getSwarmWithMostProduction();
		if (high_swarm)
			return new OrderModifyBuilding(high_swarm->gid, std::max(high_swarm->maxUnitWorking - 1, 0));
		else
			return new NullOrder;
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
	//Update all swarms to produce warriors as well. 1/6 of units produced are still explorers,
	//in case they all get killed or something.
	Building *out_of_date_swarm = getSwarmWithoutSettings(2,1,3);
	if(out_of_date_swarm)
	{
		Sint32 settings[3] = {2,1,3};
		return new OrderModifySwarm(out_of_date_swarm->gid, settings);
	}
	//Again, IF it hasn't been too soon since we last built something, we may build stuff...
	if (buildingDelay == 0)
	{
		//If we don't yet have the first two barracks, or if we don't have enough to train
		//our warriors, then we build more to accomodate.
		if(
			//Not using: team->stats.getLatestStat()->numberBuildingPerType[IntBuildingType::ATTACK_BUILDING] < 2
			numberOfBuildingsOfType(IntBuildingType::ATTACK_BUILDING) < 2
			|| (allBarracksAreCompletedAndFull() && numberOfIdleLevel1Warriors() >= 3)
				)
		{
			return buildBuildingOfType(IntBuildingType::ATTACK_BUILDING);
		}
		//The AI currently assumes it can accomodate 18 units per inn, which is optimistic,
		//but it can assume that either its warriors will be killed or its enemies will be
		//defeated, both of which would reduce the necessity. :)
		//The following code builds inns if there are more than 18 units for each one.
		//Not using: if(team->stats.getLatestStat()->numberBuildingPerType[IntBuildingType::FOOD_BUILDING] * 18
		if(numberOfBuildingsOfType(IntBuildingType::FOOD_BUILDING) * 18
				<
				numberOfUnitsWithSkillGreaterThanValue(WALK,0)+numberOfUnitsWithSkillGreaterThanValue(FLY,0))
					//The above is abominable and should be replaced by "numberOfUnits" or somesuch.
		{
			return buildBuildingOfType(IntBuildingType::FOOD_BUILDING);
		}
	}
	
	//We want to keep all our workers employed, all the time. This code has two problems.
	//The first is that it assumes that all workers are able and ready to work.
	//The second is that it assigns them all to the swarm (it should build hospitals
	//with them instead.)
	
	//This is the same code as above. This is not a bad thing, like most duplicate code,
	//because most changes to this should differ from those to that.
	if(numberOfJobsForWorkers() < numberOfUnitsWithSkillGreaterThanValue(HARVEST,0))
	{
		Building *low_swarm = getSwarmWithLeastProduction();
		if (low_swarm)
			return new OrderModifyBuilding(low_swarm->gid, std::min(low_swarm->maxUnitWorking + 1, 20));
		else
			return new NullOrder;
	}
	if(numberOfJobsForWorkers() > numberOfUnitsWithSkillGreaterThanValue(HARVEST,0))
	{
		Building *high_swarm = getSwarmWithMostProduction();
		if (high_swarm)
			return new OrderModifyBuilding(high_swarm->gid, std::max(high_swarm->maxUnitWorking - 1, 0));
		else
			return new NullOrder;
	}
	
	//This is the old job-handling code. Quite foolish in light of the better algorithm above,
	//which I had already implemented at the time...
	/*
	const int jobs = numberOfJobsForWorkers();
	const int workers = numberOfUnitsWithSkillGreaterThanValue(HARVEST,0);
	if(jobs != workers)
	{
		Building *swarmToModify;
		if(jobs < workers)swarmToModify = getSwarmWithLeastProduction();
		else if(jobs > workers)swarmToModify = getSwarmWithMostProduction();
		else assert(false);
		return new OrderModifyBuilding(swarmToModify->gid,swarmToModify->maxUnitWorking+workers-jobs);
	}*/
	
	
	//And we attack if we have enough warriors. This code may cause an ugly problem if enough
	//warriors get killed in the assault. Again, it should store what phase it's at.
	//(although phase-based AI is rather bad, AI that is phase-based but appears not to be by
	//predicting what it would have done already is worse.)
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
	//If we have any guard areas that aren't adjacent to an enemy building, we remove them.
	{
		BrushAccumulator acc;
		for(int x=0;x<map->w;x++)
		{
			for(int y=0;y<map->h;y++)
			{
				if(map->isGuardAreaLocal(x,y))
				{
					bool keep = false;
					for(int xmod=-1;xmod<=1;xmod++)
					{
						for(int ymod=-1;ymod<=1;ymod++)
						{
							//This is wrong. It not only has to be a building, it also has to be an
							//ENEMY building. If the enemy is very close or the AI forms an alliance
							//with the enemy, the AI will leave the guard areas there, hindering the
							//real attack.
							if(map->getBuilding(x+xmod,y+ymod))
							{
								keep=true;
							}
						}
					}
					if(!keep)
					{
						acc.applyBrush(map,BrushApplication(x,y,0));
					}
				}
			}
		}
		//Obviously we don't want to give an actual order if there is no area to remove.
		//That would prevent the area from ever being placed, in this case.
		if(acc.getApplicationCount())
		{
			return new OrderAlterateGuardArea(team->teamNumber,BrushTool::MODE_DEL,&acc);
		}
	}
	
	//There were no excess guard areas. Place on an enemy inn or swarm (or if there are none known, on any building)
	
	std::string pass = "first";
	//On the "first" pass, attack only inns and swarms. The "second" pass is there in case none are found.
	//The "done" pass is in case no enemy buildings at all are found.
	while(pass != "done")
	{
		for(int i=0;i<32;i++)
		{
			Team *t = game->teams[i];
			if(team->enemies & t->me)
			{
				for(int j=0;j<32;j++)
				{
					Building *b = t->myBuildings[j];
					BuildingType *bt = b->type;
					if(
							//the area must be discovered to prevent AI cheating.
							(
									map->isMapDiscovered(b->posX,				b->posY,				team->me)
								||	map->isMapDiscovered(b->posX+bt->width - 1,	b->posY,				team->me)
								||	map->isMapDiscovered(b->posX+bt->width - 1,	b->posY+bt->height-1,	team->me)
								||	map->isMapDiscovered(b->posX,				b->posY+bt->height-1,	team->me)
									)
							&&
							//the building must be a swarm or inn unless there are none available.
							(
									bt->shortTypeNum == IntBuildingType::SWARM_BUILDING
								||	bt->shortTypeNum == IntBuildingType::FOOD_BUILDING
								||	pass == "second"
									)
							&&
							//do not order a building attacked if the order is already in place.
							(
								!map->isGuardAreaLocal(b->posX,b->posY)
									)			
										)
					{
						BrushAccumulator acc;
						for(int x = 0; x < bt->width; x++)
						{
							for(int y = 0; y < bt->height; y++)
							{
								acc.applyBrush(map,BrushApplication(b->posX+x,b->posY+y,6));
							}
						}
						return new OrderAlterateGuardArea(team->teamNumber,BrushTool::MODE_ADD,&acc);
					}
				}
			}
		}
		if(pass == "first")pass = "second";
		if(pass == "second")pass = "done";
	}
	
	//omg there are no guard areas and no enemy buildings. I wonder where they are.
	return new NullOrder();
	
	/*
	Note from Steph: disable because it produces excessively big areas and there is a static which breaks glob2
	
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
				Uint16 gbid = map->getBuilding(x, y);
				if (gbid != NOGBID)
				{
					//The below line is a major UGH. I don't know a simpler way to get a building from a GID.
					Building *b = game->teams[Building::GIDtoTeam(gbid)]->myBuildings[Building::GIDtoID(gbid)];
					if ((b) && (b->owner->me & team->enemies))
					{
						for (int sub_x = x - 1;sub_x <= x + 1;sub_x++)
						{
							for (int sub_y = y - 1;sub_y <= y + 1;sub_y++)
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
	
	
	return new NullOrder();*/
		
	/*static bool adding = true;
		find all KNOWN enemy buildings;
		find all locations adjacent to those buildings;
	if(adding){adding = false;
		return (put guard areas in all those locations);}
	else{adding = true;
		return (remove guard areas from all other locations);}*/
}


/*
Note: Nct: Moved to AIWarrush.h
//ugh. such a large amount of code to work around something simple like "Unit8 gradient[map->w][map->h];"
struct DynamicGradientMapArray
{
public:
	typedef Uint8 element_type;
	
	DynamicGradientMapArray(std::size_t w, std::size_t h) :
		width(w),
		height(h),
		array(w*h)
	{
	}
	
	//usage: gradient(x, y)
	const element_type &operator()(size_t x, size_t y) const { return array[y * width + x]; }
	element_type &operator()(size_t x, size_t y) { return array[y * width + x]; }
	element_type* c_array() { return &array[0]; }
	
private:
	std::size_t width;
	std::size_t height;
	std::valarray<element_type> array;
};
*/

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

void AIWarrush::initializeGradientWithResource(DynamicGradientMapArray &gradient, Uint8 resource_type)
{
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{
			Case c=map->getCase(x,y);
			if (c.ressource.type==resource_type)
			{
				gradient(x, y) = 255;
			}
			/*if (
					(c.ressource.type==CORN)
					&& (
						(shortTypeNum==IntBuildingType::FOOD_BUILDING)
						|| (shortTypeNum==IntBuildingType::SWARM_BUILDING)
							)
								)
			{
				gradient(x, y) = 255;
			}
			else if (
					(c.ressource.type==WOOD)
					&& (
						(shortTypeNum==IntBuildingType::FOOD_BUILDING)
						|| (shortTypeNum==IntBuildingType::ATTACK_BUILDING)
							)
								)
			{
				gradient(x, y) = 255;
			}*/
			else if (c.ressource.type!=NO_RES_TYPE)
			{
				gradient(x, y) = 0;
			}
			else if (c.building!=NOGBID)
			{
				gradient(x, y) = 0;
			}
			else if (map->isWater(x,y))
			{
				gradient(x, y) = 0;
			}
			else
			{ //has to be desert or grass with no buildings or resources, at this point.
				gradient(x, y) = 1;
			}
		}
	}
	
	map->updateGlobalGradient(gradient.c_array());
	
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{
			if (gradient(x, y) == 255)
				gradient(x, y) = 0;
			else
				gradient(x, y)++;
		}
	}
}

Order *AIWarrush::buildBuildingOfType(Sint32 shortTypeNum)
{
	DynamicGradientMapArray wood_gradient(map->w,map->h);
	DynamicGradientMapArray wheat_gradient(map->w,map->h);
	initializeGradientWithResource(wood_gradient, WOOD);
	initializeGradientWithResource(wheat_gradient, CORN);
	
	
	BuildingType *bt=globalContainer->buildingsTypes.get(shortTypeNum);
	DynamicGradientMapArray availability_gradient(map->w,map->h);
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{
			Case c=map->getCase(x,y);
			if (c.ressource.type!=NO_RES_TYPE)
			{
				availability_gradient(x, y) = 0;
			}
			else if (c.building!=NOGBID)
			{
				availability_gradient(x, y) = 0;
			}
			else if (map->isWater(x,y))
			{
				availability_gradient(x, y) = 0;
			}
			else if(locationIsAvailableForBuilding(x-1,y-1,bt->width+2,bt->height+2)) //the extra numbers at the ends expand the building 
			{
				availability_gradient(x, y) = 255;
			}
			else availability_gradient(x, y) = 1;
		}
	}
	
	map->updateGlobalGradient(availability_gradient.c_array());
	
	Building *swarm = getSwarmWithMostProduction();
	if (!swarm) return new NullOrder;
	Sint32 destination_x,destination_y;
	{
		Sint32 x,y;
		Sint32 x_temp,y_temp;
		//map->dumpGradient(gradient.c_array(), "gradient.pgm");
		//map->dumpGradient(availability_gradient.c_array(), "availability_gradient.pgm");
		
		x = swarm->posX; y = swarm->posY;
		
		if(shortTypeNum==IntBuildingType::ATTACK_BUILDING || shortTypeNum==IntBuildingType::FOOD_BUILDING)
		{
			map->getGlobalGradientDestination(wood_gradient.c_array(), x, y, &x_temp, &y_temp);
			x = x_temp; y = y_temp;
		}
		if(shortTypeNum==IntBuildingType::SWARM_BUILDING || shortTypeNum==IntBuildingType::FOOD_BUILDING)
		{
			map->getGlobalGradientDestination(wheat_gradient.c_array(), x, y, &x_temp, &y_temp);
			x = x_temp; y = y_temp;
		}
		bool result = map->getGlobalGradientDestination(availability_gradient.c_array(), x, y, &destination_x, &destination_y);
		
		std::cout << "Trying to build " << shortTypeNum << " at " << destination_x << "," << destination_y << " from " << x << "," << y << " swarm is " << swarm->posX << "," << swarm->posY << ", found = " << result << std::endl;
	}
	
	// set delay
	buildingDelay = BUILDING_DELAY;
	
	// create and return order
	Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::typeFromShortNumber(shortTypeNum), 0, true);
	return new OrderCreate(team->teamNumber, destination_x, destination_y, typeNum);
	
	/*Code that I decided not to use -- NOT pseudocode!
	
	int x,y;
	int max_value = -10000;
	int max_x, max_y;
	BuildingType *bt=globalContainer->buildingsTypes.get(shortTypeNum);
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
					if(shortTypeNum == IntBuildingType::FOOD_BUILDING){
						value += map->getGradient(team->teamNumber,CORN,false,x,y)
						value += map->getGradient(team->teamNumber,WOOD,false,x,y)
					}
					//max value thingy y'kno..
				}
			}
		}
	}*/
}

