// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "Material.h"
#include "shared_runtime/Farming.h"
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
	if (runtime.observation().configuration->isResourceGrowthDisabled()) return;
	telemetry.count(AITrace::AI5::NewNicowar_update_farming_calls);
	updateFarmingAreas(runtime);
}


void NewNicowar::update_fruit_flags(AISharedRuntime::Runtime& runtime)
{
	telemetry.count(AITrace::AI5::NewNicowar_update_fruit_flags_calls);
	if(fruit_phase && !exploration_on_fruit)
	{
		//Constraints around nearby settlement
		AISharedRuntime::Gradients::GradientInfo gi_building;
		gi_building.add_source(new AISharedRuntime::Gradients::Entities::AnyTeamBuilding(runtime.teamNumber(), false));


		//The main order for the exploration flag on cherry
		BuildingOrder* bo_cherry = new BuildingOrder(runtime, BuildingDemand::AttractExplorers, AI_NICOWAR_FRUIT_FLAG_WORKERS);
		//You want the closest fruit to your settlement possible
		bo_cherry->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, AI_NICOWAR_FRUIT_FLAG_BUILDING_PREF));
		//Constraint around the location of fruit
		AISharedRuntime::Gradients::GradientInfo gi_cherry;
		gi_cherry.add_source(new AISharedRuntime::Gradients::Entities::MaterialSource(materialIndex(MaterialId::Cherries)));
		//You want to be on top of the cherry trees
		bo_cherry->add_constraint(new AISharedRuntime::Construction::MaximumDistance(gi_cherry, AI_NICOWAR_FRUIT_FLAG_ON_FRUIT_DIST));
		//Add the building order to the list of orders
		unsigned int id_cherry=runtime.add_building_order(bo_cherry);

		ManagementOrder* mo_completion_cherry=new ChangeFlagSize(AI_NICOWAR_FRUIT_FLAG_SIZE, id_cherry);
		runtime.add_management_order(mo_completion_cherry);



		//The main order for the exploration flag in orange
		BuildingOrder* bo_orange = new BuildingOrder(runtime, BuildingDemand::AttractExplorers, AI_NICOWAR_FRUIT_FLAG_WORKERS);
		//You want the closest fruit to your settlement possible
		bo_orange->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, AI_NICOWAR_FRUIT_FLAG_BUILDING_PREF));
		//Constraints around the location of fruit
		AISharedRuntime::Gradients::GradientInfo gi_orange;
		gi_orange.add_source(new AISharedRuntime::Gradients::Entities::MaterialSource(materialIndex(MaterialId::Oranges)));
		//You want to be on top of the orange trees
		bo_orange->add_constraint(new AISharedRuntime::Construction::MaximumDistance(gi_orange, AI_NICOWAR_FRUIT_FLAG_ON_FRUIT_DIST));
		unsigned int id_orange=runtime.add_building_order(bo_orange);

		ManagementOrder* mo_completion_orange=new ChangeFlagSize(AI_NICOWAR_FRUIT_FLAG_SIZE, id_orange);
		runtime.add_management_order(mo_completion_orange);

		//The main order for the exploration flag on prunes
		BuildingOrder* bo_prune = new BuildingOrder(runtime, BuildingDemand::AttractExplorers, AI_NICOWAR_FRUIT_FLAG_WORKERS);
		//You want the closest fruit to your settlement possible
		bo_prune->add_constraint(new AISharedRuntime::Construction::MinimizedDistance(gi_building, AI_NICOWAR_FRUIT_FLAG_BUILDING_PREF));
		AISharedRuntime::Gradients::GradientInfo gi_prune;
		gi_prune.add_source(new AISharedRuntime::Gradients::Entities::MaterialSource(materialIndex(MaterialId::Prunes)));
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

