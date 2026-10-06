// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "Material.h"
#include "AITelemetryFields.h"
#include "Game.h"
#include "shared_runtime/Runtime.h"
#include "shared_runtime/BuildingDemands.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Gradients;
using namespace AISharedRuntime::Construction;
using namespace AISharedRuntime::Management;
using namespace AISharedRuntime::Conditions;
using namespace AISharedRuntime::SearchTools;


//Explorer flags on the three nearest fruit trees
void Econo::tick_explorer_flags_fruit(Runtime& runtime)
{
	telemetry.count(AITrace::AI4::Econo_tick_explorer_flags_fruit_calls);
	if((timer%AI_SHARED_RUNTIME_RTI_FRUIT_FLAG_INTERVAL_TICKS)==0)
	{
		if(runtime.is_fruit_on_map())
		{
			if(runtime.get_team_stats().numberUnitPerType[EXPLORER]>=AI_SHARED_RUNTIME_RTI_FRUIT_FLAG_EXPLORER_MIN && !flag_on_cherry && !flag_on_orange && !flag_on_prune)
			{
				//Constraints around nearby settlement
				AISharedRuntime::Gradients::GradientInfo gi_building;
				gi_building.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.player->team->teamNumber, false));

				if(!flag_on_cherry)
				{
					//The main order for the exploration flag
					BuildingOrder* bo_cherry = new BuildingOrder(runtime, BuildingDemand::AttractExplorers, 2);

					//You want the closest fruit to your settlement possible
					bo_cherry->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, 1));

					//Constraint around the location of fruit
					AISharedRuntime::Gradients::GradientInfo gi_cherry;
					gi_cherry.add_source(new AISharedRuntime::Gradients::Entities::MaterialSource(materialIndex(MaterialId::Cherries)));
					//You want to be on top of the cherry trees
					bo_cherry->add_constraint(new AISharedRuntime::Construction::MaximumDistance(gi_cherry, 0));

					//Add the building order to the list of orders
					unsigned int id_cherry=runtime.add_building_order(bo_cherry);

					if(id_cherry!=INVALID_BUILDING)
					{
						ManagementOrder* mo_completion=new ChangeFlagSize(AI_SHARED_RUNTIME_RTI_FRUIT_FLAG_RADIUS, id_cherry);
						runtime.add_management_order(mo_completion);
						flag_on_cherry=true;

						for(enemy_team_iterator i(runtime); i!=enemy_team_iterator(); ++i)
						{
							ManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, true, indeterminate);
							runtime.add_management_order(mo_alliance);
						}
					}
				}

				if(!flag_on_orange)
				{
					//The main order for the exploration flag
					BuildingOrder* bo_orange = new BuildingOrder(runtime, BuildingDemand::AttractExplorers, 2);

					//You want the closest fruit to your settlement possible
					bo_orange->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, 1));

					//Constraints around the location of fruit
					AISharedRuntime::Gradients::GradientInfo gi_orange;
					gi_orange.add_source(new AISharedRuntime::Gradients::Entities::MaterialSource(materialIndex(MaterialId::Oranges)));
					//You want to be on top of the orange trees
					bo_orange->add_constraint(new AISharedRuntime::Construction::MaximumDistance(gi_orange, 0));

					unsigned int id_orange=runtime.add_building_order(bo_orange);

					if(id_orange!=INVALID_BUILDING)
					{
						ManagementOrder* mo_completion=new ChangeFlagSize(AI_SHARED_RUNTIME_RTI_FRUIT_FLAG_RADIUS, id_orange);
						runtime.add_management_order(mo_completion);
						flag_on_orange=true;

						for(enemy_team_iterator i(runtime); i!=enemy_team_iterator(); ++i)
						{
							ManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, true, indeterminate);
							runtime.add_management_order(mo_alliance);
						}
					}
				}

				if(!flag_on_prune)
				{
					//The main order for the exploration flag
					BuildingOrder* bo_prune = new BuildingOrder(runtime, BuildingDemand::AttractExplorers, 2);

					//You want the closest fruit to your settlement possible
					bo_prune->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, 1));

					AISharedRuntime::Gradients::GradientInfo gi_prune;
					gi_prune.add_source(new AISharedRuntime::Gradients::Entities::MaterialSource(materialIndex(MaterialId::Prunes)));
					//You want to be on top of the prune trees
					bo_prune->add_constraint(new AISharedRuntime::Construction::MaximumDistance(gi_prune, 0));

					//Add the building order to the list of orders
					unsigned int id_prune=runtime.add_building_order(bo_prune);

					if(id_prune!=INVALID_BUILDING)
					{
						ManagementOrder* mo_completion=new ChangeFlagSize(AI_SHARED_RUNTIME_RTI_FRUIT_FLAG_RADIUS, id_prune);
						runtime.add_management_order(mo_completion);
						flag_on_prune=true;

						for(enemy_team_iterator i(runtime); i!=enemy_team_iterator(); ++i)
						{
							ManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, true, indeterminate);
							runtime.add_management_order(mo_alliance);
						}
					}
				}
			}
		}
	}
}

