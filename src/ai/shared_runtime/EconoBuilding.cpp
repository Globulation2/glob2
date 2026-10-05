// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "AITelemetryFields.h"
#include "shared_runtime/Runtime.h"
#include <algorithm>
#include "shared_runtime/BuildingDemands.h"
#include "Game.h"
#include <iterator>
#include "Utilities.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Gradients;
using namespace AISharedRuntime::Construction;
using namespace AISharedRuntime::Management;
using namespace AISharedRuntime::Conditions;
using namespace AISharedRuntime::SearchTools;


//Standard Inns near wheat
void Econo::tick_inns_near_wheat(Runtime& runtime)
{
	// Feeding capacity cannot constrain production when units never need meals.
	if (runtime.player->game->gameHeader.isHungerDisabled()) return;
	telemetry.count(AITrace::AI4::Econo_tick_inns_near_wheat_calls);
	if((timer%AI_SHARED_RUNTIME_RTI_INN_INTERVAL_TICKS)==0 && (timer%AI_SHARED_RUNTIME_RTI_BIG_CYCLE_TICKS)!=0)
	{
		BuildingSearch bs_level1(runtime);
		bs_level1.add_condition(new ProvidesBuildingCapability(BuildingDemand::Feed));
		bs_level1.add_condition(new BuildingLevel(1));
		const int number1=bs_level1.count_buildings();

		BuildingSearch bs_level2(runtime);
		bs_level2.add_condition(new ProvidesBuildingCapability(BuildingDemand::Feed));
		bs_level2.add_condition(new BuildingLevel(2));
		const int number2=bs_level2.count_buildings();

		BuildingSearch bs_level3(runtime);
		bs_level3.add_condition(new ProvidesBuildingCapability(BuildingDemand::Feed));
		bs_level3.add_condition(new BuildingLevel(3));
		const int number3=bs_level3.count_buildings();

		if((runtime.player->team->stats.getLatestStat()->totalUnit)>=(number1*AI_SHARED_RUNTIME_RTI_INN_POP_PER_L1 + number2*AI_SHARED_RUNTIME_RTI_INN_POP_PER_L2 + number3*AI_SHARED_RUNTIME_RTI_INN_POP_PER_L3))
		{
			//The main order for the inn
			BuildingOrder* bo = new BuildingOrder(runtime, BuildingDemand::Feed, 2);

			//Constraints around the location of wheat
			AISharedRuntime::Gradients::GradientInfo gi_wheat;
			gi_wheat.add_source(new AISharedRuntime::Gradients::Entities::ResourceSet(bo->input_resource_mask(runtime)));
			//You want to be close to wheat
			bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_wheat, AI_SHARED_RUNTIME_RTI_INN_WHEAT_WEIGHT));
			//You can't be farther than 10 units from wheat
			bo->add_constraint(new AISharedRuntime::Construction::MaximumDistance(gi_wheat, AI_SHARED_RUNTIME_RTI_INN_WHEAT_MAX_DIST));

			//Constraints around nearby settlement
			AISharedRuntime::Gradients::GradientInfo gi_building;
            gi_building.terrainTravel=field::TerrainTravel::Swim;
			gi_building.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.player->team->teamNumber, false));
			gi_building.add_obstacle(new AISharedRuntime::Gradients::Entities::AnyResource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, AI_SHARED_RUNTIME_RTI_BUILD_CLUSTER_WEIGHT));

			AISharedRuntime::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AISharedRuntime::Gradients::Entities::AnyResource);
			//You don't want to be too close
			bo->add_constraint(new AISharedRuntime::Construction::MinimumDistance(gi_building_construction, AI_SHARED_RUNTIME_RTI_INN_CONSTRUCTION_MIN_DIST));

			if(runtime.is_fruit_on_map())
			{
				//Constraints around the location of fruit
				AISharedRuntime::Gradients::GradientInfo gi_fruit;
				gi_fruit.add_source(new AISharedRuntime::Gradients::Entities::Resource(CHERRY));
				gi_fruit.add_source(new AISharedRuntime::Gradients::Entities::Resource(ORANGE));
				gi_fruit.add_source(new AISharedRuntime::Gradients::Entities::Resource(PRUNE));
				//You want to be reasonably close to fruit, closer if possible
				bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_fruit, AI_SHARED_RUNTIME_RTI_INN_FRUIT_WEIGHT));
			}

			//Add the building order to the list of orders
			unsigned int id=runtime.add_building_order(bo);

