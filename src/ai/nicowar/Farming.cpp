// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "AITelemetryFields.h"
#include "Game.h"
#include "AINicowar.h"
#include "Building.h"
#include "Unit.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Gradients;
using namespace AISharedRuntime::Construction;
using namespace AISharedRuntime::Management;
using namespace AISharedRuntime::Conditions;
using namespace AISharedRuntime::SearchTools;



void NewNicowar::update_farming(Runtime& runtime)
{
	// Protecting growth cells would permanently withhold resources without regrowth.
	if (runtime.player->game->gameHeader.isResourceGrowthDisabled()) return;
	telemetry.count(AITrace::AI5::NewNicowar_update_farming_calls);
	//Farming wheat and wood in areas near water
	AddArea* mo_farming=new AddArea(ForbiddenArea);
	RemoveArea* mo_non_farming=new RemoveArea(ForbiddenArea);
	AddArea* mo_clearing=new AddArea(ClearingArea);
	RemoveArea* mo_non_clearing=new RemoveArea(ClearingArea);
	// With the farm-areas experiment, wheat near water is farmed with a farm
	// area on the same protection pattern. Wood keeps its forbidden spots.
	MapInfo mi(runtime);
	const bool farms = mi.farm_areas_enabled();
	AddArea* mo_farm=farms ? new AddArea(FarmArea) : nullptr;
	RemoveArea* mo_non_farm=farms ? new RemoveArea(FarmArea) : nullptr;
	AISharedRuntime::Gradients::GradientInfo gi_water;
	gi_water.add_source(new Entities::Water);
	Gradient& water_gradient=runtime.get_gradient_manager().get_gradient(gi_water);

	for(int x=0; x<mi.get_width(); ++x)
	{
		for(int y=0; y<mi.get_height(); ++y)
		{
			if(mi.is_discovered(x, y))
			{
				const int wood_dist = AI_NICOWAR_FARM_WOOD_WATER_DIST;
				const int wheat_dist = AI_NICOWAR_FARM_WHEAT_WATER_DIST;

				bool is_wood = mi.is_resource(x, y, WOOD);
				bool is_wheat = mi.is_resource(x, y, WHEAT);

				bool is_in_wheat_zone = water_gradient.within_dist(x, y, wheat_dist);
				bool is_in_wood_zone = water_gradient.within_dist(x, y, wood_dist);

				bool farm_spot = false;

				//Permanent farming exists for every second row and column
				if(x%AI_NICOWAR_FARM_PATTERN_STRIDE==1 && y%AI_NICOWAR_FARM_PATTERN_STRIDE==1)
				{
					if((is_wood && is_in_wood_zone) || (is_wheat && is_in_wheat_zone))
					{
						farm_spot = true;
					}
				}

				//Expand the farm horizontally
				if((x%AI_NICOWAR_FARM_PATTERN_STRIDE==0 && y%AI_NICOWAR_FARM_PATTERN_STRIDE==1))
				{
					if(is_wood && mi.is_resource(x-1, y, WOOD) && !mi.is_resource(x+1,y) && water_gradient.within_dist(x+1, y, wood_dist) && mi.is_grass(x+1,y))
					{
						farm_spot = true;
					}
					else if(is_wheat && mi.is_resource(x-1, y, WHEAT) && !mi.is_resource(x+1,y) && water_gradient.within_dist(x+1, y, wheat_dist) && mi.is_grass(x+1,y))
					{
						farm_spot = true;
					}
					else if(is_wood && mi.is_resource(x+1, y, WOOD) && !mi.is_resource(x-1,y) && water_gradient.within_dist(x-1, y, wood_dist) && mi.is_grass(x-1,y))
					{
						farm_spot = true;
					}
					else if(is_wheat && mi.is_resource(x+1, y, WHEAT) && !mi.is_resource(x-1,y) && water_gradient.within_dist(x-1, y, wheat_dist) && mi.is_grass(x-1,y))
					{
						farm_spot = true;
					}
				}

				//Expand the farm vertically
				if((x%AI_NICOWAR_FARM_PATTERN_STRIDE==1 && y%AI_NICOWAR_FARM_PATTERN_STRIDE==0))
				{
					if(is_wood && mi.is_resource(x, y-1, WOOD) && !mi.is_resource(x,y+1) && water_gradient.within_dist(x, y+1, wood_dist) && mi.is_grass(x,y+1))
					{
						farm_spot = true;
					}
					else if(is_wheat && mi.is_resource(x, y-1, WHEAT) && !mi.is_resource(x,y+1) && water_gradient.within_dist(x, y+1, wheat_dist) && mi.is_grass(x,y+1))
					{
						farm_spot = true;
					}
					else if(is_wood && mi.is_resource(x, y+1, WOOD) && !mi.is_resource(x,y-1) && water_gradient.within_dist(x, y-1, wood_dist) && mi.is_grass(x,y-1))
					{
						farm_spot = true;
					}
					else if(is_wheat && mi.is_resource(x, y+1, WHEAT) && !mi.is_resource(x,y-1) && water_gradient.within_dist(x, y-1, wheat_dist) && mi.is_grass(x,y-1))
					{
						farm_spot = true;
					}
				}

				// Preserve the existing wood-clearing rules.
				bool clear_wood = is_wood &&
					((is_in_wheat_zone && !is_in_wood_zone) || mi.is_resource(x-1, y, WHEAT) ||
					 mi.is_resource(x+1, y, WHEAT) || mi.is_resource(x, y-1, WHEAT) ||
					 mi.is_resource(x, y+1, WHEAT) || mi.is_resource(x-1, y-1, WHEAT) ||
					 mi.is_resource(x-1, y+1, WHEAT) || mi.is_resource(x+1, y-1, WHEAT) ||
					 mi.is_resource(x+1, y+1, WHEAT));
				bool clearing_area = mi.is_clearing_area(x,y);
				if(clear_wood && !clearing_area)
					mo_clearing->add_location(x, y);
				else if(!is_wood && clearing_area)
				{
					// Keep building clearance; release cleared farm tiles for wheat.
					bool beside_building = false;
					for(int dx=-1; dx<=1; ++dx)
						for(int dy=-1; dy<=1; ++dy)
						{
							int gid = runtime.player->map->getBuilding(x+dx, y+dy);
							if(gid!=NOGBID && Building::GIDtoTeam(gid)==runtime.player->team->teamNumber)
								beside_building = true;
						}
					if(!beside_building)
					{
						mo_non_clearing->add_location(x, y);
						clearing_area = false;
					}
				}

				if(farm_spot && (clear_wood || clearing_area))
				{
					farm_spot = false;
				}

				if(farm_spot && mi.is_sand(x,y))
				{
					farm_spot = false;
				}

				if(farms)
				{
					const bool wheat_farm = farm_spot && is_wheat && mi.can_paint_farm(x, y);
					if(wheat_farm && !mi.is_farm_area(x, y))
						mo_farm->add_location(x, y);
					else if(!wheat_farm && mi.is_farm_area(x, y))
						mo_non_farm->add_location(x, y);
					if(is_wheat) farm_spot = false;
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
	runtime.add_management_order(mo_non_clearing);
	runtime.add_management_order(mo_farming);
	runtime.add_management_order(mo_non_farming);
	if(farms)
	{
		runtime.add_management_order(mo_farm);
		runtime.add_management_order(mo_non_farm);
	}
	runtime.add_management_order(mo_clearing);
}


void NewNicowar::update_fruit_flags(AISharedRuntime::Runtime& runtime)
{
	telemetry.count(AITrace::AI5::NewNicowar_update_fruit_flags_calls);
	if(fruit_phase && !exploration_on_fruit)
	{
		//Constraints around nearby settlement
		AISharedRuntime::Gradients::GradientInfo gi_building;
		gi_building.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.player->team->teamNumber, false));


		//The main order for the exploration flag on cherry
		BuildingOrder* bo_cherry = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, AI_NICOWAR_FRUIT_FLAG_WORKERS);
		//You want the closest fruit to your settlement possible
		bo_cherry->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, AI_NICOWAR_FRUIT_FLAG_BUILDING_PREF));
		//Constraint around the location of fruit
		AISharedRuntime::Gradients::GradientInfo gi_cherry;
		gi_cherry.add_source(new AISharedRuntime::Gradients::Entities::Resource(CHERRY));
		//You want to be on top of the cherry trees
		bo_cherry->add_constraint(new AISharedRuntime::Construction::MaximumDistance(gi_cherry, AI_NICOWAR_FRUIT_FLAG_ON_FRUIT_DIST));
		//Add the building order to the list of orders
		unsigned int id_cherry=runtime.add_building_order(bo_cherry);

		ManagementOrder* mo_completion_cherry=new ChangeFlagSize(AI_NICOWAR_FRUIT_FLAG_SIZE, id_cherry);
		runtime.add_management_order(mo_completion_cherry);



		//The main order for the exploration flag in orange
		BuildingOrder* bo_orange = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, AI_NICOWAR_FRUIT_FLAG_WORKERS);
		//You want the closest fruit to your settlement possible
		bo_orange->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, AI_NICOWAR_FRUIT_FLAG_BUILDING_PREF));
		//Constraints around the location of fruit
		AISharedRuntime::Gradients::GradientInfo gi_orange;
		gi_orange.add_source(new AISharedRuntime::Gradients::Entities::Resource(ORANGE));
		//You want to be on top of the orange trees
		bo_orange->add_constraint(new AISharedRuntime::Construction::MaximumDistance(gi_orange, AI_NICOWAR_FRUIT_FLAG_ON_FRUIT_DIST));
		unsigned int id_orange=runtime.add_building_order(bo_orange);

		ManagementOrder* mo_completion_orange=new ChangeFlagSize(AI_NICOWAR_FRUIT_FLAG_SIZE, id_orange);
		runtime.add_management_order(mo_completion_orange);

		//The main order for the exploration flag on prunes
		BuildingOrder* bo_prune = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG, AI_NICOWAR_FRUIT_FLAG_WORKERS);
		//You want the closest fruit to your settlement possible
		bo_prune->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, AI_NICOWAR_FRUIT_FLAG_BUILDING_PREF));
		AISharedRuntime::Gradients::GradientInfo gi_prune;
		gi_prune.add_source(new AISharedRuntime::Gradients::Entities::Resource(PRUNE));
		//You want to be on top of the prune trees
		bo_prune->add_constraint(new AISharedRuntime::Construction::MaximumDistance(gi_prune, AI_NICOWAR_FRUIT_FLAG_ON_FRUIT_DIST));
		//Add the building order to the list of orders
		unsigned int id_prune=runtime.add_building_order(bo_prune);

		ManagementOrder* mo_completion_prune=new ChangeFlagSize(AI_NICOWAR_FRUIT_FLAG_SIZE, id_prune);
		runtime.add_management_order(mo_completion_prune);



		exploration_on_fruit=true;
	}
	update_fruit_alliances(runtime);
}


void NewNicowar::update_fruit_alliances(AISharedRuntime::Runtime& runtime)
{
	telemetry.count(AITrace::AI5::NewNicowar_update_fruit_alliances_calls);
	bool activated=fruit_phase;

	for(enemy_team_iterator i(runtime); i!=enemy_team_iterator(); ++i)
	{
		ManagementOrder* mo_alliance=new ChangeAlliances(*i, indeterminate, indeterminate, indeterminate, activated, indeterminate);
		runtime.add_management_order(mo_alliance);
	}
}

