/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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

#include <Stream.h>

#include "AIHelper.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include "Unit.h"
#include <algorithm>
#include <iterator>

using namespace std;

AIHelper::AIHelper(Player *player)
{
	init(player);
}




AIHelper::AIHelper(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	bool goodLoad=load(stream, player, versionMinor);
	assert(goodLoad);
}




void AIHelper::init(Player *player)
{
	timer=0;
	iteration=0;
	desired_explorers=0;
	developing_attack_explorers=false;
	explorer_attacking=false;
	changeUnits("upgrade-repair maneger", WORKER, std::max(MAXIMUM_TO_REPAIR, MAXIMUM_TO_UPGRADE)*MAX_CONSTRUCTION_AT_ONCE*1.5, std::max(MAXIMUM_TO_REPAIR, MAXIMUM_TO_UPGRADE), 0);

	assert(player);

	this->player=player;
	this->team=player->team;
	this->game=player->game;
	this->map=player->map;

	assert(this->team);
	assert(this->game);
	assert(this->map);
}




AIHelper::~AIHelper()
{
}




bool AIHelper::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	init(player);
	stream->readEnterSection("AIHelper");
	stream->readLeaveSection();
	return true;
}




void AIHelper::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AIHelper");
	stream->writeLeaveSection();
}




Order *AIHelper::getOrder(void)
{
	//See if there is an existing order that the AI wanted to have done
	if (!orders.empty())
	{
		Order* order = orders.front();
		orders.pop();
		return order;
	}
	//Putting this at the start allowed for certain bugs to creep in where it would skip past
	//the time where it should have done something because there where items already in the queue.
	timer++;
	//Waits for atleast 30 ticks before it does anything, because of a few off, 'goes to fast' bugs.
	if (timer<50)
		return new NullOrder();

	if(timer%TIMER_ITERATION==0)
	{
		iteration+=1;
		std::cout<<"AIHelper: getOrder: ******Entering iteration "<<iteration<<" at tick #"<<timer<<". ******"<<endl;
	}

	//The ai does stuff in iterations.
	switch(timer%TIMER_ITERATION)
	{
		case removeOldConstruction_TIME:
			//Remove any upgrade records for upgrades that where finished.
			removeOldConstruction();
			break;

		case updatePendingConstruction_TIME:
			//Update unit assignments on buildings that have gone into construction mode.
			updatePendingConstruction();
			break;

		case reassignConstruction_TIME:
			//Reassign the units on theupgrades on buildings
			reassignConstruction();
			break;

		case startNewConstruction_TIME:
			//Try and make an upgrade to a building.
			startNewConstruction();
			break;
		case exploreWorld_TIME:
			//Create, move, or destroy flags as neccecary to explore the world
			exploreWorld();
			break;
		case findCreatedFlags_TIME:
			//Find flags created by exploreWorld and add them to the record books
			findCreatedFlags();
			break;
		case moderateSwarmsForExplorers_TIME:
			//Modify swarm ratios in order to maintain the desired number of explorers.
			moderateSwarmsForExplorers();
			break;
		case explorerAttack_TIME:
			//Attack using explorers if possible
			explorerAttack();
			break;
		case moderateSwarms_TIME:
			//Change the ratios on the swarms if neccecary
			moderateSwarms();
			break;
		case recordInns_TIME:
			//Record the number of units and amount of food in each inn
			recordInns();
			break;
		case modifyInns_TIME:
			//Modify the top and bottom usage inns appropriettly
			modifyInns();
			break;
		case controlTowers_TIME:
			//Assigns units to towers
			controlTowers();
			break;
	}

	if (!orders.empty())
	{
		Order* order = orders.front();
		orders.pop();
		return order;
	}

	return new NullOrder();

}




bool AIHelper::isFreeOfFlags(unsigned int x, unsigned int y)
{
	Building** myBuildings=team->myBuildings;
	for (int i=0; i<1024; i+=1)
	{
		Building *b=myBuildings[i];
		if (b)
		{
			if (b->type->shortTypeNum==IntBuildingType::EXPLORATION_FLAG ||
				b->type->shortTypeNum==IntBuildingType::WAR_FLAG ||
				b->type->shortTypeNum==IntBuildingType::CLEARING_FLAG)
			{
				if (b->posX == x && b->posY == y)
				{
					return false;
				}
			}
		}
	}
	return true;
}




Building* AIHelper::getBuildingFromGid(unsigned int gid)
{
	for (int i=0; i<32; i++)
	{
		Team* t = game->teams[i];
		if(t)
		{
			for(int i=0; i<1024; i++)
			{
				Building* b = t->myBuildings[i];
				if (b)
				{
					if (b->gid == gid)
						return b;
				}
			}
		}
	}
	return NULL;
}




