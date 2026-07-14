// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "AINicowar.h"
#include <string>
#include "Utilities.h"
#include "Game.h"
#include "Unit.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace boost::logic;



void NewNicowar::compute_defense_flag_positioning(AIEcho::Echo& echo)
{
	//This algorithm works by finding all units and buildings under attack, and creating a potential
	//field by adding 1 to all squares within range of the units or buildings under attack. The result
	//will be that the highest square will have the largest number of buildings or units that need
	//defending within range. A flag is put onto the highest square, and the same concept is repeated,
	//except that all under-attack units or buildings that are within range of a placed defense flag
	//are ignored.
	
	//This algorithm does that, except optimized. A list is maintained to keep track of squares
	//that have a value other than 0 as these are the only ones we want to place a flag on, and
	//when a defense flag position is chosen, all units or buildings within range of the flag
	//have all squares within their range -1, effectivly doing the same as recalculating all
	//squares excluding those units now covered by a defense flag
	MapInfo     mi(echo);
	const int   w      = mi.get_width();
	const int   h      = mi.get_height();
	const int   RADIUS = AI_NICOWAR_DEFENSE_FLAG_RADIUS;
	
	Uint16* counts = new Uint16[w * h];
	Uint16* buildingGID = new Uint16[w * h];
	Uint16* unitGID = new Uint16[w * h];
	memset(counts, 0, sizeof(Uint16) * w * h);
	memset(buildingGID, NOGBID, sizeof(Uint16) * w * h);
	memset(unitGID, NOGUID, sizeof(Uint16) * w * h);
	std::list<int> locations;
	
	//For every unit thats under attack, increment in the squares surrounding it.
	//Use the 'locations' list to keep track of non-zero squares
	for(int i=0; i<Unit::MAX_COUNT; ++i)
	{
		Unit* unit = echo.player->team->myUnits[i];
		if(unit && unit->underAttackTimer && unit->movement != Unit::MOV_ATTACKING_TARGET && unit->typeNum != EXPLORER && unitGID[(unit->posX+w)%w * h + (unit->posY+h)%h] == NOGUID)
		{
			unitGID[(unit->posX+w)%w * h + (unit->posY+h)%h] = unit->gid;
			modify_points(counts, w, h, (unit->posX+w)%w, (unit->posY+h)%h, RADIUS, 1, locations);
		}
	}
	for(int i=0; i<Building::MAX_COUNT; ++i)
	{
		Building* building = echo.player->team->myBuildings[i];
		// Wrap the building corner (posX/posY can be negative when a building
		// straddles the map seam) before indexing buildingGID, exactly as the unit
		// loop above does. Without the wrap a negative coord aliases the GID marker
		// onto a far tile the decrement scan below (which reads wrapped nx/ny) can
		// never reach, so that square's count never clears and the same tile is
		// chosen as a flag position twice — tripping the assert() below.
		if(building && building->underAttackTimer && buildingGID[(building->posX+w)%w * h + (building->posY+h)%h] == NOGBID)
		{
			int nx = (building->posX - building->type->decLeft + w) %w;
			int ny = (building->posY - building->type->decTop + h) %h;
			buildingGID[(building->posX+w)%w * h + (building->posY+h)%h] = building->gid;
			modify_points(counts, w, h, nx, ny, RADIUS, 1, locations);
		}
	}
	
	///Choose the highest location, remove all units and buildings within a flags radius of that location,
	///and add that location to the list
	std::vector<int> flagLocations;
	std::vector<int> enemyUnits;
	while(!locations.empty())
	{
		//Find the square with the highest value, a flag is put here
		int max = 0;
		int maxPos = 0;
		for(std::list<int>::iterator i = locations.begin(); i!=locations.end(); ++i)
		{
			int pos = *i;
			int n = counts[pos];
			if(n > max)
			{
				maxPos = pos;
				max = n;
			}
		}

		// Inserting twice the same flag is a bug and may lead to an
		// infinite loop. The most probable cause is an insufficient
		// margin in the loop on all units and buildings below.
		for (std::vector<int>::const_iterator i = flagLocations.begin();
		     i != flagLocations.end();
		     ++i)
			assert (*i != maxPos);
		flagLocations.push_back(maxPos);
		
		int max_x = maxPos / h;
		int max_y = maxPos % h;

		//test(echo, counts, w, h, squareProtected, locations);
		
		//For all units and buildings that are under attack and within the radius of the flag, 
		//decrement the values surrounding them. At the same time, count the number of enemy
		//warriors in this zone
		int enemy_count = 0;
		// We need to loop over an area slightly bigger than RADIUS
		// because buildings are taken into account in an offset
		// location
		for(int px = -RADIUS-AI_NICOWAR_DEFENSE_BUILDING_OFFSET_MARGIN; px <= RADIUS+AI_NICOWAR_DEFENSE_BUILDING_OFFSET_MARGIN; ++px)
		{
			int nx = (max_x + px + w)%w;
			for(int py = -RADIUS-AI_NICOWAR_DEFENSE_BUILDING_OFFSET_MARGIN; py<=RADIUS+AI_NICOWAR_DEFENSE_BUILDING_OFFSET_MARGIN; ++py)
			{
				int ny = (max_y + py + h)%h;
				if(unitGID[nx * h + ny] != NOGUID)
				{
					Unit* unit = echo.player->team->myUnits[Unit::GIDtoID(unitGID[nx * h + ny])];
					modify_points(counts, w, h, (unit->posX+w)%w, (unit->posY+h)%h, RADIUS, -1, locations);
					unitGID[nx * h + ny] = NOGUID;
				}
				if(buildingGID[nx * h + ny] != NOGBID)
				{
					Building* building = echo.player->team->myBuildings[Building::GIDtoID(buildingGID[nx * h + ny])];
					int nx2 = (building->posX - building->type->decLeft + w) %w;
					int ny2 = (building->posY - building->type->decTop + h) %h;
					modify_points(counts, w, h, nx2, ny2, RADIUS, -1, locations);
					buildingGID[nx * h + ny] = NOGBID;
				}

				// Take enemy units into account only if they are
				// within RADIUS of the flag (remember that we loop
				// over a bigger area).
				if ((px >= -RADIUS) && (px <= RADIUS) && (py >= -RADIUS) && (py <= RADIUS)) {
					Uint16 guid = echo.player->map->getGroundUnit(nx, ny);
					if(guid != NOGUID && (1<<Unit::GIDtoTeam(guid)) & echo.player->team->enemies)
					{
						Unit* unit = echo.player->game->teams[Unit::GIDtoTeam(guid)]->myUnits[Unit::GIDtoID(guid)];
						if(unit->typeNum == WARRIOR)
						{
							enemy_count += 1;
						}
					}
				}
			}
		}
		enemyUnits.push_back(std::min(AI_NICOWAR_MAX_DEFENSE_FLAG_WORKERS, enemy_count));
	}
	
	//Remove all flags with an enemy_count of 0
	for(std::vector<int>::iterator i=flagLocations.begin(); i!=flagLocations.end();)
	{
		int n = i-flagLocations.begin();
		if(enemyUnits[n] == 0)
		{
			i = flagLocations.erase(i);
			enemyUnits.erase(enemyUnits.begin() + n);
		}
		else
		{
			++i;
		}
	}

	//Take all existing defense flags, and move them to the nearest new flag position
	std::vector<int> existing_defense_flags(defense_flags);
	while(!existing_defense_flags.empty())
	{
		int min_dist = INT_MAX;
		int min_flag = 0;
		int min_pos = 0;
		int min_pos_x = 0;
		int min_pos_y = 0;
		int min_enemy = 0;
		///Choose the flag <-> flag location combination that has the lowest distance, start from it
		for(std::vector<int>::iterator i = existing_defense_flags.begin(); i!=existing_defense_flags.end(); ++i)
		{
			if(echo.get_building_register().is_building_found(*i))
			{
				Building* b = echo.get_building_register().get_building(*i);
				for(std::vector<int>::iterator j = flagLocations.begin(); j!=flagLocations.end(); ++j)
				{
					int flag_x = (*j) / h;
					int flag_y = (*j) % h;
					int d = echo.player->map->warpDistSquare(flag_x, flag_y, b->posX, b->posY);
					if(d < min_dist)
					{
						min_dist = d;
						min_flag = i - existing_defense_flags.begin();
						min_pos = j - flagLocations.begin();
						min_pos_x = flag_x;
						min_pos_y = flag_y;
						min_enemy = enemyUnits[j - flagLocations.begin()];
					}
				}
			}
		}
		//Don't move flags more than 8 squares
		if(min_dist < (AI_NICOWAR_DEFENSE_FLAG_MAX_MOVE_TILES * AI_NICOWAR_DEFENSE_FLAG_MAX_MOVE_TILES))
		{
			int id_flag = existing_defense_flags[min_flag];
			existing_defense_flags.erase(existing_defense_flags.begin() + min_flag);
			flagLocations.erase(flagLocations.begin() + min_pos);
			enemyUnits.erase(enemyUnits.begin() + min_pos);
			
			if(min_dist>0)
			{
				ManagementOrder* mo_move=new ChangeFlagPosition(min_pos_x, min_pos_y, id_flag);
				echo.add_management_order(mo_move);
			}
			if(min_enemy != echo.get_building_register().get_assigned(id_flag))
			{
				ManagementOrder* mo_assign=new AssignWorkers(min_enemy, id_flag);
				echo.add_management_order(mo_assign);
			}
		}
		else
		{
			break;
		}
	}
	//If there are remaining flags, its because these flags don't have a new position
	//on the map to go to, so delete them
	for(std::vector<int>::iterator i = existing_defense_flags.begin(); i!=existing_defense_flags.end(); ++i)
	{
		if(echo.get_building_register().is_building_found(*i))
		{
		    Building* b = echo.get_building_register().get_building(*i);
		    int enemy_count = 0;
		    for(int px = -AI_NICOWAR_DEFENSE_REASSIGN_RADIUS; px <= AI_NICOWAR_DEFENSE_REASSIGN_RADIUS; ++px)
		    {
				int nx = (b->posX + px + w)%w;
				for(int py = -AI_NICOWAR_DEFENSE_REASSIGN_RADIUS; py<=AI_NICOWAR_DEFENSE_REASSIGN_RADIUS; ++py)
				{
						int ny = (b->posY + py + h)%h;
						Uint16 guid = echo.player->map->getGroundUnit(nx, ny);
						if(guid != NOGUID && (1<<Unit::GIDtoTeam(guid)) & echo.player->team->enemies)
						{
								Unit* unit = echo.player->game->teams[Unit::GIDtoTeam(guid)]->myUnits[Unit::GIDtoID(guid)];
								if(unit->typeNum == WARRIOR)
								{
										enemy_count += 1;
								}
						}
				}
		    }
		    if(enemy_count == 0)
		    {
		            ManagementOrder* mo_destroyed=new DestroyBuilding(*i);
		            echo.add_management_order(mo_destroyed);
		    }
		    else
		    {
		            if(enemy_count != echo.get_building_register().get_assigned(*i))
		            {
		                    ManagementOrder* mo_assign=new AssignWorkers(std::min(AI_NICOWAR_MAX_DEFENSE_FLAG_WORKERS, enemy_count), *i);
		                    echo.add_management_order(mo_assign);
		            }
		    }
		}
	}
	
	//If there are remaining positions on the map, it is because we didn't have enough existing
	//flags to cover them, so create new ones
	for(std::vector<int>::iterator i = flagLocations.begin(); i!=flagLocations.end(); ++i)
	{
		int enemy = enemyUnits[i - flagLocations.begin()];
		int flag_x = *i / h;
		int flag_y = *i % h;

		//The main order for the war flag
		BuildingOrder* bo_flag = new BuildingOrder(IntBuildingType::WAR_FLAG, enemy);
		bo_flag->add_constraint(new Construction::SinglePosition(flag_x, flag_y));
		unsigned int id_flag=echo.add_building_order(bo_flag);
		defense_flags.push_back(id_flag);

		ManagementOrder* mo_completion=new ChangeFlagSize(AI_NICOWAR_DEFENSE_FLAG_SIZE, id_flag);
		echo.add_management_order(mo_completion);

		ManagementOrder* mo_destroyed=new SendMessage("guard flag deleted " + std::to_string(id_flag));
		mo_destroyed->add_condition(new BuildingDestroyed(id_flag));
		echo.add_management_order(mo_destroyed);
	}
	
	delete[] counts;
	delete[] unitGID;
	delete[] buildingGID;
}



