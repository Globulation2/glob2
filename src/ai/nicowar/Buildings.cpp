// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "AINicowar.h"
#include "FormatableString.h"
#include <string>
#include "Unit.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace boost::logic;



void NewNicowar::queue_buildings(Echo& echo)
{
	queue_racetracks(echo);
	queue_swimmingpools(echo);
	queue_schools(echo);
	queue_barracks(echo);
	queue_hospitals(echo);
	queue_inns(echo);
	queue_swarms(echo);
}


void NewNicowar::queue_inns(Echo& echo)
{
	//Get some statistics
	TeamStat* stat=echo.player->team->stats.getLatestStat();
	int total_workers=stat->numberUnitPerType[WORKER];
	int total_explorers=stat->numberUnitPerType[EXPLORER];
	int total_warriors=stat->numberUnitPerType[WARRIOR];

	//Count the number of inns there are at each level
	BuildingSearch bs_level1(echo);
	bs_level1.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
	bs_level1.add_condition(new BuildingLevel(1));
	bs_level1.add_condition(new NotUnderConstruction);
	const int number1=bs_level1.count_buildings() + buildings_under_construction_per_type[RegularInn];

	BuildingSearch bs_level2(echo);
	bs_level2.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
	bs_level2.add_condition(new BuildingLevel(2));
	const int number2=bs_level2.count_buildings();

	BuildingSearch bs_level3(echo);
	bs_level3.add_condition(new SpecificBuildingType(IntBuildingType::FOOD_BUILDING));
	bs_level3.add_condition(new BuildingLevel(3));
	const int number3=bs_level3.count_buildings();

	const int score =
		  number1*strategy.level_1_inn_units_can_feed
		+ number2*strategy.level_2_inn_units_can_feed
		+ number3*strategy.level_3_inn_units_can_feed;

	///(by default), A level 1 Inn can handle 8 units, a level 2 can handle 12 and a level 3 can handle 16
	if((total_workers+total_explorers+total_warriors)>=score)
	{
		placement_queue.push_back(RegularInn);
	}
	
	//Place for starving recovery inns
	if(starving_recovery)
	{
		int total_starving = stat->needFoodNoInns;
		int required_inns = total_starving / strategy.starving_recovery_phase_unfed_per_new_inn;
		if(starving_recovery_inns < required_inns)
		{
			starving_recovery_inns += 1;
			placement_queue.push_back(StarvingRecoveryInn);
		}
	}
}


void NewNicowar::queue_swarms(Echo& echo)
{
	BuildingSearch bs(echo);
	bs.add_condition(new SpecificBuildingType(IntBuildingType::SWARM_BUILDING));
	bs.add_condition(new NotUnderConstruction);
	const int swarm_count = bs.count_buildings() + buildings_under_construction_per_type[RegularSwarm];
	const int total_unit = echo.player->team->stats.getLatestStat()->totalUnit;
	int demand=0;
	if(growth_phase)
	{
		demand = std::min(strategy.growth_phase_maximum_swarms, 1 + total_unit/strategy.growth_phase_units_per_swarm);
	}
	else
	{
		demand = (total_unit/strategy.non_growth_phase_units_per_swarm);
	}

	if(demand > swarm_count)
	{
		placement_queue.push_back(RegularSwarm);
	}
}


void NewNicowar::queue_racetracks(Echo& echo)
{
	BuildingSearch bs_finished(echo);
	bs_finished.add_condition(new SpecificBuildingType(IntBuildingType::WALKSPEED_BUILDING));
	bs_finished.add_condition(new NotUnderConstruction);

	BuildingSearch bs_upgrading(echo);
	bs_upgrading.add_condition(new SpecificBuildingType(IntBuildingType::WALKSPEED_BUILDING));
	bs_upgrading.add_condition(new BeingUpgraded);

	const int racetrack_count=bs_finished.count_buildings() + bs_upgrading.count_buildings() + buildings_under_construction_per_type[RegularRacetrack];
	//const int total_unit = echo.player->team->stats.getLatestStat()->totalUnit;
	int demand=0;
	if(skilled_work_phase)
	{
		demand=strategy.skilled_work_phase_number_of_racetracks;
	}

	if(demand > racetrack_count)
	{
		placement_queue.push_back(RegularRacetrack);
	}
}


