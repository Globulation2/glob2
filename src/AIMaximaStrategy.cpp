#include "AIMaximaStrategy.h"

#include "BasePlayer.h"
#include "FileManager.h"
#include "GameHeader.h"
#include "Toolkit.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace AIMaxima
{

StrategyConfigOptions::StrategyConfigOptions()
	: baseFile("data/maxima/base.strategy")
{
}

namespace
{
	typedef MaximaStrategy Strategy;

	struct ParameterSpec
	{
		const char* key;
		const char* type;
		const char* unit;
		const char* group;
		const char* description;
		int minimum;
		int maximum;
		void (*set)(Strategy&, int);
		int (*get)(const Strategy&);
	};

#define INT_SPEC(section, member, key, low, high, unit, group, description) \
	{key, "integer", unit, group, description, low, high, \
		[](Strategy& value, int input) { value.section.member=input; }, \
		[](const Strategy& value) { return value.section.member; }}
#define BOOL_SPEC(section, member, key, unit, group, description) \
	{key, "boolean", unit, group, description, 0, 1, \
		[](Strategy& value, int input) { value.section.member=(input!=0); }, \
		[](const Strategy& value) { return value.section.member ? 1 : 0; }}

	const ParameterSpec parameterSpecs[] =
	{
		INT_SPEC(model, inn_capacity_level1, "model.inn_capacity_level1", 1, 1000, "units", "model", "Estimated level-one inn feeding capacity"),
		INT_SPEC(model, inn_capacity_level2, "model.inn_capacity_level2", 1, 1000, "units", "model", "Estimated level-two inn feeding capacity"),
		INT_SPEC(model, inn_capacity_level3, "model.inn_capacity_level3", 1, 1000, "units", "model", "Estimated level-three inn feeding capacity"),

		BOOL_SPEC(staffing, inn_adaptive_staffing_enabled, "staffing.inn_adaptive_staffing_enabled", "enabled", "staffing", "Enable corn-aware and emergency inn staffing adjustments"),
		INT_SPEC(staffing, inn_level1_low_corn_threshold, "staffing.inn_level1_low_corn_threshold", 0, 1000, "resource_units", "staffing", "Level-one inn low-corn trigger"),
		INT_SPEC(staffing, inn_level2_low_corn_threshold, "staffing.inn_level2_low_corn_threshold", 0, 1000, "resource_units", "staffing", "Level-two inn low-corn trigger"),
		INT_SPEC(staffing, inn_level3_low_corn_threshold, "staffing.inn_level3_low_corn_threshold", 0, 1000, "resource_units", "staffing", "Level-three inn low-corn trigger"),
		INT_SPEC(staffing, inn_level1_normal_workers, "staffing.inn_level1_normal_workers", 0, 100, "workers", "staffing", "Level-one inn normal worker assignment"),
		INT_SPEC(staffing, inn_level2_normal_workers, "staffing.inn_level2_normal_workers", 0, 100, "workers", "staffing", "Level-two inn normal worker assignment"),
		INT_SPEC(staffing, inn_level3_normal_workers, "staffing.inn_level3_normal_workers", 0, 100, "workers", "staffing", "Level-three inn normal worker assignment"),
		INT_SPEC(staffing, inn_level1_low_corn_workers, "staffing.inn_level1_low_corn_workers", 0, 100, "workers", "staffing", "Level-one inn low-corn worker assignment"),
		INT_SPEC(staffing, inn_level2_low_corn_workers, "staffing.inn_level2_low_corn_workers", 0, 100, "workers", "staffing", "Level-two inn low-corn worker assignment"),
		INT_SPEC(staffing, inn_level3_low_corn_workers, "staffing.inn_level3_low_corn_workers", 0, 100, "workers", "staffing", "Level-three inn low-corn worker assignment"),
		INT_SPEC(staffing, resource_tracker_samples, "staffing.resource_tracker_samples", 1, 1000, "samples", "staffing", "Resource-tracker window used by inn and swarm staffing decisions"),
		INT_SPEC(staffing, swarm_supply_radius, "staffing.swarm_supply_radius", 1, 64, "tiles", "staffing", "Maximum harvesting-route distance used to weight nearby fertile corn for swarm staffing"),
		INT_SPEC(staffing, construction_inn_workers, "staffing.construction_inn_workers", 0, 32, "workers", "staffing", "Workers assigned to a new inn construction site"),
		INT_SPEC(staffing, construction_swarm_workers, "staffing.construction_swarm_workers", 0, 32, "workers", "staffing", "Workers assigned to a new swarm construction site"),
		INT_SPEC(staffing, construction_training_workers, "staffing.construction_training_workers", 0, 32, "workers", "staffing", "Workers assigned to a new school or barracks construction site"),
		INT_SPEC(staffing, construction_hospital_workers, "staffing.construction_hospital_workers", 0, 32, "workers", "staffing", "Workers assigned to a new hospital construction site"),
		INT_SPEC(staffing, construction_large_workers, "staffing.construction_large_workers", 0, 32, "workers", "staffing", "Workers assigned to a new pool, racetrack, or tower construction site"),
		INT_SPEC(staffing, completed_tower_workers, "staffing.completed_tower_workers", 0, 32, "workers", "staffing", "Workers retained in a completed defensive tower"),
		INT_SPEC(staffing, completed_tower_emergency_workers, "staffing.completed_tower_emergency_workers", 0, 32, "workers", "staffing", "Workers retained in a tower during explorer emergency"),
		INT_SPEC(staffing, clearing_workers, "staffing.clearing_workers", 0, 32, "workers", "staffing", "Workers assigned to land-clearing flags"),
		INT_SPEC(staffing, attack_clearing_workers, "staffing.attack_clearing_workers", 0, 32, "workers", "staffing", "Workers assigned to clearing flags created along an attack route"),

		INT_SPEC(environment, local_territory_radius, "environment.local_territory_radius", 1, 128, "tiles", "environment", "Radius around colony buildings included in live resource observations"),
		INT_SPEC(environment, terrain_food_offset, "environment.terrain_food_offset", 0, 1000, "resource_units", "environment", "Per-player food removed before terrain abundance begins rising"),
		INT_SPEC(environment, terrain_food_span, "environment.terrain_food_span", 1, 5000, "resource_units", "environment", "Per-player food span mapped onto the abundance score"),
		INT_SPEC(environment, terrain_material_offset, "environment.terrain_material_offset", 0, 2000, "resource_units", "environment", "Per-player material removed before terrain abundance begins rising"),
		INT_SPEC(environment, terrain_material_span, "environment.terrain_material_span", 1, 10000, "resource_units", "environment", "Per-player material span mapped onto the abundance score"),
		INT_SPEC(environment, terrain_space_offset, "environment.terrain_space_offset", 0, 5000, "tiles", "environment", "Per-player buildable land removed before space abundance begins rising"),
		INT_SPEC(environment, terrain_space_span, "environment.terrain_space_span", 1, 20000, "tiles", "environment", "Per-player buildable-land span mapped onto the abundance score"),
		INT_SPEC(environment, terrain_food_weight, "environment.terrain_food_weight", 0, 100, "weight", "environment", "Food contribution to terrain abundance"),
		INT_SPEC(environment, terrain_material_weight, "environment.terrain_material_weight", 0, 100, "weight", "environment", "Material contribution to terrain abundance"),
		INT_SPEC(environment, terrain_space_weight, "environment.terrain_space_weight", 0, 100, "weight", "environment", "Buildable-space contribution to terrain abundance"),
		INT_SPEC(environment, corn_resource_value, "environment.corn_resource_value", 0, 32, "value", "environment", "Mobility-opportunity value of a corn tile"),
		INT_SPEC(environment, fruit_resource_value, "environment.fruit_resource_value", 0, 32, "value", "environment", "Mobility-opportunity value of a fruit tile"),
		INT_SPEC(environment, wood_resource_value, "environment.wood_resource_value", 0, 32, "value", "environment", "Mobility-opportunity value of a wood tile"),
		INT_SPEC(environment, stone_resource_value, "environment.stone_resource_value", 0, 32, "value", "environment", "Mobility-opportunity value of a stone tile"),
		INT_SPEC(environment, algae_resource_value, "environment.algae_resource_value", 0, 32, "value", "environment", "Mobility-opportunity value of an algae tile"),
		INT_SPEC(environment, hungry_weight, "environment.hungry_weight", 0, 500, "weight", "environment", "Food-stress weight for hungry units"),
		INT_SPEC(environment, critical_food_weight, "environment.critical_food_weight", 0, 500, "weight", "environment", "Food-stress weight for critically hungry units"),
		INT_SPEC(environment, unserved_food_weight, "environment.unserved_food_weight", 0, 500, "weight", "environment", "Food-stress weight for units unable to obtain food service"),
		INT_SPEC(environment, food_security_base, "environment.food_security_base", 0, 150, "score", "environment", "Food-security score before reserve and stress adjustments"),
		INT_SPEC(environment, reserve_divisor, "environment.reserve_divisor", 1, 20, "score", "environment", "Divisor controlling stored-food contribution to security"),
		INT_SPEC(environment, food_headroom_default, "environment.food_headroom_default", 0, 100, "score", "environment", "Food-headroom estimate before the colony is measurable"),
		INT_SPEC(environment, food_headroom_population_min, "environment.food_headroom_population_min", 0, 100, "units", "environment", "Population at which live inn headroom replaces the default"),
		INT_SPEC(environment, food_headroom_base, "environment.food_headroom_base", 0, 100, "score", "environment", "Base live food-headroom score"),
		INT_SPEC(environment, food_access_scale, "environment.food_access_scale", 1, 2000, "weight", "environment", "Corn-access contribution scale"),
		INT_SPEC(environment, food_access_population_offset, "environment.food_access_population_offset", 0, 500, "units", "environment", "Population offset damping corn access in small colonies"),
		INT_SPEC(environment, material_wood_weight, "environment.material_wood_weight", 0, 50, "weight", "environment", "Wood contribution to material access"),
		INT_SPEC(environment, material_stone_weight, "environment.material_stone_weight", 0, 50, "weight", "environment", "Stone contribution to material access"),
		INT_SPEC(environment, material_algae_weight, "environment.material_algae_weight", 0, 50, "weight", "environment", "Algae contribution to material access"),
		INT_SPEC(environment, resource_food_weight, "environment.resource_food_weight", 0, 100, "weight", "environment", "Food-access weight in resource capacity"),
		INT_SPEC(environment, resource_material_weight, "environment.resource_material_weight", 0, 100, "weight", "environment", "Material-access weight in resource capacity"),
		INT_SPEC(environment, resource_security_weight, "environment.resource_security_weight", 0, 100, "weight", "environment", "Food-security weight in resource capacity"),
		INT_SPEC(environment, constrained_resource_penalty, "environment.constrained_resource_penalty", 0, 100, "score", "environment", "Resource-capacity penalty for a space-constrained opening"),
		INT_SPEC(environment, space_scale, "environment.space_scale", 1, 1000, "weight", "environment", "Buildable-tile contribution scale for space capacity"),
		INT_SPEC(environment, space_population_offset, "environment.space_population_offset", 0, 1000, "units", "environment", "Population offset damping live space capacity"),
		INT_SPEC(environment, constrained_space_penalty, "environment.constrained_space_penalty", 0, 100, "score", "environment", "Space-capacity penalty for a constrained opening"),
		INT_SPEC(environment, construction_failure_space_penalty, "environment.construction_failure_space_penalty", 0, 50, "score", "environment", "Space-capacity penalty per recent placement failure"),
		INT_SPEC(environment, algae_mobility_penalty, "environment.algae_mobility_penalty", 0, 100, "score", "environment", "Mobility pressure when algae exists but swimming is unavailable"),
		INT_SPEC(environment, scarce_corn_threshold, "environment.scarce_corn_threshold", 0, 100, "tiles", "environment", "Accessible-corn threshold used to detect water-bound food scarcity"),
		INT_SPEC(environment, water_mobility_threshold, "environment.water_mobility_threshold", 0, 100, "percent", "environment", "Water share required for the scarce-corn mobility penalty"),
		INT_SPEC(environment, scarce_corn_mobility_penalty, "environment.scarce_corn_mobility_penalty", 0, 100, "score", "environment", "Mobility pressure from low corn on a water-heavy footprint"),
		INT_SPEC(environment, topology_water_isolation_divisor, "environment.topology_water_isolation_divisor", 1, 100, "score", "environment", "Damps the interaction between water coverage and land isolation"),
		INT_SPEC(environment, topology_component_cap, "environment.topology_component_cap", 0, 100, "score", "environment", "Maximum fragmentation contribution to topology complexity"),
		INT_SPEC(environment, topology_component_weight, "environment.topology_component_weight", 0, 50, "weight", "environment", "Complexity added by each additional land component"),
		INT_SPEC(environment, live_mobility_baseline, "environment.live_mobility_baseline", 0, 100, "score", "environment", "Mobility constraint ignored before live complexity rises"),
		INT_SPEC(environment, live_mobility_weight, "environment.live_mobility_weight", 0, 20, "weight", "environment", "Live mobility contribution to topology complexity"),
		INT_SPEC(environment, momentum_base, "environment.momentum_base", 0, 100, "score", "environment", "Neutral economic-momentum baseline"),
		INT_SPEC(environment, momentum_population_weight, "environment.momentum_population_weight", 0, 20, "weight", "environment", "Population trend contribution to momentum"),
		INT_SPEC(environment, momentum_food_weight, "environment.momentum_food_weight", 0, 20, "weight", "environment", "Food-pressure trend penalty to momentum"),
		INT_SPEC(environment, momentum_failure_weight, "environment.momentum_failure_weight", 0, 50, "weight", "environment", "Placement-failure penalty to momentum"),
		INT_SPEC(environment, threat_colony_weight, "environment.threat_colony_weight", 0, 100, "weight", "environment", "Visible colony-threat contribution to threat pressure"),
		INT_SPEC(environment, threat_explorer_weight, "environment.threat_explorer_weight", 0, 100, "weight", "environment", "Explorer-bomb contribution to threat pressure"),
		INT_SPEC(environment, threat_building_attack_weight, "environment.threat_building_attack_weight", 0, 100, "weight", "environment", "Attacked-building contribution to threat pressure"),
		INT_SPEC(environment, threat_unit_attack_weight, "environment.threat_unit_attack_weight", 0, 100, "weight", "environment", "Attacked-unit contribution to threat pressure"),
		INT_SPEC(environment, confidence_tick_divisor, "environment.confidence_tick_divisor", 1, 1000, "ticks", "environment", "Ticks contributing one point of environment confidence"),
		INT_SPEC(environment, confidence_tile_divisor, "environment.confidence_tile_divisor", 1, 100, "tiles", "environment", "Known tiles contributing one point of environment confidence"),
		INT_SPEC(environment, large_economy_population_min, "environment.large_economy_population_min", 0, 1000, "units", "environment", "Population required to permanently commit to a large-economy policy"),
		INT_SPEC(environment, large_economy_terrain_min, "environment.large_economy_terrain_min", 0, 100, "score", "environment", "Terrain abundance required for large-economy commitment"),
		INT_SPEC(environment, large_economy_food_min, "environment.large_economy_food_min", 0, 100, "score", "environment", "Food security required for large-economy commitment"),
		INT_SPEC(environment, large_economy_connected_min, "environment.large_economy_connected_min", 0, 100, "score", "environment", "Connected abundance sufficient for large-economy commitment"),
		INT_SPEC(environment, large_economy_resource_min, "environment.large_economy_resource_min", 0, 100, "score", "environment", "Resource capacity sufficient after swimming for large-economy commitment"),
		INT_SPEC(environment, shoreline_density_scale, "environment.shoreline_density_scale", 1, 100, "scale", "environment", "Scale mapping shoreline transitions onto the topology score"),
		INT_SPEC(environment, mobility_outside_resource_weight, "environment.mobility_outside_resource_weight", 0, 20, "weight", "environment", "Outside-resource contribution to mobility opportunity"),
		INT_SPEC(environment, mobility_opportunity_divisor, "environment.mobility_opportunity_divisor", 1, 20, "weight", "environment", "Normalizer for stranded mobility opportunity inputs"),
		INT_SPEC(environment, mobility_shoreline_divisor, "environment.mobility_shoreline_divisor", 1, 20, "score", "environment", "Shoreline-density contribution divisor for mobility opportunity"),
		INT_SPEC(environment, abundance_resource_weight, "environment.abundance_resource_weight", 0, 20, "weight", "environment", "Resource-capacity weight in live abundance"),
		INT_SPEC(environment, abundance_divisor, "environment.abundance_divisor", 1, 20, "weight", "environment", "Normalizer for live abundance inputs"),
		INT_SPEC(environment, smoothing_history_weight, "environment.smoothing_history_weight", 0, 20, "weight", "environment", "Historical weight for normally smoothed environment signals"),
		INT_SPEC(environment, smoothing_total_weight, "environment.smoothing_total_weight", 1, 20, "weight", "environment", "Normalizer for normally smoothed environment signals"),
		INT_SPEC(environment, fast_smoothing_history_weight, "environment.fast_smoothing_history_weight", 0, 20, "weight", "environment", "Historical weight for fast-reacting environment signals"),
		INT_SPEC(environment, fast_smoothing_total_weight, "environment.fast_smoothing_total_weight", 1, 20, "weight", "environment", "Normalizer for fast-reacting environment signals"),

		INT_SPEC(trends, observation_scale, "trends.observation_scale", 1, 100, "scale", "trends", "Scale applied to each newly observed per-cycle change"),
		INT_SPEC(trends, history_weight, "trends.history_weight", 0, 20, "weight", "trends", "Historical weight retained by strategic trend estimates"),
		INT_SPEC(trends, total_weight, "trends.total_weight", 1, 20, "weight", "trends", "Normalizer for strategic trend estimates"),

		INT_SPEC(demands, capacity_resource_weight, "demands.capacity_resource_weight", 0, 20, "weight", "demands", "Resource-capacity weight in effective economic capacity"),
		INT_SPEC(demands, capacity_divisor, "demands.capacity_divisor", 1, 20, "weight", "demands", "Normalizer for effective economic capacity"),
		INT_SPEC(demands, stranded_opportunity_divisor, "demands.stranded_opportunity_divisor", 1, 20, "score", "demands", "Mobility-opportunity divisor after swimming is available"),
		INT_SPEC(demands, food_resource_divisor, "demands.food_resource_divisor", 1, 20, "score", "demands", "Resource scarcity contribution divisor for food demand"),
		INT_SPEC(demands, food_headroom_divisor, "demands.food_headroom_divisor", 1, 20, "score", "demands", "Food-headroom contribution divisor for food demand"),
		INT_SPEC(demands, food_pressure_weight, "demands.food_pressure_weight", 0, 20, "weight", "demands", "Observed hunger contribution to food demand"),
		INT_SPEC(demands, survival_population_threshold, "demands.survival_population_threshold", 0, 200, "units", "demands", "Population below which survival receives an opening bonus"),
		INT_SPEC(demands, survival_population_bonus, "demands.survival_population_bonus", 0, 100, "score", "demands", "Opening survival-demand bonus"),
		INT_SPEC(demands, growth_capacity_weight, "demands.growth_capacity_weight", 0, 20, "weight", "demands", "Effective-capacity weight in growth demand"),
		INT_SPEC(demands, growth_divisor, "demands.growth_divisor", 1, 20, "weight", "demands", "Normalizer for positive growth inputs"),
		INT_SPEC(demands, growth_food_divisor, "demands.growth_food_divisor", 1, 20, "score", "demands", "Food-demand penalty divisor for growth"),
		INT_SPEC(demands, growth_threat_divisor, "demands.growth_threat_divisor", 1, 20, "score", "demands", "Threat penalty divisor for growth"),
		INT_SPEC(demands, growth_population_threshold, "demands.growth_population_threshold", 0, 300, "units", "demands", "Population below which growth receives an opening bonus"),
		INT_SPEC(demands, growth_population_bonus, "demands.growth_population_bonus", 0, 100, "score", "demands", "Opening growth-demand bonus"),
		INT_SPEC(demands, growth_terrain_baseline, "demands.growth_terrain_baseline", 0, 100, "score", "demands", "Terrain abundance treated as neutral for growth"),
		INT_SPEC(demands, growth_terrain_divisor, "demands.growth_terrain_divisor", 1, 20, "score", "demands", "Terrain-abundance contribution divisor for growth"),
		INT_SPEC(demands, expansion_divisor, "demands.expansion_divisor", 1, 20, "weight", "demands", "Normalizer for expansion inputs"),
		INT_SPEC(demands, expansion_failure_penalty, "demands.expansion_failure_penalty", 0, 50, "score", "demands", "Expansion-demand penalty per recent placement failure"),
		INT_SPEC(demands, access_base, "demands.access_base", 0, 150, "score", "demands", "Access demand before live scarcity and mobility adjustments"),
		INT_SPEC(demands, access_mobility_divisor, "demands.access_mobility_divisor", 1, 20, "score", "demands", "Mobility-constraint contribution divisor for access"),
		INT_SPEC(demands, access_opportunity_divisor, "demands.access_opportunity_divisor", 1, 20, "score", "demands", "Stranded-opportunity contribution divisor for access"),
		INT_SPEC(demands, access_population_threshold, "demands.access_population_threshold", 0, 200, "units", "demands", "Population below which access receives an opening bonus"),
		INT_SPEC(demands, access_population_bonus, "demands.access_population_bonus", 0, 100, "score", "demands", "Opening access-demand bonus"),
		INT_SPEC(demands, access_corn_threshold, "demands.access_corn_threshold", 0, 100, "tiles", "demands", "Accessible corn below which access demand rises"),
		INT_SPEC(demands, access_corn_bonus, "demands.access_corn_bonus", 0, 100, "score", "demands", "Access-demand bonus for scarce corn"),
		INT_SPEC(demands, technology_base, "demands.technology_base", 0, 150, "score", "demands", "Technology demand before adaptive inputs"),
		INT_SPEC(demands, technology_population_divisor, "demands.technology_population_divisor", 1, 50, "units", "demands", "Population contribution divisor for technology"),
		INT_SPEC(demands, technology_scarcity_divisor, "demands.technology_scarcity_divisor", 1, 50, "score", "demands", "Resource-scarcity contribution divisor for technology"),
		INT_SPEC(demands, technology_capacity_divisor, "demands.technology_capacity_divisor", 1, 50, "score", "demands", "Effective-capacity contribution divisor for technology"),
		INT_SPEC(demands, technology_terrain_divisor, "demands.technology_terrain_divisor", 1, 50, "score", "demands", "Terrain-abundance contribution divisor for technology"),
		INT_SPEC(demands, technology_growth_divisor, "demands.technology_growth_divisor", 1, 50, "score", "demands", "Growth-demand contribution divisor for technology"),
		INT_SPEC(demands, technology_survival_divisor, "demands.technology_survival_divisor", 1, 50, "score", "demands", "Survival-demand penalty divisor for technology"),
		INT_SPEC(demands, mobility_capability_bonus, "demands.mobility_capability_bonus", 0, 100, "score", "demands", "Mobility-demand bonus when algae exists but no school has been built"),
		INT_SPEC(demands, mobility_survival_divisor, "demands.mobility_survival_divisor", 1, 50, "score", "demands", "Survival-demand penalty divisor for mobility"),
		INT_SPEC(demands, military_base, "demands.military_base", 0, 150, "score", "demands", "Military demand before force, economy, and threat inputs"),
		INT_SPEC(demands, military_enemy_weight, "demands.military_enemy_weight", 0, 20, "weight", "demands", "Largest enemy force contribution to military demand"),
		INT_SPEC(demands, military_resource_divisor, "demands.military_resource_divisor", 1, 50, "score", "demands", "Resource-capacity contribution divisor for military demand"),
		INT_SPEC(demands, military_population_threshold, "demands.military_population_threshold", 0, 300, "units", "demands", "Population at which military demand receives a maturity bonus"),
		INT_SPEC(demands, military_population_bonus, "demands.military_population_bonus", 0, 100, "score", "demands", "Mature-colony military-demand bonus"),
		INT_SPEC(demands, military_target_bonus, "demands.military_target_bonus", 0, 100, "score", "demands", "Military-demand bonus when an enemy target is known"),
		INT_SPEC(demands, military_food_divisor, "demands.military_food_divisor", 1, 50, "score", "demands", "Food-demand penalty divisor for military demand"),
		INT_SPEC(demands, aggression_economy_divisor, "demands.aggression_economy_divisor", 1, 50, "score", "demands", "Normalizer for economic aggression inputs"),
		INT_SPEC(demands, aggression_warrior_weight, "demands.aggression_warrior_weight", 0, 20, "weight", "demands", "Trained-warrior contribution to aggression"),
		INT_SPEC(demands, aggression_enemy_weight, "demands.aggression_enemy_weight", 0, 20, "weight", "demands", "Enemy-force penalty to aggression"),
		INT_SPEC(demands, aggression_target_bonus, "demands.aggression_target_bonus", 0, 100, "score", "demands", "Aggression bonus when an enemy target is known"),
		BOOL_SPEC(economy, swarm_retirement_enabled, "economy.swarm_retirement_enabled", "enabled", "economy", "Allow safe retirement of remote unproductive swarms"),
		BOOL_SPEC(economy, large_economy_adaptation_enabled, "economy.large_economy_adaptation_enabled", "enabled", "economy", "Enable persistent large-economy adaptation"),
		BOOL_SPEC(economy, amphibious_network_maintenance_enabled, "economy.amphibious_network_maintenance_enabled", "enabled", "economy", "Maintain pool capacity after committing to an amphibious economy"),
		BOOL_SPEC(economy, food_service_safeguards_enabled, "economy.food_service_safeguards_enabled", "enabled", "economy", "Expand inn construction demand for observed food-service backlog"),
		BOOL_SPEC(economy, worker_birth_throttle_enabled, "economy.worker_birth_throttle_enabled", "enabled", "economy", "Reduce worker birth weight to one when free labor greatly exceeds demand"),
		INT_SPEC(economy, swarm_labor_scale_percent, "economy.swarm_labor_scale_percent", 1, 1000, "percent", "economy", "Birth labor coefficient multiplying square root of workforce"),
		INT_SPEC(economy, swarm_food_per_worker_percent, "economy.swarm_food_per_worker_percent", 1, 10000, "percent", "economy", "Reachable fertile corn tiles per birth worker at half funding"),
		INT_SPEC(economy, swarm_pressure_sensitivity, "economy.swarm_pressure_sensitivity", 0, 100, "ratio", "economy", "Smooth response to critical or unserved food fraction"),
		INT_SPEC(economy, swarm_workers_per_building, "economy.swarm_workers_per_building", 1, 20, "workers", "economy", "Funded birth workers per desired swarm"),
		INT_SPEC(economy, inn_population_offset, "economy.inn_population_offset", -100, 200, "units", "economy", "Population offset used for inn demand"),
		INT_SPEC(economy, inn_population_divisor, "economy.inn_population_divisor", 1, 200, "units", "economy", "Population divisor used for inn demand"),
		INT_SPEC(economy, mature_food_headroom_min, "economy.mature_food_headroom_min", 0, 100, "score", "economy", "Food headroom required for the mature-throughput boost"),
		INT_SPEC(economy, food_headroom_warning, "economy.food_headroom_warning", 0, 100, "score", "economy", "Food headroom warning threshold"),
		INT_SPEC(economy, food_headroom_critical, "economy.food_headroom_critical", 0, 100, "score", "economy", "Food headroom critical threshold"),
		INT_SPEC(economy, school_population_min, "economy.school_population_min", 0, 1000, "units", "economy", "Population required for adaptive schools"),
		INT_SPEC(economy, school_utility_min, "economy.school_utility_min", 0, 100, "score", "economy", "Utility required for first school"),
		INT_SPEC(economy, second_school_utility_min, "economy.second_school_utility_min", 0, 100, "score", "economy", "Utility required for second school"),
		INT_SPEC(economy, pool_population_min, "economy.pool_population_min", 0, 1000, "units", "economy", "Population required for a swimming pool"),
		INT_SPEC(economy, pool_utility_min, "economy.pool_utility_min", 0, 100, "score", "economy", "Utility required for a swimming pool"),
		INT_SPEC(economy, racetrack_population_min, "economy.racetrack_population_min", 0, 1000, "units", "economy", "Population required for a racetrack"),
		INT_SPEC(economy, racetrack_utility_min, "economy.racetrack_utility_min", 0, 100, "score", "economy", "Utility required for a racetrack"),
		INT_SPEC(economy, growth_site_utility_mid, "economy.growth_site_utility_mid", 0, 100, "score", "economy", "Utility for a second construction site"),
		INT_SPEC(economy, growth_site_utility_high, "economy.growth_site_utility_high", 0, 100, "score", "economy", "Utility for a third construction site"),
		INT_SPEC(economy, reliable_inn_percent, "economy.reliable_inn_percent", 1, 100, "percent", "economy", "Share of nominal inn capacity trusted for service planning"),
		INT_SPEC(economy, service_unserved_percent, "economy.service_unserved_percent", 0, 100, "percent", "economy", "Unserved-food share that adds an inn"),
		INT_SPEC(economy, service_critical_percent, "economy.service_critical_percent", 0, 100, "percent", "economy", "Critical-food share that adds an inn"),
		INT_SPEC(economy, service_combined_percent, "economy.service_combined_percent", 0, 100, "percent", "economy", "Combined food-pressure share that adds an inn"),
		INT_SPEC(economy, sustainable_inn_floor, "economy.sustainable_inn_floor", 1, 32, "buildings", "economy", "Minimum inn count considered sustainable regardless of visible corn"),
		INT_SPEC(economy, sustainable_inn_corn_divisor, "economy.sustainable_inn_corn_divisor", 1, 100, "tiles", "economy", "Accessible corn required per additional sustainable inn"),
		INT_SPEC(economy, sustainable_inn_offset, "economy.sustainable_inn_offset", 0, 32, "buildings", "economy", "Inn allowance added to the corn-derived sustainable limit"),
		INT_SPEC(economy, inn_target_floor, "economy.inn_target_floor", 0, 32, "buildings", "economy", "Minimum normal inn target"),
		INT_SPEC(economy, inn_target_cap, "economy.inn_target_cap", 1, 64, "buildings", "economy", "Maximum normal inn target"),
		INT_SPEC(economy, abundance_inn_target_cap, "economy.abundance_inn_target_cap", 1, 64, "buildings", "economy", "Maximum inn target during an abundance surge"),
		INT_SPEC(economy, abundance_inn_population_offset, "economy.abundance_inn_population_offset", 0, 200, "units", "economy", "Population offset in abundance-surge inn demand"),
		INT_SPEC(economy, abundance_inn_population_divisor, "economy.abundance_inn_population_divisor", 1, 200, "units", "economy", "Population per abundance-surge inn"),
		INT_SPEC(economy, early_population_threshold, "economy.early_population_threshold", 0, 300, "units", "economy", "Population boundary for early worker production"),
		INT_SPEC(economy, early_worker_ratio, "economy.early_worker_ratio", 0, 20, "ratio", "economy", "Worker production ratio in the early colony"),
		INT_SPEC(economy, worker_backlog_high, "economy.worker_backlog_high", 0, 100, "jobs", "economy", "Open-job surplus selecting the high backlog worker ratio"),
		INT_SPEC(economy, worker_ratio_backlog_high, "economy.worker_ratio_backlog_high", 0, 20, "ratio", "economy", "Worker production ratio under high job backlog"),
		INT_SPEC(economy, worker_ratio_backlog_low, "economy.worker_ratio_backlog_low", 0, 20, "ratio", "economy", "Worker production ratio under any job backlog"),
		INT_SPEC(economy, worker_ratio_surplus, "economy.worker_ratio_surplus", 0, 20, "ratio", "economy", "Worker production ratio when labor is in surplus"),
		INT_SPEC(economy, abundance_access_population_min, "economy.abundance_access_population_min", 0, 500, "units", "economy", "Population at which abundance raises access utility"),
		INT_SPEC(economy, abundance_access_utility_floor, "economy.abundance_access_utility_floor", 0, 100, "score", "economy", "Access-utility floor during an abundance surge"),
		INT_SPEC(economy, amphibious_opportunity_min, "economy.amphibious_opportunity_min", 0, 100, "score", "economy", "Mobility opportunity required to preserve an amphibious network"),
		INT_SPEC(economy, amphibious_population_min, "economy.amphibious_population_min", 0, 1000, "units", "economy", "Population required to preserve an amphibious network"),
		INT_SPEC(economy, amphibious_utility_floor, "economy.amphibious_utility_floor", 0, 100, "score", "economy", "Access-utility floor for a preserved amphibious network"),
		INT_SPEC(economy, second_pool_population_min, "economy.second_pool_population_min", 0, 1000, "units", "economy", "Population required for a second mobility-driven pool"),
		INT_SPEC(economy, second_pool_utility_min, "economy.second_pool_utility_min", 0, 100, "score", "economy", "Mobility demand required for a second pool"),
		INT_SPEC(economy, first_pool_target, "economy.first_pool_target", 0, 8, "buildings", "economy", "Pool target after the initial access-policy gate"),
		INT_SPEC(economy, second_pool_target, "economy.second_pool_target", 0, 8, "buildings", "economy", "Pool target after advanced mobility or abundance gates"),
		INT_SPEC(economy, abundance_pool_population_min, "economy.abundance_pool_population_min", 0, 1000, "units", "economy", "Population required for abundance-driven pool construction"),
		INT_SPEC(economy, abundance_pool_school_min, "economy.abundance_pool_school_min", 0, 16, "buildings", "economy", "Schools required before abundance requests a second pool"),
		INT_SPEC(economy, explorer_cap, "economy.explorer_cap", 0, 1000, "units", "economy", "Maximum access-policy explorer target"),
		INT_SPEC(economy, explorer_floor, "economy.explorer_floor", 0, 1000, "units", "economy", "Minimum access-policy explorer target"),
		INT_SPEC(economy, explorer_population_divisor, "economy.explorer_population_divisor", 1, 1000, "units", "economy", "Population per access-policy explorer"),
		INT_SPEC(economy, explorer_population_offset, "economy.explorer_population_offset", 0, 100, "units", "economy", "Base explorers added to the population-derived target"),
		INT_SPEC(economy, explorer_utility_divisor, "economy.explorer_utility_divisor", 1, 100, "score", "economy", "Access utility per additional explorer"),
		INT_SPEC(economy, second_racetrack_population_min, "economy.second_racetrack_population_min", 0, 1000, "units", "economy", "Population required for a second racetrack"),
		INT_SPEC(economy, second_racetrack_utility_min, "economy.second_racetrack_utility_min", 0, 100, "score", "economy", "Technology utility required for a second racetrack"),
		INT_SPEC(economy, first_school_target, "economy.first_school_target", 0, 8, "buildings", "economy", "School target after the initial technology gate"),
		INT_SPEC(economy, second_school_target, "economy.second_school_target", 0, 8, "buildings", "economy", "School target after the high-technology gate"),
		INT_SPEC(economy, first_racetrack_target, "economy.first_racetrack_target", 0, 8, "buildings", "economy", "Racetrack target after the initial technology gate"),
		INT_SPEC(economy, second_racetrack_target, "economy.second_racetrack_target", 0, 8, "buildings", "economy", "Racetrack target after the advanced technology gate"),
		INT_SPEC(economy, worker_birth_stop_population_min, "economy.worker_birth_stop_population_min", 0, 1000, "units", "economy", "Population at which a large labor surplus reduces worker birth weight to one"),
		INT_SPEC(economy, worker_birth_stop_surplus_divisor, "economy.worker_birth_stop_surplus_divisor", 1, 100, "workers", "economy", "Worker fraction defining a large free-labor surplus"),
		INT_SPEC(economy, pool_bid_utility_min, "economy.pool_bid_utility_min", 0, 100, "score", "economy", "Access utility required for the arbiter to accept pool demand"),
		INT_SPEC(economy, school_bid_utility_min, "economy.school_bid_utility_min", 0, 100, "score", "economy", "Technology utility required for the arbiter to accept school demand"),
		INT_SPEC(economy, racetrack_bid_utility_min, "economy.racetrack_bid_utility_min", 0, 100, "score", "economy", "Technology utility required for the arbiter to accept racetrack demand"),
		INT_SPEC(economy, barracks_bid_utility_min, "economy.barracks_bid_utility_min", 0, 100, "score", "economy", "Defense utility required for the arbiter to accept barracks demand"),
		INT_SPEC(economy, offense_bid_utility_min, "economy.offense_bid_utility_min", 0, 100, "score", "economy", "Offense utility required for the arbiter to accept offensive warrior demand"),
		INT_SPEC(economy, connected_boom_baseline, "economy.connected_boom_baseline", 0, 100, "score", "economy", "Connected abundance treated as neutral by economy threshold adaptation"),
		INT_SPEC(economy, growth_mid_boom_divisor, "economy.growth_mid_boom_divisor", 1, 50, "score", "economy", "Connected-boom reduction divisor for medium growth-site threshold"),
		INT_SPEC(economy, growth_barrier_divisor, "economy.growth_barrier_divisor", 1, 50, "score", "economy", "Access-barrier increase divisor for growth-site thresholds"),
		INT_SPEC(economy, school_boom_divisor, "economy.school_boom_divisor", 1, 50, "score", "economy", "Connected-boom reduction divisor for school thresholds"),
		INT_SPEC(economy, second_school_barrier_divisor, "economy.second_school_barrier_divisor", 1, 50, "score", "economy", "Access-barrier increase divisor for the second-school threshold"),
		INT_SPEC(economy, pool_population_barrier_divisor, "economy.pool_population_barrier_divisor", 1, 50, "score", "economy", "Access-barrier reduction divisor for pool population requirement"),
		INT_SPEC(economy, pool_utility_floor, "economy.pool_utility_floor", 0, 100, "score", "economy", "Minimum adaptive pool utility threshold"),
		INT_SPEC(economy, pool_utility_barrier_divisor, "economy.pool_utility_barrier_divisor", 1, 50, "score", "economy", "Access-barrier reduction divisor for pool utility requirement"),
		INT_SPEC(economy, food_upgrade_utility_min, "economy.food_upgrade_utility_min", 0, 100, "score", "economy", "Food demand independently enabling survival-driven upgrades"),
		INT_SPEC(economy, food_upgrade_headroom_max, "economy.food_upgrade_headroom_max", 0, 100, "score", "economy", "Food headroom below which survival enables upgrades"),
		INT_SPEC(economy, explorer_ratio_high_utility, "economy.explorer_ratio_high_utility", 0, 100, "score", "economy", "Access utility selecting the high explorer production ratio"),
		INT_SPEC(economy, explorer_ratio_low_utility, "economy.explorer_ratio_low_utility", 0, 100, "score", "economy", "Access utility selecting the low explorer production ratio"),
		INT_SPEC(economy, explorer_ratio_high, "economy.explorer_ratio_high", 0, 20, "ratio", "economy", "Explorer production ratio under high access utility"),
		INT_SPEC(economy, explorer_ratio_low, "economy.explorer_ratio_low", 0, 20, "ratio", "economy", "Explorer production ratio under moderate access utility"),
		INT_SPEC(economy, offense_explorer_ratio_utility, "economy.offense_explorer_ratio_utility", 0, 100, "score", "economy", "Offense utility enabling explorer production without large-economy commitment"),
		INT_SPEC(economy, large_economy_explorer_ratio, "economy.large_economy_explorer_ratio", 0, 20, "ratio", "economy", "Offensive explorer production ratio in an established large economy"),
		INT_SPEC(economy, normal_offense_explorer_ratio, "economy.normal_offense_explorer_ratio", 0, 20, "ratio", "economy", "Offensive explorer production ratio outside a large economy"),

		BOOL_SPEC(upgrades, enabled, "upgrades.enabled", "enabled", "upgrades", "Enable all building upgrades"),
		INT_SPEC(upgrades, level1_population_min, "upgrades.level1_population_min", 0, 1000, "units", "upgrades", "Population required for level-one upgrades"),
		INT_SPEC(upgrades, level2_population_min, "upgrades.level2_population_min", 0, 1000, "units", "upgrades", "Population required for level-two upgrades"),
		INT_SPEC(upgrades, level1_workers, "upgrades.level1_workers", 0, 100, "workers", "upgrades", "Workers assigned to level-one upgrades"),
		INT_SPEC(upgrades, level2_workers, "upgrades.level2_workers", 0, 100, "workers", "upgrades", "Workers assigned to level-two upgrades"),
		INT_SPEC(upgrades, level1_trained_units_per_slot, "upgrades.level1_trained_units_per_slot", 1, 1000, "units", "upgrades", "Trained units required per concurrent level-one upgrade"),
		INT_SPEC(upgrades, level2_trained_units_per_slot, "upgrades.level2_trained_units_per_slot", 1, 1000, "units", "upgrades", "Trained units required per concurrent level-two upgrade"),
		INT_SPEC(upgrades, level1_inn_base_weight, "upgrades.level1_inn_base_weight", 0, 1000, "weight", "upgrades", "Level-one inn upgrade weight"),
		INT_SPEC(upgrades, level1_hospital_base_weight, "upgrades.level1_hospital_base_weight", 0, 1000, "weight", "upgrades", "Level-one hospital upgrade weight"),
		INT_SPEC(upgrades, level1_racetrack_base_weight, "upgrades.level1_racetrack_base_weight", 0, 1000, "weight", "upgrades", "Level-one racetrack upgrade weight"),
		INT_SPEC(upgrades, level1_pool_base_weight, "upgrades.level1_pool_base_weight", 0, 1000, "weight", "upgrades", "Level-one swimming-pool upgrade weight"),
		INT_SPEC(upgrades, level1_barracks_base_weight, "upgrades.level1_barracks_base_weight", 0, 1000, "weight", "upgrades", "Level-one barracks upgrade weight"),
		INT_SPEC(upgrades, level2_inn_base_weight, "upgrades.level2_inn_base_weight", 0, 1000, "weight", "upgrades", "Level-two inn upgrade weight"),
		INT_SPEC(upgrades, level2_hospital_base_weight, "upgrades.level2_hospital_base_weight", 0, 1000, "weight", "upgrades", "Level-two hospital upgrade weight"),
		INT_SPEC(upgrades, level2_racetrack_base_weight, "upgrades.level2_racetrack_base_weight", 0, 1000, "weight", "upgrades", "Level-two racetrack upgrade weight"),
		INT_SPEC(upgrades, level2_pool_base_weight, "upgrades.level2_pool_base_weight", 0, 1000, "weight", "upgrades", "Level-two swimming-pool upgrade weight"),
		INT_SPEC(upgrades, level2_barracks_base_weight, "upgrades.level2_barracks_base_weight", 0, 1000, "weight", "upgrades", "Level-two barracks upgrade weight"),
		INT_SPEC(upgrades, first_prestige_trained_workers, "upgrades.first_prestige_trained_workers", 0, 1000, "units", "upgrades", "Trained-worker threshold for first prestige upgrades"),
		INT_SPEC(upgrades, second_prestige_trained_workers, "upgrades.second_prestige_trained_workers", 0, 1000, "units", "upgrades", "Trained-worker threshold for second prestige upgrades"),
		INT_SPEC(upgrades, second_prestige_population_min, "upgrades.second_prestige_population_min", 0, 1000, "units", "upgrades", "Population threshold for second prestige upgrades"),
		INT_SPEC(upgrades, food_demand_divisor, "upgrades.food_demand_divisor", 1, 50, "score", "upgrades", "Food-demand contribution divisor for inn upgrade priority"),
		INT_SPEC(upgrades, food_headroom_divisor, "upgrades.food_headroom_divisor", 1, 50, "score", "upgrades", "Food-headroom contribution divisor for inn upgrade priority"),
		INT_SPEC(upgrades, threat_divisor, "upgrades.threat_divisor", 1, 50, "score", "upgrades", "Threat-pressure contribution divisor for hospital upgrade priority"),
		INT_SPEC(upgrades, technology_divisor, "upgrades.technology_divisor", 1, 50, "score", "upgrades", "Technology-demand contribution divisor for racetrack upgrade priority"),
		INT_SPEC(upgrades, mobility_divisor, "upgrades.mobility_divisor", 1, 50, "score", "upgrades", "Mobility-demand contribution divisor for pool upgrade priority"),
		INT_SPEC(upgrades, military_divisor, "upgrades.military_divisor", 1, 50, "score", "upgrades", "Military-demand contribution divisor for barracks upgrade priority"),
		INT_SPEC(upgrades, recovery_inn_weight, "upgrades.recovery_inn_weight", 0, 1000, "weight", "upgrades", "Inn upgrade priority forced during recovery posture"),

		BOOL_SPEC(repairs, enabled, "repairs.enabled", "enabled", "repairs", "Enable repairs of damaged buildings"),

		INT_SPEC(construction, population_mid, "construction.population_mid", 0, 1000, "units", "construction", "Population threshold for a second site"),
		INT_SPEC(construction, population_high, "construction.population_high", 0, 1000, "units", "construction", "Population threshold for a third site"),
		INT_SPEC(construction, sites_low, "construction.sites_low", 1, 16, "sites", "construction", "Construction-site cap below the mid population threshold"),
		INT_SPEC(construction, sites_mid, "construction.sites_mid", 1, 16, "sites", "construction", "Construction-site cap between population thresholds"),
		INT_SPEC(construction, sites_high, "construction.sites_high", 1, 16, "sites", "construction", "Construction-site cap above the high population threshold"),
		INT_SPEC(construction, survival_utility_high, "construction.survival_utility_high", 0, 100, "score", "construction", "Survival utility that requests the high site count"),
		INT_SPEC(construction, growth_utility_high_sites, "construction.growth_utility_high_sites", 1, 16, "sites", "construction", "Sites requested by high growth utility"),
		INT_SPEC(construction, growth_utility_mid_sites, "construction.growth_utility_mid_sites", 1, 16, "sites", "construction", "Sites requested by medium growth utility"),
		INT_SPEC(construction, growth_utility_low_sites, "construction.growth_utility_low_sites", 1, 16, "sites", "construction", "Sites requested by low growth utility"),
		INT_SPEC(construction, policy_utility_high, "construction.policy_utility_high", 0, 100, "score", "construction", "Utility at which access, technology, or defense requests more sites"),
		INT_SPEC(construction, policy_high_sites, "construction.policy_high_sites", 1, 16, "sites", "construction", "Sites requested by a high-utility supporting policy"),
		INT_SPEC(construction, policy_low_sites, "construction.policy_low_sites", 1, 16, "sites", "construction", "Sites requested by a low-utility supporting policy"),
		INT_SPEC(construction, emergency_min_sites, "construction.emergency_min_sites", 1, 16, "sites", "construction", "Minimum construction sites retained in a food emergency"),
		INT_SPEC(construction, emergency_max_sites, "construction.emergency_max_sites", 1, 16, "sites", "construction", "Maximum construction sites retained in a food emergency"),
		INT_SPEC(construction, defense_emergency_sites, "construction.defense_emergency_sites", 1, 16, "sites", "construction", "Minimum sites during an explorer-defense emergency"),

		BOOL_SPEC(military, counterattack_enabled, "military.counterattack_enabled", "enabled", "military", "Allow campaigns to launch while the colony is directly threatened"),
		BOOL_SPEC(military, explorer_defense_enabled, "military.explorer_defense_enabled", "enabled", "military", "Enable anti-explorer towers, defenders, and emergency reactions"),
		BOOL_SPEC(military, warrior_training_backlog_throttle_enabled, "military.warrior_training_backlog_throttle_enabled", "enabled", "military", "Pause warrior births while the training backlog is saturated"),
		INT_SPEC(military, defense_reserve_min, "military.defense_reserve_min", 0, 100, "units", "military", "Minimum defense reserve"),
		INT_SPEC(military, campaign_population_base, "military.campaign_population_base", 0, 1000, "units", "military", "Population basis for campaign readiness"),
		INT_SPEC(military, campaign_force_margin, "military.campaign_force_margin", 0, 100, "units", "military", "Force margin over estimated opposition"),
		INT_SPEC(military, campaign_worker_floor_base, "military.campaign_worker_floor_base", 0, 1000, "units", "military", "Worker floor retained during campaigns"),
		INT_SPEC(military, attack_unit_cap, "military.attack_unit_cap", 1, 20, "units", "military", "Maximum units in one create-unit order"),
		INT_SPEC(military, tower_active_count, "military.tower_active_count", 0, 32, "buildings", "military", "Normal defensive tower target"),
		INT_SPEC(military, tower_emergency_increment, "military.tower_emergency_increment", 0, 32, "buildings", "military", "Additional emergency towers"),
		INT_SPEC(military, tower_bomb_increment, "military.tower_bomb_increment", 0, 32, "buildings", "military", "Additional bombardment towers"),
		INT_SPEC(military, defender_explorer_percent, "military.defender_explorer_percent", 0, 100, "percent", "military", "Population percentage used as explorer defenders"),
		INT_SPEC(military, defender_explorer_cap, "military.defender_explorer_cap", 0, 1000, "units", "military", "Maximum explorer defenders"),
		INT_SPEC(military, defense_reserve_floor, "military.defense_reserve_floor", 0, 1000, "units", "military", "Adaptive defense reserve floor"),
		INT_SPEC(military, defense_enemy_percent, "military.defense_enemy_percent", 0, 200, "percent", "military", "Enemy force percentage reserved for defense"),
		INT_SPEC(military, offense_warrior_base_percent, "military.offense_warrior_base_percent", 0, 100, "percent", "military", "Base population percentage for offensive warriors"),
		INT_SPEC(military, campaign_deployable_min, "military.campaign_deployable_min", 0, 1000, "units", "military", "Deployable force required to launch a campaign"),
		INT_SPEC(military, reserve_enemy_bonus, "military.reserve_enemy_bonus", 0, 100, "units", "military", "Units added above the enemy-scaled defense reserve"),
		INT_SPEC(military, reserve_force_divisor, "military.reserve_force_divisor", 1, 20, "units", "military", "Own trained-force divisor contributing to defense reserve"),
		INT_SPEC(military, endgame_enemy_count, "military.endgame_enemy_count", 1, 16, "teams", "military", "Remaining enemies at or below which endgame campaign rules apply"),
		INT_SPEC(military, endgame_force_floor, "military.endgame_force_floor", 0, 200, "units", "military", "Minimum campaign force in the endgame"),
		INT_SPEC(military, campaign_force_floor, "military.campaign_force_floor", 0, 200, "units", "military", "Minimum campaign force outside the endgame"),
		INT_SPEC(military, endgame_population_floor, "military.endgame_population_floor", 0, 1000, "units", "military", "Minimum campaign population in the endgame"),
		INT_SPEC(military, campaign_population_floor, "military.campaign_population_floor", 0, 1000, "units", "military", "Minimum campaign population outside the endgame"),
		INT_SPEC(military, endgame_population_discount, "military.endgame_population_discount", 0, 500, "units", "military", "Population discount applied to campaign readiness in the endgame"),
		INT_SPEC(military, readiness_demand_divisor, "military.readiness_demand_divisor", 1, 50, "score", "military", "Demand divisor reducing campaign population and worker floors"),
		INT_SPEC(military, campaign_worker_floor, "military.campaign_worker_floor", 0, 1000, "workers", "military", "Absolute minimum worker count retained before campaigning"),
		INT_SPEC(military, campaign_food_percent, "military.campaign_food_percent", 0, 100, "percent", "military", "Food-pressure threshold for economic campaign support; does not gate tactics"),
		INT_SPEC(military, campaign_worker_population_ratio, "military.campaign_worker_population_ratio", 1, 10, "ratio", "military", "Population per worker allowed at campaign launch"),
		INT_SPEC(military, campaign_sustainable_food_percent, "military.campaign_sustainable_food_percent", 0, 100, "percent", "military", "Legacy compatibility value; food pressure no longer cancels an active campaign"),
		INT_SPEC(military, counterattack_force_margin, "military.counterattack_force_margin", 0, 100, "units", "military", "Deployable force required above the enemy for a counterattack"),
		INT_SPEC(military, defense_utility_floor_bonus, "military.defense_utility_floor_bonus", 0, 100, "score", "military", "Threat-pressure bonus establishing minimum defense utility"),
		INT_SPEC(military, explorer_defense_utility_bonus, "military.explorer_defense_utility_bonus", 0, 100, "score", "military", "Defense-utility bonus while explorer defense is active"),
		INT_SPEC(military, warrior_cap, "military.warrior_cap", 0, 1000, "units", "military", "Maximum defense or offense warrior target"),
		INT_SPEC(military, warrior_floor, "military.warrior_floor", 0, 1000, "units", "military", "Minimum defense warrior target"),
		INT_SPEC(military, warrior_enemy_margin, "military.warrior_enemy_margin", 0, 100, "units", "military", "Defense warriors requested above the largest enemy force"),
		INT_SPEC(military, warrior_population_percent, "military.warrior_population_percent", 0, 100, "percent", "military", "Base population share requested as defensive warriors"),
		INT_SPEC(military, warrior_utility_divisor, "military.warrior_utility_divisor", 1, 50, "score", "military", "Defense utility converted into additional warrior percentage"),
		INT_SPEC(military, first_barracks_population_min, "military.first_barracks_population_min", 0, 1000, "units", "military", "Population required for the first barracks"),
		INT_SPEC(military, second_barracks_population_min, "military.second_barracks_population_min", 0, 1000, "units", "military", "Population independently justifying a second barracks"),
		INT_SPEC(military, second_barracks_enemy_min, "military.second_barracks_enemy_min", 0, 1000, "units", "military", "Enemy force independently justifying a second barracks"),
		INT_SPEC(military, third_barracks_backlog_min, "military.third_barracks_backlog_min", 0, 1000, "units", "military", "Untrained-warrior backlog independently justifying a third barracks"),
		INT_SPEC(military, third_barracks_enemy_min, "military.third_barracks_enemy_min", 0, 1000, "units", "military", "Enemy force independently justifying a third barracks"),
		INT_SPEC(military, emergency_barracks_threat_min, "military.emergency_barracks_threat_min", 0, 1000, "units", "military", "Visible colony threat forcing three barracks"),
		INT_SPEC(military, hospital_warrior_min, "military.hospital_warrior_min", 0, 1000, "units", "military", "Warrior count that creates hospital demand without current injuries"),
		INT_SPEC(military, hospital_cap, "military.hospital_cap", 0, 64, "buildings", "military", "Maximum hospital target"),
		INT_SPEC(military, hospital_units_per_building, "military.hospital_units_per_building", 1, 200, "units", "military", "Warriors supported by each desired hospital"),
		INT_SPEC(military, bomb_attack_explorers_min, "military.bomb_attack_explorers_min", 0, 1000, "units", "military", "Visible attack explorers triggering maximum tower escalation"),
		INT_SPEC(military, bomb_colony_explorers_min, "military.bomb_colony_explorers_min", 0, 1000, "units", "military", "Explorer threat near the colony triggering maximum tower escalation"),
		INT_SPEC(military, offense_warrior_floor, "military.offense_warrior_floor", 0, 1000, "units", "military", "Minimum offensive warrior target"),
		INT_SPEC(military, offense_explorer_floor, "military.offense_explorer_floor", 0, 1000, "units", "military", "Minimum offensive explorer target after prestige"),
		INT_SPEC(military, offense_explorer_cap, "military.offense_explorer_cap", 0, 1000, "units", "military", "Maximum offensive explorer target after prestige"),
		INT_SPEC(military, warrior_ratio_high_utility, "military.warrior_ratio_high_utility", 0, 100, "score", "military", "Utility selecting the high warrior production ratio"),
		INT_SPEC(military, warrior_ratio_low_utility, "military.warrior_ratio_low_utility", 0, 100, "score", "military", "Utility selecting the low warrior production ratio"),
		INT_SPEC(military, warrior_ratio_high, "military.warrior_ratio_high", 0, 20, "ratio", "military", "Warrior production ratio under high military utility"),
		INT_SPEC(military, warrior_ratio_low, "military.warrior_ratio_low", 0, 20, "ratio", "military", "Warrior production ratio under moderate military utility"),
		INT_SPEC(military, training_backlog_floor, "military.training_backlog_floor", 0, 1000, "units", "military", "Minimum untrained-warrior backlog that pauses warrior births"),
		INT_SPEC(military, training_backlog_per_barracks, "military.training_backlog_per_barracks", 1, 1000, "units", "military", "Untrained warriors tolerated per barracks before births pause"),
		INT_SPEC(military, defender_explorer_floor, "military.defender_explorer_floor", 0, 1000, "units", "military", "Minimum explorer defenders while explorer defense is active"),
		INT_SPEC(military, defender_explorer_ratio, "military.defender_explorer_ratio", 0, 20, "ratio", "military", "Explorer production ratio while explorer defense is active"),
		INT_SPEC(military, offense_utility_divisor, "military.offense_utility_divisor", 1, 50, "score", "military", "Offense utility converted into additional warrior percentage"),
		BOOL_SPEC(military, preemptive_defense_enabled, "military.preemptive_defense_enabled", "boolean", "military", "Whether preemptive choke defense is enabled"),
		INT_SPEC(military, preemptive_defense_min_trained_warriors, "military.preemptive_warriors_min", 0, 1000, "units", "military", "Trained warriors required for preemptive defense"),
		INT_SPEC(military, preemptive_defense_warriors_per_zone, "military.preemptive_warriors_per_zone", 1, 1000, "units", "military", "Trained warriors supporting each preemptive zone"),
		BOOL_SPEC(military, preemptive_defense_amphibious_enabled, "military.preemptive_amphibious_enabled", "boolean", "military", "Whether preemptive defense analyzes amphibious routes"),
		INT_SPEC(military, preemptive_defense_min_swimming_warriors, "military.preemptive_swimming_warriors_min", 0, 1000, "units", "military", "Swimming warriors required for amphibious preemptive defense"),
		INT_SPEC(military, preemptive_defense_inner_distance, "military.preemptive_inner_distance", 0, 256, "tiles", "military", "Inner choke-defense distance"),
		INT_SPEC(military, preemptive_defense_band_width, "military.preemptive_band_width", 1, 256, "tiles", "military", "Choke-defense distance band width"),
		INT_SPEC(military, preemptive_defense_path_slack, "military.preemptive_path_slack", 0, 256, "tiles", "military", "Allowed choke path slack"),
		INT_SPEC(military, preemptive_defense_choke_probe_radius, "military.preemptive_probe_radius", 1, 64, "tiles", "military", "Radius used to measure choke cross-sections"),
		INT_SPEC(military, preemptive_defense_max_cross_section, "military.preemptive_cross_section_max", 1, 256, "tiles", "military", "Maximum accepted choke cross-section"),
		INT_SPEC(military, preemptive_defense_zone_radius, "military.preemptive_zone_radius", 0, 64, "tiles", "military", "Radius of each preemptive guard zone"),
		INT_SPEC(military, preemptive_defense_max_zones, "military.preemptive_zone_max", 0, 64, "zones", "military", "Maximum simultaneous preemptive zones"),
		INT_SPEC(military, tower_barrier_bonus, "military.tower_barrier_bonus", 0, 100, "weight", "military", "Placement bonus near barrier-defense points"),

		BOOL_SPEC(postures, recover_enabled, "postures.recover_enabled", "enabled", "postures", "Allow selection of the recovery posture"),
		BOOL_SPEC(postures, defend_enabled, "postures.defend_enabled", "enabled", "postures", "Allow selection of the defense posture"),
		BOOL_SPEC(postures, expand_enabled, "postures.expand_enabled", "enabled", "postures", "Allow selection of the expansion posture"),
		BOOL_SPEC(postures, develop_enabled, "postures.develop_enabled", "enabled", "postures", "Allow selection of the development posture"),
		BOOL_SPEC(postures, mobilize_enabled, "postures.mobilize_enabled", "enabled", "postures", "Allow selection of the mobilization posture"),
		BOOL_SPEC(postures, campaign_enabled, "postures.campaign_enabled", "enabled", "postures", "Allow selection and continuation of the campaign posture"),
		BOOL_SPEC(postures, finish_enabled, "postures.finish_enabled", "enabled", "postures", "Allow selection of the finishing posture"),
		INT_SPEC(postures, recover_food_pressure_min, "postures.recover_food_pressure_min", 0, 100, "percent", "postures", "Food pressure at which recovery posture becomes competitive"),
		INT_SPEC(postures, recover_base, "postures.recover_base", -200, 300, "score", "postures", "Recovery-posture utility at its activation threshold"),
		INT_SPEC(postures, recover_food_weight, "postures.recover_food_weight", 0, 50, "weight", "postures", "Recovery utility gained per excess food-pressure point"),
		INT_SPEC(postures, recover_inactive_utility, "postures.recover_inactive_utility", -300, 0, "score", "postures", "Recovery utility below its food-pressure threshold"),
		INT_SPEC(postures, defend_colony_weight, "postures.defend_colony_weight", 0, 100, "weight", "postures", "Visible colony-threat contribution to defend posture"),
		INT_SPEC(postures, defend_building_weight, "postures.defend_building_weight", 0, 200, "weight", "postures", "Attacked-building contribution to defend posture"),
		INT_SPEC(postures, defend_unit_weight, "postures.defend_unit_weight", 0, 50, "weight", "postures", "Attacked-unit contribution to defend posture"),
		INT_SPEC(postures, defend_attack_explorer_weight, "postures.defend_attack_explorer_weight", 0, 100, "weight", "postures", "Visible attack-explorer contribution to defend posture"),
		INT_SPEC(postures, defend_colony_explorer_weight, "postures.defend_colony_explorer_weight", 0, 200, "weight", "postures", "Explorer threat near the colony contribution to defend posture"),
		INT_SPEC(postures, defend_emergency_bonus, "postures.defend_emergency_bonus", 0, 300, "score", "postures", "Defend-posture bonus during an explorer emergency"),
		INT_SPEC(postures, expand_base, "postures.expand_base", -100, 300, "score", "postures", "Expansion-posture utility before population and pressure penalties"),
		INT_SPEC(postures, expand_population_cap, "postures.expand_population_cap", 0, 300, "score", "postures", "Maximum population penalty to expansion posture"),
		INT_SPEC(postures, expand_population_divisor, "postures.expand_population_divisor", 1, 50, "units", "postures", "Population per expansion penalty point"),
		INT_SPEC(postures, expand_food_weight, "postures.expand_food_weight", 0, 50, "weight", "postures", "Food-pressure penalty to expansion posture"),
		INT_SPEC(postures, develop_population_min, "postures.develop_population_min", 0, 1000, "units", "postures", "Population required for development posture"),
		INT_SPEC(postures, develop_base, "postures.develop_base", -100, 300, "score", "postures", "Base development-posture utility"),
		INT_SPEC(postures, develop_population_cap, "postures.develop_population_cap", 0, 200, "score", "postures", "Maximum population contribution to development posture"),
		INT_SPEC(postures, develop_population_divisor, "postures.develop_population_divisor", 1, 50, "units", "postures", "Population per development utility point"),
		INT_SPEC(postures, develop_no_school_population_min, "postures.develop_no_school_population_min", 0, 1000, "units", "postures", "Population at which lacking a school strongly favors development"),
		INT_SPEC(postures, develop_no_school_bonus, "postures.develop_no_school_bonus", 0, 300, "score", "postures", "Development bonus for a mature colony without a school"),
		INT_SPEC(postures, inactive_utility, "postures.inactive_utility", -300, 0, "score", "postures", "Utility assigned to population-gated develop or mobilize postures"),
		INT_SPEC(postures, mobilize_population_min, "postures.mobilize_population_min", 0, 1000, "units", "postures", "Population required for mobilization posture"),
		INT_SPEC(postures, mobilize_base, "postures.mobilize_base", -100, 300, "score", "postures", "Base mobilization-posture utility"),
		INT_SPEC(postures, desired_force_floor, "postures.desired_force_floor", 0, 500, "units", "postures", "Minimum desired force used by mobilization scoring"),
		INT_SPEC(postures, desired_force_margin, "postures.desired_force_margin", 0, 200, "units", "postures", "Desired force above the largest enemy army"),
		INT_SPEC(postures, mobilize_force_weight, "postures.mobilize_force_weight", 0, 50, "weight", "postures", "Mobilization utility per missing warrior"),
		INT_SPEC(postures, campaign_base, "postures.campaign_base", -100, 300, "score", "postures", "Base campaign-posture utility after readiness gates pass"),
		INT_SPEC(postures, campaign_warrior_weight, "postures.campaign_warrior_weight", 0, 20, "weight", "postures", "Trained-warrior contribution to campaign posture"),
		INT_SPEC(postures, campaign_target_divisor, "postures.campaign_target_divisor", 1, 200, "score", "postures", "Opponent score divisor contributing to campaign posture"),
		INT_SPEC(postures, campaign_food_weight, "postures.campaign_food_weight", 0, 50, "weight", "postures", "Food-pressure penalty to campaign posture"),
		INT_SPEC(postures, campaign_threat_weight, "postures.campaign_threat_weight", 0, 50, "weight", "postures", "Colony-threat penalty to campaign posture"),
		INT_SPEC(postures, finish_population_min, "postures.finish_population_min", 0, 1000, "units", "postures", "Population required for finishing posture"),
		INT_SPEC(postures, finish_warrior_min, "postures.finish_warrior_min", 0, 500, "units", "postures", "Trained warriors required for finishing posture"),
		INT_SPEC(postures, finish_building_max, "postures.finish_building_max", 0, 100, "buildings", "postures", "Maximum known buildings on the weakest target for finishing posture"),
		INT_SPEC(postures, finish_base, "postures.finish_base", -100, 400, "score", "postures", "Base finishing-posture utility after readiness gates pass"),
		INT_SPEC(postures, finish_building_weight, "postures.finish_building_weight", 0, 100, "weight", "postures", "Finishing utility gained as the target loses buildings"),
		INT_SPEC(postures, finish_food_weight, "postures.finish_food_weight", 0, 50, "weight", "postures", "Food-pressure penalty to finishing posture"),
		INT_SPEC(postures, finish_threat_weight, "postures.finish_threat_weight", 0, 50, "weight", "postures", "Colony-threat penalty to finishing posture"),
		INT_SPEC(postures, campaign_active_bonus, "postures.campaign_active_bonus", 0, 300, "score", "postures", "Campaign utility retained while a campaign is active or paused"),
		INT_SPEC(postures, finish_active_bonus, "postures.finish_active_bonus", 0, 300, "score", "postures", "Finishing utility bonus for an active campaign against a weak target"),
		INT_SPEC(postures, campaign_demand_weight, "postures.campaign_demand_weight", 0, 20, "weight", "postures", "Aggression-demand contribution to campaign posture"),
		INT_SPEC(postures, emergency_commitment_ticks, "postures.emergency_commitment_ticks", 1, 1000000, "ticks", "postures", "Minimum commitment to recovery or defense posture"),
		INT_SPEC(postures, emergency_campaign_cooldown_ticks, "postures.emergency_campaign_cooldown_ticks", 0, 1000000, "ticks", "postures", "Campaign cooldown imposed when entering an emergency posture"),
		INT_SPEC(postures, labor_pressure_divisor, "postures.labor_pressure_divisor", 1, 50, "jobs", "postures", "Labor pressure contribution divisor for posture scoring"),
		INT_SPEC(postures, develop_worker_divisor, "postures.develop_worker_divisor", 1, 50, "workers", "postures", "Trained-worker contribution divisor for development posture"),
		INT_SPEC(postures, develop_food_weight, "postures.develop_food_weight", 0, 50, "weight", "postures", "Food-pressure penalty to development posture"),
		INT_SPEC(postures, mobilize_food_weight, "postures.mobilize_food_weight", 0, 50, "weight", "postures", "Food-pressure penalty to mobilization posture"),
		INT_SPEC(postures, campaign_inactive_utility, "postures.campaign_inactive_utility", -300, 0, "score", "postures", "Campaign utility when readiness gates fail"),
		INT_SPEC(postures, finish_inactive_utility, "postures.finish_inactive_utility", -300, 0, "score", "postures", "Finishing utility when readiness gates fail"),
		INT_SPEC(postures, portfolio_expansion_divisor, "postures.portfolio_expansion_divisor", 1, 20, "score", "postures", "Normalizer for growth and expansion demand added to expansion posture"),

		BOOL_SPEC(placement, food_preservation_enabled, "placement.food_preservation_enabled", "enabled", "placement", "Enable farm-loss and food-zone placement penalties"),
		BOOL_SPEC(placement, defensive_siting_enabled, "placement.defensive_siting_enabled", "enabled", "placement", "Enable defendedness, threat-exposure, and tower-defense siting"),
		BOOL_SPEC(placement, spacing_compactness_enabled, "placement.spacing_compactness_enabled", "enabled", "placement", "Enable compactness and building-spacing placement heuristics"),
		BOOL_SPEC(placement, artery_routing_enabled, "placement.artery_routing_enabled", "enabled", "placement", "Enable circulation-artery route scoring and reservation"),
		INT_SPEC(placement, unmet_demand_weight, "placement.unmet_demand_weight", 0, 100, "weight", "placement", "Placement score weight for unmet strategic demand"),
		INT_SPEC(placement, service_gain_weight, "placement.service_gain_weight", 0, 100, "weight", "placement", "Placement score weight for service-throughput gain"),
		INT_SPEC(placement, capability_gain_weight, "placement.capability_gain_weight", 0, 100, "weight", "placement", "Placement score weight for capability gain"),
		INT_SPEC(placement, parallelism_gain_weight, "placement.parallelism_gain_weight", 0, 100, "weight", "placement", "Placement score weight for added parallel service"),
		INT_SPEC(placement, redundancy_gain_weight, "placement.redundancy_gain_weight", 0, 100, "weight", "placement", "Placement score weight for redundancy"),
		INT_SPEC(placement, role_location_weight, "placement.role_location_weight", 0, 100, "weight", "placement", "Placement score weight for building-specific location quality"),
		INT_SPEC(placement, defendedness_weight, "placement.defendedness_weight", 0, 100, "weight", "placement", "Placement score weight for defensive protection"),
		INT_SPEC(placement, compactness_weight, "placement.compactness_weight", 0, 100, "weight", "placement", "Placement score weight for compact campuses"),
		INT_SPEC(placement, spacing_target_tiles, "placement.spacing_target_tiles", 0, 16, "tiles", "placement", "Preferred empty gap between newly opened parcels and existing building footprints"),
		INT_SPEC(placement, spacing_weight, "placement.spacing_weight", 0, 100, "weight", "placement", "Placement score weight for reaching the preferred parcel gap"),
		INT_SPEC(placement, farm_loss_weight, "placement.farm_loss_weight", 0, 100, "weight", "placement", "Placement penalty weight for projected farming loss"),
		INT_SPEC(placement, food_zone_penalty_weight, "placement.food_zone_penalty_weight", 0, 100, "weight", "placement", "Penalty weight reserving valuable corn halos for food infrastructure"),
		INT_SPEC(placement, reserved_land_weight, "placement.reserved_land_weight", 0, 100, "weight", "placement", "Placement penalty weight for newly reserved land"),
		INT_SPEC(placement, resource_scarcity_weight, "placement.resource_scarcity_weight", 0, 100, "weight", "placement", "Placement penalty weight for scarce construction resources"),
		INT_SPEC(placement, construction_labor_weight, "placement.construction_labor_weight", 0, 100, "weight", "placement", "Placement penalty weight for construction labor"),
		INT_SPEC(placement, service_downtime_weight, "placement.service_downtime_weight", 0, 100, "weight", "placement", "Placement penalty weight for upgrade service downtime"),
		INT_SPEC(placement, threat_exposure_weight, "placement.threat_exposure_weight", 0, 100, "weight", "placement", "Placement penalty weight for local threat exposure"),
		INT_SPEC(placement, artery_length_weight, "placement.artery_length_weight", 0, 100, "weight", "placement", "Placement penalty weight for new circulation-artery length"),
		INT_SPEC(placement, unmet_count_weight, "placement.unmet_count_weight", 0, 100, "weight", "placement", "Utility added for each still-missing building"),
		INT_SPEC(placement, unmet_count_cap, "placement.unmet_count_cap", 0, 100, "score", "placement", "Maximum missing-building contribution to placement utility"),
		INT_SPEC(placement, upgrade_unmet_demand, "placement.upgrade_unmet_demand", 0, 100, "score", "placement", "Unmet-demand utility assigned to upgrades without a build intent"),
		INT_SPEC(placement, service_gain_scale, "placement.service_gain_scale", 0, 50, "weight", "placement", "Service-throughput gain converted into placement utility"),
		INT_SPEC(placement, capability_gain_scale, "placement.capability_gain_scale", 0, 50, "weight", "placement", "Capability gain converted into placement utility"),
		INT_SPEC(placement, parallel_service_base, "placement.parallel_service_base", 0, 100, "score", "placement", "Parallelism utility for any service-producing target"),
		INT_SPEC(placement, parallel_build_bonus, "placement.parallel_build_bonus", 0, 100, "score", "placement", "Parallelism utility added for new construction"),
		INT_SPEC(placement, parallel_no_service, "placement.parallel_no_service", 0, 100, "score", "placement", "Parallelism utility for a target without service throughput"),
		INT_SPEC(placement, duplicate_first_score, "placement.duplicate_first_score", 0, 100, "score", "placement", "Redundancy utility for the first building of a type"),
		INT_SPEC(placement, duplicate_score_scale, "placement.duplicate_score_scale", 0, 200, "score", "placement", "Redundancy utility divided by existing buildings of the same type"),
		INT_SPEC(placement, resource_distance_weight, "placement.resource_distance_weight", 0, 50, "weight", "placement", "Resource-quality loss per tile of travel"),
		INT_SPEC(placement, food_zone_radius, "placement.food_zone_radius", 0, 8, "tiles", "placement", "Radius around valuable corn and fertile expansion cells reserved for food infrastructure"),
		INT_SPEC(placement, inner_food_zone_multiplier, "placement.inner_food_zone_multiplier", 0, 200, "percent", "placement", "Food-zone penalty multiplier for schools and other inner strategic buildings"),
		INT_SPEC(placement, hospital_food_zone_multiplier, "placement.hospital_food_zone_multiplier", 0, 200, "percent", "placement", "Food-zone penalty multiplier for hospitals"),
		INT_SPEC(placement, tower_food_zone_multiplier, "placement.tower_food_zone_multiplier", 0, 200, "percent", "placement", "Food-zone penalty multiplier for defensive towers"),
		INT_SPEC(placement, tower_critical_distance_weight, "placement.tower_critical_distance_weight", 0, 50, "weight", "placement", "Tower location penalty per tile from critical buildings"),
		INT_SPEC(placement, tower_spacing_target, "placement.tower_spacing_target", 0, 64, "tiles", "placement", "Preferred distance between defensive towers"),
		INT_SPEC(placement, tower_spacing_weight, "placement.tower_spacing_weight", 0, 50, "weight", "placement", "Tower location penalty per tile away from preferred spacing"),
		INT_SPEC(placement, tower_threat_target, "placement.tower_threat_target", 0, 100, "score", "placement", "Preferred local threat level for tower placement"),
		INT_SPEC(placement, tower_threat_weight, "placement.tower_threat_weight", 0, 50, "weight", "placement", "Tower location penalty per point away from preferred threat"),
		INT_SPEC(placement, labor_scale, "placement.labor_scale", 0, 50, "weight", "placement", "Construction labor utility cost per assigned worker"),
		INT_SPEC(placement, downtime_worker_scale, "placement.downtime_worker_scale", 1, 50, "weight", "placement", "Worker effectiveness scale reducing upgrade downtime"),
		INT_SPEC(placement, artery_length_scale, "placement.artery_length_scale", 0, 50, "weight", "placement", "Utility cost per new circulation-artery tile"),
		INT_SPEC(placement, repair_base_demand, "placement.repair_base_demand", 0, 100, "score", "placement", "Base unmet-demand utility assigned to repairs"),
		INT_SPEC(placement, action_timeout_ticks, "placement.action_timeout_ticks", 1, 1000000, "ticks", "placement", "Maximum wait for an issued placement action to appear in the world"),
		INT_SPEC(placement, route_clearable_resource_cost, "placement.route_clearable_resource_cost", 0, 1000, "cost", "placement", "Route-search penalty for crossing a clearable resource"),
		INT_SPEC(placement, route_farm_cost, "placement.route_farm_cost", 0, 1000, "cost", "placement", "Route-search penalty for crossing productive farmland"),
		INT_SPEC(placement, route_fertility_cost, "placement.route_fertility_cost", 0, 1000, "cost", "placement", "Route-search penalty for crossing any fertile tile"),
		INT_SPEC(placement, guard_area_protectedness, "placement.guard_area_protectedness", 0, 100, "score", "placement", "Base protection score inside an active guard area"),
		INT_SPEC(placement, baseline_protectedness, "placement.baseline_protectedness", 0, 100, "score", "placement", "Base protection score outside active guard areas"),
		INT_SPEC(placement, barrier_protection_radius, "placement.barrier_protection_radius", 0, 64, "tiles", "placement", "Radius over which planned barrier points protect building sites"),
		INT_SPEC(placement, barrier_protectedness, "placement.barrier_protectedness", 0, 100, "score", "placement", "Protection score supplied by planned barrier points"),
		INT_SPEC(placement, enemy_threat_radius, "placement.enemy_threat_radius", 0, 64, "tiles", "placement", "Radius over which a visible enemy building raises placement threat"),
		INT_SPEC(placement, enemy_threat_base, "placement.enemy_threat_base", 0, 500, "score", "placement", "Threat score at a visible enemy building"),
		INT_SPEC(placement, enemy_threat_falloff, "placement.enemy_threat_falloff", 0, 100, "score_per_tile", "placement", "Enemy-building threat removed per tile of distance"),
		INT_SPEC(placement, building_protection_radius, "placement.building_protection_radius", 0, 64, "tiles", "placement", "Radius over which an owned building protects nearby sites"),
		INT_SPEC(placement, building_protection_base, "placement.building_protection_base", 0, 500, "score", "placement", "Protection score at an owned building"),
		INT_SPEC(placement, building_protection_falloff, "placement.building_protection_falloff", 0, 100, "score_per_tile", "placement", "Owned-building protection removed per tile of distance"),

		BOOL_SPEC(colonization, enabled, "colonization.enabled", "boolean", "colonization", "Enable independent new-food colony construction"),
		INT_SPEC(colonization, minimum_anchor_distance, "colonization.minimum_anchor_distance", 0, 256, "tiles", "colonization", "Minimum wrapped distance from an existing or planned swarm or inn"),
		INT_SPEC(colonization, maximum_threat, "colonization.maximum_threat", 0, 500, "score", "colonization", "Maximum threat allowed on any colonial parcel tile"),
		INT_SPEC(colonization, conquered_merge_radius, "colonization.conquered_merge_radius", 0, 256, "tiles", "colonization", "Distance within which cleared enemy sites merge"),
		INT_SPEC(colonization, conquered_memory_ticks, "colonization.conquered_memory_ticks", 1, 1000000, "ticks", "colonization", "Lifetime of cleared enemy settlement memory"),

		INT_SPEC(colonization, minimum_new_food, "colonization.minimum_new_food", 1, 10000, "food_capacity", "colonization", "Minimum additional reachable fertility-weighted food, excluding established and reserved claims"),
		INT_SPEC(colonization, minimum_value, "colonization.minimum_value", 1, 10000, "food_per_100_cost", "colonization", "Minimum new food per 100 establishment cost units (materials plus builder travel)"),

		BOOL_SPEC(reactive_defense, enabled, "defense.reactive.enabled", "enabled", "defense", "Enable reactive local war-flag defense"),
		INT_SPEC(reactive_defense, flag_radius, "defense.reactive.flag_radius", 1, 32, "tiles", "defense", "Reactive-defense flag radius"),
		INT_SPEC(reactive_defense, move_radius, "defense.reactive.move_radius", 1, 256, "tiles", "defense", "Distance within which an existing defense flag may move"),
		INT_SPEC(reactive_defense, move_deadband, "defense.reactive.move_deadband", 0, 32, "tiles", "defense", "Distance a reactive-defense target may shift without moving its existing flag"),
		INT_SPEC(reactive_defense, unit_cap, "defense.reactive.unit_cap", 0, 20, "units", "defense", "Maximum units assigned to one reactive-defense cluster"),
		INT_SPEC(reactive_defense, advantage_min, "defense.reactive.advantage_min", 0, 1000, "units", "defense", "Minimum friendly advantage assigned above visible attackers"),
		INT_SPEC(reactive_defense, advantage_percent, "defense.reactive.advantage_percent", 0, 200, "percent", "defense", "Percentage advantage assigned above visible attackers"),

		BOOL_SPEC(tactics, enabled, "tactics.enabled", "enabled", "tactics", "Enable the warrior mission executor"),
		BOOL_SPEC(tactics, siege_enabled, "tactics.siege_enabled", "enabled", "tactics", "Enable warrior sieges against enemy buildings"),
		BOOL_SPEC(tactics, dig_out_enabled, "tactics.dig_out_enabled", "enabled", "tactics", "Enable attack-route clearing against sealed targets"),
		BOOL_SPEC(tactics, failed_target_quarantine_enabled, "tactics.failed_target_quarantine_enabled", "enabled", "tactics", "Quarantine targets after fast failed attacks"),
		BOOL_SPEC(tactics, siege_target_lock_enabled, "tactics.siege_target_lock_enabled", "enabled", "tactics", "Retain a target team after productive siege progress"),
		INT_SPEC(tactics, review_interval_ticks, "tactics.review_interval_ticks", 1, 1000000, "ticks", "tactics", "Interval between warrior mission reviews"),
		INT_SPEC(tactics, rally_flag_radius, "tactics.rally_flag_radius", 1, 32, "tiles", "tactics", "Warrior muster and withdrawal flag radius"),
		INT_SPEC(tactics, siege_flag_radius, "tactics.siege_flag_radius", 1, 32, "tiles", "tactics", "Siege engagement flag radius"),
		INT_SPEC(tactics, siege_min_force, "tactics.siege_min_force", 1, 20, "units", "tactics", "Minimum staged warriors for a siege"),
		INT_SPEC(tactics, siege_muster_percent, "tactics.siege_muster_percent", 1, 100, "percent", "tactics", "Percentage of requested siege warriors required on site; no absolute muster minimum"),
		INT_SPEC(tactics, siege_strength_percent, "tactics.siege_strength_percent", 1, 400, "percent", "tactics", "Required power relative to target-area opposition"),
		INT_SPEC(tactics, siege_casualty_percent, "tactics.siege_casualty_percent", 1, 100, "percent", "tactics", "Siege losses that force withdrawal"),
		INT_SPEC(tactics, siege_local_threat_radius, "tactics.siege_local_threat_radius", 1, 64, "tiles", "tactics", "Radius used to estimate siege opposition"),
		INT_SPEC(tactics, siege_target_lock_ticks, "tactics.siege_target_lock_ticks", 0, 1000000, "ticks", "tactics", "Target-team commitment after siege progress"),
		INT_SPEC(tactics, target_swarm_value, "tactics.target_swarm_value", 0, 1000, "score", "tactics", "Siege value of a swarm"),
		INT_SPEC(tactics, target_food_value, "tactics.target_food_value", 0, 1000, "score", "tactics", "Siege value of an inn"),
		INT_SPEC(tactics, target_barracks_value, "tactics.target_barracks_value", 0, 1000, "score", "tactics", "Siege value of a barracks"),
		INT_SPEC(tactics, target_school_value, "tactics.target_school_value", 0, 1000, "score", "tactics", "Siege value of a school"),
		INT_SPEC(tactics, target_hospital_value, "tactics.target_hospital_value", 0, 1000, "score", "tactics", "Siege value of a hospital"),
		INT_SPEC(tactics, target_default_value, "tactics.target_default_value", 0, 1000, "score", "tactics", "Siege value of any other building"),
		INT_SPEC(tactics, target_construction_bonus, "tactics.target_construction_bonus", 0, 1000, "score", "tactics", "Additional siege value of a building site"),
		INT_SPEC(tactics, target_tower_penalty, "tactics.target_tower_penalty", 0, 1000, "score", "tactics", "Penalty per known tower near a siege target"),
		INT_SPEC(tactics, route_distance_weight, "tactics.route_distance_weight", 0, 100, "weight", "tactics", "Siege route-distance penalty"),
		INT_SPEC(tactics, uncertainty_percent, "tactics.uncertainty_percent", 0, 100, "percent", "tactics", "Unseen target force reserved under weak intelligence"),
		INT_SPEC(tactics, failed_target_max_duration_ticks, "tactics.failed_target_max_duration_ticks", 0, 1000000, "ticks", "tactics", "Maximum duration treated as a fast failed attack for quarantine"),
		INT_SPEC(tactics, failed_target_quarantine_ticks, "tactics.failed_target_quarantine_ticks", 0, 1000000, "ticks", "tactics", "Time before a fast failed attack target may be selected again"),

		BOOL_SPEC(raiding, enabled, "raiding.enabled", "enabled", "raiding", "Enable warrior raids against visible worker clusters"),
		INT_SPEC(raiding, population_min, "raiding.population_min", 0, 1000, "units", "raiding", "Population required before warrior raids"),
		INT_SPEC(raiding, worker_min, "raiding.worker_min", 1, 100, "workers", "raiding", "Minimum currently visible workers in a raid cluster"),
		INT_SPEC(raiding, cluster_radius, "raiding.cluster_radius", 1, 64, "tiles", "raiding", "Distance joining visible workers into a cluster"),
		INT_SPEC(raiding, threat_radius, "raiding.threat_radius", 1, 64, "tiles", "raiding", "Escort detection radius around a worker cluster"),
		INT_SPEC(raiding, force_bonus, "raiding.force_bonus", 0, 20, "units", "raiding", "Warriors added above visible worker count"),
		INT_SPEC(raiding, min_force, "raiding.min_force", 1, 20, "units", "raiding", "Minimum warrior raid force"),
		INT_SPEC(raiding, max_force, "raiding.max_force", 1, 20, "units", "raiding", "Maximum warrior raid force"),
		INT_SPEC(raiding, flag_radius, "raiding.flag_radius", 1, 8, "tiles", "raiding", "Tight worker-raid engagement radius"),
		INT_SPEC(raiding, muster_percent, "raiding.muster_percent", 1, 100, "percent", "raiding", "Percentage of requested raiders required on site; no absolute muster minimum"),
		INT_SPEC(raiding, muster_timeout_ticks, "raiding.muster_timeout_ticks", 1, 1000000, "ticks", "raiding", "Maximum raid muster duration"),
		INT_SPEC(raiding, contact_ttl_ticks, "raiding.contact_ttl_ticks", 1, 1000000, "ticks", "raiding", "Maximum pursuit age of a last-known worker cluster"),
		INT_SPEC(raiding, max_engagement_ticks, "raiding.max_engagement_ticks", 1, 1000000, "ticks", "raiding", "Maximum raid engagement duration"),
		INT_SPEC(raiding, casualty_percent, "raiding.casualty_percent", 1, 100, "percent", "raiding", "Raid losses that force withdrawal"),
		INT_SPEC(raiding, survivor_min, "raiding.survivor_min", 1, 20, "units", "raiding", "Minimum useful surviving raid force"),
		INT_SPEC(raiding, cooldown_ticks, "raiding.cooldown_ticks", 0, 1000000, "ticks", "raiding", "Cooldown after a warrior raid"),
		INT_SPEC(raiding, defender_min, "raiding.defender_min", 1, 20, "units", "raiding", "Minimum visible escorts that make a raid unsafe"),
		INT_SPEC(raiding, defender_percent, "raiding.defender_percent", 1, 200, "percent", "raiding", "Escort strength relative to launched raiders that forces withdrawal"),
		INT_SPEC(raiding, building_buffer, "raiding.building_buffer", 0, 32, "tiles", "raiding", "Required separation from known enemy buildings"),
		INT_SPEC(raiding, tower_buffer, "raiding.tower_buffer", 0, 32, "tiles", "raiding", "Additional separation from known towers"),
		INT_SPEC(raiding, retarget_margin, "raiding.retarget_margin", 0, 1000, "score", "raiding", "Score improvement required to retarget a raid"),
		INT_SPEC(raiding, worker_weight, "raiding.worker_weight", 0, 1000, "weight", "raiding", "Raid value per visible worker"),
		INT_SPEC(raiding, harvesting_bonus, "raiding.harvesting_bonus", 0, 1000, "score", "raiding", "Raid bonus per actively harvesting worker"),
		INT_SPEC(raiding, carrying_bonus, "raiding.carrying_bonus", 0, 1000, "score", "raiding", "Raid bonus per resource-carrying worker"),
		INT_SPEC(raiding, resource_weight, "raiding.resource_weight", 0, 1000, "weight", "raiding", "Multiplier for observed resource value"),
		INT_SPEC(raiding, defender_penalty, "raiding.defender_penalty", 0, 1000, "score", "raiding", "Raid penalty per visible escort"),
		INT_SPEC(raiding, route_distance_weight, "raiding.route_distance_weight", 0, 100, "weight", "raiding", "Raid route-distance penalty"),
		INT_SPEC(raiding, outskirts_weight, "raiding.outskirts_weight", 0, 100, "weight", "raiding", "Bonus for workers far outside known enemy buildings"),
		INT_SPEC(raiding, allied_pressure_bonus, "raiding.allied_pressure_bonus", 0, 1000, "score", "raiding", "Raid bonus for a target under visible allied warrior pressure"),
		INT_SPEC(raiding, ffa_third_party_penalty, "raiding.ffa_third_party_penalty", 0, 1000, "score", "raiding", "FFA penalty while another enemy threatens the colony"),
		INT_SPEC(raiding, food_resource_value, "raiding.food_resource_value", 0, 100, "value", "raiding", "Economic value of corn or algae carried by a worker"),
		INT_SPEC(raiding, material_resource_value, "raiding.material_resource_value", 0, 100, "value", "raiding", "Economic value of wood or stone carried by a worker"),
		INT_SPEC(raiding, fruit_resource_value, "raiding.fruit_resource_value", 0, 100, "value", "raiding", "Economic value of fruit carried by a worker"),
		INT_SPEC(raiding, other_resource_value, "raiding.other_resource_value", 0, 100, "value", "raiding", "Economic value of any other carried resource"),

		BOOL_SPEC(teamplay, enabled, "teamplay.enabled", "enabled", "teamplay", "Enable all ally-aware tactical behavior"),
		BOOL_SPEC(teamplay, pressure_coordination_enabled, "teamplay.pressure_coordination_enabled", "enabled", "teamplay", "Use visible allied pressure in raid and siege scoring"),
		BOOL_SPEC(teamplay, defense_enabled, "teamplay.defense_enabled", "enabled", "teamplay", "Allow surplus warriors to defend a visibly threatened ally"),
		INT_SPEC(teamplay, allied_pressure_radius, "teamplay.allied_pressure_radius", 1, 64, "tiles", "teamplay", "Radius used to measure visible allied warrior pressure"),
		INT_SPEC(teamplay, defense_min_force, "teamplay.defense_min_force", 1, 20, "units", "teamplay", "Minimum warriors assigned to an allied relief mission"),
		INT_SPEC(teamplay, defense_strength_percent, "teamplay.defense_strength_percent", 1, 400, "percent", "teamplay", "Desired combined allied power relative to visible attackers"),
		INT_SPEC(teamplay, defense_base_score, "teamplay.defense_base_score", 0, 1000, "score", "teamplay", "Base utility of defending a threatened ally"),
		INT_SPEC(teamplay, defense_threat_weight, "teamplay.defense_threat_weight", 0, 1000, "score", "teamplay", "Relief utility per visible enemy warrior"),
		INT_SPEC(teamplay, defense_under_attack_bonus, "teamplay.defense_under_attack_bonus", 0, 1000, "score", "teamplay", "Relief utility bonus when the protected asset is under attack"),
		INT_SPEC(teamplay, defense_unit_value, "teamplay.defense_unit_value", 0, 1000, "score", "teamplay", "Relief value of a threatened allied unit"),
		INT_SPEC(teamplay, defense_route_distance_weight, "teamplay.defense_route_distance_weight", 0, 100, "weight", "teamplay", "Relief route-distance penalty"),
		INT_SPEC(teamplay, defense_contact_ttl_ticks, "teamplay.defense_contact_ttl_ticks", 1, 1000000, "ticks", "teamplay", "Time a relief mission may pursue its last visible threat"),
		INT_SPEC(teamplay, defense_max_engagement_ticks, "teamplay.defense_max_engagement_ticks", 1, 1000000, "ticks", "teamplay", "Maximum relief mission duration"),
		INT_SPEC(teamplay, defense_cooldown_ticks, "teamplay.defense_cooldown_ticks", 0, 1000000, "ticks", "teamplay", "Cooldown after an allied relief mission"),
		INT_SPEC(teamplay, defense_follow_radius, "teamplay.defense_follow_radius", 1, 64, "tiles", "teamplay", "Distance a relief target may move without retarget hysteresis"),
		INT_SPEC(teamplay, defense_retarget_margin, "teamplay.defense_retarget_margin", 0, 1000, "score", "teamplay", "Score improvement required for a distant relief retarget"),
		INT_SPEC(teamplay, siege_player_pressure_bonus, "teamplay.siege_player_pressure_bonus", 0, 1000, "score", "teamplay", "Siege bonus for targeting an opponent engaged by visible allied warriors"),
		INT_SPEC(teamplay, siege_building_pressure_bonus, "teamplay.siege_building_pressure_bonus", 0, 1000, "score", "teamplay", "Siege bonus for a building near visible allied warriors"),

		BOOL_SPEC(explorer_campaign, enabled, "explorer_campaign.enabled", "enabled", "explorer_campaign", "Enable explorer strikes against enemy warrior groups"),
		INT_SPEC(explorer_campaign, trained_min, "explorer_campaign.trained_min", 0, 1000, "units", "explorer_campaign", "Trained explorers required to activate a campaign"),
		INT_SPEC(explorer_campaign, large_economy_trained_min, "explorer_campaign.large_economy_trained_min", 0, 1000, "units", "explorer_campaign", "Trained explorers required when the economy can sustain an early campaign"),
		INT_SPEC(explorer_campaign, multi_flag_trained_min, "explorer_campaign.multi_flag_trained_min", 0, 1000, "units", "explorer_campaign", "Trained explorers required for multiple campaign flags"),
		INT_SPEC(explorer_campaign, max_flags, "explorer_campaign.max_flags", 0, 32, "flags", "explorer_campaign", "Maximum simultaneous explorer campaign flags"),
		INT_SPEC(explorer_campaign, units_per_flag, "explorer_campaign.units_per_flag", 0, 20, "units", "explorer_campaign", "Explorers assigned to each campaign flag"),

		BOOL_SPEC(fruit, enabled, "fruit.enabled", "enabled", "fruit", "Enable fruit exploration flags and inn sharing"),
		INT_SPEC(fruit, population_min, "fruit.population_min", 0, 1000, "units", "fruit", "Population required for fruit operations"),
		INT_SPEC(fruit, units_per_flag, "fruit.units_per_flag", 0, 20, "units", "fruit", "Explorers assigned to each fruit flag"),
		INT_SPEC(fruit, flag_radius, "fruit.flag_radius", 1, 32, "tiles", "fruit", "Fruit flag radius"),

		BOOL_SPEC(reconnaissance, enabled, "recon.enabled", "enabled", "recon", "Enable strategic reconnaissance observation, memory, and missions"),
		BOOL_SPEC(reconnaissance, scouting_missions_enabled, "recon.scouting_missions_enabled", "enabled", "recon", "Enable contact and frontier reconnaissance flags"),
		BOOL_SPEC(reconnaissance, economic_watch_enabled, "recon.economic_watch_enabled", "enabled", "recon", "Enable late-game economic watch patrols"),
		BOOL_SPEC(reconnaissance, force_memory_enabled, "recon.force_memory_enabled", "enabled", "recon", "Retain and decay previously observed enemy force strength"),
		INT_SPEC(reconnaissance, explorer_attack_warning_threshold, "recon.attack_warning_threshold", 0, 1000, "units", "recon", "Visible enemy attack explorers that trigger warning"),
		INT_SPEC(reconnaissance, explorer_colony_warning_threshold, "recon.colony_warning_threshold", 0, 1000, "units", "recon", "Visible colony explorer threat that triggers warning"),
		INT_SPEC(reconnaissance, offense_explorer_population_divisor, "recon.offense_population_divisor", 1, 1000, "units", "recon", "Population divisor for offensive explorers"),
		INT_SPEC(reconnaissance, memory_horizon_ticks, "recon.memory_horizon_ticks", 1, 1000000, "ticks", "recon", "Age at which a remembered force estimate reaches zero"),
		INT_SPEC(reconnaissance, force_memory_hold_ticks, "recon.force_memory_hold_ticks", 0, 1000000, "ticks", "recon", "Age through which a remembered force retains full strength"),
		INT_SPEC(reconnaissance, force_sample_interval_ticks, "recon.force_sample_interval_ticks", 1, 1000000, "ticks", "recon", "Interval between lightweight visible-force samples"),
		INT_SPEC(reconnaissance, force_sample_phase_offset_ticks, "recon.force_sample_phase_offset_ticks", 0, 1000000, "ticks", "recon", "Phase offset for lightweight visible-force samples"),
		INT_SPEC(reconnaissance, stale_contact_age_ticks, "recon.stale_contact_age_ticks", 1, 1000000, "ticks", "recon", "Age at which contact becomes stale"),
		INT_SPEC(reconnaissance, mission_review_interval_ticks, "recon.mission_review_interval_ticks", 1, 1000000, "ticks", "recon", "Interval between mission reviews"),
		INT_SPEC(reconnaissance, flag_radius_tiles, "recon.flag_radius_tiles", 1, 256, "tiles", "recon", "Reconnaissance flag radius"),
		INT_SPEC(reconnaissance, economic_watch_population_min, "recon.economic_watch_population_min", 0, 1000, "units", "recon", "Population required for late-game economic watch"),
		INT_SPEC(reconnaissance, economic_watch_radius_min, "recon.economic_watch_radius_min", 0, 256, "tiles", "recon", "Minimum distance from a known enemy building for economic watch"),
		INT_SPEC(reconnaissance, economic_watch_radius_max, "recon.economic_watch_radius_max", 1, 256, "tiles", "recon", "Maximum distance from a known enemy building for economic watch"),
		INT_SPEC(reconnaissance, economic_watch_revisit_ticks, "recon.economic_watch_revisit_ticks", 1, 1000000, "ticks", "recon", "Interval between economic-watch patrol points"),
		INT_SPEC(reconnaissance, economic_watch_max_missions, "recon.economic_watch_max_missions", 0, 8, "flags", "recon", "Maximum late-game economic-watch missions"),
		INT_SPEC(reconnaissance, economic_watch_patrol_sites, "recon.economic_watch_patrol_sites", 1, 256, "sites", "recon", "High-value resource sites rotated by each economic-watch patrol"),
		INT_SPEC(reconnaissance, swarm_building_value, "recon.swarm_building_value", 0, 100, "score", "recon", "Strategic reconnaissance value of a known enemy swarm"),
		INT_SPEC(reconnaissance, inn_building_value, "recon.inn_building_value", 0, 100, "score", "recon", "Strategic reconnaissance value of a known enemy inn"),
		INT_SPEC(reconnaissance, barracks_building_value, "recon.barracks_building_value", 0, 100, "score", "recon", "Strategic reconnaissance value of a known enemy barracks"),
		INT_SPEC(reconnaissance, school_building_value, "recon.school_building_value", 0, 100, "score", "recon", "Strategic reconnaissance value of a known enemy school"),
		INT_SPEC(reconnaissance, default_building_value, "recon.default_building_value", 0, 100, "score", "recon", "Strategic reconnaissance value of other enemy buildings"),
		INT_SPEC(reconnaissance, building_count_weight, "recon.building_count_weight", 0, 50, "weight", "recon", "Opponent score added per known building"),
		INT_SPEC(reconnaissance, unreachable_penalty, "recon.unreachable_penalty", 0, 500, "score", "recon", "Opponent penalty when no known building is reachable"),
		INT_SPEC(reconnaissance, finishing_bonus, "recon.finishing_bonus", 0, 500, "score", "recon", "Opponent score bonus for a nearly eliminated target"),
		INT_SPEC(reconnaissance, finishing_building_penalty, "recon.finishing_building_penalty", 0, 100, "weight", "recon", "Finishing bonus removed per remaining target building"),
		INT_SPEC(reconnaissance, saturation_percent, "recon.saturation_percent", 0, 100, "percent", "recon", "Local explored share at which a frontier mission is considered saturated"),
		INT_SPEC(reconnaissance, retask_percent, "recon.retask_percent", 100, 500, "percent", "recon", "Required new-objective score as a percentage of the current mission score"),
		INT_SPEC(reconnaissance, mission_population_divisor, "recon.mission_population_divisor", 1, 1000, "units", "recon", "Population per extra reconnaissance mission"),

		BOOL_SPEC(farming, enabled, "farming.enabled", "enabled", "farming", "Enable all strategic farming and land-clearing behavior"),
		BOOL_SPEC(farming, farm_protection_enabled, "farming.farm_protection_enabled", "enabled", "farming", "Protect selected wheat and wood growth cells"),
		BOOL_SPEC(farming, barrier_topology_enabled, "farming.barrier_topology_enabled", "enabled", "farming", "Analyze and maintain strategic farming barriers and gates"),
		BOOL_SPEC(farming, coastal_porosity_enabled, "farming.coastal_porosity_enabled", "enabled", "farming", "Open alternating passive cells when a coastal farm envelope seals an island"),
		BOOL_SPEC(farming, gate_clearing_enabled, "farming.gate_clearing_enabled", "enabled", "farming", "Clear resources blocking strategic gates"),
		BOOL_SPEC(farming, maintenance_clearing_enabled, "farming.maintenance_clearing_enabled", "enabled", "farming", "Maintain clearing areas for parcels, gates, and firebreaks"),
		BOOL_SPEC(farming, resource_preserving_circulation_enabled, "farming.resource_preserving_circulation_enabled", "enabled", "farming", "Grandfather existing wheat and wood in new building circulation zones"),
		BOOL_SPEC(farming, wheat_invasion_clearing_enabled, "farming.wheat_invasion_clearing_enabled", "enabled", "farming", "Clear wood that invades protected wheat boundaries"),
		BOOL_SPEC(farming, wood_firebreak_enabled, "farming.wood_firebreak_enabled", "enabled", "farming", "Maintain the fertility-banded wood firebreak"),
		BOOL_SPEC(farming, proactive_clearing_enabled, "farming.proactive_clearing_enabled", "enabled", "farming", "Launch proactive wood-clearing campaigns"),
		INT_SPEC(farming, normal_interval_ticks, "farming.normal_interval_ticks", 1, 1000000, "ticks", "farming", "Normal farming evaluation interval"),
		INT_SPEC(farming, urgent_interval_ticks, "farming.urgent_interval_ticks", 1, 1000000, "ticks", "farming", "Urgent farming evaluation interval"),
		INT_SPEC(farming, management_radius, "farming.management_radius", 0, 128, "tiles", "farming", "Farm and wood firebreak radius from allied physical buildings; zero disables cutoff"),
		INT_SPEC(farming, wheat_fertility_min, "farming.wheat_fertility_min", 0, 65536, "fertility", "farming", "Minimum fertility for protected wheat seeds and bridges"),
		INT_SPEC(farming, wood_fertility_base_percent, "farming.wood_fertility_base_percent", 0, 100, "percent", "farming", "Base retained wood fertility percentage"),
		INT_SPEC(farming, wood_fertility_pressure_percent, "farming.wood_fertility_pressure_percent", 0, 100, "percent", "farming", "Additional retained fertility under pressure"),
		INT_SPEC(farming, wood_firebreak_fertility_min_percent, "farming.wood_firebreak_fertility_min_percent", 0, 100, "percent", "farming", "Minimum fertility included in the persistent wood-clearing firebreak"),
		INT_SPEC(farming, wood_firebreak_fertility_max_percent, "farming.wood_firebreak_fertility_max_percent", 0, 100, "percent", "farming", "Maximum fertility included in the persistent wood-clearing firebreak"),
		INT_SPEC(farming, wood_pressure_base, "farming.wood_pressure_base", 0, 100, "score", "farming", "Base wood-clearing pressure"),
		INT_SPEC(farming, wood_pressure_space_divisor, "farming.wood_pressure_space_divisor", 1, 100, "score", "farming", "Space contribution divisor"),
		INT_SPEC(farming, wood_pressure_supply_divisor, "farming.wood_pressure_supply_divisor", 1, 100, "score", "farming", "Wood supply contribution divisor"),
		INT_SPEC(farming, wood_pressure_construction_divisor, "farming.wood_pressure_construction_divisor", 1, 100, "score", "farming", "Construction failure contribution divisor"),
		INT_SPEC(farming, wood_pressure_growth_divisor, "farming.wood_pressure_growth_divisor", 1, 100, "score", "farming", "Growth demand contribution divisor"),
		INT_SPEC(farming, economic_envelope_radius_tiles, "farming.economic_envelope_radius_tiles", 1, 256, "tiles", "farming", "Radius of the farming economic envelope"),
		INT_SPEC(farming, wood_supply_scale, "farming.wood_supply_scale", 1, 2000, "weight", "farming", "Accessible-wood contribution to the wood-supply score"),
		INT_SPEC(farming, wood_supply_population_offset, "farming.wood_supply_population_offset", 0, 1000, "units", "farming", "Population offset damping wood supply in small colonies"),
		INT_SPEC(farming, construction_failure_pressure, "farming.construction_failure_pressure", 0, 100, "score", "farming", "Wood-clearing pressure per recent placement failure"),
		INT_SPEC(farming, proactive_workers_min, "farming.proactive_workers_min", 0, 1000, "workers", "farming", "Workers required before proactive land clearing"),
		INT_SPEC(farming, proactive_cooldown_ticks, "farming.proactive_cooldown_ticks", 0, 1000000, "ticks", "farming", "Cooldown between proactive clearing campaigns"),
		INT_SPEC(farming, proactive_duration_ticks, "farming.proactive_duration_ticks", 1, 1000000, "ticks", "farming", "Maximum duration of one proactive clearing campaign"),
		INT_SPEC(farming, proactive_quota, "farming.proactive_quota", 0, 1000, "tiles", "farming", "Wood tiles targeted by one proactive clearing campaign"),
		INT_SPEC(farming, proactive_failure_threshold, "farming.proactive_failure_threshold", 0, 100, "failures", "farming", "Recent placement failures that create clearing pressure"),
		INT_SPEC(farming, proactive_building_threshold, "farming.proactive_building_threshold", 0, 1000, "buildings", "farming", "Buildings required before recurring wood-pressure clearing"),
		INT_SPEC(farming, proactive_start_tick, "farming.proactive_start_tick", 0, 1000000, "ticks", "farming", "Earliest tick for proactive land clearing"),
		INT_SPEC(farming, gate_clearing_radius, "farming.gate_clearing_radius", 1, 8, "tiles", "farming", "Radius of a strategic-gate clearing flag"),
		INT_SPEC(farming, gate_clearing_workers_min, "farming.gate_clearing_workers_min", 0, 1000, "workers", "farming", "Workers required before clearing costly resources from a non-emergency strategic gate"),
		INT_SPEC(farming, gate_relocation_penalty_cap, "farming.gate_relocation_penalty_cap", 0, 100, "resource_units", "farming", "Maximum resource-burden-equivalent penalty for moving strategic gates away from their previous positions"),
		INT_SPEC(farming, urgent_space_threshold, "farming.urgent_space_threshold", 0, 100, "score", "farming", "Space capacity below which farming review becomes urgent"),

		INT_SPEC(scoring, posture_switch_margin, "scoring.posture_switch_margin", 0, 200, "score", "scoring", "Utility margin required to switch posture"),
		INT_SPEC(scoring, target_switch_margin, "scoring.target_switch_margin", 0, 1000, "score", "scoring", "Score margin required to switch targets"),
		INT_SPEC(scoring, target_reachable_weight, "scoring.target_reachable_weight", 0, 100, "weight", "scoring", "Weight for reachable enemy buildings"),
		INT_SPEC(scoring, target_warrior_weight, "scoring.target_warrior_weight", -100, 100, "weight", "scoring", "Weight for estimated enemy warriors"),
		INT_SPEC(scoring, target_distance_bias, "scoring.target_distance_bias", 0, 1000, "score", "scoring", "Distance score bias for targets"),
		INT_SPEC(scoring, priority_food_headroom_divisor, "scoring.priority_food_headroom_divisor", 1, 50, "score", "scoring", "Food-headroom contribution divisor for inn priority"),
		INT_SPEC(scoring, priority_abundance_swarm_bonus, "scoring.priority_abundance_swarm_bonus", 0, 200, "score", "scoring", "Swarm priority bonus during an abundance surge"),
		INT_SPEC(scoring, priority_mobility_divisor, "scoring.priority_mobility_divisor", 1, 50, "score", "scoring", "Mobility-demand contribution divisor for pool priority"),
		INT_SPEC(scoring, priority_pool_swimmer_threshold, "scoring.priority_pool_swimmer_threshold", 0, 1000, "units", "scoring", "Swimming warriors below which abundance raises pool priority"),
		INT_SPEC(scoring, priority_pool_swimmer_bonus, "scoring.priority_pool_swimmer_bonus", 0, 200, "score", "scoring", "Pool priority bonus while swimming-warrior capacity is low"),
		INT_SPEC(scoring, priority_pool_population_min, "scoring.priority_pool_population_min", 0, 1000, "units", "scoring", "Population required for the persistent amphibious pool-priority bonus"),
		INT_SPEC(scoring, priority_pool_count_max, "scoring.priority_pool_count_max", 0, 32, "buildings", "scoring", "Pool count below which the amphibious-network bonus applies"),
		INT_SPEC(scoring, priority_pool_network_bonus, "scoring.priority_pool_network_bonus", 0, 200, "score", "scoring", "Pool priority bonus for completing an amphibious network"),
		INT_SPEC(scoring, priority_abundance_school_bonus, "scoring.priority_abundance_school_bonus", 0, 200, "score", "scoring", "School priority bonus during an abundance surge"),
		INT_SPEC(scoring, priority_racetrack_penalty, "scoring.priority_racetrack_penalty", 0, 200, "score", "scoring", "Fixed reduction applied to racetrack priority"),
		INT_SPEC(scoring, priority_threat_cap, "scoring.priority_threat_cap", 0, 500, "score", "scoring", "Maximum visible-threat contribution to barracks priority"),
		INT_SPEC(scoring, priority_threat_weight, "scoring.priority_threat_weight", 0, 50, "weight", "scoring", "Barracks priority gained per visible colony threat"),
		INT_SPEC(scoring, priority_healing_bonus, "scoring.priority_healing_bonus", 0, 200, "score", "scoring", "Hospital priority bonus while units need healing"),
		INT_SPEC(scoring, priority_large_tower_bonus, "scoring.priority_large_tower_bonus", 0, 200, "score", "scoring", "Tower priority bonus when a large economy faces prestige"),
		INT_SPEC(scoring, priority_tower_active_bonus, "scoring.priority_tower_active_bonus", 0, 200, "score", "scoring", "Tower priority bonus while explorer defense is active"),
		INT_SPEC(scoring, priority_tower_emergency_bonus, "scoring.priority_tower_emergency_bonus", 0, 200, "score", "scoring", "Additional tower priority during an explorer emergency"),

		INT_SPEC(scheduling, normal_posture_commitment_ticks, "scheduling.posture_commitment_ticks", 1, 1000000, "ticks", "scheduling", "Normal minimum posture commitment"),
		INT_SPEC(scheduling, campaign_stall_ticks, "scheduling.campaign_stall_ticks", 1, 1000000, "ticks", "scheduling", "Campaign progress stall timeout"),
		INT_SPEC(scheduling, campaign_retreat_cooldown_ticks, "scheduling.campaign_retreat_cooldown_ticks", 0, 1000000, "ticks", "scheduling", "Cooldown after campaign retreat"),
		INT_SPEC(scheduling, preemptive_defense_recompute_ticks, "scheduling.preemptive_defense_recompute_ticks", 1, 1000000, "ticks", "scheduling", "Preemptive topology recompute interval"),
		INT_SPEC(scheduling, barrier_topology_interval_ticks, "scheduling.barrier_topology_interval_ticks", 1, 1000000, "ticks", "scheduling", "Barrier topology recompute interval"),
		INT_SPEC(scheduling, strategy_interval_ticks, "scheduling.strategy_interval_ticks", 1, 1000000, "ticks", "scheduling", "Interval between strategic director evaluations"),
		INT_SPEC(scheduling, strategy_phase_offset_ticks, "scheduling.strategy_phase_offset_ticks", 0, 1000000, "ticks", "scheduling", "Phase offset for strategic director evaluations"),
		INT_SPEC(scheduling, building_interval_ticks, "scheduling.building_interval_ticks", 1, 1000000, "ticks", "scheduling", "Interval between building-management passes"),
		INT_SPEC(scheduling, building_phase_offset_ticks, "scheduling.building_phase_offset_ticks", 0, 1000000, "ticks", "scheduling", "Phase offset for building-management passes"),
		INT_SPEC(scheduling, defense_interval_ticks, "scheduling.defense_interval_ticks", 1, 1000000, "ticks", "scheduling", "Interval between strategic defense-positioning passes"),
		INT_SPEC(scheduling, defense_phase_offset_ticks, "scheduling.defense_phase_offset_ticks", 0, 1000000, "ticks", "scheduling", "Phase offset for strategic defense positioning"),
		INT_SPEC(scheduling, fruit_interval_ticks, "scheduling.fruit_interval_ticks", 1, 1000000, "ticks", "scheduling", "Interval between fruit-management passes"),
		INT_SPEC(scheduling, fruit_phase_offset_ticks, "scheduling.fruit_phase_offset_ticks", 0, 1000000, "ticks", "scheduling", "Phase offset for fruit management"),
		INT_SPEC(scheduling, explorer_attack_interval_ticks, "scheduling.explorer_attack_interval_ticks", 1, 1000000, "ticks", "scheduling", "Interval between explorer attack-positioning passes"),
		INT_SPEC(scheduling, explorer_attack_phase_offset_ticks, "scheduling.explorer_attack_phase_offset_ticks", 0, 1000000, "ticks", "scheduling", "Phase offset for explorer attack positioning"),
		INT_SPEC(scheduling, placement_interval_ticks, "scheduling.placement_interval_ticks", 1, 1000000, "ticks", "scheduling", "Interval between development-planner cycles"),
		INT_SPEC(scheduling, explorer_warning_duration_ticks, "scheduling.explorer_warning_duration_ticks", 0, 1000000, "ticks", "scheduling", "Duration of a general explorer-bomb warning"),
		INT_SPEC(scheduling, explorer_colony_warning_duration_ticks, "scheduling.explorer_colony_warning_duration_ticks", 0, 1000000, "ticks", "scheduling", "Duration of an explorer warning near the colony"),

		BOOL_SPEC(emergencies, food_enabled, "emergencies.food_enabled", "enabled", "emergencies", "Enable food-collapse emergency overrides"),
		BOOL_SPEC(emergencies, colony_enabled, "emergencies.colony_enabled", "enabled", "emergencies", "Enable combat-loss emergency overrides"),
		INT_SPEC(emergencies, food_unserved_percent, "emergencies.food_unserved_percent", 0, 100, "percent", "emergencies", "Unserved-food emergency threshold"),
		INT_SPEC(emergencies, food_critical_percent, "emergencies.food_critical_percent", 0, 100, "percent", "emergencies", "Critical-food emergency threshold"),
		INT_SPEC(emergencies, food_combined_percent, "emergencies.food_combined_percent", 0, 100, "percent", "emergencies", "Combined hunger emergency threshold"),
		INT_SPEC(emergencies, population_trend_threshold, "emergencies.population_trend_threshold", -10000, 0, "trend", "emergencies", "Legacy compatibility value; population trends no longer trigger food emergencies"),
		INT_SPEC(emergencies, food_trend_threshold, "emergencies.food_trend_threshold", 0, 1000, "trend", "emergencies", "Legacy compatibility value; hunger trends no longer trigger food emergencies"),
		INT_SPEC(emergencies, colony_threat_threshold, "emergencies.colony_threat_threshold", 0, 1000, "units", "emergencies", "Visible colony threat that immediately creates a colony emergency"),
		INT_SPEC(emergencies, buildings_under_attack_threshold, "emergencies.buildings_under_attack_threshold", 0, 100, "buildings", "emergencies", "Attacked buildings required by the combined attack emergency"),
		INT_SPEC(emergencies, units_under_attack_threshold, "emergencies.units_under_attack_threshold", 0, 1000, "units", "emergencies", "Attacked units required by the combined attack emergency"),
		INT_SPEC(emergencies, declining_units_under_attack_threshold, "emergencies.declining_units_under_attack_threshold", 0, 1000, "units", "emergencies", "Attacked units required when population is sharply declining"),
		INT_SPEC(emergencies, declining_population_trend_threshold, "emergencies.declining_population_trend_threshold", -10000, 0, "trend", "emergencies", "Population trend defining a sharp combat decline"),
		INT_SPEC(emergencies, proportional_units_floor, "emergencies.proportional_units_floor", 0, 1000, "units", "emergencies", "Minimum attacked units for the population-proportional emergency"),
		INT_SPEC(emergencies, proportional_population_divisor, "emergencies.proportional_population_divisor", 1, 1000, "units", "emergencies", "Population per attacked unit in the proportional emergency threshold"),
		INT_SPEC(emergencies, proportional_population_trend_threshold, "emergencies.proportional_population_trend_threshold", -10000, 0, "trend", "emergencies", "Population decline required by the proportional attack emergency")
	};

#undef INT_SPEC
#undef BOOL_SPEC

	const size_t parameterCount=sizeof(parameterSpecs)/sizeof(parameterSpecs[0]);

	std::string trim(const std::string& input)
	{
		const std::string whitespace=" \t\r\n";
		const std::string::size_type first=input.find_first_not_of(whitespace);
		if(first==std::string::npos)
			return std::string();
		const std::string::size_type last=input.find_last_not_of(whitespace);
		return input.substr(first, last-first+1);
	}





	const ParameterSpec* findSpec(const std::string& key)
	{
		for(size_t i=0; i<parameterCount; ++i)
			if(key==parameterSpecs[i].key)
				return &parameterSpecs[i];
		return NULL;
	}

	bool parseValue(const ParameterSpec& spec, const std::string& text,
		int& value, std::string& error)
	{
		if(std::string(spec.type)=="boolean")
		{
			if(text=="true" || text=="1") value=1;
			else if(text=="false" || text=="0") value=0;
			else
			{
				error="expected boolean true or false";
				return false;
			}
		}
		else
		{
			errno=0;
			char* end=NULL;
			const long parsed=std::strtol(text.c_str(), &end, 10);
			if(errno==ERANGE || !end || *end!='\0' || parsed<INT_MIN || parsed>INT_MAX)
			{
				error="expected a base-10 integer";
				return false;
			}
			value=int(parsed);
		}
		if(value<spec.minimum || value>spec.maximum)
		{
			std::ostringstream out;
			out<<"value "<<value<<" is outside ["<<spec.minimum<<", "
				<<spec.maximum<<"]";
			error=out.str();
			return false;
		}
		return true;
	}

	bool applyAssignment(const std::string& source, int lineNumber,
		const std::string& raw, Strategy& strategy,
		std::map<std::string, std::string>& provenance,
		std::set<std::string>& layerKeys, std::string& error)
	{
		std::string line=raw;
		const std::string::size_type comment=line.find('#');
		if(comment!=std::string::npos)
			line=line.substr(0, comment);
		line=trim(line);
		if(line.empty())
			return true;
		const std::string::size_type equals=line.find('=');
		if(equals==std::string::npos || line.find('=', equals+1)!=std::string::npos)
		{
			std::ostringstream out;
			out<<source<<":"<<lineNumber<<": expected one 'key = value' assignment";
			error=out.str();
			return false;
		}
		const std::string key=trim(line.substr(0, equals));
		const std::string text=trim(line.substr(equals+1));
		if(key.empty() || text.empty())
		{
			std::ostringstream out;
			out<<source<<":"<<lineNumber<<": key and value must be non-empty";
			error=out.str();
			return false;
		}
		if(!layerKeys.insert(key).second)
		{
			std::ostringstream out;
			out<<source<<":"<<lineNumber<<": duplicate key '"<<key<<"'";
			error=out.str();
			return false;
		}
		const ParameterSpec* spec=findSpec(key);
		if(!spec)
		{
			std::ostringstream out;
			out<<source<<":"<<lineNumber<<": unknown Maxima key '"<<key<<"'";
			error=out.str();
			return false;
		}
		int value=0;
		std::string valueError;
		if(!parseValue(*spec, text, value, valueError))
		{
			std::ostringstream out;
			out<<source<<":"<<lineNumber<<": invalid value for '"<<key<<"': "
				<<valueError;
			error=out.str();
			return false;
		}
		spec->set(strategy, value);
		std::ostringstream location;
		location<<source<<":"<<lineNumber;
		provenance[key]=location.str();
		return true;
	}

	std::ifstream* openStrategyFile(const std::string& fileName)
	{
		std::ifstream* direct=new std::ifstream(fileName.c_str());
		if(direct->good())
			return direct;
		delete direct;
		if(GAGCore::Toolkit::getFileManager())
			return GAGCore::Toolkit::getFileManager()->openIFStream(fileName);
		return NULL;
	}

	bool applyFile(const std::string& fileName, bool requireComplete,
		Strategy& strategy, std::map<std::string, std::string>& provenance,
		std::string& error)
	{
		std::ifstream* stream=openStrategyFile(fileName);
		if(!stream)
		{
			error="required Maxima strategy file not found: "+fileName;
			return false;
		}
		std::set<std::string> keys;
		std::string line;
		int lineNumber=0;
		while(std::getline(*stream, line))
		{
			++lineNumber;
			if(!applyAssignment(fileName, lineNumber, line, strategy,
				provenance, keys, error))
			{
				delete stream;
				return false;
			}
		}
		if(stream->bad())
		{
			error="failed while reading Maxima strategy file: "+fileName;
			delete stream;
			return false;
		}
		delete stream;
		if(requireComplete && keys.size()!=parameterCount)
		{
			for(size_t i=0; i<parameterCount; ++i)
				if(keys.find(parameterSpecs[i].key)==keys.end())
				{
					error=fileName+": required base strategy is missing key '"
						+parameterSpecs[i].key+"'";
					return false;
				}
		}
		return true;
	}



	bool applyInline(const std::string& source, const std::string& settings,
		Strategy& strategy, std::map<std::string, std::string>& provenance,
		std::string& error)
	{
		std::string normalized=settings;
		for(size_t i=0; i<normalized.size(); ++i)
			if(normalized[i]==';') normalized[i]=',';
		std::istringstream entries(normalized);
		std::set<std::string> keys;
		std::string entry;
		int number=0;
		while(std::getline(entries, entry, ','))
		{
			++number;
			if(trim(entry).empty())
			{
				std::ostringstream out;
				out<<source<<":"<<number<<": empty override assignment";
				error=out.str();
				return false;
			}
			if(!applyAssignment(source, number, entry, strategy, provenance,
				keys, error))
				return false;
		}
		return true;
	}

	std::string valueSource(const std::map<std::string, std::string>& provenance,
		const std::string& key)
	{
		std::map<std::string, std::string>::const_iterator found=provenance.find(key);
		return found==provenance.end() ? std::string("unknown source") : found->second;
	}

	bool validateRelations(const Strategy& value,
		const std::map<std::string, std::string>& provenance, std::string& error)
	{
		if(!value.postures.recover_enabled && !value.postures.defend_enabled
		   && !value.postures.expand_enabled && !value.postures.develop_enabled
		   && !value.postures.mobilize_enabled && !value.postures.campaign_enabled
		   && !value.postures.finish_enabled)
			error="at least one postures.*_enabled switch must be true";
		else if(value.economy.food_headroom_critical>
			value.economy.food_headroom_warning)
			error="economy.food_headroom_critical ("+valueSource(provenance,
				"economy.food_headroom_critical")+") must be <= "
				"economy.food_headroom_warning ("+valueSource(provenance,
				"economy.food_headroom_warning")+")";
		else if(value.construction.population_mid>value.construction.population_high)
			error="construction.population_mid ("+valueSource(provenance,
				"construction.population_mid")+") must be <= construction.population_high ("
				+valueSource(provenance, "construction.population_high")+")";
		else if(value.construction.sites_low>value.construction.sites_mid
			|| value.construction.sites_mid>value.construction.sites_high)
			error="construction site caps must be nondecreasing";
		else if(value.construction.emergency_min_sites>
			value.construction.emergency_max_sites)
			error="construction.emergency_min_sites must be <= "
				"construction.emergency_max_sites";
		else if(value.economy.first_pool_target>
			value.economy.second_pool_target
			|| value.economy.first_school_target>
				value.economy.second_school_target
			|| value.economy.first_racetrack_target>
				value.economy.second_racetrack_target)
			error="economy initial infrastructure targets must not exceed "
				"their advanced targets";
		else if(value.environment.smoothing_history_weight>=
			value.environment.smoothing_total_weight
			|| value.environment.fast_smoothing_history_weight>=
				value.environment.fast_smoothing_total_weight)
			error="environment smoothing history weights must be smaller than "
				"their total weights";
		else if(value.trends.history_weight>=value.trends.total_weight)
			error="trends.history_weight must be < trends.total_weight";
		else if(value.placement.guard_area_protectedness<
			value.placement.baseline_protectedness
			|| value.placement.barrier_protectedness<
				value.placement.baseline_protectedness)
			error="placement guard and barrier protection must be >= baseline "
				"protectedness";
		else if(value.placement.enemy_threat_base<
			value.placement.enemy_threat_radius
				*value.placement.enemy_threat_falloff)
			error="placement enemy threat must remain nonnegative within its radius";
		else if(value.placement.building_protection_base
			-value.placement.building_protection_radius
				*value.placement.building_protection_falloff
			<value.placement.baseline_protectedness)
			error="placement building protection must remain >= baseline within "
				"its radius";
		else if(value.farming.wood_firebreak_fertility_min_percent>
			value.farming.wood_firebreak_fertility_max_percent)
			error="farming.wood_firebreak_fertility_min_percent must be <= "
				"farming.wood_firebreak_fertility_max_percent";
		else if(value.reactive_defense.move_deadband>
			value.reactive_defense.move_radius)
			error="defense.reactive.move_deadband must be <= "
				"defense.reactive.move_radius";
		else if(value.upgrades.level1_population_min>
			value.upgrades.level2_population_min)
			error="upgrades.level1_population_min ("+valueSource(provenance,
				"upgrades.level1_population_min")+") must be <= "
				"upgrades.level2_population_min ("+valueSource(provenance,
				"upgrades.level2_population_min")+")";
		else if(value.model.inn_capacity_level1>
			value.model.inn_capacity_level2
			|| value.model.inn_capacity_level2>
			value.model.inn_capacity_level3)
			error="inn capacities must be nondecreasing by level (sources: "
				+valueSource(provenance, "model.inn_capacity_level1")+", "
				+valueSource(provenance, "model.inn_capacity_level2")+", "
				+valueSource(provenance, "model.inn_capacity_level3")+")";
		else if(value.farming.urgent_interval_ticks>value.farming.normal_interval_ticks)
			error="farming.urgent_interval_ticks ("+valueSource(provenance,
				"farming.urgent_interval_ticks")+") must be <= farming.normal_interval_ticks ("
				+valueSource(provenance, "farming.normal_interval_ticks")+")";
		else if(value.reconnaissance.force_memory_hold_ticks>=
			value.reconnaissance.memory_horizon_ticks)
			error="recon.force_memory_hold_ticks ("+valueSource(provenance,
				"recon.force_memory_hold_ticks")+") must be < recon.memory_horizon_ticks ("
				+valueSource(provenance, "recon.memory_horizon_ticks")+")";
		else if(value.reconnaissance.force_sample_phase_offset_ticks>=
			value.reconnaissance.force_sample_interval_ticks)
			error="recon.force_sample_phase_offset_ticks ("+valueSource(provenance,
				"recon.force_sample_phase_offset_ticks")+") must be < "
				"recon.force_sample_interval_ticks ("+valueSource(provenance,
				"recon.force_sample_interval_ticks")+")";
		else if(value.reconnaissance.stale_contact_age_ticks>
			value.reconnaissance.memory_horizon_ticks)
			error="recon.stale_contact_age_ticks ("+valueSource(provenance,
				"recon.stale_contact_age_ticks")+") must be <= recon.memory_horizon_ticks ("
				+valueSource(provenance, "recon.memory_horizon_ticks")+")";
		else if(value.reconnaissance.economic_watch_radius_min>
			value.reconnaissance.economic_watch_radius_max)
			error="recon.economic_watch_radius_min must be <= "
				"recon.economic_watch_radius_max";
		else if(value.raiding.min_force>value.raiding.max_force)
			error="raiding.min_force ("+valueSource(provenance,
				"raiding.min_force")+") must be <= raiding.max_force ("
				+valueSource(provenance, "raiding.max_force")+")";
		else if(value.raiding.survivor_min>value.raiding.max_force)
			error="raiding.survivor_min must be <= raiding.max_force";
		else if(value.teamplay.defense_min_force>value.military.attack_unit_cap)
			error="teamplay.defense_min_force must be <= military.attack_unit_cap";
		else if(value.teamplay.defense_contact_ttl_ticks>
			value.teamplay.defense_max_engagement_ticks)
			error="teamplay.defense_contact_ttl_ticks must be <= "
				"teamplay.defense_max_engagement_ticks";
		else
			return true;
		return false;
	}

	std::string formatFile(MatchFormat format)
	{
		return std::string("data/maxima/")
			+StrategyResolver::formatName(format)+".strategy";
	}

	bool resolveInternal(const StrategyConfigOptions& options, MatchFormat format,
		ResolvedStrategy& result, std::string& error)
	{
		result=ResolvedStrategy();
		result.format=format;
		if(!applyFile(options.baseFile, true, result.values,
			result.provenance, error))
			return false;
		result.sources.push_back(options.baseFile);
		const std::string selectedFormatFile=formatFile(format);
		if(!applyFile(selectedFormatFile, false, result.values,
			result.provenance, error))
			return false;
		result.sources.push_back(selectedFormatFile);
		for(std::vector<std::string>::const_iterator file=options.layerFiles.begin();
			file!=options.layerFiles.end(); ++file)
		{
			if(!applyFile(*file, false, result.values, result.provenance, error))
				return false;
			result.sources.push_back(*file);
		}
		return validateRelations(result.values, result.provenance, error);
	}
}

std::vector<StrategyParameterInfo> StrategyResolver::schema()
{
	std::vector<StrategyParameterInfo> result;
	std::set<std::string> keys;
	for(size_t i=0; i<parameterCount; ++i)
	{
		StrategyParameterInfo info;
		info.key=parameterSpecs[i].key;
		info.type=parameterSpecs[i].type;
		info.unit=parameterSpecs[i].unit;
		info.group=parameterSpecs[i].group;
		info.description=parameterSpecs[i].description;
		info.hardMinimum=parameterSpecs[i].minimum;
		info.hardMaximum=parameterSpecs[i].maximum;
		if(keys.insert(info.key).second)
			result.push_back(info);
	}
	return result;
}

bool StrategyResolver::resolve(const StrategyConfigOptions& options,
	const GameHeader* gameHeader, ResolvedStrategy& result, std::string& error)
{
    if(!gameHeader)
    {
        error="Maxima strategy resolution requires a game header";
        return false;
    }
    const MatchFormat format=inferFormat(*gameHeader);
	return resolveInternal(options, format, result, error);
}

bool StrategyResolver::resolveForFormat(const StrategyConfigOptions& options,
	MatchFormat format, ResolvedStrategy& result, std::string& error)
{
	return resolveInternal(options, format, result, error);
}

bool StrategyResolver::parseFormat(const std::string& name, MatchFormat& format)
{
	if(name=="duel") format=MatchFormatDuel;
	else if(name=="ffa3") format=MatchFormatFfa3;
	else if(name=="ffa4") format=MatchFormatFfa4;
	else if(name=="ffa5plus") format=MatchFormatFfa5Plus;
	else if(name=="2v2") format=MatchFormat2v2;
	else return false;
	return true;
}

MatchFormat StrategyResolver::inferFormat(const GameHeader& gameHeader)
{
	const int players=gameHeader.getNumberOfPlayers();
	if(players<=2) return MatchFormatDuel;
	if(players==3) return MatchFormatFfa3;
	if(players>=5) return MatchFormatFfa5Plus;
	std::map<int, int> allianceSizes;
	for(int i=0; i<players; ++i)
	{
		const int team=gameHeader.getBasePlayer(i).teamNumber;
		++allianceSizes[gameHeader.getAllyTeamNumber(team)];
	}
	if(allianceSizes.size()==2)
	{
		bool pairs=true;
		for(std::map<int, int>::const_iterator group=allianceSizes.begin();
			group!=allianceSizes.end(); ++group)
			pairs=pairs && group->second==2;
		if(pairs) return MatchFormat2v2;
	}
	return MatchFormatFfa4;
}

const char* StrategyResolver::formatName(MatchFormat format)
{
	switch(format)
	{
		case MatchFormatDuel: return "duel";
		case MatchFormatFfa3: return "ffa3";
		case MatchFormatFfa4: return "ffa4";
		case MatchFormatFfa5Plus: return "ffa5plus";
		case MatchFormat2v2: return "2v2";
	}
	return "unknown";
}



std::string StrategyResolver::canonicalValues(const Strategy& strategy)
{
	std::ostringstream out;
	for(size_t i=0; i<parameterCount; ++i)
	{
		if(i) out<<",";
		out<<parameterSpecs[i].key<<"=";
		if(std::string(parameterSpecs[i].type)=="boolean")
			out<<(parameterSpecs[i].get(strategy) ? "true" : "false");
		else
			out<<parameterSpecs[i].get(strategy);
	}
	return out.str();
}



}

namespace AIMaxima {
bool StrategyResolver::restoreValues(const std::string& text, MaximaStrategy& values,
    std::string& error)
{
    MaximaStrategy restored{};
    std::map<std::string, std::string> provenance;
    if(!applyInline("saved strategy", text, restored, provenance, error)) return false;
    if(provenance.size()!=parameterCount)
    {
        error="Incomplete saved Maxima strategy";
        return false;
    }
    if(!validateRelations(restored, provenance, error)) return false;
    values=restored;
    return true;
}
}
