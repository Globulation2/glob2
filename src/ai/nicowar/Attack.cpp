// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "AITelemetryFields.h"
#include "AINicowar.h"
#include <string>
#include "Utilities.h"
#include "Game.h"
#include "Unit.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Gradients;
using namespace AISharedRuntime::Construction;
using namespace AISharedRuntime::Management;
using namespace AISharedRuntime::Conditions;
using namespace AISharedRuntime::SearchTools;



int NewNicowar::choose_building_to_attack(Runtime& runtime)
{
	telemetry.count(AITrace::AI5::NewNicowar_choose_building_to_attack_calls);
	std::vector<int> buildings_to_attack;
	buildings_to_attack.reserve(100);

	AISharedRuntime::Gradients::GradientInfo gi_building;
    gi_building.terrainTravel=field::TerrainTravel::Swim;
	gi_building.add_source(new Entities::AnyTeamBuilding(runtime.teamNumber(), false));
	gi_building.add_obstacle(new Entities::AnyResource);
	Gradient& gradient=runtime.get_gradient_manager().get_gradient(gi_building);

	for(enemy_building_iterator ebi(runtime, target, -1, -1, indeterminate); ebi!=enemy_building_iterator(); ++ebi)
	{
		const AIEngine::BuildingView* b=runtime.observation().buildingSlots(target)[Building::GIDtoID(*ebi)];
		if(gradient.get_height(b->posX, b->posY) != AI_NICOWAR_GRADIENT_UNREACHABLE)
			buildings_to_attack.push_back(*ebi);
	}

	if(buildings_to_attack.size() == 0)
		return telemetry.returnedInt(AITrace::AI5::NewNicowar_choose_building_to_attack_result, -1);

	int num=runtime.random() % buildings_to_attack.size();
	return telemetry.returnedInt(AITrace::AI5::NewNicowar_choose_building_to_attack_result,
								 buildings_to_attack[num]);
}


void NewNicowar::attack_building(Runtime& runtime)
{
	int building=choose_building_to_attack(runtime);
	if(building==-1)
	{
		if(!is_digging_out)
			if(!dig_out_enemy(runtime))
			{
				target = AI_NICOWAR_NO_TARGET;
			}
		return;
	}
	BuildingOrder* bo = new BuildingOrder(runtime, BuildingDemand::AttractWarriors, strategy.war_phase_war_flag_units_assigned);
	bo->add_constraint(new CenterOfBuilding(building));
	unsigned int id=runtime.add_building_order(bo);

	ManagementOrder* mo_minimum=new ChangeFlagMinimumLevel(runtime.observation().configuration->isUnitUpgradesDisabled() ? 1 : AI_NICOWAR_WAR_FLAG_MIN_LEVEL,id);
	runtime.add_management_order(mo_minimum);

	ManagementOrder* mo_destroyed_1=new RetireAttraction(id,1u<<WARRIOR);
	mo_destroyed_1->add_condition(new EnemyBuildingDestroyed(runtime, building));
	runtime.add_management_order(mo_destroyed_1);

	ManagementOrder* mo_destroyed_2=new SendMessage("attack finished "+std::to_string(id));
	mo_destroyed_2->add_condition(new AttractionRetiredOrDestroyed(id,1u<<WARRIOR));
	runtime.add_management_order(mo_destroyed_2);
	
	attack_flags.push_back(id);
}


void NewNicowar::control_attacks(Runtime& runtime)
{
	// Combat cannot damage opponents here; military work must not reserve economic labour.
	if (runtime.observation().configuration->isPeacefulModeEnabled()) return;
	telemetry.count(AITrace::AI5::NewNicowar_control_attacks_calls);
	choose_enemy_target(runtime);

	if(target!=AI_NICOWAR_NO_TARGET)
	{
		unsigned number_attacks=0;
		if(war)
		{
			number_attacks=strategy.war_phase_num_attack_flags;
		}

		if(attack_flags.size() < number_attacks)
		{
			attack_building(runtime);
		}
	}

	BuildingSearch bs_pool(runtime);
	bs_pool.add_condition(new ProvidesBuildingCapability(BuildingDemand::TrainSwim));
	int num_pool=bs_pool.count_buildings();
	
	AISharedRuntime::Gradients::GradientInfo gi_building;
    gi_building.terrainTravel=field::TerrainTravel::Swim;
	gi_building.add_source(new Entities::AnyTeamBuilding(runtime.teamNumber(), false));
	gi_building.add_obstacle(new Entities::AnyResource);
	if(num_pool == 0)
    {
        gi_building.terrainTravel=field::TerrainTravel::Walk;
		gi_building.add_obstacle(new Entities::Unwalkable);
    }
	Gradient& gradient=runtime.get_gradient_manager().get_gradient(gi_building);
	
	for(unsigned i=0; i<attack_flags.size(); ++i)
	{
		if(runtime.get_building_register().is_building_found(attack_flags[i]))
		{
			const AIEngine::BuildingView* b = runtime.get_building_register().get_building(attack_flags[i]);
			if(b && gradient.get_height(b->posX, b->posY) == AI_NICOWAR_GRADIENT_UNREACHABLE)
			{
				ManagementOrder* mo_destroy=new RetireAttraction(attack_flags[i],1u<<WARRIOR);
				runtime.add_management_order(mo_destroy);
			}
		}
	}
}