void NewNicowar::queue_swimmingpools(Echo& echo)
{
	BuildingSearch bs_finished(echo);
	bs_finished.add_condition(new SpecificBuildingType(IntBuildingType::SWIMSPEED_BUILDING));
	bs_finished.add_condition(new NotUnderConstruction);

	BuildingSearch bs_upgrading(echo);
	bs_upgrading.add_condition(new SpecificBuildingType(IntBuildingType::SWIMSPEED_BUILDING));
	bs_upgrading.add_condition(new BeingUpgraded);

	const int swimmingpool_count=bs_finished.count_buildings() + bs_upgrading.count_buildings() + buildings_under_construction_per_type[RegularSwimmingpool];
	//const int total_unit = echo.player->team->stats.getLatestStat()->totalUnit;
	int demand=0;
	if(skilled_work_phase)
	{
		demand=strategy.skilled_work_phase_number_of_swimmingpools;
	}

	if(demand > swimmingpool_count)
	{
		placement_queue.push_back(RegularSwimmingpool);
	}
}


void NewNicowar::queue_schools(Echo& echo)
{
	BuildingSearch bs_finished(echo);
	bs_finished.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	bs_finished.add_condition(new NotUnderConstruction);

	BuildingSearch bs_upgrading(echo);
	bs_upgrading.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	bs_upgrading.add_condition(new BeingUpgraded);

	const int school_count=bs_finished.count_buildings() + bs_upgrading.count_buildings() + buildings_under_construction_per_type[RegularSchool];
	//const int total_unit = echo.player->team->stats.getLatestStat()->totalUnit;
	int demand=0;
	if(skilled_work_phase)
	{
		demand=strategy.skilled_work_phase_number_of_schools;
	}

	if(demand > school_count)
	{
		placement_queue.push_back(RegularSchool);
	}
}


void NewNicowar::queue_barracks(Echo& echo)
{
	BuildingSearch bs_finished(echo);
	bs_finished.add_condition(new SpecificBuildingType(IntBuildingType::ATTACK_BUILDING));
	bs_finished.add_condition(new NotUnderConstruction);

	BuildingSearch bs_upgrading(echo);
	bs_upgrading.add_condition(new SpecificBuildingType(IntBuildingType::ATTACK_BUILDING));
	bs_upgrading.add_condition(new BeingUpgraded);

	const int barracks_count=bs_finished.count_buildings() + bs_upgrading.count_buildings() + buildings_under_construction_per_type[RegularBarracks];

	int demand=0;
	if(war_preperation)
	{
		demand=strategy.war_preparation_phase_number_of_barracks;
		///This only kicks in right at the start, so that it doesn't build barracks when it doesn't need to
		demand = std::min(demand, echo.player->team->stats.getLatestStat()->isFree[WARRIOR] / AI_NICOWAR_BARRACKS_FREE_WARRIOR_DIVISOR);
	}

	if(demand > barracks_count)
	{
		placement_queue.push_back(RegularBarracks);
	}
}


void NewNicowar::queue_hospitals(Echo& echo)
{
	BuildingSearch bs_finished(echo);
	bs_finished.add_condition(new SpecificBuildingType(IntBuildingType::HEAL_BUILDING));
	bs_finished.add_condition(new NotUnderConstruction);

	BuildingSearch bs_upgrading(echo);
	bs_upgrading.add_condition(new SpecificBuildingType(IntBuildingType::HEAL_BUILDING));
	bs_upgrading.add_condition(new BeingUpgraded);

	const int hospital_count=bs_finished.count_buildings() + bs_upgrading.count_buildings() + buildings_under_construction_per_type[RegularHospital];
	const int total_warrior = echo.player->team->stats.getLatestStat()->numberUnitPerType[WARRIOR];

	int demand=0;
	if(echo.player->team->stats.getLatestStat()->needHeal > 0)
		demand += strategy.base_number_of_hospitals;
	if(war_preperation || war)
	{
		demand+=total_warrior/strategy.war_preperation_phase_warriors_per_hospital;
	}

	if(demand > hospital_count)
	{
		placement_queue.push_back(RegularHospital);
	}
}



