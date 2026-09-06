// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2005 Eli Dupree

#include "AIWarrush.h"
#include "AIWarrushTuning.h"
#include "Building.h"
#include "Unit.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Brush.h"
#include "Utilities.h"

using std::shared_ptr;

namespace {
	template<typename Pred>
	int countUnitsIf(const Team *team, Pred p)
	{
		int n = 0;
		for (int i = 0; i < Unit::MAX_COUNT; i++)
		{
			Unit *u = team->myUnits[i];
			if (u && p(u)) n++;
		}
		return n;
	}

	template<typename Pred>
	Unit *findUnitIf(const Team *team, Pred p)
	{
		for (int i = 0; i < Unit::MAX_COUNT; i++)
		{
			Unit *u = team->myUnits[i];
			if (u && p(u)) return u;
		}
		return nullptr;
	}

	template<typename Pred>
	int countBuildingsIf(const Team *team, Pred p)
	{
		int n = 0;
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building *b = team->myBuildings[i];
			if (b && p(b)) n++;
		}
		return n;
	}

	template<typename Pred>
	Building *findBuildingIf(const Team *team, Pred p)
	{
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building *b = team->myBuildings[i];
			if (b && p(b)) return b;
		}
		return nullptr;
	}
}

void AIWarrush::init(Player *player)
{
	assert(player);
	
	this->player=player;
	this->team=player->team;
	this->game=player->game;
	this->map=player->map;
	buildingDelay = 0;
	areaUpdatingDelay = 0;
	
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

int AIWarrush::numberOfUnitsWithSkillGreaterThanValue(const int skill, const int value)const
{
	return countUnitsIf(team, [skill, value](Unit *u) { return u->performance[skill] > value; });
}

int AIWarrush::numberOfUnitsWithSkillEqualToValue(const int skill, const int value)const
{
	return countUnitsIf(team, [skill, value](Unit *u) { return u->performance[skill] == value; });
}

bool AIWarrush::isAnyUnitWithLessThanOneThirdFood()const
{
	//Yeah, it's a half, not a third. Weird huh? :P
	return findUnitIf(team, [](Unit *u) { return u->hungry < (Unit::HUNGRY_MAX/AI_WARRUSH_HUNGRY_THRESHOLD_DIVISOR); }) != nullptr;
}

Building *AIWarrush::getSwarmWithoutSettings(const int workerRatio, const int explorerRatio, const int warriorRatio)const
{
	return findBuildingIf(team, [=](Building *b) {
		return b->type->shortTypeNum == IntBuildingType::SWARM_BUILDING
			&& b->constructionResultState == Building::NO_CONSTRUCTION
			&& (b->ratio[0] != workerRatio || b->ratio[1] != explorerRatio || b->ratio[2] != warriorRatio);
	});
}

Building *AIWarrush::getBuildingWithoutWorkersAssigned(Sint32 shortTypeNum, int num_workers)const
{
	return findBuildingIf(team, [=](Building *b) {
		return b->type->shortTypeNum == shortTypeNum
			&& b->maxUnitWorking != num_workers
			&& (b->constructionResultState != Building::NO_CONSTRUCTION
				|| (shortTypeNum != IntBuildingType::ATTACK_BUILDING
					&& shortTypeNum != IntBuildingType::HEAL_BUILDING
					&& shortTypeNum != IntBuildingType::WALKSPEED_BUILDING
					&& shortTypeNum != IntBuildingType::SWIMSPEED_BUILDING
					&& shortTypeNum != IntBuildingType::SCIENCE_BUILDING));
	});
}

Building *AIWarrush::getSwarmAtRandom()const
{
	Building **myBuildings=team->myBuildings;
	int swarmsfound = 0;
	Building *chosen_swarm = NULL;
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b=myBuildings[i];
		if ((b) && (b->type->shortTypeNum==IntBuildingType::SWARM_BUILDING))
		{
			++swarmsfound;
			if(syncRand()%swarmsfound == 0)
			{
				chosen_swarm = b;
			}
		}
	}
	return chosen_swarm;
}

bool AIWarrush::allOfBuildingTypeAreCompleted(Sint32 shortTypeNum)const
{
	return findBuildingIf(team, [shortTypeNum](Building *b) {
		return b->type->shortTypeNum == shortTypeNum
			&& (b->constructionResultState != Building::NO_CONSTRUCTION
				|| b->buildingState == Building::DEAD);
	}) == nullptr;
}