bool AIHelper::buildingStillExists(Building* building)
{
	for (int i=0; i<32; i++)
	{
		Team* t = game->teams[i];
		if(t)
		{
			for(int i=0; i<1024; i++)
			{
				Building* b = t->myBuildings[i];
				if (b)
				{
					if(b == building)
						return true;
				}
			}
		}
	}
	return false;
}




bool AIHelper::buildingStillExists(unsigned int gid)
{
	return getBuildingFromGid(gid)!=NULL;
}




int AIHelper::pollArea(unsigned int x, unsigned int y, unsigned int width, unsigned int height, pollType poll_type)
{

	unsigned int bound_h=x+width;
	unsigned int bound_y=y+height;
	int score=0;
	for(; x<=bound_h; ++x)
	{
		for(; y<=bound_y; ++y)
		{
			switch (poll_type)
			{

				case HIDDEN_SQUARES:
					if(!map->isMapDiscovered(x, y, team->me))
					{
						score++;
					}
					break;
				case ENEMY_UNITS:
					Building* b = getBuildingFromGid(map->getBuilding(x, y));
					if (b)
					{
						if(b->owner->me&team->enemies)
						{
							score++;
						}
					}
					break;

			}
		}
	}
	return score;
}




std::vector<AIHelper::pollRecord> AIHelper::pollMap(unsigned int area_width, unsigned int area_height, int horizontal_overlap, int vertical_overlap, unsigned int requested_spots, pollType poll_type)
{

	vector<AIHelper::pollRecord> best;
	best.reserve((map->getW()/(area_width-horizontal_overlap))*
		map->getH()/(area_height-vertical_overlap));

	for (int x=0; x<=map->getW()-area_width; x+=(area_width)-(horizontal_overlap))
	{
		for (int y=0; y<=map->getH()-area_height; y+=(area_height)-(vertical_overlap))
		{
			int score=pollArea(x, y, area_width, area_height, poll_type);
			best.push_back(pollRecord(x, y, area_width, area_height, score, poll_type));
		}
	}
	std::sort(best.begin(), best.end(), greater<pollRecord>());
	best.erase(best.begin()+requested_spots, best.end());
	return best;
}




void AIHelper::removeOldConstruction(void)
{
	for (std::list<AIHelper::constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end();)
	{
		Building *b=i->building;
		if(!buildingStillExists(b))
		{
			i=active_construction.erase(i);
			continue;
		}

		int original = i->original;
		if (b->constructionResultState==Building::NO_CONSTRUCTION)
		{
			if(AIHelper_DEBUG)
				std::cout<<"AIHelper: removeOldConstruction: Removing an old "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" from the active construction list, changing assigned number of units back to "<<original<<" from "<<i->assigned<<"."<<endl;
			i=active_construction.erase(i);
			orders.push(new OrderModifyBuilding(b->gid, original));
			continue;
		}
		i++;
	}
}




void AIHelper::updatePendingConstruction(void)
{
	for (std::list<AIHelper::constructionRecord>::iterator i = pending_construction.begin(); i!=pending_construction.end();)
	{
		Building *b=i->building;
		if(!buildingStillExists(b))
		{
			i=active_construction.erase(i);
			continue;
		}
		int assigned = i->assigned;
		if (b->buildingState != Building::WAITING_FOR_CONSTRUCTION && b->buildingState != Building::WAITING_FOR_CONSTRUCTION_ROOM)
		{
			AIHelper::constructionRecord u=*i;
			if(AIHelper_DEBUG)
				std::cout<<"AIHelper: updatePendingConstruction: The "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" was found that it is no longer pending construction, I am assigning number of requested units, "<<assigned<<", to it."<<endl;
			active_construction.push_back(u);
			i=pending_construction.erase(i);
			orders.push(new OrderModifyBuilding(b->gid, assigned));
			continue;
		}
		i++;
	}
}




int AIHelper::getAvailableUnitsForConstruction(int level)
{
	int free_workers[NB_UNIT_LEVELS];
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		free_workers[j]=getFreeUnits(BUILD, j+1);
	}

	int total_free = 0;
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		if ((j+1)>=level)
			total_free+=free_workers[j];
	}
	return total_free;
}




