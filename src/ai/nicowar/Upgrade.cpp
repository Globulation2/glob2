// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "AITelemetryFields.h"
#include "AINicowar.h"
#include "FormatableString.h"
#include <string>
#include "Utilities.h"
#include "Unit.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Gradients;
using namespace AISharedRuntime::Construction;
using namespace AISharedRuntime::Management;
using namespace AISharedRuntime::Conditions;
using namespace AISharedRuntime::SearchTools;



int NewNicowar::choose_building_upgrade_type_level1(Runtime& runtime)
{
	telemetry.count(AITrace::AI5::NewNicowar_choose_building_upgrade_type_level1_calls);
	BuildingSearch schools(runtime);
	schools.add_condition(new ProvidesBuildingCapability(BuildingDemand::TrainConstruction));
	schools.add_condition(new BeingUpgradedTo(2));
	const int school_counts=schools.count_buildings();

	///Schools are only upgraded one at a time
	int school_chance = strategy.upgrading_phase_1_school_chance;
	if(school_counts>0)
		school_chance=0;

	return telemetry.returnedInt(
		AITrace::AI5::NewNicowar_choose_building_upgrade_type_level1_result,
		choose_building_upgrade_type(runtime, 1, strategy.upgrading_phase_1_inn_chance,
									 strategy.upgrading_phase_1_hospital_chance,
									 strategy.upgrading_phase_1_racetrack_chance,
									 strategy.upgrading_phase_1_swimmingpool_chance,
									 strategy.upgrading_phase_1_barracks_chance, school_chance,
									 strategy.upgrading_phase_1_tower_chance));
}



int NewNicowar::choose_building_upgrade_type_level2(Runtime& runtime)
{
	telemetry.count(AITrace::AI5::NewNicowar_choose_building_upgrade_type_level2_calls);
	BuildingSearch schools_upgrading(runtime);
	schools_upgrading.add_condition(new ProvidesBuildingCapability(BuildingDemand::TrainConstruction));
	schools_upgrading.add_condition(new BeingUpgradedTo(3));
	const int school_counts_upgrading=schools_upgrading.count_buildings();

	BuildingSearch schools_lvl2(runtime);
	schools_lvl2.add_condition(new ProvidesBuildingCapability(BuildingDemand::TrainConstruction));
	schools_lvl2.add_condition(new BuildingLevel(2));
	schools_lvl2.add_condition(new NotUnderConstruction);
	const int school_counts_level2=schools_lvl2.count_buildings();

	BuildingSearch schools_lvl3(runtime);
	schools_lvl3.add_condition(new ProvidesBuildingCapability(BuildingDemand::TrainConstruction));
	schools_lvl3.add_condition(new BuildingLevel(3));
	schools_lvl3.add_condition(new NotUnderConstruction);
	const int school_counts_level3=schools_lvl3.count_buildings();

	///Schools are only upgraded one at a time
	int school_chance = strategy.upgrading_phase_2_school_chance;
	if(school_counts_upgrading>0 || (school_counts_level2 + school_counts_level3)<AI_NICOWAR_LVL2_SCHOOL_THRESHOLD)
		school_chance=0;

	return telemetry.returnedInt(
		AITrace::AI5::NewNicowar_choose_building_upgrade_type_level2_result,
		choose_building_upgrade_type(runtime, 2, strategy.upgrading_phase_2_inn_chance,
									 strategy.upgrading_phase_2_hospital_chance,
									 strategy.upgrading_phase_2_racetrack_chance,
									 strategy.upgrading_phase_2_swimmingpool_chance,
									 strategy.upgrading_phase_2_barracks_chance, school_chance,
									 strategy.upgrading_phase_2_tower_chance));
}