void NewNicowar::choose_enemy_target(Runtime& runtime)
{
	telemetry.count(AITrace::AI5::NewNicowar_choose_enemy_target_calls);
	AISharedRuntime::Gradients::GradientInfo gi_building;
    gi_building.terrainTravel=field::TerrainTravel::Swim;
	gi_building.add_source(new Entities::AnyTeamBuilding(runtime.teamNumber(), false));
	gi_building.add_obstacle(new Entities::AnyResource);
	Gradient& gradient=runtime.get_gradient_manager().get_gradient(gi_building);

	if(target==AI_NICOWAR_NO_TARGET || !runtime.observation().teams[target].alive)
	{
		std::vector<int> available_reachable_targets;
		std::vector<int> available_targets;
		for(enemy_team_iterator i(runtime); i!=enemy_team_iterator(); ++i)
		{
			if(runtime.observation().teams[*i].alive)
			{
				available_targets.push_back(*i);
				enemy_building_iterator ebi(runtime, *i, -1, -1, indeterminate);
				/* Make sure we know of at least one
				   building that we can directly attack
				   before committing to a particular enemy.
				   It used to be that we did not (normally)
				   need to test this, because all starting
				   buildings were known. But that was
				   cheating and has been fixed. */
				for(; ebi != enemy_building_iterator(); ++ebi)
				{
					const AIEngine::BuildingView* b=runtime.observation().buildingSlots(*i)[Building::GIDtoID(*ebi)];
					if(gradient.get_height(b->posX, b->posY) != AI_NICOWAR_GRADIENT_UNREACHABLE)
					{
						available_reachable_targets.push_back(*i);
						break;
					}
				}
			}
		}
		if(available_reachable_targets.size()!=0)
			target=available_reachable_targets[runtime.random() % available_reachable_targets.size()];
		else if(available_targets.size()!=0)
			target=available_targets[runtime.random() % available_targets.size()];
		else
			target=AI_NICOWAR_NO_TARGET;
	}
}



