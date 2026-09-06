// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include <algorithm>
#include "IntBuildingType.h"
#include <iterator>
#include "Utilities.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;


//Standard Inns near wheat
void ReachToInfinity::tick_inns_near_wheat(Echo& echo)
{
	if((timer%AI_ECHO_RTI_INN_INTERVAL_TICKS)==0 && (timer%AI_ECHO_RTI_BIG_CYCLE_TICKS)!=0)
	{
		BuildingSearch bs_level1(echo);
		bs_level1.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
		bs_level1.add_condition(new BuildingLevel(1));
		const int number1=bs_level1.count_buildings();

		BuildingSearch bs_level2(echo);
		bs_level2.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
		bs_level2.add_condition(new BuildingLevel(2));
		const int number2=bs_level2.count_buildings();

		BuildingSearch bs_level3(echo);
		bs_level3.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
		bs_level3.add_condition(new BuildingLevel(3));
		const int number3=bs_level3.count_buildings();

		if((echo.player->team->stats.getLatestStat()->totalUnit)>=(number1*AI_ECHO_RTI_INN_POP_PER_L1 + number2*AI_ECHO_RTI_INN_POP_PER_L2 + number3*AI_ECHO_RTI_INN_POP_PER_L3))
		{
			//The main order for the inn
			BuildingOrder* bo = new BuildingOrder(IntBuildingType::FOOD_BUILDING, 2);

			//Constraints arround the location of wheat
			AIEcho::Gradients::GradientInfo gi_wheat;
			gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
			//You want to be close to wheat
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, AI_ECHO_RTI_INN_WHEAT_WEIGHT));
			//You can't be farther than 10 units from wheat
			bo->add_constraint(new AIEcho::Construction::MaximumDistance(gi_wheat, AI_ECHO_RTI_INN_WHEAT_MAX_DIST));

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, AI_ECHO_RTI_BUILD_CLUSTER_WEIGHT));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, AI_ECHO_RTI_INN_CONSTRUCTION_MIN_DIST));

			if(echo.is_fruit_on_map())
			{
				//Constraints arround the location of fruit
				AIEcho::Gradients::GradientInfo gi_fruit;
				gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(CHERRY));
				gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(ORANGE));
				gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(PRUNE));
				//You want to be reasnobly close to fruit, closer if possible
				bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_fruit, AI_ECHO_RTI_INN_FRUIT_WEIGHT));
			}

			//Add the building order to the list of orders
			unsigned int id=echo.add_building_order(bo);

//			std::cout<<"inn ordered, id="<<id<<std::endl;

			ManagementOrder* mo_completion=new AssignWorkers(1, id);
			mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_completion);

			ManagementOrder* mo_tracker=new AddRessourceTracker(AI_ECHO_RTI_TRACKER_LENGTH, CORN, id);
			mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_tracker);
		}
	}
}

//Standard swarms near wheat. Uses special mechanism, builds more swarms early on.
void ReachToInfinity::tick_swarms_near_wheat(Echo& echo)
{
	if((timer%AI_ECHO_RTI_BIG_CYCLE_TICKS)==AI_ECHO_RTI_SWARM_OFFSET_TICKS)
	{
		BuildingSearch bs(echo);
		bs.add_condition(new SpecificBuildingType(IntBuildingType::SWARM_BUILDING));
		const int number=bs.count_buildings();
		if((number<=AI_ECHO_RTI_SWARM_EARLY_LIMIT && (echo.player->team->stats.getLatestStat()->totalUnit/AI_ECHO_RTI_SWARM_EARLY_RATIO)>=number) ||
		   (echo.player->team->stats.getLatestStat()->totalUnit/AI_ECHO_RTI_SWARM_LATE_RATIO)>=number)
		{
//			std::cout<<"Constructing swarm"<<std::endl;
			//The main order for the swarm
			BuildingOrder* bo = new BuildingOrder(IntBuildingType::SWARM_BUILDING, AI_ECHO_RTI_SWARM_WORKERS_NEW);

			//Constraints arround the location of wheat
			AIEcho::Gradients::GradientInfo gi_wheat;
			gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
			//You want to be close to wheat
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, AI_ECHO_RTI_INN_WHEAT_WEIGHT));

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, AI_ECHO_RTI_SWARM_CLUSTER_WEIGHT));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, AI_ECHO_RTI_INN_CONSTRUCTION_MIN_DIST));

			//Add the building order to the list of orders
			unsigned int id=echo.add_building_order(bo);