void NewNicowar::order_buildings(Echo& echo)
{
	while(!placement_queue.empty())
	{
		BuildingPlacement b=placement_queue.front();
		placement_queue.erase(placement_queue.begin());
		construction_queue.push_back(b);
		buildings_under_construction_per_type[int(b)]+=1;
	}
	///Increase the maximum number of buildings under construction when starving recovery is active
	int maximum_under_construction = strategy.base_number_of_construction_sites;
	if(starving_recovery)
		maximum_under_construction += strategy.starving_recovery_phase_number_of_extra_construction_sites;

	while(!construction_queue.empty() && buildings_under_construction < maximum_under_construction)
	{
		int id=-1;
		BuildingPlacement b=construction_queue.front();
		construction_queue.erase(construction_queue.begin());
		if(b==RegularInn)
		{
			id=order_regular_inn(echo);
		}
		if(b==StarvingRecoveryInn)
		{
			id=order_regular_inn(echo);
			ManagementOrder* mo_completion_message=new SendMessage("finished starving recovery inn");
			mo_completion_message->add_condition(new EitherCondition(
			                             new ParticularBuilding(new NotUnderConstruction, id),
			                             new BuildingDestroyed(id)));
			echo.add_management_order(mo_completion_message);
		}
		if(b==RegularSwarm)
		{
			id=order_regular_swarm(echo);
		}
		if(b==RegularRacetrack)
		{
			id=order_regular_racetrack(echo);
		}
		if(b==RegularSwimmingpool)
		{
			id=order_regular_swimmingpool(echo);
		}
		if(b==RegularSchool)
		{
			id=order_regular_school(echo);
		}
		if(b==RegularBarracks)
		{
			id=order_regular_barracks(echo);
		}
		if(b==RegularHospital)
		{
			id=order_regular_hospital(echo);
		}

		///This code keeps track of the number of buildings that are under construction at any one point
		buildings_under_construction+=1;
		ManagementOrder* mo_completion_message=new SendMessage("building completed "+std::to_string(int(b)));
		mo_completion_message->add_condition(new EitherCondition(
		                             new ParticularBuilding(new NotUnderConstruction, id),
		                             new BuildingDestroyed(id)));
		echo.add_management_order(mo_completion_message);
		if(b == RegularInn || b==RegularSwarm)
		{		
			ManagementOrder* mo_construction_completion_message=new SendMessage("update clearing zone1 "+std::to_string(int(id)));
			mo_construction_completion_message->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_construction_completion_message);
		}
		else
		{
			ManagementOrder* mo_construction_completion_message=new SendMessage("update clearing zone2 "+std::to_string(int(id)));
			mo_construction_completion_message->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_construction_completion_message);
		}
	}
}