//			std::cout<<"inn ordered, id="<<id<<std::endl;

			ManagementOrder* mo_completion=new AssignWorkers(1, id);
			mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			runtime.add_management_order(mo_completion);

			ManagementOrder* mo_tracker=new AddResourceTracker(AI_SHARED_RUNTIME_RTI_TRACKER_LENGTH, RecurringInputStock, id);
			mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			runtime.add_management_order(mo_tracker);
		}
	}
}

//Standard swarms near wheat. Uses special mechanism, builds more swarms early on.
void Econo::tick_swarms_near_wheat(Runtime& runtime)
{
	telemetry.count(AITrace::AI4::Econo_tick_swarms_near_wheat_calls);
	if((timer%AI_SHARED_RUNTIME_RTI_BIG_CYCLE_TICKS)==AI_SHARED_RUNTIME_RTI_SWARM_OFFSET_TICKS)
	{
		BuildingSearch bs(runtime);
		bs.add_condition(new ProvidesBuildingCapability(BuildingDemand::ProduceWorker));
		const int number=bs.count_buildings();
		if((number<=AI_SHARED_RUNTIME_RTI_SWARM_EARLY_LIMIT && (runtime.player->team->stats.getLatestStat()->totalUnit/AI_SHARED_RUNTIME_RTI_SWARM_EARLY_RATIO)>=number) ||
		   (runtime.player->team->stats.getLatestStat()->totalUnit/AI_SHARED_RUNTIME_RTI_SWARM_LATE_RATIO)>=number)
		{
//			std::cout<<"Constructing swarm"<<std::endl;
			//The main order for the swarm
			BuildingOrder* bo = new BuildingOrder(runtime, BuildingDemand::ProduceWorker, AI_SHARED_RUNTIME_RTI_SWARM_WORKERS_NEW);

			//Constraints around the location of wheat
			AISharedRuntime::Gradients::GradientInfo gi_wheat;
			gi_wheat.add_source(new AISharedRuntime::Gradients::Entities::ResourceSet(bo->input_resource_mask(runtime)));
			//You want to be close to wheat
			bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_wheat, AI_SHARED_RUNTIME_RTI_INN_WHEAT_WEIGHT));

			//Constraints around nearby settlement
			AISharedRuntime::Gradients::GradientInfo gi_building;
            gi_building.terrainTravel=field::TerrainTravel::Swim;
			gi_building.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.player->team->teamNumber, false));
			gi_building.add_obstacle(new AISharedRuntime::Gradients::Entities::AnyResource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, AI_SHARED_RUNTIME_RTI_SWARM_CLUSTER_WEIGHT));

			AISharedRuntime::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AISharedRuntime::Gradients::Entities::AnyResource);
			//You don't want to be too close
			bo->add_constraint(new AISharedRuntime::Construction::MinimumDistance(gi_building_construction, AI_SHARED_RUNTIME_RTI_INN_CONSTRUCTION_MIN_DIST));

			//Add the building order to the list of orders
			unsigned int id=runtime.add_building_order(bo);

//			std::cout<<"Swarm ordered, id="<<id<<std::endl;

			//Change the number of workers assigned when the building is finished
			ManagementOrder* mo_completion=new AssignWorkers(AI_SHARED_RUNTIME_RTI_SWARM_WORKERS_FINISHED, id);
			mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			runtime.add_management_order(mo_completion);

			//Change the ratio of the swarm when its finished
			ManagementOrder* mo_ratios=new ChangeSwarm(AI_SHARED_RUNTIME_RTI_SWARM_RATIO_WORKER, AI_SHARED_RUNTIME_RTI_SWARM_RATIO_EXPLORER, AI_SHARED_RUNTIME_RTI_SWARM_RATIO_WARRIOR, id);
			mo_ratios->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			runtime.add_management_order(mo_ratios);

			//Add a tracker
			ManagementOrder* mo_tracker=new AddResourceTracker(AI_SHARED_RUNTIME_RTI_TRACKER_LENGTH, RecurringInputStock, id);
			mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			runtime.add_management_order(mo_tracker);

		}
	}
}