//			std::cout<<"Swarm ordered, id="<<id<<std::endl;

			//Change the number of workers assigned when the building is finished
			ManagementOrder* mo_completion=new AssignWorkers(AI_ECHO_RTI_SWARM_WORKERS_FINISHED, id);
			mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_completion);

			//Change the ratio of the swarm when its finished
			ManagementOrder* mo_ratios=new ChangeSwarm(AI_ECHO_RTI_SWARM_RATIO_WORKER, AI_ECHO_RTI_SWARM_RATIO_EXPLORER, AI_ECHO_RTI_SWARM_RATIO_WARRIOR, id);
			mo_ratios->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_ratios);

			//Add a tracker
			ManagementOrder* mo_tracker=new AddRessourceTracker(AI_ECHO_RTI_TRACKER_LENGTH, CORN, id);
			mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_tracker);

		}
	}
}

//Standard racetrack near stone and wood
void ReachToInfinity::tick_racetrack_near_stone_wood(Echo& echo)
{
	if((timer%AI_ECHO_RTI_BIG_CYCLE_TICKS)==AI_ECHO_RTI_RACETRACK_OFFSET_TICKS)
	{
		BuildingSearch bs(echo);
		bs.add_condition(new SpecificBuildingType(IntBuildingType::WALKSPEED_BUILDING));
		const int number=bs.count_buildings();
		if((echo.player->team->stats.getLatestStat()->totalUnit/AI_ECHO_RTI_SECONDARY_BLDG_RATIO)>=number && number<AI_ECHO_RTI_RACETRACK_MAX)
		{
			//The main order for the racetrack
			BuildingOrder* bo = new BuildingOrder(IntBuildingType::WALKSPEED_BUILDING, AI_ECHO_RTI_RACETRACK_WORKERS);

			//Constraints arround the location of wood
			AIEcho::Gradients::GradientInfo gi_wood;
			gi_wood.add_source(new AIEcho::Gradients::Entities::Ressource(WOOD));
			//You want to be close to wood
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wood, AI_ECHO_RTI_RACETRACK_WOOD_WEIGHT));

			//Constraints arround the location of stone
			AIEcho::Gradients::GradientInfo gi_stone;
			gi_stone.add_source(new AIEcho::Gradients::Entities::Ressource(STONE));
			//You want to be close to stone
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_stone, AI_ECHO_RTI_RACETRACK_STONE_WEIGHT));
			//But not to close, so you have room to upgrade
			bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_stone, AI_ECHO_RTI_RACETRACK_STONE_MIN_DIST));

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, AI_ECHO_RTI_BUILD_CLUSTER_WEIGHT));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, AI_ECHO_RTI_RACETRACK_CONSTR_MIN_DIST));

			//Add the building order to the list of orders
			echo.add_building_order(bo);
		}
	}
}

//Standard swimming pool near wheat and wood
void ReachToInfinity::tick_swimmingpool_near_wheat_wood(Echo& echo)
{
	if((timer%AI_ECHO_RTI_BIG_CYCLE_TICKS)==AI_ECHO_RTI_SWIMMINGPOOL_OFFSET_TICKS)
	{
		BuildingSearch bs(echo);
		bs.add_condition(new SpecificBuildingType(IntBuildingType::SWIMSPEED_BUILDING));
		const int number=bs.count_buildings();
		if((echo.player->team->stats.getLatestStat()->totalUnit/AI_ECHO_RTI_SECONDARY_BLDG_RATIO)>=number && number<AI_ECHO_RTI_SWIMMINGPOOL_MAX)
		{
			//The main order for the swimmingpool
			BuildingOrder* bo = new BuildingOrder(IntBuildingType::SWIMSPEED_BUILDING, AI_ECHO_RTI_SWIMMINGPOOL_WORKERS);

			//Constraints arround the location of wood
			AIEcho::Gradients::GradientInfo gi_wood;
			gi_wood.add_source(new AIEcho::Gradients::Entities::Ressource(WOOD));
			//You want to be close to wood
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wood, AI_ECHO_RTI_SWIMMINGPOOL_WOOD_WEIGHT));

			//Constraints arround the location of wheat
			AIEcho::Gradients::GradientInfo gi_wheat;
			gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
			//You want to be close to wheat
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, AI_ECHO_RTI_SWIMMINGPOOL_WHEAT_WEIGHT));

			//Constraints arround the location of stone
			AIEcho::Gradients::GradientInfo gi_stone;
			gi_stone.add_source(new AIEcho::Gradients::Entities::Ressource(STONE));
			//You don't want to be too close, so you have room to upgrade
			bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_stone, AI_ECHO_RTI_SWIMMINGPOOL_STONE_MIN_DIST));

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, AI_ECHO_RTI_BUILD_CLUSTER_WEIGHT));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, AI_ECHO_RTI_SWIMMINGPOOL_CONSTR_MIN_DIST));

			//Add the building order to the list of orders
			echo.add_building_order(bo);
		}
	}
}


