// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "AINicowar.h"
#include "FormatableString.h"
#include <string>
#include "Utilities.h"
#include "Unit.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace boost::logic;



int NewNicowar::choose_building_upgrade_type_level1(Echo& echo)
{
	BuildingSearch schools(echo);
	schools.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools.add_condition(new BeingUpgradedTo(2));
	const int school_counts=schools.count_buildings();

	///Schools are only upgraded one at a time
	int school_chance = strategy.upgrading_phase_1_school_chance;
	if(school_counts>0)
		school_chance=0;

	return choose_building_upgrade_type(echo, 1, strategy.upgrading_phase_1_inn_chance, strategy.upgrading_phase_1_hospital_chance, strategy.upgrading_phase_1_racetrack_chance, strategy.upgrading_phase_1_swimmingpool_chance, strategy.upgrading_phase_1_barracks_chance, school_chance, strategy.upgrading_phase_1_tower_chance);
}



int NewNicowar::choose_building_upgrade_type_level2(Echo& echo)
{
	BuildingSearch schools_upgrading(echo);
	schools_upgrading.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools_upgrading.add_condition(new BeingUpgradedTo(3));
	const int school_counts_upgrading=schools_upgrading.count_buildings();

	BuildingSearch schools_lvl2(echo);
	schools_lvl2.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools_lvl2.add_condition(new BuildingLevel(2));
	schools_lvl2.add_condition(new NotUnderConstruction);
	const int school_counts_level2=schools_lvl2.count_buildings();

	BuildingSearch schools_lvl3(echo);
	schools_lvl3.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools_lvl3.add_condition(new BuildingLevel(3));
	schools_lvl3.add_condition(new NotUnderConstruction);
	const int school_counts_level3=schools_lvl3.count_buildings();

	///Schools are only upgraded one at a time
	int school_chance = strategy.upgrading_phase_2_school_chance;
	if(school_counts_upgrading>0 || (school_counts_level2 + school_counts_level3)<AI_NICOWAR_LVL2_SCHOOL_THRESHOLD)
		school_chance=0;

	return choose_building_upgrade_type(echo, 2, strategy.upgrading_phase_2_inn_chance, strategy.upgrading_phase_2_hospital_chance, strategy.upgrading_phase_2_racetrack_chance, strategy.upgrading_phase_2_swimmingpool_chance, strategy.upgrading_phase_2_barracks_chance, school_chance, strategy.upgrading_phase_2_tower_chance);
}


int NewNicowar::choose_building_upgrade_type(Echo& echo, int level, int inn_ratio, int hospital_ratio, int racetrack_ratio, int swimmingpool_ratio, int barracks_ratio, int school_ratio, int tower_ratio)
{
	///First count the types of buildings that are available to us for upgrading
	///you wouldn't want to choose a Barracks to be upgraded if there are none
	int building_count[IntBuildingType::NB_BUILDING];
	std::fill(building_count, building_count+IntBuildingType::NB_BUILDING, 0);

	BuildingSearch bs(echo);
	bs.add_condition(new NotUnderConstruction);
	bs.add_condition(new BuildingLevel(level));
	bs.add_condition(new Upgradable);
	for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
	{	
		building_count[echo.get_building_register().get_type(*i)]+=1;
	}

	///Next, add in the n slices for each of the buildings with respect to their ratio
	std::vector<int> buildings;
	buildings.reserve(100);
	if(building_count[IntBuildingType::FOOD_BUILDING] > 0)
	{
		for(int n=0; n<inn_ratio; ++n)
			buildings.push_back(IntBuildingType::FOOD_BUILDING);
	}
	if(building_count[IntBuildingType::HEAL_BUILDING] > 0)
	{
		for(int n=0; n<hospital_ratio; ++n)
			buildings.push_back(IntBuildingType::HEAL_BUILDING);
	}
	if(building_count[IntBuildingType::WALKSPEED_BUILDING] > 0)
	{
		for(int n=0; n<racetrack_ratio; ++n)
			buildings.push_back(IntBuildingType::WALKSPEED_BUILDING);
	}
	if(building_count[IntBuildingType::SWIMSPEED_BUILDING] > 0)
	{
		for(int n=0; n<swimmingpool_ratio; ++n)
			buildings.push_back(IntBuildingType::SWIMSPEED_BUILDING);
	}
	if(building_count[IntBuildingType::ATTACK_BUILDING] > 0)
	{
		for(int n=0; n<barracks_ratio; ++n)
			buildings.push_back(IntBuildingType::ATTACK_BUILDING);
	}
	if(building_count[IntBuildingType::SCIENCE_BUILDING] > 0)
	{
		for(int n=0; n<school_ratio; ++n)
			buildings.push_back(IntBuildingType::SCIENCE_BUILDING);
	}
	if(building_count[IntBuildingType::DEFENSE_BUILDING] > 0)
	{
		for(int n=0; n<tower_ratio; ++n)
			buildings.push_back(IntBuildingType::DEFENSE_BUILDING);
	}

	if(buildings.size()==0)
		return AI_NICOWAR_NO_BUILDING_TYPE;

	//Now choose a building, or return AI_NICOWAR_NO_BUILDING_TYPE for none available
	int random = syncRand() % buildings.size();

	return buildings[random];
}