int NewNicowar::choose_building_upgrade_type(Runtime& runtime, int level, int inn_ratio, int hospital_ratio, int racetrack_ratio, int swimmingpool_ratio, int barracks_ratio, int school_ratio, int tower_ratio)
{
	telemetry.set(AITrace::AI5::NewNicowar_choose_building_upgrade_type_input_tower_ratio,
				  tower_ratio);
	telemetry.set(AITrace::AI5::NewNicowar_choose_building_upgrade_type_input_school_ratio,
				  school_ratio);
	telemetry.set(AITrace::AI5::NewNicowar_choose_building_upgrade_type_input_barracks_ratio,
				  barracks_ratio);
	telemetry.set(AITrace::AI5::NewNicowar_choose_building_upgrade_type_input_swimmingpool_ratio,
				  swimmingpool_ratio);
	telemetry.set(AITrace::AI5::NewNicowar_choose_building_upgrade_type_input_racetrack_ratio,
				  racetrack_ratio);
	telemetry.set(AITrace::AI5::NewNicowar_choose_building_upgrade_type_input_hospital_ratio,
				  hospital_ratio);
	telemetry.set(AITrace::AI5::NewNicowar_choose_building_upgrade_type_input_inn_ratio, inn_ratio);
	telemetry.set(AITrace::AI5::NewNicowar_choose_building_upgrade_type_input_level, level);
	telemetry.count(AITrace::AI5::NewNicowar_choose_building_upgrade_type_calls);
	///First count the types of buildings that are available to us for upgrading
	///you wouldn't want to choose a Barracks to be upgraded if there are none
	int building_count[BuildingDemand::Count];
	std::fill(building_count, building_count+BuildingDemand::Count, 0);

	BuildingSearch bs(runtime);
	bs.add_condition(new NotUnderConstruction);
	bs.add_condition(new BuildingLevel(level));
	bs.add_condition(new Upgradable);
	for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
	{
		for(int demand=0;demand<BuildingDemand::Count;++demand)
   if(runtime.get_building_register().provides(*i,demand)) ++building_count[demand];
	}

	///Next, add in the n slices for each of the buildings with respect to their ratio
	std::vector<int> buildings;
	buildings.reserve(100);
	if(building_count[BuildingDemand::Feed] > 0)
	{
		for(int n=0; n<inn_ratio; ++n)
			buildings.push_back(BuildingDemand::Feed);
	}
	if(building_count[BuildingDemand::Heal] > 0)
	{
		for(int n=0; n<hospital_ratio; ++n)
			buildings.push_back(BuildingDemand::Heal);
	}
	if(building_count[BuildingDemand::TrainWalk] > 0)
	{
		for(int n=0; n<racetrack_ratio; ++n)
			buildings.push_back(BuildingDemand::TrainWalk);
	}
	if(building_count[BuildingDemand::TrainSwim] > 0)
	{
		for(int n=0; n<swimmingpool_ratio; ++n)
			buildings.push_back(BuildingDemand::TrainSwim);
	}
	if(building_count[BuildingDemand::TrainAttackStrength] > 0)
	{
		for(int n=0; n<barracks_ratio; ++n)
			buildings.push_back(BuildingDemand::TrainAttackStrength);
	}
	if(building_count[BuildingDemand::TrainConstruction] > 0)
	{
		for(int n=0; n<school_ratio; ++n)
			buildings.push_back(BuildingDemand::TrainConstruction);
	}
	if(building_count[BuildingDemand::ProjectileDefense] > 0)
	{
		for(int n=0; n<tower_ratio; ++n)
			buildings.push_back(BuildingDemand::ProjectileDefense);
	}

	if(buildings.size()==0)
		return telemetry.returnedInt(AITrace::AI5::NewNicowar_choose_building_upgrade_type_result,
									 AI_NICOWAR_NO_BUILDING_TYPE);

	//Now choose a building, or return AI_NICOWAR_NO_BUILDING_TYPE for none available
	int random = runtime.random() % buildings.size();

	return telemetry.returnedInt(AITrace::AI5::NewNicowar_choose_building_upgrade_type_result,
								 buildings[random]);
}