//Standard school inland away from the enemies
void ReachToInfinity::tick_school_inland(Echo& echo)
{
	if((timer%AI_ECHO_RTI_BIG_CYCLE_TICKS)==AI_ECHO_RTI_SCHOOL_OFFSET_TICKS)
	{
		BuildingSearch bs(echo);
		bs.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
		const int number=bs.count_buildings();
		if((echo.player->team->stats.getLatestStat()->totalUnit/AI_ECHO_RTI_SECONDARY_BLDG_RATIO)>=number && number<AI_ECHO_RTI_SCHOOL_MAX)
		{
			//The main order for the school
			BuildingOrder* bo = new BuildingOrder(IntBuildingType::SCIENCE_BUILDING, AI_ECHO_RTI_SCHOOL_WORKERS);

			//Constraints arround nearby settlement
			AIEcho::Gradients::GradientInfo gi_building;
			gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
			gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, AI_ECHO_RTI_BUILD_CLUSTER_WEIGHT));

			AIEcho::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			//You don't want to be too close
			bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, AI_ECHO_RTI_SCHOOL_CONSTR_MIN_DIST));

			//Constraints arround the enemy
			AIEcho::Gradients::GradientInfo gi_enemy;
			for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
			{
				gi_enemy.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(*i, false));
			}
			gi_enemy.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
			bo->add_constraint(new AIEcho::Construction::MaximizedDistance(gi_enemy, AI_ECHO_RTI_SCHOOL_ENEMY_DIST_WEIGHT));

			//Add the building order to the list of orders
			echo.add_building_order(bo);
		}
	}
}


//Level 1 to level 2 upgrades
void ReachToInfinity::tick_upgrade_l1_to_l2(Echo& echo)
{
	if((timer%AI_ECHO_RTI_UPGRADE_INTERVAL_TICKS)==0)
	{
		BuildingSearch level_twos(echo);
		level_twos.add_condition(new BeingUpgradedTo(AI_ECHO_RTI_UPGRADE_TARGET_LEVEL_2));
		const int level_two_counts=level_twos.count_buildings();

		BuildingSearch schools(echo);
		schools.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
		schools.add_condition(new NotUnderConstruction);
		const int school_counts=schools.count_buildings();

		BuildingSearch buildings(echo);
		buildings.add_condition(new BuildingLevel(1));
		const int total_buildings=buildings.count_buildings();
		if(level_two_counts<=(total_buildings/AI_ECHO_RTI_CONCURRENT_UPGRADE_FRACTION) && school_counts>0)
		{
			BuildingSearch bs(echo);
			bs.add_condition(new Upgradable);
			bs.add_condition(new BuildingLevel(1));
			if(school_counts<AI_ECHO_RTI_SCHOOL_THRESHOLD_FOR_UPGRADE)
				bs.add_condition(new NotSpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
			std::vector<int> buildings;
			std::copy(bs.begin(), bs.end(), std::back_insert_iterator<std::vector<int> >(buildings));

			if(buildings.size()!=0)
			{
				int chosen=syncRand()%buildings.size();
				ManagementOrder* uro = new UpgradeRepair(buildings[chosen]);
				echo.add_management_order(uro);

				int assigned=echo.get_building_register().get_assigned(buildings[chosen]);

				ManagementOrder* mo_assign=new AssignWorkers(AI_ECHO_RTI_UPGRADE_WORKERS_DURING, buildings[chosen]);
				mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, buildings[chosen]));
				echo.add_management_order(mo_assign);

				if(echo.get_building_register().get_type(buildings[chosen])==IntBuildingType::FOOD_BUILDING)
				{
					ManagementOrder* mo_tracker_pause=new PauseRessourceTracker(buildings[chosen]);
					mo_tracker_pause->add_condition(new ParticularBuilding(new UnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_tracker_pause);

					ManagementOrder* mo_tracker_unpause=new UnPauseRessourceTracker(buildings[chosen]);
					mo_tracker_unpause->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_tracker_unpause);

					ManagementOrder* mo_completion=new AssignWorkers(AI_ECHO_RTI_INN_L2_WORKERS_FINISHED, buildings[chosen]);
					mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_completion);
				}
				else
				{
					ManagementOrder* mo_assign=new AssignWorkers(assigned, buildings[chosen]);
					mo_assign->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_assign);
				}
			}
		}
	}
}