bool AIWarrush::allOfBuildingTypeAreFull(Sint32 shortTypeNum)const
{
	return findBuildingIf(team, [shortTypeNum](Building *b) {
		return b->type->shortTypeNum == shortTypeNum
			&& b->unitsInside.size() < (size_t)b->maxUnitInside;
	}) == nullptr;
}

int AIWarrush::numberOfBuildingsOfType(Sint32 shortTypeNum)const
{
	return countBuildingsIf(team, [shortTypeNum](Building *b) {
		return b->shortTypeNum == shortTypeNum;
	});
}


int AIWarrush::numberOfExtraBuildings()const
{
	return countBuildingsIf(team, [](Building *b) {
		return b->shortTypeNum == IntBuildingType::HEAL_BUILDING
			|| b->shortTypeNum == IntBuildingType::WALKSPEED_BUILDING
			|| b->shortTypeNum == IntBuildingType::SWIMSPEED_BUILDING
			|| b->shortTypeNum == IntBuildingType::SCIENCE_BUILDING
			|| b->shortTypeNum == IntBuildingType::DEFENSE_BUILDING;
	});
}

bool AIWarrush::allOfBuildingTypeAreFullyWorked(Sint32 shortTypeNum)const
{
	return findBuildingIf(team, [shortTypeNum](Building *b) {
		return b->shortTypeNum == shortTypeNum
			&& b->unitsWorking.size() != (size_t)b->maxUnitWorking;
	}) == nullptr;
}

bool AIWarrush::percentageOfBuildingsAreFullyWorked(int percentage)const
{
	Building **myBuildings=team->myBuildings;
	int num_buildings = 0;
	int num_worked_buildings = 0;
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		Building *b=myBuildings[i];
		if((b)&&(b->shortTypeNum != IntBuildingType::WAR_FLAG)&&(b->shortTypeNum != IntBuildingType::EXPLORATION_FLAG)&&(b->shortTypeNum != IntBuildingType::CLEARING_FLAG))
		{
			++num_buildings;
			if(b->unitsWorking.size() == (size_t)b->maxUnitWorking)
			{
				++num_worked_buildings;
				if(verbose)std::cout << "A";
			}
			else if((b->type->shortTypeNum == IntBuildingType::SWARM_BUILDING
					|| b->type->shortTypeNum == IntBuildingType::FOOD_BUILDING)
					&&
					b->constructionResultState == Building::NO_CONSTRUCTION
					&&
					(b->ressources[CORN]) > ((b->wishedResources[CORN]) * AI_WARRUSH_HEAVILY_WORKED_RATIO_NUM / AI_WARRUSH_HEAVILY_WORKED_RATIO_DEN))
			{//heavily worked swarms and inns sometimes are full and have no workers
				++num_worked_buildings;
				if(verbose)std::cout << "C";
			}
		}
	}
	if(verbose)std::cout << ": " << num_worked_buildings << " worked out of " << num_buildings << "\n";
	return num_worked_buildings * 100 >= num_buildings * percentage;
}