void AIHelper::reassignConstruction(void)
{
	//Get the numbers of free units
	int free_workers[NB_UNIT_LEVELS];
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		free_workers[j]=getFreeUnits(BUILD, j+1);
	}

	//Add in the numbers of units that are already working
	for (list<AIHelper::constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end(); i++)
	{
		Building *b=i->building;
		for (std::list<Unit*>::iterator i = b->unitsWorking.begin(); i!=b->unitsWorking.end(); i++)
		{
			Unit* u=*i;
			free_workers[u->level[BUILD]]+=1;
		}
	}

	//Finally, iterate through the shuffled list of records changing the number of units allocated to upgrade the buildings.
	for (list<AIHelper::constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end(); i++)
	{
		Building *b=i->building;
		int assigned=i->assigned;
		bool is_repair=i->is_repair;

		//Find the number of workers with enough of a level to upgrade this building
		int available_upgrade = 0;
		int available_repair  = 0;
		for (int j = 0; j<NB_UNIT_LEVELS; j++)
		{
			//Important, b->type->level takes on the level number of the building its being upgraded to.
			//So in this function, b->type->level will be different then in startNewConstruction for
			//buildings being upgraded. I have copy and pasted this section of code a thousand times
			//between the two when I change it and each time it causes me trouble.
			if (j > (b->type->level)-1 )
			{
				available_upgrade+=free_workers[j];
			}
			if (j >= (b->type->level) )
			{
				available_repair+=free_workers[j];
			}
		}

		if (!is_repair && available_upgrade==0)
		{
			if(AIHelper_DEBUG)
				std::cout<<"AIHelper: reassignConstruction: There are not enough available units. Canceling upgrade on the "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<"."<<endl;
			orders.push(new OrderCancelConstruction(b->gid));
			continue;
		}

		else if (is_repair && available_repair==0)
		{
			if(AIHelper_DEBUG)
				std::cout<<"AIHelper: reassignConstruction: There are not enough available units. Canceling repair on the "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<"."<<endl;
			orders.push(new OrderCancelConstruction(b->gid));
			continue;
		}

		//Issue the command to change the number of units working on the building to the new amount
		int num_to_assign=0;
		int generic_available=0;
		if (!is_repair)
		{
			num_to_assign=available_upgrade;
			generic_available=available_upgrade;
			if (num_to_assign>MAXIMUM_TO_UPGRADE)
				num_to_assign=MAXIMUM_TO_UPGRADE;
		}
		else if (is_repair)
		{
			num_to_assign=available_repair;
			generic_available=available_repair;
			if (num_to_assign>MAXIMUM_TO_REPAIR)
				num_to_assign=MAXIMUM_TO_REPAIR;
		}

		if (num_to_assign != assigned)
		{
			if(AIHelper_DEBUG)
				std::cout<<"AIHelper: reassignConstruction: Retasking "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" that is under construction. Number of units available: "<<generic_available<< ". Number of units originally assigned: "<<assigned<<". Number of units assigning: "<<num_to_assign<<"."<<endl;
			orders.push(new OrderModifyBuilding(b->gid, num_to_assign));
			i->assigned=num_to_assign;
		}
		reduce(free_workers, b->type->level, num_to_assign);
	}
}




int AIHelper::getFreeUnits(int ability, int level)
{
	level-=1;                                                                                       //This is because ability levels are counted from 0, not 1. I'm not sure why,
	//though, because 0 would mean that the unit does not have that skill, which
	//would be more appropriette.
	int free_workers=0;
	Unit **myUnits=team->myUnits;
	for (int i=0; i<1024; i++)
	{
		Unit* u = myUnits[i];
		if (u)
		{
			if ( (u->activity==Unit::ACT_RANDOM || u->movement==Unit::MOV_RANDOM_GROUND) && u->level[ability]==level && u->medical==Unit::MED_FREE)
			{
				free_workers+=1;
			}
		}
	}
	return free_workers;
}




