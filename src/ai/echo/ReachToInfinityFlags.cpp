// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "IntBuildingType.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace boost::logic;


//Explorer flags on the three nearest fruit trees
void ReachToInfinity::tick_explorer_flags_fruit(Echo& echo)
{
	if((timer%AI_ECHO_RTI_FRUIT_FLAG_INTERVAL_TICKS)==0)
	{
		if(echo.is_fruit_on_map())
		{
			if(echo.get_team_stats().numberUnitPerType[EXPLORER]>=AI_ECHO_RTI_FRUIT_FLAG_EXPLORER_MIN && !flag_on_cherry && !flag_on_orange && !flag_on_prune)
			{
				//Constraints arround nearby settlement
				AIEcho::Gradients::GradientInfo gi_building;
				gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));

				if(!flag_on_cherry)
				{
					//The main order for the exploration flag
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

					if(id_cherry!=INVALID_BUILDING)
					{
						ManagementOrder* mo_completion=new ChangeFlagSize(AI_ECHO_RTI_FRUIT_FLAG_RADIUS, id_cherry);
						echo.add_management_order(mo_completion);
						flag_on_cherry=true;

						for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
						{
							ManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, true, indeterminate);
							echo.add_management_order(mo_alliance);
						}
					}
				}

				if(!flag_on_orange)
				{
					//The main order for the exploration flag
					BuildingOrder* bo_orange = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, 2);

					//You want the closest fruit to your settlement possible
					bo_orange->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));

					//Constraints arround the location of fruit
					AIEcho::Gradients::GradientInfo gi_orange;
					gi_orange.add_source(new AIEcho::Gradients::Entities::Ressource(ORANGE));
					//You want to be ontop of the orange trees
					bo_orange->add_constraint(new AIEcho::Construction::MaximumDistance(gi_orange, 0));

					unsigned int id_orange=echo.add_building_order(bo_orange);

					if(id_orange!=INVALID_BUILDING)
					{
						ManagementOrder* mo_completion=new ChangeFlagSize(AI_ECHO_RTI_FRUIT_FLAG_RADIUS, id_orange);
						echo.add_management_order(mo_completion);
						flag_on_orange=true;

						for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
						{
							ManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, true, indeterminate);
							echo.add_management_order(mo_alliance);
						}
					}
				}

				if(!flag_on_prune)
				{
					//The main order for the exploration flag
					BuildingOrder* bo_prune = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, 2);

					//You want the closest fruit to your settlement possible
					bo_prune->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 1));

					AIEcho::Gradients::GradientInfo gi_prune;
					gi_prune.add_source(new AIEcho::Gradients::Entities::Ressource(PRUNE));
					//You want to be ontop of the prune trees
					bo_prune->add_constraint(new AIEcho::Construction::MaximumDistance(gi_prune, 0));

					//Add the building order to the list of orders
					unsigned int id_prune=echo.add_building_order(bo_prune);

					if(id_prune!=INVALID_BUILDING)
					{
						ManagementOrder* mo_completion=new ChangeFlagSize(AI_ECHO_RTI_FRUIT_FLAG_RADIUS, id_prune);
						echo.add_management_order(mo_completion);
						flag_on_prune=true;

						for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
						{
							ManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, true, indeterminate);
							echo.add_management_order(mo_alliance);
						}
					}
				}
			}
		}
	}
}

//Place exploration flags on the enemy swarms
void ReachToInfinity::tick_explorer_flags_enemies(Echo& echo)
{
	if((timer%AI_ECHO_RTI_ENEMY_SCAN_INTERVAL_TICKS)==0)
	{
		if(echo.get_team_stats().numberUnitPerType[EXPLORER]>=AI_ECHO_RTI_ENEMY_FLAG_EXPLORER_MIN)
		{
			for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
			{
				for(enemy_building_iterator ebi(echo, *i, IntBuildingType::SWARM_BUILDING, AI_ECHO_WILDCARD_LEVEL, false); ebi!=enemy_building_iterator(); ++ebi)
				{
					if(flags_on_enemy.find(*i)!=flags_on_enemy.end())
						continue;

					BuildingOrder* bo = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, 1);
					bo->add_constraint(new CenterOfBuilding(*ebi));
					unsigned int id=echo.add_building_order(bo);

					if(id!=INVALID_BUILDING)
					{
						ManagementOrder* mo_completion=new ChangeFlagSize(AI_ECHO_RTI_ENEMY_FLAG_RADIUS, id);
						echo.add_management_order(mo_completion);

						ManagementOrder* mo_destroyed=new DestroyBuilding(id);
						mo_destroyed->add_condition(new EnemyBuildingDestroyed(echo, *ebi));
						echo.add_management_order(mo_destroyed);

						flags_on_enemy.insert(*i);
					}
				}
			}
		}
	}
}

//Farming wheat and wood near water
void ReachToInfinity::tick_farming_areas(Echo& echo)
{
	if((timer%AI_ECHO_RTI_FARMING_INTERVAL_TICKS)==0)
	{
		AddArea* mo_farming=new AddArea(ForbiddenArea);
		RemoveArea* mo_non_farming=new RemoveArea(ForbiddenArea);
		AIEcho::Gradients::GradientInfo gi_water;
		gi_water.add_source(new Entities::Water);
		Gradient& gradient=echo.get_gradient_manager().get_gradient(gi_water);
		MapInfo mi(echo);
		for(int x=0; x<mi.get_width(); ++x)
		{
			for(int y=0; y<mi.get_height(); ++y)
			{
				if((x%AI_ECHO_RTI_FARMING_PATTERN_STRIDE==1 && y%AI_ECHO_RTI_FARMING_PATTERN_STRIDE==1))
				{
					if((!mi.is_ressource(x, y, WOOD) &&
					    !mi.is_ressource(x, y, CORN)) &&
					    mi.is_forbidden_area(x, y))
					{
						mo_non_farming->add_location(x, y);
					}
					else
					{
						if((mi.is_ressource(x, y, WOOD) ||
						    mi.is_ressource(x, y, CORN)) &&
						    mi.is_discovered(x, y) &&
						    !mi.is_forbidden_area(x, y) &&
						    gradient.within_dist(x, y, AI_ECHO_RTI_FARMING_WATER_MAX_DIST))
						{
							mo_farming->add_location(x, y);
						}
					}
				}
			}
		}
		echo.add_management_order(mo_farming);
		echo.add_management_order(mo_non_farming);
	}
}