//Place exploration flags on the enemy swarms
void Econo::tick_explorer_flags_enemies(Runtime& runtime)
{
	// Combat cannot damage opponents here; military work must not reserve economic labour.
	if (runtime.player->game->gameHeader.isPeacefulModeEnabled()) return;
	telemetry.count(AITrace::AI4::Econo_tick_explorer_flags_enemies_calls);
	if((timer%AI_SHARED_RUNTIME_RTI_ENEMY_SCAN_INTERVAL_TICKS)==0)
	{
		if(runtime.get_team_stats().numberUnitPerType[EXPLORER]>=AI_SHARED_RUNTIME_RTI_ENEMY_FLAG_EXPLORER_MIN)
		{
			for(enemy_team_iterator i(runtime); i!=enemy_team_iterator(); ++i)
			{
				for(enemy_building_iterator ebi(runtime, *i, BuildingDemand::ProduceWorker, AI_SHARED_RUNTIME_WILDCARD_LEVEL, false); ebi!=enemy_building_iterator(); ++ebi)
				{
					if(flags_on_enemy.find(*i)!=flags_on_enemy.end())
						continue;

					BuildingOrder* bo = new BuildingOrder(runtime, BuildingDemand::AttractExplorers, 1);
					bo->add_constraint(new CenterOfBuilding(*ebi));
					unsigned int id=runtime.add_building_order(bo);

					if(id!=INVALID_BUILDING)
					{
						ManagementOrder* mo_completion=new ChangeFlagSize(AI_SHARED_RUNTIME_RTI_ENEMY_FLAG_RADIUS, id);
						runtime.add_management_order(mo_completion);

						ManagementOrder* mo_destroyed=new RetireAttraction(id,1u<<EXPLORER);
						mo_destroyed->add_condition(new EnemyBuildingDestroyed(runtime, *ebi));
						runtime.add_management_order(mo_destroyed);

						flags_on_enemy.insert(*i);
					}
				}
			}
		}
	}
}

//Farming wheat and wood near water
void Econo::tick_farming_areas(Runtime& runtime)
{
	// Protecting growth cells would permanently withhold resources without regrowth.
	if (runtime.player->game->gameHeader.isResourceGrowthDisabled()) return;
	telemetry.count(AITrace::AI4::Econo_tick_farming_areas_calls);
	if((timer%AI_SHARED_RUNTIME_RTI_FARMING_INTERVAL_TICKS)==0)
	{
		AddArea* mo_farming=new AddArea(ForbiddenArea);
		RemoveArea* mo_non_farming=new RemoveArea(ForbiddenArea);
		AISharedRuntime::Gradients::GradientInfo gi_water;
		gi_water.add_source(new Entities::Water);
		Gradient& gradient=runtime.get_gradient_manager().get_gradient(gi_water);
		MapInfo mi(runtime);
		// With the farm-areas experiment, wheat near water is farmed with a farm
		// area on the same lattice; forbidden spots continue to protect wood.
		const bool farms = mi.farm_areas_enabled();
		AddArea* mo_farm=farms ? new AddArea(FarmArea) : nullptr;
		RemoveArea* mo_non_farm=farms ? new RemoveArea(FarmArea) : nullptr;
		for(int x=0; x<mi.get_width(); ++x)
		{
			for(int y=0; y<mi.get_height(); ++y)
			{
				const bool farm_spot = x%AI_SHARED_RUNTIME_RTI_FARMING_PATTERN_STRIDE==1 &&
					y%AI_SHARED_RUNTIME_RTI_FARMING_PATTERN_STRIDE==1;
				if(farms && mi.is_discovered(x, y))
				{
					const bool wheat_farm = farm_spot &&
						mi.is_resource(x, y, materialIndex(MaterialId::Food)) && mi.can_paint_farm(x, y) &&
						gradient.within_dist(x, y, AI_SHARED_RUNTIME_RTI_FARMING_WATER_MAX_DIST);
					if(wheat_farm && !mi.is_farm_area(x, y))
						mo_farm->add_location(x, y);
					else if(!wheat_farm && mi.is_farm_area(x, y))
						mo_non_farm->add_location(x, y);
				}
				if(farm_spot)
				{
					const bool protected_resource = farms
						? mi.is_resource(x, y, materialIndex(MaterialId::Wood))
						: mi.is_resource(x, y, materialIndex(MaterialId::Wood)) || mi.is_resource(x, y, materialIndex(MaterialId::Food));
					if(!protected_resource && mi.is_forbidden_area(x, y))
					{
						mo_non_farming->add_location(x, y);
					}
					else
					{
						if(protected_resource &&
						    mi.is_discovered(x, y) &&
						    !mi.is_forbidden_area(x, y) &&
						    gradient.within_dist(x, y, AI_SHARED_RUNTIME_RTI_FARMING_WATER_MAX_DIST))
						{
							mo_farming->add_location(x, y);
						}
					}
				}
			}
		}
		runtime.add_management_order(mo_farming);
		runtime.add_management_order(mo_non_farming);
		if(farms)
		{
			runtime.add_management_order(mo_farm);
			runtime.add_management_order(mo_non_farm);
		}
	}
}