std::shared_ptr<Order> AIWarrush::getOrder(void)
{
	// reduce delays
	if (buildingDelay > 0)
		buildingDelay--;
	if (areaUpdatingDelay > 0)
		areaUpdatingDelay--;
	
	if(game->stepCounter < AI_WARRUSH_BOOTSTRAP_EXPLORE_WINDOW && game->stepCounter%AI_WARRUSH_BOOTSTRAP_EXPLORE_INTERVAL == 0)
	{
		int teamIndex = game->stepCounter / AI_WARRUSH_BOOTSTRAP_EXPLORE_INTERVAL;
		Team *enemy_team = game->teams[teamIndex];
		if((enemy_team)&&(team->enemies & enemy_team->me))return setupExploreFlagForTeam(enemy_team);
	}

	//keep those areas up to date
	if(areaUpdatingDelay == AI_WARRUSH_AREAS_DELAY_TICKS*AI_WARRUSH_AREAS_PRUNE_PHASE_NUM/AI_WARRUSH_AREAS_PRUNE_PHASE_DEN)
		return pruneGuardAreas();
	if(areaUpdatingDelay == AI_WARRUSH_AREAS_DELAY_TICKS/AI_WARRUSH_AREAS_PLACE_PHASE_DEN)
		return placeGuardAreas();
	if(areaUpdatingDelay <= 0)
	{
		areaUpdatingDelay = AI_WARRUSH_AREAS_DELAY_TICKS;
		return farm();
	}

	//assuming we didn't have to mess with areas or explore flags, check if we can build stuff
	if (buildingDelay <= 0)
	{
		bool shouldBuildMore = percentageOfBuildingsAreFullyWorked(AI_WARRUSH_BUILD_MORE_PCT_THRESHOLD);
		if(verbose)if(shouldBuildMore)std::cout << "AIWarrush is ready to build more stuff!";
		//Build another swarm if all are swarms are working at capacity, and if we have other random stuff we should-have / need
		if(verbose)std::cout << "Chance to build swarm: ";
		if(shouldBuildMore && allOfBuildingTypeAreCompleted(IntBuildingType::SWARM_BUILDING) && numberOfExtraBuildings() >= numberOfBuildingsOfType(IntBuildingType::SWARM_BUILDING) && numberOfBuildingsOfType(IntBuildingType::FOOD_BUILDING) >= numberOfBuildingsOfType(IntBuildingType::SWARM_BUILDING) * AI_WARRUSH_INNS_PER_SWARM_RATIO)
		{
			if(verbose)std::cout << "TAKEN!\n";
			return buildBuildingOfType(IntBuildingType::SWARM_BUILDING);
		}
		if(verbose)std::cout << "ignored.\n";
		
		//Silly inns. Don't build them right away and don't build too many at a time and don't build too many.
		//More limits than it should have, maybe, but the idea of the AI is that it should either win
		//or lose warriors (so it shouldn't need so many inns.)
		if(verbose)std::cout << "Chance to build inn: ";
		if(isAnyUnitWithLessThanOneThirdFood()
				&&
				shouldBuildMore
				&&
				 numberOfExtraBuildings() >= numberOfBuildingsOfType(IntBuildingType::FOOD_BUILDING) - AI_WARRUSH_INN_LOOKAHEAD
				&&
				(
				 (allOfBuildingTypeAreCompleted(IntBuildingType::FOOD_BUILDING)
				&& allOfBuildingTypeAreFull(IntBuildingType::FOOD_BUILDING))
				|| allOfBuildingTypeAreFullyWorked(IntBuildingType::FOOD_BUILDING))
				  )
		{
			if(verbose)std::cout << "TAKEN!\n";
			return buildBuildingOfType(IntBuildingType::FOOD_BUILDING);
		}
		if(verbose)std::cout << "ignored.\n";
		
		//if the barracks are all working at capacity,
		//build more barracks! (this also builds the first barracks...)
		if(verbose)std::cout << "Chance to build barracks: ";
		if(
			allOfBuildingTypeAreCompleted(IntBuildingType::ATTACK_BUILDING)
			&& allOfBuildingTypeAreFull(IntBuildingType::ATTACK_BUILDING)
				)
		{
			if(verbose)std::cout << "TAKEN!\n";
			return buildBuildingOfType(IntBuildingType::ATTACK_BUILDING);
		}
		if(verbose)std::cout << "ignored.\n";
	
		//and if we have excess workers, build random other buildings!
		if(verbose)std::cout << "Chance to build etc: ";
		if(shouldBuildMore)
		{
			if(verbose)std::cout << "TAKEN!\n";
			Sint32 type;
			int random_number = syncRand()%AI_WARRUSH_RANDOM_BUILDING_DENOM;
			if(random_number < AI_WARRUSH_HEAL_PCT_THRESHOLD || numberOfBuildingsOfType(IntBuildingType::HEAL_BUILDING) == 0)type = IntBuildingType::HEAL_BUILDING;
			else if(random_number < AI_WARRUSH_WALKSPEED_PCT_THRESHOLD || numberOfBuildingsOfType(IntBuildingType::WALKSPEED_BUILDING) == 0)type = IntBuildingType::WALKSPEED_BUILDING;
			else if(random_number < AI_WARRUSH_SWIMSPEED_PCT_THRESHOLD)type = IntBuildingType::SWIMSPEED_BUILDING;
			else if(random_number < AI_WARRUSH_SCIENCE_PCT_THRESHOLD)type = IntBuildingType::SCIENCE_BUILDING;
			else type = IntBuildingType::DEFENSE_BUILDING;
			return buildBuildingOfType(type);
		}
		if(verbose)std::cout << "ignored.\n";
	}

	//If we have enough workers, we can switch to dedicated warrushing production.
	if(numberOfUnitsWithSkillGreaterThanValue(HARVEST,0) >= AI_WARRUSH_HARVESTER_THRESHOLD)
	{
		//This is basically a way to change all the swarms without bothering to remember
		//anything. (It can only issue one order per tick, so it has to do it over several
		//ticks and calculate the orders seperately.)
		Building *out_of_date_swarm = getSwarmWithoutSettings(AI_WARRUSH_SWARM_RATIO_WORKER, AI_WARRUSH_SWARM_RATIO_EXPLORER, AI_WARRUSH_SWARM_RATIO_WARRIOR);
		if(out_of_date_swarm)
		{
			Sint32 settings[3] = {AI_WARRUSH_SWARM_RATIO_WORKER, AI_WARRUSH_SWARM_RATIO_EXPLORER, AI_WARRUSH_SWARM_RATIO_WARRIOR};
			return shared_ptr<Order>(new OrderModifySwarm(out_of_date_swarm->gid, settings));
		}
	}

	//all swarms should always have 5 workers at them!
	Building *weak_swarm = getBuildingWithoutWorkersAssigned(IntBuildingType::SWARM_BUILDING, AI_WARRUSH_SWARM_WORKER_COUNT);
	if (weak_swarm) return shared_ptr<Order>(new OrderModifyBuilding(weak_swarm->gid, AI_WARRUSH_SWARM_WORKER_COUNT));

	//all inns should always have 3 workers at them! (best to build fast, make sure they're fed)
	Building *weak_inn = getBuildingWithoutWorkersAssigned(IntBuildingType::FOOD_BUILDING, AI_WARRUSH_INN_WORKER_COUNT);
	if (weak_inn) return shared_ptr<Order>(new OrderModifyBuilding(weak_inn->gid, AI_WARRUSH_INN_WORKER_COUNT));

	//work barracks more too.
	Building *weak_barracks = getBuildingWithoutWorkersAssigned(IntBuildingType::ATTACK_BUILDING, AI_WARRUSH_BARRACKS_WORKER_COUNT);
	if (weak_barracks && weak_barracks->constructionResultState != Building::NO_CONSTRUCTION)
	 return shared_ptr<Order>(new OrderModifyBuilding(weak_barracks->gid, AI_WARRUSH_BARRACKS_WORKER_COUNT));
	
	//nothing at all to do?!
	return shared_ptr<Order>(new NullOrder);
}