void AIHelper::startNewConstruction(void)
{
	//If we already have more than our max, don't do any more
	if (active_construction.size()+pending_construction.size()>=MAX_CONSTRUCTION_AT_ONCE)
		return;

	//Get the numbers of free units
	int free_workers[NB_UNIT_LEVELS];
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		free_workers[j]=getFreeUnits(BUILD, j+1);
	}

	//Look through each building we previoussly assigned to be upgraded, and subtract their requested number of units from the free ones (ones that they haven't recieved yet but wanted.). Also take not of the type of building, counting up the numbers for each type.
	int construction_counts[IntBuildingType::NB_BUILDING];
	for (int j=0; j!=IntBuildingType::NB_BUILDING; j++)
		construction_counts[j]=0;

	for (list<AIHelper::constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end(); i++)
	{
		Building *b=i->building;
		int assigned = i->assigned;
		reduce(free_workers, b->type->level, assigned-b->unitsWorking.size());
		construction_counts[b->type->shortTypeNum]+=1;
	}

	for (list<AIHelper::constructionRecord>::iterator i = pending_construction.begin(); i!=pending_construction.end(); i++)
	{
		Building *b=i->building;
		int assigned = i->assigned;
		reduce(free_workers, b->type->level, assigned-b->unitsWorking.size());
		construction_counts[b->type->shortTypeNum]+=1;
	}

	//Make a copy of the array of buildings, then shuffle it so that every building has an equal chance of being upgraded with the spare units.
	vector<Building*> buildings;
	buildings.reserve(1024);
	for (int i=0; i<1024; i+=1)
	{
		Building *b=team->myBuildings[i];
		if (b)
			buildings.push_back(b);
	}
	random_shuffle(buildings.begin(), buildings.end());

	//Look through the buildings and find one that needs to be upgraded or repaired, and if their are atleast 4 of the correct unit type available to upgrade it, then do it.
	for (vector<Building*>::iterator i = buildings.begin(); i!=buildings.end(); i++)
	{
		Building *b=*i;
		if (b)
		{
			//Check if its worthy of having anything done to it.
			if (b->constructionResultState==Building::NO_CONSTRUCTION      &&
				b->buildingState==Building::ALIVE                          &&
				construction_counts[b->type->shortTypeNum] != MAX_BUILDING_SPECIFIC_CONSTRUCTION_LIMITS[b->type->shortTypeNum])
			{
				//Find the number of workers with enough of a level to upgrade and repair this building (if it will need it).
				int available_upgrade = 0;
				int available_repair  = 0;
				for (int j = 0; j<NB_UNIT_LEVELS; j++)
				{
					if (j > (b->type->level) )
					{
						available_upgrade+=free_workers[j];
					}
					if (j >= (b->type->level) )
					{
						available_repair+=free_workers[j];
					}
				}

				//See if the building needs repair. Note the code for initializing a repair and the code for initializing an upgrade are very similiar.
				if (b->type->shortTypeNum!=IntBuildingType::SWARM_BUILDING         &&
					b->type->shortTypeNum!=IntBuildingType::EXPLORATION_FLAG       &&
					b->type->shortTypeNum!=IntBuildingType::WAR_FLAG               &&
					b->type->shortTypeNum!=IntBuildingType::CLEARING_FLAG          &&
					available_repair>=MINIMUM_TO_REPAIR                            &&
					b->hp  <  b->type->hpMax)
				{
					int num_to_assign=available_repair;
					if( num_to_assign > MAXIMUM_TO_REPAIR)
						num_to_assign=MAXIMUM_TO_REPAIR;
					if(AIHelper_DEBUG)
						cout<<"AIHelper: startNewConstruction: Found "<<available_repair<<" available workers, assigning "<<num_to_assign<<" workers to repair the "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" with "<<b->maxUnitWorking<<" units already working on it."<<endl;
					AIHelper::constructionRecord u;
					u.building=b;
					u.assigned=num_to_assign;
					u.original=b->maxUnitWorking;
					u.is_repair=true;
					pending_construction.push_back(u);
					orders.push(new OrderConstruction(b->gid));
					reduce(free_workers, b->type->level, num_to_assign);
					construction_counts[b->type->shortTypeNum]+=1;
				}

				//If we don't need to repair, see if we can upgrade
				else if (b->type->level!=2                                     &&
					(b->type->shortTypeNum==IntBuildingType::FOOD_BUILDING     ||
					b->type->shortTypeNum==IntBuildingType::HEAL_BUILDING      ||
					b->type->shortTypeNum==IntBuildingType::WALKSPEED_BUILDING ||
					b->type->shortTypeNum==IntBuildingType::SWIMSPEED_BUILDING ||
					b->type->shortTypeNum==IntBuildingType::ATTACK_BUILDING    ||
					b->type->shortTypeNum==IntBuildingType::SCIENCE_BUILDING   ||
					b->type->shortTypeNum==IntBuildingType::DEFENSE_BUILDING  )&&
					available_upgrade>=MINIMUM_TO_UPGRADE)
				{

					//If we have a minimum of 4 available workers, upgrade the building.
					int num_to_assign=available_upgrade;
					if( num_to_assign > MAXIMUM_TO_UPGRADE)
						num_to_assign=MAXIMUM_TO_UPGRADE;
					if(AIHelper_DEBUG)
						cout<<"AIHelper: startNewConstruction: Found "<<available_upgrade<<" available workers, assigning "<<num_to_assign<<" workers to upgrade the "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" with "<<b->maxUnitWorking<<" units already working on it."<<endl;
					AIHelper::constructionRecord u;
					u.building=b;
					u.assigned=num_to_assign;
					u.original=b->maxUnitWorking;
					u.is_repair=false;
					pending_construction.push_back(u);
					orders.push(new OrderConstruction(b->gid));
					reduce(free_workers, b->type->level+1, num_to_assign);
					construction_counts[b->type->shortTypeNum]+=1;
				}
			}
		}
	}
}