//Level 2 to level 3 upgrades
void ReachToInfinity::tick_upgrade_l2_to_l3(Echo& echo)
{
	if((timer%AI_ECHO_RTI_UPGRADE_INTERVAL_TICKS)==0)
	{
		BuildingSearch level_threes(echo);
		level_threes.add_condition(new BeingUpgradedTo(AI_ECHO_RTI_UPGRADE_TARGET_LEVEL_3));
		const int level_three_counts=level_threes.count_buildings();

		BuildingSearch schools(echo);
		schools.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
		schools.add_condition(new NotUnderConstruction);
		schools.add_condition(new BuildingLevel(2));
		int school_counts=schools.count_buildings();

		BuildingSearch schools2(echo);
		schools2.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
		schools2.add_condition(new NotUnderConstruction);
		schools2.add_condition(new BuildingLevel(3));
		school_counts+=schools2.count_buildings();

		BuildingSearch buildings(echo);
		buildings.add_condition(new BuildingLevel(2));
		const int total_buildings=buildings.count_buildings();
		if(level_three_counts<=(total_buildings/AI_ECHO_RTI_CONCURRENT_UPGRADE_FRACTION) && school_counts>0)
		{
			BuildingSearch bs(echo);
			bs.add_condition(new Upgradable);
			bs.add_condition(new BuildingLevel(2));
			if(school_counts<AI_ECHO_RTI_SCHOOL_THRESHOLD_FOR_UPGRADE)
				bs.add_condition(new NotSpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
			std::vector<int> buildings;
			std::copy(bs.begin(), bs.end(), std::back_insert_iterator<std::vector<int> >(buildings));

			if(buildings.size()!=0)
			{
				int chosen=syncRand()%buildings.size();
				ManagementOrder* uro = new UpgradeRepair(buildings[chosen]);
				echo.add_management_order(uro);

				int assigned=echo.get_building_register().get_assigned(buildings[chosen]);

				ManagementOrder* mo_assign=new AssignWorkers(AI_ECHO_RTI_UPGRADE_WORKERS_DURING, buildings[chosen]);
				mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, buildings[chosen]));
				echo.add_management_order(mo_assign);

				if(echo.get_building_register().get_type(buildings[chosen])==IntBuildingType::FOOD_BUILDING)
				{
					ManagementOrder* mo_tracker_pause=new PauseRessourceTracker(buildings[chosen]);
					mo_tracker_pause->add_condition(new ParticularBuilding(new UnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_tracker_pause);

					ManagementOrder* mo_tracker_unpause=new UnPauseRessourceTracker(buildings[chosen]);
					mo_tracker_unpause->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_tracker_unpause);

					ManagementOrder* mo_completion=new AssignWorkers(AI_ECHO_RTI_INN_L3_WORKERS_FINISHED, buildings[chosen]);
					mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_completion);
				}
				else
				{
					ManagementOrder* mo_assign=new AssignWorkers(assigned, buildings[chosen]);
					mo_assign->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					echo.add_management_order(mo_assign);
				}
			}
		}
	}
}



//Delete old inns and swarms that are hard to keep full of wheat
void ReachToInfinity::tick_delete_old_inns_swarms(Echo& echo)
{
	if((timer%AI_ECHO_RTI_DELETE_SCAN_INTERVAL_TICKS)==0)
	{
		BuildingSearch inns(echo);
		inns.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
		inns.add_condition(new NotUnderConstruction);
		for(building_search_iterator i=inns.begin(); i!=inns.end(); ++i)
		{
			std::shared_ptr<RessourceTracker> rt=echo.get_ressource_tracker(*i);
			if(rt)
			{
				if(rt->get_age()>AI_ECHO_RTI_INN_DELETE_AGE_TICKS)
				{
					if(rt->get_total_level() < AI_ECHO_RTI_INN_DELETE_FOOD_PER_LEVEL*echo.get_building_register().get_level(*i))
					{
						ManagementOrder* mo_destroy=new DestroyBuilding(*i);
						echo.add_management_order(mo_destroy);
					}
				}
			}
		}


		BuildingSearch swarms(echo);
		swarms.add_condition(new SpecificBuildingType(IntBuildingType::SWARM_BUILDING));
		swarms.add_condition(new NotUnderConstruction);
		for(building_search_iterator i=swarms.begin(); i!=swarms.end(); ++i)
		{
			std::shared_ptr<RessourceTracker> rt=echo.get_ressource_tracker(*i);
			if(rt)
			{
				if(rt->get_age()>AI_ECHO_RTI_SWARM_DELETE_AGE_TICKS)
				{
					if(rt->get_total_level() < AI_ECHO_RTI_SWARM_DELETE_FOOD)
					{
						ManagementOrder* mo_destroy=new DestroyBuilding(*i);
						echo.add_management_order(mo_destroy);
					}
				}
			}
		}
	}
}