std::shared_ptr<Order> AIWarrush::pruneGuardAreas()
{
	//If we have any guard areas that aren't adjacent to an enemy building, we remove them.
	BrushAccumulator acc;
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{
			if(map->isGuardArea(x,y,team->me))
			{
				bool keep = false;
				for(int xmod=-1;xmod<=1;xmod++)
				{
					for(int ymod=-1;ymod<=1;ymod++)
					{
						//if there's a building...
						if(map->getBuilding(x+xmod,y+ymod)!=NOGBID)
						{
							//...AND it's an enemy building...
							if(team->enemies & game->teams[Building::GIDtoTeam(map->getBuilding(x+xmod,y+ymod))]->me)
							{
								//...then we still want it guarded.
								keep=true;
							}
						}
					}
				}
				if(!keep)
				{
					acc.applyBrush(BrushApplication(x,y,0), map);
				}
			}
		}
	}
	if(acc.getApplicationCount())
	{
		return shared_ptr<Order>(new OrderAlterateGuardArea(team->teamNumber,BrushTool::MODE_DEL,&acc,map));
	}
	else return shared_ptr<Order>(new NullOrder);
}
	
std::shared_ptr<Order> AIWarrush::placeGuardAreas()
{
	BrushAccumulator guard_add_acc;
	//Place guard area on an enemy building if there is one...
	for(int i=0;i<Team::MAX_COUNT;i++)
	{
		Team *t = game->teams[i];
		if((t)&&(team->enemies & t->me))
		{
			for(int j=0;j<Building::MAX_COUNT;j++)
			{
				Building *b = t->myBuildings[j];
				if ((b)&&(b->buildingState != Building::DEAD)&&(b->hp != 1 || b->constructionResultState == Building::NO_CONSTRUCTION)&&(b->shortTypeNum != IntBuildingType::WAR_FLAG)&&(b->shortTypeNum != IntBuildingType::EXPLORATION_FLAG)&&(b->shortTypeNum != IntBuildingType::CLEARING_FLAG))
				{
					BuildingType *bt = b->type;
					if(
							//the area must be discovered to prevent AI cheating.
							(
									map->isFOWDiscovered(b->posX,               b->posY,              team->me)
								||	map->isFOWDiscovered(b->posX+bt->width - 1, b->posY,              team->me)
								||	map->isFOWDiscovered(b->posX+bt->width - 1, b->posY+bt->height-1, team->me)
								||	map->isFOWDiscovered(b->posX,               b->posY+bt->height-1, team->me)
									)
							&&
							//do not order a building attacked if the order is already in place.
							(
								!map->isGuardArea(b->posX,b->posY,team->me)
									)			
										)
					{
						if((map->getBuilding(b->posX, b->posY)!=NOGBID)&&(team->enemies & game->teams[Building::GIDtoTeam(map->getBuilding(b->posX, b->posY))]->me)) //paranoia
						{
							for(int x = 0; x < bt->width; x++)
							{
								for(int y = 0; y < bt->height; y++)
								{
									guard_add_acc.applyBrush(BrushApplication((b->posX+x) % map->getW(), (b->posY+y) % map->getH(),AI_WARRUSH_GUARD_BRUSH_SIZE), map);
								}
							}
						}
					}
				}
			}
		}
	}
	
	if(guard_add_acc.getApplicationCount())
	{
		return shared_ptr<Order>(new OrderAlterateGuardArea(team->teamNumber,BrushTool::MODE_ADD,&guard_add_acc, map));
	}
	else return shared_ptr<Order>(new NullOrder);
}
	