void AIHelper::exploreWorld(void)
{
	std::vector<Building*> orphaned_flags;

	int exploration_count=0;
	int attack_count=0;
	for (std::list<explorationRecord>::iterator i = active_exploration.begin(); i!=active_exploration.end();)
	{
		bool was_orphaned=false;
		if(!i->isAssaultFlag || explorer_attacking)
		{
			int score=0;
			if(i->isAssaultFlag)
				score=pollArea(i->zone_x, i->zone_y, i->width, i->height, ENEMY_UNITS);
			else
				score=pollArea(i->zone_x, i->zone_y, i->width, i->height, HIDDEN_SQUARES);

			if(score==0)
			{
				orphaned_flags.push_back(i->flag);
				was_orphaned=true;
				i=active_exploration.erase(i);
			}
			else
			{
				if(i->isAssaultFlag)
					attack_count++;
				else
					exploration_count++;
			}
		}
		if (!was_orphaned)
		{
			++i;
		}
	}

	if (exploration_count==EXPLORER_MAX_REGIONS_AT_ONCE && attack_count==EXPLORER_ATTACKS_AT_ONCE)
		return;

	int wanted_exploration=EXPLORER_MAX_REGIONS_AT_ONCE-exploration_count;
	int wanted_attack=EXPLORER_ATTACKS_AT_ONCE-attack_count;
	if(! explorer_attacking )
		wanted_attack=0;
	vector<pollRecord> best = pollMap(EXPLORER_REGION_WIDTH, EXPLORER_REGION_HEIGHT, EXPLORER_REGION_HORIZONTAL_OVERLAP, EXPLORER_REGION_VERTICAL_OVERLAP, exploration_count+wanted_exploration+1, HIDDEN_SQUARES);

	vector<pollRecord> best_attack = pollMap(EXPLORER_ATTACK_AREA_WIDTH, EXPLORER_ATTACK_AREA_HEIGHT, EXPLORER_ATTACK_AREA_HORIZONTAL_OVERLAP, EXPLORER_ATTACK_AREA_VERTICAL_OVERLAP, attack_count+wanted_attack+1, ENEMY_UNITS);

	std::copy(best_attack.begin(), best_attack.end(), back_insert_iterator<vector<pollRecord> >(best));

	for(vector<pollRecord>::iterator i = best.begin(); i!=best.end();)
	{
		if(!isFreeOfFlags(i->x+i->width/2, i->y+i->height/2))
		{
			i = best.erase(i);
		}
		else
		{
			++i;
		}
	}

	if (best.at(0).score==0)
	{
		for(std::vector<Building*>::iterator i = orphaned_flags.begin(); i!=orphaned_flags.end(); i++)
		{
			if(AIHelper_DEBUG)
				std::cout<<"AIHelper: exploreWorld: Removing an existing explorer flag, it is no longer needed."<<endl;
			desired_explorers-=EXPLORERS_PER_REGION;
			orders.push(new OrderDelete((*i)->gid));
		}
		return;
	}

	else
	{
		for (int i=0; i<wanted_exploration+wanted_attack; ++i)
		{
			pollRecord pr = best.at(0);
			best.erase(best.begin());
			explorationRecord exploration_record;
			//This flag will be picked up by findCreatedFlags
			exploration_record.flag=NULL;
			exploration_record.flag_x=pr.x+(pr.width/2);
			exploration_record.flag_y=pr.y+(pr.height/2);
			exploration_record.zone_x=pr.x;
			exploration_record.zone_y=pr.y;
			exploration_record.width=pr.width;
			exploration_record.height=pr.height;
			if(pr.poll_type == HIDDEN_SQUARES)
			{
				exploration_record.assigned=EXPLORERS_PER_REGION;
				exploration_record.radius=EXPLORATION_FLAG_RADIUS;
				exploration_record.isAssaultFlag=false;
			}
			else
			{
				exploration_record.assigned=EXPLORERS_PER_ATTACK;
				exploration_record.radius=EXPLORATION_FLAG_ATTACK_RADIUS;
				exploration_record.isAssaultFlag=true;
			}

			active_exploration.push_back(exploration_record);
			if(orphaned_flags.size()>0)
			{
				Building* orphaned_flag = orphaned_flags.at(0);
				orphaned_flags.erase(orphaned_flags.begin());
				if(AIHelper_DEBUG)
					std::cout<<"AIHelper: exploreWorld: Moving an existing explorer flag from position "<<orphaned_flag->posX<<","<<orphaned_flag->posY<<" to position "<<exploration_record.flag_x<<","<<exploration_record.flag_y<<"."<<endl;
				orders.push(new OrderMoveFlag(orphaned_flag->gid, exploration_record.flag_x, exploration_record.flag_y, true));
				return;
			}
			else
			{
				Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("explorationflag", 0, false);
				if(AIHelper_DEBUG)
					std::cout<<"AIHelper: exploreWorld: Creating a new flag at position "<<exploration_record.flag_x<<","<<exploration_record.flag_y<<"."<<endl;
				desired_explorers+=EXPLORERS_PER_REGION;
				orders.push(new OrderCreate(team->teamNumber, exploration_record.flag_x, exploration_record.flag_y,typeNum));
			}
		}
	}
}