int NewNicowar::order_regular_inn(Echo& echo)
{
	//The main order for the inn
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::FOOD_BUILDING, AI_NICOWAR_INN_ORDER_WORKERS);

	//Constraints arround the location of wheat
	AIEcho::Gradients::GradientInfo gi_wheat;
	gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
	//You want to be close to wheat
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, AI_NICOWAR_INN_WHEAT_MIN_DIST));
	//You can't be farther than 10 units from wheat
	bo->add_constraint(new AIEcho::Construction::MaximumDistance(gi_wheat, AI_NICOWAR_INN_WHEAT_MAX_DIST));

	//Constraints about the distance to water.
	AIEcho::Gradients::GradientInfo gi_water;
	gi_water.add_source(new AIEcho::Gradients::Entities::Water);
	//You dont want to be too close to water, so that farm can develop between it and water
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_water, AI_NICOWAR_INN_WATER_MIN_DIST));

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings, but wheat is more important
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, AI_NICOWAR_INN_BUILDING_PREF));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, AI_NICOWAR_INN_CONSTRUCTION_MIN));

	///Add constraints for all enemy teams to keep distance
	AIEcho::Gradients::GradientInfo gi_enemy;
	for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
	{
		gi_enemy.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(*i, false));
	}
	bo->add_constraint(new AIEcho::Construction::MaximizedDistance(gi_enemy, AI_NICOWAR_INN_ENEMY_MAX_DIST));

	if(echo.is_fruit_on_map())
	{
		//Constraints arround the location of fruit
		AIEcho::Gradients::GradientInfo gi_fruit;
		gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(CHERRY));
		gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(ORANGE));
		gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(PRUNE));
		//You want to be reasnobly close to fruit, closer if possible
		bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_fruit, AI_NICOWAR_INN_FRUIT_PREF));
	}

	//Add the building order to the list of orders
	unsigned int id=echo.add_building_order(bo);

	//Change the number of workers assigned when the building is finished
	ManagementOrder* mo_completion=new SendMessage(FormatableString("update inn %0").arg(id));
	mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
	echo.add_management_order(mo_completion);

	ManagementOrder* mo_tracker=new AddRessourceTracker(AI_NICOWAR_RESSOURCE_TRACKER_DEPTH, CORN, id);
	mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
	echo.add_management_order(mo_tracker);

	return id;
}


int NewNicowar::order_regular_swarm(Echo& echo)
{
	//The main order for the swarm
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::SWARM_BUILDING, AI_NICOWAR_SWARM_ORDER_WORKERS);

	//Constraints arround the location of wheat
	AIEcho::Gradients::GradientInfo gi_wheat;
	gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
	//You want to be close to wheat
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, AI_NICOWAR_SWARM_WHEAT_PREF));

	//Constraints about the distance to water.
	AIEcho::Gradients::GradientInfo gi_water;
	gi_water.add_source(new AIEcho::Gradients::Entities::Water);
	//You dont want to be too close to water, so that farm can develop between it and water
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_water, AI_NICOWAR_SWARM_WATER_MIN_DIST));

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings, but wheat is more important
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, AI_NICOWAR_SWARM_BUILDING_PREF));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, AI_NICOWAR_SWARM_CONSTRUCTION_MIN));

	//Add the building order to the list of orders
	unsigned int id=echo.add_building_order(bo);

	//Change the number of workers assigned when the building is finished
	ManagementOrder* mo_completion=new SendMessage(FormatableString("update swarm %0").arg(id));
	mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
	echo.add_management_order(mo_completion);

	ManagementOrder* mo_tracker=new AddRessourceTracker(AI_NICOWAR_RESSOURCE_TRACKER_DEPTH, CORN, id);
	mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
	echo.add_management_order(mo_tracker);

	return id;
}


int NewNicowar::order_regular_racetrack(Echo& echo)
{
	//The main order for the racetrack
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::WALKSPEED_BUILDING, AI_NICOWAR_RACETRACK_ORDER_WORKERS);

	//Constraints arround the location of wood
	AIEcho::Gradients::GradientInfo gi_wood;
	gi_wood.add_source(new AIEcho::Gradients::Entities::Ressource(WOOD));
	//You want to be close to wood
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wood, AI_NICOWAR_RACETRACK_WOOD_PREF));

	//Constraints about the distance to water.
	AIEcho::Gradients::GradientInfo gi_water;
	gi_water.add_source(new AIEcho::Gradients::Entities::Water);
	//You dont want to be too close to water. allows farms to develop
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_water, AI_NICOWAR_RACETRACK_WATER_MIN_DIST));

	//Constraints arround the location of stone
	AIEcho::Gradients::GradientInfo gi_stone;
	gi_stone.add_source(new AIEcho::Gradients::Entities::Ressource(STONE));
	//You want to be close to stone
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_stone, AI_NICOWAR_RACETRACK_STONE_PREF));
	//But not to close, so you have room to upgrade
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_stone, AI_NICOWAR_RACETRACK_STONE_MIN));

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings, but wheat is more important
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, AI_NICOWAR_RACETRACK_BUILDING_PREF));

	//Constraints arround water. Can't be too close to sand.
	AIEcho::Gradients::GradientInfo gi_sand;
	gi_sand.add_source(new AIEcho::Gradients::Entities::Sand);
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_sand, AI_NICOWAR_RACETRACK_SAND_MIN));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, AI_NICOWAR_RACETRACK_CONSTRUCTION_MIN));

	//Add the building order to the list of orders
	int id = echo.add_building_order(bo);

	return id;
}


