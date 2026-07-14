// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "AINicowar.h"
#include "Utilities.h"
#include "Game.h"
#include "Unit.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace boost::logic;



void NewNicowar::check_phases(Echo& echo)
{
	TeamStat* stat=echo.player->team->stats.getLatestStat();

	///Qualifications for the growth phase:
	///1) Less than strategy.growth_phase_unit_max units
	if(stat->totalUnit<strategy.growth_phase_unit_max)
	{
		growth_phase=true;
	}
	else
	{
		growth_phase=false;
	}

	///Qualifications for the skilled work phase:
	///1) More than strategy.skilled_work_phase_unit_min units
	if(stat->totalUnit>=strategy.skilled_work_phase_unit_min)
	{
		skilled_work_phase=true;
	}
	else
	{
		skilled_work_phase=false;
	}

	///Qualifications for the upgrading phase 1:
	///1) Atleast strategy.upgrading_phase_1_school_min schools
	///2) Atleast strategy.upgrading_phase_1_unit_min units
	///3) Atleast strategy.upgrading_phase_1_trained_worker_min of them are trained for upgrading to level 2
	BuildingSearch schools(echo);
	schools.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools.add_condition(new NotUnderConstruction);
	const int school_counts=schools.count_buildings();
	const int trained_count=stat->upgradeState[BUILD][1] + stat->upgradeState[BUILD][2] + stat->upgradeState[BUILD][3];

	if(stat->totalUnit>=strategy.upgrading_phase_1_unit_min && school_counts>=strategy.upgrading_phase_1_school_min && trained_count>strategy.upgrading_phase_1_trained_worker_min)
	{
		upgrading_phase_1=true;
	}
	else
	{
		upgrading_phase_1=false;
	}

	///Qualifications for the upgrading phase 2:
	///1) Atleast strategy.upgrading_phase_2_school_min level 2 or level 3 schools
	///2) Atleast strategy.upgrading_phase_2_unit_min units
	///3) Atleast strategy.upgrading_phase_2_trained_worker_min of them are trained for upgrading to level 3
	BuildingSearch schools_2(echo);
	schools_2.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools_2.add_condition(new NotUnderConstruction);
	schools_2.add_condition(new BuildingLevel(2));
	BuildingSearch schools_3(echo);
	schools_3.add_condition(new SpecificBuildingType(IntBuildingType::SCIENCE_BUILDING));
	schools_3.add_condition(new NotUnderConstruction);
	schools_3.add_condition(new BuildingLevel(3));
	const int school_counts_2=schools_2.count_buildings() + schools_3.count_buildings();
	const int trained_count_2=echo.get_team_stats().upgradeState[BUILD][2] + stat->upgradeState[BUILD][3];

	if(stat->totalUnit>=strategy.upgrading_phase_2_unit_min && school_counts_2>=strategy.upgrading_phase_2_school_min && trained_count_2>strategy.upgrading_phase_2_trained_worker_min)
	{
		upgrading_phase_2=true;
	}
	else
	{
		upgrading_phase_2=false;
	}

	///Qualifications for the war preperation phase:
	///1) Atleast strategy.war_preperation_phase_unit_min units
	///2) Less than strategy.war_preperation_phase_barracks_max barracks OR
	///3) Less than strategy.war_preperation_phase_trained_warrior_max trained warriors
	BuildingSearch barracks(echo);
	barracks.add_condition(new SpecificBuildingType(IntBuildingType::ATTACK_BUILDING));
	int barracks_count=barracks.count_buildings();

	int warrior_count=0;
	for(int i=strategy.minimum_warrior_level_for_trained; i<=AI_NICOWAR_MAX_UPGRADE_LEVEL; ++i)
	{
		warrior_count += stat->upgradeState[ATTACK_SPEED][i];
	}

	if(stat->totalUnit>=strategy.war_preperation_phase_unit_min && (warrior_count < strategy.war_preperation_phase_trained_warrior_max || barracks_count<strategy.war_preperation_phase_barracks_max))
	{
		war_preperation=true;
	}
	else
	{
		war_preperation=false;
	}

	///Qualifications for the war phase:
	///Atleast strategy.war_phase_trained_warrior_min trained warriors
	if(warrior_count >= strategy.war_phase_trained_warrior_min)
	{
		war=true;
	}
	else
	{
		war=false;
	}

	///Qualifcations for the fruit phase:
	///Atleast strategy.fruit_phase_unit_min units, and fruits on the map
	if(echo.is_fruit_on_map() && stat->totalUnit >= strategy.fruit_phase_unit_min)
	{
		fruit_phase=true;
	}
	else
	{
		fruit_phase=false;
	}
	
	///Qualifications for the starving recovery phase:
	///1) More than strategy.starvation_recovery_phase_starving_no_inn_min_percent % units hungry but not able to eat
	///2) Atleast one unit (because of devision by 0)
	if(stat->totalUnit > AI_NICOWAR_STARVATION_MIN_UNITS)
	{
		int total_starving_percent = stat->needFoodNoInns * 100 / stat->totalUnit;
		if(total_starving_percent >= strategy.starvation_recovery_phase_starving_no_inn_min_percent)
		{
			starving_recovery=true;
		}
		else
		{
			starving_recovery=false;
		}
	}
	else
	{
		starving_recovery=false;
	}
	
	///Qualifications for the no worker phase:
	///1) More than strategy.no_workers_phase_free_worker_minimum_percen % workers free
	///2) No needed jobs
	///3) Atleast one worker (because of devision by 0)
	if(stat->numberUnitPerType[WORKER] > 0)
	{
		const int workers_free = (stat->isFree[WORKER]  -  stat->totalNeeded) * 100 / stat->numberUnitPerType[WORKER];
		if(workers_free > strategy.no_workers_phase_free_worker_minimum_percent)
		{
			no_workers_phase=true;
		}
		else
		{
			no_workers_phase=false;
		}
	}
	else
	{
		no_workers_phase=false;
	}
	
	///Qualifications for the can swim phase:
	///1) Atleast one worker that can swim
	int total_can_swim=0;
	for(int i=0; i<AI_NICOWAR_LEVEL_COUNT; ++i)
		total_can_swim += stat->upgradeStatePerType[WORKER][SWIM][i];
	if(total_can_swim>0)
	{
		can_swim=true;
	}
	else
	{
		can_swim=false;
	}
	
	///Qualifications for the defend explorers phase
	///1) Prestige, not counting this teams prestige, is more than 0, indicating that ground attacking explorers are being created
	if(echo.player->game->totalPrestige - echo.player->team->prestige > 0)
	{
		defend_explorers=true;
	}
	else
	{
		defend_explorers=false;
	}
	
	///Qualifications for the explorer attack preperation phase
	//1) This teams prestige greater than 0
	if(echo.player->team->prestige > 0)
	{
		explorer_attack_preperation_phase = true;
	}
	else
	{
		explorer_attack_preperation_phase = false;
	}
	
	///Qualifications for the explorer attack phase
	//1) The minimum number of trained explorers is greater than offense_explorer_minimum
	if(stat->upgradeStatePerType[EXPLORER][MAGIC_ATTACK_GROUND][AI_NICOWAR_EXPLORER_MAX_LEVEL] > strategy.offense_explorer_minimum)
	{
		explorer_attack_phase = true;
	}
	else
	{
		explorer_attack_phase = false;
	}
}