void AIHelper::findCreatedFlags(void)
{
	//Find the record that has NULL for a flag, if it exists.
	std::vector<explorationRecord*> explr;
	for (std::list<explorationRecord>::iterator i = active_exploration.begin(); i!=active_exploration.end(); ++i)
	{
		if(i->flag == NULL)
			explr.push_back(&(*i));
	}
	if (explr.size()==0)
		return;

	//Iterate through the buildings (which includes flags), looking for a flag that is not on the lists, that is in the right
	//spot to be one of our null records flags.
	Building** myBuildings=team->myBuildings;
	for (int i=0; i<1024; ++i)
	{
		Building* b=myBuildings[i];
		if (b)
		{
			if (b->type->shortTypeNum==IntBuildingType::EXPLORATION_FLAG)
			{
				bool found=false;
				for (std::list<explorationRecord>::iterator i = active_exploration.begin(); i!=active_exploration.end(); ++i)
				{
					if(i->flag == b)
					{
						found=true;
						break;
					}
				}
				//If the flag is not in the list, look through our null records list and see if it matches any
				if (!found)
				{
					for (vector<explorationRecord*>::iterator i = explr.begin(); i!=explr.end(); i++)
					{
						if (b->posX==((*i)->flag_x) && b->posY==((*i)->flag_y))
						{
							(*i)->flag=b;
							if(AIHelper_DEBUG)
								std::cout<<"AIHelper: findCreatedFlags: Found a newly created or moved flag matching our records at positon "<<b->posX<<","<<b->posY<<" , inserting this flag into our records."<<endl;
							orders.push(new OrderModifyFlag(b->gid, (*i)->radius));
							orders.push(new OrderModifyBuilding(b->gid, (*i)->assigned));
							break;
						}
					}
				}
			}
		}
	}
}




void AIHelper::moderateSwarmsForExplorers(void)
{
	changeUnits("aircontrol", EXPLORER, static_cast<int>(desired_explorers/2) , desired_explorers, 0);
}




void AIHelper::explorerAttack(void)
{
	bool lvl3_school_exists=false;
	Building** myBuildings=team->myBuildings;
	for (int i=0; i<1024; ++i)
	{
		Building* b=myBuildings[i];
		if (b)
		{
			if (b->type->shortTypeNum==IntBuildingType::SCIENCE_BUILDING && b->type->level==2)
			{
				lvl3_school_exists=true;
			}
		}
	}
	if (!lvl3_school_exists)
	{
		return;
	}

	if (!developing_attack_explorers)
	{
		developing_attack_explorers=true;
		desired_explorers+=static_cast<int>(EXPLORERS_PER_ATTACK*EXPLORER_ATTACKS_AT_ONCE);
		return;
	}

	//check if we have enough explorers to start launching attacks.
	int ground_attack_explorers=0;
	Unit** myUnits = team->myUnits;
	for (int i=0; i<1024; i++)
	{
		Unit* u = myUnits[i];
		if (u)
		{
			if (u->canLearn[MAGIC_ATTACK_GROUND] && u->performance[MAGIC_ATTACK_GROUND])
			{
				ground_attack_explorers+=1;
			}
		}
	}

	if (ground_attack_explorers<static_cast<int>(EXPLORERS_PER_ATTACK*EXPLORER_ATTACKS_AT_ONCE))
		return;
	if(AIHelper_DEBUG)
		if(!explorer_attacking)
			std::cout<<"AIHelper: explorerAttack: Enabling explorer attacks."<<endl;
	explorer_attacking=true;
}




void AIHelper::targetEnemy()
{

}




void AIHelper::attack()
{

}




void AIHelper::changeUnits(string module_name, unsigned int unit_type, unsigned int desired_units, unsigned int required_units, unsigned int emergency_units)
{
	unitRecord ur;
	if (module_demands.count(module_name))
		ur=module_demands[module_name];
	else
	{
		for (int i=0; i<NB_UNIT_TYPE; i++)
		{
			ur.desired_units[i]=0;
			ur.required_units[i]=0;
			ur.emergency_units[i]=0;
		}
	}
	ur.desired_units[unit_type]=desired_units;
	ur.required_units[unit_type]=required_units;
	ur.emergency_units[unit_type]=emergency_units;
	module_demands[module_name]=ur;
}