int NewNicowar::order_regular_swimmingpool(Echo& echo)
{
	//The main order for the swimmingpool
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::SWIMSPEED_BUILDING, AI_NICOWAR_SWIMMINGPOOL_ORDER_WORKERS);

	//Constraints arround the location of wood
	AIEcho::Gradients::GradientInfo gi_wood;
	gi_wood.add_source(new AIEcho::Gradients::Entities::Ressource(WOOD));
	//You want to be close to wood
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wood, AI_NICOWAR_SWIMMINGPOOL_WOOD_PREF));

	//Constraints about the distance to water.
	AIEcho::Gradients::GradientInfo gi_water;
	gi_water.add_source(new AIEcho::Gradients::Entities::Water);
	//You dont want to be too close to water. allows farms to develop
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_water, AI_NICOWAR_SWIMMINGPOOL_WATER_MIN_DIST));

	//Constraints arround the location of wheat
	AIEcho::Gradients::GradientInfo gi_wheat;
	gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
	//You want to be close to wheat
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, AI_NICOWAR_SWIMMINGPOOL_WHEAT_PREF));

	//Constraints arround the location of stone
	AIEcho::Gradients::GradientInfo gi_stone;
	gi_stone.add_source(new AIEcho::Gradients::Entities::Ressource(STONE));
	//You don't want to be too close, so you have room to upgrade
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_stone, AI_NICOWAR_SWIMMINGPOOL_STONE_MIN));

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings, but wheat is more important
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, AI_NICOWAR_SWIMMINGPOOL_BUILDING_PREF));

	//Constraints arround water. Can't be too close to sand.
	AIEcho::Gradients::GradientInfo gi_sand;
	gi_sand.add_source(new AIEcho::Gradients::Entities::Sand);
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_sand, AI_NICOWAR_SWIMMINGPOOL_SAND_MIN));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, AI_NICOWAR_SWIMMINGPOOL_CONSTRUCTION_MIN));

	//Add the building order to the list of orders
	int id = echo.add_building_order(bo);

	return id;
}


int NewNicowar::order_regular_school(Echo& echo)
{
	//The main order for the school
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::SCIENCE_BUILDING, AI_NICOWAR_SCHOOL_ORDER_WORKERS);

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, AI_NICOWAR_SCHOOL_BUILDING_PREF));

	//Constraints about the distance to water.
	AIEcho::Gradients::GradientInfo gi_water;
	gi_water.add_source(new AIEcho::Gradients::Entities::Water);
	//You dont want to be too close to water. allows farms to develop
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_water, AI_NICOWAR_SCHOOL_WATER_MIN_DIST));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, AI_NICOWAR_SCHOOL_CONSTRUCTION_MIN));

	//Constraints arround the enemy
	AIEcho::Gradients::GradientInfo gi_enemy;
	for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
	{
		gi_enemy.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(*i, false));
	}
//	gi_enemy.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	bo->add_constraint(new AIEcho::Construction::MaximizedDistance(gi_enemy, AI_NICOWAR_SCHOOL_ENEMY_MAX_DIST));

	//Add the building order to the list of orders
	int id = echo.add_building_order(bo);

	return id;
}


