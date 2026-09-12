/*
  Unified, deterministic strategy configuration for Maxima.
*/

#ifndef AI_MAXIMA_STRATEGY_H
#define AI_MAXIMA_STRATEGY_H

#include <map>
#include <string>
#include <vector>

class GameHeader;

namespace AIMaxima
{

enum MatchFormat
{
	MatchFormatDuel,
	MatchFormatFfa3,
	MatchFormatFfa4,
	MatchFormatFfa5Plus,
	MatchFormat2v2
};

struct StrategyConfigOptions
{
	StrategyConfigOptions();
	std::string baseFile;
	std::vector<std::string> layerFiles;
	std::string inlineOverrides;
	std::string explicitFormat;
};

/// Every member below is immutable after resolution. Integer units are used so
/// strategy evaluation remains deterministic on every supported platform.
struct MaximaStrategy
{
	struct Model
	{
		int inn_capacity_level1;
		int inn_capacity_level2;
		int inn_capacity_level3;
	} model;

	struct Staffing
	{
		int control_window_samples;
		int control_low_permille;
		int control_high_permille;
		int control_slack;
		int control_minimum_workers;
		int control_maximum_workers;
		int control_cooldown_passes;
		int new_inn_workers;
		int new_swarm_workers;
		int swarm_supply_radius;
		int construction_inn_workers;
		int construction_swarm_workers;
		int construction_training_workers;
		int construction_hospital_workers;
		int construction_large_workers;
		int completed_tower_workers;
		int completed_tower_emergency_workers;
		int clearing_workers;
		int attack_clearing_workers;
	} staffing;

	struct Environment
	{
		int local_territory_radius;
		int terrain_food_offset;
		int terrain_food_span;
		int terrain_material_offset;
		int terrain_material_span;
		int terrain_space_offset;
		int terrain_space_span;
		int terrain_food_weight;
		int terrain_material_weight;
		int terrain_space_weight;
		int corn_resource_value;
		int fruit_resource_value;
		int wood_resource_value;
		int stone_resource_value;
		int algae_resource_value;
		int hungry_weight;
		int critical_food_weight;
		int unserved_food_weight;
		int food_security_base;
		int reserve_divisor;
		int food_headroom_default;
		int food_headroom_population_min;
		int food_headroom_base;
		int food_access_scale;
		int food_access_population_offset;
		int material_wood_weight;
		int material_stone_weight;
		int material_algae_weight;
		int resource_food_weight;
		int resource_material_weight;
		int resource_security_weight;
		int constrained_resource_penalty;
		int space_scale;
		int space_population_offset;
		int constrained_space_penalty;
		int construction_failure_space_penalty;
		int algae_mobility_penalty;
		int scarce_corn_threshold;
		int water_mobility_threshold;
		int scarce_corn_mobility_penalty;
		int topology_water_isolation_divisor;
		int topology_component_cap;
		int topology_component_weight;
		int live_mobility_baseline;
		int live_mobility_weight;
		int momentum_base;
		int momentum_population_weight;
		int momentum_food_weight;
		int momentum_failure_weight;
		int threat_colony_weight;
		int threat_explorer_weight;
		int threat_building_attack_weight;
		int threat_unit_attack_weight;
		int confidence_tick_divisor;
		int confidence_tile_divisor;
		int large_economy_population_min;
		int large_economy_terrain_min;
		int large_economy_food_min;
		int large_economy_connected_min;
		int large_economy_resource_min;
		int shoreline_density_scale;
		int mobility_outside_resource_weight;
		int mobility_opportunity_divisor;
		int mobility_shoreline_divisor;
		int abundance_resource_weight;
		int abundance_divisor;
		int smoothing_history_weight;
		int smoothing_total_weight;
		int fast_smoothing_history_weight;
		int fast_smoothing_total_weight;
	} environment;

	struct Trends
	{
		int observation_scale;
		int history_weight;
		int total_weight;
	} trends;