//Standard racetrack near stone and wood
void Econo::tick_racetrack_near_stone_wood(Runtime& runtime)
{
	// Training cannot increase levels here; do not fund or wait for an impossible upgrade.
	if (runtime.player->game->gameHeader.isUnitUpgradesDisabled()) return;
	telemetry.count(AITrace::AI4::Econo_tick_racetrack_near_stone_wood_calls);
	if((timer%AI_SHARED_RUNTIME_RTI_BIG_CYCLE_TICKS)==AI_SHARED_RUNTIME_RTI_RACETRACK_OFFSET_TICKS)
	{
		BuildingSearch bs(runtime);
		bs.add_condition(new ProvidesBuildingCapability(BuildingDemand::TrainWalk));
		const int number=bs.count_buildings();
		if((runtime.player->team->stats.getLatestStat()->totalUnit/AI_SHARED_RUNTIME_RTI_SECONDARY_BLDG_RATIO)>=number && number<AI_SHARED_RUNTIME_RTI_RACETRACK_MAX)
		{
			//The main order for the racetrack
			BuildingOrder* bo = new BuildingOrder(runtime, BuildingDemand::TrainWalk, AI_SHARED_RUNTIME_RTI_RACETRACK_WORKERS);

			//Constraints around the location of wood
			AISharedRuntime::Gradients::GradientInfo gi_wood;
			gi_wood.add_source(new AISharedRuntime::Gradients::Entities::ResourceSet(bo->input_resource_mask(runtime)));
			//You want to be close to wood
			bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_wood, AI_SHARED_RUNTIME_RTI_RACETRACK_WOOD_WEIGHT));

			//Constraints around the location of stone
			AISharedRuntime::Gradients::GradientInfo gi_stone;
			gi_stone.add_source(new AISharedRuntime::Gradients::Entities::ResourceSet(bo->input_resource_mask(runtime)));
			//You want to be close to stone
			bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_stone, AI_SHARED_RUNTIME_RTI_RACETRACK_STONE_WEIGHT));
			//But not to close, so you have room to upgrade
			bo->add_constraint(new AISharedRuntime::Construction::MinimumDistance(gi_stone, AI_SHARED_RUNTIME_RTI_RACETRACK_STONE_MIN_DIST));

			//Constraints around nearby settlement
			AISharedRuntime::Gradients::GradientInfo gi_building;
            gi_building.terrainTravel=field::TerrainTravel::Swim;
			gi_building.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.player->team->teamNumber, false));
			gi_building.add_obstacle(new AISharedRuntime::Gradients::Entities::AnyResource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, AI_SHARED_RUNTIME_RTI_BUILD_CLUSTER_WEIGHT));

			AISharedRuntime::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AISharedRuntime::Gradients::Entities::AnyResource);
			//You don't want to be too close
			bo->add_constraint(new AISharedRuntime::Construction::MinimumDistance(gi_building_construction, AI_SHARED_RUNTIME_RTI_RACETRACK_CONSTR_MIN_DIST));

			//Add the building order to the list of orders
			runtime.add_building_order(bo);
		}
	}
}

