// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "AINicowar.h"
#include "GlobalContainer.h"
#include "FormatableString.h"
#include <string>
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


void NicowarStrategy::loadFromConfigFile(const ConfigBlock *configBlock)
{
	configBlock->load(growth_phase_unit_max, "growth_phase_unit_max");
	configBlock->load(skilled_work_phase_unit_min, "skilled_work_phase_unit_min");
	configBlock->load(upgrading_phase_1_school_min, "upgrading_phase_1_school_min");
	configBlock->load(upgrading_phase_1_unit_min, "upgrading_phase_1_unit_min");
	configBlock->load(upgrading_phase_1_trained_worker_min, "upgrading_phase_1_trained_worker_min");
	configBlock->load(upgrading_phase_2_school_min, "upgrading_phase_2_school_min");
	configBlock->load(upgrading_phase_2_unit_min, "upgrading_phase_2_unit_min");
	configBlock->load(upgrading_phase_2_trained_worker_min, "upgrading_phase_2_trained_worker_min");
	configBlock->load(minimum_warrior_level_for_trained, "minimum_warrior_level_for_trained");
	configBlock->load(war_preperation_phase_unit_min, "war_preperation_phase_unit_min");
	configBlock->load(war_preperation_phase_barracks_max, "war_preperation_phase_barracks_max");
	configBlock->load(war_preperation_phase_trained_warrior_max, "war_preperation_phase_trained_warrior_max");
	configBlock->load(war_phase_trained_warrior_min, "war_phase_trained_warrior_min");
	configBlock->load(fruit_phase_unit_min, "fruit_phase_unit_min");
	configBlock->load(starvation_recovery_phase_starving_no_inn_min_percent, "starvation_recovery_phase_starving_no_inn_min_percent");
	configBlock->load(starving_recovery_phase_unfed_per_new_inn, "starving_recovery_phase_unfed_per_new_inn");
	configBlock->load(no_workers_phase_free_worker_minimum_percent, "no_workers_phase_free_worker_minimum_percent");
	configBlock->load(level_1_inn_units_can_feed, "level_1_inn_units_can_feed");
	configBlock->load(level_2_inn_units_can_feed, "level_2_inn_units_can_feed");
	configBlock->load(level_3_inn_units_can_feed, "level_3_inn_units_can_feed");
	configBlock->load(growth_phase_units_per_swarm, "growth_phase_units_per_swarm");
	configBlock->load(non_growth_phase_units_per_swarm, "non_growth_phase_units_per_swarm");
	configBlock->load(growth_phase_maximum_swarms, "growth_phase_maximum_swarms");
	configBlock->load(skilled_work_phase_number_of_racetracks, "skilled_work_phase_number_of_racetracks");
	configBlock->load(skilled_work_phase_number_of_swimmingpools, "skilled_work_phase_number_of_swimmingpools");
	configBlock->load(skilled_work_phase_number_of_schools, "skilled_work_phase_number_of_schools");
	configBlock->load(war_preparation_phase_number_of_barracks, "war_preparation_phase_number_of_barracks");
	configBlock->load(base_number_of_hospitals, "base_number_of_hospitals");
	configBlock->load(war_preperation_phase_warriors_per_hospital, "war_preperation_phase_warriors_per_hospital");
	configBlock->load(base_number_of_construction_sites, "base_number_of_construction_sites");
	configBlock->load(starving_recovery_phase_number_of_extra_construction_sites, "starving_recovery_phase_number_of_extra_construction_sites");
	configBlock->load(level_1_inn_low_wheat_trigger_ammount, "level_1_inn_low_wheat_trigger_ammount");
	configBlock->load(level_2_inn_low_wheat_trigger_ammount, "level_2_inn_low_wheat_trigger_ammount");
	configBlock->load(level_3_inn_low_wheat_trigger_ammount, "level_3_inn_low_wheat_trigger_ammount");
	configBlock->load(level_1_inn_units_assigned_normal_wheat, "level_1_inn_units_assigned_normal_wheat");
	configBlock->load(level_2_inn_units_assigned_normal_wheat, "level_2_inn_units_assigned_normal_wheat");
	configBlock->load(level_3_inn_units_assigned_normal_wheat, "level_3_inn_units_assigned_normal_wheat");
	configBlock->load(level_1_inn_units_assigned_low_wheat, "level_1_inn_units_assigned_low_wheat");
	configBlock->load(level_2_inn_units_assigned_low_wheat, "level_2_inn_units_assigned_low_wheat");
	configBlock->load(level_3_inn_units_assigned_low_wheat, "level_3_inn_units_assigned_low_wheat");
	configBlock->load(base_swarm_units_assigned, "base_swarm_units_assigned");
	configBlock->load(base_swarm_low_wheat_trigger_ammount, "base_swarm_low_wheat_trigger_ammount");
	configBlock->load(base_swarm_hungry_reduce_trigger_percent, "base_swarm_hungry_reduce_trigger_percent");
	configBlock->load(growth_phase_swarm_worker_ratio, "growth_phase_swarm_worker_ratio");
	configBlock->load(non_growth_phase_swarm_worker_ratio, "non_growth_phase_swarm_worker_ratio");
	configBlock->load(base_number_of_explorers, "base_number_of_explorers");
	configBlock->load(fruit_phase_extra_number_of_explorers, "fruit_phase_extra_number_of_explorers");
	configBlock->load(base_swarm_explorer_ratio, "base_swarm_explorer_ratio");
	configBlock->load(war_preperation_swarm_warrior_ratio, "war_preperation_swarm_warrior_ratio");
	configBlock->load(defense_explorer_population_percent, "defense_explorer_population_percent");
	configBlock->load(offense_explorer_number, "offense_explorer_number");
	configBlock->load(offense_explorer_minimum, "offense_explorer_minimum");
	configBlock->load(offense_explorer_flag_number, "offense_explorer_flag_number");
	configBlock->load(offense_explorer_flag_assigned, "offense_explorer_flag_assigned");
	configBlock->load(upgrading_phase_1_inn_chance, "upgrading_phase_1_inn_chance");
	configBlock->load(upgrading_phase_1_hospital_chance, "upgrading_phase_1_hospital_chance");
	configBlock->load(upgrading_phase_1_racetrack_chance, "upgrading_phase_1_racetrack_chance");
	configBlock->load(upgrading_phase_1_swimmingpool_chance, "upgrading_phase_1_swimmingpool_chance");
	configBlock->load(upgrading_phase_1_barracks_chance, "upgrading_phase_1_barracks_chance");
	configBlock->load(upgrading_phase_1_school_chance, "upgrading_phase_1_school_chance");
	configBlock->load(upgrading_phase_1_tower_chance, "upgrading_phase_1_tower_chance");
	configBlock->load(upgrading_phase_2_inn_chance, "upgrading_phase_2_inn_chance");
	configBlock->load(upgrading_phase_2_hospital_chance, "upgrading_phase_2_hospital_chance");
	configBlock->load(upgrading_phase_2_racetrack_chance, "upgrading_phase_2_racetrack_chance");
	configBlock->load(upgrading_phase_2_swimmingpool_chance, "upgrading_phase_2_swimmingpool_chance");
	configBlock->load(upgrading_phase_2_barracks_chance, "upgrading_phase_2_barracks_chance");
	configBlock->load(upgrading_phase_2_school_chance, "upgrading_phase_2_school_chance");
	configBlock->load(upgrading_phase_2_tower_chance, "upgrading_phase_2_tower_chance");
	configBlock->load(upgrading_phase_1_units_assigned, "upgrading_phase_1_units_assigned");
	configBlock->load(upgrading_phase_2_units_assigned, "upgrading_phase_2_units_assigned");
	configBlock->load(upgrading_phase_1_num_units, "upgrading_phase_1_num_units");
	configBlock->load(upgrading_phase_2_num_units, "upgrading_phase_2_num_units");
	configBlock->load(war_phase_war_flag_units_assigned, "war_phase_war_flag_units_assigned");
	configBlock->load(war_phase_num_attack_flags, "war_phase_num_attack_flags");
	
}


std::string NicowarStrategy::getStrategyName()
{
	return name;
}
	

void NicowarStrategy::setStrategyName(const std::string& name)
{
	this->name=name;
}


NicowarStrategyLoader::NicowarStrategyLoader()
{
	ConfigVector<NicowarStrategy>::load("data/nicowar.default.txt", true);
	ConfigVector<NicowarStrategy>::load("data/nicowar.txt");
}


	
NicowarStrategy NicowarStrategyLoader::chooseRandomStrategy()
{
	int chosen = syncRand() % entries.size();
	entries[chosen]->setStrategyName(entriesToName[chosen]);
	return *entries[chosen];
}



NicowarStrategy NicowarStrategyLoader::getParticularStrategy(const std::string& name)
{
	entries[nameToEntries[name]]->setStrategyName(name);
	return *entries[nameToEntries[name]];
}


