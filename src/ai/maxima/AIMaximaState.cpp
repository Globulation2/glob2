// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

// Save/load of the resolved strategy, director, and pending execution work.
#include "AIMaximaContinuation.h"
#include "AIMaxima.h"
#include "Game.h"
#include "Player.h"
#include "Map.h"
#include "FormatableString.h"

#include <stdexcept>

using namespace AIMaximaRuntime;

namespace AIMaxima
{

namespace Recon
{
template<class A> void fields(A& a,MissionObjective& value)
{ a("targetTeam",value.targetTeam); a("frontier",value.frontier); a("economicWatch",value.economicWatch); a("x",value.x); a("y",value.y); a("score",value.score); }
}
template<class Archive> void Maxima::executionState(Archive& a)
{
	a("budget.construction_sites",budget.construction_sites);
	a("budget.desired_inns",budget.desired_inns);
	a("budget.desired_swarms",budget.desired_swarms);
	a("budget.desired_barracks",budget.desired_barracks);
	a("budget.desired_schools",budget.desired_schools);
	a("budget.desired_pools",budget.desired_pools);
	a("budget.desired_racetracks",budget.desired_racetracks);
	// Retain the historical serialized slot; live demand is recomputed below.
	a("budget.desired_hospitals",budget.desired_hospital_beds);
	a("budget.desired_towers",budget.desired_towers);
	a("budget.swarm_workers",budget.swarm_workers);
	a("budget.worker_ratio",budget.worker_ratio);
	a("budget.explorer_ratio",budget.explorer_ratio);
	a("budget.warrior_ratio",budget.warrior_ratio);
	a("budget.desired_explorers",budget.desired_explorers);
	a("budget.desired_warriors",budget.desired_warriors);
	a("budget.defense_reserve",budget.defense_reserve);
	a("budget.attack_flags",budget.attack_flags);
	a("budget.attack_units",budget.attack_units);
	a("budget.allow_upgrades",budget.allow_upgrades);
	a("budget.allow_level2_upgrades",budget.allow_level2_upgrades);
	a("budget.upgrade_level1_workers",budget.upgrade_level1_workers);
	a("budget.upgrade_level2_workers",budget.upgrade_level2_workers);
	a("budget.upgrade_level1_trained_units_per_slot",budget.upgrade_level1_trained_units_per_slot);
	a("budget.upgrade_level2_trained_units_per_slot",budget.upgrade_level2_trained_units_per_slot);
	a("budget.upgrade_level1_inn_weight",budget.upgrade_level1_inn_weight);
	a("budget.upgrade_level1_hospital_weight",budget.upgrade_level1_hospital_weight);
	a("budget.upgrade_level1_racetrack_weight",budget.upgrade_level1_racetrack_weight);
	a("budget.upgrade_level1_pool_weight",budget.upgrade_level1_pool_weight);
	a("budget.upgrade_level1_barracks_weight",budget.upgrade_level1_barracks_weight);
	a("budget.upgrade_level2_inn_weight",budget.upgrade_level2_inn_weight);
	a("budget.upgrade_level2_hospital_weight",budget.upgrade_level2_hospital_weight);
	a("budget.upgrade_level2_racetrack_weight",budget.upgrade_level2_racetrack_weight);
	a("budget.upgrade_level2_pool_weight",budget.upgrade_level2_pool_weight);
	a("budget.upgrade_level2_barracks_weight",budget.upgrade_level2_barracks_weight);
	a("budget.first_prestige_trained_workers",budget.first_prestige_trained_workers);
	a("budget.second_prestige_trained_workers",budget.second_prestige_trained_workers);
	a("budget.second_prestige_population_min",budget.second_prestige_population_min);


	a("budget.staffing_window_samples",budget.staffing_window_samples);
	a("budget.staffing_low_permille",budget.staffing_low_permille);
	a("budget.staffing_high_permille",budget.staffing_high_permille);
	a("budget.staffing_slack",budget.staffing_slack);
	a("budget.staffing_minimum_workers",budget.staffing_minimum_workers);
	a("budget.staffing_maximum_workers",budget.staffing_maximum_workers);
	a("budget.staffing_cooldown_passes",budget.staffing_cooldown_passes);
	// Each building's control loop is integral state: a save that dropped it
	// would restart every building from the minimum.
	a("staffing_control",staffing_control);
	a("budget.swarm_supply_radius",budget.swarm_supply_radius);
	a("budget.attack_clearing_workers",budget.attack_clearing_workers);
	a("budget.can_swim",budget.can_swim);
	a("budget.recovery_active",budget.recovery_active);
	a("budget.food_emergency",budget.food_emergency);
	a("budget.colony_emergency",budget.colony_emergency);
	a("budget.colony_swarm_requested",budget.colony_swarm_requested);
	a("budget.colony_swarm_priority",budget.colony_swarm_priority);
	a("budget.explorer_campaign_active",budget.explorer_campaign_active);
	a("budget.explorer_campaign_flags",budget.explorer_campaign_flags);
	a("budget.explorer_campaign_units_per_flag",budget.explorer_campaign_units_per_flag);
	a("budget.fruit_active",budget.fruit_active);
	a("budget.fruit_units_per_flag",budget.fruit_units_per_flag);
	a("budget.fruit_flag_radius",budget.fruit_flag_radius);
	a("budget.reactive_defense_enabled",budget.reactive_defense_enabled);
	a("budget.reactive_defense_flag_radius",budget.reactive_defense_flag_radius);
	a("budget.reactive_defense_move_radius",budget.reactive_defense_move_radius);
	a("budget.reactive_defense_move_deadband",budget.reactive_defense_move_deadband);
	a("budget.reactive_defense_unit_cap",budget.reactive_defense_unit_cap);
	a("budget.reactive_defense_advantage_min",budget.reactive_defense_advantage_min);
	a("budget.reactive_defense_advantage_percent",budget.reactive_defense_advantage_percent);
	a("budget.preemptive_defense_active",budget.preemptive_defense_active);
	a("budget.preemptive_amphibious_active",budget.preemptive_amphibious_active);
	a("budget.preemptive_effective_zone_max",budget.preemptive_effective_zone_max);
	a("budget.target_switch_margin",budget.target_switch_margin);
	a("budget.preemptive_recompute_ticks",budget.preemptive_recompute_ticks);
	a("budget.preemptive_inner_distance",budget.preemptive_inner_distance);
	a("budget.preemptive_band_width",budget.preemptive_band_width);
	a("budget.preemptive_path_slack",budget.preemptive_path_slack);
	a("budget.preemptive_probe_radius",budget.preemptive_probe_radius);
	a("budget.preemptive_cross_section_max",budget.preemptive_cross_section_max);
	a("budget.preemptive_zone_radius",budget.preemptive_zone_radius);
	a("budget.preemptive_zone_max",budget.preemptive_zone_max);
	a("budget.reconnaissance_suspended",budget.reconnaissance_suspended);
	a("budget.reconnaissance_flag_radius",budget.reconnaissance_flag_radius);
	a("budget.reconnaissance_review_interval",budget.reconnaissance_review_interval);
	a("budget.reconnaissance_economic_watch_revisit",budget.reconnaissance_economic_watch_revisit);
	a("budget.reconnaissance_objectives",budget.reconnaissance_objectives);
	a("budget.farming_normal_interval",budget.farming_normal_interval);
	a("budget.farming_urgent_interval",budget.farming_urgent_interval);
	a("budget.farming_enabled",budget.farming_enabled);
	a("budget.farming_protection_enabled",budget.farming_protection_enabled);
	a("budget.farming_maintenance_clearing_enabled",budget.farming_maintenance_clearing_enabled);
	a("budget.farming_resource_preserving_circulation_enabled",budget.farming_resource_preserving_circulation_enabled);
	a("budget.farming_wheat_invasion_clearing_enabled",budget.farming_wheat_invasion_clearing_enabled);
	a("budget.farming_wood_firebreak_enabled",budget.farming_wood_firebreak_enabled);
	a("budget.farming_proactive_clearing_enabled",budget.farming_proactive_clearing_enabled);
	a("budget.farming_urgent",budget.farming_urgent);
	a("budget.farming_wood_pressure",budget.farming_wood_pressure);
	a("budget.farming_minimum_wood_fertility",budget.farming_minimum_wood_fertility);
	a("budget.farming_allow_proactive_clearing",budget.farming_allow_proactive_clearing);
	a("budget.farming_clearing_for_placement",budget.farming_clearing_for_placement);
	a("budget.farming_min_workers_for_clearing",budget.farming_min_workers_for_clearing);
	a("budget.farming_clearing_cooldown",budget.farming_clearing_cooldown);
	a("budget.farming_clearing_duration",budget.farming_clearing_duration);
	a("budget.farming_clearing_quota",budget.farming_clearing_quota);
	a("budget.farming_management_radius",budget.farming_management_radius);
	a("budget.farming_wheat_fertility_min",budget.farming_wheat_fertility_min);
	a("budget.farming_wood_fertility_base_percent",budget.farming_wood_fertility_base_percent);
	a("budget.farming_wood_fertility_pressure_percent",budget.farming_wood_fertility_pressure_percent);
	a("budget.farming_wood_pressure_base",budget.farming_wood_pressure_base);
	a("budget.farming_wood_pressure_space_divisor",budget.farming_wood_pressure_space_divisor);
	a("budget.farming_wood_pressure_supply_divisor",budget.farming_wood_pressure_supply_divisor);
	a("budget.farming_wood_pressure_construction_divisor",budget.farming_wood_pressure_construction_divisor);
	a("budget.farming_wood_pressure_growth_divisor",budget.farming_wood_pressure_growth_divisor);
	a("budget.farming_economic_envelope_radius",budget.farming_economic_envelope_radius);
	a("budget.priority_inns",budget.priority_inns);
	a("budget.priority_swarms",budget.priority_swarms);
	a("budget.priority_barracks",budget.priority_barracks);
	a("budget.priority_schools",budget.priority_schools);
	a("budget.priority_pools",budget.priority_pools);
	a("budget.priority_racetracks",budget.priority_racetracks);
	a("budget.priority_hospitals",budget.priority_hospitals);
	a("budget.priority_towers",budget.priority_towers);
	a("budget.tactical_target_team",budget.tactical_target_team);
	a("budget.tactical_dig_out_team",budget.tactical_dig_out_team);
	a("budget.tactical_target_gid",budget.tactical_target_gid);
	a("budget.tactical_target_x",budget.tactical_target_x);
	a("budget.tactical_target_y",budget.tactical_target_y);
	a("budget.tactical_candidate_score",budget.tactical_candidate_score);
	a("budget.tactical_requested_force",budget.tactical_requested_force);
	a("budget.tactical_review_interval",budget.tactical_review_interval);
	a("budget.tactics_enabled",budget.tactics_enabled);
	a("budget.tactical_flag_level",budget.tactical_flag_level);
	a("budget.tactical_stall_ticks",budget.tactical_stall_ticks);
	a("budget.tactical_kind",budget.tactical_kind);
	a("director.initialized",director.initialized);
	a("director.dirty",director.dirty);
	a("director_initialized",director_initialized);
	a("known_algae_units",known_algae_units);
	a("walk_accessible_algae_units",walk_accessible_algae_units);
	a("swim_accessible_algae_units",swim_accessible_algae_units);
	a("accessible_algae_units",accessible_algae_units);
	a("development_planner_initialized",development_planner_initialized);
	a("development_reported_states",development_reported_states);

	a("food_burden_since",food_burden_since);
	a("food_retirement_issued",food_retirement_issued);
	a("last_food_retirement_tick",last_food_retirement_tick);
	a("food_supported_inns",food_supported_inns);
	a("food_supported_swarms",food_supported_swarms);
	a("food_ledger_valid",food_ledger_valid);

	a("relocation_since",relocation_since);
	a("relocation_target_building",relocation_target_building);
	a("relocation_target_since",relocation_target_since);
	a("relocation_completed_tick",relocation_completed_tick);
	a("last_food_relocation_tick",last_food_relocation_tick);
	a("relocation_destroy_issued",relocation_destroy_issued);
	a("operating_colonies",operating_colonies);
	a("last_preemptive_effective_zone_max",last_preemptive_effective_zone_max);
	a("last_preemptive_amphibious_active",last_preemptive_amphibious_active);
	a("applied_farm_protection_mask",applied_farm_protection_mask);
	a("applied_maintenance_clearing_mask",applied_maintenance_clearing_mask);
	a("maintenance_circulation_mask",maintenance_circulation_mask);
	a("wood_firebreak_mask",wood_firebreak_mask);
	a("farm_protection_mask",farm_protection_mask);
	a("wheat_farm_protection_mask",wheat_farm_protection_mask);
	a("last_farming_tick",last_farming_tick);
	a("farming_urgent",farming_urgent);
	a("land_clearing_pending",land_clearing_pending);
	a("maintenance_clearing_pending",maintenance_clearing_pending);
	a("development_cycle_pending",development_cycle_pending);
	a("preemptive_defense_pending",preemptive_defense_pending);
	a("reactive_defense_pending",reactive_defense_pending);
	a("environment.accessible_corn_fraction",environment.accessible_corn_fraction);
	a("offense_waves",offense_waves);
	a("labour_observation",labour_observation);
	a("labour_plan",labour_plan);
	a("swarm_allowance",swarm_allowance);
	a("force_beliefs",force_beliefs);
}
void Maxima::saveExecutionState(GAGCore::OutputStream* stream)
{
    stream->writeEnterSection("MaximaExecution");
    stream->writeText(StrategyResolver::canonicalValues(strategy),"strategy");
    AIMaximaContinuation::Writer archive(stream);
    executionState(archive);
    development_planner.saveExecutionState(stream);
    context.saveExecutionState(stream);
    stream->writeLeaveSection();
}
void Maxima::loadExecutionState(GAGCore::InputStream* stream, Sint32 versionMinor)
{
    stream->readEnterSection("MaximaExecution");
    const std::string savedStrategy=stream->readText("strategy");
    std::string error;
    if(!StrategyResolver::restoreValues(savedStrategy,strategy,error,versionMinor))
        throw std::runtime_error(error);
    context.player->game->gameHeader.setAIConfig(context.player->number, StrategyResolver::canonicalValues(strategy));
    // Reconfigure derived policies before restoring incremental work. Local
    // configuration may have changed since this game was saved.
    configure_development_planner();
    reconnaissance.configure(strategy.reconnaissance.memory_horizon_ticks,
        strategy.reconnaissance.force_memory_hold_ticks,
        strategy.reconnaissance.stale_contact_age_ticks,
        strategy.reconnaissance.force_memory_enabled);
    AIMaximaContinuation::Reader archive(stream);
    executionState(archive);
    if(offense_waves.size()>64)
        throw std::runtime_error("Too many saved Maxima offense waves");
    for(const auto& wave:offense_waves)
        if(wave.flagId<0 || wave.requestedForce<0 || wave.requestedForce>20
           || (wave.phase!=Tactics::WaveMuster && wave.phase!=Tactics::WaveAdvance))
            throw std::runtime_error("Invalid saved Maxima offense wave");
    development_planner.loadExecutionState(stream,versionMinor);
    context.loadExecutionState(stream, versionMinor);
    stream->readLeaveSection();

}

std::string Maxima::auditStrategyJson() const
{
    ResolvedStrategy actual;
    std::string error;
    StrategyResolver::resolveForPlayer(context.player->game->gameHeader,
        context.player->number, actual, error);
    actual.values=strategy;
    return StrategyResolver::resolvedJson(actual);
}

std::shared_ptr<Order> Maxima::getOrder()
{
	ensure_strategy();
	context.telemetry = telemetry;
	return context.getOrder(*this);
}


void Maxima::saveDirector(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("Director");
#define MAXIMA_SNAPSHOT_FIELDS(DO) DO(tick) DO(population) DO(workers) DO(free_warriors) DO(explorers) DO(trained_explorers) DO(warriors) DO(trained_workers) DO(trained_workers_level2) DO(trained_warriors) DO(swimming_workers) DO(swimming_explorers) DO(swimming_warriors) DO(amphibious_attack_explorers) DO(free_workers) DO(worker_jobs_open) DO(hungry) DO(critical_food) DO(unserved_food) DO(need_heal) DO(buildings) DO(building_sites) DO(swarms) DO(inns) DO(inn_level1) DO(inn_level2) DO(inn_level3) DO(barracks) DO(schools) DO(school_level1) DO(school_level2) DO(school_level3) DO(pools) DO(hospitals) DO(racetracks) DO(towers) DO(own_buildings_under_attack) DO(own_units_under_attack) DO(visible_enemy_warriors) DO(visible_enemy_explorers) DO(visible_enemy_attack_explorers) DO(visible_colony_explorer_threat) DO(visible_colony_threat) DO(alive_enemies) DO(total_hp) DO(attack_power) DO(prestige) DO(enemy_prestige) DO(tower_stone) DO(tower_bullets)
	stream->writeEnterSection("snapshot");
#define WRITE_SNAPSHOT(field) stream->writeSint32(snapshot.field,#field);
	MAXIMA_SNAPSHOT_FIELDS(WRITE_SNAPSHOT)
#undef WRITE_SNAPSHOT
	stream->writeLeaveSection();stream->writeEnterSection("previous_snapshot");
#define WRITE_PREVIOUS(field) stream->writeSint32(previous_snapshot.field,#field);
	MAXIMA_SNAPSHOT_FIELDS(WRITE_PREVIOUS)
#undef WRITE_PREVIOUS
	stream->writeLeaveSection();
	stream->writeEnterSection("trends");stream->writeSint32(trends.population,"population");stream->writeSint32(trends.workers,"workers");stream->writeSint32(trends.warriors,"warriors");stream->writeSint32(trends.food_pressure,"food_pressure");stream->writeSint32(trends.colony_pressure,"colony_pressure");stream->writeLeaveSection();
#define MAXIMA_ENV_FIELDS(DO) DO(known_tiles) DO(accessible_corn) DO(accessible_wood) DO(accessible_stone) DO(accessible_algae) DO(buildable_tiles) DO(water_tiles) DO(feeding_capacity) DO(food_headroom) DO(resource_capacity) DO(space_capacity) DO(food_security) DO(abundance) DO(terrain_abundance) DO(connected_abundance) DO(mobility_opportunity) DO(economic_momentum) DO(mobility_constraint) DO(topology_complexity) DO(threat_pressure) DO(confidence)
	stream->writeEnterSection("environment");
#define WRITE_ENV(field) stream->writeSint32(environment.field,#field);
	MAXIMA_ENV_FIELDS(WRITE_ENV)
#undef WRITE_ENV
	stream->writeLeaveSection();
	stream->writeEnterSection("demands");stream->writeSint32(demands.survival,"survival");stream->writeSint32(demands.food,"food");stream->writeSint32(demands.growth,"growth");stream->writeSint32(demands.expansion,"expansion");stream->writeSint32(demands.access,"access");stream->writeSint32(demands.technology,"technology");stream->writeSint32(demands.mobility,"mobility");stream->writeSint32(demands.military,"military");stream->writeSint32(demands.aggression,"aggression");stream->writeLeaveSection();
	stream->writeEnterSection("opponents");for(int i=0;i<Team::MAX_COUNT;++i){stream->writeEnterSection(i);const OpponentAssessment& o=opponents[i];stream->writeUint8(o.alive,"alive");stream->writeSint32(o.visible_warriors,"visible_warriors");stream->writeSint32(o.estimated_warriors,"estimated_warriors");stream->writeSint32(o.last_observed_warriors,"last_observed_warriors");stream->writeSint32(o.visible_explorers,"visible_explorers");stream->writeSint32(o.visible_buildings,"visible_buildings");stream->writeSint32(o.known_buildings,"known_buildings");stream->writeSint32(o.reachable_buildings,"reachable_buildings");stream->writeSint32(o.strategic_value,"strategic_value");stream->writeSint32(o.nearest_building,"nearest_building");stream->writeSint32(o.score,"score");stream->writeSint32(o.last_seen_tick,"last_seen_tick");stream->writeSint32(o.last_force_seen_tick,"last_force_seen_tick");stream->writeSint32(o.last_building_seen_tick,"last_building_seen_tick");stream->writeSint32(o.intel_confidence,"intel_confidence");stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeEnterSection("campaign");stream->writeSint32(campaign.state,"state");stream->writeSint32(campaign.target_team,"target_team");stream->writeSint32(campaign.started_tick,"started_tick");stream->writeSint32(campaign.last_progress_tick,"last_progress_tick");stream->writeSint32(campaign.last_target_buildings,"last_target_buildings");stream->writeSint32(campaign.buildings_destroyed,"buildings_destroyed");stream->writeSint32(campaign.cooldown_until,"cooldown_until");stream->writeLeaveSection();
	stream->writeEnterSection("Tactics");stream->writeSint32(tactical_mission.kind,"kind");stream->writeSint32(tactical_mission.phase,"phase");stream->writeSint32(tactical_mission.flagId,"flag_id");stream->writeSint32(tactical_mission.targetTeam,"target_team");stream->writeSint32(tactical_mission.targetGid,"target_gid");stream->writeSint32(tactical_mission.targetX,"target_x");stream->writeSint32(tactical_mission.targetY,"target_y");stream->writeSint32(tactical_mission.requestedForce,"requested_force");stream->writeSint32(tactical_mission.startedTick,"started_tick");stream->writeSint32(tactical_mission.phaseSinceTick,"phase_since_tick");stream->writeSint32(tactical_mission.lastProgressTick,"last_progress_tick");stream->writeSint32(tactical_mission.candidateScore,"candidate_score");stream->writeSint32(tactical_mission.lastTargetHp,"last_target_hp");stream->writeLeaveSection();
	stream->writeSint32(posture,"posture");stream->writeSint32(posture_since,"posture_since");for(int i=0;i<PostureCount;++i)stream->writeSint32(posture_utilities[i],FormattableString("posture_utility_%0").arg(i).c_str());
#define WRITE_SCALAR(field) stream->writeSint32(field,#field);
	WRITE_SCALAR(explorer_threat_until) WRITE_SCALAR(explorer_colony_threat_until) WRITE_SCALAR(global_water_percent) WRITE_SCALAR(global_shoreline_density) WRITE_SCALAR(global_land_components) WRITE_SCALAR(global_largest_land_percent) WRITE_SCALAR(global_start_land_percent) WRITE_SCALAR(global_chokepoint_density) WRITE_SCALAR(global_water_tiles) WRITE_SCALAR(global_grass_tiles) WRITE_SCALAR(global_land_tiles) WRITE_SCALAR(global_buildable_tiles) WRITE_SCALAR(global_corn_tiles) WRITE_SCALAR(global_wood_tiles) WRITE_SCALAR(global_stone_tiles) WRITE_SCALAR(global_algae_tiles) WRITE_SCALAR(global_fruit_tiles) WRITE_SCALAR(global_terrain_abundance) WRITE_SCALAR(global_connected_abundance) WRITE_SCALAR(global_mobility_opportunity) WRITE_SCALAR(recent_construction_failures) WRITE_SCALAR(last_construction_failure_tick) WRITE_SCALAR(proactive_clearing_flag) WRITE_SCALAR(proactive_clearing_started_tick) WRITE_SCALAR(proactive_clearing_initial_wood) WRITE_SCALAR(last_proactive_clearing_tick) WRITE_SCALAR(last_preemptive_defense_tick)
#undef WRITE_SCALAR
	stream->writeUint8(large_economy_committed,"large_economy_committed");stream->writeUint8(topology_initialized,"topology_initialized");stream->writeUint8(opening_space_constrained,"opening_space_constrained");stream->writeUint32(preemptive_building_signature,"preemptive_building_signature");
	auto writeIntMap=[stream](const char* name,const std::map<int,int>& values){stream->writeEnterSection(name);stream->writeUint32(values.size(),"size");size_t n=0;for(std::map<int,int>::const_iterator i=values.begin();i!=values.end();++i,++n){stream->writeEnterSection(n);stream->writeSint32(i->first,"key");stream->writeSint32(i->second,"value");stream->writeLeaveSection();}stream->writeLeaveSection();};
	writeIntMap("attack_flag_targets",attack_flag_targets);writeIntMap("attack_flag_started_ticks",attack_flag_started_ticks);writeIntMap("attack_target_quarantine_until",attack_target_quarantine_until);
	stream->writeEnterSection("attack_flag_end_reasons");stream->writeUint32(attack_flag_end_reasons.size(),"size");size_t n=0;for(std::map<int,std::string>::const_iterator i=attack_flag_end_reasons.begin();i!=attack_flag_end_reasons.end();++i,++n){stream->writeEnterSection(n);stream->writeSint32(i->first,"key");stream->writeText(i->second,"value");stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeEnterSection("ColonizationState");

	stream->writeSint32(last_colony_accounted_action_id,"last_accounted_action_id");

	stream->writeUint32(cleared_enemy_sites.size(),"hotspot_count");
	for(size_t site=0; site<cleared_enemy_sites.size(); ++site)
	{
		stream->writeEnterSection(site);
		stream->writeSint32(cleared_enemy_sites[site].x,"x");
		stream->writeSint32(cleared_enemy_sites[site].y,"y");
		stream->writeSint32(cleared_enemy_sites[site].confirmedTick,
			"confirmed_tick");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	const Recon::ReconReport& recon=reconnaissance.report();
	stream->writeEnterSection("Recon");
	stream->writeSint32(recon.tick,"tick");stream->writeSint32(recon.visibleWarriors,"visible_warriors");stream->writeSint32(recon.visibleExplorers,"visible_explorers");stream->writeSint32(recon.visibleAttackExplorers,"visible_attack_explorers");stream->writeSint32(recon.visibleColonyThreat,"visible_colony_threat");stream->writeSint32(recon.visibleColonyExplorerThreat,"visible_colony_explorer_threat");stream->writeSint32(recon.aliveEnemies,"alive_enemies");stream->writeSint32(recon.exploredPercent,"explored_percent");stream->writeSint32(recon.desiredMissions,"desired_missions");stream->writeSint32(last_recon_mission_tick,"last_mission_tick");stream->writeUint8(reconnaissance_suspended,"suspended");
	stream->writeEnterSection("opponents");stream->writeUint32(recon.opponents.size(),"size");n=0;
	for(std::map<int,Recon::OpponentIntel>::const_iterator opponent=recon.opponents.begin();opponent!=recon.opponents.end();++opponent,++n){stream->writeEnterSection(n);stream->writeSint32(opponent->first,"team");const Recon::OpponentIntel& intel=opponent->second;stream->writeUint8(intel.alive,"alive");stream->writeSint32(intel.visibleWarriors,"visible_warriors");stream->writeSint32(intel.visibleExplorers,"visible_explorers");stream->writeSint32(intel.visibleAttackExplorers,"visible_attack_explorers");stream->writeSint32(intel.visibleBuildings,"visible_buildings");stream->writeSint32(intel.lastObservedWarriors,"last_observed_warriors");stream->writeSint32(intel.lastObservedExplorers,"last_observed_explorers");stream->writeSint32(intel.estimatedWarriors,"estimated_warriors");stream->writeSint32(intel.estimatedExplorers,"estimated_explorers");stream->writeSint32(intel.knownBuildings,"known_buildings");stream->writeSint32(intel.strategicValue,"strategic_value");stream->writeSint32(intel.reachableBuildings,"reachable_buildings");stream->writeSint32(intel.nearestBuilding,"nearest_building");stream->writeSint32(intel.lastSeenTick,"last_seen_tick");stream->writeSint32(intel.lastForceSeenTick,"last_force_seen_tick");stream->writeSint32(intel.lastWarriorSeenTick,"last_warrior_seen_tick");stream->writeSint32(intel.lastExplorerSeenTick,"last_explorer_seen_tick");stream->writeSint32(intel.lastBuildingSeenTick,"last_building_seen_tick");stream->writeSint32(intel.lastEconomicSeenTick,"last_economic_seen_tick");stream->writeSint32(intel.lastEconomicX,"last_economic_x");stream->writeSint32(intel.lastEconomicY,"last_economic_y");stream->writeSint32(intel.confidence,"confidence");stream->writeEnterSection("buildings");stream->writeUint32(intel.buildings.size(),"size");size_t b=0;for(std::map<int,Recon::BuildingSighting>::const_iterator building=intel.buildings.begin();building!=intel.buildings.end();++building,++b){stream->writeEnterSection(b);const Recon::BuildingSighting& sighting=building->second;stream->writeSint32(sighting.gid,"gid");stream->writeSint32(sighting.team,"team");stream->writeSint32(sighting.type,"type");stream->writeSint32(sighting.x,"x");stream->writeSint32(sighting.y,"y");stream->writeSint32(sighting.width,"width");stream->writeSint32(sighting.height,"height");stream->writeUint8(sighting.construction,"construction");stream->writeSint32(sighting.lastSeenTick,"last_seen_tick");stream->writeUint8(sighting.currentlyVisible,"currently_visible");stream->writeLeaveSection();}stream->writeLeaveSection();stream->writeLeaveSection();}
	stream->writeLeaveSection();
	stream->writeEnterSection("missions");stream->writeUint32(recon.missions.size(),"size");for(n=0;n<recon.missions.size();++n){stream->writeEnterSection(n);const Recon::ReconMission& mission=recon.missions[n];stream->writeSint32(mission.flagId,"flag_id");stream->writeSint32(mission.targetTeam,"target_team");stream->writeUint8(mission.frontier,"frontier");stream->writeUint8(mission.economicWatch,"economic_watch");stream->writeSint32(mission.x,"x");stream->writeSint32(mission.y,"y");stream->writeSint32(mission.createdTick,"created_tick");stream->writeSint32(mission.lastRetaskTick,"last_retask_tick");stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeLeaveSection();
	stream->writeLeaveSection();
#undef MAXIMA_ENV_FIELDS
#undef MAXIMA_SNAPSHOT_FIELDS
}


bool Maxima::loadDirector(GAGCore::InputStream* stream,
	Sint32 versionMinor)
{
	stream->readEnterSection("Director");
#define MAXIMA_SNAPSHOT_FIELDS(DO) DO(tick) DO(population) DO(workers) DO(free_warriors) DO(explorers) DO(trained_explorers) DO(warriors) DO(trained_workers) DO(trained_workers_level2) DO(trained_warriors) DO(swimming_workers) DO(swimming_explorers) DO(swimming_warriors) DO(amphibious_attack_explorers) DO(free_workers) DO(worker_jobs_open) DO(hungry) DO(critical_food) DO(unserved_food) DO(need_heal) DO(buildings) DO(building_sites) DO(swarms) DO(inns) DO(inn_level1) DO(inn_level2) DO(inn_level3) DO(barracks) DO(schools) DO(school_level1) DO(school_level2) DO(school_level3) DO(pools) DO(hospitals) DO(racetracks) DO(towers) DO(own_buildings_under_attack) DO(own_units_under_attack) DO(visible_enemy_warriors) DO(visible_enemy_explorers) DO(visible_enemy_attack_explorers) DO(visible_colony_explorer_threat) DO(visible_colony_threat) DO(alive_enemies) DO(total_hp) DO(attack_power) DO(prestige) DO(enemy_prestige) DO(tower_stone) DO(tower_bullets)
	stream->readEnterSection("snapshot");
#define READ_SNAPSHOT(field) snapshot.field=stream->readSint32(#field);
	MAXIMA_SNAPSHOT_FIELDS(READ_SNAPSHOT)
#undef READ_SNAPSHOT
	stream->readLeaveSection();stream->readEnterSection("previous_snapshot");
#define READ_PREVIOUS(field) previous_snapshot.field=stream->readSint32(#field);
	MAXIMA_SNAPSHOT_FIELDS(READ_PREVIOUS)
#undef READ_PREVIOUS
	stream->readLeaveSection();

	stream->readEnterSection("trends");trends.population=stream->readSint32("population");trends.workers=stream->readSint32("workers");trends.warriors=stream->readSint32("warriors");trends.food_pressure=stream->readSint32("food_pressure");trends.colony_pressure=stream->readSint32("colony_pressure");stream->readLeaveSection();
#define MAXIMA_ENV_FIELDS(DO) DO(known_tiles) DO(accessible_corn) DO(accessible_wood) DO(accessible_stone) DO(accessible_algae) DO(buildable_tiles) DO(water_tiles) DO(feeding_capacity) DO(food_headroom) DO(resource_capacity) DO(space_capacity) DO(food_security) DO(abundance) DO(terrain_abundance) DO(connected_abundance) DO(mobility_opportunity) DO(economic_momentum) DO(mobility_constraint) DO(topology_complexity) DO(threat_pressure) DO(confidence)
	stream->readEnterSection("environment");
#define READ_ENV(field) environment.field=stream->readSint32(#field);
	MAXIMA_ENV_FIELDS(READ_ENV)
#undef READ_ENV
	stream->readLeaveSection();
	stream->readEnterSection("demands");demands.survival=stream->readSint32("survival");demands.food=stream->readSint32("food");demands.growth=stream->readSint32("growth");demands.expansion=stream->readSint32("expansion");demands.access=stream->readSint32("access");demands.technology=stream->readSint32("technology");demands.mobility=stream->readSint32("mobility");demands.military=stream->readSint32("military");demands.aggression=stream->readSint32("aggression");stream->readLeaveSection();
	stream->readEnterSection("opponents");for(int i=0;i<Team::MAX_COUNT;++i){stream->readEnterSection(i);OpponentAssessment& o=opponents[i];o.alive=stream->readUint8("alive");o.visible_warriors=stream->readSint32("visible_warriors");o.estimated_warriors=stream->readSint32("estimated_warriors");o.last_observed_warriors=stream->readSint32("last_observed_warriors");o.visible_explorers=stream->readSint32("visible_explorers");o.visible_buildings=stream->readSint32("visible_buildings");o.known_buildings=stream->readSint32("known_buildings");o.reachable_buildings=stream->readSint32("reachable_buildings");o.strategic_value=stream->readSint32("strategic_value");o.nearest_building=stream->readSint32("nearest_building");o.score=stream->readSint32("score");o.last_seen_tick=stream->readSint32("last_seen_tick");o.last_force_seen_tick=stream->readSint32("last_force_seen_tick");{o.last_building_seen_tick=stream->readSint32("last_building_seen_tick");o.intel_confidence=stream->readSint32("intel_confidence");}stream->readLeaveSection();}stream->readLeaveSection();
	stream->readEnterSection("campaign");campaign.state=static_cast<CampaignState>(stream->readSint32("state"));campaign.target_team=stream->readSint32("target_team");campaign.started_tick=stream->readSint32("started_tick");campaign.last_progress_tick=stream->readSint32("last_progress_tick");campaign.last_target_buildings=stream->readSint32("last_target_buildings");campaign.buildings_destroyed=stream->readSint32("buildings_destroyed");campaign.cooldown_until=stream->readSint32("cooldown_until");stream->readLeaveSection();
	{
		stream->readEnterSection("Tactics");
		tactical_mission.kind=static_cast<Tactics::MissionKind>(stream->readSint32("kind"));tactical_mission.phase=static_cast<Tactics::MissionPhase>(stream->readSint32("phase"));tactical_mission.flagId=stream->readSint32("flag_id");tactical_mission.targetTeam=stream->readSint32("target_team");tactical_mission.targetGid=stream->readSint32("target_gid");tactical_mission.targetX=stream->readSint32("target_x");tactical_mission.targetY=stream->readSint32("target_y");tactical_mission.requestedForce=stream->readSint32("requested_force");tactical_mission.startedTick=stream->readSint32("started_tick");tactical_mission.phaseSinceTick=stream->readSint32("phase_since_tick");tactical_mission.lastProgressTick=stream->readSint32("last_progress_tick");tactical_mission.candidateScore=stream->readSint32("candidate_score");tactical_mission.lastTargetHp=stream->readSint32("last_target_hp");
		stream->readLeaveSection();
	}
	posture=static_cast<StrategicPosture>(stream->readSint32("posture"));posture_since=stream->readSint32("posture_since");for(int i=0;i<PostureCount;++i)posture_utilities[i]=stream->readSint32(FormattableString("posture_utility_%0").arg(i).c_str());
#define READ_SCALAR(field) field=stream->readSint32(#field);
	READ_SCALAR(explorer_threat_until) READ_SCALAR(explorer_colony_threat_until) READ_SCALAR(global_water_percent) READ_SCALAR(global_shoreline_density) READ_SCALAR(global_land_components) READ_SCALAR(global_largest_land_percent) READ_SCALAR(global_start_land_percent) READ_SCALAR(global_chokepoint_density) READ_SCALAR(global_water_tiles) READ_SCALAR(global_grass_tiles) READ_SCALAR(global_land_tiles) READ_SCALAR(global_buildable_tiles) READ_SCALAR(global_corn_tiles) READ_SCALAR(global_wood_tiles) READ_SCALAR(global_stone_tiles) READ_SCALAR(global_algae_tiles) READ_SCALAR(global_fruit_tiles) READ_SCALAR(global_terrain_abundance) READ_SCALAR(global_connected_abundance) READ_SCALAR(global_mobility_opportunity) READ_SCALAR(recent_construction_failures) READ_SCALAR(last_construction_failure_tick) READ_SCALAR(proactive_clearing_flag) READ_SCALAR(proactive_clearing_started_tick) READ_SCALAR(proactive_clearing_initial_wood) READ_SCALAR(last_proactive_clearing_tick) READ_SCALAR(last_preemptive_defense_tick)
#undef READ_SCALAR

	large_economy_committed=stream->readUint8("large_economy_committed");topology_initialized=stream->readUint8("topology_initialized");opening_space_constrained=stream->readUint8("opening_space_constrained");preemptive_building_signature=stream->readUint32("preemptive_building_signature");
	auto readIntMap=[stream](const char* name,std::map<int,int>& values){values.clear();stream->readEnterSection(name);const Uint32 size=stream->readUint32("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);const int key=stream->readSint32("key");values[key]=stream->readSint32("value");stream->readLeaveSection();}stream->readLeaveSection();};
	readIntMap("attack_flag_targets",attack_flag_targets);readIntMap("attack_flag_started_ticks",attack_flag_started_ticks);readIntMap("attack_target_quarantine_until",attack_target_quarantine_until);
	attack_flag_end_reasons.clear();stream->readEnterSection("attack_flag_end_reasons");Uint32 size=stream->readUint32("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);const int key=stream->readSint32("key");attack_flag_end_reasons[key]=stream->readText("value");stream->readLeaveSection();}stream->readLeaveSection();
	{
		stream->readEnterSection("ColonizationState");

		last_colony_accounted_action_id=
			stream->readSint32("last_accounted_action_id");

		cleared_enemy_sites.clear();
		const Uint32 hotspotCount=stream->readUint32("hotspot_count");
		for(Uint32 site=0; site<hotspotCount; ++site)
		{
			stream->readEnterSection(site);
			ClearedEnemySite value;
			value.x=stream->readSint32("x");
			value.y=stream->readSint32("y");
			value.confirmedTick=stream->readSint32("confirmed_tick");
			cleared_enemy_sites.push_back(value);
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
	}
	{
	reconnaissance.reset();
	force_beliefs.clear();
	Recon::ReconReport& recon=reconnaissance.mutableReport();
		stream->readEnterSection("Recon");recon.tick=stream->readSint32("tick");recon.visibleWarriors=stream->readSint32("visible_warriors");recon.visibleExplorers=stream->readSint32("visible_explorers");recon.visibleAttackExplorers=stream->readSint32("visible_attack_explorers");recon.visibleColonyThreat=stream->readSint32("visible_colony_threat");recon.visibleColonyExplorerThreat=stream->readSint32("visible_colony_explorer_threat");recon.aliveEnemies=stream->readSint32("alive_enemies");recon.exploredPercent=stream->readSint32("explored_percent");recon.desiredMissions=stream->readSint32("desired_missions");last_recon_mission_tick=stream->readSint32("last_mission_tick");reconnaissance_suspended=stream->readUint8("suspended");
		stream->readEnterSection("opponents");size=stream->readUint32("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);const int team=stream->readSint32("team");Recon::OpponentIntel& intel=recon.opponents[team];intel.alive=stream->readUint8("alive");intel.visibleWarriors=stream->readSint32("visible_warriors");intel.visibleExplorers=stream->readSint32("visible_explorers");intel.visibleAttackExplorers=stream->readSint32("visible_attack_explorers");intel.visibleBuildings=stream->readSint32("visible_buildings");intel.lastObservedWarriors=stream->readSint32("last_observed_warriors");intel.lastObservedExplorers=stream->readSint32("last_observed_explorers");intel.estimatedWarriors=stream->readSint32("estimated_warriors");intel.estimatedExplorers=stream->readSint32("estimated_explorers");intel.knownBuildings=stream->readSint32("known_buildings");intel.strategicValue=stream->readSint32("strategic_value");intel.reachableBuildings=stream->readSint32("reachable_buildings");intel.nearestBuilding=stream->readSint32("nearest_building");intel.lastSeenTick=stream->readSint32("last_seen_tick");intel.lastForceSeenTick=stream->readSint32("last_force_seen_tick");intel.lastWarriorSeenTick=stream->readSint32("last_warrior_seen_tick");intel.lastExplorerSeenTick=stream->readSint32("last_explorer_seen_tick");intel.lastBuildingSeenTick=stream->readSint32("last_building_seen_tick");{intel.lastEconomicSeenTick=stream->readSint32("last_economic_seen_tick");intel.lastEconomicX=stream->readSint32("last_economic_x");intel.lastEconomicY=stream->readSint32("last_economic_y");}intel.confidence=stream->readSint32("confidence");stream->readEnterSection("buildings");const Uint32 building_count=stream->readUint32("size");for(Uint32 b=0;b<building_count;++b){stream->readEnterSection(b);Recon::BuildingSighting sighting;sighting.gid=stream->readSint32("gid");sighting.team=stream->readSint32("team");sighting.type=stream->readSint32("type");sighting.x=stream->readSint32("x");sighting.y=stream->readSint32("y");sighting.width=stream->readSint32("width");sighting.height=stream->readSint32("height");sighting.construction=stream->readUint8("construction");sighting.lastSeenTick=stream->readSint32("last_seen_tick");sighting.currentlyVisible=stream->readUint8("currently_visible");intel.buildings[sighting.gid]=sighting;stream->readLeaveSection();}stream->readLeaveSection();stream->readLeaveSection();}stream->readLeaveSection();
		stream->readEnterSection("missions");size=stream->readUint32("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);Recon::ReconMission mission;mission.flagId=stream->readSint32("flag_id");mission.targetTeam=stream->readSint32("target_team");mission.frontier=stream->readUint8("frontier");mission.economicWatch=stream->readUint8("economic_watch");mission.x=stream->readSint32("x");mission.y=stream->readSint32("y");mission.createdTick=stream->readSint32("created_tick");mission.lastRetaskTick=stream->readSint32("last_retask_tick");recon.missions.push_back(mission);stream->readLeaveSection();}stream->readLeaveSection();stream->readLeaveSection();
	}
	stream->readLeaveSection();
#undef MAXIMA_ENV_FIELDS
#undef MAXIMA_SNAPSHOT_FIELDS
	// All saved plans are derived. Preserve durable campaign and tactical
	// ownership, then force a fresh plan before any executor can run.
	director=StrategyDirector();
	return true;
}


bool Maxima::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	if(versionMinor<115)
		throw std::runtime_error("This Maxima save uses a retired strategy format");
	ensure_strategy();
	stream->readEnterSection("AIMaxima");
	context.load(stream, versionMinor);
	const bool loaded=loadState(stream, player, versionMinor);
	stream->readLeaveSection();
	return loaded;
}

bool Maxima::loadState(GAGCore::InputStream *stream, Player *player,
	Sint32 versionMinor)
{
	if(versionMinor<115)
		throw std::runtime_error("This Maxima save uses a retired strategy format");
	director=StrategyDirector();
	development_planner.reset();
	configure_development_planner();
	development_planner_initialized=true;
	development_reported_states.clear();
	environment=EnvironmentModel();
	demands=StrategicDemands();
	explorer_threat_until=-1000000;
	explorer_colony_threat_until=-1000000;
	large_economy_committed=false;
	topology_initialized=false;
	global_water_percent=0;
	global_shoreline_density=0;
	global_land_components=0;
	global_largest_land_percent=100;
	global_start_land_percent=100;
	global_chokepoint_density=0;
	global_water_tiles=0;
	global_grass_tiles=0;
	global_land_tiles=0;
	global_buildable_tiles=0;
	global_corn_tiles=0;
	global_wood_tiles=0;
	global_stone_tiles=0;
	global_algae_tiles=0;
	global_fruit_tiles=0;
	global_terrain_abundance=50;
	global_connected_abundance=50;
	global_mobility_opportunity=0;
	recent_construction_failures=0;
	last_construction_failure_tick=-1000000;
	opening_space_constrained=false;
	proactive_clearing_flag=-1;
	proactive_clearing_started_tick=-1000000;
	proactive_clearing_initial_wood=0;
	proactive_clearing_campaigns=0;
	last_proactive_clearing_tick=-1000000;
	attack_flag_targets.clear();
	attack_flag_started_ticks.clear();
	attack_flag_end_reasons.clear();
	attack_target_quarantine_until.clear();
	campaign=CampaignPlan();
	preemptive_guard_tiles.clear();
	last_preemptive_defense_tick=-1000000;
	preemptive_building_signature=0;
	last_preemptive_effective_zone_max=-1;
	last_preemptive_amphibious_active=false;
	applied_farm_protection_mask.clear();
	applied_maintenance_clearing_mask.clear();
	maintenance_circulation_mask.clear();
	wood_firebreak_mask.clear();
	farm_protection_mask.clear();
	wheat_farm_protection_mask.clear();
	farming_shoreline_mask.clear();
	farming_cardinal_shoreline_mask.clear();
	last_farming_tick=-1000000;
	farming_urgent=false;
	land_clearing_pending=false;
	maintenance_clearing_pending=false;
	development_cycle_pending=false;
	preemptive_defense_pending=false;
	reactive_defense_pending=false;
	reconnaissance.reset();
	force_beliefs.clear();
	last_recon_mission_tick=-1000000;
	reconnaissance_suspended=false;
	cleared_enemy_sites.clear();
	operating_colonies.clear();
	last_colony_completed_tick=-1000000;
	last_colony_accounted_action_id=0;
	established_colonies=0;
	colony_gate_reason="save default: immediately eligible";
	stream->readEnterSection("MaximaState");
	timer=stream->readUint32("timer");
		target = stream->readSint8("target");
		is_digging_out = stream->readUint8("is_digging_out");

		stream->readEnterSection("attack_flags");
		size_t size = stream->readUint16("size");
		for(size_t n = 0; n<size; ++n)
		{
			stream->readEnterSection(n);
			int flag = stream->readUint32("flag");
			attack_flags.push_back(flag);
			stream->readLeaveSection();
		}
		stream->readLeaveSection();

		stream->readEnterSection("defense_flags");
			size = stream->readUint16("size");
			for(size_t n = 0; n<size; ++n)
			{
				stream->readEnterSection(n);
				int flag = stream->readUint32("flag");
				defense_flags.push_back(flag);
				stream->readLeaveSection();
			}
			stream->readLeaveSection();

		stream->readEnterSection("explorer_attack_flags");
			size = stream->readUint16("size");
			for(size_t n = 0; n<size; ++n)
			{
				stream->readEnterSection(n);
				int flag = stream->readUint32("flag");
				explorer_attack_flags.push_back(flag);
				stream->readLeaveSection();
				}
				stream->readLeaveSection();

				stream->readEnterSection("preemptive_guard_tiles");
				size=stream->readUint32("size");
				const int map_size=player->map->getW()*player->map->getH();
				for(size_t n=0; n<size; ++n)
				{
					stream->readEnterSection(n);
					const int index=stream->readUint32("index");
					if(index>=0 && index<map_size)
						preemptive_guard_tiles.insert(index);
					stream->readLeaveSection();
				}
				stream->readLeaveSection();

	loadDirector(stream, versionMinor);
	development_planner.load(stream, versionMinor);
	// Restore pending work after initializing terrain caches.
	development_cycle_pending=true;
	if(player && player->map)
		initialize_farming_cache(context);
	loadExecutionState(stream, versionMinor);
	stream->readLeaveSection();
	return true;
}


void Maxima::save(GAGCore::OutputStream *stream)
{
	ensure_strategy();
	stream->writeEnterSection("AIMaxima");
	context.save(stream);
	stream->writeEnterSection("MaximaState");
	stream->writeUint32(timer, "timer");
	stream->writeUint8(target, "target");
	stream->writeUint8(is_digging_out, "is_digging_out");

	stream->writeEnterSection("attack_flags");
	stream->writeUint16(attack_flags.size(), "size");
	size_t n=0;
	for(n = 0; n<attack_flags.size(); ++n)
	{
		stream->writeEnterSection(n);
		stream->writeUint32(attack_flags[n], "flag");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("defense_flags");
	stream->writeUint16(defense_flags.size(), "size");
	for(n = 0; n<defense_flags.size(); ++n)
	{
		stream->writeEnterSection(n);
		stream->writeUint32(defense_flags[n], "flag");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("explorer_attack_flags");
	stream->writeUint16(explorer_attack_flags.size(), "size");
	for(n = 0; n<explorer_attack_flags.size(); ++n)
	{
		stream->writeEnterSection(n);
		stream->writeUint32(explorer_attack_flags[n], "flag");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("preemptive_guard_tiles");
	stream->writeUint32(preemptive_guard_tiles.size(), "size");
	n=0;
	for(std::set<int>::const_iterator tile=preemptive_guard_tiles.begin();
		tile!=preemptive_guard_tiles.end(); ++tile)
	{
		stream->writeEnterSection(n++);
		stream->writeUint32(*tile, "index");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	saveDirector(stream);
	development_planner.save(stream);
	saveExecutionState(stream);
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



}