//Standard swimming pool near wheat and wood
void Econo::tick_swimmingpool_near_wheat_wood(Runtime& runtime)
{
	// Training cannot increase levels here; do not fund or wait for an impossible upgrade.
	if (runtime.player->game->gameHeader.isUnitUpgradesDisabled()) return;
	telemetry.count(AITrace::AI4::Econo_tick_swimmingpool_near_wheat_wood_calls);
	if((timer%AI_SHARED_RUNTIME_RTI_BIG_CYCLE_TICKS)==AI_SHARED_RUNTIME_RTI_SWIMMINGPOOL_OFFSET_TICKS)
	{
		BuildingSearch bs(runtime);
		bs.add_condition(new ProvidesBuildingCapability(BuildingDemand::TrainSwim));
		const int number=bs.count_buildings();
		if((runtime.player->team->stats.getLatestStat()->totalUnit/AI_SHARED_RUNTIME_RTI_SECONDARY_BLDG_RATIO)>=number && number<AI_SHARED_RUNTIME_RTI_SWIMMINGPOOL_MAX)
		{
			//The main order for the swimming pool
			BuildingOrder* bo = new BuildingOrder(runtime, BuildingDemand::TrainSwim, AI_SHARED_RUNTIME_RTI_SWIMMINGPOOL_WORKERS);

			//Constraints around the location of wood
			AISharedRuntime::Gradients::GradientInfo gi_wood;
			gi_wood.add_source(new AISharedRuntime::Gradients::Entities::ResourceSet(bo->input_resource_mask(runtime)));
			//You want to be close to wood
			bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_wood, AI_SHARED_RUNTIME_RTI_SWIMMINGPOOL_WOOD_WEIGHT));

			//Constraints around the location of wheat
			AISharedRuntime::Gradients::GradientInfo gi_wheat;
			gi_wheat.add_source(new AISharedRuntime::Gradients::Entities::ResourceSet(bo->input_resource_mask(runtime)));
			//You want to be close to wheat
			bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_wheat, AI_SHARED_RUNTIME_RTI_SWIMMINGPOOL_WHEAT_WEIGHT));

			//Constraints around the location of stone
			AISharedRuntime::Gradients::GradientInfo gi_stone;
			gi_stone.add_source(new AISharedRuntime::Gradients::Entities::ResourceSet(bo->input_resource_mask(runtime)));
			//You don't want to be too close, so you have room to upgrade
			bo->add_constraint(new AISharedRuntime::Construction::MinimumDistance(gi_stone, AI_SHARED_RUNTIME_RTI_SWIMMINGPOOL_STONE_MIN_DIST));

			//Constraints around nearby settlement
			AISharedRuntime::Gradients::GradientInfo gi_building;
            gi_building.terrainTravel=field::TerrainTravel::Swim;
			gi_building.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.player->team->teamNumber, false));
			gi_building.add_obstacle(new AISharedRuntime::Gradients::Entities::AnyResource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, AI_SHARED_RUNTIME_RTI_BUILD_CLUSTER_WEIGHT));

			AISharedRuntime::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AISharedRuntime::Gradients::Entities::AnyResource);
			//You don't want to be too close
			bo->add_constraint(new AISharedRuntime::Construction::MinimumDistance(gi_building_construction, AI_SHARED_RUNTIME_RTI_SWIMMINGPOOL_CONSTR_MIN_DIST));

			//Add the building order to the list of orders
			runtime.add_building_order(bo);
		}
	}
}


//Standard school inland away from the enemies
void Econo::tick_school_inland(Runtime& runtime)
{
	// Training cannot increase levels here; do not fund or wait for an impossible upgrade.
	if (runtime.player->game->gameHeader.isUnitUpgradesDisabled()) return;
	telemetry.count(AITrace::AI4::Econo_tick_school_inland_calls);
	if((timer%AI_SHARED_RUNTIME_RTI_BIG_CYCLE_TICKS)==AI_SHARED_RUNTIME_RTI_SCHOOL_OFFSET_TICKS)
	{
		BuildingSearch bs(runtime);
		bs.add_condition(new ProvidesBuildingCapability(BuildingDemand::TrainConstruction));
		const int number=bs.count_buildings();
		if((runtime.player->team->stats.getLatestStat()->totalUnit/AI_SHARED_RUNTIME_RTI_SECONDARY_BLDG_RATIO)>=number && number<AI_SHARED_RUNTIME_RTI_SCHOOL_MAX)
		{
			//The main order for the school
			BuildingOrder* bo = new BuildingOrder(runtime, BuildingDemand::TrainConstruction, AI_SHARED_RUNTIME_RTI_SCHOOL_WORKERS);

			//Constraints around nearby settlement
			AISharedRuntime::Gradients::GradientInfo gi_building;
            gi_building.terrainTravel=field::TerrainTravel::Swim;
			gi_building.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.player->team->teamNumber, false));
			gi_building.add_obstacle(new AISharedRuntime::Gradients::Entities::AnyResource);
			//You want to be close to other buildings, but wheat is more important
			bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, AI_SHARED_RUNTIME_RTI_BUILD_CLUSTER_WEIGHT));

			AISharedRuntime::Gradients::GradientInfo gi_building_construction;
			gi_building_construction.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.player->team->teamNumber, true));
			gi_building_construction.add_obstacle(new AISharedRuntime::Gradients::Entities::AnyResource);
			//You don't want to be too close
			bo->add_constraint(new AISharedRuntime::Construction::MinimumDistance(gi_building_construction, AI_SHARED_RUNTIME_RTI_SCHOOL_CONSTR_MIN_DIST));

			//Constraints around the enemy
			AISharedRuntime::Gradients::GradientInfo gi_enemy;
			for(enemy_team_iterator i(runtime); i!=enemy_team_iterator(); ++i)
			{
				gi_enemy.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(*i, false));
			}
			gi_enemy.add_obstacle(new AISharedRuntime::Gradients::Entities::AnyResource);
			bo->add_constraint(new AISharedRuntime::Construction::MaximizedDistance(gi_enemy, AI_SHARED_RUNTIME_RTI_SCHOOL_ENEMY_DIST_WEIGHT));

			//Add the building order to the list of orders
			runtime.add_building_order(bo);
		}
	}
}


