/*
  Copyright (C) 2006 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef AIMaxima_h
#define AIMaxima_h

#include "AIMaximaRuntime.h"
#include "AIMaximaDefense.h"
#include "AIMaximaFarming.h"
#include "AIMaximaPlacement.h"
#include "AIMaximaRecon.h"
#include "AIMaximaStaffingControl.h"
#include "AIMaximaStrategy.h"
#include "AIMaximaTactics.h"
#include <map>
#include <set>

namespace AIMaxima
{

///Nicowar is a new powerhouse AI for Globulation 2
class Maxima : public AIImplementation, private AIMaximaRuntime::RuntimeAI
{
public:
	explicit Maxima(Player *player);
	Maxima(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	/// Consume versions 84-88 using their original serialized field layout.
	bool loadLegacyState(GAGCore::InputStream *stream, Player *player,
		Sint32 versionMinor);
	bool loadState(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	std::shared_ptr<Order> getOrder();
	std::string auditStrategyJson() const;
	void getDiagnosticSections(std::vector<AIDiagnosticSection>& sections) const;
	const AITopologyDiagnosticSnapshot* getTopologyDiagnosticSnapshot() const;
	void tick(AIMaximaRuntime::Context& echo);
	void handle_event(AIMaximaRuntime::Context& echo, const AIMaximaRuntime::RuntimeEvent& event);
private:
	AIMaximaRuntime::Context context;
	///Owns strategic cadence and invalidation. Maxima's remaining methods are
	///snapshot collectors and policy modules invoked through this boundary.
	class StrategyDirector
	{
	public:
		StrategyDirector() : initialized(false), dirty(true) {}
		void evaluate(Maxima& owner,
			AIMaximaRuntime::Context& echo);
		void invalidate() { dirty=true; }
		void committed() { initialized=true; dirty=false; }
		bool initialized;
		bool dirty;
	};
	enum StrategicPosture
	{
		PostureExpand,
		PostureDevelop,
		PostureMobilize,
		PostureCampaign,
		PostureFinish,
		PostureDefend,
		PostureRecover,
		PostureCount
	};

	struct StrategicSnapshot
	{
		StrategicSnapshot();
		int tick;
		int population;
		int workers;
		int free_warriors;
		int explorers;
		int trained_explorers;
		int warriors;
		int trained_workers;
		int trained_workers_level2;
		int trained_warriors;
		int swimming_workers;
		int swimming_explorers;
		int swimming_warriors;
		int amphibious_attack_explorers;
		int free_workers;
		int worker_jobs_open;
		int hungry;
		int critical_food;
		int unserved_food;
		int need_heal;
		int buildings;
		int building_sites;
		int swarms;
		int completed_swarms;
		int inns;
		int inn_level1;
		int inn_level2;
		int inn_level3;
		int barracks;
		int schools;
		int school_level1;
		int school_level2;
		int school_level3;
		int pools;
		int hospitals;
		int racetracks;
		int towers;
		int own_buildings_under_attack;
		int own_units_under_attack;
		int visible_enemy_warriors;
		int visible_enemy_explorers;
		int visible_enemy_attack_explorers;
		int visible_colony_explorer_threat;
		int visible_colony_threat;
		int alive_enemies;
		int total_hp;
		int attack_power;
		int prestige;
		int enemy_prestige;
		int tower_stone;
		int tower_bullets;
	};

	struct StrategicTrends
	{
		StrategicTrends();
		int population;
		int workers;
		int warriors;
		int food_pressure;
		int colony_pressure;
	};

	///Immutable output of the strategic director for one strategic cadence.
	///Executors consume this value and may only perform tactical validation,
	///geometry, pathfinding, and order issuance.
	struct DirectorPlan
	{
		DirectorPlan();
		int construction_sites;
		int desired_inns;
		int desired_swarms;
		int desired_barracks;
		int desired_schools;
		int desired_pools;
		int desired_racetracks;
		int desired_hospitals;
		int desired_towers;
		int swarm_workers;
		int worker_ratio;
		int explorer_ratio;
		int warrior_ratio;
		int desired_explorers;
		int desired_warriors;
		int defense_reserve;
		int attack_flags;
		int attack_units;
		bool allow_upgrades;
		bool allow_level2_upgrades;
		int upgrade_level1_workers;
		int upgrade_level2_workers;
		int upgrade_level1_trained_units_per_slot;
		int upgrade_level2_trained_units_per_slot;
		int upgrade_level1_inn_weight;
		int upgrade_level1_hospital_weight;
		int upgrade_level1_racetrack_weight;
		int upgrade_level1_pool_weight;
		int upgrade_level1_barracks_weight;
		int upgrade_level2_inn_weight;
		int upgrade_level2_hospital_weight;
		int upgrade_level2_racetrack_weight;
		int upgrade_level2_pool_weight;
		int upgrade_level2_barracks_weight;
		int first_prestige_trained_workers;
		int second_prestige_trained_workers;
		int second_prestige_population_min;
		bool swarm_retirement_enabled;
		///Food ledger authority. The director decides whether the ledger runs,
		///what coverage counts as a burden and how long one must persist. The
		///executor only measures coverage and issues the retirement order.
		bool food_ledger_enabled;
		bool food_retirement_enabled;
		int food_inn_burden_percent;
		int food_swarm_burden_percent;
		int food_recovered_percent;
		int food_burden_confirm_ticks;
		int food_retirement_cooldown_ticks;
		///Reliable seats a completed inn of each level actually serves, already
		///discounted, so the executor never needs the economic model itself.
		int food_inn_seats_level1;
		int food_inn_seats_level2;
		int food_inn_seats_level3;
		///Per-building staffing control. Every inn and swarm regulates its own
		///worker request from its own wheat stock and its actual staffing;
		///there is no colony budget and no apportionment between buildings.
		int staffing_window_samples;
		int staffing_low_permille;
		int staffing_high_permille;
		int staffing_slack;
		int staffing_minimum_workers;
		int staffing_maximum_workers;
		int staffing_cooldown_passes;
		int swarm_supply_radius;
		int attack_clearing_workers;
		bool can_swim;
		bool recovery_active;
		bool food_emergency;
		bool colony_emergency;
		bool colony_swarm_requested;
		int colony_swarm_priority;
		bool explorer_campaign_active;
		int explorer_campaign_flags;
		int explorer_campaign_units_per_flag;
		bool fruit_active;
		int fruit_units_per_flag;
		int fruit_flag_radius;
		bool reactive_defense_enabled;
		int reactive_defense_flag_radius;
		int reactive_defense_move_radius;
		int reactive_defense_move_deadband;
		int reactive_defense_unit_cap;
		int reactive_defense_advantage_min;
		int reactive_defense_advantage_percent;
		bool preemptive_defense_active;
		bool preemptive_amphibious_active;
		int preemptive_effective_zone_max;
		int target_switch_margin;
		int preemptive_recompute_ticks;
		int preemptive_inner_distance;
		int preemptive_band_width;
		int preemptive_path_slack;
		int preemptive_probe_radius;
		int preemptive_cross_section_max;
		int preemptive_zone_radius;
		int preemptive_zone_max;
		bool reconnaissance_suspended;
		int reconnaissance_flag_radius;
		int reconnaissance_review_interval;
		int reconnaissance_economic_watch_revisit;
		std::vector<Recon::MissionObjective> reconnaissance_objectives;
		int farming_normal_interval;
		int farming_urgent_interval;
		bool farming_enabled;
		bool farming_protection_enabled;
		bool farming_maintenance_clearing_enabled;
		bool farming_resource_preserving_circulation_enabled;
		bool farming_wheat_invasion_clearing_enabled;
		bool farming_wood_firebreak_enabled;
		bool farming_proactive_clearing_enabled;
			bool farming_urgent;
			int farming_wood_pressure;
			Uint32 farming_minimum_wood_fertility;
		bool farming_allow_proactive_clearing;
		bool farming_clearing_for_placement;
		int farming_min_workers_for_clearing;
		int farming_clearing_cooldown;
		int farming_clearing_duration;
		int farming_clearing_quota;
		int farming_management_radius;
		int farming_wheat_fertility_min;
		int farming_wood_fertility_base_percent;
		int farming_wood_fertility_pressure_percent;
		int farming_wood_pressure_base;
		int farming_wood_pressure_space_divisor;
		int farming_wood_pressure_supply_divisor;
		int farming_wood_pressure_construction_divisor;
		int farming_wood_pressure_growth_divisor;
		int farming_economic_envelope_radius;
		int priority_inns;
		int priority_swarms;
		int priority_barracks;
		int priority_schools;
		int priority_pools;
		int priority_racetracks;
		int priority_hospitals;
		int priority_towers;
		Tactics::MissionKind tactical_kind;
		int tactical_target_team;
		int tactical_dig_out_team;
		int tactical_target_gid;
		int tactical_target_x;
		int tactical_target_y;
		int tactical_candidate_score;
		int tactical_requested_force;
		int tactical_review_interval;
		bool tactics_enabled;
		int tactical_flag_level;
		int tactical_siege_radius;
		int raid_flag_radius;
		int tactical_stall_ticks;
		bool tactical_quarantine_enabled;
		int tactical_quarantine_ticks;
	};

	enum PolicyKind
	{
		PolicySurvival,
		PolicyGrowth,
		PolicyAccess,
		PolicyTechnology,
		PolicyDefense,
		PolicyOffense,
		PolicyCount
	};

	///A policy bid describes the marginal resources one strategic concern would
	///buy if it controlled the next slice of capacity. The arbiter can combine
	///compatible bids while making conflicts explicit.
	struct PolicyBid
	{
		PolicyBid();
		int utility;
		int construction_sites;
		int desired_inns;
		int desired_swarms;
		int desired_barracks;
		int desired_schools;
		int desired_pools;
		int desired_racetracks;
		int desired_hospitals;
		int desired_towers;
		int swarm_workers;
		int worker_ratio;
		int explorer_ratio;
		int warrior_ratio;
		int desired_explorers;
		int desired_warriors;
		int defense_reserve;
		int attack_flags;
		int attack_units;
		bool request_upgrades;
	};

	///A smoothed, map-name-independent model of what this colony can currently
	///support.  These are independent axes rather than an exclusive map mode: a
	///water-heavy start can be food-rich, and a formerly rich colony can become
	///capacity constrained after rapid growth.
	struct EnvironmentModel
	{
		EnvironmentModel();
		int known_tiles;
		int accessible_corn;
		int accessible_wood;
		int accessible_stone;
		int accessible_algae;
		int buildable_tiles;
		int water_tiles;
		int feeding_capacity;
		int food_headroom;
		int resource_capacity;
		int space_capacity;
		int food_security;
		int abundance;
		int terrain_abundance;
		int connected_abundance;
		int mobility_opportunity;
		int economic_momentum;
		int mobility_constraint;
		int topology_complexity;
		int threat_pressure;
		int confidence;
	};

	///Continuous competing demands used by the allocator.  Several demands may
	///be funded together; postures provide commitment and emergency overrides,
	///not a single global script for the whole economy.
	struct StrategicDemands
	{
		StrategicDemands();
		int survival;
		int food;
		int growth;
		int expansion;
		int access;
		int technology;
		int mobility;
		int military;
		int aggression;
	};

	struct OpponentAssessment
	{
		OpponentAssessment();
		bool alive;
		int visible_warriors;
		int estimated_warriors;
		int last_observed_warriors;
		int visible_explorers;
		int visible_buildings;
		int known_buildings;
		int reachable_buildings;
		int strategic_value;
		int nearest_building;
		int score;
		int last_seen_tick;
		int last_force_seen_tick;
		int last_building_seen_tick;
		int intel_confidence;
	};

	enum CampaignState
	{
		CampaignIdle,
		CampaignPreparing,
		CampaignActive,
		CampaignPaused,
		CampaignRetreating
	};

	struct CampaignPlan
	{
		CampaignPlan();
		CampaignState state;
		int target_team;
		int started_tick;
		int last_progress_tick;
		int last_target_buildings;
		int buildings_destroyed;
		int cooldown_until;
	};

	/// Ephemeral explanation of the most recent tactical authorization pass.
	/// This is deliberately not serialized: it is derived from the same fog-safe
	/// observations as DirectorPlan and exists only for the in-game debug HUD.
	/// Explanation of the most recent offensive plan for the F10 panel and telemetry.
	struct OffenseDiagnostics
	{
		OffenseDiagnostics() { reset(0); }
		void reset(int currentTick);
		int tick;
		std::string gate;
		std::string decision;
		int eligibleWarriors;
		int openTrainingSlots;
		int buildingCandidates;
		int viableBuildings;
		int clusterCandidates;
		int viableClusters;
		int bestScore;
		std::map<std::string, int> rejections;
	};

	struct ClearedEnemySite
	{
		ClearedEnemySite();
		ClearedEnemySite(int siteX, int siteY, int tick);
		int x;
		int y;
		int confirmedTick;
	};

	void evaluate_strategy(AIMaximaRuntime::Context& echo);
	StrategicSnapshot collect_snapshot(AIMaximaRuntime::Context& echo);
	void update_trends();
	void update_opponent_models(AIMaximaRuntime::Context& echo);
	void sample_reconnaissance_forces(AIMaximaRuntime::Context& echo);
	void update_reconnaissance(AIMaximaRuntime::Context& echo);
	void remember_cleared_enemy_site(AIMaximaRuntime::Context& echo,
		const Recon::BuildingSighting& sighting);
	void prune_cleared_enemy_sites();
	void plan_reconnaissance_objectives(AIMaximaRuntime::Context& echo);
	void update_reconnaissance_missions(AIMaximaRuntime::Context& echo);
	void remove_reconnaissance_missions(AIMaximaRuntime::Context& echo,
		const char* reason);
	std::vector<unsigned char> reconnaissance_discovery_map(
		AIMaximaRuntime::Context& echo) const;
	void initialize_topology_profile(AIMaximaRuntime::Context& echo);
	void update_environment_model(AIMaximaRuntime::Context& echo);
	void score_demands();
	void score_postures();
	void select_posture();
	void allocate_resources();
	void build_policy_bids();
	void arbitrate_policy_bids();
	const char* policy_name(PolicyKind policy) const;
	void finalize_director_plan(AIMaximaRuntime::Context& echo);
	void plan_offense(AIMaximaRuntime::Context& echo);
	void emit_telemetry(AIMaximaRuntime::Context& echo, const std::string& event,
		const std::string& fields=std::string()) const;
	void emit_ablation_opportunities(AIMaximaRuntime::Context& echo) const;
	void emit_director_snapshot(AIMaximaRuntime::Context& echo) const;
	const char* posture_name(StrategicPosture posture) const;
	bool severe_food_emergency() const;
	bool severe_colony_emergency() const;
	bool explorer_defense_active() const;
	bool explorer_defense_emergency() const;
	bool abundance_surge_active() const;
	bool large_economy_established() const;
	void saveDirector(GAGCore::OutputStream*) const;
	void saveExecutionState(GAGCore::OutputStream*);
	void loadExecutionState(GAGCore::InputStream*, Sint32 versionMinor);
	template<class Archive> void executionState(Archive&);
	bool loadDirector(GAGCore::InputStream*, Sint32 versionMinor);

	StrategicSnapshot snapshot;
	StrategicSnapshot previous_snapshot;
	StrategicTrends trends;
	DirectorPlan budget;
	// Versions 86-88 serialized both derived plans and their blend state.
	DirectorPlan dynamic_plan;
	DirectorPlan classic_plan;
	EnvironmentModel environment;
	StrategicDemands demands;
	PolicyBid policy_bids[PolicyCount];
	OpponentAssessment opponents[Team::MAX_COUNT];
	Recon::Program reconnaissance;
	int last_recon_mission_tick;
	bool reconnaissance_suspended;
	CampaignPlan campaign;
	Tactics::Program tactics;
	Tactics::Mission tactical_mission;
	OffenseDiagnostics offense_diagnostics;
	std::vector<ClearedEnemySite> cleared_enemy_sites;
	// Runtime startup latch; saves before version 95 recheck workers and food.
	std::set<int> operating_colonies;
	int last_colony_completed_tick;
	int last_colony_accounted_action_id;
	int established_colonies;
	std::string colony_gate_reason;
	StrategicPosture posture;
	int posture_utilities[PostureCount];
	int posture_since;
	bool director_initialized;
	StrategyDirector director;
	int last_director_telemetry_tick;
	int explorer_threat_until;
	int explorer_colony_threat_until;
	bool large_economy_committed;
	bool topology_initialized;
	int global_water_percent;
	int global_shoreline_density;
	int global_land_components;
	int global_largest_land_percent;
	int global_start_land_percent;
	int global_chokepoint_density;
	int global_water_tiles;
	int global_grass_tiles;
	int global_land_tiles;
	int global_buildable_tiles;
	int global_corn_tiles;
	int global_wood_tiles;
	int global_stone_tiles;
	int global_algae_tiles;
	int global_fruit_tiles;
	int global_terrain_abundance;
	int global_connected_abundance;
	int global_mobility_opportunity;
	// Preserve the current reachability observation until its scheduled refresh.
	int known_algae_units;
	int walk_accessible_algae_units;
	int swim_accessible_algae_units;
	int accessible_algae_units;
	int dynamic_plan_weight;
	int target_dynamic_plan_weight;
	std::map<int, int> attack_flag_targets;
	std::map<int, int> attack_flag_started_ticks;
	std::map<int, int> attack_flag_last_hp;
	std::map<int, int> attack_flag_last_progress;
	std::map<int, std::string> attack_flag_end_reasons;
	std::map<int, int> attack_target_quarantine_until;
	///The immutable, fully resolved strategy used for this match.
	MaximaStrategy strategy;

	// Legacy versions 84-88 serialized phase and construction queues. They are
	// consumed on load, then replaced by the deterministic development planner.
	enum LegacyBuildingPlacement
	{
		LegacyRegularInn,
		LegacyStarvingRecoveryInn,
		LegacyRegularSwarm,
		LegacyRegularRacetrack,
		LegacyRegularSwimmingpool,
		LegacyRegularSchool,
		LegacyRegularBarracks,
		LegacyRegularHospital,
		LegacyRegularTower,
		LegacyPlacementSize
	};
	bool growth_phase;
	bool skilled_work_phase;
	bool upgrading_phase_1;
	bool upgrading_phase_2;
	bool war_preperation;
	bool war;
	bool fruit_phase;
	bool starving_recovery;
	bool no_workers_phase;
	bool can_swim;
	bool defend_explorers;
	bool explorer_attack_preperation_phase;
	bool explorer_attack_phase;
	int starving_recovery_inns;
	int buildings_under_construction;
	int buildings_under_construction_per_type[LegacyPlacementSize];
	std::list<LegacyBuildingPlacement> placement_queue;
	std::list<LegacyBuildingPlacement> construction_queue;

	///This function is called at the very begginning of the game,
	///to initialize the existing buildings with the right amount of units
	void initialize(AIMaximaRuntime::Context& echo);

	///Recalculate the sole strategic plan.
	void check_phases(AIMaximaRuntime::Context& echo);


	///Unified deterministic construction, upgrade and repair selector.
	void development_cycle(AIMaximaRuntime::Context& echo);
	void configure_development_planner();
	AIMaximaPlacement::WorldState collect_development_world(
		AIMaximaRuntime::Context& echo, uint32_t* signature=NULL) const;
	const std::vector<AIMaximaPlacement::BuildingProfile>&
		collect_building_profiles() const;
	int school_algae_requirement() const;
	std::vector<AIMaximaPlacement::DevelopmentIntent>
		collect_development_intents(
			const AIMaximaPlacement::WorldState& world) const;
	AIMaximaPlacement::DevelopmentLimits collect_development_limits(
		AIMaximaRuntime::Context& echo) const;
	bool issue_development_action(AIMaximaRuntime::Context& echo,
		AIMaximaPlacement::DevelopmentAction& action);
	void emit_placement_diagnostics(AIMaximaRuntime::Context& echo,
		const char* outcome,
		const AIMaximaPlacement::DevelopmentAction* action) const;
	mutable std::vector<AIMaximaPlacement::BuildingProfile>
		development_building_profiles;
	AIMaximaPlacement::Planner development_planner;
	bool development_planner_initialized;
	std::map<int, AIMaximaPlacement::ActionLifecycleState>
		development_reported_states;
	///One independent controller per inn and swarm, keyed by building id. This
	///is the controller's integral state, so it is carried across saves.
	std::map<int, StaffingControl::State> staffing_control;
	///Runs one control pass and issues the order when the request changes.
	int staff_building(AIMaximaRuntime::Context& echo, int id);
	std::map<int, int> remote_swarm_since;
	std::set<int> remote_swarms_ready;
	std::set<int> remote_swarm_deletion_issued;
	void update_swarm_retirement(AIMaximaRuntime::Context& echo);
	///Food ledger retirement. A building whose protected farm capacity stays
	///below its burden threshold for the confirmation window is removed, but
	///only while doing so cannot leave the population without inn seats.
	std::map<int, int> food_burden_since;
	std::set<int> food_retirement_issued;
	int last_food_retirement_tick;
	///Targets the ledger can actually supply, carried across saves so a loaded
	///game plans from the same numbers as the run that saved it.
	int food_supported_inns;
	int food_supported_swarms;
	bool food_ledger_valid;
	void update_food_retirement(AIMaximaRuntime::Context& echo,
		const AIMaximaPlacement::WorldState& world);
	///Neighbouring cells a protected stack's growth can actually spread into.
	///Unlike farm expansion, a partly harvested wheat neighbour absorbs growth.
	int growth_absorbing_neighbors(AIMaximaRuntime::Context& echo,
		int x, int y) const;
	///Placement pressure drives small, wheat-safe clearing campaigns. Clearing
	///damages workers, so campaigns are rate-limited and health-gated.
	int recent_construction_failures;
	int last_construction_failure_tick;
	bool opening_space_constrained;
	void record_construction_space_failure();
	int proactive_clearing_flag;
	int proactive_clearing_started_tick;
	int proactive_clearing_initial_wood;
	int proactive_clearing_campaigns;
	int last_proactive_clearing_tick;
	void manage_land_clearing(AIMaximaRuntime::Context& echo);
	bool continue_clearing_campaign(AIMaximaRuntime::Context& echo);
	void retire_clearing_campaign(AIMaximaRuntime::Context& echo,
		const char* reason, const std::string& details=std::string());
	struct WoodClearingTarget;
	WoodClearingTarget select_wood_clearing_target(
		AIMaximaRuntime::Context& echo) const;
	///Keeps planner parcels and circulation routes under durable clearing
	///orders. Only cells owned by this subsystem are removed.
	void update_maintenance_clearing_areas(AIMaximaRuntime::Context& echo);
	std::vector<Uint8> worker_reachable_circulation(
		AIMaximaRuntime::Context& echo) const;
	std::vector<std::vector<int> > reservation_member_footprints(
		AIMaximaRuntime::Context& echo,
		const AIMaximaPlacement::Reservation& contract) const;
	struct MaintenanceClearingPlan;
	MaintenanceClearingPlan build_maintenance_clearing_plan(
		AIMaximaRuntime::Context& echo);
	void apply_maintenance_clearing_plan(AIMaximaRuntime::Context& echo,
		const MaintenanceClearingPlan& plan);
	void initialize_farming_cache(AIMaximaRuntime::Context& echo);
	int available_expansion_neighbors(AIMaximaRuntime::Context& echo,
		int x, int y) const;


	///This function updates all of the buildings that are not under construction.
	void manage_buildings(AIMaximaRuntime::Context& echo);
	///This function updates the units assigned to a particular Inn.
	///Using the messaging system, it is called after the completion
	///of a new Inn and periodically thereafter
	void manage_inn(AIMaximaRuntime::Context& echo, int id);
	///This function updated the units assigned and the creation
	///ratios of a particular Swarm. Its done afeter the completion
	///of a new swarm and periodically thereafter
	long long nearby_farm_capacity(AIMaximaRuntime::Context& echo, int id,
		std::set<int>* shared_tiles=NULL);
	void manage_swarm(AIMaximaRuntime::Context& echo, int id);


	///This function chooses an enemy building to be destroyed. It returns -1 if there are no accessible buildings
	int choose_building_to_attack(AIMaximaRuntime::Context& echo);
	///This function starts an attack on another enemy building
	void attack_building(AIMaximaRuntime::Context& echo);
	///Keep one war flag on the planned offensive target; runs every review tick.
	void control_offense(AIMaximaRuntime::Context& echo);
	void end_offense(AIMaximaRuntime::Context& echo, const char* reason);
	///This function chooses the enemy team to target
	void choose_enemy_target(AIMaximaRuntime::Context& echo);
	///This function digs out an enemy building that is surrounded by resources.
	///It will also cause Maxima to dig itself out in certain situations.
	///Returns true if there are buildings that it can dig out, false otherwise
	bool dig_out_enemy(AIMaximaRuntime::Context& echo);

	///This integer stores the currently targetted enemy
	int target;
	///This integer stores whether a building is being dug out at the current moment
	bool is_digging_out;
	///This vector stores the ID's for all current war flags
	std::vector<int> attack_flags;
	
	
	///This function calculates the positions of defense flags
	void compute_defense_flag_positioning(AIMaximaRuntime::Context& echo);
	///This function adds the specific value to the counts arround the given pos, used in compute_defense_flag_positioning
	void modify_points(Uint16* counts, int w, int h, int x, int y, int dist, int value, std::list<int>& locations);
	///This vector stores the ID's for all current defense flags
	std::vector<int> defense_flags;

	///Refreshes fog-safe guard zones on narrow enemy approach corridors.
	void update_preemptive_defense(AIMaximaRuntime::Context& echo);
	void clear_preemptive_defense(AIMaximaRuntime::Context& echo);
	Uint32 compute_preemptive_building_signature(AIMaximaRuntime::Context& echo) const;
	void refresh_preemptive_diagnostics(const AIMaxima::Defense::PlanResult& plan);
	///Only cells added by this subsystem are owned and therefore removable.
	std::set<int> preemptive_guard_tiles;
	int last_preemptive_defense_tick;
	Uint32 preemptive_building_signature;
	int last_preemptive_effective_zone_max;
	bool last_preemptive_amphibious_active;
	AITopologyDiagnosticSnapshot preemptive_diagnostics;

	///This function calculates the positions of explorer flags for explorer flag attacks
	void compute_explorer_flag_attack_positioning(AIMaximaRuntime::Context& echo);
	///This vector stores the list of explorer attack flags
	std::vector<int> explorer_attack_flags;

	///This function updates the restricted areas for farming
	void update_farming(AIMaximaRuntime::Context& echo);
	///The policy is staged: classify a desired plan, restore passive coastal
	///access, then reconcile that plan with the engine-owned forbidden area.
	bool has_hard_farming_contract(int index) const;
	bool wheat_invasion_clearing_required(AIMaximaRuntime::Context& echo,
		int index, const std::vector<Uint8>& protected_wheat) const;
	struct FarmProtectionPlan;
	void resolve_wheat_invasion_clearing(AIMaximaRuntime::Context& echo,
		FarmProtectionPlan& plan);
	void release_farming_protection(AIMaximaRuntime::Context& echo);
	FarmProtectionPlan build_farming_protection_plan(
		AIMaximaRuntime::Context& echo);
	void restore_passive_coastal_access(AIMaximaRuntime::Context& echo,
		FarmProtectionPlan& plan);
	void apply_farming_protection(AIMaximaRuntime::Context& echo,
		const FarmProtectionPlan& plan, int& added, int& removed);
	Farming::ExactFertilityCache fertility_cache;
	/// Terrain-only adjacency, built with fertility once per map load.
	std::vector<Uint8> farming_shoreline_mask;
	std::vector<Uint8> farming_cardinal_shoreline_mask;
	std::vector<Uint8> applied_farm_protection_mask;
	std::vector<Uint8> applied_maintenance_clearing_mask;
	std::vector<Uint8> maintenance_circulation_mask;
	std::vector<Uint8> wood_firebreak_mask;
	std::vector<Uint8> farm_protection_mask;
	/// Protected wheat pattern only, used to detect adjacent wood invasion.
	std::vector<Uint8> wheat_farm_protection_mask;
	int last_farming_tick;
	bool farming_urgent;
	///Map-area orders produced by farming and placement are intentionally applied
	///on later ticks so their scans cannot extend the calculation that created
	///them. Version 95 preserves pending work to retain exact order timing.
	bool land_clearing_pending;
	bool maintenance_clearing_pending;
	bool development_cycle_pending;
	bool preemptive_defense_pending;
	bool reactive_defense_pending;

	///This function puts exploration flags on fruit trees once the fruit phase
	///has been activated.
	void update_fruit_flags(AIMaximaRuntime::Context& echo);
	///This function updates the alliances with opponents once the fruit phase
	///has been activated.
	void update_fruit_alliances(AIMaximaRuntime::Context& echo);
	///Legacy save field; live fruit missions are reconciled from runtime records.
	bool exploration_on_fruit;


	int timer;
};


}


#endif
