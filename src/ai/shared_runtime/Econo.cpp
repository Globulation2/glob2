// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "AITelemetryFields.h"
#include "shared_runtime/Runtime.h"
#include "shared_runtime/BuildingDemands.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Gradients;
using namespace AISharedRuntime::Construction;
using namespace AISharedRuntime::Management;
using namespace AISharedRuntime::Conditions;
using namespace AISharedRuntime::SearchTools;



Econo::Econo()
{
	timer=0;
	flag_on_cherry=false;
	flag_on_orange=false;
	flag_on_prune=false;
}


bool Econo::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	flags_on_enemy.clear();
	// Binary saves ignore section names; the numeric AI ID remains unchanged.
	stream->readEnterSection("Econo");
	timer=stream->readUint32("timer");
	flag_on_cherry=stream->readUint32("flag_on_cherry");
	flag_on_orange=stream->readUint32("flag_on_orange");
	flag_on_prune=stream->readUint32("flag_on_prune");

	stream->readEnterSection("flags_on_enemy");
	Uint32 flagsOnEnemySize=stream->readCount("size");
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


void Econo::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Econo");
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


void Econo::tick(Runtime& runtime)
{
	timer++;

	tick_initial_setup(runtime);
	tick_explorer_flags_fruit(runtime);
	tick_explorer_flags_enemies(runtime);
	tick_inns_near_wheat(runtime);
	tick_swarms_near_wheat(runtime);
	tick_racetrack_near_stone_wood(runtime);
	tick_swimmingpool_near_wheat_wood(runtime);
	tick_school_inland(runtime);
	tick_upgrade_l1_to_l2(runtime);
	tick_upgrade_l2_to_l3(runtime);
	tick_delete_old_inns(runtime);
	tick_farming_areas(runtime);
}


void Econo::tick_initial_setup(Runtime& runtime)
{
	telemetry.count(AITrace::AI4::Econo_tick_initial_setup_calls);
	if(timer==1)
	{
		BuildingSearch bs(runtime);
		for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
		{
			if((runtime.get_building_register().provides(*i,BuildingDemand::ProduceWorker) || runtime.get_building_register().provides(*i,static_cast<int>(AIPlanning::BuildingIntent::ProduceExplorer)) || runtime.get_building_register().provides(*i,static_cast<int>(AIPlanning::BuildingIntent::ProduceWarrior))))
			{
				ManagementOrder* mo_completion=new AssignWorkers(AI_SHARED_RUNTIME_RTI_INITIAL_SWARM_WORKERS, *i);
				runtime.add_management_order(mo_completion);

				ManagementOrder* mo_ratios=new ChangeSwarm(AI_SHARED_RUNTIME_RTI_SWARM_RATIO_WORKER, AI_SHARED_RUNTIME_RTI_SWARM_RATIO_EXPLORER, AI_SHARED_RUNTIME_RTI_SWARM_RATIO_WARRIOR, *i);
				mo_ratios->add_condition(new ParticularBuilding(new NotUnderConstruction, *i));
				runtime.add_management_order(mo_ratios);

				ManagementOrder* mo_tracker=new AddResourceTracker(AI_SHARED_RUNTIME_RTI_TRACKER_LENGTH, RecurringInputStock, *i);
				runtime.add_management_order(mo_tracker);
			}
			if(runtime.get_building_register().provides(*i,BuildingDemand::Feed))
			{
				ManagementOrder* mo_tracker=new AddResourceTracker(AI_SHARED_RUNTIME_RTI_TRACKER_LENGTH, RecurringInputStock, *i);
				runtime.add_management_order(mo_tracker);
			}
		}
	}
}


void Econo::handle_message(Runtime& runtime, const std::string& message)
{
	if(message=="construct inn")
	{
		//The main order for the inn
		BuildingOrder* bo = new BuildingOrder(runtime, BuildingDemand::Feed, 2);

		//Constraints around the location of wheat
		AISharedRuntime::Gradients::GradientInfo gi_wheat;
		gi_wheat.add_source(new AISharedRuntime::Gradients::Entities::Resource(WHEAT));
		//You want to be close to wheat
		bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_wheat, AI_SHARED_RUNTIME_RTI_INN_WHEAT_WEIGHT));
		//You can't be farther than 10 units from wheat
		bo->add_constraint(new AISharedRuntime::Construction::MaximumDistance(gi_wheat, AI_SHARED_RUNTIME_RTI_INN_WHEAT_MAX_DIST));

		//Constraints around nearby settlement
		AISharedRuntime::Gradients::GradientInfo gi_building;
            gi_building.terrainTravel=field::TerrainTravel::Swim;
		gi_building.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.teamNumber(), false));
		gi_building.add_obstacle(new AISharedRuntime::Gradients::Entities::AnyResource);
		//You want to be close to other buildings, but wheat is more important
		bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, AI_SHARED_RUNTIME_RTI_BUILD_CLUSTER_WEIGHT));

		AISharedRuntime::Gradients::GradientInfo gi_building_construction;
		gi_building_construction.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.teamNumber(), true));
		gi_building_construction.add_obstacle(new AISharedRuntime::Gradients::Entities::AnyResource);
		//You don't want to be too close
		bo->add_constraint(new AISharedRuntime::Construction::MinimumDistance(gi_building_construction, AI_SHARED_RUNTIME_RTI_INN_CONSTRUCTION_MIN_DIST));

		//Constraints around the location of fruit
		if(runtime.is_fruit_on_map())
		{
			AISharedRuntime::Gradients::GradientInfo gi_fruit;
			gi_fruit.add_source(new AISharedRuntime::Gradients::Entities::Resource(CHERRY));
			gi_fruit.add_source(new AISharedRuntime::Gradients::Entities::Resource(ORANGE));
			gi_fruit.add_source(new AISharedRuntime::Gradients::Entities::Resource(PRUNE));
			//You want to be reasonably close to fruit, closer if possible
			bo->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_fruit, AI_SHARED_RUNTIME_RTI_INN_FRUIT_WEIGHT));
		}

		//Add the building order to the list of orders
		unsigned int id=runtime.add_building_order(bo);

//				std::cout<<"inn ordered, id="<<id<<std::endl;

		ManagementOrder* mo_completion=new AssignWorkers(1, id);
		mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
		runtime.add_management_order(mo_completion);

		ManagementOrder* mo_tracker=new AddResourceTracker(AI_SHARED_RUNTIME_RTI_TRACKER_LENGTH, RecurringInputStock, id);
		mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
		runtime.add_management_order(mo_tracker);
	}
}