	struct Demands
	{
		int capacity_resource_weight;
		int capacity_divisor;
		int stranded_opportunity_divisor;
		int food_resource_divisor;
		int food_headroom_divisor;
		int food_pressure_weight;
		int survival_population_threshold;
		int survival_population_bonus;
		int growth_capacity_weight;
		int growth_divisor;
		int growth_food_divisor;
		int growth_threat_divisor;
		int growth_population_threshold;
		int growth_population_bonus;
		int growth_terrain_baseline;
		int growth_terrain_divisor;
		int expansion_divisor;
		int expansion_failure_penalty;
		int access_base;
		int access_mobility_divisor;
		int access_opportunity_divisor;
		int access_population_threshold;
		int access_population_bonus;
		int access_corn_threshold;
		int access_corn_bonus;
		int technology_base;
		int technology_population_divisor;
		int technology_scarcity_divisor;
		int technology_capacity_divisor;
		int technology_terrain_divisor;
		int technology_growth_divisor;
		int technology_survival_divisor;
		int mobility_capability_bonus;
		int mobility_survival_divisor;
		int military_base;
		int military_enemy_weight;
		int military_resource_divisor;
		int military_population_threshold;
		int military_population_bonus;
		int military_target_bonus;
		int military_food_divisor;
		int aggression_economy_divisor;
		int aggression_warrior_weight;
		int aggression_enemy_weight;
		int aggression_target_bonus;
	} demands;

	struct Economy
	{
		bool swarm_retirement_enabled;
		bool large_economy_adaptation_enabled;
		bool amphibious_network_maintenance_enabled;
		bool food_service_safeguards_enabled;
		bool worker_birth_throttle_enabled;
		int swarm_labor_scale_percent;
		int swarm_food_per_worker_percent;
		int swarm_pressure_sensitivity;
		int swarm_workers_per_building;
		int inn_population_offset;
		int inn_population_divisor;
		int food_headroom_warning;
		int food_headroom_critical;
		int school_population_min;
		int school_utility_min;
		int second_school_utility_min;
		int pool_population_min;
		int pool_utility_min;
		int racetrack_population_min;
		int racetrack_utility_min;
		int growth_site_utility_mid;
		int growth_site_utility_high;
		int reliable_inn_percent;
		int service_unserved_percent;
		int service_critical_percent;
		int service_combined_percent;
		int sustainable_inn_floor;
		int sustainable_inn_corn_divisor;
		int sustainable_inn_offset;
		int inn_target_floor;
		int inn_target_cap;
		int abundance_inn_target_cap;
		int abundance_inn_population_offset;
		int abundance_inn_population_divisor;
		int early_population_threshold;
		int early_worker_ratio;
		int worker_backlog_high;
		int worker_ratio_backlog_high;
		int worker_ratio_backlog_low;
		int worker_ratio_surplus;
		int abundance_access_population_min;
		int abundance_access_utility_floor;
		int amphibious_opportunity_min;
		int amphibious_population_min;
		int amphibious_utility_floor;
		int second_pool_population_min;
		int second_pool_utility_min;
		int first_pool_target;
		int second_pool_target;
		int abundance_pool_population_min;
		int abundance_pool_school_min;
		int explorer_cap;
		int explorer_floor;
		int explorer_population_divisor;
		int explorer_population_offset;
		int explorer_utility_divisor;
		int second_racetrack_population_min;
		int second_racetrack_utility_min;
		int first_school_target;
		int second_school_target;
		int first_racetrack_target;
		int second_racetrack_target;
		int worker_birth_stop_population_min;
		int worker_birth_stop_surplus_divisor;
		int pool_bid_utility_min;
		int school_bid_utility_min;
		int racetrack_bid_utility_min;
		int barracks_bid_utility_min;
		int offense_bid_utility_min;
		int connected_boom_baseline;
		int growth_mid_boom_divisor;
		int growth_barrier_divisor;
		int school_boom_divisor;
		int second_school_barrier_divisor;
		int pool_population_barrier_divisor;
		int pool_utility_floor;
		int pool_utility_barrier_divisor;
		int food_upgrade_utility_min;
		int food_upgrade_headroom_max;
		int explorer_ratio_high_utility;
		int explorer_ratio_low_utility;
		int explorer_ratio_high;
		int explorer_ratio_low;
		int offense_explorer_ratio_utility;
		int large_economy_explorer_ratio;
		int normal_offense_explorer_ratio;
	} economy;