void AIHelper::moderateSwarms()
{
	int unit_scores[NB_UNIT_TYPE];
	int num_wanted[NB_UNIT_TYPE];
	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		unit_scores[i]=0;
		num_wanted[i]=0;
	}

	for (std::map<string, unitRecord>::iterator i = module_demands.begin(); i!=module_demands.end(); i++)
	{
		for (int unit_t=0; unit_t<NB_UNIT_TYPE; unit_t++)
		{
			unit_scores[unit_t]+=i->second.desired_units[unit_t]*DESIRED_UNIT_SCORE;
			unit_scores[unit_t]+=i->second.required_units[unit_t]*REQUIRED_UNIT_SCORE;
			unit_scores[unit_t]+=i->second.emergency_units[unit_t]*EMERGENCY_UNIT_SCORE;
			num_wanted[unit_t]+=i->second.desired_units[unit_t];
			num_wanted[unit_t]+=i->second.required_units[unit_t];
			num_wanted[unit_t]+=i->second.emergency_units[unit_t];

		}
	}

	int ratios[NB_UNIT_TYPE];
	std::copy(unit_scores, unit_scores+NB_UNIT_TYPE, ratios);
	int max=*max_element(ratios, ratios+NB_UNIT_TYPE);
	float devisor=1;
	int total_wanted=0;
	if(max>20)
		devisor=static_cast<float>(max)/20.0;
	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		total_wanted+=num_wanted[i]-team->stats.getLatestStat()->numberUnitPerType[i];
		if(num_wanted[i]-team->stats.getLatestStat()->numberUnitPerType[i] <= 0)
			ratios[i]=0;
		else
			ratios[i]=static_cast<int>(std::floor(ratios[i]/devisor+0.5));
	}

	if(total_wanted<0)
		total_wanted=0;

	changeUnits("spawn-manager", WORKER, 0, team->swarms.size()*(total_wanted/CREATION_UNIT_REQUIREMENT+1), 0);

	for (std::list<Building*>::iterator i = team->swarms.begin(); i != team->swarms.end(); ++i)
	{
		Building* swarm=*i;
		bool changed=false;
		for (int x = 0; x<NB_UNIT_TYPE; x++)
			if (swarm->ratio[x]!=ratios[x])
				changed=true;

		if(swarm->maxUnitWorking != total_wanted/CREATION_UNIT_REQUIREMENT+1)
			orders.push(new OrderModifyBuilding(swarm->gid, total_wanted/CREATION_UNIT_REQUIREMENT+1));

		if(!changed)
			continue;

		if(AIHelper_DEBUG)
			std::cout<<"AIHelper: moderateSpawns: Turning changing production ratios on a swarm from {Worker:"<<swarm->ratio[0]<<", Explorer:"<<swarm->ratio[1]<<", Warrior:"<<swarm->ratio[2]<<"} to {Worker:"<<ratios[0]<<", Explorer:"<<ratios[1]<<", Warrior:"<<ratios[2]<<"}. Assigning "<<total_wanted/CREATION_UNIT_REQUIREMENT+1<<" workers."<<endl;
		orders.push(new OrderModifySwarm(swarm->gid, ratios));
	}
}




void AIHelper::recordInns()
{
	for(int i=0; i<1024; ++i)
	{
		Building* b=team->myBuildings[i];
		if (b)
		{
			if(b->type->shortTypeNum==IntBuildingType::FOOD_BUILDING)
			{
				innRecord i = inns[b->gid];
				i.records[i.pos].food_amount=b->ressources[CORN];
				i.records[i.pos].units_eating=b->unitsInside.size();
				i.pos+=1;
				if (i.pos==INN_RECORD_MAX)
				{
					i.pos=0;
				}
				inns[b->gid]=i;
			}
		}
	}
}




struct innScore
{
	Building* inn;
	int food_score;
	int unit_score;
	innScore() {food_score=0; unit_score=0;}
};

struct finalInnScore
{
	Building* inn;
	int food_score;
	int food_index;
	int unit_score;
	int unit_index;
	finalInnScore() {food_score=0; food_index=0; unit_score=0; unit_index=0;}
};

//One inn is greater than another if it has less food. If they are equal, however, it will compare the unit amounts.
bool compare_food(const innScore& first, const innScore& other)
{
	if (first.food_score == other.food_score)
		return first.unit_score<other.unit_score;

	return first.food_score>other.food_score;
}




//One inn is greater than another if it has less units. If they are equal, however, it will compare the food amounts.
bool compare_unit(const innScore& first, const innScore& other)
{
	if (first.unit_score == other.unit_score)
		return first.food_score>other.food_score;

	return first.unit_score<other.unit_score;
}