std::shared_ptr<Order> AIWarrush::farm()
{
	// Algorithm initially stolen from Nicowar.
	DynamicGradientMapArray water_gradient(map->w,map->h);
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{	
			if (map->isWater(x,y))
			{
				water_gradient(x, y) = AI_WARRUSH_GRADIENT_MAX;
			}
			else
			{
				water_gradient(x, y) = 1;
			}
		}
	}
	map->updateGlobalGradient(water_gradient.c_array());

	BrushAccumulator del_acc;
	BrushAccumulator add_acc;
	BrushAccumulator clr_del_acc;
	BrushAccumulator clr_add_acc;
	for(int x=0;x<map->w;x++)
	{
		for(int y=0;y<map->h;y++)
		{
			if((!map->isRessourceTakeable(x, y, WOOD) && !map->isRessourceTakeable(x, y, CORN)))
			{
				if(map->isForbidden(x, y, team->me))
				{
					if(
						//Make sure we're not deleting buildings' forbidden area!
						!map->isForbidden (x + 1,y,team->me)
						&& !map->isForbidden (x - 1,y,team->me)
						&& !map->isForbidden (x,y + 1,team->me)
						&& !map->isForbidden (x,y - 1,team->me)
						//Or fruits'!
						&& !map->isRessourceTakeable(x, y, CHERRY)
						&& !map->isRessourceTakeable(x, y, ORANGE)
						&& !map->isRessourceTakeable(x, y, PRUNE)
						)
					{
						del_acc.applyBrush(BrushApplication(x, y, 0), map);
					}
				}
			}
			
			if(map->isForbidden(x, y, team->me) && map->isClearArea(x, y, team->me))
			{
				del_acc.applyBrush(BrushApplication(x, y, 0), map);
			}
			
			//we never clear anything but wood
			if(!map->isRessourceTakeable(x, y, WOOD))
			{
				if(map->isClearArea(x, y, team->me))
				{
					clr_del_acc.applyBrush(BrushApplication(x, y, 0), map);
				}
			}

			//we clear wood if it's next to nice stuff like wheat or buildings
			if(map->isRessourceTakeable(x, y, WOOD))
			{
				if(!map->isClearArea(x, y, team->me) && map->isMapDiscovered(x, y, team->me))
				{
					for(int xmod=-1;xmod<=1;xmod++)
					{
						for(int ymod=-1;ymod<=1;ymod++)
						{
							if(map->isRessourceTakeable(x+xmod, y+ymod, CORN)
									|| (map->getBuilding(x+xmod,y+ymod)!=NOGBID
									&& (team->me & game->teams[Building::GIDtoTeam(map->getBuilding(x+xmod,y+ymod))]->me)))
							{
								clr_add_acc.applyBrush(BrushApplication(x, y, 0), map);
								goto doublebreak;
							}
						}
					}
					doublebreak:
					/*statement for label to point to*/;
				}
			}


			if(x%2==1 && ((y%2==1 && x%4==1) || (y%2==0 && x%4==3)))
			{
				if(map->isRessourceTakeable(x, y, WOOD))
				{
					if(!map->isForbidden(x, y, team->me) && !map->isClearArea(x, y, team->me) && map->isMapDiscovered(x, y, team->me) && water_gradient(x, y) > (AI_WARRUSH_GRADIENT_MAX - AI_WARRUSH_WATER_NEAR_OFFSET))
					{
						add_acc.applyBrush(BrushApplication(x, y, 0), map);
					}
				}
			}

			if(x%2==y%2)
			{
				if(map->isRessourceTakeable(x, y, CORN))
				{
					if(!map->isForbidden(x, y, team->me) && map->isMapDiscovered(x, y, team->me) && water_gradient(x, y) > (AI_WARRUSH_GRADIENT_MAX - AI_WARRUSH_WATER_NEAR_OFFSET))
					{
						add_acc.applyBrush(BrushApplication(x, y, 0), map);
					}
				}
			}

			//FORBID FRUITS!!! They're horrible for our warriors and we hate converting.
			if(
				(	map->isRessourceTakeable(x, y, CHERRY)
					|| map->isRessourceTakeable(x, y, ORANGE)
					|| map->isRessourceTakeable(x, y, PRUNE)	)
				&& !map->isForbidden(x, y, team->me)
				&& map->isMapDiscovered(x, y, team->me)
					)
			{
				add_acc.applyBrush(BrushApplication(x, y, 0), map);
			}

		}
	}

	if(del_acc.getApplicationCount()>0)
		return shared_ptr<Order>(new OrderAlterateForbidden(team->teamNumber, BrushTool::MODE_DEL, &del_acc, map));
	if(add_acc.getApplicationCount()>0)
		return shared_ptr<Order>(new OrderAlterateForbidden(team->teamNumber, BrushTool::MODE_ADD, &add_acc, map));
	if(clr_del_acc.getApplicationCount()>0)
		return shared_ptr<Order>(new OrderAlterateClearArea(team->teamNumber, BrushTool::MODE_DEL, &clr_del_acc, map));
	if(clr_add_acc.getApplicationCount()>0)
		return shared_ptr<Order>(new OrderAlterateClearArea(team->teamNumber, BrushTool::MODE_ADD, &clr_add_acc, map));

	//nothing to do...
	return shared_ptr<Order>(new NullOrder());
}

