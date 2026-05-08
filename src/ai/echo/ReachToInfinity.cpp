// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Building.h"
#include <stack>
#include <queue>
#include <map>
#include <limits>
#include <algorithm>
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include <iterator>
#include "Utilities.h"
#include <tuple>
#include "Brush.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace boost::logic;
using std::shared_ptr;



ReachToInfinity::ReachToInfinity()
{
	timer=0;
	flag_on_cherry=false;
	flag_on_orange=false;
	flag_on_prune=false;
}


bool ReachToInfinity::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ReachToInfinity");
	timer=stream->readUint32("timer");
	flag_on_cherry=stream->readUint32("flag_on_cherry");
	flag_on_orange=stream->readUint32("flag_on_orange");
	flag_on_prune=stream->readUint32("flag_on_prune");

	stream->readEnterSection("flags_on_enemy");
	Uint32 flagsOnEnemySize=stream->readUint32("size");
	for(Uint32 flagsOnEnemyIndex=0; flagsOnEnemyIndex<flagsOnEnemySize; ++flagsOnEnemyIndex)
	{
		stream->readEnterSection(flagsOnEnemyIndex);
		flags_on_enemy.insert(stream->readUint32("gid"));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readLeaveSection();
	return true;
}


void ReachToInfinity::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ReachToInfinity");
	stream->writeUint32(timer, "timer");
	stream->writeUint32(flag_on_cherry, "flag_on_cherry");
	stream->writeUint32(flag_on_orange, "flag_on_orange");
	stream->writeUint32(flag_on_prune, "flag_on_prune");

	stream->writeEnterSection("flags_on_enemy");
	Uint32 flagsOnEnemyIndex=0;
	stream->writeUint32(flags_on_enemy.size(), "size");
	for(std::set<int>::iterator i=flags_on_enemy.begin(); i!=flags_on_enemy.end(); ++i, ++flagsOnEnemyIndex)
	{
		stream->writeEnterSection(flagsOnEnemyIndex);
		stream->writeUint32(*i, "gid");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeLeaveSection();
}