//One final inn score is greater than another if its food_score+unit_score is greater than the others.
//If in doubt (a tie), check which building has the higher unit score (without food score added in).
bool compare_final(const finalInnScore& first, const finalInnScore& other)
{
	if (first.food_index+first.unit_index == other.food_index+other.unit_index)
	{
		if(first.unit_index<other.unit_index)
			return true;
		return false;
	}
	return first.food_index+first.unit_index < other.food_index+other.unit_index;
}




void AIHelper::modifyInns()
{

	std::vector<innScore> scores;

	for(std::map<int, innRecord>::iterator i = inns.begin(); i!=inns.end();)
	{
		innScore score;
		score.inn=getBuildingFromGid(i->first);
		if (score.inn==NULL)
		{
			inns.erase(i);
			continue;
		}
		for (vector<singleInnRecord>::iterator record = i->second.records.begin(); record!=i->second.records.end(); ++record)
		{
			score.food_score+=record->food_amount;
			score.unit_score+=record->units_eating;
		}
		scores.push_back(score);
		i++;
	}
	if(scores.size()==0)
		return;

	std::vector<innScore> scores_total_food(scores);
	std::vector<innScore> scores_total_units(scores);

	sort(scores_total_food.begin(), scores_total_food.end(), &compare_food);
	sort(scores_total_units.begin(), scores_total_units.end(), &compare_unit);

	std::vector<finalInnScore> score_finals;

	int total_workers_needed=0;

	for (std::vector<innScore>::iterator i = scores.begin(); i!=scores.end(); ++i)
	{
		finalInnScore final_score;
		final_score.inn = i->inn;
		total_workers_needed+=i->inn->maxUnitWorking;
		for (std::vector<innScore>::iterator food_i = scores_total_food.begin(); food_i!=scores_total_food.end(); ++food_i)
		{
			if(i->inn == food_i->inn)
			{
				final_score.food_index=food_i-scores_total_food.begin();
				final_score.food_score=food_i->food_score;
				break;
			}
		}
		for (std::vector<innScore>::iterator unit_i = scores_total_units.begin(); unit_i!=scores_total_units.end(); ++unit_i)
		{
			if(i->inn == unit_i->inn)
			{
				final_score.unit_index=unit_i-scores_total_units.begin();
				final_score.unit_score=unit_i->unit_score;
				break;
			}
		}
		score_finals.push_back(final_score);
	}

	sort(score_finals.begin(), score_finals.end(), &compare_final);

	Building* to_increase;
	Building* to_decrease;

	for (std::vector<finalInnScore>::reverse_iterator i = score_finals.rbegin(); i!=score_finals.rend(); i++)
	{
		to_increase=i->inn;
		if(i-score_finals.rbegin()<(score_finals.size()/2) && to_increase->maxUnitWorking<INN_MAX[to_increase->type->level])
		{
			if(AIHelper_DEBUG)
				std::cout<<"AIHelper: modifyInns: Increasing the number of units assigned to an inn."<<endl;
			total_workers_needed+=1;
			orders.push(new OrderModifyBuilding(to_increase->gid, to_increase->maxUnitWorking+1));
			break;
		}
	}

	for (std::vector<finalInnScore>::iterator i = score_finals.begin(); i!=score_finals.end(); i++)
	{
		to_decrease=i->inn;
		if(i-score_finals.begin()<(score_finals.size()/2) && to_decrease->maxUnitWorking>INN_MINIMUM[to_decrease->type->level])
		{
			if(AIHelper_DEBUG)
				std::cout<<"AIHelper: modifyInns: Decreasing the number of units assigned to an inn."<<endl;
			total_workers_needed-=1;
			orders.push(new OrderModifyBuilding(to_decrease->gid, to_decrease->maxUnitWorking-1));
			break;
		}
	}
	changeUnits("inn-manager", WORKER, inns.size(), total_workers_needed, 0);
}




void AIHelper::findDefense()
{

}




void AIHelper::updateFlags()
{

}




void AIHelper::findCreatedDefenseFlags()
{

}




void AIHelper::controlTowers()
{

	int count=0;
	for(int i=0; i<1024; i++)
	{
		Building* b = team->myBuildings[i];
		if (b)
		{
			if(b->type->shortTypeNum == IntBuildingType::DEFENSE_BUILDING &&
				b->buildingState==Building::ALIVE &&
				b->constructionResultState==Building::NO_CONSTRUCTION)
			{
				count+=1;
				if(b->maxUnitWorking != NUM_PER_TOWER)
				{
					if(AIHelper_DEBUG)
						std::cout<<"AIHelper: controlTowers: Changing number of units assigned to a tower, from "<<b->maxUnitWorking<<" to "<<NUM_PER_TOWER<<"."<<endl;
					orders.push(new OrderModifyBuilding(b->gid, NUM_PER_TOWER));
				}
			}
		}
	}
	changeUnits("tower-controller", WORKER, 0, count*NUM_PER_TOWER, 0);
}