//Simple hack to place explore flags on opponents' starting swarms.
std::shared_ptr<Order> AIWarrush::setupExploreFlagForTeam(Team *enemy_team)
{
	if(verbose)std::cout << "looking for swarms:\n";
	for(int j=0;j<Building::MAX_COUNT;j++)
	{
		Building *b = enemy_team->myBuildings[j];
		if((b)&&(b->type->shortTypeNum == IntBuildingType::SWARM_BUILDING)&&(b->constructionResultState == Building::NO_CONSTRUCTION))
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("explorationflag", 0, false);
			return shared_ptr<Order>(new OrderCreate(team->teamNumber, b->posX, b->posY, typeNum, 1, 1));
		}
	}
	if(verbose)std::cout << "No swarms found\n";
	//what, they have no swarm? o_O Find any building:
	for(int j=0;j<Building::MAX_COUNT;j++)
	{
		Building *b = enemy_team->myBuildings[j];
		if(b)
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("explorationflag", 0, false);
			return shared_ptr<Order>(new OrderCreate(team->teamNumber, b->posX, b->posY, typeNum, 1, 1));
		}
	}
	if(verbose)std::cout << "No buildings found\n";
	//what, they have no buildings? o_O Find any unit:
	for(int j=0;j<Unit::MAX_COUNT;j++)
	{
		Unit *u = enemy_team->myUnits[j];
		if(u)
		{
			Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("explorationflag", 0, false);
			return shared_ptr<Order>(new OrderCreate(team->teamNumber, u->posX, u->posY, typeNum, 1, 1));
		}
	}
	//what, enemy has no buildings or units at the beginning of the game? o_O O_o o_O
	if(verbose)std::cout << "No buildings found, no units found o_O\n";
	return shared_ptr<Order>(new NullOrder);
}