	struct Upgrades
	{
		bool enabled;
		int level1_population_min;
		int level2_population_min;
		int level1_workers;
		int level2_workers;
		int level1_trained_units_per_slot;
		int level2_trained_units_per_slot;
		int level1_inn_base_weight;
		int level1_hospital_base_weight;
		int level1_racetrack_base_weight;
		int level1_pool_base_weight;
		int level1_barracks_base_weight;
		int level2_inn_base_weight;
		int level2_hospital_base_weight;
		int level2_racetrack_base_weight;
		int level2_pool_base_weight;
		int level2_barracks_base_weight;
		int first_prestige_trained_workers;
		int second_prestige_trained_workers;
		int second_prestige_population_min;
		int food_demand_divisor;
		int food_headroom_divisor;
		int threat_divisor;
		int technology_divisor;
		int mobility_divisor;
		int military_divisor;
		int recovery_inn_weight;
	} upgrades;

	struct Repairs
	{
		bool enabled;
	} repairs;

	struct Construction
	{
		int population_mid;
		int population_high;
		int sites_low;
		int sites_mid;
		int sites_high;
		int survival_utility_high;
		int growth_utility_high_sites;
		int growth_utility_mid_sites;
		int growth_utility_low_sites;
		int policy_utility_high;
		int policy_high_sites;
		int policy_low_sites;
		int emergency_min_sites;
		int emergency_max_sites;
		int defense_emergency_sites;
	} construction;

	struct Military
	{
		bool counterattack_enabled;
		bool explorer_defense_enabled;
		bool warrior_training_backlog_throttle_enabled;
		int defense_reserve_min;
		int campaign_population_base;
		int campaign_force_margin;
		int campaign_worker_floor_base;
		int attack_unit_cap;
		int tower_active_count;
		int tower_emergency_increment;
		int tower_bomb_increment;
		int defender_explorer_percent;
		int defender_explorer_cap;
		int defense_reserve_floor;
		int defense_enemy_percent;
		int offense_warrior_base_percent;
		int campaign_deployable_min;
		int reserve_enemy_bonus;
		int reserve_force_divisor;
		int endgame_enemy_count;
		int endgame_force_floor;
		int campaign_force_floor;
		int endgame_population_floor;
		int campaign_population_floor;
		int endgame_population_discount;
		int readiness_demand_divisor;
		int campaign_worker_floor;
		int campaign_food_percent;
		int campaign_worker_population_ratio;
		int campaign_sustainable_food_percent;
		int counterattack_force_margin;
		int defense_utility_floor_bonus;
		int explorer_defense_utility_bonus;
		int warrior_cap;
		int warrior_floor;
		int warrior_enemy_margin;
		int warrior_population_percent;
		int warrior_utility_divisor;
		int first_barracks_population_min;
		int second_barracks_population_min;
		int second_barracks_enemy_min;
		int third_barracks_backlog_min;
		int third_barracks_enemy_min;
		int emergency_barracks_threat_min;
		int hospital_warrior_min;
		int hospital_cap;
		int hospital_units_per_building;
		int bomb_attack_explorers_min;
		int bomb_colony_explorers_min;
		int offense_warrior_floor;
		int offense_explorer_floor;
		int offense_explorer_cap;
		int warrior_ratio_high_utility;
		int warrior_ratio_low_utility;
		int warrior_ratio_high;
		int warrior_ratio_low;
		int training_backlog_floor;
		int training_backlog_per_barracks;
		int defender_explorer_floor;
		int defender_explorer_ratio;
		int offense_utility_divisor;

		bool preemptive_defense_enabled;
		int preemptive_defense_min_trained_warriors;
		int preemptive_defense_warriors_per_zone;
		bool preemptive_defense_amphibious_enabled;
		int preemptive_defense_min_swimming_warriors;
		int preemptive_defense_inner_distance;
		int preemptive_defense_band_width;
		int preemptive_defense_path_slack;
		int preemptive_defense_choke_probe_radius;
		int preemptive_defense_max_cross_section;
		int preemptive_defense_zone_radius;
		int preemptive_defense_max_zones;
	} military;