int NewNicowar::order_regular_barracks(Echo& echo)
{
	//The main order for the barracks
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::ATTACK_BUILDING, AI_NICOWAR_BARRACKS_ORDER_WORKERS);

	//Constraints about the distance to water.
	AIEcho::Gradients::GradientInfo gi_water;
	gi_water.add_source(new AIEcho::Gradients::Entities::Water);
	//You dont want to be too close to water. allows farms to develop
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_water, AI_NICOWAR_BARRACKS_WATER_MIN_DIST));

	//Constraints arround the location of stone
	AIEcho::Gradients::GradientInfo gi_stone;
	gi_stone.add_source(new AIEcho::Gradients::Entities::Ressource(STONE));
	//You want to be close to stone
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_stone, AI_NICOWAR_BARRACKS_STONE_PREF));

	//Constraints arround the location of wood
	AIEcho::Gradients::GradientInfo gi_wood;
	gi_wood.add_source(new AIEcho::Gradients::Entities::Ressource(WOOD));
	//You want to be close to wood
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wood, AI_NICOWAR_BARRACKS_WOOD_PREF));

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, AI_NICOWAR_BARRACKS_BUILDING_PREF));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, AI_NICOWAR_BARRACKS_CONSTRUCTION_MIN));

	//Add the building order to the list of orders
	int id = echo.add_building_order(bo);

	return id;
}


int NewNicowar::order_regular_hospital(Echo& echo)
{
	//The main order for the hospital
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::HEAL_BUILDING, AI_NICOWAR_HOSPITAL_ORDER_WORKERS);

	//Constraints arround the location of wood
	AIEcho::Gradients::GradientInfo gi_wood;
	gi_wood.add_source(new AIEcho::Gradients::Entities::Ressource(WOOD));
	//You want to be close to wood
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wood, AI_NICOWAR_HOSPITAL_WOOD_PREF));

	//Constraints about the distance to water.
	AIEcho::Gradients::GradientInfo gi_water;
	gi_water.add_source(new AIEcho::Gradients::Entities::Water);
	//You dont want to be too close to water. allows farms to develop
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_water, AI_NICOWAR_HOSPITAL_WATER_MIN_DIST));

	//Constraints arround nearby settlement
	AIEcho::Gradients::GradientInfo gi_building;
	gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
	gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You want to be close to other buildings
	bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, AI_NICOWAR_HOSPITAL_BUILDING_PREF));

	AIEcho::Gradients::GradientInfo gi_building_construction;
	gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
	gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
	if(!can_swim)
		gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::Water);
	//You don't want to be too close
	bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, AI_NICOWAR_HOSPITAL_CONSTRUCTION_MIN));

	//Add the building order to the list of orders
	int id = echo.add_building_order(bo);

	return id;

}


void NewNicowar::manage_buildings(Echo& echo)
{
	BuildingSearch bs(echo);
	bs.add_condition(new NotUnderConstruction);
	for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
	{	
		if(echo.get_building_register().get_type(*i)==IntBuildingType::SWARM_BUILDING)
		{
			manage_swarm(echo, *i);
		}
		if(echo.get_building_register().get_type(*i)==IntBuildingType::FOOD_BUILDING)
		{
			manage_inn(echo, *i);
		}
	}
}


void NewNicowar::manage_inn(Echo& echo, int id)
{
	int level=echo.get_building_register().get_level(id);
	int assigned=echo.get_building_register().get_assigned(id);

	//Do nothing if the ressource_tracker order hasn't been processed yet
	if(! echo.get_ressource_tracker(id))
		return;
	int total_ressource_level = echo.get_ressource_tracker(id)->get_total_level();
	
	int to_assign = 0;
	if(level==1 && total_ressource_level>(strategy.level_1_inn_low_wheat_trigger_ammount*AI_NICOWAR_RESSOURCE_TRACKER_DEPTH))
		to_assign=strategy.level_1_inn_units_assigned_normal_wheat;
	else if(level==1 && total_ressource_level<=(strategy.level_1_inn_low_wheat_trigger_ammount*AI_NICOWAR_RESSOURCE_TRACKER_DEPTH))
		to_assign=strategy.level_1_inn_units_assigned_low_wheat;

	if(level==2 && total_ressource_level>(strategy.level_2_inn_low_wheat_trigger_ammount*AI_NICOWAR_RESSOURCE_TRACKER_DEPTH))
		to_assign=strategy.level_2_inn_units_assigned_normal_wheat;
	else if(level==2 && total_ressource_level<=(strategy.level_2_inn_low_wheat_trigger_ammount*AI_NICOWAR_RESSOURCE_TRACKER_DEPTH))
		to_assign=strategy.level_2_inn_units_assigned_low_wheat;

	if(level==3 && total_ressource_level>(strategy.level_3_inn_low_wheat_trigger_ammount*AI_NICOWAR_RESSOURCE_TRACKER_DEPTH))
		to_assign=strategy.level_3_inn_units_assigned_normal_wheat;
	else if(level==3 && total_ressource_level<=(strategy.level_3_inn_low_wheat_trigger_ammount*AI_NICOWAR_RESSOURCE_TRACKER_DEPTH))
		to_assign=strategy.level_3_inn_units_assigned_low_wheat;
	
	///The number of units assigned to an Inn depends entirely on its level
	if(to_assign != assigned)
	{
		ManagementOrder* mo_assign=new AssignWorkers(to_assign, id);
		echo.add_management_order(mo_assign);
	}
}