bool AIWarrush::locationIsAvailableForBuilding(int x, int y, int width, int height)
{
	/*if(map->isHardSpaceForBuilding(x,y,width,height))
	{*/
		if(		map->isMapDiscovered(x,			y,			team->me)
			||	map->isMapDiscovered(x+width-1,	y,			team->me)
			||	map->isMapDiscovered(x+width-1,	y+height-1,	team->me)
			||	map->isMapDiscovered(x,			y+height-1,	team->me)
				)
		{
			return true;
		}
	/*}*/
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
				gradient(x, y) = AI_WARRUSH_GRADIENT_MAX;
			}
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
			if (gradient(x, y) == AI_WARRUSH_GRADIENT_MAX)
				gradient(x, y) = 0;
			else
				gradient(x, y)++;
		}
	}
}

std::shared_ptr<Order> AIWarrush::buildBuildingOfType(Sint32 shortTypeNum)
{
	
	// set delay
	// now doing this first in order to avoid repeated failed builds
	// WARNING THIS IS A HACK FIX
	// in reality, if it fails to build, it should go on and get another order.
	buildingDelay = AI_WARRUSH_BUILDING_DELAY_TICKS;

	DynamicGradientMapArray wood_gradient(map->w,map->h);
	DynamicGradientMapArray wheat_gradient(map->w,map->h);
	initializeGradientWithResource(wood_gradient, WOOD);
	initializeGradientWithResource(wheat_gradient, CORN);
	
	
	BuildingType *bt=globalContainer->buildingsTypes.getByType(IntBuildingType::typeFromShortNumber(shortTypeNum), 0, true);
	
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
			else if(map->isHardSpaceForBuilding(x-(bt->width / AI_WARRUSH_BUILDING_CENTER_DIVISOR),y-(bt->width / AI_WARRUSH_BUILDING_CENTER_DIVISOR),bt->width*AI_WARRUSH_BUILDING_CLEARANCE_MULT,bt->height*AI_WARRUSH_BUILDING_CLEARANCE_MULT) && locationIsAvailableForBuilding(x,y,bt->width,bt->height)) //the extra numbers at the ends expand the building
			{
				availability_gradient(x, y) = AI_WARRUSH_GRADIENT_MAX;
			}
			else availability_gradient(x, y) = 1;
		}
	}
	
	map->updateGlobalGradient(availability_gradient.c_array());
	
	Building *swarm = getSwarmAtRandom();
	if (!swarm)
	{
		if(verbose)std::cout << "No swarm found!\n";
		return shared_ptr<Order>(new NullOrder);
	}
	Sint32 destination_x,destination_y;
	{
		Sint32 x,y;
		Sint32 x_temp,y_temp;
		
		x = swarm->posX; y = swarm->posY;
		
		if(shortTypeNum==IntBuildingType::ATTACK_BUILDING || shortTypeNum==IntBuildingType::HEAL_BUILDING || shortTypeNum==IntBuildingType::SCIENCE_BUILDING || shortTypeNum==IntBuildingType::WALKSPEED_BUILDING || shortTypeNum==IntBuildingType::SWIMSPEED_BUILDING || shortTypeNum==IntBuildingType::DEFENSE_BUILDING)
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
		
		if(verbose)std::cout << "Trying to build " << shortTypeNum << "(" << bt->width << " x " << bt->height << ")" << " at " << destination_x << "," << destination_y << " from " << x << "," << y << " swarm is " << swarm->posX << "," << swarm->posY << ", found = " << result << std::endl;
		if(verbose)if((int)availability_gradient(destination_x, destination_y) != AI_WARRUSH_GRADIENT_MAX)std::cout << "Could not find valid location for building! Best spot: " << destination_x << "," << destination_y << " (" << (int)availability_gradient(destination_x, destination_y) << ")\n";
	}
		
	// create and return order
	Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::typeFromShortNumber(shortTypeNum), 0, true);
	return shared_ptr<Order>(new OrderCreate(team->teamNumber, destination_x, destination_y, typeNum, 1, 1));
}