	struct Postures
	{
		bool recover_enabled;
		bool defend_enabled;
		bool expand_enabled;
		bool develop_enabled;
		bool mobilize_enabled;
		bool campaign_enabled;
		bool finish_enabled;
		int recover_food_pressure_min;
		int recover_base;
		int recover_food_weight;
		int recover_inactive_utility;
		int defend_colony_weight;
		int defend_building_weight;
		int defend_unit_weight;
		int defend_attack_explorer_weight;
		int defend_colony_explorer_weight;
		int defend_emergency_bonus;
		int expand_base;
		int expand_population_cap;
		int expand_population_divisor;
		int expand_food_weight;
		int develop_population_min;
		int develop_base;
		int develop_population_cap;
		int develop_population_divisor;
		int develop_no_school_population_min;
		int develop_no_school_bonus;
		int inactive_utility;
		int mobilize_population_min;
		int mobilize_base;
		int desired_force_floor;
		int desired_force_margin;
		int mobilize_force_weight;
		int campaign_base;
		int campaign_warrior_weight;
		int campaign_target_divisor;
		int campaign_food_weight;
		int campaign_threat_weight;
		int finish_population_min;
		int finish_warrior_min;
		int finish_building_max;
		int finish_base;
		int finish_building_weight;
		int finish_food_weight;
		int finish_threat_weight;
		int campaign_active_bonus;
		int finish_active_bonus;
		int campaign_demand_weight;
		int emergency_commitment_ticks;
		int emergency_campaign_cooldown_ticks;
		int labor_pressure_divisor;
		int develop_worker_divisor;
		int develop_food_weight;
		int mobilize_food_weight;
		int campaign_inactive_utility;
		int finish_inactive_utility;
		int portfolio_expansion_divisor;
	} postures;

	struct Placement
	{
		bool food_preservation_enabled;
		bool defensive_siting_enabled;
		bool spacing_compactness_enabled;
		bool artery_routing_enabled;
		int unmet_demand_weight;
		int service_gain_weight;
		int capability_gain_weight;
		int parallelism_gain_weight;
		int redundancy_gain_weight;
		int role_location_weight;
		int defendedness_weight;
		int compactness_weight;
		int spacing_target_tiles;
		int spacing_weight;
		int farm_loss_weight;
		int food_zone_penalty_weight;
		int reserved_land_weight;
		int resource_scarcity_weight;
		int construction_labor_weight;
		int service_downtime_weight;
		int threat_exposure_weight;
		int artery_length_weight;
		int unmet_count_weight;
		int unmet_count_cap;
		int upgrade_unmet_demand;
		int service_gain_scale;
		int capability_gain_scale;
		int parallel_service_base;
		int parallel_build_bonus;
		int parallel_no_service;
		int duplicate_first_score;
		int duplicate_score_scale;
		int resource_distance_weight;
		int food_zone_radius;
		int inner_food_zone_multiplier;
		int hospital_food_zone_multiplier;
		int tower_food_zone_multiplier;
		int tower_critical_distance_weight;
		int tower_spacing_target;
		int tower_spacing_weight;
		int tower_threat_target;
		int tower_threat_weight;
		int labor_scale;
		int downtime_worker_scale;
		int artery_length_scale;
		int repair_base_demand;
		int action_timeout_ticks;
		int route_clearable_resource_cost;
		int route_farm_cost;
		int route_fertility_cost;
		int guard_area_protectedness;
		int baseline_protectedness;
		int enemy_threat_radius;
		int enemy_threat_base;
		int enemy_threat_falloff;
		int building_protection_radius;
		int building_protection_base;
		int building_protection_falloff;
	} placement;

	struct Colonization
	{
		bool enabled;
		int minimum_anchor_distance;
		int maximum_threat;
		int conquered_merge_radius;
		int conquered_memory_ticks;
		int minimum_new_food;
		int minimum_value;
	} colonization;

	struct ReactiveDefense
	{
		bool enabled;
		int flag_radius;
		int move_radius;
		int move_deadband;
		int unit_cap;
		int advantage_min;
		int advantage_percent;
	} reactive_defense;

	struct Tactics
	{
		bool enabled;
		bool siege_enabled;
		bool dig_out_enabled;
		bool failed_target_quarantine_enabled;
		bool siege_target_lock_enabled;
		int review_interval_ticks;
		int rally_flag_radius;
		int siege_flag_radius;
		int siege_min_force;
		int siege_muster_percent;
		int siege_strength_percent;
		int siege_casualty_percent;
		int siege_local_threat_radius;
		int siege_target_lock_ticks;
		int target_swarm_value;
		int target_food_value;
		int target_barracks_value;
		int target_school_value;
		int target_hospital_value;
		int target_default_value;
		int target_construction_bonus;
		int target_tower_penalty;
		int route_distance_weight;
		int uncertainty_percent;
		int failed_target_max_duration_ticks;
		int failed_target_quarantine_ticks;
	} tactics;