bool NewNicowar::dig_out_enemy(Runtime& runtime)
{
	telemetry.count(AITrace::AI5::NewNicowar_dig_out_enemy_calls);
	///First choose an enemy building to dig out
	std::vector<int> buildings_to_attack;
	buildings_to_attack.reserve(100);

	MapInfo mi(runtime);

	AISharedRuntime::Gradients::GradientInfo gi_building;
    gi_building.terrainTravel=field::TerrainTravel::Swim;
	gi_building.add_source(new Entities::AnyTeamBuilding(runtime.teamNumber(), false));
	gi_building.add_obstacle(new Entities::AnyResource);
	Gradient& gradient=runtime.get_gradient_manager().get_gradient(gi_building);

	for(enemy_building_iterator ebi(runtime, target, -1, -1, indeterminate); ebi!=enemy_building_iterator(); ++ebi)
	{
		const AIEngine::BuildingView* b=runtime.observation().buildingSlots(target)[Building::GIDtoID(*ebi)];
		int bx = (b->posX + mi.get_width()) % mi.get_width();
		int by = (b->posY + mi.get_height()) % mi.get_height();
		if(gradient.get_height(bx, by) == AI_NICOWAR_GRADIENT_UNREACHABLE)
			buildings_to_attack.push_back(*ebi);
	}

	if(buildings_to_attack.size() == 0)
		return telemetry.returnedBool(AITrace::AI5::NewNicowar_dig_out_enemy_result,
									  AITrace::AI5::NewNicowar_dig_out_enemy_true, false);

	int num=runtime.random() % buildings_to_attack.size();


	int building=buildings_to_attack[num];
	const int bx=(runtime.observation().buildingSlots(target)[Building::GIDtoID(building)]->posX) % mi.get_width();
	const int by=(runtime.observation().buildingSlots(target)[Building::GIDtoID(building)]->posY) % mi.get_height();

	AISharedRuntime::Gradients::GradientInfo gi_pathfind;
    gi_pathfind.terrainTravel=field::TerrainTravel::Swim;
	gi_pathfind.add_source(new Entities::Position(bx, by));
	gi_pathfind.add_obstacle(new Entities::Resource(STONE));
	Gradient& gradient_pathfind=runtime.get_gradient_manager().get_gradient(gi_pathfind);

	///Next, find the closest point manhattan distance wise, to the building that is accessible
	int closest_x=0;
	int closest_y=0;
	int closest_distance=AI_NICOWAR_DIG_OUT_INIT_DIST;
	for(int x=0; x<mi.get_width(); ++x)
	{
		for(int y=0; y<mi.get_height(); ++y)
		{
			if(gradient.get_height(x, y) >= 0)
			{
				int dist=gradient_pathfind.get_height(x, y);
				if(dist < closest_distance)
				{
					closest_x=x;
					closest_y=y;
					closest_distance=dist;
				}
			}
		}
	}

	///Next, follow a path around stone between the closest point and the buildings position, 
	///placing Clearing flags as you go

	int xpos=closest_x;
	int ypos=closest_y;

	int flag_dist_count=AI_NICOWAR_DIG_FLAG_INIT_COUNTER;

	int w=mi.get_width();
	int h=mi.get_height();

	while(xpos != bx || ypos!=by)
	{
		int nxpos = xpos;
		int nypos = ypos;
		int rx=(xpos+1+w) % w;
		int lx=(xpos-1+w) % w;
		int dy=(ypos+1+h) % h;
		int uy=(ypos-1+h) % h;
		int lowest_entity=gradient_pathfind.get_height(xpos, ypos)+AI_NICOWAR_PATHFIND_TOLERANCE;

		if(lowest_entity == 0)
			break;

		//Test diagonals first, then the horizontals and verticals.
		if(gradient_pathfind.get_height(lx, uy) < lowest_entity && gradient_pathfind.get_height(lx, uy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(lx, uy);
			nxpos=lx;
			nypos=uy;
		}
		if(gradient_pathfind.get_height(rx, uy) < lowest_entity && gradient_pathfind.get_height(rx, uy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(rx, uy);
			nxpos=rx;
			nypos=uy;
		}
		if(gradient_pathfind.get_height(lx, dy) < lowest_entity && gradient_pathfind.get_height(lx, dy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(lx, dy);
			nxpos=lx;
			nypos=dy;
		}
		if(gradient_pathfind.get_height(rx, dy) < lowest_entity && gradient_pathfind.get_height(rx, dy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(rx, dy);
			nxpos=rx;
			nypos=dy;
		}

		if(gradient_pathfind.get_height(xpos, uy) < lowest_entity && gradient_pathfind.get_height(xpos, uy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(xpos, uy);
			nxpos=xpos;
			nypos=uy;
		}
		if(gradient_pathfind.get_height(lx, ypos) < lowest_entity && gradient_pathfind.get_height(lx, ypos)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(lx, ypos);
			nxpos=lx;
			nypos=ypos;
		}
		if(gradient_pathfind.get_height(rx, ypos) < lowest_entity && gradient_pathfind.get_height(rx, ypos)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(rx, ypos);
			nxpos=rx;
			nypos=ypos;
		}
		if(gradient_pathfind.get_height(xpos, dy) < lowest_entity && gradient_pathfind.get_height(xpos, dy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(xpos, dy);
			nxpos=xpos;
			nypos=dy;
		}


		flag_dist_count+=1;


		if(flag_dist_count>AI_NICOWAR_DIG_FLAG_INTERVAL)
		{
			flag_dist_count=0;
			//The main order for the clearing flag
			BuildingOrder* bo_flag = new BuildingOrder(runtime, BuildingDemand::ClearResources, AI_NICOWAR_DIG_CLEARING_WORKERS);
			//Place it on the current point
			bo_flag->add_constraint(new Construction::SinglePosition(xpos, ypos));
			//Add the building order to the list of orders
			unsigned int id_flag=runtime.add_building_order(bo_flag);

			ManagementOrder* mo_destroyed=new RetireAttraction(id_flag,1u<<WORKER);
			mo_destroyed->add_condition(new EnemyBuildingDestroyed(runtime, building));
			runtime.add_management_order(mo_destroyed);


			ManagementOrder* mo_completion=new ChangeFlagSize(AI_NICOWAR_DIG_FLAG_SIZE, id_flag);
			runtime.add_management_order(mo_completion);
		}
		xpos = nxpos;
		ypos = nypos;

	}

	ManagementOrder* mo_destroyed=new SendMessage("finished digging out");
	mo_destroyed->add_condition(new EnemyBuildingDestroyed(runtime, building));
	runtime.add_management_order(mo_destroyed);

	is_digging_out=true;

	return telemetry.returnedBool(AITrace::AI5::NewNicowar_dig_out_enemy_result,
								  AITrace::AI5::NewNicowar_dig_out_enemy_true, true);
}