//Level 1 to level 2 upgrades
void Econo::tick_upgrade_l1_to_l2(Runtime& runtime)
{
	// Higher-tier buildings cannot be created under this rule. Skip the entire
	// upgrade search, including its trained-worker and construction waits.
	if (runtime.player->game->gameHeader.isUnitUpgradesDisabled()) return;
	telemetry.count(AITrace::AI4::Econo_tick_upgrade_l1_to_l2_calls);
	if((timer%AI_SHARED_RUNTIME_RTI_UPGRADE_INTERVAL_TICKS)==0)
	{
		BuildingSearch level_twos(runtime);
		level_twos.add_condition(new BeingUpgradedTo(AI_SHARED_RUNTIME_RTI_UPGRADE_TARGET_LEVEL_2));
		const int level_two_counts=level_twos.count_buildings();

		BuildingSearch schools(runtime);
		schools.add_condition(new ProvidesBuildingCapability(BuildingDemand::TrainConstruction));
		schools.add_condition(new NotUnderConstruction);
		const int school_counts=schools.count_buildings();

		BuildingSearch buildings(runtime);
		buildings.add_condition(new BuildingLevel(1));
		const int total_buildings=buildings.count_buildings();
		if(level_two_counts<=(total_buildings/AI_SHARED_RUNTIME_RTI_CONCURRENT_UPGRADE_FRACTION) && school_counts>0)
		{
			BuildingSearch bs(runtime);
			bs.add_condition(new Upgradable);
			bs.add_condition(new BuildingLevel(1));
			if(school_counts<AI_SHARED_RUNTIME_RTI_SCHOOL_THRESHOLD_FOR_UPGRADE)
				bs.add_condition(new LacksBuildingCapability(BuildingDemand::TrainConstruction));
			std::vector<int> buildings;
			std::copy(bs.begin(), bs.end(), std::back_insert_iterator<std::vector<int> >(buildings));

			if(buildings.size()!=0)
			{
				int chosen=runtime.random()%buildings.size();
				ManagementOrder* uro = new UpgradeRepair(buildings[chosen]);
				runtime.add_management_order(uro);

				int assigned=runtime.get_building_register().get_assigned(buildings[chosen]);

				ManagementOrder* mo_assign=new AssignWorkers(AI_SHARED_RUNTIME_RTI_UPGRADE_WORKERS_DURING, buildings[chosen]);
				mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, buildings[chosen]));
				runtime.add_management_order(mo_assign);

				if(runtime.get_building_register().provides(buildings[chosen],BuildingDemand::Feed))
				{
					ManagementOrder* mo_tracker_pause=new PauseResourceTracker(buildings[chosen]);
					mo_tracker_pause->add_condition(new ParticularBuilding(new UnderConstruction, buildings[chosen]));
					runtime.add_management_order(mo_tracker_pause);

					ManagementOrder* mo_tracker_unpause=new UnPauseResourceTracker(buildings[chosen]);
					mo_tracker_unpause->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					runtime.add_management_order(mo_tracker_unpause);

					ManagementOrder* mo_completion=new AssignWorkers(AI_SHARED_RUNTIME_RTI_INN_L2_WORKERS_FINISHED, buildings[chosen]);
					mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					runtime.add_management_order(mo_completion);
				}
				else
				{
					ManagementOrder* mo_assign=new AssignWorkers(assigned, buildings[chosen]);
					mo_assign->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					runtime.add_management_order(mo_assign);
				}
			}
		}
	}
}