	struct Raiding
	{
		bool enabled;
		int population_min;
		int worker_min;
		int cluster_radius;
		int threat_radius;
		int force_bonus;
		int min_force;
		int max_force;
		int flag_radius;
		int muster_percent;
		int muster_timeout_ticks;
		int contact_ttl_ticks;
		int max_engagement_ticks;
		int casualty_percent;
		int survivor_min;
		int cooldown_ticks;
		int defender_min;
		int defender_percent;
		int building_buffer;
		int tower_buffer;
		int retarget_margin;
		int worker_weight;
		int harvesting_bonus;
		int carrying_bonus;
		int resource_weight;
		int defender_penalty;
		int route_distance_weight;
		int outskirts_weight;
		int allied_pressure_bonus;
		int ffa_third_party_penalty;
		int food_resource_value;
		int material_resource_value;
		int fruit_resource_value;
		int other_resource_value;
	} raiding;

	struct Teamplay
	{
		bool enabled;
		bool pressure_coordination_enabled;
		bool defense_enabled;
		int allied_pressure_radius;
		int defense_min_force;
		int defense_strength_percent;
		int defense_base_score;
		int defense_threat_weight;
		int defense_under_attack_bonus;
		int defense_unit_value;
		int defense_route_distance_weight;
		int defense_contact_ttl_ticks;
		int defense_max_engagement_ticks;
		int defense_cooldown_ticks;
		int defense_follow_radius;
		int defense_retarget_margin;
		int siege_player_pressure_bonus;
		int siege_building_pressure_bonus;
	} teamplay;

	struct ExplorerCampaign
	{
		bool enabled;
		int trained_min;
		int large_economy_trained_min;
		int multi_flag_trained_min;
		int max_flags;
		int units_per_flag;
	} explorer_campaign;

	struct Fruit
	{
		bool enabled;
		int population_min;
		int units_per_flag;
		int flag_radius;
	} fruit;

	struct Reconnaissance
	{
		bool enabled;
		bool scouting_missions_enabled;
		bool economic_watch_enabled;
		bool force_memory_enabled;
		int explorer_attack_warning_threshold;
		int explorer_colony_warning_threshold;
		int offense_explorer_population_divisor;
		int memory_horizon_ticks;
		int force_memory_hold_ticks;
		int force_sample_interval_ticks;
		int force_sample_phase_offset_ticks;
		int stale_contact_age_ticks;
		int mission_review_interval_ticks;
		int flag_radius_tiles;
		int economic_watch_population_min;
		int economic_watch_radius_min;
		int economic_watch_radius_max;
		int economic_watch_revisit_ticks;
		int economic_watch_max_missions;
		int economic_watch_patrol_sites;
		int swarm_building_value;
		int inn_building_value;
		int barracks_building_value;
		int school_building_value;
		int default_building_value;
		int building_count_weight;
		int unreachable_penalty;
		int finishing_bonus;
		int finishing_building_penalty;
		int saturation_percent;
		int retask_percent;
		int mission_population_divisor;
	} reconnaissance;

	struct Farming
	{
		bool enabled;
		bool farm_protection_enabled;
		bool maintenance_clearing_enabled;
		bool resource_preserving_circulation_enabled;
		bool wheat_invasion_clearing_enabled;
		bool wood_firebreak_enabled;
		bool proactive_clearing_enabled;
		int normal_interval_ticks;
		int urgent_interval_ticks;
		int management_radius;
		int wheat_fertility_min;
		int wood_fertility_base_percent;
		int wood_fertility_pressure_percent;
		int wood_firebreak_fertility_min_percent;
		int wood_firebreak_fertility_max_percent;
		int wood_pressure_base;
		int wood_pressure_space_divisor;
		int wood_pressure_supply_divisor;
		int wood_pressure_construction_divisor;
		int wood_pressure_growth_divisor;
		int economic_envelope_radius_tiles;
		int wood_supply_scale;
		int wood_supply_population_offset;
		int construction_failure_pressure;
		int proactive_workers_min;
		int proactive_cooldown_ticks;
		int proactive_duration_ticks;
		int proactive_quota;
		int proactive_failure_threshold;
		int proactive_building_threshold;
		int proactive_start_tick;
		int urgent_space_threshold;
	} farming;