void NewNicowar::modify_points(Uint16* counts, int w, int h, int x, int y, int dist, int value, std::list<int>& locations)
{
	for(int px = -dist; px <= dist; ++px)
	{
		int nx = (x + px + w)%w;
		for(int py = -dist; py <= dist; ++py)
		{
			int ny = (y + py + h)%h;
			if(px * px + py * py <= dist * dist)
			{
				if(value>0)
				{
					if(counts[nx * h + ny] == 0)
						locations.push_back(nx * h + ny);
					counts[nx * h + ny] += value;
				}
				else if(value<0)
				{
					counts[nx * h + ny] += value;
					if(counts[nx * h + ny] == 0)
						locations.remove(nx * h + ny);
				}
			}
		}
	}
}



void NewNicowar::compute_explorer_flag_attack_positioning(AIEcho::Echo& echo)
{
	//The algorithm here is interesting. Bassically, an enemy unit is selected. Every enemy unit within 4 squares of this unit
	//is counted as part of the larger group, and every unit 4 squares from those and so on, as long as it doesn't go past
	//6 squares from the average. Flags are put on the average x and y of largest groups
	MapInfo mi(echo);
	const int w = mi.get_width();
	const int h = mi.get_height();

	std::vector<std::tuple<int, int, int> > groups;
	
	if(explorer_attack_phase && target!=-1)
	{
		std::vector<Unit*> units(Unit::MAX_COUNT, nullptr);
		Unit* first = NULL;
		for(int i=0; i<Unit::MAX_COUNT; ++i)
		{
			Unit* unit = echo.player->game->teams[target]->myUnits[i];
			if(unit && mi.is_discovered(unit->posX, unit->posY) && unit->typeNum != EXPLORER && unit->activity != Unit::ACT_UPGRADING)
			{
				if(!first)
					first = unit;
				units[i] = unit;
			}
			else
			{
				units[i] = NULL;
			}
		}
		
		while(true)
		{
			int group_x = 0;
			int group_y = 0;
			int group_size = 0;
		
			std::queue<Unit*> proccess;
			std::queue<int> xposs;
			std::queue<int> yposs;
			for(int i=0; i<Unit::MAX_COUNT; ++i)
			{
				if(units[i])
				{
					group_x += units[i]->posX;
					group_y += units[i]->posY;
					proccess.push(units[i]);
					xposs.push(units[i]->posX);
					yposs.push(units[i]->posY);
					units[i] = NULL;
					group_size+=1;
					break;
				}
			}
			
			if(group_size == 0)
				break;
			
			while(!proccess.empty())
			{
				Unit* top = proccess.front();
				int ix = xposs.front();
				int iy = yposs.front();
				proccess.pop();
				xposs.pop();
				yposs.pop();
				for(int dx = -AI_NICOWAR_EXPLORER_GROUP_SEARCH_RADIUS; dx<=AI_NICOWAR_EXPLORER_GROUP_SEARCH_RADIUS; ++dx)
				{
					int nx = (top->posX + dx + w) % w;
					for(int dy = -AI_NICOWAR_EXPLORER_GROUP_SEARCH_RADIUS; dy<=AI_NICOWAR_EXPLORER_GROUP_SEARCH_RADIUS; ++dy)
					{
						int ny = (top->posY + dy + h) % h;
						if(echo.player->map->warpDistSquare(group_x / group_size, group_y / group_size, nx, ny) < (AI_NICOWAR_EXPLORER_GROUP_COHESION_TILES * AI_NICOWAR_EXPLORER_GROUP_COHESION_TILES))
						{
							Uint16 guid = echo.player->map->getGroundUnit(nx, ny);
							if(guid != NOGUID && Unit::GIDtoTeam(guid) == target)
							{
								int id = Unit::GIDtoID(guid);
								if(units[id])
								{
									group_x += ix + dx;
									group_y += iy + dy;
									proccess.push(units[id]);
									xposs.push(ix + dx);
									yposs.push(iy + dy);
									units[id] = NULL;
									group_size+=1;
								}
							}
						}
					}
				}
			}
			group_x = (group_x / group_size + w)%w;
			group_y = (group_y / group_size + h)%h;
			
			groups.push_back(std::make_tuple(group_size, group_x, group_y));
		}
	}
	
	std::sort(groups.begin(), groups.end(), std::greater<std::tuple<int, int, int> >());
	int total_attacks = strategy.offense_explorer_flag_number;
	if(!explorer_attack_phase)
		total_attacks = 0;
	
	//Go through existing flags and see if they can be moved to be on top of new groups
	std::vector<int> existing_explorer_attack_flags(explorer_attack_flags);
	while(total_attacks && !existing_explorer_attack_flags.empty())
	{
		int min_dist = INT_MAX;
		int min_flag = 0;
		int min_pos = 0;
		int min_pos_x = 0;
		int min_pos_y = 0;
		///Choose the flag <-> flag location combination that has the lowest distance, start from it
		for(std::vector<int>::iterator i = existing_explorer_attack_flags.begin(); i!=existing_explorer_attack_flags.end(); ++i)
		{
			if(echo.get_building_register().is_building_found(*i))
			{
				Building* b = echo.get_building_register().get_building(*i);
				for(std::vector<std::tuple<int, int, int> >::iterator j = groups.begin(); j!=groups.end(); ++j)
				{
					int flag_x = std::get<1>(*j);
					int flag_y = std::get<2>(*j);
					int d = echo.player->map->warpDistSquare(flag_x, flag_y, b->posX, b->posY);
					if(d < min_dist)
					{
						min_dist = d;
						min_flag = i - existing_explorer_attack_flags.begin();
						min_pos = j - groups.begin();
						min_pos_x = flag_x;
						min_pos_y = flag_y;
					}
				}
			}
		}
		
		if(min_dist != INT_MAX)
		{
			total_attacks-=1;
			int id_flag = existing_explorer_attack_flags[min_flag];

			existing_explorer_attack_flags.erase(existing_explorer_attack_flags.begin() + min_flag);
			groups.erase(groups.begin() + min_pos);
			
			if(min_dist != 0)
			{
				ManagementOrder* mo_move=new ChangeFlagPosition(min_pos_x, min_pos_y, id_flag);
				echo.add_management_order(mo_move);
			}
		}
		else
		{
			break;
		}
	}
	
	//If there are remaining flags, its because these flags don't have a new position
	//on the map to go to, so delete them
	for(std::vector<int>::iterator i = existing_explorer_attack_flags.begin(); i!=existing_explorer_attack_flags.end(); ++i)
	{
		if(echo.get_building_register().is_building_found(*i))
		{
			ManagementOrder* mo_destroyed=new DestroyBuilding(*i);
			echo.add_management_order(mo_destroyed);
		}
	}
	
	while(total_attacks && !groups.empty())
	{
		std::tuple<int, int, int> groupInfo = *groups.begin();
		groups.erase(groups.begin());
		total_attacks -= 1;
			
		BuildingOrder* bo_flag = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, strategy.offense_explorer_flag_assigned);
		bo_flag->add_constraint(new Construction::SinglePosition(std::get<1>(groupInfo), std::get<2>(groupInfo)));
		unsigned int id_flag=echo.add_building_order(bo_flag);

		ManagementOrder* mo_completion=new ChangeFlagSize(AI_NICOWAR_EXPLORER_ATTACK_FLAG_SIZE, id_flag);
		echo.add_management_order(mo_completion);

		// [POSSIBLE BUG / preserved] Skill levels run 0..3; passing 4 here
		// either locks the flag entirely or is silently capped at 3 by the
		// engine. See bugs_surfaced_during_magic_number_audit.md M8.
		ManagementOrder* mo_level=new ChangeFlagMinimumLevel(AI_NICOWAR_EXPLORER_ATTACK_MIN_LEVEL, id_flag);
		echo.add_management_order(mo_level);
		
		explorer_attack_flags.push_back(id_flag);
		
		ManagementOrder* mo_destroyed=new SendMessage("explorer attack flag deleted " + std::to_string(id_flag));
		mo_destroyed->add_condition(new BuildingDestroyed(id_flag));
		echo.add_management_order(mo_destroyed);
	}
}