void ReachToInfinity::tick(Echo& echo)
{
	timer++;
	if(timer==1)
	{
		BuildingSearch bs(echo);
		for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
		{	
			if(echo.get_building_register().get_type(*i)==IntBuildingType::SWARM_BUILDING)
			{
				ManagementOrder* mo_completion=new AssignWorkers(AI_ECHO_RTI_INITIAL_SWARM_WORKERS, *i);
				echo.add_management_order(mo_completion);

				ManagementOrder* mo_ratios=new ChangeSwarm(AI_ECHO_RTI_SWARM_RATIO_WORKER, AI_ECHO_RTI_SWARM_RATIO_EXPLORER, AI_ECHO_RTI_SWARM_RATIO_WARRIOR, *i);
				mo_ratios->add_condition(new ParticularBuilding(new NotUnderConstruction, *i));
				echo.add_management_order(mo_ratios);

				ManagementOrder* mo_tracker=new AddRessourceTracker(AI_ECHO_RTI_TRACKER_LENGTH, CORN, *i);
				echo.add_management_order(mo_tracker);
			}
			if(echo.get_building_register().get_type(*i)==IntBuildingType::FOOD_BUILDING)
			{
				ManagementOrder* mo_tracker=new AddRessourceTracker(AI_ECHO_RTI_TRACKER_LENGTH, CORN, *i);
				echo.add_management_order(mo_tracker);
			}
		}
	}

/*
	///This is demonstration code for the advanced use of Conditions
	if(timer==100)
	{
		for(int g=0; g<1; ++g)
		{
			int prev_id=-1;
			int first_id=-1;
			int fifth_id=-1;
			for(int n=0; n<15; ++n)
			{
				//The main order for the inn
				BuildingOrder* bo = new BuildingOrder(IntBuildingType::FOOD_BUILDING, 2);
	
				//Constraints arround the location of wheat
				AIEcho::Gradients::GradientInfo gi_wheat;
				gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
				//You want to be close to wheat
				bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, 4));
				//You can't be farther than 10 units from wheat
				bo->add_constraint(new AIEcho::Construction::MaximumDistance(gi_wheat, 10));
	
				//Constraints arround nearby settlement
				AIEcho::Gradients::GradientInfo gi_building;
				gi_building.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, false));
				gi_building.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
				//You want to be close to other buildings, but wheat is more important
				bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_building, 2));

				AIEcho::Gradients::GradientInfo gi_building_construction;
				gi_building_construction.add_source(new AIEcho::Gradients::Entities::AnyTeamBuilding(echo.player->team->teamNumber, true));
				gi_building_construction.add_obstacle(new AIEcho::Gradients::Entities::AnyRessource);
				//You don't want to be too close
				bo->add_constraint(new AIEcho::Construction::MinimumDistance(gi_building_construction, 3));

				//Constraints arround the location of fruit
				AIEcho::Gradients::GradientInfo gi_fruit;
				gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(CHERRY));
				gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(ORANGE));
				gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(PRUNE));
				//You want to be reasnobly close to fruit, closer if possible
				bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_fruit, 1));
	
				if(prev_id!=-1)
				{
					bo->add_condition(new EitherCondition(new ParticularBuilding(new NotUnderConstruction, prev_id), new BuildingDestroyed(prev_id)));
				}
				else
					bo->add_condition(new Population(true, true, true, 5, Population::Greater));
	
				//Add the building order to the list of orders
				unsigned int id=echo.add_building_order(bo);

				if(prev_id!=-1)
				{
					ManagementOrder* mo_upgrade = new UpgradeRepair(id);
					mo_upgrade->add_condition(new ParticularBuilding(new NotUnderConstruction, prev_id));
					mo_upgrade->add_condition(new ParticularBuilding(new BuildingLevel(2), prev_id));
					echo.add_management_order(mo_upgrade);

					ManagementOrder* mo_assign=new AssignWorkers(6, id);
					mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, id));
					mo_assign->add_condition(new ParticularBuilding(new BuildingLevel(2), id));
					echo.add_management_order(mo_assign);

					ManagementOrder* mo_finish=new AssignWorkers(2, id);
					mo_finish->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
					mo_finish->add_condition(new ParticularBuilding(new BuildingLevel(2), id));
					echo.add_management_order(mo_finish);
				}
				if(n==0)
				{
					first_id=id;
				}
				if(n==4)
				{
					fifth_id=id;
				}
				
				ManagementOrder* mo_completion=new AssignWorkers(1, id);
				mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
				echo.add_management_order(mo_completion);
	
				ManagementOrder* mo_tracker=new AddRessourceTracker(12, id, CORN);
				mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
				echo.add_management_order(mo_tracker);

				ManagementOrder* mo_delete=new DestroyBuilding(id);
				mo_delete->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
				mo_delete->add_condition(new ParticularBuilding(new RessourceTrackerAge(500, RessourceTrackerAge::Greater), id));
				mo_delete->add_condition(new ParticularBuilding(new RessourceTrackerAmount(48, RessourceTrackerAmount::Lesser), id));
				echo.add_management_order(mo_delete);

				ManagementOrder* mo_reconstruct = new SendMessage("construct inn");
				mo_reconstruct->add_condition(new BuildingDestroyed(id));
				echo.add_management_order(mo_reconstruct);
	
				prev_id=id;
			}

			ManagementOrder* mo_upgrade = new UpgradeRepair(first_id);
			mo_upgrade->add_condition(new ParticularBuilding(new NotUnderConstruction, fifth_id));
			echo.add_management_order(mo_upgrade);

			ManagementOrder* mo_assign=new AssignWorkers(6, first_id);
			mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, first_id));
			mo_assign->add_condition(new ParticularBuilding(new BuildingLevel(2), first_id));
			echo.add_management_order(mo_assign);

			ManagementOrder* mo_finish=new AssignWorkers(2, first_id);
			mo_finish->add_condition(new ParticularBuilding(new NotUnderConstruction, first_id));
			mo_finish->add_condition(new ParticularBuilding(new BuildingLevel(2), first_id));
			echo.add_management_order(mo_finish);
		}
	}

*/



	//Explorer flags on the three nearest fruit trees
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

	//Place exploration flags on the enemy swarms
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



	//Standard Inns near wheat
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

	//Standard swarms near wheat. Uses special mechanism, builds more swarms early on.
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

	//Standard racetrack near stone and wood
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

	//Standard swimming pool near wheat and wood
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


	//Standard school inland away from the enemies
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


	//Level 1 to level 2 upgrades
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

	//Level 2 to level 3 upgrades
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



	//Delete old inns and swarms that are hard to keep full of wheat
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

	//Farming wheat and wood near water
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


void ReachToInfinity::handle_message(Echo& echo, const std::string& message)
{
	if(message=="construct inn")
	{
		//The main order for the inn
		BuildingOrder* bo = new BuildingOrder(IntBuildingType::FOOD_BUILDING, 2);
	
		//Constraints around the location of wheat
		AIEcho::Gradients::GradientInfo gi_wheat;
		gi_wheat.add_source(new AIEcho::Gradients::Entities::Ressource(CORN));
		//You want to be close to wheat
		bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_wheat, AI_ECHO_RTI_INN_WHEAT_WEIGHT));
		//You can't be farther than 10 units from wheat
		bo->add_constraint(new AIEcho::Construction::MaximumDistance(gi_wheat, AI_ECHO_RTI_INN_WHEAT_MAX_DIST));

		//Constraints around nearby settlement
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

		//Constraints around the location of fruit
		if(echo.is_fruit_on_map())
		{
			AIEcho::Gradients::GradientInfo gi_fruit;
			gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(CHERRY));
			gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(ORANGE));
			gi_fruit.add_source(new AIEcho::Gradients::Entities::Ressource(PRUNE));
			//You want to be reasnobly close to fruit, closer if possible
			bo->add_constraint(new AIEcho::Construction::MinimizedDistance(gi_fruit, AI_ECHO_RTI_INN_FRUIT_WEIGHT));
		}

		//Add the building order to the list of orders
		unsigned int id=echo.add_building_order(bo);

//				std::cout<<"inn ordered, id="<<id<<std::endl;

		ManagementOrder* mo_completion=new AssignWorkers(1, id);
		mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
		echo.add_management_order(mo_completion);

		ManagementOrder* mo_tracker=new AddRessourceTracker(AI_ECHO_RTI_TRACKER_LENGTH, CORN, id);
		mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
		echo.add_management_order(mo_tracker);
	}
}