	/// Food ledger: which inns and swarms are actually backed by protected farm
	/// capacity. Supply is equilibrium capacity, so only protected wheat cells
	/// that currently carry wheat count.
	struct Food
	{
		bool enabled;
		bool retirement_enabled;
		bool target_capping_enabled;
		int growth_period_ticks;
		int ticks_per_meal;
		int inn_demand_percent;
		int swarm_demand_percent;
		int placement_margin_percent;
		int quality_band_tiles;
		int unreachable_penalty_tiles;
		int inn_burden_coverage_percent;
		int swarm_burden_coverage_percent;
		int recovered_coverage_percent;
		int burden_confirm_ticks;
		int retirement_cooldown_ticks;
	} food;

	struct Scoring
	{
		int posture_switch_margin;
		int target_switch_margin;
		int target_reachable_weight;
		int target_warrior_weight;
		int target_distance_bias;
		int priority_food_headroom_divisor;
		int priority_abundance_swarm_bonus;
		int priority_mobility_divisor;
		int priority_pool_swimmer_threshold;
		int priority_pool_swimmer_bonus;
		int priority_pool_population_min;
		int priority_pool_count_max;
		int priority_pool_network_bonus;
		int priority_abundance_school_bonus;
		int priority_racetrack_penalty;
		int priority_threat_cap;
		int priority_threat_weight;
		int priority_healing_bonus;
		int priority_large_tower_bonus;
		int priority_tower_active_bonus;
		int priority_tower_emergency_bonus;
	} scoring;

	struct Scheduling
	{
		int normal_posture_commitment_ticks;
		int campaign_stall_ticks;
		int campaign_retreat_cooldown_ticks;
		int preemptive_defense_recompute_ticks;
		int strategy_interval_ticks;
		int strategy_phase_offset_ticks;
		int building_interval_ticks;
		int building_phase_offset_ticks;
		int defense_interval_ticks;
		int defense_phase_offset_ticks;
		int fruit_interval_ticks;
		int fruit_phase_offset_ticks;
		int explorer_attack_interval_ticks;
		int explorer_attack_phase_offset_ticks;
		int placement_interval_ticks;
		int explorer_warning_duration_ticks;
		int explorer_colony_warning_duration_ticks;
	} scheduling;

	struct Emergencies
	{
		bool food_enabled;
		bool colony_enabled;
		int food_unserved_percent;
		int food_critical_percent;
		int food_combined_percent;
		int population_trend_threshold;
		int food_trend_threshold;
		int colony_threat_threshold;
		int buildings_under_attack_threshold;
		int units_under_attack_threshold;
		int declining_units_under_attack_threshold;
		int declining_population_trend_threshold;
		int proportional_units_floor;
		int proportional_population_divisor;
		int proportional_population_trend_threshold;
	} emergencies;
};

enum StrategyParameterImpact
{
	StrategyImpactLow=1,
	StrategyImpactMedium=2,
	StrategyImpactHigh=3,
	StrategyImpactCritical=4
};

struct StrategyParameterInfo
{
	std::string key;
	std::string type;
	std::string unit;
	std::string group;
	std::string description;
	StrategyParameterImpact impact;
	int hardMinimum;
	int hardMaximum;
	int searchMinimum;
	int searchMaximum;
};

struct ResolvedStrategy
{
	MaximaStrategy values;
	MatchFormat format;
	std::map<std::string, std::string> provenance;
	std::vector<std::string> sources;
};

class StrategyResolver
{
public:
	static std::vector<StrategyParameterInfo> schema();
	static bool resolve(const StrategyConfigOptions& options,
		const GameHeader* gameHeader, ResolvedStrategy& result,
		std::string& error);
	static bool resolveForFormat(const StrategyConfigOptions& options,
		MatchFormat format, ResolvedStrategy& result, std::string& error);
	static bool parseFormat(const std::string& name, MatchFormat& format);
	static MatchFormat inferFormat(const GameHeader& gameHeader);
	static const char* formatName(MatchFormat format);
	static std::string schemaJson();
	static std::string resolvedJson(const ResolvedStrategy& strategy);
	static bool restoreValues(const std::string& text, MaximaStrategy& values, std::string& error);
	static std::string canonicalValues(const MaximaStrategy& strategy);
};

}

#endif