int NewNicowar::choose_building_for_upgrade(Runtime& runtime, int type, int level)
{
	telemetry.set(AITrace::AI5::NewNicowar_choose_building_for_upgrade_input_level, level);
	telemetry.set(AITrace::AI5::NewNicowar_choose_building_for_upgrade_input_type, type);
	telemetry.count(AITrace::AI5::NewNicowar_choose_building_for_upgrade_calls);
	BuildingSearch bs(runtime);
	bs.add_condition(new ProvidesBuildingCapability(type));
	bs.add_condition(new NotUnderConstruction);
	bs.add_condition(new BuildingLevel(level));
	bs.add_condition(new Upgradable);
	std::vector<int> buildings;
	std::copy(bs.begin(), bs.end(), std::back_insert_iterator<std::vector<int> >(buildings));
	if(buildings.empty()) return AI_NICOWAR_NO_BUILDING_TYPE;
 int random=runtime.random() % buildings.size();
	int id=buildings[random];

	return telemetry.returnedInt(AITrace::AI5::NewNicowar_choose_building_for_upgrade_result, id);
}


void NewNicowar::upgrade_buildings(Runtime& runtime)
{
	TeamStat* stat=runtime.player->team->stats.getLatestStat();
	int can_upgrade_level1 = stat->workersByConstructionLevel[1] + stat->workersByConstructionLevel[2] + stat->workersByConstructionLevel[3];
	int can_upgrade_level2 = stat->workersByConstructionLevel[2] + stat->workersByConstructionLevel[3];

	int num_to_upgrade_level1=0;
	int num_to_upgrade_level2=0;
	if(upgrading_phase_1)
	{
		//rounded up
		num_to_upgrade_level1=(can_upgrade_level1 + strategy.upgrading_phase_1_num_units/2) / (strategy.upgrading_phase_1_num_units);
	}
	else
	{
		num_to_upgrade_level1=0;
	}

	if(upgrading_phase_2)
	{
		//rounded up
		num_to_upgrade_level2=(can_upgrade_level2 + strategy.upgrading_phase_2_num_units/2) / (strategy.upgrading_phase_2_num_units);
	}
	else
	{
		num_to_upgrade_level2=0;
	}

	BuildingSearch bs_lvl1(runtime);
	bs_lvl1.add_condition(new BeingUpgradedTo(2));
	int num_upgrading_level1=bs_lvl1.count_buildings();

	BuildingSearch bs_lvl2(runtime);
	bs_lvl2.add_condition(new BeingUpgradedTo(3));
	int num_upgrading_level2=bs_lvl2.count_buildings();

	///Level one upgrades
	if(num_upgrading_level1 < num_to_upgrade_level1)
	{
		int building_type=choose_building_upgrade_type_level1(runtime);
		if(building_type!=AI_NICOWAR_NO_BUILDING_TYPE)
		{


			int id=choose_building_for_upgrade(runtime, building_type, 1);
 if(id==AI_NICOWAR_NO_BUILDING_TYPE) return;

			ManagementOrder* uro = new UpgradeRepair(id);
			runtime.add_management_order(uro);

			ManagementOrder* mo_assign=new AssignWorkers(strategy.upgrading_phase_1_units_assigned, id);
			mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, id));
			runtime.add_management_order(mo_assign);

			//Cause the building to be updated after its completion. Not all buildings need
			//to be updated, in which case the order will simply be ignored
			ManagementOrder* mo_completion=new SendMessage(FormattableString("update services %0").arg(id));
			mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			runtime.add_management_order(mo_completion);
		}
	}

	///Level two upgrades
	if(num_upgrading_level2 < num_to_upgrade_level2)
	{
		int building_type=choose_building_upgrade_type_level2(runtime);
		if(building_type!=AI_NICOWAR_NO_BUILDING_TYPE)
		{


			int id=choose_building_for_upgrade(runtime, building_type, 2);
 if(id==AI_NICOWAR_NO_BUILDING_TYPE) return;
			ManagementOrder* uro = new UpgradeRepair(id);
			runtime.add_management_order(uro);

			ManagementOrder* mo_assign=new AssignWorkers(strategy.upgrading_phase_2_units_assigned, id);
			mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, id));
			runtime.add_management_order(mo_assign);

			//Cause the building to be updated after its completion. Not all buildings need
			//to be updated, in which case the order will simply be ignored
			ManagementOrder* mo_completion=new SendMessage(FormattableString("update services %0").arg(id));
			mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			runtime.add_management_order(mo_completion);
		}
	}
}
