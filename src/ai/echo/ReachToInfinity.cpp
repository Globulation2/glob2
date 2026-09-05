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

	tick_initial_setup(echo);
	tick_explorer_flags_fruit(echo);
	tick_explorer_flags_enemies(echo);
	tick_inns_near_wheat(echo);
	tick_swarms_near_wheat(echo);
	tick_racetrack_near_stone_wood(echo);
	tick_swimmingpool_near_wheat_wood(echo);
	tick_school_inland(echo);
	tick_upgrade_l1_to_l2(echo);
	tick_upgrade_l2_to_l3(echo);
	tick_delete_old_inns_swarms(echo);
	tick_farming_areas(echo);
}


void ReachToInfinity::tick_initial_setup(Echo& echo)
{
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
