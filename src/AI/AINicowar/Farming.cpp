/*
  Copyright (C) 2006 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "AINicowar.h"
#include "GlobalContainer.h"
#include "FormatableString.h"
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



void NewNicowar::update_farming(Echo& echo)
{
	//Farming wheat and wood in areas near water
	AddArea* mo_farming=new AddArea(ForbiddenArea);
	RemoveArea* mo_non_farming=new RemoveArea(ForbiddenArea);
	AIEcho::Gradients::GradientInfo gi_water;
	gi_water.add_source(new Entities::Water);
	Gradient& water_gradient=echo.get_gradient_manager().get_gradient(gi_water);

	MapInfo mi(echo);
	for(int x=0; x<mi.get_width(); ++x)
	{
		for(int y=0; y<mi.get_height(); ++y)
		{
			if(mi.is_discovered(x, y))
			{
				const int wood_dist = 6;
				const int wheat_dist = 10;

				bool is_wood = mi.is_ressource(x, y, WOOD);
				bool is_wheat = mi.is_ressource(x, y, CORN);

				bool is_in_wheat_zone = water_gradient.within_dist(x, y, wheat_dist);
				bool is_in_wood_zone = water_gradient.within_dist(x, y, wood_dist);

				bool farm_spot = false;

				//Permament farming exists for every second row and column
				if(x%2==1 && y%2==1)
				{
					if((is_wood && is_in_wood_zone) || (is_wheat && is_in_wheat_zone))
					{
						farm_spot = true;
					}
				}

				//Expand the farm horizontally
				if((x%2==0 && y%2==1))
				{
					if(is_wood && mi.is_ressource(x-1, y, WOOD) && !mi.is_ressource(x+1,y) && water_gradient.within_dist(x+1, y, wood_dist) && mi.is_grass(x+1,y))
					{
						farm_spot = true;
					}
					else if(is_wheat && mi.is_ressource(x-1, y, CORN) && !mi.is_ressource(x+1,y) && water_gradient.within_dist(x+1, y, wheat_dist) && mi.is_grass(x+1,y))
					{
						farm_spot = true;
					}
					else if(is_wood && mi.is_ressource(x+1, y, WOOD) && !mi.is_ressource(x-1,y) && water_gradient.within_dist(x-1, y, wood_dist) && mi.is_grass(x-1,y))
					{
						farm_spot = true;
					}
					else if(is_wheat && mi.is_ressource(x+1, y, CORN) && !mi.is_ressource(x-1,y) && water_gradient.within_dist(x-1, y, wheat_dist) && mi.is_grass(x-1,y))
					{
						farm_spot = true;
					}
				}

				//Expand the farm vertically
				if((x%2==1 && y%2==0))
				{
					if(is_wood && mi.is_ressource(x, y-1, WOOD) && !mi.is_ressource(x,y+1) && water_gradient.within_dist(x, y+1, wood_dist) && mi.is_grass(x,y+1))
					{
						farm_spot = true;
					}
					else if(is_wheat && mi.is_ressource(x, y-1, CORN) && !mi.is_ressource(x,y+1) && water_gradient.within_dist(x, y+1, wheat_dist) && mi.is_grass(x,y+1))
					{
						farm_spot = true;
					}
					else if(is_wood && mi.is_ressource(x, y+1, WOOD) && !mi.is_ressource(x,y-1) && water_gradient.within_dist(x, y-1, wood_dist) && mi.is_grass(x,y-1))
					{
						farm_spot = true;
					}
					else if(is_wheat && mi.is_ressource(x, y+1, CORN) && !mi.is_ressource(x,y-1) && water_gradient.within_dist(x, y-1, wheat_dist) && mi.is_grass(x,y-1))
					{
						farm_spot = true;
					}
				}


				if(farm_spot && mi.is_clearing_area(x,y))
				{
					farm_spot = false;
				}

				if(farm_spot && mi.is_sand(x,y))
				{
					farm_spot = false;
				}

				if(farm_spot && !mi.is_forbidden_area(x, y))
				{
					mo_farming->add_location(x, y);
				}
				else if(!farm_spot && mi.is_forbidden_area(x, y))
				{
					mo_non_farming->add_location(x, y);
				}
			}
		}
	}
	echo.add_management_order(mo_farming);
	echo.add_management_order(mo_non_farming);
}


void NewNicowar::update_fruit_flags(AIEcho::Echo& echo)
{
	if(fruit_phase && !exploration_on_fruit)
	{
		//Constraints arround nearby settlement
		AIEcho::Gradients::GradientInfo gi_building;
		gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));


		//The main order for the exploration flag on cherry
		BuildingOrder* bo_cherry = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, 2);
		//You want the closest fruit to your settlement possible
		bo_cherry->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));
		//Constraint arround the location of fruit
		AIEcho::Gradients::GradientInfo gi_cherry;
		gi_cherry.add_source(new AIEcho::Gradients::Entities::Ressource(CHERRY));
		//You want to be ontop of the cherry trees
		bo_cherry->add_constraint(new AIEcho::Construction::MaximumDistance(gi_cherry, 0));
		//Add the building order to the list of orders
		unsigned int id_cherry=echo.add_building_order(bo_cherry);

		ManagementOrder* mo_completion_cherry=new ChangeFlagSize(4, id_cherry);
		echo.add_management_order(mo_completion_cherry);



		//The main order for the exploration flag in orange
		BuildingOrder* bo_orange = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, 2);
		//You want the closest fruit to your settlement possible
		bo_orange->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));
		//Constraints arround the location of fruit
		AIEcho::Gradients::GradientInfo gi_orange;
		gi_orange.add_source(new AIEcho::Gradients::Entities::Ressource(ORANGE));
		//You want to be ontop of the orange trees
		bo_orange->add_constraint(new AIEcho::Construction::MaximumDistance(gi_orange, 0));
		unsigned int id_orange=echo.add_building_order(bo_orange);

		ManagementOrder* mo_completion_orange=new ChangeFlagSize(4, id_orange);
		echo.add_management_order(mo_completion_orange);

		//The main order for the exploration flag on prunes
		BuildingOrder* bo_prune = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, 2);
		//You want the closest fruit to your settlement possible
		bo_prune->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));
		AIEcho::Gradients::GradientInfo gi_prune;
		gi_prune.add_source(new AIEcho::Gradients::Entities::Ressource(PRUNE));
		//You want to be ontop of the prune trees
		bo_prune->add_constraint(new AIEcho::Construction::MaximumDistance(gi_prune, 0));
		//Add the building order to the list of orders
		unsigned int id_prune=echo.add_building_order(bo_prune);

		ManagementOrder* mo_completion_prune=new ChangeFlagSize(4, id_prune);
		echo.add_management_order(mo_completion_prune);



		exploration_on_fruit=true;
	}
	update_fruit_alliances(echo);
}


void NewNicowar::update_fruit_alliances(AIEcho::Echo& echo)
{
	bool activated=fruit_phase;

	for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
	{
		ManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, activated, indeterminate);
		echo.add_management_order(mo_alliance);
	}
}

