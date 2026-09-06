// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Building.h"
#include <map>
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "Game.h"
#include "Utilities.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace boost::logic;
using std::shared_ptr;



building_search_iterator::building_search_iterator() : found_id(AI_ECHO_ITER_NOT_STARTED), is_end(true), search(NULL)
{

}


building_search_iterator::building_search_iterator(BuildingSearch& search) : found_id(AI_ECHO_ITER_NOT_STARTED), is_end(false), search(&search)
{
	set_to_next();
}



const unsigned int building_search_iterator::operator*()
{
	return found_id;
}



building_search_iterator& building_search_iterator::operator++()
{
	set_to_next();
	return *this;
}



building_search_iterator building_search_iterator::operator++(int)
{
	building_search_iterator copy(*this);
	set_to_next();
	return copy;
}



bool building_search_iterator::operator!=(const building_search_iterator& rhs) const
{
	if(is_end==rhs.is_end)
		return false;
	return is_end!=rhs.is_end || position!=rhs.position || found_id!=rhs.found_id ;
}



void building_search_iterator::set_to_next()
{
	Construction::BuildingRegister::found_iterator positionSaved = position;
	if(is_end)
		return;
	if(found_id==AI_ECHO_ITER_NOT_STARTED)
	{
		position=search->echo.get_building_register().begin();
	}
	else
		position++;
	for(; position!=search->echo.get_building_register().end() && !search->passes_conditions(position->first); position++)
	{
	}
	if(position==search->echo.get_building_register().end())
	{
		is_end=true;
		return;
	}
	if(position->first==AI_ECHO_ITER_NOT_STARTED && positionSaved==position)
	{                        // This fixes an infinit loop.
		is_end=true;     // In some special cases the program Logic 
		return;          // must have been wrong.
	}
	found_id=position->first;
}



BuildingSearch::BuildingSearch(Echo& echo) : echo(echo)
{

}



void BuildingSearch::add_condition(Conditions::BuildingCondition* condition)
{
	conditions.push_back(std::shared_ptr<Conditions::BuildingCondition>(condition));
}



int BuildingSearch::count_buildings()
{
	int count=0;
	for(Construction::BuildingRegister::found_iterator i=echo.get_building_register().begin(); i!=echo.get_building_register().end(); ++i)
	{
		if(passes_conditions(i->first))
		{
			count++;
		}
	}
	return count;
}



building_search_iterator BuildingSearch::begin()
{
	return building_search_iterator(*this);
}



building_search_iterator BuildingSearch::end()
{
	return building_search_iterator();
}



bool BuildingSearch::passes_conditions(int b)
{
	for(std::vector<std::shared_ptr<Conditions::BuildingCondition> >::iterator i = conditions.begin();  i!=conditions.end(); ++i)
	{
		if(!(*i)->passes(echo, b))
			return false;
	}
	return true;
}


enemy_team_iterator::enemy_team_iterator(Echo& echo) :  team_number(AI_ECHO_ITER_NOT_STARTED), is_end(false), echo(&echo)
{
	set_to_next();
}


enemy_team_iterator::enemy_team_iterator() : team_number(AI_ECHO_ITER_NOT_STARTED), is_end(true), echo(NULL)
{

}


const unsigned int enemy_team_iterator::operator*()
{
	return team_number;
}


enemy_team_iterator& enemy_team_iterator::operator++()
{
	set_to_next();
	return *this;
}


enemy_team_iterator enemy_team_iterator::operator++(int)
{
	enemy_team_iterator copy(*this);
	set_to_next();
	return copy;
}


bool enemy_team_iterator::operator!=(const enemy_team_iterator& rhs) const
{
	if(rhs.is_end && is_end)
		return false;
	return rhs.is_end != is_end || rhs.team_number!=team_number;
}


void enemy_team_iterator::set_to_next()
{
	if(is_end)
		return;
	if(team_number==AI_ECHO_ITER_NOT_STARTED)
	{
		team_number=0;
	}
	else
		team_number++;
	for(; echo->player->team->game->teams[team_number]!=NULL && !(echo->player->team->enemies & echo->player->team->game->teams[team_number]->me); team_number++)
	{
	}

	if(echo->player->team->game->teams[team_number]==NULL)
	{
		is_end=true;
		return;
	}

}


int SearchTools::is_flag(Echo& echo, int x, int y)
{
	Building** buildings=echo.player->team->myBuildings;
	for(int n=0; n<Building::MAX_COUNT; ++n)
	{
		Building* b=buildings[n];
		if(b)
		{
			if(b->posX==x && b->posY==y)
			{
				if(b->type->shortTypeNum > (int)(IntBuildingType::DEFENSE_BUILDING) && b->type->shortTypeNum < (int)(IntBuildingType::STONE_WALL))
				{
					return b->gid;
				}
			}
		}
	}
	return NOGBID;
}




enemy_building_iterator::enemy_building_iterator() : is_end(true)
{

}



enemy_building_iterator::enemy_building_iterator(Echo& echo, int team, int building_type, int level, boost::logic::tribool construction_site) : current_gid(AI_ECHO_ITER_NOT_STARTED), team(team), building_type(building_type), level(level), construction_site(construction_site), is_end(false), echo(&echo)
{
	set_to_next();
}



const unsigned int enemy_building_iterator::operator*()
{
	return current_gid;
}



enemy_building_iterator& enemy_building_iterator::operator++()
{
	set_to_next();
	return *this;
}



enemy_building_iterator enemy_building_iterator::operator++(int)
{
	enemy_building_iterator copy(*this);
	set_to_next();
	return copy;
}



bool enemy_building_iterator::operator!=(const enemy_building_iterator& rhs) const
{
	if(is_end && rhs.is_end)
		return false;
	return is_end!=rhs.is_end || team!=rhs.team || building_type!=rhs.building_type || level!=rhs.level || bool(construction_site!=rhs.construction_site);
}



void enemy_building_iterator::set_to_next()
{
	if(current_gid==AI_ECHO_ITER_NOT_STARTED)
	{
		current_index=0;
	}
	else
		current_index++;

	while(current_index<Building::MAX_COUNT)
	{
		Building* b=echo->player->game->teams[team]->myBuildings[current_index];
		if(b)
		{
			if( (b->seenByMask&echo->player->team->me
                             // Don't allow AIs to cheat!!!!!!
                             // || echo->get_starting_buildings().find(b->gid)!=echo->get_starting_buildings().end()
                             ) &&
				(building_type==AI_ECHO_WILDCARD_TYPE || b->type->shortTypeNum==building_type) &&
				(level==AI_ECHO_WILDCARD_LEVEL || b->type->level==(level-AI_ECHO_LEVEL_OFFSET_USER_TO_ENGINE)))
			{
				if(construction_site)
				{
					if(b->type->isBuildingSite)
					{
						current_gid=b->gid;
						break;
					}
				}
				else if(!construction_site)
				{
					if(!b->type->isBuildingSite)
					{
						current_gid=b->gid;
						break;
					}
				}
				else
				{
					current_gid=b->gid;
					break;
				}
			}
		}
		current_index++;
	}

	if(current_index==Building::MAX_COUNT)
		is_end=true;
}