int NewNicowar::choose_building_for_upgrade(Echo& echo, int type, int level)
{
	BuildingSearch bs(echo);
	bs.add_condition(new SpecificBuildingType(type));
	bs.add_condition(new NotUnderConstruction);
	bs.add_condition(new BuildingLevel(level));
	bs.add_condition(new Upgradable);
	std::vector<int> buildings;
	std::copy(bs.begin(), bs.end(), std::back_insert_iterator<std::vector<int> >(buildings));
	int random=syncRand() % buildings.size();
	int id=buildings[random];

	return id;
}


void NewNicowar::upgrade_buildings(Echo& echo)
{
	TeamStat* stat=echo.player->team->stats.getLatestStat();
	int can_upgrade_level1 = stat->upgradeState[BUILD][1] + stat->upgradeState[BUILD][2] + stat->upgradeState[BUILD][3];
	int can_upgrade_level2 = stat->upgradeState[BUILD][2] + stat->upgradeState[BUILD][3];

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

	BuildingSearch bs_lvl1(echo);
	bs_lvl1.add_condition(new BeingUpgradedTo(2));
	int num_upgrading_level1=bs_lvl1.count_buildings();

	BuildingSearch bs_lvl2(echo);
	bs_lvl2.add_condition(new BeingUpgradedTo(3));
	int num_upgrading_level2=bs_lvl2.count_buildings();

	///Level one upgrades
	if(num_upgrading_level1 < num_to_upgrade_level1)
	{
		int building_type=choose_building_upgrade_type_level1(echo);
		if(building_type!=AI_NICOWAR_NO_BUILDING_TYPE)
		{
			std::string type=IntBuildingType::typeFromShortNumber(building_type);

			int id=choose_building_for_upgrade(echo, building_type, 1);	

			ManagementOrder* uro = new UpgradeRepair(id);
			echo.add_management_order(uro);
			
			ManagementOrder* mo_assign=new AssignWorkers(strategy.upgrading_phase_1_units_assigned, id);
			mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, id));
			echo.add_management_order(mo_assign);

			//Cause the building to be updated after its completion. Not all buildings need
			//to be updated, in which case the order will simply be ignored
			ManagementOrder* mo_completion=new SendMessage(FormatableString("update %0 %1").arg(type).arg(id));
			mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_completion);
		}
	}

	///Level two upgrades
	if(num_upgrading_level2 < num_to_upgrade_level2)
	{
		int building_type=choose_building_upgrade_type_level2(echo);
		if(building_type!=AI_NICOWAR_NO_BUILDING_TYPE)
		{
			std::string type=IntBuildingType::typeFromShortNumber(building_type);

			int id=choose_building_for_upgrade(echo, building_type, 2);			
			ManagementOrder* uro = new UpgradeRepair(id);
			echo.add_management_order(uro);
			
			ManagementOrder* mo_assign=new AssignWorkers(strategy.upgrading_phase_2_units_assigned, id);
			mo_assign->add_condition(new ParticularBuilding(new UnderConstruction, id));
			echo.add_management_order(mo_assign);

			//Cause the building to be updated after its completion. Not all buildings need
			//to be updated, in which case the order will simply be ignored
			ManagementOrder* mo_completion=new SendMessage(FormatableString("update %0 %1").arg(type).arg(id));
			mo_completion->add_condition(new ParticularBuilding(new NotUnderConstruction, id));
			echo.add_management_order(mo_completion);
		}
	}
}