void NewNicowar::manage_swarm(Echo& echo, int id)
{
	//Get some statistics
	TeamStat* stat=echo.player->team->stats.getLatestStat();
	int total_explorers=stat->numberUnitPerType[EXPLORER];
	if(stat->totalUnit == 0)
		return;
	int total_starving_percent = stat->needFoodCritical * 100 / stat->totalUnit;
	int total_hungry_percent = stat->needFood * 100 / stat->totalUnit;

	int assigned=echo.get_building_register().get_assigned(id);
	int to_assign=0;

	//Do nothing if the ressource_tracker order hasn't been processed yet
	if(! echo.get_ressource_tracker(id))
		return;
	int total_ressource_level = echo.get_ressource_tracker(id)->get_total_level();

	int worker_ratio=0;
	int explorer_ratio=0;
	int warrior_ratio=0;


	to_assign=strategy.base_swarm_units_assigned;

	///Double units when ressource level is low
	if(total_ressource_level <= (strategy.base_swarm_low_wheat_trigger_ammount * AI_NICOWAR_RESSOURCE_TRACKER_DEPTH))
		to_assign*=2;

	///Half units if world is hungry
	if((total_starving_percent + total_hungry_percent) > strategy.base_swarm_hungry_reduce_trigger_percent)
		to_assign/=2;
	
	///No units when the world is starving
	if(starving_recovery)
		to_assign=0;


	///The ratio of workers during the growth phase is different, due to the fact
	///that most explorers are made during the growth phase
	if(growth_phase)
	{
		worker_ratio=strategy.growth_phase_swarm_worker_ratio;

	}
	else
	{
		if(no_workers_phase)
			worker_ratio=0;
		else
			worker_ratio=strategy.non_growth_phase_swarm_worker_ratio;
	}

	//Base needed explorers never exceed 1/10 of population
	int needed_explorers=std::min(strategy.base_number_of_explorers, stat->totalUnit/AI_NICOWAR_EXPLORER_POP_DIVISOR+AI_NICOWAR_EXPLORER_MIN);
	if(fruit_phase)
		needed_explorers+=strategy.fruit_phase_extra_number_of_explorers;
	if(defend_explorers)
		needed_explorers+=(stat->totalUnit * strategy.defense_explorer_population_percent) / 100;
	if(explorer_attack_preperation_phase)
		needed_explorers+=strategy.offense_explorer_number;

	if(total_explorers<needed_explorers)
		explorer_ratio=strategy.base_swarm_explorer_ratio;
	else
		explorer_ratio=0;

	///Warriors are constructed during the war preperation phase
	if(war_preperation)
	{
		warrior_ratio=strategy.war_preperation_swarm_warrior_ratio;
	}
	else
	{
		warrior_ratio=0;
	}

	if(assigned != to_assign)
	{
		ManagementOrder* mo_assign=new AssignWorkers(to_assign, id);
		echo.add_management_order(mo_assign);
	}

	//Change the ratio of the swarm when its finished
	ManagementOrder* mo_ratios=new ChangeSwarm(worker_ratio, explorer_ratio, warrior_ratio, id);
	echo.add_management_order(mo_ratios);
}