//Level 2 to level 3 upgrades
void Econo::tick_upgrade_l2_to_l3(Runtime& runtime)
{
	// Keep existing high-tier buildings useful, but do not schedule new tier
	// transitions or reserve workers for an upgrade that cannot start.
	if (runtime.player->game->gameHeader.isUnitUpgradesDisabled()) return;
	telemetry.count(AITrace::AI4::Econo_tick_upgrade_l2_to_l3_calls);
	if((timer%AI_SHARED_RUNTIME_RTI_UPGRADE_INTERVAL_TICKS)==0)
	{
		BuildingSearch level_threes(runtime);
		level_threes.add_condition(new BeingUpgradedTo(AI_SHARED_RUNTIME_RTI_UPGRADE_TARGET_LEVEL_3));
		const int level_three_counts=level_threes.count_buildings();

		BuildingSearch schools(runtime);
		schools.add_condition(new ProvidesBuildingCapability(BuildingDemand::TrainConstruction));
		schools.add_condition(new NotUnderConstruction);
		schools.add_condition(new BuildingLevel(2));
		int school_counts=schools.count_buildings();

		BuildingSearch schools2(runtime);
		schools2.add_condition(new ProvidesBuildingCapability(BuildingDemand::TrainConstruction));
		schools2.add_condition(new NotUnderConstruction);
		schools2.add_condition(new BuildingLevel(3));
		school_counts+=schools2.count_buildings();

		BuildingSearch buildings(runtime);
		buildings.add_condition(new BuildingLevel(2));
		const int total_buildings=buildings.count_buildings();
		if(level_three_counts<=(total_buildings/AI_SHARED_RUNTIME_RTI_CONCURRENT_UPGRADE_FRACTION) && school_counts>0)
		{
			BuildingSearch bs(runtime);
			bs.add_condition(new Upgradable);
			bs.add_condition(new BuildingLevel(2));
			if(school_counts<AI_SHARED_RUNTIME_RTI_SCHOOL_THRESHOLD_FOR_UPGRADE)
				bs.add_condition(new LacksBuildingCapability(BuildingDemand::TrainConstruction));
			std::vector<int> buildings;
			std::copy(bs.begin(), bs.end(), std::back_insert_iterator<std::vector<int> >(buildings));

			if(buildings.size()!=0)
			{
				int chosen=runtime.random()%buildings.size();
				ManagementOrder* uro = new UpgradeRepair(buildings[chosen]);
				runtime.add_management_order(uro);

				int assigned=runtime.get_building_register().get_assigned(buildings[chosen]);

				ManagementOrder* mo_assign=new AssignWorkers(AI_SHARED_RUNTIME_RTI_UPGRADE_WORKERS_DURING, buildings[chosen]);
				mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, buildings[chosen]));
				runtime.add_management_order(mo_assign);

				if(runtime.get_building_register().provides(buildings[chosen],BuildingDemand::Feed))
				{
					ManagementOrder* mo_tracker_pause=new PauseResourceTracker(buildings[chosen]);
					mo_tracker_pause->add_condition(new ParticularBuilding(new UnderConstruction, buildings[chosen]));
					runtime.add_management_order(mo_tracker_pause);

					ManagementOrder* mo_tracker_unpause=new UnPauseResourceTracker(buildings[chosen]);
					mo_tracker_unpause->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					runtime.add_management_order(mo_tracker_unpause);

					ManagementOrder* mo_completion=new AssignWorkers(AI_SHARED_RUNTIME_RTI_INN_L3_WORKERS_FINISHED, buildings[chosen]);
					mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					runtime.add_management_order(mo_completion);
				}
				else
				{
					ManagementOrder* mo_assign=new AssignWorkers(assigned, buildings[chosen]);
					mo_assign->add_condition(new ParticularBuilding(new NotUnderConstruction, buildings[chosen]));
					runtime.add_management_order(mo_assign);
				}
			}
		}
	}
}



//Delete old inns that are hard to keep full of wheat. Existing swarms are retained.
void Econo::tick_delete_old_inns(Runtime& runtime)
{
	// Preserve the historical telemetry column for save/load continuity.
	telemetry.count(AITrace::AI4::Econo_tick_delete_old_inns_swarms_calls);
	if((timer%AI_SHARED_RUNTIME_RTI_DELETE_SCAN_INTERVAL_TICKS)==0)
	{
		BuildingSearch inns(runtime);
		inns.add_condition(new ProvidesBuildingCapability(BuildingDemand::Feed));
		inns.add_condition(new NotUnderConstruction);
		for(building_search_iterator i=inns.begin(); i!=inns.end(); ++i)
		{
			std::shared_ptr<ResourceTracker> rt=runtime.get_resource_tracker(*i);
			if(rt)
			{
				if(rt->get_age()>AI_SHARED_RUNTIME_RTI_INN_DELETE_AGE_TICKS)
				{
					if(rt->get_total_level() < AI_SHARED_RUNTIME_RTI_INN_DELETE_FOOD_PER_LEVEL*runtime.get_building_register().get_level(*i))
					{
						ManagementOrder* mo_destroy=new DestroyBuilding(*i);
						runtime.add_management_order(mo_destroy);
					}
				}
			}
		}
	}
}
