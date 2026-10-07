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

#include "field/UniformTraversal.h"
#include "GameDiagnostics.h"
#include "AITelemetryFields.h"
#include "AIMaxima.h"
#include "AIMaximaBuildings.h"
#include "AIMaximaFeedingEstimate.h"
#include "AIMaximaFeedingDemand.h"
#include "AIMaximaWorldHelpers.h"
#include "AIMaximaSwarmController.h"
#include "AIMaximaFoodSupply.h"
#include "GlobalContainer.h"
#include "FormatableString.h"
#include "Utilities.h"
#include "Game.h"
#include "Unit.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <deque>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace AIMaximaRuntime;
using namespace AIMaximaRuntime::Gradients;
using namespace AIMaximaRuntime::Construction;
using namespace AIMaximaRuntime::Management;
using namespace AIMaximaRuntime::Conditions;
using namespace AIMaximaRuntime::SearchTools;

namespace AIMaxima
{
using namespace WorldHelpers;

namespace
{
	const int PREEMPTIVE_UNREACHABLE=-1;

    int initialCarrierRequest(const BuildingType& type,const MaximaStrategy& strategy)
    {
        int requested=-1;
        if(type.semantics.production.enabledUnitMask)requested=strategy.staffing.new_swarm_workers;
        if(type.maxUnitInside>0)requested=std::max(requested,strategy.staffing.new_inn_workers);
        if(type.shootingRange>0 && type.shootRhythm>0)requested=std::max(requested,strategy.staffing.completed_tower_workers);
        if(requested<0)requested=type.presentation.defaultAssigned<0?2:type.presentation.defaultAssigned;
        return std::clamp(requested,0,std::min(type.semantics.assignmentLimit,strategy.staffing.control_maximum_workers));
    }

    FeedingEstimate operatingEstimate(const BuildingType& type,const MaximaStrategy& strategy,const GameHeader& rules,bool constrainHauling=true)
    {
        FeedingPlan plan;
        plan.constrainHauling=constrainHauling;
        // The same physical staffing request supports every active recipe.
        // The feedback controller may later adjust it; temporary shortfalls are
        // represented by coverage, not by inventing demand for another building.
        plan.feeding=!rules.isHungerDisabled();
        plan.training=!rules.isUnitUpgradesDisabled() && strategy.upgrades.enabled;
        plan.projectiles=!rules.isPeacefulModeEnabled();
        if(rules.isPeacefulModeEnabled())plan.productionMask&=~(1u<<WARRIOR);
        plan.carriers=initialCarrierRequest(type,strategy);
        plan.oneWayTravelTicks=strategy.farming.management_radius*strategy.food.carrier_ticks_per_tile;
        plan.handlingTicks=strategy.food.carrier_fixed_ticks_per_trip;
        return estimateFeeding(type,plan);
    }


	int clamp_score(int value)
	{
		return std::max(0, std::min(100, value));
	}

	int staggered_phase(int configuredPhase, int interval, int team)
	{
		if(interval<=0)return 0;
		// 37 is coprime to the important Maxima cadences (100, 150, 200,
		// 250 and 512). Each team therefore receives a stable phase without
		// collapsing onto a small common divisor of those intervals.
		const int value=(configuredPhase+team*37)%interval;
		return value<0 ? value+interval : value;
	}

	bool remembered_footprint_currently_visible(const AIEngine::AIWorldView& world,unsigned observedTeam,
		const Recon::BuildingSighting& building)
	{
				for(int dy=0; dy<building.height; ++dy)
			for(int dx=0; dx<building.width; ++dx)
				if(!AIEngine::ObservationQueries::visible(world,building.x+dx,
					building.y+dy, world.teams[observedTeam].mask))
					return false;
		return true;
	}

	int reconnaissance_building_value(const AIEngine::AIWorldView& game,int type,const MaximaStrategy::Reconnaissance& policy)
	{
	 const auto* b=&game.catalog->at(type).resolvedType;if(!b)return policy.default_building_value;
	 int value=policy.default_building_value;
	 for(const auto& entry:std::initializer_list<std::pair<int,int>>{{AIMaximaBuildings::Production,policy.swarm_building_value},{AIMaximaBuildings::Feeding,policy.inn_building_value},{AIMaximaBuildings::CombatTraining,policy.barracks_building_value},{AIMaximaBuildings::ConstructionTraining,policy.school_building_value}})
	  if(AIMaximaBuildings::serves(game,*b,entry.first))value=std::max(value,entry.second);
	 return value;
	}

	struct EconomicWatchSite
	{
		int x;
		int y;
		int score;
		int distance;
	};

	struct EconomicWatchTeam
	{
		int team;
		bool preferred;
		int lastActivity;
		int strategicValue;
	};

	bool better_economic_watch_site(const EconomicWatchSite& left,
		const EconomicWatchSite& right)
	{
		if(left.score!=right.score)
			return left.score>right.score;
		if(left.distance!=right.distance)
			return left.distance>right.distance;
		if(left.y!=right.y)
			return left.y<right.y;
		return left.x<right.x;
	}

	bool better_economic_watch_team(const EconomicWatchTeam& left,
		const EconomicWatchTeam& right)
	{
		if(left.preferred!=right.preferred)
			return left.preferred;
		if(left.lastActivity!=right.lastActivity)
			return left.lastActivity>right.lastActivity;
		if(left.strategicValue!=right.strategicValue)
			return left.strategicValue>right.strategicValue;
		return left.team<right.team;
	}

	void compute_preemptive_distance_field(int w, int h,
		const std::vector<Uint8>& walkable, const std::vector<int>& sources,
		std::vector<int>& distances)
	{
		distances.assign(w*h, PREEMPTIVE_UNREACHABLE);
		field::Frontier queue;
		for(std::vector<int>::const_iterator source=sources.begin();
			source!=sources.end(); ++source)
		{
			if(*source<0 || *source>=w*h || !walkable[*source]
			   || distances[*source]!=PREEMPTIVE_UNREACHABLE)
				continue;
			distances[*source]=0;
			queue.push_back(*source);
		}
		field::expandDistances(distances,queue,{w,h},field::Surrounding,PREEMPTIVE_UNREACHABLE,[&](int next){return walkable[next]!=0;});
	}

	struct ResourceAccessObservation
	{
		ResourceAccessObservation()
			: knownAlgaeUnits(0), walkingAlgaeUnits(0), swimmingAlgaeUnits(0),
			  accessibleAlgaeTiles(0), accessibleAlgaeUnits(0),
			  accessibleCornTiles(0), accessibleWoodTiles(0), accessibleStoneTiles(0)
		{
		}

		int knownAlgaeUnits;
		int walkingAlgaeUnits;
		int swimmingAlgaeUnits;
		int accessibleAlgaeTiles;
		int accessibleAlgaeUnits;
		int accessibleCornTiles;
		int accessibleWoodTiles;
		int accessibleStoneTiles;
	};

	void collect_worker_sources(const AIEngine::AIWorldView& world,unsigned observedTeam, bool swimming,
		const std::vector<Uint8>& walkable, std::vector<int>& sources)
	{
		const AIEngine::AIWorldView* map=&world;
		const int width=(*map).width;
		const int height=(*map).height;
		for(int id=0; id<Unit::MAX_COUNT; ++id)
		{
			const AIEngine::UnitView* worker=world.unitSlots(observedTeam)[id];
			if(!worker || worker->typeNum!=WORKER
			   || (swimming && worker->performance[SWIM]<=0))
				continue;
			const int x=(*map).normalizeX(worker->posX);
			const int y=(*map).normalizeY(worker->posY);
			const int index=y*width+x;
			if(walkable[index])
			{
				sources.push_back(index);
				continue;
			}
			// Workers can temporarily be represented on an occupied entrance tile.
			// Seed its free perimeter so that entering a building does not make the
			// colony appear disconnected for one strategy sample.
			for(int dy=-1; dy<=1; ++dy)
				for(int dx=-1; dx<=1; ++dx)
					if(dx || dy)
					{
						const int neighbor=((y+dy+height)%height)*width
							+((x+dx+width)%width);
						if(walkable[neighbor])
							sources.push_back(neighbor);
					}
		}
	}

	ResourceAccessObservation observe_resource_access(const AIEngine::AIWorldView& world,unsigned observedTeam,
		const std::vector<Uint8>& localTiles)
	{
		ResourceAccessObservation result;
		const AIEngine::AIWorldView* map=&world;
		const int width=(*map).width;
		const int height=(*map).height;
		std::vector<Uint8> walking(width*height, 0);
		std::vector<Uint8> swimming(width*height, 0);
		for(int y=0; y<height; ++y)
			for(int x=0; x<width; ++x)
			{
				const int index=y*width+x;
				const auto tile=AIEngine::ObservationQueries::spatialTile((*map),x, y);
				const bool clear=(tile.discovered&world.teams[observedTeam].mask)!=0
					&& !(tile.forbidden&world.teams[observedTeam].mask)
					&& tile.building==NOGBID
					&& tile.resource.type==NO_RES_TYPE;
				if(clear) {
					const auto& terrain=AIEngine::ObservationQueries::terrain((*map),index);
					swimming[index]=terrain.walkable || terrain.swimmable;
					walking[index]=terrain.walkable;
				} else swimming[index]=walking[index]=0;
			}

		std::vector<int> walkingSources;
		std::vector<int> swimmingSources;
		collect_worker_sources(world,observedTeam, false, walking, walkingSources);
		collect_worker_sources(world,observedTeam, true, swimming, swimmingSources);
		std::vector<int> walkingDistance;
		std::vector<int> swimmingDistance;
		compute_preemptive_distance_field(width, height, walking, walkingSources,
			walkingDistance);
		compute_preemptive_distance_field(width, height, swimming, swimmingSources,
			swimmingDistance);

		for(int y=0; y<height; ++y)
			for(int x=0; x<width; ++x)
			{
				const int index=y*width+x;
				const auto resource=map->resourceAt(index).resource;
				if(!localTiles[index]
				   || !(map->visibilityAt(index).discovered&world.teams[observedTeam].mask)
				   || resource.amount<=0
				   || (resource.type!=ALGA && resource.type!=WHEAT
					   && resource.type!=WOOD && resource.type!=STONE))
					continue;
				if(resource.type==ALGA)
					result.knownAlgaeUnits+=resource.amount;
				if(map->areasAt(index).forbidden&world.teams[observedTeam].mask)
					continue;
				bool walkingReach=false;
				bool swimmingReach=false;
				for(int dy=-1; dy<=1; ++dy)
					for(int dx=-1; dx<=1; ++dx)
						if(dx || dy)
						{
							const int neighbor=((y+dy+height)%height)*width
								+((x+dx+width)%width);
							walkingReach=walkingReach
								|| walkingDistance[neighbor]!=PREEMPTIVE_UNREACHABLE;
							swimmingReach=swimmingReach
								|| swimmingDistance[neighbor]!=PREEMPTIVE_UNREACHABLE;
						}
				if(resource.type==ALGA && walkingReach)
					result.walkingAlgaeUnits+=resource.amount;
				if(resource.type==ALGA && swimmingReach)
					result.swimmingAlgaeUnits+=resource.amount;
				if(walkingReach || swimmingReach)
				{
					switch(resource.type)
					{
						case WHEAT: ++result.accessibleCornTiles; break;
						case WOOD: ++result.accessibleWoodTiles; break;
						case STONE: ++result.accessibleStoneTiles; break;
						case ALGA:
							++result.accessibleAlgaeTiles;
							result.accessibleAlgaeUnits+=resource.amount;
							break;
					}
				}
			}
		return result;
	}
}

Maxima::StrategicSnapshot::StrategicSnapshot()
	: tick(0), population(0), workers(0), free_warriors(0), explorers(0),
	  trained_explorers(0), warriors(0), trained_workers(0),
	  trained_workers_level2(0), trained_warriors(0), swimming_workers(0),
	  swimming_explorers(0), swimming_warriors(0), amphibious_attack_explorers(0),
	  free_workers(0), worker_jobs_open(0), hungry(0),
	  critical_food(0), unserved_food(0), need_heal(0), buildings(0),
	  building_sites(0), swarms(0), completed_swarms(0), inns(0), inn_level1(0), inn_level2(0),
	  inn_level3(0), barracks(0), schools(0), school_level1(0),
	  school_level2(0), school_level3(0), pools(0),
	  hospitals(0), racetracks(0), towers(0), own_buildings_under_attack(0),
	  own_units_under_attack(0), visible_enemy_warriors(0),
	  visible_enemy_explorers(0), visible_enemy_attack_explorers(0),
	  visible_colony_explorer_threat(0), visible_colony_threat(0), alive_enemies(0),
	  total_hp(0), attack_power(0), prestige(0), enemy_prestige(0),
	  tower_stone(0), tower_bullets(0)
{
}


Maxima::StrategicTrends::StrategicTrends()
	: population(0), workers(0), warriors(0), food_pressure(0), colony_pressure(0)
{
}


Maxima::ClearedEnemySite::ClearedEnemySite()
	: x(0), y(0), confirmedTick(0)
{
}


Maxima::ClearedEnemySite::ClearedEnemySite(int siteX, int siteY, int tick)
	: x(siteX), y(siteY), confirmedTick(tick)
{
}


Maxima::DirectorPlan::DirectorPlan()
	: construction_sites(1), desired_inns(2), desired_swarms(1),
	  desired_barracks(0), desired_schools(0), desired_pools(0),
	  desired_racetracks(0), desired_hospital_beds(0), desired_towers(0), swarm_workers(3),
	  worker_ratio(4), explorer_ratio(1), warrior_ratio(0),
	  desired_explorers(3), desired_warriors(12), defense_reserve(8), attack_flags(0),
	  attack_units(10), allow_upgrades(false), allow_level2_upgrades(false),
	  upgrade_level1_workers(0), upgrade_level2_workers(0),
	  upgrade_level1_trained_units_per_slot(1), upgrade_level2_trained_units_per_slot(1),
	  upgrade_level1_inn_weight(0), upgrade_level1_hospital_weight(0),
	  upgrade_level1_racetrack_weight(0), upgrade_level1_pool_weight(0),
	  upgrade_level1_barracks_weight(0), upgrade_level2_inn_weight(0),
	  upgrade_level2_hospital_weight(0), upgrade_level2_racetrack_weight(0),
	  upgrade_level2_pool_weight(0), upgrade_level2_barracks_weight(0),
	  first_prestige_trained_workers(0), second_prestige_trained_workers(0),
	  second_prestige_population_min(0),
	  food_ledger_enabled(true), food_retirement_enabled(true),
	  food_inn_burden_percent(60), food_swarm_burden_percent(60),
	  food_recovered_percent(85), food_burden_confirm_ticks(3000),
	  food_retirement_cooldown_ticks(3000), food_relocation_enabled(true),
	  food_relocation_min_quality_tiles(5), food_relocation_confirm_ticks(3000),
	  food_relocation_cooldown_ticks(6000), food_relocation_offer_ticks(3000),
	  food_inn_seats_level1(0),
	  food_inn_seats_level2(0), food_inn_seats_level3(0),
	  swarm_supply_radius(12),
	  attack_clearing_workers(0),
	  can_swim(false), recovery_active(false), food_emergency(false),
	  colony_emergency(false), colony_swarm_requested(false),
	  colony_swarm_priority(0), explorer_campaign_active(false),
	  explorer_campaign_flags(0), explorer_campaign_units_per_flag(0),
	  fruit_active(false), fruit_units_per_flag(0), fruit_flag_radius(1),
	  reactive_defense_enabled(true),
	  reactive_defense_flag_radius(1), reactive_defense_move_radius(1),
	  reactive_defense_move_deadband(0),
	  reactive_defense_unit_cap(0), reactive_defense_advantage_min(0),
	  reactive_defense_advantage_percent(0), preemptive_defense_active(false),
	  preemptive_amphibious_active(false), preemptive_effective_zone_max(0),
	  target_switch_margin(0),
	  preemptive_recompute_ticks(1), preemptive_inner_distance(0),
	  preemptive_band_width(0), preemptive_path_slack(0),
	  preemptive_probe_radius(0), preemptive_cross_section_max(0),
	  preemptive_zone_radius(0), preemptive_zone_max(0),
	  reconnaissance_suspended(false), reconnaissance_flag_radius(1),
	  reconnaissance_review_interval(1), reconnaissance_economic_watch_revisit(1),
	  farming_normal_interval(1), farming_urgent_interval(1),
	  farming_enabled(true), farming_protection_enabled(true),
	  farming_maintenance_clearing_enabled(true),
	  farming_resource_preserving_circulation_enabled(true),
	  farming_wheat_invasion_clearing_enabled(true),
	  farming_wood_firebreak_enabled(true),
	  farming_proactive_clearing_enabled(true),
	  farming_urgent(false), farming_wood_pressure(0),
	  farming_minimum_wood_fertility(0), farming_allow_proactive_clearing(false),
	  farming_clearing_for_placement(false),
	  farming_min_workers_for_clearing(0),
	  farming_clearing_cooldown(1), farming_clearing_duration(1),
	  farming_clearing_quota(0),
	  farming_management_radius(0), farming_wheat_fertility_min(0), farming_wood_fertility_base_percent(0),
	  farming_wood_fertility_pressure_percent(0), farming_wood_pressure_base(0),
	  farming_wood_pressure_space_divisor(1), farming_wood_pressure_supply_divisor(1),
	  farming_wood_pressure_construction_divisor(1),
	  farming_wood_pressure_growth_divisor(1), farming_economic_envelope_radius(0),
	  priority_inns(50), priority_swarms(40), priority_barracks(30), priority_schools(25),
	  priority_pools(25), priority_racetracks(20), priority_hospitals(30),
	  priority_towers(20), tactical_kind(Tactics::MissionNone),
	  tactical_target_team(-1), tactical_dig_out_team(-1),
	  tactical_target_gid(-1), tactical_target_x(0),
	  tactical_target_y(0), tactical_candidate_score(INT_MIN),
	  tactical_requested_force(0), tactical_review_interval(100),
	  tactics_enabled(true), tactical_flag_level(2), tactical_siege_radius(6),
	  raid_flag_radius(2), tactical_stall_ticks(3000),
	  tactical_quarantine_enabled(true), tactical_quarantine_ticks(5000)
{
	const StaffingControl::Policy defaults;
	staffing_window_samples=defaults.windowSamples;
	staffing_low_permille=defaults.lowPermille;
	staffing_high_permille=defaults.highPermille;
	staffing_slack=defaults.slack;
	staffing_minimum_workers=defaults.minimumWorkers;
	staffing_maximum_workers=defaults.maximumWorkers;
	staffing_cooldown_passes=defaults.cooldownPasses;
	staffing_new_inn_workers=4;
	staffing_new_swarm_workers=8;
}


Maxima::PolicyBid::PolicyBid()
	: utility(0), construction_sites(0), desired_inns(0), desired_swarms(0),
	  desired_barracks(0), desired_schools(0), desired_pools(0),
	  desired_racetracks(0), desired_hospital_beds(0), desired_towers(0),
	  swarm_workers(0), worker_ratio(0), explorer_ratio(0), warrior_ratio(0),
	  desired_explorers(0), desired_warriors(0), defense_reserve(0),
	  attack_flags(0), attack_units(0), request_upgrades(false)
{
}


Maxima::EnvironmentModel::EnvironmentModel()
	: known_tiles(0), accessible_corn(0), accessible_corn_fraction(0), accessible_wood(0),
	  accessible_stone(0), accessible_algae(0), buildable_tiles(0),
	  water_tiles(0), feeding_capacity(0), food_headroom(70), resource_capacity(50), space_capacity(50),
	  food_security(70), abundance(50), terrain_abundance(50),
	  connected_abundance(50), mobility_opportunity(0),
	  economic_momentum(50), mobility_constraint(0),
	  topology_complexity(0), threat_pressure(0), confidence(0)
{
}


Maxima::StrategicDemands::StrategicDemands()
	: survival(0), food(30), growth(50), expansion(50), access(20), technology(30),
	  mobility(0), military(20), aggression(0)
{
}


 Maxima::OpponentAssessment::OpponentAssessment()
	: alive(false), visible_warriors(0), estimated_warriors(0),
	  last_observed_warriors(0), visible_explorers(0), visible_buildings(0),
	  known_buildings(0), reachable_buildings(0), strategic_value(0),
	  nearest_building(INT_MAX), score(INT_MIN), last_seen_tick(-1000000),
	  last_force_seen_tick(-1000000), last_building_seen_tick(-1000000),
	  intel_confidence(0)
{
}


Maxima::CampaignPlan::CampaignPlan()
	: state(CampaignIdle), target_team(-1), started_tick(0),
	  last_progress_tick(0), last_target_buildings(0), buildings_destroyed(0),
	  cooldown_until(0)
{
}


void Maxima::StrategyDirector::evaluate(Maxima& owner, Context& runtime)
{
	if(initialized && !dirty
	   && owner.snapshot.tick==owner.timer)
		return;
	owner.evaluate_strategy(runtime);
	committed();
}


const char* Maxima::posture_name(StrategicPosture selected) const
{
	static const char* names[PostureCount]={
		"expand", "develop", "mobilize", "campaign", "finish", "defend", "recover"
	};
	return names[int(selected)];
}


void Maxima::emit_telemetry(Context& runtime, const std::string& event,
	const std::string& fields) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	if(!telemetry_enabled())
		return;
    std::ostringstream text;
	text<<"MAXIMA_TELEMETRY\t"<<timer<<"\t"
		<<runtime.teamNumber()<<"\t"<<event<<fields
		<<"\tgame_tick="<<runtime.observation().tick<<'\n';
    runtime.bufferedDiagnostics.push_back({{},{},text.str(),true});
}


void Maxima::emit_ablation_opportunities(Context& runtime) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	if(!telemetry_enabled())
		return;
	Uint32 context=2166136261u;
	const int values[]={snapshot.population, snapshot.workers,
		snapshot.trained_warriors, snapshot.visible_colony_threat,
		snapshot.visible_enemy_attack_explorers,
		snapshot.visible_colony_explorer_threat, snapshot.unserved_food,
		snapshot.critical_food, int(posture)};
	for(size_t i=0; i<sizeof(values)/sizeof(values[0]); ++i)
	{
		context^=static_cast<Uint32>(values[i]);
		context*=16777619u;
	}
	const int player=runtime.playerNumber();
	const int team=runtime.teamNumber();
	const Uint32 eventTick=runtime.observation().tick;
	const int population=std::max(1, snapshot.population);
	const bool rawFood=snapshot.population>1 && (
		snapshot.unserved_food*100
			>=snapshot.population*strategy.emergencies.food_unserved_percent
		|| snapshot.critical_food*100
			>=snapshot.population*strategy.emergencies.food_critical_percent
		|| std::max(snapshot.unserved_food,snapshot.critical_food)*100
			>=snapshot.population*strategy.emergencies.food_combined_percent);
	const bool rawExplorer=
		snapshot.visible_enemy_attack_explorers
			>=strategy.reconnaissance.explorer_attack_warning_threshold
		|| snapshot.visible_colony_explorer_threat
			>=strategy.reconnaissance.explorer_colony_warning_threshold;
	const bool rawColony=rawExplorer
		|| snapshot.visible_colony_threat
			>=strategy.emergencies.colony_threat_threshold
		|| snapshot.own_buildings_under_attack
			>=strategy.emergencies.buildings_under_attack_threshold
		|| snapshot.own_units_under_attack
			>=strategy.emergencies.units_under_attack_threshold;
	const int foodSeverity=100*std::max(snapshot.unserved_food,
		snapshot.critical_food)/population;
	const int colonySeverity=std::max(snapshot.visible_colony_threat,
		snapshot.own_units_under_attack);
	const int tacticalSeverity=std::max(budget.tactical_requested_force,
		budget.attack_units);
	auto emit=[&](const std::string& key, bool enabled, int severity)
	{
        std::ostringstream text;
		text<<"MAXIMA_ABLATION_OPPORTUNITY"
			<<"\ttick="<<eventTick<<"\tteam="<<team<<"\tplayer="<<player
			<<"\tswitch="<<key<<"\tenabled="<<(enabled ? 1 : 0)
			<<"\tseverity="<<severity<<"\tcontext_hash="
			<<std::hex<<context<<std::dec<<'\n';
        runtime.bufferedDiagnostics.push_back({{},{},text.str(),true});
	};
	if(timer==1)
	{
		emit("farming.enabled", strategy.farming.enabled,
			environment.food_security);
		emit("recon.enabled", strategy.reconnaissance.enabled,
			reconnaissance.report().exploredPercent);
		emit("economy.food_service_safeguards_enabled",
			strategy.economy.food_service_safeguards_enabled, foodSeverity);
		emit("economy.large_economy_adaptation_enabled",
			strategy.economy.large_economy_adaptation_enabled,
			snapshot.population);
		emit("upgrades.enabled", strategy.upgrades.enabled, snapshot.population);
	}
	const bool postureEnabled=posture==PostureExpand
		? strategy.postures.expand_enabled : posture==PostureDevelop
		? strategy.postures.develop_enabled : posture==PostureMobilize
		? strategy.postures.mobilize_enabled : posture==PostureCampaign
		? strategy.postures.campaign_enabled : posture==PostureFinish
		? strategy.postures.finish_enabled : posture==PostureDefend
		? strategy.postures.defend_enabled : strategy.postures.recover_enabled;
	emit(std::string("postures.")+posture_name(posture)+"_enabled",
		postureEnabled, posture_utilities[posture]);
	if(budget.colony_swarm_requested)
		emit("colonization.enabled", strategy.colonization.enabled,
			budget.colony_swarm_priority);
	if(budget.tactical_kind!=Tactics::MissionNone
		|| budget.tactical_dig_out_team>=0)
		emit("tactics.enabled", strategy.tactics.enabled, tacticalSeverity);
	if(snapshot.visible_colony_threat>0 || snapshot.own_buildings_under_attack>0
		|| snapshot.own_units_under_attack>0)
		emit("defense.reactive.enabled", strategy.reactive_defense.enabled,
			colonySeverity);
	if(budget.preemptive_defense_active)
		emit("military.preemptive_defense_enabled",
			strategy.military.preemptive_defense_enabled,
			budget.preemptive_effective_zone_max);
	if(rawExplorer)
		emit("military.explorer_defense_enabled",
			strategy.military.explorer_defense_enabled,
			std::max(snapshot.visible_enemy_attack_explorers,
				snapshot.visible_colony_explorer_threat));
	if(rawFood)
		emit("emergencies.food_enabled", strategy.emergencies.food_enabled,
			foodSeverity);
	if(rawColony)
		emit("emergencies.colony_enabled", strategy.emergencies.colony_enabled,
			colonySeverity);
}


Maxima::StrategicSnapshot Maxima::collect_snapshot(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	StrategicSnapshot state;
    collect_building_profiles();
    std::array<long long,3> recurringMeals{};
	state.tick=timer;
	const TeamStat* stat=&runtime.observedTeam().statistics;
	state.population=stat->totalUnit;
	state.workers=stat->numberUnitPerType[WORKER];
	state.free_warriors=stat->isFree[WARRIOR];
	state.explorers=stat->numberUnitPerType[EXPLORER];
	state.trained_explorers=
		stat->upgradeStatePerType[EXPLORER][MAGIC_ATTACK_GROUND][3];
	state.warriors=stat->numberUnitPerType[WARRIOR];
	state.free_workers=stat->isFree[WORKER];
	state.worker_jobs_open=stat->totalNeeded;
	state.hungry=stat->needFood;
	state.critical_food=stat->needFoodCritical;
	state.unserved_food=stat->needFoodNoInns;
	if (runtime.observation().configuration->isHungerDisabled())
		state.hungry=state.critical_food=state.unserved_food=0;
	state.need_heal=stat->needHeal;
	state.buildings=stat->totalBuilding;
	for(int id=0;id<Building::MAX_COUNT;++id) {
	 const auto* b=runtime.observation().buildingSlots(runtime.teamNumber())[id];if(!b||b->buildingState==Building::DEAD)continue;
	 const unsigned roles=AIMaximaBuildings::capabilities(runtime.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*b));
	 auto has=[&](int role){return bool(roles&AIMaximaBuildings::roleBit(role));};
	 state.swarms+=has(AIMaximaBuildings::Production);state.inns+=has(AIMaximaBuildings::Feeding);
	 state.barracks+=has(AIMaximaBuildings::CombatTraining);state.schools+=has(AIMaximaBuildings::ConstructionTraining);
	 state.pools+=has(AIMaximaBuildings::SwimTraining);state.hospitals+=has(AIMaximaBuildings::Healing);
	 state.racetracks+=has(AIMaximaBuildings::WalkTraining);state.towers+=has(AIMaximaBuildings::ProjectileDefense);
	 if(!AIEngine::ObservationQueries::buildingType(runtime.observation(),*b).isBuildingSite) {
	  const int level=std::min(3,AIMaximaBuildings::lineagePosition(runtime.observation(),b->typeNum));
	  if(has(AIMaximaBuildings::Feeding)){if(level==1)++state.inn_level1;else if(level==2)++state.inn_level2;else ++state.inn_level3;}
	  if(has(AIMaximaBuildings::ConstructionTraining)){if(level==1)++state.school_level1;else if(level==2)++state.school_level2;else ++state.school_level3;}
	 }
	}
	state.total_hp=stat->totalHP;
	state.attack_power=stat->totalAttackPower;
	state.prestige=runtime.observedTeam().prestige;
	for(enemy_team_iterator enemy(runtime); enemy!=enemy_team_iterator(); ++enemy)
	{
		const AIEngine::TeamView* hostile=(&runtime.observation().teams[*enemy]);
		if(hostile && hostile->alive)
			state.enemy_prestige+=hostile->prestige;
	}

	for(int level=1; level<4; ++level)
	{
		state.trained_workers+=stat->workersByConstructionLevel[level];
		if(level>=2)
			state.trained_workers_level2+=
				stat->workersByConstructionLevel[level];
		state.trained_warriors+=stat->upgradeStatePerType[WARRIOR][ATTACK_SPEED][level];
		// Level zero means the unit cannot swim. Counting it here made Maxima
		// treat every sizeable army as amphibious and issue impossible routes
		// across water on maps such as G2.
		state.swimming_workers+=stat->upgradeStatePerType[WORKER][SWIM][level];
		state.swimming_explorers+=stat->upgradeStatePerType[EXPLORER][SWIM][level];
		state.swimming_warriors+=stat->upgradeStatePerType[WARRIOR][SWIM][level];
	}
	for(int i=0; i<Unit::MAX_COUNT; ++i)
	{
		const AIEngine::UnitView* unit=runtime.observation().unitSlots(runtime.teamNumber())[i];
        if(unit && !unit->isDead)recurringMeals[unit->typeNum]+=recipient_meal_rate(*unit);
		if(unit && unit->underAttackTimer)
			state.own_units_under_attack+=1;
		if(unit && unit->typeNum==EXPLORER
		   && unit->performance[SWIM]>0
		   && unit->performance[MAGIC_ATTACK_GROUND]>0)
			state.amphibious_attack_explorers+=1;
	}
    for(int unit=0;unit<3;++unit)state.feeding_demand[unit]=int(std::min<long long>(INT_MAX,(recurringMeals[unit]+MealRatePrecision/2)/MealRatePrecision));

	for(int i=0; i<Building::MAX_COUNT; ++i)
	{
		const AIEngine::BuildingView* building=runtime.observation().buildingSlots(runtime.teamNumber())[i];
		if(!building)
			continue;
		// Only a level-zero site is new construction. Upgrade sites retain the
		// building's previous level and are governed by the separate quotas.
		if(AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).isBuildingSite&&building->constructionResultState==Building::NEW_BUILDING)
			state.building_sites+=1;
		if(AIMaximaBuildings::serves(context.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*building),AIMaximaBuildings::Production)
		   && !AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).isBuildingSite
		   && building->constructionResultState==Building::NO_CONSTRUCTION)
			++state.completed_swarms;
		if(building->underAttackTimer)
			state.own_buildings_under_attack+=1;
		if(!AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).isBuildingSite
		   && AIMaximaBuildings::serves(context.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*building),AIMaximaBuildings::ProjectileDefense))
		{
			state.tower_stone+=runtime.observation().buildingResources(*building)[STONE];
			state.tower_bullets+=building->bullets;
		}
	}
	const Recon::ReconReport& intel=reconnaissance.report();
	state.alive_enemies=intel.aliveEnemies;
	state.visible_enemy_warriors=intel.visibleWarriors;
	state.visible_enemy_explorers=intel.visibleExplorers;
	state.visible_enemy_attack_explorers=intel.visibleAttackExplorers;
	state.visible_colony_threat=intel.visibleColonyThreat;
	state.visible_colony_explorer_threat=intel.visibleColonyExplorerThreat;
	// In this snapshot trained_warriors measures usable production capacity,
	// not engine levels. No training means all standing warriors clear the
	// backlog; actual starting levels stay intact for combat calculations.
	if (runtime.observation().configuration->isUnitUpgradesDisabled()) state.trained_warriors=state.warriors;
	return state;
}


void Maxima::update_trends()
{
	telemetry.count(AITrace::AI7::Maxima_update_trends_calls);
	// Unserved hunger includes critically hungry units that cannot currently eat.
	// Use the larger count as the pressure population instead of double-counting
	// the overlap.
	const int old_food=std::max(previous_snapshot.unserved_food,
		previous_snapshot.critical_food);
	const int new_food=std::max(snapshot.unserved_food,snapshot.critical_food);
	const MaximaStrategy::Trends& policy=strategy.trends;
	trends.population=(trends.population*policy.history_weight
		+(snapshot.population-previous_snapshot.population)*policy.observation_scale)
		/policy.total_weight;
	trends.workers=(trends.workers*policy.history_weight
		+(snapshot.workers-previous_snapshot.workers)*policy.observation_scale)
		/policy.total_weight;
	trends.warriors=(trends.warriors*policy.history_weight
		+(snapshot.warriors-previous_snapshot.warriors)*policy.observation_scale)
		/policy.total_weight;
	trends.food_pressure=(trends.food_pressure*policy.history_weight
		+(new_food-old_food)*policy.observation_scale)/policy.total_weight;
	trends.colony_pressure=(trends.colony_pressure*policy.history_weight
		+(snapshot.visible_colony_threat-previous_snapshot.visible_colony_threat)
			*policy.observation_scale)/policy.total_weight;
}


void Maxima::initialize_topology_profile(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	if(topology_initialized)
		return;
	MapInfo map(runtime);
	const int width=map.get_width();
	const int height=map.get_height();
	const int tiles=std::max(1, width*height);
	int water=0;
	int shoreline_edges=0;
	int land=0;
	int chokepoints=0;
	int buildable=0;
	int corn=0;
	int wood=0;
	int stone=0;
	int algae=0;
	int fruit=0;
	int grass=0;
	std::vector<int> component(tiles, -1);
	for(int x=0; x<width; ++x)
		for(int y=0; y<height; ++y)
		{
			const bool here=map.is_water(x, y);
			const bool resource=map.is_resource(x, y);
			water+=here ? 1 : 0;
			land+=map.is_walkable(x,y) ? 1 : 0;
			grass+=map.is_crop_habitat(x, y) ? 1 : 0;
			buildable+=map.is_grass(x, y) && !resource ? 1 : 0;
			corn+=map.is_resource(x, y, WHEAT) ? 1 : 0;
			wood+=map.is_resource(x, y, WOOD) ? 1 : 0;
			stone+=map.is_resource(x, y, STONE) ? 1 : 0;
			algae+=map.is_resource(x, y, ALGA) ? 1 : 0;
			fruit+=map.is_resource(x, y, CHERRY)
				|| map.is_resource(x, y, ORANGE)
				|| map.is_resource(x, y, PRUNE) ? 1 : 0;
			shoreline_edges+=here!=map.is_water((x+1)%width, y) ? 1 : 0;
			shoreline_edges+=here!=map.is_water(x, (y+1)%height) ? 1 : 0;
			if(map.is_walkable(x,y))
			{
				const bool horizontal=map.is_walkable((x+width-1)%width, y)
					&& map.is_walkable((x+1)%width, y);
				const bool vertical=map.is_walkable(x, (y+height-1)%height)
					&& map.is_walkable(x, (y+1)%height);
				const bool blocked_horizontal=!map.is_walkable((x+width-1)%width, y)
					&& !map.is_walkable((x+1)%width, y);
				const bool blocked_vertical=!map.is_walkable(x, (y+height-1)%height)
					&& !map.is_walkable(x, (y+1)%height);
				if((horizontal && blocked_vertical)
				   || (vertical && blocked_horizontal))
					chokepoints+=1;
			}
		}
	global_water_percent=water*100/tiles;
	// There are two undirected right/down edges per tile. Scale their land/water
	// transition rate onto a 0..100 axis.
	global_shoreline_density=clamp_score(
		shoreline_edges*strategy.environment.shoreline_density_scale/tiles);

	std::vector<int> component_sizes;
	for(int x=0; x<width; ++x)
		for(int y=0; y<height; ++y)
		{
			const int origin=y*width+x;
			if(!map.is_walkable(x, y) || component[origin]>=0)
				continue;
			const int label=component_sizes.size();
			int size=0;
			std::deque<int> pending;
			component[origin]=label;
			pending.push_back(origin);
			while(!pending.empty())
			{
				const int current=pending.front();
				pending.pop_front();
				size+=1;
				const int cx=current%width;
				const int cy=current/width;
				const int nx[4]={ (cx+width-1)%width, (cx+1)%width, cx, cx };
				const int ny[4]={ cy, cy, (cy+height-1)%height, (cy+1)%height };
				for(int direction=0; direction<4; ++direction)
				{
					const int next=ny[direction]*width+nx[direction];
					if(component[next]<0 && map.is_walkable(nx[direction], ny[direction]))
					{
						component[next]=label;
						pending.push_back(next);
					}
				}
			}
			component_sizes.push_back(size);
		}
	global_land_components=component_sizes.size();
	int largest=0;
	int largest_label=-1;
	for(std::vector<int>::const_iterator size=component_sizes.begin();
		size!=component_sizes.end(); ++size)
	{
		if(*size>largest)
		{
			largest=*size;
			largest_label=size-component_sizes.begin();
		}
	}
	global_largest_land_percent=land>0 ? largest*100/land : 0;
	global_start_land_percent=global_largest_land_percent;
	std::vector<int> component_buildable(component_sizes.size(), 0);
	std::vector<int> component_corn(component_sizes.size(), 0);
	std::vector<int> component_wood(component_sizes.size(), 0);
	std::vector<int> component_stone(component_sizes.size(), 0);
	std::vector<int> component_fruit(component_sizes.size(), 0);
	for(int x=0; x<width; ++x)
		for(int y=0; y<height; ++y)
		{
			const int label=component[y*width+x];
			if(label<0)
				continue;
			component_buildable[label]+=map.is_grass(x, y)
				&& !map.is_resource(x, y) ? 1 : 0;
			component_corn[label]+=map.is_resource(x, y, WHEAT) ? 1 : 0;
			component_wood[label]+=map.is_resource(x, y, WOOD) ? 1 : 0;
			component_stone[label]+=map.is_resource(x, y, STONE) ? 1 : 0;
			component_fruit[label]+=map.is_resource(x, y, CHERRY)
				|| map.is_resource(x, y, ORANGE)
				|| map.is_resource(x, y, PRUNE) ? 1 : 0;
		}
	int start_label=largest_label;
	for(int id=0; id<Building::MAX_COUNT; ++id)
	{
		const AIEngine::BuildingView* building=runtime.observation().buildingSlots(runtime.teamNumber())[id];
		if(!building || AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).isVirtual)
			continue;
		const int bx=(building->posX%width+width)%width;
		const int by=(building->posY%height+height)%height;
		start_label=component[by*width+bx];
		if(start_label>=0 && start_label<static_cast<int>(component_sizes.size()))
			global_start_land_percent=land>0
				? component_sizes[start_label]*100/land : 0;
		break;
	}
	if(start_label<0 || start_label>=static_cast<int>(component_sizes.size()))
		start_label=largest_label;
	global_chokepoint_density=land>0
		? clamp_score(chokepoints*100/land) : 0;

	// The terrain profile is deliberately computed from the complete map. It is
	// equivalent to the information a human has after selecting a known map and
	// must not drift with fog-of-war discovery or opening build order.
	global_water_tiles=water;
	global_grass_tiles=grass;
	global_land_tiles=land;
	global_buildable_tiles=buildable;
	global_corn_tiles=corn;
	global_wood_tiles=wood;
	global_stone_tiles=stone;
	global_algae_tiles=algae;
	global_fruit_tiles=fruit;
	const int participants=std::max(1,
		runtime.observation().configuration->getNumberOfPlayers());
	const int global_food_per_player=(corn+fruit/2)/participants;
	const int global_material_per_player=(wood+stone*3+algae*2)/participants;
	const int global_space_per_player=buildable/participants;
	// Convert per-player carrying capacity to broad 0..100 axes. The offsets
	// exclude the resources needed merely to survive; the spans distinguish low,
	// medium, and genuinely huge economies without saturating ordinary maps.
	const MaximaStrategy::Environment& policy=strategy.environment;
	const int global_food_score=clamp_score(
		(global_food_per_player-policy.terrain_food_offset)*100
		/policy.terrain_food_span);
	const int global_material_score=clamp_score(
		(global_material_per_player-policy.terrain_material_offset)*100
		/policy.terrain_material_span);
	const int global_space_score=clamp_score(
		(global_space_per_player-policy.terrain_space_offset)*100
		/policy.terrain_space_span);
	global_terrain_abundance=clamp_score(
		(global_food_score*policy.terrain_food_weight
			+global_material_score*policy.terrain_material_weight
			+global_space_score*policy.terrain_space_weight)/100);

	int start_resource_points=0;
	int start_buildable=0;
	int start_size=0;
	if(start_label>=0)
	{
		start_size=component_sizes[start_label];
		start_buildable=component_buildable[start_label];
		start_resource_points=component_corn[start_label]*policy.corn_resource_value
			+component_fruit[start_label]*policy.fruit_resource_value
			+component_wood[start_label]*policy.wood_resource_value
			+component_stone[start_label]*policy.stone_resource_value;
		const int expected_start_participants=std::max(1,
			(participants*start_size+std::max(1, land)/2)/std::max(1, land));
		const int connected_food_per_player=
			(component_corn[start_label]+component_fruit[start_label]/2)
				/expected_start_participants;
		const int connected_material_per_player=
			(component_wood[start_label]+component_stone[start_label]*3)
				/expected_start_participants;
		const int connected_space_per_player=start_buildable
			/expected_start_participants;
		const int connected_food_score=clamp_score(
			(connected_food_per_player-policy.terrain_food_offset)*100
			/policy.terrain_food_span);
		const int connected_material_score=clamp_score(
			(connected_material_per_player-policy.terrain_material_offset)*100
			/policy.terrain_material_span);
		const int connected_space_score=clamp_score(
			(connected_space_per_player-policy.terrain_space_offset)*100
			/policy.terrain_space_span);
		global_connected_abundance=clamp_score(
			(connected_food_score*policy.terrain_food_weight
				+connected_material_score*policy.terrain_material_weight
				+connected_space_score*policy.terrain_space_weight)/100);
	}
	else
		global_connected_abundance=global_terrain_abundance;
	const int total_resource_points=corn*policy.corn_resource_value
		+fruit*policy.fruit_resource_value+wood*policy.wood_resource_value
		+stone*policy.stone_resource_value+algae*policy.algae_resource_value;
	const int outside_resource_percent=total_resource_points>0
		? (total_resource_points-start_resource_points)*100/total_resource_points : 0;
	const int outside_land_percent=land>0 ? (land-start_size)*100/land : 0;
	// This is opportunity stranded behind mobility, not a declaration that water
	// is intrinsically bad. A rich archipelago scores high; a large connected
	// continent with decorative water does not.
	global_mobility_opportunity=clamp_score(
		(outside_resource_percent*policy.mobility_outside_resource_weight
			+outside_land_percent+(100-global_start_land_percent))
			/policy.mobility_opportunity_divisor
		+global_shoreline_density/policy.mobility_shoreline_divisor);
	topology_initialized=true;
}


void Maxima::update_environment_model(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	telemetry.count(AITrace::AI7::Maxima_update_environment_model_calls);
	initialize_topology_profile(runtime);
	const MaximaStrategy::Environment& policy=strategy.environment;
	EnvironmentModel observed;
	observed.known_tiles=0;
	observed.accessible_corn=0;
	observed.accessible_wood=0;
	observed.accessible_stone=0;
	observed.accessible_algae=0;
	observed.buildable_tiles=0;
	observed.water_tiles=0;
    collect_building_profiles();
	std::array<long long,8> feedingRates{};
	for(int id=0;id<Building::MAX_COUNT;++id){const auto* b=runtime.observation().buildingSlots(runtime.teamNumber())[id];
	 if(!b||AIEngine::ObservationQueries::buildingType(runtime.observation(),*b).isBuildingSite||b->buildingState!=Building::ALIVE||!AIMaximaBuildings::serves(runtime.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*b),AIMaximaBuildings::Feeding))continue;
     const unsigned mask=AIEngine::ObservationQueries::buildingType(runtime.observation(),*b).semantics.feeding.unitMask&AIEngine::ObservationQueries::buildingType(runtime.observation(),*b).semantics.admittedUnitMask&7u;
     feedingRates[mask]+=development_feeding_visit_rate[b->typeNum];
	}
    observed.feeding_capacity=aggregate_feeding_capacity(feedingRates);
	observed.terrain_abundance=global_terrain_abundance;
	observed.connected_abundance=global_connected_abundance;
	observed.mobility_opportunity=global_mobility_opportunity;

	// Measure the discovered working territory around the current colony.  This
	// naturally expands as the colony and its knowledge expand, so an island can
	// be reclassified after swimming opens new resources without knowing the map
	// name or peeking through fog of war.
	MapInfo map(runtime);
	const int map_width=map.get_width(),map_height=map.get_height();
	std::vector<Uint8> local_tiles(map_width*map_height,0);
	for(int id=0;id<Building::MAX_COUNT;++id)
	{
		const AIEngine::BuildingView* building=runtime.observation().buildingSlots(runtime.teamNumber())[id];
		if(!building||AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).isVirtual)continue;
		for(int dy=-policy.local_territory_radius;
			dy<=policy.local_territory_radius;++dy)
			for(int dx=-policy.local_territory_radius;
				dx<=policy.local_territory_radius;++dx)
			if(dx*dx+dy*dy<=policy.local_territory_radius
				*policy.local_territory_radius)
			{
				const int x=runtime.observation().normalizeX(building->posX+dx);
				const int y=runtime.observation().normalizeY(building->posY+dy);
				local_tiles[y*map_width+x]=1;
			}
	}
	// Use the same worker connectivity for food and materials as for algae.
	// Nearby deposits across water or behind blocked routes are not live supply.
	const ResourceAccessObservation resources=
		observe_resource_access(runtime.observation(),runtime.teamNumber(),local_tiles);
	known_algae_units=resources.knownAlgaeUnits;
	walk_accessible_algae_units=resources.walkingAlgaeUnits;
	swim_accessible_algae_units=resources.swimmingAlgaeUnits;
	accessible_algae_units=resources.accessibleAlgaeUnits;
	observed.accessible_algae=resources.accessibleAlgaeTiles;
	std::set<int> food_tiles;
	long long food_capacity=0;
	std::vector<const AIEngine::BuildingView*> food_sources;
	BuildingSearch food_buildings(runtime);
	food_buildings.add_condition(new NotUnderConstruction);
	for(building_search_iterator i=food_buildings.begin();i!=food_buildings.end();++i)
		if(runtime.get_building_register().has_role(*i,AIMaximaBuildings::Feeding)
		   || runtime.get_building_register().has_role(*i,AIMaximaBuildings::Production))
		{
			food_capacity+=nearby_farm_capacity(runtime,*i,&food_tiles);
			const AIEngine::BuildingView* source=runtime.get_building_register().get_building(*i);
			if(source)food_sources.push_back(source);
		}
	if(food_capacity==0 && !food_sources.empty())
		food_capacity=distantFoodCapacity(&runtime.observation(),food_sources,
			runtime.observedTeam().mask,budget.can_swim,budget.swarm_supply_radius,
			fertility_cache,&applied_farm_protection_mask,
			strategy.farming.wheat_stock_horizon_ticks);
	observed.accessible_corn=int(food_capacity/65536);
	observed.accessible_corn_fraction=int(food_capacity%65536);
	observed.accessible_wood=resources.accessibleWoodTiles;
	observed.accessible_stone=resources.accessibleStoneTiles;
	for(int x=0; x<map.get_width(); ++x)
	{
		for(int y=0; y<map.get_height(); ++y)
		{
			if(!map.is_discovered(x, y))
				continue;
			if(!local_tiles[y*map_width+x])
				continue;
			observed.known_tiles+=1;
			if(map.is_water(x, y))
				observed.water_tiles+=1;
			if(map.is_grass(x, y) && !map.is_resource(x, y))
				observed.buildable_tiles+=1;
		}
	}

	const TeamStat* stat=&runtime.observedTeam().statistics;
	const int population=std::max(1, snapshot.population);
	const int food_stress=(snapshot.hungry*policy.hungry_weight
		+snapshot.critical_food*policy.critical_food_weight
		+snapshot.unserved_food*policy.unserved_food_weight)/population;
	const int reserve=stat->totalFoodCapacity>0
		? std::min(100, stat->totalFood*100/stat->totalFoodCapacity) : 0;
	observed.food_security=clamp_score(policy.food_security_base
		+reserve/policy.reserve_divisor-food_stress);
	observed.food_headroom=policy.food_headroom_default;
	if(snapshot.population>=policy.food_headroom_population_min
		&& observed.feeding_capacity>0)
	{
		observed.food_headroom=clamp_score(policy.food_headroom_base
			+(observed.feeding_capacity-snapshot.population)*100/population);
	}
	const int food_access=std::min(100,
		observed.accessible_corn*policy.food_access_scale
		/(population+policy.food_access_population_offset));
	const int material_access=std::min(100,
		observed.accessible_wood*policy.material_wood_weight
			+observed.accessible_stone*policy.material_stone_weight
			+observed.accessible_algae*policy.material_algae_weight);
	const int unconstrained_space_capacity=clamp_score(
		observed.buildable_tiles*policy.space_scale
			/(population+policy.space_population_offset)
		-recent_construction_failures*policy.construction_failure_space_penalty);
	opening_space_constrained=Farming::openingSpaceConstrained(
		unconstrained_space_capacity, strategy.farming.urgent_space_threshold,
		recent_construction_failures, strategy.farming.proactive_failure_threshold,
		timer-last_construction_failure_tick,
		strategy.farming.proactive_duration_ticks);
	observed.resource_capacity=clamp_score(
		(food_access*policy.resource_food_weight
			+material_access*policy.resource_material_weight
			+observed.food_security*policy.resource_security_weight)/100
		-(opening_space_constrained ? policy.constrained_resource_penalty : 0));
	observed.space_capacity=clamp_score(
		unconstrained_space_capacity
		-(opening_space_constrained ? policy.constrained_space_penalty : 0));
	const int water_percent=observed.known_tiles>0
		? observed.water_tiles*100/observed.known_tiles : 0;
	observed.mobility_constraint=clamp_score(water_percent
		+(known_algae_units>walk_accessible_algae_units
			&& snapshot.swimming_workers==0
			? policy.algae_mobility_penalty : 0)
		+(observed.accessible_corn<policy.scarce_corn_threshold
			&& water_percent>=policy.water_mobility_threshold
			? policy.scarce_corn_mobility_penalty : 0));
	// Water coverage alone is not a mobility problem: Isles has abundant water
	// around one continuous continent, while FourSquares has less water but splits
	// useful land into isolated pockets. Weight actual fragmentation and boundary
	// complexity much more heavily than the raw blue-tile percentage.
	const int land_isolation=100-global_start_land_percent;
	const int topology_prior=clamp_score(
		global_water_percent*land_isolation/policy.topology_water_isolation_divisor
		+global_shoreline_density
		+std::min(policy.topology_component_cap,
			std::max(0, global_land_components-1)*policy.topology_component_weight)
			*land_isolation/100);
	const int live_mobility=clamp_score(
		(observed.mobility_constraint-policy.live_mobility_baseline)
		*policy.live_mobility_weight);
	observed.topology_complexity=std::max(topology_prior, live_mobility);
	observed.economic_momentum=clamp_score(policy.momentum_base
		+trends.population*policy.momentum_population_weight+trends.workers
		-std::max(0, trends.food_pressure)*policy.momentum_food_weight
		-recent_construction_failures*policy.momentum_failure_weight);
	observed.abundance=clamp_score(
		(observed.resource_capacity*policy.abundance_resource_weight
		+observed.space_capacity+observed.food_security
		+observed.economic_momentum+observed.terrain_abundance
		+observed.connected_abundance)/policy.abundance_divisor);
	observed.threat_pressure=clamp_score(
		snapshot.visible_colony_threat*policy.threat_colony_weight
		+snapshot.visible_colony_explorer_threat*policy.threat_explorer_weight
		+snapshot.own_buildings_under_attack*policy.threat_building_attack_weight
		+snapshot.own_units_under_attack*policy.threat_unit_attack_weight);
	observed.confidence=clamp_score(timer/policy.confidence_tick_divisor
		+observed.known_tiles/policy.confidence_tile_divisor);

	if(!director.initialized)
	{
		environment=observed;
		return;
	}
	// Counts are current observations for telemetry.  Strategic axes are
	// smoothed to prevent one unlucky harvest or sighting from thrashing policy.
	environment.known_tiles=observed.known_tiles;
	environment.accessible_corn=observed.accessible_corn;
	environment.accessible_corn_fraction=observed.accessible_corn_fraction;
	environment.accessible_wood=observed.accessible_wood;
	environment.accessible_stone=observed.accessible_stone;
	environment.accessible_algae=observed.accessible_algae;
	environment.buildable_tiles=observed.buildable_tiles;
	environment.water_tiles=observed.water_tiles;
	environment.feeding_capacity=observed.feeding_capacity;
	environment.food_headroom=(environment.food_headroom
		*policy.fast_smoothing_history_weight+observed.food_headroom)
		/policy.fast_smoothing_total_weight;
	environment.resource_capacity=(environment.resource_capacity
		*policy.smoothing_history_weight+observed.resource_capacity)
		/policy.smoothing_total_weight;
	environment.space_capacity=(environment.space_capacity
		*policy.smoothing_history_weight+observed.space_capacity)
		/policy.smoothing_total_weight;
	environment.food_security=(environment.food_security
		*policy.smoothing_history_weight+observed.food_security)
		/policy.smoothing_total_weight;
	environment.abundance=(environment.abundance*policy.smoothing_history_weight
		+observed.abundance)/policy.smoothing_total_weight;
	environment.terrain_abundance=observed.terrain_abundance;
	environment.connected_abundance=observed.connected_abundance;
	environment.mobility_opportunity=observed.mobility_opportunity;
	environment.economic_momentum=(environment.economic_momentum
		*policy.smoothing_history_weight+observed.economic_momentum)
		/policy.smoothing_total_weight;
	environment.mobility_constraint=(environment.mobility_constraint
		*policy.smoothing_history_weight+observed.mobility_constraint)
		/policy.smoothing_total_weight;
	environment.topology_complexity=(environment.topology_complexity
		*policy.smoothing_history_weight+observed.topology_complexity)
		/policy.smoothing_total_weight;
	environment.threat_pressure=(environment.threat_pressure
		*policy.fast_smoothing_history_weight+observed.threat_pressure)
		/policy.fast_smoothing_total_weight;
	environment.confidence=observed.confidence;
	if(strategy.economy.large_economy_adaptation_enabled
	   && snapshot.population>=policy.large_economy_population_min
	   && observed.terrain_abundance>=policy.large_economy_terrain_min
	   && observed.food_security>=policy.large_economy_food_min
	   && (observed.connected_abundance>=policy.large_economy_connected_min
		   || (snapshot.swimming_workers>0
			   && observed.resource_capacity>=policy.large_economy_resource_min)))
		large_economy_committed=true;
}


void Maxima::score_demands()
{
	telemetry.count(AITrace::AI7::Maxima_score_demands_calls);
	const MaximaStrategy::Demands& policy=strategy.demands;
	int largest_enemy_force=0;
	bool known_target=false;
	for(int team=0; team<Team::MAX_COUNT; ++team)
	{
		if(!opponents[team].alive)
			continue;
		largest_enemy_force=std::max(largest_enemy_force,
			opponents[team].estimated_warriors);
		known_target=known_target || opponents[team].known_buildings>0;
	}
	const int population=std::max(1, snapshot.population);
	const int food_pressure=std::max(snapshot.unserved_food,snapshot.critical_food)
		*100/population;
	const int effective_capacity=clamp_score(
		(environment.resource_capacity*policy.capacity_resource_weight
			+environment.connected_abundance+environment.terrain_abundance)
			/policy.capacity_divisor);
	const int stranded_opportunity=snapshot.swimming_workers>0
		? environment.mobility_opportunity/policy.stranded_opportunity_divisor
		: environment.mobility_opportunity;
	demands.food=clamp_score(100-environment.food_security
		+(100-environment.resource_capacity)/policy.food_resource_divisor
		+(100-environment.food_headroom)/policy.food_headroom_divisor
		+food_pressure*policy.food_pressure_weight);
	// Feeding pressure must disappear before it suppresses growth and military budgets.
	if (context.observation().configuration->isHungerDisabled()) demands.food=0;
	demands.survival=clamp_score(std::max(demands.food,
		environment.threat_pressure)+
		(snapshot.population<policy.survival_population_threshold
			? policy.survival_population_bonus : 0));
	demands.growth=clamp_score((effective_capacity*policy.growth_capacity_weight
		+environment.space_capacity+environment.economic_momentum)
		/policy.growth_divisor
		-demands.food/policy.growth_food_divisor
		-environment.threat_pressure/policy.growth_threat_divisor
		+(snapshot.population<policy.growth_population_threshold
			? policy.growth_population_bonus : 0)
		+(environment.terrain_abundance-policy.growth_terrain_baseline)
			/policy.growth_terrain_divisor);
	demands.expansion=clamp_score((environment.space_capacity
		+effective_capacity*policy.growth_capacity_weight+demands.growth)
		/policy.expansion_divisor
		-recent_construction_failures*policy.expansion_failure_penalty);
	// Access demand is high when the known colony footprint cannot support its
	// population. Unlike raw growth, it funds the means to reach the next pocket
	// (scouting and mobility) rather than blindly adding more consumers.
	demands.access=clamp_score(policy.access_base-environment.resource_capacity
		+environment.mobility_constraint/policy.access_mobility_divisor
		+stranded_opportunity/policy.access_opportunity_divisor
		+(snapshot.population<policy.access_population_threshold
			? policy.access_population_bonus : 0)
		+(environment.accessible_corn<policy.access_corn_threshold
			? policy.access_corn_bonus : 0));
	// Scarcity increases the value of vertical efficiency once survival is
	// stable; abundance increases the ability to pay for it.  This U-shaped
	// demand is intentional and avoids making technology exclusive to one map.
	demands.technology=clamp_score(policy.technology_base
		+snapshot.population/policy.technology_population_divisor
		+(100-environment.resource_capacity)/policy.technology_scarcity_divisor
		+effective_capacity/policy.technology_capacity_divisor
		+environment.terrain_abundance/policy.technology_terrain_divisor
		+demands.growth/policy.technology_growth_divisor
		-demands.survival/policy.technology_survival_divisor);
	demands.mobility=clamp_score(environment.mobility_constraint
		+stranded_opportunity
		+(snapshot.schools==0
			&& known_algae_units>walk_accessible_algae_units
			? policy.mobility_capability_bonus : 0)
		-demands.survival/policy.mobility_survival_divisor);
	demands.military=clamp_score(policy.military_base
		+largest_enemy_force*policy.military_enemy_weight
		+environment.threat_pressure
		+environment.resource_capacity/policy.military_resource_divisor
		+(snapshot.population>=policy.military_population_threshold
			? policy.military_population_bonus : 0)
		+(known_target ? policy.military_target_bonus : 0)
		-demands.food/policy.military_food_divisor);
	demands.aggression=clamp_score((environment.resource_capacity
		+environment.economic_momentum)/policy.aggression_economy_divisor
		+snapshot.trained_warriors*policy.aggression_warrior_weight
		-largest_enemy_force*policy.aggression_enemy_weight
		+(known_target ? policy.aggression_target_bonus : 0)-demands.survival);
}


std::vector<unsigned char> Maxima::reconnaissance_discovery_map(
	Context& runtime) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const AIEngine::AIWorldView* map=&runtime.observation();
	const int width=(*map).width;
	const int height=(*map).height;
	std::vector<unsigned char> discovered(width*height, 0);
	for(int y=0; y<height; ++y)
		for(int x=0; x<width; ++x)
			discovered[y*width+x]=AIEngine::ObservationQueries::discovered((*map),
				x, y, runtime.observedTeam().mask) ? 1 : 0;
	return discovered;
}


void Maxima::remember_cleared_enemy_site(Context& runtime,
	const Recon::BuildingSighting& sighting)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	if(sighting.construction)
		return;
	const AIEngine::AIWorldView* map=&runtime.observation();
	const int x=(*map).normalizeX(sighting.x+sighting.width/2);
	const int y=(*map).normalizeY(sighting.y+sighting.height/2);
	const int width=(*map).width;
	const int height=(*map).height;
	bool merged=false;
	for(std::vector<ClearedEnemySite>::iterator site=cleared_enemy_sites.begin();
		site!=cleared_enemy_sites.end(); ++site)
	{
		const int rawDx=std::abs(site->x-x);
		const int rawDy=std::abs(site->y-y);
		const int distance=std::min(rawDx, width-rawDx)
			+std::min(rawDy, height-rawDy);
		if(distance<=strategy.colonization.conquered_merge_radius)
		{
			site->x=x;
			site->y=y;
			site->confirmedTick=timer;
			merged=true;
			break;
		}
	}
	if(!merged)
		cleared_enemy_sites.push_back(ClearedEnemySite(x, y, timer));
	std::ostringstream fields;
	fields<<"\tx="<<x<<"\ty="<<y<<"\tteam="<<sighting.team
		<<"\tbuilding_type="<<sighting.type<<"\tmerged="<<(merged ? 1 : 0)
		<<"\thotspot_count="<<cleared_enemy_sites.size();
	emit_telemetry(runtime, "colony_site_remembered", fields.str());
}


void Maxima::prune_cleared_enemy_sites()
{
	for(std::vector<ClearedEnemySite>::iterator site=cleared_enemy_sites.begin();
		site!=cleared_enemy_sites.end();)
	{
		if(timer-site->confirmedTick>=strategy.colonization.conquered_memory_ticks)
			site=cleared_enemy_sites.erase(site);
		else
			++site;
	}
}


void Maxima::sample_reconnaissance_forces(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	std::vector<int> living;
	for(enemy_team_iterator enemy(runtime); enemy!=enemy_team_iterator(); ++enemy)
	{
		const AIEngine::TeamView* enemy_team=(&runtime.observation().teams[*enemy]);
		if(enemy_team && enemy_team->alive)
			living.push_back(*enemy);
	}
	reconnaissance.beginForceObservation(timer, living);
	std::vector<Tactics::ThreatSighting> threats;
	std::vector<const AIEngine::BuildingView*> own_buildings;
	for(int b=0; b<Building::MAX_COUNT; ++b)
	{
		const AIEngine::BuildingView* building=runtime.observation().buildingSlots(runtime.teamNumber())[b];
		if(building && !AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).isVirtual)
			own_buildings.push_back(building);
	}

	for(std::vector<int>::const_iterator team=living.begin(); team!=living.end(); ++team)
	{
		const AIEngine::TeamView* enemy_team=(&runtime.observation().teams[*team]);
		for(int i=0; i<Unit::MAX_COUNT; ++i)
		{
			const AIEngine::UnitView* unit=runtime.observation().unitSlots(enemy_team->number)[i];
			if(!unit || !AIEngine::ObservationQueries::visible(runtime.observation(),
				unit->posX, unit->posY, runtime.observedTeam().mask))
				continue;
			const bool warrior=unit->typeNum==WARRIOR;
			const bool explorer=unit->typeNum==EXPLORER;
			if(!warrior && !explorer)
				continue;
			const bool attack_explorer=explorer
				&& unit->performance[MAGIC_ATTACK_GROUND]>0;
			if(warrior)
				threats.push_back(Tactics::ThreatSighting(unit->gid, *team,
					unit->posX, unit->posY,
					std::max(1, AIEngine::ObservationQueries::realAttackStrength(runtime.observation(),*unit)
						*unit->performance[ATTACK_SPEED]*unit->hp
						/std::max(1, unit->performance[HP]))));
			bool colony_threat=false;
			bool colony_explorer_threat=false;
			if(warrior || attack_explorer)
			{
				const int threat_radius=warrior ? 144 : 324;
				for(std::vector<const AIEngine::BuildingView*>::const_iterator building=
					own_buildings.begin(); building!=own_buildings.end(); ++building)
				{
					if(runtime.observation().distanceSquared(unit->posX, unit->posY,
						(*building)->posX, (*building)->posY)<=threat_radius)
					{
						colony_threat=warrior;
						colony_explorer_threat=attack_explorer;
						break;
					}
				}
			}
			reconnaissance.observeUnit(*team, warrior, explorer,
				attack_explorer, colony_threat, colony_explorer_threat);
		}
	}
	reconnaissance.finishForceObservation();
	apply_force_beliefs();
	tactics.replaceThreats(timer, threats);
}


void Maxima::apply_force_beliefs()
{
	if(!strategy.reconnaissance.force_memory_enabled) return;
	for(auto& entry:reconnaissance.mutableReport().opponents)
	{
		const auto belief=force_beliefs.find(entry.first);
		if(entry.second.alive && belief!=force_beliefs.end() && belief->second.initialized)
			entry.second.estimatedWarriors=std::max(entry.second.visibleWarriors,
				belief->second.prediction.rounded(ForceModel::Warriors));
	}
}


void Maxima::update_reconnaissance(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	telemetry.count(AITrace::AI7::Maxima_update_reconnaissance_calls);
	std::vector<int> living;
	for(enemy_team_iterator enemy(runtime); enemy!=enemy_team_iterator(); ++enemy)
	{
		const AIEngine::TeamView* enemy_team=(&runtime.observation().teams[*enemy]);
		if(enemy_team && enemy_team->alive)
			living.push_back(*enemy);
	}
	reconnaissance.beginObservation(timer, living);
	tactics.beginObservation(timer);
	std::map<int, int64_t> visible_workers, visible_power;

	for(std::vector<int>::const_iterator team=living.begin(); team!=living.end(); ++team)
	{
		const AIEngine::TeamView* enemy_team=(&runtime.observation().teams[*team]);
		for(int i=0; i<Unit::MAX_COUNT; ++i)
		{
			const AIEngine::UnitView* unit=runtime.observation().unitSlots(enemy_team->number)[i];
			if(!unit || !AIEngine::ObservationQueries::visible(runtime.observation(),
				unit->posX, unit->posY, runtime.observedTeam().mask))
				continue;
			if(!unit->isDead)
			{
				if(unit->typeNum==WORKER) ++visible_workers[*team];
				if(unit->typeNum==WARRIOR) visible_power[*team]+=warrior_power(runtime.observation(),unit);
			}
			const bool warrior=unit->typeNum==WARRIOR;
			const bool explorer=unit->typeNum==EXPLORER;
			const bool attack_explorer=explorer
				&& unit->performance[MAGIC_ATTACK_GROUND]>0;
			if(unit->typeNum==WORKER)
			{
				reconnaissance.observeEconomicActivity(*team,
					unit->posX, unit->posY);
				int economic_value=strategy.raiding.other_resource_value;
				if(unit->carriedResource==WHEAT)
					economic_value=strategy.raiding.food_resource_value;
				else if(unit->carriedResource==WOOD
				   || unit->carriedResource==STONE
				   || unit->carriedResource==ALGA)
					economic_value=strategy.raiding.material_resource_value;
				else if(unit->carriedResource>=HAPPINESS_BASE
				   && unit->carriedResource<MAX_RESOURCES)
					economic_value=strategy.raiding.fruit_resource_value;
				tactics.observeWorker(Tactics::WorkerSighting(unit->gid, *team,
					unit->posX, unit->posY, timer,
					unit->displacement==Unit::DIS_HARVESTING,
					unit->carriedResource>=0, economic_value));
			}
			if(warrior)
				tactics.observeThreat(Tactics::ThreatSighting(unit->gid, *team,
					unit->posX, unit->posY,
					std::max(1, AIEngine::ObservationQueries::realAttackStrength(runtime.observation(),*unit)
						*unit->performance[ATTACK_SPEED]*unit->hp
						/std::max(1, unit->performance[HP]))));
			bool colony_threat=false;
			bool colony_explorer_threat=false;
			if(warrior || attack_explorer)
			{
				const int threat_radius=warrior ? 144 : 324;
				for(int b=0; b<Building::MAX_COUNT; ++b)
				{
					const AIEngine::BuildingView* own=runtime.observation().buildingSlots(runtime.teamNumber())[b];
					if(own && !AIEngine::ObservationQueries::buildingType(runtime.observation(),*own).isVirtual
					   && runtime.observation().distanceSquared(unit->posX, unit->posY,
						own->posX, own->posY)<=threat_radius)
					{
						colony_threat=warrior;
						colony_explorer_threat=attack_explorer;
						break;
					}
				}
			}
			reconnaissance.observeUnit(*team, warrior, explorer,
				attack_explorer, colony_threat, colony_explorer_threat);
		}

		for(int i=0; i<Building::MAX_COUNT; ++i)
		{
			const AIEngine::BuildingView* building=runtime.observation().buildingSlots(enemy_team->number)[i];
			if(!building || AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).isVirtual
			   || !building_currently_visible(runtime.observation(),runtime.observedTeam().mask,building))
				continue;
			reconnaissance.observeBuilding(Recon::BuildingSighting(
				building->gid, *team, building->typeNum,
				building->posX, building->posY, AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).width,
				AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).height, AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).isBuildingSite, timer));
		}

		const Recon::OpponentIntel* remembered=reconnaissance.opponent(*team);
		std::vector<int> absent;
		if(remembered)
		{
			for(std::map<int, Recon::BuildingSighting>::const_iterator building=
				remembered->buildings.begin();
				building!=remembered->buildings.end(); ++building)
			{
				if(building->second.currentlyVisible
				   || !remembered_footprint_currently_visible(
						runtime.observation(),runtime.teamNumber(),building->second))
					continue;
				const int id=Building::GIDtoID(building->first);
				const AIEngine::BuildingView* actual=id>=0 && id<Building::MAX_COUNT
					? runtime.observation().buildingSlots(enemy_team->number)[id] : NULL;
				if(!actual || actual->gid!=building->first
				   || runtime.observation().occupancyAt(runtime.observation().tileIndex(building->second.x,
					building->second.y)).building!=building->first)
					absent.push_back(building->first);
			}
		}
		for(std::vector<int>::const_iterator gid=absent.begin();
			gid!=absent.end(); ++gid)
		{
			const Recon::OpponentIntel* before=reconnaissance.opponent(*team);
			if(before)
			{
				std::map<int, Recon::BuildingSighting>::const_iterator sighting=
					before->buildings.find(*gid);
				if(sighting!=before->buildings.end() && !sighting->second.construction)
					remember_cleared_enemy_site(runtime, sighting->second);
			}
			reconnaissance.confirmBuildingAbsent(*team, *gid);
		}
	}
	reconnaissance.finishObservation();
	Tactics::RaidRules raid_rules;
	raid_rules.width=runtime.observation().width;
	raid_rules.height=runtime.observation().height;
	raid_rules.tick=timer;
	raid_rules.clusterRadius=strategy.raiding.cluster_radius;
	raid_rules.flagRadius=strategy.raiding.flag_radius;
	raid_rules.threatRadius=strategy.raiding.threat_radius;
	raid_rules.workerMinimum=strategy.raiding.worker_min;
	raid_rules.workerWeight=strategy.raiding.worker_weight;
	raid_rules.harvestingBonus=strategy.raiding.harvesting_bonus;
	raid_rules.carryingBonus=strategy.raiding.carrying_bonus;
	raid_rules.resourceWeight=strategy.raiding.resource_weight;
	raid_rules.defenderPenalty=strategy.raiding.defender_penalty;
	tactics.finishObservation(raid_rules);

	const std::vector<unsigned char> discovered=reconnaissance_discovery_map(runtime);
	int explored=0;
	for(std::vector<unsigned char>::const_iterator tile=discovered.begin();
		tile!=discovered.end(); ++tile)
		explored+=*tile ? 1 : 0;
	reconnaissance.setExploredPercent(discovered.empty()
		? 0 : explored*100/int(discovered.size()));
	if(strategy.reconnaissance.force_memory_enabled)
	{
		for(const auto& entry:reconnaissance.report().opponents)
		{
			const Recon::OpponentIntel& intel=entry.second;
			if(!intel.alive) { force_beliefs.erase(entry.first); continue; }
			const int64_t observation[ForceModel::ObservationFeatures]={
				intel.visibleWarriors, intel.lastObservedWarriors,
				int64_t(timer)-intel.lastWarriorSeenTick, int64_t(timer)-intel.lastForceSeenTick,
				int64_t(timer)-intel.lastBuildingSeenTick, intel.visibleExplorers,
				visible_workers[entry.first], visible_power[entry.first], intel.knownBuildings,
				intel.visibleBuildings, intel.confidence, reconnaissance.report().exploredPercent, timer};
			ForceModel::State& belief=force_beliefs[entry.first];
			belief.observe(observation);
			belief.forecast(timer);
			// A fresh sighting remains a lower bound between model observations.
			for(int q=0;q<ForceModel::QuantileCount;++q)
			{
				belief.prediction.values[ForceModel::Warriors][q]=std::max(
					belief.prediction.values[ForceModel::Warriors][q], int64_t(intel.visibleWarriors)*ForceModel::Scale);
				belief.prediction.values[ForceModel::Power][q]=std::max(
					belief.prediction.values[ForceModel::Power][q], visible_power[entry.first]*ForceModel::Scale);
			}
		}
		apply_force_beliefs();
	}
}


void Maxima::update_opponent_models(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	telemetry.count(AITrace::AI7::Maxima_update_opponent_models_calls);
	AIMaximaRuntime::Gradients::GradientInfo home_info;
    home_info.terrainTravel=snapshot.swimming_warriors<6?field::TerrainTravel::Walk:field::TerrainTravel::Swim;
	home_info.add_source(new Entities::AnyTeamBuilding(
		runtime.teamNumber(), CompletedBuildings));
	home_info.add_obstacle(new Entities::AnyResource);
	if(snapshot.swimming_warriors<6)
		home_info.add_obstacle(new Entities::Unwalkable);
	Gradient& home=runtime.get_gradient_manager().get_gradient(home_info);

	for(int team=0; team<Team::MAX_COUNT; ++team)
		opponents[team]=OpponentAssessment();
	const Recon::ReconReport& report=reconnaissance.report();
	for(std::map<int, Recon::OpponentIntel>::const_iterator found=
		report.opponents.begin(); found!=report.opponents.end(); ++found)
	{
		if(found->first<0 || found->first>=Team::MAX_COUNT)
			continue;
		const Recon::OpponentIntel& intel=found->second;
		OpponentAssessment& model=opponents[found->first];
		model.alive=intel.alive;
		if(!model.alive)
			continue;
		model.visible_warriors=intel.visibleWarriors;
		model.estimated_warriors=intel.estimatedWarriors;
		model.last_observed_warriors=intel.lastObservedWarriors;
		model.visible_explorers=intel.visibleExplorers;
		model.visible_buildings=intel.visibleBuildings;
		model.known_buildings=intel.knownBuildings;
		model.last_seen_tick=intel.lastSeenTick;
		model.last_force_seen_tick=intel.lastForceSeenTick;
		model.last_building_seen_tick=intel.lastBuildingSeenTick;
		model.intel_confidence=intel.confidence;
		for(std::map<int, Recon::BuildingSighting>::const_iterator building=
			intel.buildings.begin(); building!=intel.buildings.end(); ++building)
		{
			const int confidence=Recon::Program::confidenceForAge(
				timer-building->second.lastSeenTick,
				strategy.reconnaissance.memory_horizon_ticks);
			model.strategic_value+=reconnaissance_building_value(runtime.observation(),
				building->second.type, strategy.reconnaissance)*confidence/100;
			const int distance=home.get_height(
				building->second.x, building->second.y);
			if(distance>=0)
			{
				model.reachable_buildings+=1;
				model.nearest_building=std::min(model.nearest_building, distance);
			}
		}
		reconnaissance.setBuildingAssessment(found->first,
			model.strategic_value, model.reachable_buildings,
			model.nearest_building);
		if(model.known_buildings>0)
		{
			model.score=model.strategic_value+model.known_buildings
				*strategy.reconnaissance.building_count_weight
				+model.reachable_buildings*strategy.scoring.target_reachable_weight
				+model.estimated_warriors*strategy.scoring.target_warrior_weight;
			if(model.nearest_building!=INT_MAX)
				model.score+=std::max(0,
					strategy.scoring.target_distance_bias-model.nearest_building);
			else
				model.score-=strategy.reconnaissance.unreachable_penalty;
			if(found->first==target
				&& model.known_buildings<=strategy.postures.finish_building_max)
				model.score+=strategy.reconnaissance.finishing_bonus
					-model.known_buildings
						*strategy.reconnaissance.finishing_building_penalty;
		}
	}
}


void Maxima::remove_reconnaissance_missions(Context& runtime,
	const char* reason)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const std::vector<Recon::ReconMission> missions=
		reconnaissance.report().missions;
	for(std::vector<Recon::ReconMission>::const_iterator mission=missions.begin();
		mission!=missions.end(); ++mission)
	{
		if(runtime.get_building_register().is_building_found(mission->flagId)
		   || runtime.get_building_register().is_building_pending(mission->flagId))
			runtime.add_management_order(new RetireAttraction(mission->flagId,1u<<EXPLORER));
		emit_telemetry(runtime, "recon_mission_removed",
			"\tflag="+telemetryText(mission->flagId)
			+"\ttarget_team="+telemetryText(mission->targetTeam)
			+"\treason="+reason);
	}
	reconnaissance.clearMissions();
}


void Maxima::update_reconnaissance_missions(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	telemetry.count(AITrace::AI7::Maxima_update_reconnaissance_missions_calls);
	Recon::ReconReport& report=reconnaissance.mutableReport();
	for(std::vector<Recon::ReconMission>::iterator mission=report.missions.begin();
		mission!=report.missions.end();)
	{
		if(runtime.get_building_register().is_building_found(mission->flagId)
		   || runtime.get_building_register().is_building_pending(mission->flagId))
		{
			++mission;
			continue;
		}
		emit_telemetry(runtime, "recon_mission_removed",
			"\tflag="+telemetryText(mission->flagId)
			+"\ttarget_team="+telemetryText(mission->targetTeam)
			+"\treason=flag_missing");
		mission=report.missions.erase(mission);
	}

	if(budget.reconnaissance_suspended || report.exploredPercent<strategy.reconnaissance.active_min_explored_percent)
	{
		if(!reconnaissance_suspended)
			emit_telemetry(runtime, "recon_suspended");
		reconnaissance_suspended=true;
		if(!report.missions.empty())
			remove_reconnaissance_missions(runtime,
				report.exploredPercent<strategy.reconnaissance.active_min_explored_percent ? "initial_exploration" : "emergency");
		last_recon_mission_tick=-1000000;
		return;
	}
	if(reconnaissance_suspended)
	{
		reconnaissance_suspended=false;
		last_recon_mission_tick=-1000000;
		emit_telemetry(runtime, "recon_resumed");
	}
	if(budget.reconnaissance_objectives.empty())
	{
		if(!report.missions.empty())
			remove_reconnaissance_missions(runtime, "no_living_enemy");
		return;
	}
	if(timer-last_recon_mission_tick<budget.reconnaissance_review_interval)
		return;
	last_recon_mission_tick=timer;

	const AIEngine::AIWorldView* map=&runtime.observation();
	const std::vector<unsigned char> discovered=reconnaissance_discovery_map(runtime);
	const std::vector<Recon::MissionObjective>& objectives=
		budget.reconnaissance_objectives;
	std::vector<Recon::ReconMission>& missions=report.missions;

	for(size_t index=0; index<objectives.size(); ++index)
	{
		const Recon::MissionObjective& objective=objectives[index];
		size_t match=missions.size();
		for(size_t candidate=index; candidate<missions.size(); ++candidate)
		{
			const bool same=missions[candidate].economicWatch
				==objective.economicWatch && (objective.frontier
				? missions[candidate].frontier
				: (!missions[candidate].frontier
					&& missions[candidate].targetTeam==objective.targetTeam));

			if(same)
			{
				match=candidate;
				break;
			}
		}
		if(match<missions.size() && match!=index)
			std::swap(missions[index], missions[match]);

		if(index>=missions.size())
		{
			BuildingOrder* order=new BuildingOrder(
				AIMaximaBuildings::ExploreAttraction, 1);
			order->add_constraint(new Construction::SinglePosition(
				objective.x, objective.y));
			const int flag=runtime.add_building_order(order);
			runtime.add_management_order(new ChangeFlagSize(
				budget.reconnaissance_flag_radius, flag));
			ManagementOrder* removed=new Notify(
				RuntimeEvent(RuntimeEvent::ReconFlagDeleted, flag));
			removed->add_condition(new AttractionRetiredOrDestroyed(flag,1u<<EXPLORER));
			runtime.add_management_order(removed);
			missions.push_back(Recon::ReconMission(flag, objective.targetTeam,
				objective.frontier, objective.x, objective.y, timer,
				objective.economicWatch));
			emit_telemetry(runtime, "recon_mission_created",
				"\tflag="+telemetryText(flag)
				+"\ttarget_team="+telemetryText(objective.targetTeam)
				+"\tfrontier="+telemetryText(objective.frontier ? 1 : 0)
				+"\teconomic_watch="+telemetryText(
					objective.economicWatch ? 1 : 0)
				+"\tx="+telemetryText(objective.x)
				+"\ty="+telemetryText(objective.y));
			continue;
		}

		Recon::ReconMission& mission=missions[index];
		const bool assignment_changed=mission.frontier!=objective.frontier
			|| mission.economicWatch!=objective.economicWatch
			|| mission.targetTeam!=objective.targetTeam;
		const bool location_changed=mission.x!=objective.x || mission.y!=objective.y;
		const int current_score=Recon::Program::frontierScore(
			mission.x, mission.y, (*map).width, (*map).height, discovered,
			budget.reconnaissance_flag_radius);
		const bool saturated=objective.frontier
			&& Recon::Program::exploredPercentInRadius(
			mission.x, mission.y, (*map).width, (*map).height, discovered,
				budget.reconnaissance_flag_radius)
				>=strategy.reconnaissance.saturation_percent;
		bool newer_building=false;
		if(!objective.frontier && !objective.economicWatch)
		{
			const Recon::OpponentIntel* intel=
				reconnaissance.opponent(objective.targetTeam);
			newer_building=intel
				&& intel->lastBuildingSeenTick>mission.lastRetaskTick;
		}
		const bool materially_better=!objective.economicWatch
			&& objective.score*100
			>=std::max(1, current_score)*strategy.reconnaissance.retask_percent;
		const bool economic_revisit=objective.economicWatch
			&& timer-mission.lastRetaskTick
				>=budget.reconnaissance_economic_watch_revisit;
		if(location_changed && (assignment_changed || saturated
		   || materially_better || newer_building || economic_revisit))
		{
			runtime.add_management_order(new ChangeFlagPosition(
				objective.x, objective.y, mission.flagId));
			emit_telemetry(runtime, "recon_mission_moved",
				"\tflag="+telemetryText(mission.flagId)
				+"\tprevious_team="+telemetryText(mission.targetTeam)
				+"\ttarget_team="+telemetryText(objective.targetTeam)
				+"\tx="+telemetryText(objective.x)
				+"\ty="+telemetryText(objective.y));
			mission.x=objective.x;
			mission.y=objective.y;
			mission.lastRetaskTick=timer;
		}
		mission.targetTeam=objective.targetTeam;
		mission.frontier=objective.frontier;
		mission.economicWatch=objective.economicWatch;
	}

	while(missions.size()>objectives.size())
	{
		const Recon::ReconMission mission=missions.back();
		if(runtime.get_building_register().is_building_found(mission.flagId)
		   || runtime.get_building_register().is_building_pending(mission.flagId))
			runtime.add_management_order(new RetireAttraction(mission.flagId,1u<<EXPLORER));
		emit_telemetry(runtime, "recon_mission_removed",
			"\tflag="+telemetryText(mission.flagId)
			+"\ttarget_team="+telemetryText(mission.targetTeam)
			+"\treason=capacity_reduced");
		missions.pop_back();
	}
}


void Maxima::plan_reconnaissance_objectives(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	telemetry.count(AITrace::AI7::Maxima_plan_reconnaissance_objectives_calls);
	Recon::ReconReport& report=reconnaissance.mutableReport();
	if(!strategy.reconnaissance.enabled
	   || !strategy.reconnaissance.scouting_missions_enabled
	   || report.exploredPercent<strategy.reconnaissance.active_min_explored_percent)
	{
		budget.reconnaissance_suspended=true;
		budget.reconnaissance_objectives.clear();
		reconnaissance.setDesiredMissions(0);
		return;
	}
	const bool emergency=budget.colony_emergency;
	const int stale=Recon::Program::staleOrUnseenEnemies(report, timer,
		strategy.reconnaissance.stale_contact_age_ticks);
	const int contact_desired=Recon::Program::desiredMissionCount(
		report.aliveEnemies, stale, snapshot.population, emergency,
		strategy.reconnaissance.mission_population_divisor);
	budget.reconnaissance_suspended=emergency;
	budget.reconnaissance_objectives.clear();
	if(emergency || contact_desired<=0)
	{
		reconnaissance.setDesiredMissions(0);
		return;
	}

	const AIEngine::AIWorldView* map=&runtime.observation();
	const std::vector<unsigned char> discovered=reconnaissance_discovery_map(runtime);
	budget.reconnaissance_objectives=Recon::Program::planObjectives(
		report, contact_desired, (*map).width, (*map).height, discovered,
		budget.reconnaissance_flag_radius);

	// Explorers are a late-game information service, not a raiding force.  Once
	// prestige is available, dedicate a small patrol capacity to revisiting
	// observed worker activity and previously discovered resource belts outside
	// known enemy buildings.  Only sightings made when the patrol restores FOW
	// are passed to the warrior raid planner.
	const bool economic_watch=strategy.reconnaissance.economic_watch_enabled
		&& snapshot.prestige>0
		&& snapshot.population
			>=strategy.reconnaissance.economic_watch_population_min;
	if(economic_watch)
	{
		std::vector<EconomicWatchTeam> teams;
		const int preferred_team=tactical_mission.kind==Tactics::MissionRaid
			? tactical_mission.targetTeam
			: (budget.tactical_kind==Tactics::MissionRaid
				? budget.tactical_target_team : campaign.target_team);
		for(std::map<int, Recon::OpponentIntel>::const_iterator opponent=
			report.opponents.begin(); opponent!=report.opponents.end(); ++opponent)
		{
			const Recon::OpponentIntel& intel=opponent->second;
			if(!intel.alive || (intel.buildings.empty()
			   && intel.lastEconomicSeenTick<0))
				continue;
			EconomicWatchTeam candidate;
			candidate.team=opponent->first;
			candidate.preferred=candidate.team==preferred_team;
			candidate.lastActivity=intel.lastEconomicSeenTick;
			candidate.strategicValue=intel.strategicValue;
			teams.push_back(candidate);
		}
		std::sort(teams.begin(), teams.end(), better_economic_watch_team);
		const int watch_count=std::min(int(teams.size()),
			strategy.reconnaissance.economic_watch_max_missions);
		for(int watch=0; watch<watch_count; ++watch)
		{
			const int team=teams[watch].team;
			const Recon::OpponentIntel& intel=report.opponents.find(team)->second;
			int watch_x=-1;
			int watch_y=-1;
			int watch_score=0;
			if(timer-intel.lastEconomicSeenTick
			   <=strategy.reconnaissance.economic_watch_revisit_ticks*2)
			{
				watch_x=intel.lastEconomicX;
				watch_y=intel.lastEconomicY;
				watch_score=strategy.raiding.worker_weight;
			}
			else
			{
				std::vector<EconomicWatchSite> sites;
				std::set<int> seen_sites;
				const int minimum=
					strategy.reconnaissance.economic_watch_radius_min;
				const int maximum=
					strategy.reconnaissance.economic_watch_radius_max;
				for(std::map<int, Recon::BuildingSighting>::const_iterator building=
					intel.buildings.begin(); building!=intel.buildings.end(); ++building)
				{
					const int center_x=building->second.x+building->second.width/2;
					const int center_y=building->second.y+building->second.height/2;
					for(int dy=-maximum; dy<=maximum; ++dy)
						for(int dx=-maximum; dx<=maximum; ++dx)
						{
							const int distance=dx*dx+dy*dy;
							if(distance<minimum*minimum || distance>maximum*maximum)
								continue;
							const int x=((center_x+dx)%(*map).width
								+(*map).width)%(*map).width;
							const int y=((center_y+dy)%(*map).height
								+(*map).height)%(*map).height;
							const int index=y*(*map).width+x;
							if(!discovered[index] || !AIEngine::ObservationQueries::terrain((*map),index).walkable
							   || (*map).resourceAt((*map).tileIndex(x, y)).resource.type==NO_RES_TYPE
							   || !seen_sites.insert(index).second)
								continue;
							EconomicWatchSite site;
							site.x=x;
							site.y=y;
							const int resource=(*map).resourceAt((*map).tileIndex(x, y)).resource.type;
							site.score=(resource==CHERRY || resource==ORANGE
								|| resource==PRUNE)
								? strategy.raiding.fruit_resource_value
								: (resource==WHEAT
									? strategy.raiding.food_resource_value
									: ((resource==WOOD || resource==STONE)
										? strategy.raiding.material_resource_value
										: strategy.raiding.other_resource_value));
							site.distance=distance;
							sites.push_back(site);
						}
				}
				std::sort(sites.begin(), sites.end(), better_economic_watch_site);
				if(!sites.empty())
				{
					const int patrol_span=std::min(
						strategy.reconnaissance.economic_watch_patrol_sites,
						int(sites.size()));
					const int patrol=(timer
						/strategy.reconnaissance.economic_watch_revisit_ticks
						+watch)%patrol_span;
					watch_x=sites[patrol].x;
					watch_y=sites[patrol].y;
					watch_score=sites[patrol].score;
				}
			}
			if(watch_x>=0)
				budget.reconnaissance_objectives.push_back(
					Recon::MissionObjective(team, false, watch_x, watch_y,
						watch_score, true));
		}
	}
	const int desired=int(budget.reconnaissance_objectives.size());
	reconnaissance.setDesiredMissions(desired);
	budget.desired_explorers=std::max(budget.desired_explorers, desired);
}


bool Maxima::severe_food_emergency() const
{
	if(!strategy.emergencies.food_enabled)
		return false;
	if(snapshot.population<=1)
		return false;
	return snapshot.unserved_food*100
			>=snapshot.population*strategy.emergencies.food_unserved_percent
		|| snapshot.critical_food*100
			>=snapshot.population*strategy.emergencies.food_critical_percent
		|| std::max(snapshot.unserved_food,snapshot.critical_food)*100
			>=snapshot.population*strategy.emergencies.food_combined_percent;
}


bool Maxima::severe_colony_emergency() const
{
	if(!strategy.emergencies.colony_enabled)
		return false;
	return explorer_defense_emergency()
		|| snapshot.visible_colony_threat
			>=strategy.emergencies.colony_threat_threshold
		|| (snapshot.own_buildings_under_attack
				>=strategy.emergencies.buildings_under_attack_threshold
			&& snapshot.own_units_under_attack
				>=strategy.emergencies.units_under_attack_threshold)
		|| (snapshot.own_units_under_attack
				>=strategy.emergencies.declining_units_under_attack_threshold
			&& trends.population
				<=strategy.emergencies.declining_population_trend_threshold)
		|| (snapshot.own_units_under_attack>=std::max(
				strategy.emergencies.proportional_units_floor,
				snapshot.population
					/strategy.emergencies.proportional_population_divisor)
			&& trends.population
				<strategy.emergencies.proportional_population_trend_threshold);
}


bool Maxima::explorer_defense_active() const
{
	if(!strategy.military.explorer_defense_enabled)
		return false;
	// Prestige merely means an opponent could field ground-attack explorers.
	// Towers are too expensive to buy on possibility alone; wait for the visible
	// buildup that constitutes an actual explorer bomb.
	return snapshot.visible_enemy_attack_explorers
			>=strategy.reconnaissance.explorer_attack_warning_threshold
		|| snapshot.visible_colony_explorer_threat
			>=strategy.reconnaissance.explorer_colony_warning_threshold
		|| timer<explorer_threat_until;
}


bool Maxima::explorer_defense_emergency() const
{
	if(!strategy.military.explorer_defense_enabled)
		return false;
	return snapshot.visible_colony_explorer_threat
			>=strategy.reconnaissance.explorer_colony_warning_threshold
		|| timer<explorer_colony_threat_until;
}


bool Maxima::abundance_surge_active() const
{
	// Static terrain knowledge is valid from the first strategic cycle. Do not
	// make a rich opening wait for fog-derived confidence; only require that the
	// immediately connected pocket is viable, or that swimming has unlocked the
	// rest of the map.
	return strategy.economy.large_economy_adaptation_enabled
		&& environment.terrain_abundance>=strategy.environment.large_economy_terrain_min
		&& environment.food_security>=strategy.environment.large_economy_food_min
		&& (environment.connected_abundance
			>=strategy.environment.large_economy_connected_min
			|| snapshot.swimming_workers>0);
}


bool Maxima::large_economy_established() const
{
	return strategy.economy.large_economy_adaptation_enabled
		&& large_economy_committed;
}


void Maxima::score_postures()
{
	telemetry.count(AITrace::AI7::Maxima_score_postures_calls);
	const MaximaStrategy::Postures& policy=strategy.postures;
	const int population=std::max(1, snapshot.population);
	const int unserved_percent=snapshot.unserved_food*100/population;
	const int critical_percent=snapshot.critical_food*100/population;
	const int food_pressure=std::max(unserved_percent, critical_percent);
	const int labor_pressure=std::max(0,
		snapshot.worker_jobs_open-snapshot.free_workers);
	int largest_enemy_force=0;
	int best_target_score=INT_MIN;
	int weakest_known_buildings=INT_MAX;
	for(int team=0; team<Team::MAX_COUNT; ++team)
	{
		if(!opponents[team].alive)
			continue;
		largest_enemy_force=std::max(largest_enemy_force, opponents[team].estimated_warriors);
		best_target_score=std::max(best_target_score, opponents[team].score);
		if(opponents[team].known_buildings>0)
			weakest_known_buildings=std::min(weakest_known_buildings,
				opponents[team].known_buildings);
	}

	posture_utilities[PostureRecover]=food_pressure>=policy.recover_food_pressure_min
		? policy.recover_base
			+(food_pressure-policy.recover_food_pressure_min)
				*policy.recover_food_weight+std::max(0, trends.food_pressure)
			+labor_pressure/policy.labor_pressure_divisor
		: policy.recover_inactive_utility;
	posture_utilities[PostureDefend]=
		snapshot.visible_colony_threat*policy.defend_colony_weight
		+snapshot.own_buildings_under_attack*policy.defend_building_weight
		+snapshot.own_units_under_attack*policy.defend_unit_weight
		+std::max(0, trends.colony_pressure)
		+snapshot.visible_enemy_attack_explorers*policy.defend_attack_explorer_weight
		+snapshot.visible_colony_explorer_threat*policy.defend_colony_explorer_weight
		+(explorer_defense_emergency() ? policy.defend_emergency_bonus : 0);
	posture_utilities[PostureExpand]=policy.expand_base
		-std::min(policy.expand_population_cap,
			snapshot.population/policy.expand_population_divisor)
		-food_pressure*policy.expand_food_weight
		-labor_pressure/policy.labor_pressure_divisor;
	posture_utilities[PostureDevelop]=snapshot.population>=policy.develop_population_min
		? policy.develop_base+std::min(policy.develop_population_cap,
				snapshot.population/policy.develop_population_divisor)
			+snapshot.trained_workers/policy.develop_worker_divisor
			+(snapshot.population>=policy.develop_no_school_population_min
				&& snapshot.schools==0 ? policy.develop_no_school_bonus : 0)
			-food_pressure*policy.develop_food_weight
		: policy.inactive_utility;
	const int desired_force=std::max(policy.desired_force_floor,
		largest_enemy_force+policy.desired_force_margin);
	posture_utilities[PostureMobilize]=snapshot.population>=policy.mobilize_population_min
		? policy.mobilize_base+std::max(0,
			desired_force-snapshot.trained_warriors)*policy.mobilize_force_weight
			+largest_enemy_force-food_pressure*policy.mobilize_food_weight
		: policy.inactive_utility;
	const bool endgame=snapshot.alive_enemies<=strategy.military.endgame_enemy_count;
	// Readiness is measured before the defensive reserve is subtracted.  The old
	// gate only required "enemy + 8", then kept ten or more warriors home, so a
	// supposedly ready expedition could actually be outnumbered at launch.
	const int estimated_reserve=std::max(strategy.military.defense_reserve_floor,
		std::max(largest_enemy_force*strategy.military.defense_enemy_percent/100
				+strategy.military.reserve_enemy_bonus,
			snapshot.trained_warriors/strategy.military.reserve_force_divisor));
	const int campaign_readiness=endgame
		? std::max(strategy.military.endgame_force_floor,
			largest_enemy_force+estimated_reserve
			+std::max(0, strategy.military.campaign_force_margin-1))
		: std::max(strategy.military.campaign_force_floor,
			largest_enemy_force+estimated_reserve
			+strategy.military.campaign_force_margin);
	const int campaign_population=std::max(
		endgame ? strategy.military.endgame_population_floor
			: strategy.military.campaign_population_floor,
		(endgame ? strategy.military.campaign_population_base
				-strategy.military.endgame_population_discount
			: strategy.military.campaign_population_base)
			-demands.aggression/strategy.military.readiness_demand_divisor);
	posture_utilities[PostureCampaign]=snapshot.population>=campaign_population
		&& snapshot.trained_warriors>=campaign_readiness
		&& best_target_score!=INT_MIN
		? policy.campaign_base
			+snapshot.trained_warriors*policy.campaign_warrior_weight
			+best_target_score/policy.campaign_target_divisor
			-food_pressure*policy.campaign_food_weight
			-snapshot.visible_colony_threat*policy.campaign_threat_weight
		: policy.campaign_inactive_utility;
	posture_utilities[PostureFinish]=snapshot.population>=policy.finish_population_min
		&& snapshot.trained_warriors>=policy.finish_warrior_min
		&& weakest_known_buildings<=policy.finish_building_max
		? policy.finish_base
			+(policy.finish_building_max+1-weakest_known_buildings)
				*policy.finish_building_weight+snapshot.trained_warriors
			-food_pressure*policy.finish_food_weight
			-snapshot.visible_colony_threat*policy.finish_threat_weight
		: policy.finish_inactive_utility;
	// Postures express commitment; the continuous demands express the whole
	// portfolio. A colony can keep developing while it mobilizes instead of
	// turning every non-selected concern completely off.
	posture_utilities[PostureRecover]+=demands.food;
	posture_utilities[PostureDefend]+=environment.threat_pressure;
	posture_utilities[PostureExpand]+=(demands.growth+demands.expansion)
		/policy.portfolio_expansion_divisor;
	posture_utilities[PostureDevelop]+=demands.technology;
	posture_utilities[PostureMobilize]+=demands.military;
	posture_utilities[PostureCampaign]+=
		demands.aggression*policy.campaign_demand_weight;
	posture_utilities[PostureFinish]+=demands.aggression;
	if(campaign.state==CampaignActive || campaign.state==CampaignPaused)
	{
		posture_utilities[PostureCampaign]+=policy.campaign_active_bonus;
		if(campaign.target_team>=0
		   && opponents[campaign.target_team].known_buildings
				<=policy.finish_building_max)
			posture_utilities[PostureFinish]+=policy.finish_active_bonus;
	}
	const bool campaign_established=campaign.state==CampaignActive
		|| campaign.state==CampaignPaused;
	const bool campaign_safe=!severe_food_emergency()
		&& !severe_colony_emergency();
	const int campaign_worker_floor=std::max(strategy.military.campaign_worker_floor,
		strategy.military.campaign_worker_floor_base
			-demands.growth/strategy.military.readiness_demand_divisor);
	const bool campaign_economy_ready=snapshot.population>0
		&& std::max(snapshot.unserved_food,snapshot.critical_food)*100
			<=snapshot.population*strategy.military.campaign_food_percent
		&& snapshot.workers>=campaign_worker_floor
		&& snapshot.workers*strategy.military.campaign_worker_population_ratio
			>=snapshot.population;
	if(!campaign_established && (!campaign_safe || !campaign_economy_ready))
	{
		posture_utilities[PostureCampaign]=policy.campaign_inactive_utility;
		posture_utilities[PostureFinish]=policy.finish_inactive_utility;
	}
	if(timer<campaign.cooldown_until)
	{
		posture_utilities[PostureCampaign]=policy.finish_inactive_utility;
		posture_utilities[PostureFinish]=policy.finish_inactive_utility;
	}
	if(!policy.recover_enabled) posture_utilities[PostureRecover]=INT_MIN;
	if(!policy.defend_enabled) posture_utilities[PostureDefend]=INT_MIN;
	if(!policy.expand_enabled) posture_utilities[PostureExpand]=INT_MIN;
	if(!policy.develop_enabled) posture_utilities[PostureDevelop]=INT_MIN;
	if(!policy.mobilize_enabled) posture_utilities[PostureMobilize]=INT_MIN;
	if(!policy.campaign_enabled) posture_utilities[PostureCampaign]=INT_MIN;
	if(!policy.finish_enabled) posture_utilities[PostureFinish]=INT_MIN;
}


void Maxima::select_posture()
{
	telemetry.count(AITrace::AI7::Maxima_select_posture_calls);
	const bool enabled[PostureCount]={
		strategy.postures.expand_enabled, strategy.postures.develop_enabled,
		strategy.postures.mobilize_enabled, strategy.postures.campaign_enabled,
		strategy.postures.finish_enabled, strategy.postures.defend_enabled,
		strategy.postures.recover_enabled};
	StrategicPosture selected=PostureExpand;
	for(int candidate=0; candidate<PostureCount; ++candidate)
		if(enabled[candidate])
		{
			selected=StrategicPosture(candidate);
			break;
		}
	if(strategy.postures.defend_enabled && severe_colony_emergency())
		selected=PostureDefend;
	else if(strategy.postures.recover_enabled && severe_food_emergency())
		selected=PostureRecover;
	else
	{
		for(int candidate=0; candidate<PostureCount; ++candidate)
			if(enabled[candidate]
			   && posture_utilities[candidate]>posture_utilities[selected])
				selected=StrategicPosture(candidate);
	}

	if(!director.initialized)
	{
		posture=selected;
		posture_since=timer;
		return;
	}
	const bool emergency=selected==PostureDefend || selected==PostureRecover;
	const bool current_emergency=posture==PostureDefend || posture==PostureRecover;
	const int minimum_commitment_ticks=current_emergency
		? strategy.postures.emergency_commitment_ticks
		: strategy.scheduling.normal_posture_commitment_ticks;
	const bool minimum_commitment=timer-posture_since<minimum_commitment_ticks;
	const bool materially_better=posture_utilities[selected]
		>=posture_utilities[posture]+strategy.scoring.posture_switch_margin;
	const bool enter_emergency=emergency && !current_emergency;
	const bool change_emergency=emergency && current_emergency
		&& !minimum_commitment && materially_better;
	const bool leave_emergency=current_emergency && !emergency
		&& !minimum_commitment && materially_better;
	const bool change_normal=!emergency && !current_emergency
		&& !minimum_commitment && materially_better;
	const bool current_enabled=enabled[posture];
	if(selected!=posture && (!current_enabled || enter_emergency || change_emergency
		|| leave_emergency || change_normal))
	{
		// Economic recovery cannot delay an independent military campaign.
		if(enter_emergency && selected==PostureDefend)
			campaign.cooldown_until=std::max(campaign.cooldown_until,
				timer+strategy.postures.emergency_campaign_cooldown_ticks);
		posture=selected;
		posture_since=timer;
	}
}


void Maxima::allocate_resources()
{
	telemetry.count(AITrace::AI7::Maxima_allocate_resources_calls);
	budget=DirectorPlan();
	int largest_enemy_force=0;
	for(int team=0; team<Team::MAX_COUNT; ++team)
		if(opponents[team].alive)
			largest_enemy_force=std::max(largest_enemy_force, opponents[team].estimated_warriors);
	// The reserve is advisory for the reactive defense; it no longer rations
	// the offense.
	budget.defense_reserve=std::max(strategy.military.defense_reserve_min,
		std::max(largest_enemy_force*strategy.military.defense_enemy_percent/100
				+strategy.military.reserve_enemy_bonus,
			snapshot.trained_warriors/strategy.military.reserve_force_divisor));
	budget.attack_flags=0;
	bool known_target=false;
	for(int team=0; team<Team::MAX_COUNT; ++team)
		if(opponents[team].alive && opponents[team].score!=INT_MIN)
			known_target=true;
	// The offense keeps a flag on the enemy whenever a target is known; the
	// economic director never rations existing warriors.
	if(strategy.tactics.enabled && known_target && !severe_colony_emergency())
		budget.attack_flags=1;
	budget.attack_units=budget.attack_flags>0
		? std::min(strategy.military.attack_unit_cap, snapshot.trained_warriors) : 0;

	// Campaign readiness feeds the offense bid; the arbiter owns the executable
	// economy and production budget.
	build_policy_bids();
	arbitrate_policy_bids();
}


const char* Maxima::policy_name(PolicyKind policy) const
{
	switch(policy)
	{
		case PolicySurvival: return "survival";
		case PolicyGrowth: return "growth";
		case PolicyAccess: return "access";
		case PolicyTechnology: return "technology";
		case PolicyDefense: return "defense";
		case PolicyOffense: return "offense";
		default: return "unknown";
	}
}


void Maxima::build_policy_bids()
{
	telemetry.count(AITrace::AI7::Maxima_build_policy_bids_calls);
	for(int p=0; p<PolicyCount; ++p)
		policy_bids[p]=PolicyBid();
	const bool abundance_surge=abundance_surge_active();
	const int connected_boom=std::max(0,
		environment.connected_abundance-strategy.economy.connected_boom_baseline);
	const int access_barrier=environment.mobility_opportunity
		*(100-environment.connected_abundance)/100;
	// Existing boundary-search outcomes show earlier construction and schools on
	// abundant connected terrain, but delayed secondary technology and fewer
	// swarm workers while valuable capacity is stranded behind mobility. Apply
	// those effects continuously to the optimized baseline instead of selecting a
	// map-specific policy.
	const int growth_site_utility_mid=clamp_score(
		strategy.economy.growth_site_utility_mid
			-connected_boom/strategy.economy.growth_mid_boom_divisor
			+access_barrier/strategy.economy.growth_barrier_divisor);
	const int growth_site_utility_high=clamp_score(
		strategy.economy.growth_site_utility_high-connected_boom
			+access_barrier/strategy.economy.growth_barrier_divisor);
	const int school_population_min=std::max(0,
		strategy.economy.school_population_min
			-connected_boom/strategy.economy.school_boom_divisor
			+access_barrier);
	const int second_school_utility_min=clamp_score(
		strategy.economy.second_school_utility_min
			-connected_boom/strategy.economy.school_boom_divisor
			+access_barrier/strategy.economy.second_school_barrier_divisor);
	const int pool_population_min=std::max(0,
		strategy.economy.pool_population_min
			-access_barrier/strategy.economy.pool_population_barrier_divisor);
	const int pool_utility_min=std::max(strategy.economy.pool_utility_floor,
		strategy.economy.pool_utility_min
			-access_barrier/strategy.economy.pool_utility_barrier_divisor);

	PolicyBid& survival=policy_bids[PolicySurvival];
	survival.utility=std::max(demands.survival, demands.food);
	// Nominal service throughput is an optimistic bound: travel time, uneven
	// corn distribution and staffing all reduce the population that a real inn
	// can feed. Plan against 80% of nominal capacity, but derive the target from
	// population rather than the current inn count so a temporary queue cannot
	// ratchet construction upward forever.
	const int reliable_inn_capacity=std::max(1,feeding_capacity(preferred_profile(AIMaximaBuildings::Feeding),1)*strategy.economy.reliable_inn_percent/100);
	const int nominal_service_inns=(snapshot.population
		+reliable_inn_capacity-1)/reliable_inn_capacity;
	int service_required_inns=nominal_service_inns;
	const int observed_food_pressure=std::max(snapshot.unserved_food,
		snapshot.critical_food);
	const bool food_service_stressed=strategy.economy.food_service_safeguards_enabled
		&& snapshot.population>0
		&& (snapshot.unserved_food*100
				>=snapshot.population*strategy.economy.service_unserved_percent
			|| snapshot.critical_food*100
				>=snapshot.population*strategy.economy.service_critical_percent
			|| observed_food_pressure*100
				>=snapshot.population*strategy.economy.service_combined_percent);
	if(food_service_stressed)
		service_required_inns=std::max(service_required_inns,
			nominal_service_inns+1);
    // A second feeder redistributes the same recipients' recurring meals. A
    // crop-tiles-per-building ceiling charges that demand again and can block
    // service recovery. Keep the strategy's construction cap; the placement
    // ledger checks actual meal shares and independent hybrid costs. Without
    // that ledger, crop count still cannot constrain free/non-wheat services.
	int demographic_inns=(snapshot.population
		+strategy.economy.inn_population_offset)
		/strategy.economy.inn_population_divisor+1;
	demographic_inns+=(environment.food_headroom
		<strategy.economy.food_headroom_warning ? 1 : 0);
	demographic_inns+=(environment.food_headroom
		<strategy.economy.food_headroom_critical ? 1 : 0);
	demographic_inns+=(demands.food>=strategy.construction.survival_utility_high
		? 1 : 0);
	survival.desired_inns=std::min(strategy.economy.inn_target_cap,
			std::max(strategy.economy.inn_target_floor,
			std::max(service_required_inns, demographic_inns)));
	if(abundance_surge)
		survival.desired_inns=std::min(strategy.economy.abundance_inn_target_cap,
				std::max(survival.desired_inns,
					(snapshot.population
						+strategy.economy.abundance_inn_population_offset)
						/strategy.economy.abundance_inn_population_divisor+1));
	survival.construction_sites=
		survival.utility>=strategy.construction.survival_utility_high
			? strategy.construction.policy_high_sites
			: strategy.construction.policy_low_sites;
	survival.request_upgrades=snapshot.schools>0
		&& (demands.food>=strategy.economy.food_upgrade_utility_min
			|| environment.food_headroom<strategy.economy.food_upgrade_headroom_max);

	PolicyBid& growth=policy_bids[PolicyGrowth];
	growth.utility=demands.growth;
	// Food funds births. The local acreage is what existing buildings can
	// reach; the ledger's supported swarms say what the discovered wheat as a
	// whole could feed, including stacks a colony would have to move to. A
	// Production has its own packet budget; provider counts are not acreage.
	const long long ledger_food=strategy.food.enabled && food_ledger_valid
        ? birth_food_acreage() : 0;
	const SwarmController::Plan birth=SwarmController::plan(snapshot.workers,
		snapshot.population, snapshot.critical_food, snapshot.unserved_food,
		std::max(ledger_food,
			environment.accessible_corn*65536LL+environment.accessible_corn_fraction),
		strategy.economy.swarm_labor_scale_percent,
		strategy.economy.swarm_food_per_worker_percent,
		strategy.economy.swarm_pressure_sensitivity,
		strategy.economy.swarm_workers_per_building,65536);
	growth.desired_swarms=birth.swarms;
	growth.swarm_workers=birth.workers;
	// Retain useful existing production capacity and budget new sites from
	// unclaimed farm supply. Local feasibility still gates every placement;
	// a provider's uncontested reach never supplies extra birth funding.
	if(strategy.food.enabled && strategy.food.target_capping_enabled
	   && food_ledger_valid)
	{
        // Feeding serves the same colony population across multiple providers.
        // A count derived from each provider's peak demand cannot bound this
        // target. The placement ledger checks redistributed local meal claims,
        // including independent costs of a hybrid, before any order is issued.
		growth.desired_swarms=std::min(growth.desired_swarms,
			std::max(1,food_supported_swarms));
	}
	growth.construction_sites=
		growth.utility>=growth_site_utility_high
			? strategy.construction.growth_utility_high_sites
		: (growth.utility>=growth_site_utility_mid
			? strategy.construction.growth_utility_mid_sites
			: strategy.construction.growth_utility_low_sites);
	if(snapshot.population<strategy.economy.early_population_threshold)
		growth.worker_ratio=strategy.economy.early_worker_ratio;
	else if(snapshot.worker_jobs_open
		>snapshot.free_workers+strategy.economy.worker_backlog_high)
		growth.worker_ratio=strategy.economy.worker_ratio_backlog_high;
	else if(snapshot.worker_jobs_open>snapshot.free_workers)
		growth.worker_ratio=strategy.economy.worker_ratio_backlog_low;
	else
		growth.worker_ratio=strategy.economy.worker_ratio_surplus;

	PolicyBid& access=policy_bids[PolicyAccess];
	access.utility=std::max(demands.access, demands.mobility);
	if(abundance_surge
		&& snapshot.population>=strategy.economy.abundance_access_population_min)
		access.utility=std::max(access.utility,
			strategy.economy.abundance_access_utility_floor);
	// FourSquares can look fully explored while its economy still depends on
	// crossing water between resource pockets. Once a large colony has committed
	// to amphibious development, do not let a quiet mobility sample (or the loss
	// of one pool in combat) switch that capability back off.
	const bool maintain_amphibious_network=
		strategy.economy.amphibious_network_maintenance_enabled
		&& large_economy_established()
		&& environment.mobility_opportunity>=strategy.economy.amphibious_opportunity_min
		&& snapshot.population>=strategy.economy.amphibious_population_min;
	if(maintain_amphibious_network)
		access.utility=std::max(access.utility, strategy.economy.amphibious_utility_floor);
	access.desired_pools=
		snapshot.population>=pool_population_min
		&& access.utility>=pool_utility_min
		? strategy.economy.first_pool_target : 0;
	if(snapshot.population>=strategy.economy.second_pool_population_min
		&& demands.mobility>=strategy.economy.second_pool_utility_min)
		access.desired_pools=strategy.economy.second_pool_target;
	if(abundance_surge
		&& snapshot.population>=strategy.economy.abundance_pool_population_min)
		access.desired_pools=(snapshot.school_level1+snapshot.school_level2
			+snapshot.school_level3>=strategy.economy.abundance_pool_school_min)
			? strategy.economy.second_pool_target
			: strategy.economy.first_pool_target;
	if(maintain_amphibious_network)
		access.desired_pools=strategy.economy.second_pool_target;
	access.desired_explorers=std::min(strategy.economy.explorer_cap,
		std::max(strategy.economy.explorer_floor,
			snapshot.population/strategy.economy.explorer_population_divisor
			+strategy.economy.explorer_population_offset
			+access.utility/strategy.economy.explorer_utility_divisor));
	access.explorer_ratio=access.utility>=strategy.economy.explorer_ratio_high_utility
		? strategy.economy.explorer_ratio_high
		: (access.utility>=strategy.economy.explorer_ratio_low_utility
			? strategy.economy.explorer_ratio_low : 0);
	access.construction_sites=access.utility>=strategy.construction.policy_utility_high
		? strategy.construction.policy_high_sites
		: strategy.construction.policy_low_sites;

	PolicyBid& technology=policy_bids[PolicyTechnology];
	technology.utility=demands.technology;
	// School demand is independent of algae discovery and distance.
	technology.desired_schools=snapshot.population>=school_population_min
		&& technology.utility>=strategy.economy.school_utility_min
		? (technology.utility>=second_school_utility_min
			? std::max(strategy.economy.second_school_target,snapshot.population/70)
			: strategy.economy.first_school_target)
		: 0;
	technology.desired_racetracks=
		snapshot.population>=strategy.economy.racetrack_population_min
		&& technology.utility>=strategy.economy.racetrack_utility_min
		? strategy.economy.first_racetrack_target : 0;
	if(snapshot.population>=strategy.economy.second_racetrack_population_min
		&& technology.utility>=strategy.economy.second_racetrack_utility_min)
		technology.desired_racetracks=strategy.economy.second_racetrack_target;
	technology.request_upgrades=snapshot.schools>0
		&& snapshot.population>=strategy.upgrades.level1_population_min
		&& !severe_colony_emergency();
	technology.construction_sites=
		technology.utility>=strategy.construction.policy_utility_high
			? strategy.construction.policy_high_sites
			: strategy.construction.policy_low_sites;
	if(abundance_surge)
		technology.construction_sites=strategy.construction.sites_high;

	PolicyBid& defense=policy_bids[PolicyDefense];
	defense.utility=clamp_score(std::max(demands.military,
		environment.threat_pressure+strategy.military.defense_utility_floor_bonus)
		+(explorer_defense_active()
			? strategy.military.explorer_defense_utility_bonus : 0));
	int largest_enemy_force=0;
	for(int team=0; team<Team::MAX_COUNT; ++team)
		if(opponents[team].alive)
			largest_enemy_force=std::max(largest_enemy_force,
				opponents[team].estimated_warriors);
	defense.desired_warriors=Tactics::desiredArmy(largest_enemy_force,
		context.observation().tick,strategy.assault.force_growth_ticks,
		strategy.military.warrior_floor,Unit::MAX_COUNT);
	defense.defense_reserve=std::max(strategy.military.defense_reserve_floor,
		std::max(largest_enemy_force*strategy.military.defense_enemy_percent/100
				+strategy.military.reserve_enemy_bonus,
			snapshot.trained_warriors/strategy.military.reserve_force_divisor));
	defense.desired_barracks=
		snapshot.population>=strategy.military.first_barracks_population_min ? 1 : 0;
	if(snapshot.population>=strategy.military.second_barracks_population_min
		|| largest_enemy_force>=strategy.military.second_barracks_enemy_min)
		defense.desired_barracks=2;
	if(snapshot.warriors-snapshot.trained_warriors
			>=strategy.military.third_barracks_backlog_min
		|| largest_enemy_force>=strategy.military.third_barracks_enemy_min)
		defense.desired_barracks=3;
	if(snapshot.visible_colony_threat>=strategy.military.emergency_barracks_threat_min)
		defense.desired_barracks=std::max(defense.desired_barracks, 3);
	defense.desired_hospital_beds=Labour::hospitalBedsWanted(snapshot.warriors,
		strategy.military.hospital_beds_per_warrior_percent);
	const int active_towers=strategy.military.tower_active_count;
	const int emergency_towers=active_towers+strategy.military.tower_emergency_increment;
	const int bomb_towers=emergency_towers+strategy.military.tower_bomb_increment;
	defense.desired_towers=explorer_defense_active() ? active_towers : 0;
	if(strategy.military.explorer_defense_enabled
	   && large_economy_established() && snapshot.enemy_prestige>0)
		defense.desired_towers=std::max(defense.desired_towers,
			active_towers+1);
	if(explorer_defense_emergency())
		defense.desired_towers=emergency_towers;
	if(strategy.military.explorer_defense_enabled
	   && (snapshot.visible_enemy_attack_explorers
			>=strategy.military.bomb_attack_explorers_min
	   || snapshot.visible_colony_explorer_threat
			>=strategy.military.bomb_colony_explorers_min))
		defense.desired_towers=bomb_towers;
	defense.warrior_ratio=
		defense.utility>=strategy.military.warrior_ratio_high_utility
			? strategy.military.warrior_ratio_high
			: (defense.utility>=strategy.military.warrior_ratio_low_utility
				? strategy.military.warrior_ratio_low : 0);
	defense.desired_explorers=explorer_defense_active()
		? std::min(strategy.military.defender_explorer_cap,
			std::max(strategy.military.defender_explorer_floor,
				snapshot.population*strategy.military.defender_explorer_percent/100))
		: 0;
	defense.explorer_ratio=explorer_defense_active()
		? strategy.military.defender_explorer_ratio : 0;
	defense.construction_sites=explorer_defense_emergency()
		? strategy.construction.defense_emergency_sites
		: (defense.utility>=strategy.construction.policy_utility_high
			? strategy.construction.policy_high_sites
			: strategy.construction.policy_low_sites);

	PolicyBid& offense=policy_bids[PolicyOffense];
	offense.utility=demands.aggression;
	offense.desired_warriors=defense.desired_warriors;
	offense.warrior_ratio=
		offense.utility>=strategy.military.warrior_ratio_high_utility
			? strategy.military.warrior_ratio_high
			: (offense.utility>=strategy.military.warrior_ratio_low_utility
				? strategy.military.warrior_ratio_low : 0);
	offense.desired_explorers=snapshot.prestige>0
		? std::min(strategy.military.offense_explorer_cap,
			std::max(strategy.military.offense_explorer_floor,
			snapshot.population/strategy.reconnaissance.offense_explorer_population_divisor)) : 0;
	offense.explorer_ratio=snapshot.prestige>0
		&& (large_economy_established()
			|| offense.utility>=strategy.economy.offense_explorer_ratio_utility)
		? (large_economy_established()
			? strategy.economy.large_economy_explorer_ratio
			: strategy.economy.normal_offense_explorer_ratio) : 0;
	// Campaign readiness and target reachability were already evaluated by the
	// tactical layer immediately above. The offense bidder asks for execution;
	// the arbiter may still reject it during a survival emergency.
	offense.attack_flags=budget.attack_flags;
	offense.attack_units=budget.attack_units;
	offense.construction_sites=strategy.construction.policy_low_sites;
}


void Maxima::arbitrate_policy_bids()
{
	const auto& rules=*context.observation().configuration;
	// Capability gates precede arbitration: an impossible technology bid would
	// otherwise win scarce labour and crowd out attainable economic projects.
	if (rules.isUnitUpgradesDisabled())
	{
		policy_bids[PolicyTechnology]=PolicyBid();
		policy_bids[PolicyAccess].desired_pools=0;
		for (auto& bid:policy_bids) bid.request_upgrades=false;
	}
	if (rules.isHungerDisabled()) policy_bids[PolicySurvival].desired_inns=0;
	if (rules.isPeacefulModeEnabled())
	{ policy_bids[PolicyDefense]=PolicyBid(); policy_bids[PolicyOffense]=PolicyBid(); }
	telemetry.count(AITrace::AI7::Maxima_arbitrate_policy_bids_calls);
	DirectorPlan result;
	const bool abundance_surge=abundance_surge_active();
	const PolicyBid& survival=policy_bids[PolicySurvival];
	const PolicyBid& growth=policy_bids[PolicyGrowth];
	const PolicyBid& access=policy_bids[PolicyAccess];
	const PolicyBid& technology=policy_bids[PolicyTechnology];
	const PolicyBid& defense=policy_bids[PolicyDefense];
	const PolicyBid& offense=policy_bids[PolicyOffense];

	result.desired_inns=Labour::innsWorthBuilding(survival.desired_inns,
		labour_observation.inns, labour_observation.innSeats,
		labour_observation.hungryUnits,
		abundance_surge ? strategy.economy.abundance_inn_target_cap
			: strategy.economy.inn_target_cap);
	result.desired_swarms=growth.desired_swarms;
	result.desired_pools=access.utility>=strategy.economy.pool_bid_utility_min
		? access.desired_pools : 0;
	result.desired_schools=technology.utility>=strategy.economy.school_bid_utility_min
		? technology.desired_schools : 0;
	result.desired_racetracks=
		technology.utility>=strategy.economy.racetrack_bid_utility_min
		? technology.desired_racetracks : 0;
	result.desired_barracks=defense.utility>=strategy.economy.barracks_bid_utility_min
		? defense.desired_barracks : 0;
	result.desired_hospital_beds=defense.desired_hospital_beds;
	result.desired_towers=defense.desired_towers;

	result.construction_sites=std::max(survival.construction_sites,
		std::max(growth.construction_sites,
			std::max(access.construction_sites,
				std::max(technology.construction_sites,
					defense.construction_sites))));
	const int workforce_site_cap=
		snapshot.population<strategy.construction.population_mid
			? strategy.construction.sites_low
		: (snapshot.population<strategy.construction.population_high
			? strategy.construction.sites_mid : strategy.construction.sites_high);
	result.construction_sites=std::min(result.construction_sites,
		workforce_site_cap);
	// Idle labour is the one thing a site cannot make up for later. Every
	// eight free workers beyond the training reserve open another site, so a
	// large colony with nothing to do builds instead of standing still.
	{
		const int spare=std::max(0, labour_observation.idle-labour_plan.trainingReserve);
		result.construction_sites=std::min(8,
			std::max(result.construction_sites, result.construction_sites+spare/8));
	}
	if(severe_food_emergency())
		result.construction_sites=std::max(strategy.construction.emergency_min_sites,
			std::min(strategy.construction.emergency_max_sites,
				survival.construction_sites));
	if(explorer_defense_emergency())
		result.construction_sites=std::max(result.construction_sites,
			strategy.construction.defense_emergency_sites);

	// The swarm controller is the sole authority for birth capacity.
	result.swarm_workers=growth.swarm_workers;
	result.worker_ratio=growth.worker_ratio;
	// An unmet explorer target always gets a nonzero production weight.
	// Utility may accelerate replacement, but cannot block it. Swarms apply
	// this mix only below the target and while the colony funds births.
	result.explorer_ratio=std::max(1, std::max(access.explorer_ratio,
		std::max(defense.explorer_ratio, offense.explorer_ratio)));
	result.warrior_ratio=std::max(defense.warrior_ratio, offense.warrior_ratio);
	result.desired_warriors=std::max(defense.desired_warriors,
		offense.utility>=strategy.economy.offense_bid_utility_min
			? offense.desired_warriors : 0);
	const bool army_short=snapshot.warriors<result.desired_warriors;
	const auto barracks=barracks_capacity(context);
	if(!rules.isUnitUpgradesDisabled() && army_short && snapshot.workers>=strategy.military.second_barracks_population_min)
	{
		// Keep one training place per four wanted warriors. Credit the finished
		// capacity of existing upgrade sites instead of imposing a building cap.
		const int seats_wanted=(result.desired_warriors+3)/4;
		const int new_seats=std::max(0,seats_wanted-barracks.second);
		const int basic_seats=std::max(1,(profile_variant(preferred_profile(AIMaximaBuildings::CombatTraining)) ? profile_variant(preferred_profile(AIMaximaBuildings::CombatTraining))->seats : 0));
		result.desired_barracks=std::max(result.desired_barracks,
			snapshot.barracks+(new_seats+basic_seats-1)/basic_seats);
	}
	if(army_short && labour_observation.workers>=labour_policy().bootstrapWorkforce)
	{
		// A hungry or injured worker is still part of the workforce. Fund jobs
		// plus one relief worker per three jobs, rather than replacing everybody
		// temporarily away at an inn/hospital with another mouth to feed.
		const int jobs=labour_observation.innCarriers+labour_observation.swarmCarriers
			+labour_observation.builders+labour_observation.otherAssigned
			+std::max(0,snapshot.worker_jobs_open);
		const int workers_wanted=std::max(labour_policy().bootstrapWorkforce,
			(jobs*4+2)/3+labour_plan.trainingReserve);
		if(snapshot.workers>=workers_wanted)
		{
			result.worker_ratio=0;
			result.warrior_ratio=std::max(1,result.warrior_ratio);
		}
	}
	// Training capacity constrains the combined birth stream, regardless of
	// which policy requested the warriors.
	const int untrained_warriors=std::max(0,
		snapshot.warriors-snapshot.trained_warriors);
	// Births are paced to the seats that can train them (pull, not push).
	const int training_backlog_limit=Labour::warriorBacklogLimit(
		std::max(labour_observation.barracksSeats,barracks.second),
		strategy.military.training_backlog_floor);
	if(!rules.isUnitUpgradesDisabled() && strategy.military.warrior_training_backlog_throttle_enabled
	   && untrained_warriors>=training_backlog_limit)
	{
		result.warrior_ratio=0;
	}
	result.desired_explorers=std::max(access.desired_explorers,
		std::max(defense.desired_explorers, offense.desired_explorers));
	// A rule that raised the warrior ratio for a large colony short of its army
	// used to sit here. It ran after the training-capacity throttle above and
	// silently overrode it, so births outran barracks seats and the colony made
	// untrained warriors, which carry a third of a trained one's damage. It
	// came from a batch that measured worse and was never justified on its own.
	result.defense_reserve=defense.defense_reserve;
	result.allow_upgrades=survival.request_upgrades
		|| technology.request_upgrades;
	result.allow_level2_upgrades=result.allow_upgrades
		&& snapshot.population>=strategy.upgrades.level2_population_min;
	result.attack_flags=!severe_colony_emergency() ? offense.attack_flags : 0;
	result.attack_units=result.attack_flags>0 ? offense.attack_units : 0;

	result.priority_inns=survival.utility+(100-environment.food_headroom)
		/strategy.scoring.priority_food_headroom_divisor;
	result.priority_swarms=growth.utility+(abundance_surge
		? strategy.scoring.priority_abundance_swarm_bonus : 0);
	result.priority_pools=access.utility+demands.mobility
		/strategy.scoring.priority_mobility_divisor
		+(abundance_surge
			&& snapshot.swimming_warriors
				<strategy.scoring.priority_pool_swimmer_threshold
			? strategy.scoring.priority_pool_swimmer_bonus : 0)
		+(large_economy_established()
			&& snapshot.population>=strategy.scoring.priority_pool_population_min
			&& snapshot.pools<strategy.scoring.priority_pool_count_max
			? strategy.scoring.priority_pool_network_bonus : 0);
	result.priority_schools=technology.utility+(abundance_surge
		? strategy.scoring.priority_abundance_school_bonus : 0);
	result.priority_racetracks=std::max(0,
		technology.utility-strategy.scoring.priority_racetrack_penalty);
	// Maxima used to buy a racetrack on a payback appraisal of walking speed,
	// and a school behind it. Measured over 228 paired games, dropping both is
	// worth 26 games turned from loss to win against 11 the other way. The
	// appraisal was right that faster walking pays and wrong about what it
	// competes with: the engine sends a free worker to whichever training
	// building is nearest, idle time is the scarcest thing the colony has, and
	// a racetrack spends it on walking when the same visit at a school would
	// buy the build level Maxima is actually short of. Technology is left to
	// the strategy values again, which also makes those values mean something.
	int barracks_priority_floor=0;
	// Military capital is different: warriors are work in progress until a
	// barracks has trained them, so the first barracks comes with the workforce
	// that can afford it rather than at a population mark, and a hospital
	// follows it, because recycling one trained warrior repays the building.
	if(snapshot.workers>=14 && !severe_food_emergency())
	{
		result.desired_barracks=std::max(result.desired_barracks, 1);
		if(snapshot.barracks==0)
			barracks_priority_floor=result.priority_swarms+4;
	}
	const int hospital_priority_floor=
		labour_observation.hurtUnits>labour_observation.hospitalSeats
			? result.priority_swarms+6 : 0;
	// Swimming lessons cost the same thousand worker-ticks as any other and
	// return nothing where water separates the colony from nothing.
	if(!labour_swimming_matters())
		result.desired_pools=0;

	result.priority_barracks=std::max(barracks_priority_floor,
		std::max(defense.utility, offense.utility)
		+std::min(strategy.scoring.priority_threat_cap,
			snapshot.visible_colony_threat*strategy.scoring.priority_threat_weight));
	result.priority_hospitals=std::max(hospital_priority_floor,
		defense.utility+(snapshot.need_heal>0
			? strategy.scoring.priority_healing_bonus : 0));
	result.priority_towers=defense.utility
		+(large_economy_established() && snapshot.enemy_prestige>0
			? strategy.scoring.priority_large_tower_bonus : 0)
		+(explorer_defense_active()
			? strategy.scoring.priority_tower_active_bonus : 0)
		+(explorer_defense_emergency()
			? strategy.scoring.priority_tower_emergency_bonus : 0);

	// Arbitration can add tactical minima; strip only unavailable capabilities.
	if (rules.isUnitUpgradesDisabled())
	{ result.desired_schools=result.desired_racetracks=result.desired_pools=0;
	  result.desired_barracks=0;
	  result.allow_upgrades=result.allow_level2_upgrades=false; }
	if (rules.isHungerDisabled()) result.desired_inns=0;
	if (rules.isPeacefulModeEnabled())
	{ result.desired_barracks=result.desired_towers=0; result.warrior_ratio=0;
	  result.attack_flags=result.attack_units=0; }
	budget=result;
}


void Maxima::finalize_director_plan(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	// Copy every executor-facing threshold into the immutable plan. From this
	// point until the next strategic cadence, tactical code does not reinterpret
	// configuration or posture.
	budget.allow_upgrades=!runtime.observation().configuration->isUnitUpgradesDisabled() && strategy.upgrades.enabled && budget.allow_upgrades
		&& snapshot.schools>=1
		&& snapshot.population>=strategy.upgrades.level1_population_min;
	budget.allow_level2_upgrades=budget.allow_upgrades
		&& budget.allow_level2_upgrades
		&& snapshot.population>=strategy.upgrades.level2_population_min;
	budget.upgrade_level1_workers=strategy.upgrades.level1_workers;
	budget.upgrade_level2_workers=strategy.upgrades.level2_workers;
	budget.upgrade_level1_trained_units_per_slot=
		strategy.upgrades.level1_trained_units_per_slot;
	budget.upgrade_level2_trained_units_per_slot=
		strategy.upgrades.level2_trained_units_per_slot;
	budget.upgrade_level1_inn_weight=strategy.upgrades.level1_inn_base_weight
		+demands.food/strategy.upgrades.food_demand_divisor
		+(100-environment.food_headroom)/strategy.upgrades.food_headroom_divisor;
	budget.upgrade_level1_hospital_weight=strategy.upgrades.level1_hospital_base_weight
		+environment.threat_pressure/strategy.upgrades.threat_divisor;
	budget.upgrade_level1_racetrack_weight=strategy.upgrades.level1_racetrack_base_weight
		+demands.technology/strategy.upgrades.technology_divisor;
	budget.upgrade_level1_pool_weight=strategy.upgrades.level1_pool_base_weight
		+demands.mobility/strategy.upgrades.mobility_divisor;
	budget.upgrade_level1_barracks_weight=strategy.upgrades.level1_barracks_base_weight
		+demands.military/strategy.upgrades.military_divisor;
	budget.upgrade_level2_inn_weight=strategy.upgrades.level2_inn_base_weight
		+demands.food/strategy.upgrades.food_demand_divisor
		+(100-environment.food_headroom)/strategy.upgrades.food_headroom_divisor;
	budget.upgrade_level2_hospital_weight=strategy.upgrades.level2_hospital_base_weight
		+environment.threat_pressure/strategy.upgrades.threat_divisor;
	budget.upgrade_level2_racetrack_weight=strategy.upgrades.level2_racetrack_base_weight
		+demands.technology/strategy.upgrades.technology_divisor;
	budget.upgrade_level2_pool_weight=strategy.upgrades.level2_pool_base_weight
		+demands.mobility/strategy.upgrades.mobility_divisor;
	budget.upgrade_level2_barracks_weight=strategy.upgrades.level2_barracks_base_weight
		+demands.military/strategy.upgrades.military_divisor;
	if(posture==PostureRecover)
	{
		budget.upgrade_level1_inn_weight=strategy.upgrades.recovery_inn_weight;
		budget.upgrade_level1_hospital_weight=0;
		budget.upgrade_level1_racetrack_weight=0;
		budget.upgrade_level1_pool_weight=0;
		budget.upgrade_level1_barracks_weight=0;
		budget.upgrade_level2_inn_weight=strategy.upgrades.recovery_inn_weight;
		budget.upgrade_level2_hospital_weight=0;
		budget.upgrade_level2_racetrack_weight=0;
		budget.upgrade_level2_pool_weight=0;
		budget.upgrade_level2_barracks_weight=0;
	}
	// An inn being upgraded feeds nobody. When the hungry already outnumber
	// half the seats the queue is forming, and taking an inn offline then is
	// the wrong way to add capacity: a new inn adds seats without losing any.
	// Upgrades wait for slack, whatever food pressure says.
	if(2*labour_observation.hungryUnits>labour_observation.innSeats)
	{
		budget.upgrade_level1_inn_weight=0;
		budget.upgrade_level2_inn_weight=0;
	}
	budget.first_prestige_trained_workers=
		strategy.upgrades.first_prestige_trained_workers;
	budget.second_prestige_trained_workers=
		strategy.upgrades.second_prestige_trained_workers;
	budget.second_prestige_population_min=
		strategy.upgrades.second_prestige_population_min;
	budget.food_ledger_enabled=strategy.food.enabled;
	budget.food_retirement_enabled=strategy.food.retirement_enabled;
	budget.food_inn_burden_percent=strategy.food.inn_burden_coverage_percent;
	budget.food_swarm_burden_percent=strategy.food.swarm_burden_coverage_percent;
	budget.food_recovered_percent=strategy.food.recovered_coverage_percent;
	budget.food_burden_confirm_ticks=strategy.food.burden_confirm_ticks;
	budget.food_retirement_cooldown_ticks=
		strategy.food.retirement_cooldown_ticks;
	budget.food_relocation_enabled=strategy.food.relocation_enabled;
	budget.food_relocation_min_quality_tiles=
		strategy.food.relocation_min_quality_tiles;
	budget.food_relocation_confirm_ticks=strategy.food.relocation_confirm_ticks;
	budget.food_relocation_cooldown_ticks=
		strategy.food.relocation_cooldown_ticks;
	budget.food_relocation_offer_ticks=strategy.food.relocation_offer_ticks;
	// Discount modelled inn capacity once, here, so the executor never needs the
	// economic model to judge whether removing an inn would starve anyone.
    budget.food_inn_seats_level1=budget.food_inn_seats_level2=budget.food_inn_seats_level3=0;

	budget.staffing_window_samples=strategy.staffing.control_window_samples;
	budget.staffing_low_permille=strategy.staffing.control_low_permille;
	budget.staffing_high_permille=strategy.staffing.control_high_permille;
	budget.staffing_slack=strategy.staffing.control_slack;
	budget.staffing_minimum_workers=strategy.staffing.control_minimum_workers;
	budget.staffing_maximum_workers=strategy.staffing.control_maximum_workers;
	budget.staffing_cooldown_passes=strategy.staffing.control_cooldown_passes;
	budget.staffing_new_inn_workers=strategy.staffing.new_inn_workers;
	budget.staffing_new_swarm_workers=strategy.staffing.new_swarm_workers;
	budget.swarm_supply_radius=
		strategy.staffing.swarm_supply_radius;
	budget.attack_clearing_workers=strategy.staffing.attack_clearing_workers;

	budget.recovery_active=posture==PostureRecover;
	budget.food_emergency=severe_food_emergency();
	budget.colony_emergency=severe_colony_emergency();
	// Expansion buys access to new food, independently of the birth-capacity
	// target. It still shares real builders and the normal swarm staffing pool.
	const bool colony_active=development_planner.activeRoleBuildCount(
		AIMaximaBuildings::Production, AIMaximaPlacement::ColonySeed)>0;
	bool colony_starting=false;
	for(const auto& entry:development_planner.actions())
	{
		const auto& action=entry.second;
		if(action.purpose!=AIMaximaPlacement::ColonySeed
		   || action.state!=AIMaximaPlacement::Completed) continue;
		const AIEngine::BuildingView* building=runtime.get_building_register().get_building(action.buildingId);
		// Completion alone is not success: do not chain empty, unstaffed outposts.
		// Once provisioned, remember startup so a later pause cannot block expansion.
		if(building && !operating_colonies.count(action.id))
		{
			bool ready=false;for(const auto& recipe:AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).semantics.production.recipes)if(recipe.enabled){bool stocked=true;for(int r=0;r<MAX_NB_RESOURCES;++r)stocked&=runtime.observation().buildingResources(*building)[r]>=recipe.cost[r];ready|=stocked;}
			if(ready)
			{
				operating_colonies.insert(action.id);
				emit_telemetry(runtime,"colony_swarm_operating",
					"\tbuilding_id="+telemetryText(action.buildingId));
			}
			else colony_starting=true;
		}
	}
	budget.colony_swarm_requested=false;
	if(!strategy.colonization.enabled) colony_gate_reason="disabled";
	else if(colony_active) colony_gate_reason="colonial construction active";
	else if(colony_starting) colony_gate_reason="colony awaiting workers and food";
	else if(snapshot.free_workers-snapshot.worker_jobs_open
		<strategy.staffing.construction_swarm_workers)
		colony_gate_reason="insufficient free builders";
	else
	{
		budget.colony_swarm_requested=true;
		colony_gate_reason="eligible";
	}
	budget.can_swim=snapshot.swimming_workers>0;
	if(strategy.economy.worker_birth_throttle_enabled && snapshot.workers>0
	   && snapshot.population>=strategy.economy.worker_birth_stop_population_min
	   && snapshot.free_workers-snapshot.worker_jobs_open
		>snapshot.workers/strategy.economy.worker_birth_stop_surplus_divisor)
		budget.worker_ratio=1;

	const int explorer_minimum=large_economy_established()
		? strategy.explorer_campaign.large_economy_trained_min
		: strategy.explorer_campaign.trained_min;
	budget.explorer_campaign_active=strategy.explorer_campaign.enabled
		&& snapshot.prestige>0
		&& !budget.colony_emergency
		&& snapshot.trained_explorers>=explorer_minimum;
	budget.explorer_campaign_flags=budget.explorer_campaign_active
		? std::min(strategy.explorer_campaign.max_flags,
			snapshot.trained_explorers>=strategy.explorer_campaign.multi_flag_trained_min
				? strategy.explorer_campaign.max_flags : 1)
		: 0;
	budget.explorer_campaign_units_per_flag=
		strategy.explorer_campaign.units_per_flag;
	budget.fruit_active=strategy.fruit.enabled && runtime.is_fruit_on_map();
	if(budget.fruit_active)
	{
		int missions=0;
		for(int fruit=CHERRY;fruit<=PRUNE;++fruit)
			missions+=runtime.resource_flags(fruit).size();
		budget.desired_explorers+=missions*strategy.fruit.units_per_flag;
	}
	budget.fruit_units_per_flag=strategy.fruit.units_per_flag;
	budget.fruit_flag_radius=strategy.fruit.flag_radius;

	budget.reactive_defense_enabled=strategy.reactive_defense.enabled;
	budget.reactive_defense_flag_radius=strategy.reactive_defense.flag_radius;
	budget.reactive_defense_move_radius=strategy.reactive_defense.move_radius;
	budget.reactive_defense_move_deadband=
		strategy.reactive_defense.move_deadband;
	budget.reactive_defense_unit_cap=strategy.reactive_defense.unit_cap;
	budget.reactive_defense_advantage_min=strategy.reactive_defense.advantage_min;
	budget.reactive_defense_advantage_percent=
		strategy.reactive_defense.advantage_percent;
	budget.tactical_review_interval=strategy.tactics.review_interval_ticks;
	budget.tactics_enabled=strategy.tactics.enabled;
	if(tactical_mission.flagId<0)
		budget.tactical_flag_level=runtime.observation().configuration->isUnitUpgradesDisabled() ? 1 : strategy.tactics.flag_minimum_level;
	budget.tactical_siege_radius=strategy.tactics.siege_flag_radius;
	budget.raid_flag_radius=strategy.raiding.flag_radius;
	budget.tactical_stall_ticks=strategy.tactics.stall_ticks;
	budget.tactical_quarantine_enabled=strategy.tactics.failed_target_quarantine_enabled;
	budget.tactical_quarantine_ticks=strategy.tactics.failed_target_quarantine_ticks;
	budget.preemptive_effective_zone_max=
		strategy.military.preemptive_defense_enabled
		? AIMaxima::Defense::effectiveZoneCap(snapshot.trained_warriors,
			strategy.military.preemptive_defense_min_trained_warriors,
			strategy.military.preemptive_defense_warriors_per_zone,
			strategy.military.preemptive_defense_max_zones) : 0;
	budget.preemptive_defense_active=
		budget.preemptive_effective_zone_max>0;
	budget.preemptive_amphibious_active=AIMaxima::Defense::amphibiousEligible(
		budget.preemptive_defense_active,
		strategy.military.preemptive_defense_amphibious_enabled,
		snapshot.swimming_warriors,
		strategy.military.preemptive_defense_min_swimming_warriors);
	budget.target_switch_margin=strategy.scoring.target_switch_margin;
	budget.preemptive_recompute_ticks=
		strategy.scheduling.preemptive_defense_recompute_ticks;
	budget.preemptive_inner_distance=
		strategy.military.preemptive_defense_inner_distance;
	budget.preemptive_band_width=
		strategy.military.preemptive_defense_band_width;
	budget.preemptive_path_slack=
		strategy.military.preemptive_defense_path_slack;
	budget.preemptive_probe_radius=
		strategy.military.preemptive_defense_choke_probe_radius;
	budget.preemptive_cross_section_max=
		strategy.military.preemptive_defense_max_cross_section;
	budget.preemptive_zone_radius=
		strategy.military.preemptive_defense_zone_radius;
	budget.preemptive_zone_max=
		strategy.military.preemptive_defense_max_zones;
	budget.reconnaissance_flag_radius=strategy.reconnaissance.flag_radius_tiles;
	budget.reconnaissance_review_interval=
		strategy.reconnaissance.mission_review_interval_ticks;
	budget.reconnaissance_economic_watch_revisit=
		strategy.reconnaissance.economic_watch_revisit_ticks;
	budget.farming_normal_interval=strategy.farming.normal_interval_ticks;
	budget.farming_urgent_interval=strategy.farming.urgent_interval_ticks;
	budget.farming_enabled=strategy.farming.enabled;
	budget.farming_protection_enabled=strategy.farming.farm_protection_enabled;
	budget.farming_maintenance_clearing_enabled=
		strategy.farming.maintenance_clearing_enabled;
	budget.farming_resource_preserving_circulation_enabled=
		strategy.farming.resource_preserving_circulation_enabled;
	budget.farming_wheat_invasion_clearing_enabled=
		strategy.farming.wheat_invasion_clearing_enabled;
	budget.farming_wood_firebreak_enabled=
		strategy.farming.wood_firebreak_enabled;
	budget.farming_proactive_clearing_enabled=
		strategy.farming.proactive_clearing_enabled;
	budget.farming_management_radius=strategy.farming.management_radius;
	budget.farming_wheat_fertility_min=strategy.farming.wheat_fertility_min;
	budget.farming_wood_fertility_base_percent=
		strategy.farming.wood_fertility_base_percent;
	budget.farming_wood_fertility_pressure_percent=
		strategy.farming.wood_fertility_pressure_percent;
	budget.farming_wood_pressure_base=strategy.farming.wood_pressure_base;
	budget.farming_wood_pressure_space_divisor=
		strategy.farming.wood_pressure_space_divisor;
	budget.farming_wood_pressure_supply_divisor=
		strategy.farming.wood_pressure_supply_divisor;
	budget.farming_wood_pressure_construction_divisor=
		strategy.farming.wood_pressure_construction_divisor;
	budget.farming_wood_pressure_growth_divisor=
		strategy.farming.wood_pressure_growth_divisor;
	budget.farming_economic_envelope_radius=
		strategy.farming.economic_envelope_radius_tiles;
	const int wood_supply=Farming::woodSupplyScore(environment.accessible_wood,
		snapshot.population, strategy.farming.wood_supply_scale,
		strategy.farming.wood_supply_population_offset);
	budget.farming_wood_pressure=Farming::woodClearPressure(
		environment.space_capacity, wood_supply, recent_construction_failures,
		demands.growth, budget.farming_wood_pressure_base,
		budget.farming_wood_pressure_space_divisor,
		budget.farming_wood_pressure_supply_divisor,
		budget.farming_wood_pressure_construction_divisor,
		budget.farming_wood_pressure_growth_divisor,
		strategy.farming.construction_failure_pressure);
	budget.farming_minimum_wood_fertility=Farming::minimumWoodFertility(
		budget.farming_wood_pressure, budget.farming_wood_fertility_base_percent,
		budget.farming_wood_fertility_pressure_percent);
	budget.farming_min_workers_for_clearing=strategy.farming.proactive_workers_min;
	budget.farming_clearing_cooldown=strategy.farming.proactive_cooldown_ticks;
	budget.farming_clearing_duration=strategy.farming.proactive_duration_ticks;
	budget.farming_clearing_quota=strategy.farming.proactive_quota;
	const bool placement_pressure=recent_construction_failures
		>=strategy.farming.proactive_failure_threshold
		&& timer-last_construction_failure_tick<=budget.farming_clearing_duration;
	const bool established_wood_pressure=
		snapshot.buildings>=strategy.farming.proactive_building_threshold
		&& (opening_space_constrained
			|| budget.farming_wood_pressure>budget.farming_wood_pressure_base);
	budget.farming_allow_proactive_clearing=
		strategy.farming.enabled
		&& strategy.farming.proactive_clearing_enabled
		&& timer>=strategy.farming.proactive_start_tick && !budget.recovery_active
		&& timer-last_proactive_clearing_tick>=budget.farming_clearing_cooldown
		&& snapshot.workers>=budget.farming_min_workers_for_clearing
		&& (placement_pressure || established_wood_pressure);
	budget.farming_clearing_for_placement=placement_pressure;
	budget.farming_urgent=farming_urgent || budget.food_emergency
		|| environment.space_capacity<strategy.farming.urgent_space_threshold
		|| recent_construction_failures>=strategy.farming.proactive_failure_threshold;
}


void Maxima::emit_director_snapshot(Context& runtime) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	MapInfo world(runtime);
	int estimated_enemy_warriors=0;
	for(int team=0; team<Team::MAX_COUNT; ++team)
		if(opponents[team].alive)
			estimated_enemy_warriors=std::max(estimated_enemy_warriors,
				opponents[team].estimated_warriors);
	int assigned_building_workers=0;
	int inn_workers=0;
	int swarm_workers=0;
	int technology_workers=0;
	int military_workers=0;
	for(int i=0; i<Building::MAX_COUNT; ++i)
	{
		const AIEngine::BuildingView* building=runtime.observation().buildingSlots(runtime.teamNumber())[i];
		if(!building || AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).isBuildingSite)
			continue;
		const int assigned=building->maxUnitWorking;
		assigned_building_workers+=assigned;
		const unsigned roles=AIMaximaBuildings::capabilities(runtime.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*building));
		// Each assigned worker is charged once even when its building supplies
		// several services from the same inventory.
		if(roles&AIMaximaBuildings::roleBit(AIMaximaBuildings::Production))swarm_workers+=assigned;
		else if(roles&AIMaximaBuildings::roleBit(AIMaximaBuildings::Feeding))inn_workers+=assigned;
		else if(roles&(AIMaximaBuildings::roleBit(AIMaximaBuildings::ConstructionTraining)|AIMaximaBuildings::roleBit(AIMaximaBuildings::WalkTraining)|AIMaximaBuildings::roleBit(AIMaximaBuildings::SwimTraining)))technology_workers+=assigned;
		else if(roles)military_workers+=assigned;
	}
	const TeamStat* stat=&runtime.observedTeam().statistics;
	const bool endgame=snapshot.alive_enemies<=2;
	const int campaign_population_required=std::max(endgame ? 45 : 50,
		(endgame ? 55 : 70)-demands.aggression/5);
	const int campaign_warriors_required=endgame
		? std::max(24, estimated_enemy_warriors+budget.defense_reserve+3)
		: std::max(28, estimated_enemy_warriors+budget.defense_reserve+4);
	const bool campaign_safe=!severe_food_emergency()
		&& !severe_colony_emergency();
	const int campaign_worker_floor=std::max(38, 55-demands.growth/5);
	const bool campaign_economy_ready=snapshot.population>0
		&& std::max(snapshot.unserved_food,snapshot.critical_food)*100
			<=snapshot.population*12
		&& snapshot.workers>=campaign_worker_floor
		&& snapshot.workers*2>=snapshot.population;
	const int connected_boom=std::max(0,
		environment.connected_abundance-50);
	const int access_barrier=environment.mobility_opportunity
		*(100-environment.connected_abundance)/100;
	const int effective_growth_site_mid=clamp_score(
		strategy.economy.growth_site_utility_mid-connected_boom/2
			+access_barrier/2);
	const int effective_growth_site_high=clamp_score(
		strategy.economy.growth_site_utility_high-connected_boom
			+access_barrier/2);
	const int effective_school_population=std::max(0,
		strategy.economy.school_population_min-connected_boom/3
			+access_barrier);
	const int effective_second_school_utility=clamp_score(
		strategy.economy.second_school_utility_min-connected_boom/3
			+access_barrier/2);
	const int effective_pool_population=std::max(0,
		strategy.economy.pool_population_min-access_barrier/4);
	const int effective_pool_utility=std::max(10,
		strategy.economy.pool_utility_min-access_barrier/3);
	std::ostringstream fields;
	fields<<"\tposture="<<posture_name(posture)
		<<"\tmap_width="<<world.get_width()
		<<"\tmap_height="<<world.get_height()
		<<"\tglobal_water_tiles="<<global_water_tiles
		<<"\tglobal_grass_tiles="<<global_grass_tiles
		<<"\tglobal_land_tiles="<<global_land_tiles
		<<"\tglobal_buildable_tiles="<<global_buildable_tiles
		<<"\tglobal_corn_tiles="<<global_corn_tiles
		<<"\tglobal_wood_tiles="<<global_wood_tiles
		<<"\tglobal_stone_tiles="<<global_stone_tiles
		<<"\tglobal_algae_tiles="<<global_algae_tiles
		<<"\tknown_algae_units="<<known_algae_units
		<<"\twalk_accessible_algae_units="<<walk_accessible_algae_units
		<<"\tswim_accessible_algae_units="<<swim_accessible_algae_units
		<<"\taccessible_algae_units="<<accessible_algae_units
		<<"\tglobal_fruit_tiles="<<global_fruit_tiles
		<<"\tglobal_water_percent="<<global_water_percent
		<<"\tglobal_shoreline_density="<<global_shoreline_density
		<<"\tglobal_land_components="<<global_land_components
		<<"\tglobal_largest_land_percent="<<global_largest_land_percent
		<<"\tglobal_start_land_percent="<<global_start_land_percent
		<<"\tglobal_chokepoint_density="<<global_chokepoint_density
		<<"\tterrain_abundance="<<environment.terrain_abundance
		<<"\tconnected_abundance="<<environment.connected_abundance
		<<"\tmobility_opportunity="<<environment.mobility_opportunity
		<<"\tterrain_connected_boom="<<connected_boom
		<<"\tterrain_access_barrier="<<access_barrier
		<<"\teffective_growth_site_mid="<<effective_growth_site_mid
		<<"\teffective_growth_site_high="<<effective_growth_site_high
		<<"\teffective_school_population="<<effective_school_population
		<<"\teffective_second_school_utility="<<effective_second_school_utility
		<<"\teffective_pool_population="<<effective_pool_population
		<<"\teffective_pool_utility="<<effective_pool_utility
		<<"\ttopology_complexity="<<environment.topology_complexity
		<<"\tposture_age="<<(timer-posture_since)
		<<"\tenvironment_confidence="<<environment.confidence
		<<"\tresource_capacity="<<environment.resource_capacity
		<<"\tspace_capacity="<<environment.space_capacity
		<<"\tfood_security="<<environment.food_security
		<<"\tfood_headroom="<<environment.food_headroom
		<<"\tabundance="<<environment.abundance
		<<"\tabundance_surge="<<(abundance_surge_active() ? 1 : 0)
		<<"\teconomic_momentum="<<environment.economic_momentum
		<<"\tmobility_constraint="<<environment.mobility_constraint
		<<"\tthreat_pressure="<<environment.threat_pressure
		<<"\tknown_local_tiles="<<environment.known_tiles
		<<"\taccessible_corn="<<environment.accessible_corn
		<<"\taccessible_corn_fraction="<<environment.accessible_corn_fraction
		<<"\taccessible_wood="<<environment.accessible_wood
		<<"\taccessible_stone="<<environment.accessible_stone
		<<"\taccessible_algae="<<environment.accessible_algae
		<<"\tbuildable_tiles="<<environment.buildable_tiles
		<<"\twater_tiles="<<environment.water_tiles
		<<"\tfeeding_capacity="<<environment.feeding_capacity
		<<"\tdemand_survival="<<demands.survival
		<<"\tdemand_food="<<demands.food
		<<"\tdemand_growth="<<demands.growth
		<<"\tdemand_expansion="<<demands.expansion
		<<"\tdemand_access="<<demands.access
		<<"\tdemand_technology="<<demands.technology
		<<"\tdemand_mobility="<<demands.mobility
		<<"\tdemand_military="<<demands.military
		<<"\tdemand_aggression="<<demands.aggression
		<<"\tpopulation="<<snapshot.population
		<<"\tpopulation_trend="<<trends.population
		<<"\tworkers="<<snapshot.workers
		<<"\ttrained_workers="<<snapshot.trained_workers
		<<"\texplorers="<<snapshot.explorers
		<<"\ttrained_explorers="<<snapshot.trained_explorers
		<<"\tprestige="<<snapshot.prestige
		<<"\tenemy_prestige="<<snapshot.enemy_prestige
		<<"\tworker_trend="<<trends.workers
		<<"\tfree_workers="<<snapshot.free_workers
		<<"\tworker_jobs_open="<<snapshot.worker_jobs_open
		<<"\tassigned_building_workers="<<assigned_building_workers
		<<"\tinn_workers="<<inn_workers
		<<"\tswarm_workers="<<swarm_workers
		<<"\ttechnology_workers="<<technology_workers
		<<"\tmilitary_workers="<<military_workers
		<<"\tworker_ratio="<<budget.worker_ratio
		<<"\texplorer_ratio="<<budget.explorer_ratio
		<<"\twarrior_ratio="<<budget.warrior_ratio
		<<"\tswarm_worker_budget="<<budget.swarm_workers
		<<"\twarriors="<<snapshot.warriors
		<<"\ttrained_warriors="<<snapshot.trained_warriors
		<<"\tswimming_workers="<<snapshot.swimming_workers
		<<"\tswimming_explorers="<<snapshot.swimming_explorers
		<<"\tswimming_warriors="<<snapshot.swimming_warriors
		<<"\tamphibious_attack_explorers="
			<<snapshot.amphibious_attack_explorers
		<<"\tdefense_reserve="<<budget.defense_reserve
		<<"\twarrior_trend="<<trends.warriors
		<<"\tunserved_food="<<snapshot.unserved_food
		<<"\tcritical_food="<<snapshot.critical_food
		<<"\tfood="<<stat->totalFood
		<<"\tfood_capacity="<<stat->totalFoodCapacity
		<<"\tfood_trend="<<trends.food_pressure
		<<"\tvisible_enemy_warriors="<<snapshot.visible_enemy_warriors
		<<"\tvisible_enemy_explorers="<<snapshot.visible_enemy_explorers
		<<"\tvisible_enemy_attack_explorers="
			<<snapshot.visible_enemy_attack_explorers
		<<"\tvisible_colony_explorer_threat="
			<<snapshot.visible_colony_explorer_threat
		<<"\texplorer_defense_active="<<(explorer_defense_active() ? 1 : 0)
		<<"\texplorer_defense_emergency="
			<<(explorer_defense_emergency() ? 1 : 0)
		<<"\testimated_enemy_warriors="<<estimated_enemy_warriors
		<<"\talive_enemies="<<snapshot.alive_enemies
		<<"\tvisible_colony_threat="<<snapshot.visible_colony_threat
		<<"\tthreatened_units="<<snapshot.own_units_under_attack
		<<"\tthreatened_buildings="<<snapshot.own_buildings_under_attack
		<<"\tneed_heal="<<snapshot.need_heal
		<<"\tbuildings="<<snapshot.buildings
		<<"\tbuilding_sites="<<snapshot.building_sites
		<<"\tinns="<<snapshot.inns
		<<"\tswarms="<<snapshot.swarms
		<<"\tbarracks="<<snapshot.barracks
		<<"\thospitals="<<snapshot.hospitals
		<<"\tschools="<<snapshot.schools
		<<"\tschool_level1="<<snapshot.school_level1
		<<"\tschool_level2="<<snapshot.school_level2
		<<"\tschool_level3="<<snapshot.school_level3
		<<"\tpools="<<snapshot.pools
		<<"\tracetracks="<<snapshot.racetracks
		<<"\tallow_upgrades="<<(budget.allow_upgrades ? 1 : 0)
		<<"\tupgrading_phase_1="<<(budget.allow_upgrades ? 1 : 0)
		<<"\tupgrading_phase_2="<<(budget.allow_level2_upgrades ? 1 : 0)
		<<"\tconstruction_budget="<<budget.construction_sites
		<<"\tpriority_inns="<<budget.priority_inns
		<<"\tpriority_swarms="<<budget.priority_swarms
		<<"\tpriority_pools="<<budget.priority_pools
		<<"\tpriority_schools="<<budget.priority_schools
		<<"\tpriority_barracks="<<budget.priority_barracks
		<<"\tpriority_hospitals="<<budget.priority_hospitals
		<<"\tpriority_towers="<<budget.priority_towers
		<<"\tdesired_inns="<<budget.desired_inns
		<<"\tinn_level1="<<snapshot.inn_level1
		<<"\tinn_level2="<<snapshot.inn_level2
		<<"\tinn_level3="<<snapshot.inn_level3
		<<"\tdesired_swarms="<<budget.desired_swarms
		<<"\tcolonization_eligible="<<(budget.colony_swarm_requested ? 1 : 0)
		<<"\tcolonization_gate="<<colony_gate_reason
		<<"\tactive_colonial_action="<<development_planner.activeRoleBuildCount(
			AIMaximaBuildings::Production, AIMaximaPlacement::ColonySeed)
		<<"\testablished_colonies="<<established_colonies
		<<"\tcleared_hotspots="<<cleared_enemy_sites.size()
		<<"\tdesired_barracks="<<budget.desired_barracks
		<<"\tdesired_hospital_beds="<<budget.desired_hospital_beds
		<<"\tdesired_schools="<<budget.desired_schools
		<<"\tdesired_towers="<<budget.desired_towers
		<<"\ttowers="<<snapshot.towers
		<<"\ttower_stone="<<snapshot.tower_stone
		<<"\ttower_bullets="<<snapshot.tower_bullets
		<<"\tdesired_explorers="<<budget.desired_explorers
		<<"\tdesired_warriors="<<budget.desired_warriors
		<<"\tspace_constrained="<<(opening_space_constrained ? 1 : 0)
		<<"\tconstruction_failures="<<recent_construction_failures
		<<"\tclearing_active="<<(proactive_clearing_flag!=-1 ? 1 : 0)
		<<"\tattack_budget="<<budget.attack_flags
		<<"\tcampaign_population_required="<<campaign_population_required
		<<"\tcampaign_worker_floor="<<campaign_worker_floor
		<<"\tcampaign_warriors_required="<<campaign_warriors_required
		<<"\tcampaign_population_ready="
			<<(snapshot.population>=campaign_population_required ? 1 : 0)
		<<"\tcampaign_force_ready="
			<<(snapshot.trained_warriors>=campaign_warriors_required ? 1 : 0)
		<<"\tcampaign_safe="<<(campaign_safe ? 1 : 0)
		<<"\tcampaign_economy_ready="<<(campaign_economy_ready ? 1 : 0)
		<<"\tcombat_ready_idle="
			<<(snapshot.trained_warriors>=campaign_warriors_required
				&& budget.attack_flags==0 ? 1 : 0)
		<<"\tcampaign_ready_idle="
			<<(snapshot.population>=campaign_population_required
				&& snapshot.trained_warriors>=campaign_warriors_required
				&& campaign_safe && campaign_economy_ready
				&& timer>=campaign.cooldown_until
				&& budget.attack_flags==0 ? 1 : 0)
		<<"\tcampaign_state="<<int(campaign.state)
		<<"\tcampaign_cooldown="<<std::max(0, campaign.cooldown_until-timer)
		<<"\ttarget="<<target
		<<"\ttactical_review_tick="<<offense_diagnostics.tick
		<<"\toffense_gate="<<offense_diagnostics.gate
		<<"\toffense_decision="<<offense_diagnostics.decision
		<<"\toffense_eligible="<<offense_diagnostics.eligibleWarriors
		<<"\toffense_training_slots="<<offense_diagnostics.openTrainingSlots
		<<"\tsiege_candidates="<<offense_diagnostics.buildingCandidates
		<<"\tviable_sieges="<<offense_diagnostics.viableBuildings
		<<"\traid_candidates="<<offense_diagnostics.clusterCandidates
		<<"\tviable_raids="<<offense_diagnostics.viableClusters
		<<"\tmission_kind="<<Tactics::missionKindName(tactical_mission.kind)
		<<"\tmission_phase="<<Tactics::missionPhaseName(tactical_mission.phase)
		<<"\tmission_requested="<<tactical_mission.requestedForce
		<<"\tconfigured_campaign_worker_floor="<<std::max(
			strategy.military.campaign_worker_floor,
			strategy.military.campaign_worker_floor_base
				-demands.growth/strategy.military.readiness_demand_divisor)
		<<"\tconfigured_attack_unit_cap="<<strategy.military.attack_unit_cap;
	for(int candidate=0; candidate<PostureCount; ++candidate)
		fields<<"\tutility_"<<posture_name(StrategicPosture(candidate))
			<<"="<<posture_utilities[candidate];
	for(int policy=0; policy<PolicyCount; ++policy)
		fields<<"\tbid_"<<policy_name(PolicyKind(policy))
			<<"="<<policy_bids[policy].utility;
	for(std::map<std::string,int>::const_iterator r=offense_diagnostics.rejections.begin();
		r!=offense_diagnostics.rejections.end(); ++r)
		fields<<"\toffense_reject_"<<r->first<<"="<<r->second;
	emit_telemetry(runtime, "director_snapshot", fields.str());
}


void Maxima::evaluate_strategy(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	telemetry.count(AITrace::AI7::Maxima_evaluate_strategy_calls);
	if(strategy.reconnaissance.enabled)
		update_reconnaissance(runtime);
	else
	{
		if(!reconnaissance.report().missions.empty())
			remove_reconnaissance_missions(runtime, "disabled");
		reconnaissance.reset();
		force_beliefs.clear();
		tactics.reset();
	}
	prune_cleared_enemy_sites();
	StrategicSnapshot next=collect_snapshot(runtime);
	labour_observation=observe_labour(runtime);
	if(director.initialized)
	{
		previous_snapshot=snapshot;
		snapshot=next;
		update_trends();
	}
	else
	{
		snapshot=next;
		previous_snapshot=next;
	}
	update_environment_model(runtime);
	const bool bomb_visible=strategy.military.explorer_defense_enabled
		&& (snapshot.visible_enemy_attack_explorers
			>=strategy.reconnaissance.explorer_attack_warning_threshold
		|| snapshot.visible_colony_explorer_threat
			>=strategy.reconnaissance.explorer_colony_warning_threshold);
	const bool bomb_at_colony=strategy.military.explorer_defense_enabled
		&& snapshot.visible_colony_explorer_threat
			>=strategy.reconnaissance.explorer_colony_warning_threshold;
	const bool new_explorer_alert=bomb_visible
		&& timer>=explorer_threat_until;
	const bool new_colony_alert=bomb_at_colony
		&& timer>=explorer_colony_threat_until;
	if(bomb_visible)
		explorer_threat_until=timer
			+strategy.scheduling.explorer_warning_duration_ticks;
	if(bomb_at_colony)
		explorer_colony_threat_until=timer
			+strategy.scheduling.explorer_colony_warning_duration_ticks;
	if(new_explorer_alert || new_colony_alert)
		emit_telemetry(runtime, "explorer_defense_alert",
			"\tattack_explorers="
				+telemetryText(snapshot.visible_enemy_attack_explorers)
			+"\tnear_colony="
				+telemetryText(snapshot.visible_colony_explorer_threat));
	update_opponent_models(runtime);
	if (runtime.observation().configuration->isHungerDisabled()) environment.food_headroom=100;
	score_demands();
	// Capability changes must reach posture selection, not only the final executor.
	const auto& rules=*runtime.observation().configuration;
	if (rules.isUnitUpgradesDisabled()) demands.technology=demands.mobility=0;
	if (rules.isPeacefulModeEnabled()) demands.military=demands.aggression=0;
	score_postures();
	const StrategicPosture previous_posture=posture;
	select_posture();
	if (posture != previous_posture)
		telemetry.count(AITrace::AI7::runtime_posture_changed);
	allocate_resources();
	finalize_director_plan(runtime);
	plan_offense(runtime);
	// Explorer strikes share the offensive target. Keep the existing strategic
	// target selector available when there is no warrior mission to follow.
	if(budget.tactical_kind==Tactics::MissionRaid
	   || budget.tactical_kind==Tactics::MissionSiege)
		target=budget.tactical_target_team;
	else
		choose_enemy_target(runtime);
	plan_reconnaissance_objectives(runtime);
	emit_ablation_opportunities(runtime);
	if(!director.initialized || posture!=previous_posture)
		emit_telemetry(runtime, "posture_changed",
			"\tprevious="+std::string(director.initialized
				? posture_name(previous_posture) : "none")
			+"\tposture="+posture_name(posture));
	if(timer-last_director_telemetry_tick>=1000)
	{
		emit_director_snapshot(runtime);
		std::ostringstream recon_fields;
		const Recon::ReconReport& recon=reconnaissance.report();
		recon_fields<<"\texplored_percent="<<recon.exploredPercent
			<<"\tdesired_missions="<<recon.desiredMissions
			<<"\tactive_missions="<<recon.missions.size();
		for(std::map<int, Recon::OpponentIntel>::const_iterator opponent=
			recon.opponents.begin(); opponent!=recon.opponents.end(); ++opponent)
		{
			if(!opponent->second.alive)
				continue;
			recon_fields<<"\tteam_"<<opponent->first<<"_visible_warriors="
				<<opponent->second.visibleWarriors
				<<"\tteam_"<<opponent->first<<"_estimated_warriors="
				<<opponent->second.estimatedWarriors
				<<"\tteam_"<<opponent->first<<"_visible_explorers="
				<<opponent->second.visibleExplorers
				<<"\tteam_"<<opponent->first<<"_known_buildings="
				<<opponent->second.knownBuildings
				<<"\tteam_"<<opponent->first<<"_confidence="
				<<opponent->second.confidence
				<<"\tteam_"<<opponent->first<<"_contact_age="
				<<std::max(0, timer-opponent->second.lastSeenTick);
		}
		emit_telemetry(runtime, "recon_snapshot", recon_fields.str());
		last_director_telemetry_tick=timer;
	}
}


void Maxima::ensure_strategy()
{
	if(strategy_resolved)
		return;
    auto ownerObservation=context.scopeOwnerObservation();
    // Normal engine setup captures canonical player values. Direct controller
    // construction already resolved its private file-backed defaults on the
    // owner; an empty config must not reopen those files on a worker.
    if(!context.observation().configuration->getAIConfig(context.playerNumber()).empty())
        resolve_strategy(true);
	strategy_resolved=true;
}

std::string Maxima::canonicalStrategy() const
{
	return StrategyResolver::canonicalValues(strategy);
}


void Maxima::resolve_strategy(bool announce)
{
    auto ownerObservation=context.scopeOwnerObservation();
	const auto& world=context.observation();
	ResolvedStrategy resolved;
	std::string error;
	if(!StrategyResolver::resolveForPlayer(*world.configuration,
		context.playerNumber(), resolved, error))
	{
		throw std::runtime_error("Maxima strategy error: "+error);
	}
	strategy=resolved.values;
	// Game::setGameHeader has already canonicalized the finalized configuration.
	// A controller may also be constructed before that setup step, but it never
	// changes the shared header while deciding an order.
	budget.swarm_supply_radius=
		strategy.staffing.swarm_supply_radius;
	budget.attack_clearing_workers=strategy.staffing.attack_clearing_workers;
	reconnaissance.configure(strategy.reconnaissance.memory_horizon_ticks,
		strategy.reconnaissance.force_memory_hold_ticks,
		strategy.reconnaissance.stale_contact_age_ticks,
		strategy.reconnaissance.force_memory_enabled);
	if(!announce)
		return;
    std::ostringstream text;
	text<<"Maxima strategy: format="
		<<StrategyResolver::formatName(resolved.format)
		<<" player="<<context.playerNumber()<<" team="<<context.teamNumber()<<" sources=";
	for(size_t source=0; source<resolved.sources.size(); ++source)
	{
		if(source) text<<",";
		text<<resolved.sources[source];
	}
	text<<" values="<<StrategyResolver::canonicalValues(resolved.values)<<'\n';
    bufferedDiagnostics.push_back({{},{},text.str()});
}


Maxima::Maxima(Player *player)
	: context(player)
{

	strategy_resolved=false;
    {
        auto ownerObservation=context.scopeOwnerObservation();
        resolve_strategy(false);
    }
	timer=0;
	posture=PostureExpand;
	posture_since=0;
	last_food_retirement_tick=-1000000;
	relocation_since.clear();
	relocation_target_building=-1;
	relocation_target_since=-1;
	relocation_completed_tick=-1;
	last_food_relocation_tick=-1000000;
	relocation_destroy_issued.clear();
	food_supported_inns=0;
	food_supported_swarms=0;
	food_ledger_valid=false;
	director=StrategyDirector();
	last_director_telemetry_tick=-1000000;
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
	known_algae_units=0;
	walk_accessible_algae_units=0;
	swim_accessible_algae_units=0;
	accessible_algae_units=0;
	for(int n=0; n<PostureCount; ++n)
		posture_utilities[n]=0;
	director_initialized=false;
	development_planner_initialized=false;
	development_reported_states.clear();
	food_burden_since.clear();
	food_retirement_issued.clear();
	last_food_retirement_tick=-1000000;
	relocation_since.clear();
	relocation_target_building=-1;
	relocation_target_since=-1;
	relocation_completed_tick=-1;
	last_food_relocation_tick=-1000000;
	relocation_destroy_issued.clear();
	food_supported_inns=0;
	food_supported_swarms=0;
	food_ledger_valid=false;
	recent_construction_failures=0;
	last_construction_failure_tick=-1000000;
	opening_space_constrained=false;
	proactive_clearing_flag=-1;
	proactive_clearing_started_tick=-1000000;
	proactive_clearing_initial_wood=0;
	proactive_clearing_campaigns=0;
	last_proactive_clearing_tick=-1000000;
	target=-1;
	attack_flags.clear();
	is_digging_out=false;
	preemptive_guard_tiles.clear();
	last_preemptive_defense_tick=-1000000;
	preemptive_building_signature=0;
	last_preemptive_effective_zone_max=-1;
	last_preemptive_amphibious_active=false;
	applied_maintenance_clearing_mask.clear();
	maintenance_circulation_mask.clear();
	wood_firebreak_mask.clear();
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
	last_recon_mission_tick=-1000000;
	reconnaissance_suspended=false;
	cleared_enemy_sites.clear();
	operating_colonies.clear();
	last_colony_completed_tick=-1000000;
	last_colony_accounted_action_id=0;
	established_colonies=0;
	colony_gate_reason="not evaluated";
    context.releaseObservation();
}

Maxima::Maxima(GAGCore::InputStream *stream, Player *player,
	Sint32 versionMinor)
	: Maxima(player)
{
	const bool loaded=load(stream, player, versionMinor);
	if (!loaded) throw std::runtime_error("Invalid saved Maxima AI");
}

void Maxima::tick(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	ensure_strategy();
	timer++;
	const int team=runtime.teamNumber();
	// A new game and every loaded save start with no executable plan. Likewise,
	// completion events invalidate director assumptions. Replan before any
	// scheduled executor observes the plan.
	if(!director.initialized || director.dirty)
	{
		director.evaluate(*this, runtime);
		update_reconnaissance_missions(runtime);
	}
	if(timer==1)
	{
		initialize(runtime);
		emit_telemetry(runtime, "strategy_loaded", "\tsettings="
			+StrategyResolver::canonicalValues(strategy));
	}
	if(strategy.reconnaissance.enabled && timer>1
	   && timer%strategy.reconnaissance.force_sample_interval_ticks
		==staggered_phase(
			strategy.reconnaissance.force_sample_phase_offset_ticks,
			strategy.reconnaissance.force_sample_interval_ticks,team))
		sample_reconnaissance_forces(runtime);
	if(timer%strategy.scheduling.strategy_interval_ticks
		==staggered_phase(strategy.scheduling.strategy_phase_offset_ticks,
			strategy.scheduling.strategy_interval_ticks,team))
	{
		check_phases(runtime);
		update_reconnaissance_missions(runtime);
	}
	if(timer%strategy.scheduling.building_interval_ticks
		==staggered_phase(strategy.scheduling.building_phase_offset_ticks,
			strategy.scheduling.building_interval_ticks,team))
	{
		manage_buildings(runtime);
	}
	if(budget.tactical_review_interval>0
	   && timer%budget.tactical_review_interval
		==staggered_phase(0,budget.tactical_review_interval,team))
	{
		control_offense(runtime);
	}
	const int defenseInterval=strategy.scheduling.defense_interval_ticks;
	const int defensePhase=staggered_phase(
		strategy.scheduling.defense_phase_offset_ticks,defenseInterval,team);
	if(timer%defenseInterval==defensePhase)
		preemptive_defense_pending=true;
	// Reactive flag scoring used to follow the topology pass immediately. Half
	// an interval preserves its cadence while guaranteeing a different update
	// whenever the configured interval can represent two distinct phases.
	if(timer%defenseInterval
		==staggered_phase(strategy.scheduling.defense_phase_offset_ticks
			+defenseInterval/2,defenseInterval,team))
		reactive_defense_pending=true;
	const int farming_interval=budget.farming_urgent
		? budget.farming_urgent_interval : budget.farming_normal_interval;
	const bool firstFarmingCycle=last_farming_tick<=-500000;
	const bool farmingDue=firstFarmingCycle
		?timer>=2+team*4:timer-last_farming_tick>=farming_interval;
	if(timer%strategy.scheduling.fruit_interval_ticks
		==staggered_phase(strategy.scheduling.fruit_phase_offset_ticks,
			strategy.scheduling.fruit_interval_ticks,team))
	{
		update_fruit_flags(runtime);
	}
	if(timer%strategy.scheduling.explorer_attack_interval_ticks
		==staggered_phase(strategy.scheduling.explorer_attack_phase_offset_ticks,
			strategy.scheduling.explorer_attack_interval_ticks,team))
	{
		compute_explorer_flag_attack_positioning(runtime);
	}

	// Placement plans operate on strategic state and construction lifecycles, not
	// unit-frame movement. The configured cadence bounds response latency while
	// avoiding a complete placement-world rebuild at frame rate.
	if((!development_planner_initialized&&timer>=5+team*4)
		||timer%strategy.scheduling.placement_interval_ticks
			==staggered_phase(0,
				strategy.scheduling.placement_interval_ticks,team))
		development_cycle_pending=true;

	// Execute at most one map-scale maintenance/planning job per logical update.
	// This is deliberately a deterministic priority queue rather than a wall-time
	// budget: replay results do not depend on machine speed, and a due job remains
	// pending until it receives its turn.
	if(land_clearing_pending)
	{
		land_clearing_pending=false;
		manage_land_clearing(runtime);
	}
	else if(maintenance_clearing_pending)
	{
		maintenance_clearing_pending=false;
		update_maintenance_clearing_areas(runtime);
	}
	else if(preemptive_defense_pending)
	{
		preemptive_defense_pending=false;
		update_preemptive_defense(runtime);
	}
	else if(reactive_defense_pending)
	{
		reactive_defense_pending=false;
		compute_defense_flag_positioning(runtime);
	}
	else if(farmingDue)
	{
		update_farming(runtime);
		last_farming_tick=timer;
		land_clearing_pending=true;
		maintenance_clearing_pending=true;
	}
	else if(development_cycle_pending)
	{
		development_cycle_pending=false;
		const uint32_t previousSpatialRevision=
			development_planner.spatialRevision();
		development_cycle(runtime);
		// Reservations are the placement state that changes clearing contracts.
		// Refresh immediately for those changes; otherwise the farming cadence is
		// sufficient and avoids another complete map pass after every review.
		maintenance_clearing_pending=maintenance_clearing_pending
			||previousSpatialRevision!=development_planner.spatialRevision();
	}
}


void Maxima::handle_event(Context& runtime, const RuntimeEvent& event)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	telemetry.count(AITrace::AI7::Maxima_handle_event_calls);
	if(event.type>=RuntimeEvent::BuildingResolved && event.type<=RuntimeEvent::DevelopmentEngineRejected)
		// Both sides are enumerations, and adding them directly is deprecated in
		// C++20. The intent is an offset into the per-event counter block, so the
		// index is computed as the integer it always was.
		telemetry.count(AITrace::AI7::Field(
			int(AITrace::AI7::runtime_event_BuildingResolved)+int(event.type)));
	telemetry.set(AITrace::AI7::runtime_event_first,event.first);
	telemetry.set(AITrace::AI7::runtime_event_second,event.second);
	if(event.type==RuntimeEvent::BuildingResolved
	   || event.type==RuntimeEvent::BuildingUpdated
	   || event.type==RuntimeEvent::AttackFinished
	   || event.type==RuntimeEvent::DigOutFinished)
		director.invalidate();
	if(event.type == RuntimeEvent::UpdateSwarm)
	{
		// Completing a swarm changes every share of the colony-wide budget.
		// Reconcile the existing producers as well as the completed building.
		BuildingSearch swarms(runtime);
		swarms.add_condition(new SpecificBuildingType(AIMaximaBuildings::Production));
		swarms.add_condition(new NotUnderConstruction);
		for(building_search_iterator swarm=swarms.begin(); swarm!=swarms.end(); ++swarm)
			manage_swarm(runtime, *swarm);
	}
	if(event.type == RuntimeEvent::UpdateInn)
	{
		manage_inn(runtime, event.first);
	}
	if(event.type == RuntimeEvent::AttackFinished)
	{
		const int id=event.first;
		const bool tactical_flag=tactical_mission.flagId==id;
		std::vector<int>::iterator flag=std::find(attack_flags.begin(), attack_flags.end(), id);
		if(flag!=attack_flags.end())
			attack_flags.erase(flag);
		std::string reason="flag_removed";
		std::map<int, std::string>::const_iterator recorded=attack_flag_end_reasons.find(id);
		if(recorded!=attack_flag_end_reasons.end())
			reason=recorded->second;
		std::map<int, int>::const_iterator target_it=attack_flag_targets.find(id);
		int finished_target=-1;
		int finished_team=-1;
		if(target_it!=attack_flag_targets.end())
		{
			const int gid=target_it->second;
			finished_target=gid;
			const int team=Building::GIDtoTeam(gid);
			finished_team=team;
			const int local=Building::GIDtoID(gid);
			if(team>=0 && team<Team::MAX_COUNT && std::size_t(team)<runtime.observation().teams.size()
			   && local>=0 && local<Building::MAX_COUNT
			   && runtime.observation().buildingSlots(team)[local]==NULL)
			{
				reason="target_destroyed";
				campaign.buildings_destroyed+=1;
				campaign.last_progress_tick=timer;
			}
		}
		int duration=-1;
		std::map<int, int>::const_iterator started=attack_flag_started_ticks.find(id);
		if(started!=attack_flag_started_ticks.end())
			duration=timer-started->second;
		emit_telemetry(runtime, "attack_finished",
			"\tflag="+telemetryText(id)
			+"\ttarget_building="+telemetryText(finished_target)
			+"\ttarget_team="+telemetryText(finished_team)
			+"\treason="+reason
			+"\tduration="+telemetryText(duration)
			+"\tcampaign_destroyed="
				+telemetryText(campaign.buildings_destroyed));
		attack_flag_targets.erase(id);
		attack_flag_started_ticks.erase(id);
		attack_flag_end_reasons.erase(id);
		offense_waves.erase(std::remove_if(offense_waves.begin(),offense_waves.end(),
			[id](const Tactics::Wave& wave){return wave.flagId==id;}),offense_waves.end());
		if(tactical_flag && !offense_waves.empty())
			tactical_mission.flagId=offense_waves.front().flagId;
		if(tactical_flag && offense_waves.empty())
		{
			if(!attack_flags.empty())
			{
				tactical_mission.flagId=attack_flags.front();
				if(tactical_mission.targetGid>=0)
					attack_flag_targets[tactical_mission.flagId]=tactical_mission.targetGid;
			}
			else
			{
				tactical_mission.reset();
				campaign.state=CampaignIdle;
			}
		}
	}
	if(event.type == RuntimeEvent::GuardFlagDeleted)
	{
		const int id=event.first;
		std::vector<int>::iterator flag=
			std::find(defense_flags.begin(), defense_flags.end(), id);
		if(flag!=defense_flags.end())
			defense_flags.erase(flag);
	}
	if(event.type == RuntimeEvent::ExplorerAttackFlagDeleted)
	{
		const int id=event.first;
		std::vector<int>::iterator flag=
			std::find(explorer_attack_flags.begin(), explorer_attack_flags.end(), id);
		if(flag!=explorer_attack_flags.end())
			explorer_attack_flags.erase(flag);
	}
	if(event.type == RuntimeEvent::DigOutFinished)
	{
		is_digging_out=false;
	}
	if(event.type == RuntimeEvent::ReconFlagDeleted)
	{
		reconnaissance.removeMission(event.first);
	}
}



void Maxima::initialize(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	initialize_farming_cache(runtime);
	BuildingSearch bs(runtime);
	for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
	{
	}

	manage_buildings(runtime);
}


void Maxima::check_phases(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	director.evaluate(*this, runtime);
}


const std::vector<AIMaximaPlacement::BuildingProfile>&
Maxima::collect_building_profiles() const
{
 using namespace AIMaximaPlacement;using namespace AIMaximaBuildings;
 if(development_profiles_initialized)return development_building_profiles;
 const auto& game=context.observation();
 development_profile_index.assign(game.catalog->size(),-1);
 development_feeding_visit_rate.assign(game.catalog->size(),0);
 development_feeding_pause.fill(INT_MAX);
 std::vector<FeedingEstimate> operations(game.catalog->size());
 for(size_t id=0;id<game.catalog->size();++id) {
  // Recipe/seat ceilings describe potential work. A separate shared hauling
  // plan predicts usable feeding throughput; staffing feedback owns actual
  // carrier requests and placement prices the local route independently.
  operations[id]=operatingEstimate(*(&game.catalog->at(id).resolvedType),strategy,*game.configuration,false);
  development_feeding_visit_rate[id]=int(std::min<long long>(INT_MAX,
      operatingEstimate(*(&game.catalog->at(id).resolvedType),strategy,*game.configuration,true).visitsPerTick));
 }
 development_profiles_initialized=true;
 for(size_t root=0;root<game.catalog->size();++root) {
  const auto* first=(&game.catalog->at(root).resolvedType);
  if(!first->semantics.placeable)continue;
  BuildingProfile profile;profile.buildingType=int(root);int current=int(root),position=0;
  for(size_t visited=0;visited<game.catalog->size()&&current>=0;++visited) {
   const auto* placement=(&game.catalog->at(current).resolvedType);
   const auto* complete=completed(game,*placement);if(!complete)break;
   const int completeID=placement->isBuildingSite?placement->nextLevel:current;
   BuildingLevelProfile v;v.level=++position;v.engineType=current;v.completedType=completeID;
   v.roles=capabilities(game,*complete);v.seats=complete->maxUnitInside;
   v.assignmentLimit=placement->semantics.assignmentLimit;v.requiredWorkerLevel=placement->semantics.requiredWorkerLevel;
   v.repairable=complete->semantics.repairable;v.available=placement->runtimeAvailable&&complete->runtimeAvailable;
   const int left=std::min(complete->decLeft,placement->decLeft),top=std::min(complete->decTop,placement->decTop);
   v.footprint=Footprint(left,top,std::max(complete->decLeft+complete->width,placement->decLeft+placement->width)-left,std::max(complete->decTop+complete->height,placement->decTop+placement->height)-top);
   if(placement->isBuildingSite)for(int r=0;r<8;++r)v.constructionResources[r]=placement->semantics.constructionCost[r];
   const auto& semantic=complete->semantics;
   v.foodRetirable=std::none_of(semantic.training.begin(),semantic.training.end(),[](const auto& t){return t.enabled;})
       && !(complete->shootingRange>0 && complete->shootRhythm>0
           && (semantic.projectileBuildingDamage>0 || std::any_of(semantic.projectileDamage.begin(),semantic.projectileDamage.end(),[](int n){return n>0;})));
   v.operatingAssignmentLimit=complete->semantics.assignmentLimit;
   v.initialCarriers=initialCarrierRequest(*complete,strategy);
   v.productionDemandPercent=strategy.food.swarm_demand_percent;
   for(int unit=0;unit<3;++unit)if(semantic.production.recipes[unit].enabled &&
       !(unit==WARRIOR && game.configuration->isPeacefulModeEnabled())) {
       v.productionRecipes.ticks[unit]=semantic.production.recipes[unit].duration+1;
       std::copy_n(semantic.production.recipes[unit].cost.begin(),8,v.productionRecipes.costs[unit]);
   }
   std::copy_n(complete->multiplierResource,8,v.productionRecipes.packetSize);
   const auto& operation=operations[completeID];
   std::copy(operation.resourcePackets.begin(),operation.resourcePackets.end(),v.operatingResources);
   v.feedingRate=int(std::min<long long>(INT_MAX,operation.visitsPerTick));
   v.feedingMask=semantic.feeding.unitMask&semantic.admittedUnitMask;
   std::copy(operation.feedingResourcePackets.begin(),operation.feedingResourcePackets.end(),v.feedingResources);
   const int nominalRatios[3]={1,1,1};
   const auto production=AIMaxima::productionPacketCeiling(v.productionRecipes,nominalRatios);
   std::copy(production.begin(),production.end(),v.productionResources);
   std::copy(operation.independentResourcePackets.begin(),operation.independentResourcePackets.end(),v.independentResources);
   for(int r=0;r<8;++r)v.operatingResources[r]=int(std::min<long long>(INT_MAX,
       static_cast<long long>(v.independentResources[r])+v.feedingResources[r]+v.productionResources[r]));
   const int otherWheat=v.independentResources[WHEAT];
   v.feedingResources[WHEAT]=int(std::min<long long>(INT_MAX,static_cast<long long>(v.feedingResources[WHEAT])*strategy.food.inn_demand_percent/100));
   v.productionResources[WHEAT]=int(std::min<long long>(INT_MAX,static_cast<long long>(v.productionResources[WHEAT])*strategy.food.swarm_demand_percent/100));
   v.operatingResources[WHEAT]=int(std::min<long long>(INT_MAX,static_cast<long long>(otherWheat)+v.feedingResources[WHEAT]+v.productionResources[WHEAT]));
   if(position==1 && v.available && semantic.feeding.enabled && complete->maxUnitInside>0)
       for(int unit=0;unit<3;++unit)if(v.feedingMask&(1u<<unit))
           development_feeding_pause[unit]=std::min(development_feeding_pause[unit],serviceTicks(*complete,semantic.feeding.duration));
   std::copy(operation.services.begin(),operation.services.end(),v.serviceRates);
   for(int unit=0;unit<3;++unit)if(semantic.production.recipes[unit].enabled) {
    v.productionUnitMask|=1u<<unit;
    v.productionRates[unit]=operation.productionRates[unit];
   }
   if(v.roles&roleBit(ProjectileDefense)) {
    const long long fullUtility=complete->shootingRange+static_cast<long long>(semantic.projectileDamage[WARRIOR])*std::max(1,complete->shootRhythm)/32;
    const long long fullRate=FeedingEstimate::Scale*complete->shootRhythm/65536;
    v.serviceRates[ProjectileDefense]=int(std::min<long long>(1000000,fullUtility*operation.projectileRate/std::max(1LL,fullRate)));
   }
   int sharedSeats=0;for(int role=Feeding;role<=ConstructionTraining;++role)sharedSeats=std::max(sharedSeats,v.serviceRates[role]);
   v.serviceThroughput=sharedSeats+v.serviceRates[Production]+v.serviceRates[ProjectileDefense];
   v.durability=complete->hpMax*game.configuration->getBuildingHpMultiplier();v.capability=complete->prestige;
   for(int role=0;role<RoleCount;++role)v.capability+=bool(v.roles&roleBit(role));
   profile.levels.push_back(v);current=complete->nextLevel;
  }
  if(!profile.levels.empty()) {development_profile_index[root]=int(development_building_profiles.size());development_building_profiles.push_back(profile);}
 }
 for(auto& pause:development_feeding_pause)if(pause==INT_MAX)pause=0;
 return development_building_profiles;
}

const AIMaximaPlacement::BuildingLevelProfile* Maxima::profile_variant(int root,int position) const
{
 collect_building_profiles();
 if(root<0 || size_t(root)>=development_profile_index.size())return nullptr;
 const int index=development_profile_index[root];
 return index<0 ? nullptr : development_building_profiles[index].atLevel(position);
}
bool Maxima::profile_serves(int root,int role,int position) const
{
 const auto* v=profile_variant(root,position);return v&&(v->roles&AIMaximaBuildings::roleBit(role));
}
int Maxima::feeding_capacity(int root,int position) const
{
 const auto* v=profile_variant(root,position);if(!v||!(v->roles&AIMaximaBuildings::roleBit(AIMaximaBuildings::Feeding)))return 0;
 const auto* b=&context.observation().catalog->at(v->completedType).resolvedType;if(!b)return 0;
 return feeding_capacity_for_type(v->completedType);
}

long long Maxima::recipient_meal_rate(const AIEngine::UnitView& unit) const
{
 if(context.observation().configuration->isHungerDisabled())return 0;
 const int action=unit.performance[FLY]>0?FLY:WALK;
 return recipientMealRate(Unit::HUNGRY_MAX,unit.trigHungry,unit.hungriness,
     unit.performance[action],action,strategy.farming.management_radius,development_feeding_pause[unit.typeNum]);
}

int Maxima::feeding_capacity_for_type(int type) const
{
 collect_building_profiles();
 const auto* definition=&context.observation().catalog->at(type).resolvedType;
 if(!definition || size_t(type)>=development_feeding_visit_rate.size())return 0;
 const unsigned admitted=definition->semantics.feeding.unitMask&definition->semantics.admittedUnitMask;
 const int populations[3]={snapshot.workers,snapshot.explorers,snapshot.warriors};
 long long demand=0,population=0;
 for(int unit=0;unit<3;++unit)if(admitted&(1u<<unit)) {
     population+=populations[unit];demand+=snapshot.feeding_demand[unit];
 }
 if(!demand) {
     population=0;
     // Future recipients use the loaded game's race, including historical
     // hunger and movement values; an empty colony is not a zero-cost meal.
     for(int unit=0;unit<3;++unit)if(admitted&(1u<<unit)) {
         const auto* base=&context.observation().unitType(unit,0);
         const int action=base->performance[FLY]>0?FLY:WALK;
         const int trigger=base->performance[ATTACK_SPEED]>0?Unit::HUNGRY_MAX*UNIT_HUNGRY_TRIG_NUM_WARRIOR/UNIT_HUNGRY_TRIG_DEN:Unit::HUNGRY_MAX/UNIT_HUNGRY_TRIG_DIVISOR_DEFAULT;
         const long long rate=recipientMealRate(Unit::HUNGRY_MAX,trigger,Race::hungriness,
             base->performance[action],action,strategy.farming.management_radius,development_feeding_pause[unit]);
         const int count=std::max(1,populations[unit]);
         demand+=rate*count;population+=count;
     }
     demand=(demand+MealRatePrecision/2)/MealRatePrecision;
 }
 if(!demand || !population)return 0;
 return int(std::min<long long>(1000000,static_cast<long long>(development_feeding_visit_rate[type])*population/demand));
}

int Maxima::aggregate_feeding_capacity(const std::array<long long,8>& rates) const
{
 std::array<int,3> demand{snapshot.feeding_demand[0],snapshot.feeding_demand[1],snapshot.feeding_demand[2]};
 if(demand==std::array<int,3>{} && !context.observation().configuration->isHungerDisabled()) {
     // Empty or manually constructed planning observations use the loaded race;
     // live observations already contain actual per-recipient recurring rates.
     std::array<int,3> population{snapshot.workers,snapshot.explorers,snapshot.warriors};
     if(population==std::array<int,3>{})population[WORKER]=snapshot.population;
     for(int unit=0;unit<3;++unit)if(population[unit]) {
         const auto* base=&context.observation().unitType(unit,0);
         const int action=base->performance[FLY]>0?FLY:WALK;
         const int trigger=base->performance[ATTACK_SPEED]>0?Unit::HUNGRY_MAX*UNIT_HUNGRY_TRIG_NUM_WARRIOR/UNIT_HUNGRY_TRIG_DEN:Unit::HUNGRY_MAX/UNIT_HUNGRY_TRIG_DIVISOR_DEFAULT;
         const long long rate=recipientMealRate(Unit::HUNGRY_MAX,trigger,Race::hungriness,
             base->performance[action],action,strategy.farming.management_radius,development_feeding_pause[unit]);
         demand[unit]=int(std::min<long long>(INT_MAX,(rate*population[unit]+MealRatePrecision/2)/MealRatePrecision));
     }
 }
 return feedingPopulationCapacity(demand,snapshot.population,rates);
}

long long Maxima::birth_food_acreage() const
{
 const long long crop=static_cast<long long>(std::max(0,food_birth_crop_rate))*strategy.food.growth_period_ticks;
 return (crop/AIMaximaFoodLedger::RateScale)*65536
     +(crop%AIMaximaFoodLedger::RateScale)*65536/AIMaximaFoodLedger::RateScale;
}

int Maxima::preferred_profile(int role) const
{
 const auto c=AIMaximaBuildings::choose(context.observation(),context.observedTeam(),role);
 return c.placementType;
}


void Maxima::configure_development_planner()
{
	using namespace AIMaximaPlacement;
	development_planner.configure(collect_building_profiles(),
		AIMaximaBuildings::Feeding, AIMaximaBuildings::Healing,
		AIMaximaBuildings::ConstructionTraining, AIMaximaBuildings::CombatTraining,
		AIMaximaBuildings::ProjectileDefense, AIMaximaBuildings::Production);
	PlacementPolicy& policy=development_planner.mutablePolicy();
	policy.unmetDemandWeight=strategy.placement.unmet_demand_weight;
	policy.serviceGainWeight=strategy.placement.service_gain_weight;
	policy.capabilityGainWeight=strategy.placement.capability_gain_weight;
	policy.parallelismGainWeight=strategy.placement.parallelism_gain_weight;
	policy.redundancyGainWeight=strategy.placement.redundancy_gain_weight;
	policy.roleLocationQualityWeight=strategy.placement.role_location_weight;
	policy.defendednessWeight=strategy.placement.defensive_siting_enabled
		? strategy.placement.defendedness_weight : 0;
	policy.compactnessWeight=strategy.placement.spacing_compactness_enabled
		? strategy.placement.compactness_weight : 0;
	policy.spacingTargetTiles=strategy.placement.spacing_target_tiles;
	policy.spacingWeight=strategy.placement.spacing_compactness_enabled
		? strategy.placement.spacing_weight : 0;
	policy.projectedFarmLossWeight=strategy.placement.food_preservation_enabled
		? strategy.placement.farm_loss_weight : 0;
	policy.foodZonePenaltyWeight=strategy.placement.food_preservation_enabled
		? strategy.placement.food_zone_penalty_weight : 0;
	policy.newlyReservedLandWeight=strategy.placement.reserved_land_weight;
	policy.resourceScarcityWeight=strategy.placement.resource_scarcity_weight;
	policy.constructionLaborWeight=strategy.placement.construction_labor_weight;
	policy.serviceDowntimeWeight=strategy.placement.service_downtime_weight;
	policy.threatExposureWeight=strategy.placement.defensive_siting_enabled
		? strategy.placement.threat_exposure_weight : 0;
	policy.newArteryLengthWeight=strategy.placement.artery_routing_enabled
		? strategy.placement.artery_length_weight : 0;
	policy.arteryRoutingEnabled=strategy.placement.artery_routing_enabled;
	policy.upgradeLevel1Workers=strategy.upgrades.level1_workers;
	policy.upgradeLevel2Workers=strategy.upgrades.level2_workers;
	policy.unmetCountWeight=strategy.placement.unmet_count_weight;
	policy.unmetCountCap=strategy.placement.unmet_count_cap;
	policy.upgradeUnmetDemand=strategy.placement.upgrade_unmet_demand;
	policy.serviceGainScale=strategy.placement.service_gain_scale;
	policy.capabilityGainScale=strategy.placement.capability_gain_scale;
	policy.parallelServiceBase=strategy.placement.parallel_service_base;
	policy.parallelBuildBonus=strategy.placement.parallel_build_bonus;
	policy.parallelNoService=strategy.placement.parallel_no_service;
	policy.duplicateFirstScore=strategy.placement.duplicate_first_score;
	policy.duplicateScoreScale=strategy.placement.duplicate_score_scale;
	policy.resourceDistanceWeight=strategy.placement.resource_distance_weight;
	policy.foodZoneRadius=strategy.placement.food_zone_radius;
	policy.innerFoodZoneMultiplier=strategy.placement.inner_food_zone_multiplier;
	policy.hospitalFoodZoneMultiplier=strategy.placement.hospital_food_zone_multiplier;
	policy.towerFoodZoneMultiplier=strategy.placement.tower_food_zone_multiplier;
	policy.towerCriticalDistanceWeight=strategy.placement.defensive_siting_enabled
		? strategy.placement.tower_critical_distance_weight : 0;
	policy.towerSpacingTarget=strategy.placement.tower_spacing_target;
	policy.towerSpacingWeight=strategy.placement.spacing_compactness_enabled
		? strategy.placement.tower_spacing_weight : 0;
	policy.towerThreatTarget=strategy.placement.tower_threat_target;
	policy.towerThreatWeight=strategy.placement.defensive_siting_enabled
		? strategy.placement.tower_threat_weight : 0;
	policy.laborScale=strategy.placement.labor_scale;
	policy.downtimeWorkerScale=strategy.placement.downtime_worker_scale;
	policy.arteryLengthScale=strategy.placement.artery_length_scale;
	policy.repairBaseDemand=strategy.placement.repair_base_demand;
	policy.actionTimeoutTicks=strategy.placement.action_timeout_ticks;
	policy.routeClearableResourceCost=
		strategy.placement.route_clearable_resource_cost;
	policy.routeFarmCost=strategy.placement.route_farm_cost;
	policy.routeFertilityCost=strategy.placement.route_fertility_cost;
	policy.colonyMinimumAnchorDistance=
		strategy.colonization.minimum_anchor_distance;
	policy.colonySupplyRadius=strategy.staffing.swarm_supply_radius;
	policy.colonyMinimumFood=strategy.colonization.minimum_new_food;
	policy.colonyMinimumValue=strategy.colonization.minimum_value;
	policy.colonyMaximumThreat=strategy.colonization.maximum_threat;
	policy.foodLedgerEnabled=strategy.food.enabled;
	policy.foodSupplyRadius=strategy.staffing.swarm_supply_radius;
	policy.foodMarginPercent=strategy.food.placement_margin_percent;
	policy.foodQualityBandTiles=strategy.food.quality_band_tiles;
	policy.foodUnreachablePenaltyTiles=strategy.food.unreachable_penalty_tiles;
	policy.relocationEnabled=strategy.food.relocation_enabled;
	policy.relocationMinGainTiles=strategy.food.relocation_min_gain_tiles;
	policy.relocationMinCoverageGainPercent=
		strategy.food.relocation_min_coverage_gain_percent;
	policy.relocationPaybackHorizonTicks=
		strategy.food.relocation_payback_horizon_ticks;
	policy.relocationCostMarginPercent=strategy.food.relocation_cost_margin_percent;
	policy.carrierTicksPerTile=strategy.food.carrier_ticks_per_tile;
	policy.carrierFixedTicksPerTrip=strategy.food.carrier_fixed_ticks_per_trip;
	policy.builderTicksPerStep=strategy.food.builder_ticks_per_step;
	policy.relocationDistanceRealisationPercent=
		strategy.food.relocation_distance_realisation_percent;
	// Demand comes from the engine's own building rates rather than a tuned
	// constant: a swarm's wheat per produced unit, and an inn's modelled
	// population times the rate at which a fed unit eats.
	const auto* swarmProfile=profile_variant(preferred_profile(AIMaximaBuildings::Production));
	policy.foodSwarmDemand=swarmProfile?swarmProfile->productionResources[WHEAT]:0;
    // Historical tier arrays remain serialized by old formats only. Live
    // demand is the same per-variant operating plan used for feeding capacity.
    std::fill(std::begin(policy.foodInnDemand),std::end(policy.foodInnDemand),0);
}


AIMaximaPlacement::WorldState Maxima::collect_development_world(
	Context& runtime, uint32_t* signature) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	using namespace AIMaximaPlacement;
	const AIEngine::AIWorldView* map=&runtime.observation();WorldState world;
	world.reset((*map).width,(*map).height);world.tick=timer;
	Uint32 worldSignature=2166136261u;
	add_preemptive_hash(worldSignature,Uint32(world.width));
	add_preemptive_hash(worldSignature,Uint32(world.height));
	// Final placement validation must see losses since the last strategy tick.
	for(int id=0; id<Unit::MAX_COUNT; ++id)
	{
		const AIEngine::UnitView* worker=runtime.observation().unitSlots(runtime.teamNumber())[id];
		if(worker && worker->typeNum==WORKER && worker->performance[SWIM]>0)
			++world.swimmingBuilders;
	}
	world.accessibleSupplies[WOOD]=environment.accessible_wood;
	world.accessibleSupplies[WHEAT]=environment.accessible_corn;
	world.accessibleSupplies[STONE]=environment.accessible_stone;
	world.accessibleSupplies[ALGA]=accessible_algae_units;
	world.profiles=collect_building_profiles();
	const bool cachedFertility=fertility_cache.validFor(world.width,world.height);
	const std::vector<uint32_t>& fertilityValues=fertility_cache.values();
	const WoodReserve wood_reserve=select_wood_reserve(runtime);
	for(int y=0;y<world.height;++y)for(int x=0;x<world.width;++x)
	{
		const int index=y*world.width+x;
		WorldTile& tile=world.tiles[index];const auto cell=AIEngine::ObservationQueries::spatialTile((*map),x,y);
		tile.discovered=(cell.discovered&runtime.observedTeam().allies)!=0;
		// Terrain is immutable during a match. Classifying the already-fetched
		// cell avoids three wrapped MapInfo calls per tile on every planner scan.
		const auto& terrain=AIEngine::ObservationQueries::terrain((*map),index);
		tile.swimmable=terrain.swimmable;
		tile.walkable=terrain.walkable;
		tile.fertilitySource=terrainProvidesFertility(terrain);
		tile.growthInhibiting=terrain.inhibitionQ8 != 0;
		tile.buildable=terrain.buildable;tile.occupied=cell.building!=NOGBID;
		tile.woodReserve=wood_reserve.cells[index]!=0;
		tile.foodTraversable=!(cell.forbidden&runtime.observedTeam().mask)
			|| applied_farm_protection_mask[index];
		tile.ownOccupied=tile.occupied
			&&Building::GIDtoTeam(cell.building)==runtime.teamNumber();
		if(cell.resource.type!=NO_RES_TYPE)
		{
			tile.resourceType=cell.resource.type;tile.resourceAmount=cell.resource.amount;
			tile.permanentResource=map->resourceEternal[cell.resource.type];
			tile.clearableResource=!tile.permanentResource;
		}
		const Uint32 flags=(tile.discovered?1u:0u)|(tile.buildable?2u:0u)
			|(tile.swimmable?4u:0u)|(tile.growthInhibiting?8u:0u)
			|(tile.permanentResource?16u:0u)
			|(tile.clearableResource?32u:0u)|(tile.occupied?64u:0u)|(tile.foodTraversable?128u:0u)|(tile.woodReserve?256u:0u)
			|(tile.walkable?512u:0u)|(tile.fertilitySource?1024u:0u);
		add_preemptive_hash(worldSignature,flags);
		add_preemptive_hash(worldSignature,Uint32(tile.resourceType+1));
		// Resource amounts fluctuate on virtually every harvest. Placement routes,
		// legality and blocked intents depend on resource presence, not stack size;
		// keep live amounts for scoring without invalidating topology caches.
		tile.fertility=Uint32(effectiveWheatRegrowth(map,cachedFertility?fertilityValues[index]:cell.fertility));
		int expansionNeighbors=0;
		if((tile.resourceType==WHEAT||tile.resourceType==WOOD)&&tile.resourceAmount>0)
		{
			expansionNeighbors=available_expansion_neighbors(runtime,x,y);
			tile.farmCapacity=Farming::usefulExpansionCapacity(tile.fertility,
				tile.resourceAmount,expansionNeighbors,
				tile.resourceType==WHEAT);
		}
		if(tile.discovered && !tile.occupied
		   && tile.resourceType==WHEAT && tile.resourceAmount>0)
		{
			// A stack is an opportunity even where nothing regrows: a colony
			// seeded beside it lives by mining it (Locust).
			tile.foodOpportunity=tile.fertility
				+uint32_t(wheatStockFertilityEquivalent(tile.resourceAmount,
					strategy.farming.wheat_stock_horizon_ticks));
			tile.farmCapacity=tile.fertility;
		}
		// Standing food supply is protected wheat that currently carries wheat.
		// Unprotected wheat is harvested away rather than kept, so it is not
		// capacity a settlement can plan against. A protected stack feeds the
		// harvestable cells around it, so its growth is only worth what its
		// neighbours can absorb.
		// Wild wheat is counted too, at a discount: it regrows by the same rule
		// but can be harvested to nothing, so it is a stock with a yield rather
		// than a kept farm. Without it a rich map's abundance never turns into
		// buildings, and the colony is sized to its farms instead of its land.
		if(strategy.food.enabled && tile.resourceType==WHEAT
		   && tile.resourceAmount>0 && tile.discovered)
		{
			const bool farm=index<int(wheat_farm_protection_mask.size())
				&& wheat_farm_protection_mask[index];
			const uint32_t cell=AIMaximaFoodLedger::cellYield(tile.fertility,
				growth_absorbing_neighbors(runtime,x,y),
				strategy.food.growth_period_ticks);
			// A stack of wheat is also a stock. Spread over the planning horizon
			// it is a rate like any other, and on infertile ground (Locust) it
			// is the only one: the colony lives by mining it out.
			const uint32_t stock=uint32_t(static_cast<long long>(tile.resourceAmount)
				*AIMaximaFoodLedger::RateScale/strategy.farming.wheat_stock_horizon_ticks);
			tile.protectedYield=(farm ? cell : cell*strategy.farming.wild_wheat_yield_percent/100)
				+(farm ? 0 : stock);
		}
		tile.protectedness=(map->areasAt(map->tileIndex(x,y)).guard&runtime.observedTeam().mask)!=0
			? strategy.placement.guard_area_protectedness
			: strategy.placement.baseline_protectedness;
	}

	for(int team=0;team<Team::MAX_COUNT;++team)
	{
		if(std::size_t(team)>=runtime.observation().teams.size()
		   || !(runtime.observedTeam().enemies&runtime.observation().teams[team].mask))continue;
		for(int id=0;id<Building::MAX_COUNT;++id)
		{
			const AIEngine::BuildingView* enemy=runtime.observation().buildingSlots(team)[id];
			if(!enemy||AIEngine::ObservationQueries::buildingType(runtime.observation(),*enemy).isVirtual
			   ||!(enemy->seenByMask&runtime.observedTeam().allies))continue;
			const int radius=strategy.placement.enemy_threat_radius;
			for(int dy=-radius;dy<=radius;++dy)
				for(int dx=-(radius-std::abs(dy));
					dx<=radius-std::abs(dy);++dx)
				{
					const int distance=std::abs(dx)+std::abs(dy);
					WorldTile& tile=world.tile(enemy->posX+dx,enemy->posY+dy);
					tile.threat=std::max(tile.threat,
						strategy.placement.enemy_threat_base
							-distance*strategy.placement.enemy_threat_falloff);
				}
		}
	}
	const std::map<int,BuildingRecord>& found=runtime.get_building_register().found();
	for(std::map<int,BuildingRecord>::const_iterator i=found.begin();i!=found.end();++i)
	{
		const AIEngine::BuildingView* building=runtime.get_building_register().get_building(i->first);
		if(!building||AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).isVirtual)continue;
		WorldBuilding value;value.id=i->first;value.gid=building->gid;
		value.buildingType=AIMaximaBuildings::lineageRoot(runtime.observation(),building->typeNum);
		value.level=AIMaximaBuildings::lineagePosition(runtime.observation(),building->typeNum);
		// A placeable intermediate variant can start its own development chain.
		// Keep the planner's chosen root when observing that project's building.
		for(const auto& [actionId,a]:development_planner.actions())if(a.buildingId==value.id)
		 for(const auto& p:world.profiles)if(p.buildingType==a.buildingType)
		  for(const auto& v:p.levels)if(v.completedType==building->typeNum||v.engineType==building->typeNum){value.buildingType=p.buildingType;value.level=v.level;}
		value.centerX=(*map).normalizeX(building->posX-AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).decLeft);
		value.centerY=(*map).normalizeY(building->posY-AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).decTop);
		value.hp=building->hp;value.hpMax=building->maxHp;
		value.age=i->second.age;value.site=AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).isBuildingSite;
		value.upgrading=runtime.get_building_register().is_building_upgrading(i->first);
        const auto* operating=profile_variant(value.buildingType,value.level);
        value.plannedCarriers=operating?operating->initialCarriers:0;
        if(!value.site) {
            const auto request=staffing_control.find(value.id);
            if(request!=staffing_control.end())value.plannedCarriers=request->second.request;
            const auto allowed=swarm_allowance.find(value.id);
            if(allowed!=swarm_allowance.end())value.plannedCarriers=std::min(value.plannedCarriers,allowed->second);
            value.plannedCarriers=std::clamp(value.plannedCarriers,0,
                std::min(AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).semantics.assignmentLimit,strategy.staffing.control_maximum_workers));
            std::copy_n(building->ratio,3,value.productionRatios);
        }
		world.buildings.push_back(value);
		const int radius=strategy.placement.building_protection_radius;
		for(int dy=-radius;dy<=radius;++dy)
			for(int dx=-(radius-std::abs(dy));
				dx<=radius-std::abs(dy);++dx)
			{
				const int distance=std::abs(dx)+std::abs(dy);
				WorldTile& tile=world.tile(value.centerX+dx,value.centerY+dy);
				tile.protectedness=std::max(tile.protectedness,
					strategy.placement.building_protection_base
						-distance*strategy.placement.building_protection_falloff);
			}
	}
	std::sort(world.buildings.begin(),world.buildings.end(),
		[](const WorldBuilding& a,const WorldBuilding& b){return a.id<b.id;});
	for(size_t i=0;i<world.buildings.size();++i)
	{
		const WorldBuilding& building=world.buildings[i];
		add_preemptive_hash(worldSignature,Uint32(building.id));
		add_preemptive_hash(worldSignature,Uint32(building.buildingType));
        add_preemptive_hash(worldSignature,Uint32(building.plannedCarriers));
        for(int ratio:building.productionRatios)add_preemptive_hash(worldSignature,Uint32(ratio));
		add_preemptive_hash(worldSignature,Uint32(world.index(
			building.centerX,building.centerY)));
	}
    // Worker-production centers with overlapping supply catchments form one
    // strategic colony. Recipients
    // belong to one nearest colony, independent of how many feeders it owns.
    std::vector<std::pair<int,int>> anchors;
    for(const auto& b:world.buildings) {
        const auto* p=world.profile(b.buildingType);const auto* v=p?p->atLevel(b.level):nullptr;
        if(!b.site && v && (v->productionUnitMask&(1u<<WORKER)))anchors.emplace_back(b.centerX,b.centerY);
    }
    std::vector<int> parents(anchors.size());
    for(size_t i=0;i<parents.size();++i)parents[i]=int(i);
    const auto root=[&](int i) {while(parents[i]!=i){parents[i]=parents[parents[i]];i=parents[i];}return i;};
    for(size_t i=0;i<anchors.size();++i)for(size_t j=0;j<i;++j)
        if(world.wrappedManhattan(anchors[i].first,anchors[i].second,anchors[j].first,anchors[j].second)<=2*strategy.staffing.swarm_supply_radius) {
            const int a=root(int(i)),b=root(int(j));parents[std::max(a,b)]=std::min(a,b);
        }
    for(size_t i=0;i<anchors.size();++i)if(root(int(i))==int(i))
        world.feedingColonies.push_back({anchors[i].first,anchors[i].second,{}});
    if(world.feedingColonies.empty()) {
        const int x=world.buildings.empty()?0:world.buildings.front().centerX;
        const int y=world.buildings.empty()?0:world.buildings.front().centerY;
        world.feedingColonies.push_back({x,y,{}});
    }
    std::vector<std::array<long long,3>> colonyMeals(world.feedingColonies.size());
    for(int id=0;id<Unit::MAX_COUNT;++id)if(const auto* u=runtime.observation().unitSlots(runtime.teamNumber())[id];u && !u->isDead)
        colonyMeals[world.feedingColonyAt(u->posX,u->posY)][u->typeNum]+=recipient_meal_rate(*u);
    for(size_t colony=0;colony<world.feedingColonies.size();++colony)for(int unit=0;unit<3;++unit)
        world.feedingColonies[colony].demand[unit]=int(std::min<long long>(INT_MAX,(colonyMeals[colony][unit]+MealRatePrecision/2)/MealRatePrecision));
    for(const auto& colony:world.feedingColonies) {
        add_preemptive_hash(worldSignature,Uint32(colony.x));add_preemptive_hash(worldSignature,Uint32(colony.y));
        for(int unit=0;unit<3;++unit)add_preemptive_hash(worldSignature,Uint32(colony.demand[unit]));
    }
	if(signature)*signature=worldSignature;
	return world;
}


std::pair<int,int> Maxima::barracks_capacity(Context& runtime,int excludedAction) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
 using namespace AIMaximaPlacement;std::map<int,std::pair<int,int>> seats;
 for(const auto& [id,record]:runtime.get_building_register().found()) {
  const auto* b=runtime.get_building_register().get_building(id);if(!b||!AIMaximaBuildings::serves(runtime.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*b),AIMaximaBuildings::CombatTraining))continue;
  const auto* complete=AIMaximaBuildings::completed(runtime.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*b));
  if(!(complete->semantics.admittedUnitMask&(1u<<WARRIOR)))continue;
  seats[b->gid]={b->buildingState==Building::ALIVE&&!AIEngine::ObservationQueries::buildingType(runtime.observation(),*b).isBuildingSite&&b->constructionResultState==Building::NO_CONSTRUCTION?b->maxUnitInside:0,complete->maxUnitInside};
 }
 for(const auto& [id,a]:development_planner.actions()) {
  if(id==excludedAction||a.type!=UpgradeBuilding||(a.state!=CreateIssued&&a.state!=SiteObserved))continue;
  const auto* b=runtime.get_building_register().get_building(a.buildingId);if(!b||!seats.count(b->gid))continue;
  const auto* target=profile_variant(a.buildingType,a.targetLevel);seats[b->gid].first=0;
  if(target&&(target->roles&AIMaximaBuildings::roleBit(AIMaximaBuildings::CombatTraining)))seats[b->gid].second=target->seats;
 }
 std::pair<int,int> result{};for(const auto& [gid,p]:seats){result.first+=p.first;result.second+=p.second;}return result;
}

int Maxima::committed_hospital_beds(const std::vector<AIMaximaPlacement::WorldBuilding>& buildings,int excludedAction) const
{
 using namespace AIMaximaPlacement;std::map<int,int> seats;
 for(const auto& b:buildings){const auto* v=profile_variant(b.buildingType,b.level);if(v&&(v->roles&AIMaximaBuildings::roleBit(AIMaximaBuildings::Healing)))seats[b.id]=v->seats;}
 int pending=0;
 for(const auto& [id,a]:development_planner.actions()) {
  if(id==excludedAction||(a.state!=ParcelReserved&&a.state!=CreateIssued&&a.state!=SiteObserved))continue;
  const auto* v=profile_variant(a.buildingType,a.targetLevel);if(!v||!(v->roles&AIMaximaBuildings::roleBit(AIMaximaBuildings::Healing)))continue;
  if(seats.count(a.buildingId))seats[a.buildingId]=std::max(seats[a.buildingId],v->seats);
  else if(a.type==BuildCampusMember||a.type==BuildStandalone)pending+=v->seats;
 }
 for(const auto& [id,n]:seats)pending+=n;return pending;
}


std::vector<AIMaximaPlacement::DevelopmentIntent>
Maxima::collect_development_intents(
	const AIMaximaPlacement::WorldState& world) const
{
	using namespace AIMaximaPlacement;
	std::vector<DevelopmentIntent> result;
	const int bedDeficit=std::max(0, Labour::hospitalBedsWanted(snapshot.warriors,
		strategy.military.hospital_beds_per_warrior_percent)
		-committed_hospital_beds(world.buildings));
	const auto* healing=profile_variant(preferred_profile(AIMaximaBuildings::Healing));
	const int basicBeds=std::max(1,healing?healing->seats:0);
	const int hospitals=development_planner.committedRoleCount(world,
		AIMaximaBuildings::Healing)+(bedDeficit+basicBeds-1)/basicBeds;
	struct Demand {int type,desired,priority,workers;unsigned productionMask=0;};
	const Demand demands[]={
		{AIMaximaBuildings::Feeding,budget.desired_inns,
			budget.priority_inns,strategy.staffing.construction_inn_workers},
		{AIMaximaBuildings::Production,budget.worker_ratio>0?budget.desired_swarms:0,
			budget.priority_swarms,strategy.staffing.construction_swarm_workers,1u<<WORKER},
		{AIMaximaBuildings::Production,budget.explorer_ratio>0&&snapshot.explorers<budget.desired_explorers?1:0,
			budget.priority_swarms,strategy.staffing.construction_swarm_workers,1u<<EXPLORER},
		{AIMaximaBuildings::Production,budget.warrior_ratio>0&&snapshot.warriors<budget.desired_warriors?1:0,
			budget.priority_swarms,strategy.staffing.construction_swarm_workers,1u<<WARRIOR},
		{AIMaximaBuildings::WalkTraining,budget.desired_racetracks,
			budget.priority_racetracks,
			strategy.staffing.construction_large_workers},
		{AIMaximaBuildings::SwimTraining,budget.desired_pools,
			budget.priority_pools,strategy.staffing.construction_large_workers},
		{AIMaximaBuildings::ConstructionTraining,budget.desired_schools,
			budget.priority_schools,strategy.staffing.construction_training_workers},
		{AIMaximaBuildings::CombatTraining,budget.desired_barracks,
			budget.priority_barracks,strategy.staffing.construction_training_workers},
		{AIMaximaBuildings::Healing,hospitals,
			budget.priority_hospitals,strategy.staffing.construction_hospital_workers},
		{AIMaximaBuildings::ProjectileDefense,budget.desired_towers,
			budget.priority_towers,strategy.staffing.construction_large_workers}};
	for(size_t i=0;i<sizeof(demands)/sizeof(demands[0]);++i)
	{
		const int current=development_planner.committedRoleCount(
			world,demands[i].type,demands[i].productionMask);
		if(demands[i].desired>current)
		{
			DevelopmentIntent intent;
			intent.unmetCount=demands[i].desired-current;
			intent.priority=clamp_score(demands[i].priority);
			// A site is staffed from what is idle, not from a fixed number: the
			// configured count is a floor, and spare hands are shared across the
			// sites the budget allows, up to what one site can use.
			intent.workers=std::min(12, std::max(demands[i].workers,
				demands[i].workers+std::max(0, labour_observation.idle
					-labour_plan.trainingReserve)
					/std::max(1, budget.construction_sites)));
			intent.emergency=demands[i].type==AIMaximaBuildings::Feeding
				?budget.recovery_active:demands[i].type==AIMaximaBuildings::ProjectileDefense
				&&explorer_defense_active();
			for(const auto& p:world.profiles){const auto* v=p.atLevel(1);
			 if(!v||!v->available||!(v->roles&AIMaximaBuildings::roleBit(demands[i].type))||v->requiredWorkerLevel>AIEngine::ObservationQueries::maxBuildLevel(context.observation(),context.teamNumber())||(demands[i].productionMask&&(v->productionUnitMask&demands[i].productionMask)!=demands[i].productionMask))continue;
			 intent.buildingType=p.buildingType;intent.workers=std::min(intent.workers,v->assignmentLimit);
			 auto old=std::find_if(result.begin(),result.end(),[&](const auto& x){return x.buildingType==intent.buildingType&&x.purpose==intent.purpose;});
			 if(old==result.end())result.push_back(intent);else {old->priority=std::max(old->priority,intent.priority);old->unmetCount=std::max(old->unmetCount,intent.unmetCount);old->emergency|=intent.emergency;}
			}
		}
	}
	// Once useful development is exhausted, idle builders fortify the colony.
	// The planner ranks this purpose below every viable ordinary action.
	const int spare=labour_observation.idle-labour_plan.trainingReserve;
	if(spare>=strategy.staffing.construction_large_workers
	   && budget.desired_towers<=development_planner.committedRoleCount(
		world,AIMaximaBuildings::ProjectileDefense))
	{
		DevelopmentIntent tower;
		tower.buildingType=preferred_profile(AIMaximaBuildings::ProjectileDefense);
		tower.purpose=Fortification;
		tower.unmetCount=1;
		tower.workers=std::min(12,spare);
		if(tower.buildingType>=0)result.push_back(tower);
	}
	if(relocation_target_building>=0)
	{
		const WorldBuilding* old=world.building(relocation_target_building);
		bool underway=false;
		for(const auto& entry:development_planner.actions())
		{
			const DevelopmentAction& action=entry.second;
			if(action.purpose!=Relocation
			   || action.replacesBuildingId!=relocation_target_building) continue;
			if(action.state==ParcelReserved || action.state==CreateIssued
			   || action.state==SiteObserved || action.state==Completed)
				underway=true;
		}
		if(old && !old->site && !underway)
		{
			const bool swarm=profile_serves(old->buildingType,AIMaximaBuildings::Production,old->level);
			DevelopmentIntent relocation;
			relocation.buildingType=old->buildingType;
			relocation.purpose=Relocation;
			relocation.replacesBuildingId=relocation_target_building;
			relocation.unmetCount=1;
			relocation.priority=clamp_score(swarm?budget.priority_swarms:budget.priority_inns);
			relocation.workers=swarm?strategy.staffing.construction_swarm_workers
				:strategy.staffing.construction_inn_workers;
			result.push_back(relocation);
		}
	}
	if(budget.colony_swarm_requested
	   && development_planner.activeRoleBuildCount(AIMaximaBuildings::Production,ColonySeed)==0)
	{
		DevelopmentIntent colony;
		colony.buildingType=preferred_profile(AIMaximaBuildings::Production);
		colony.purpose=ColonySeed;
		colony.requiredResourceType=WHEAT;
		colony.unmetCount=1;
		colony.workers=strategy.staffing.construction_swarm_workers;
		// Colony priority comes from new food / establishment cost in placement.
		colony.priority=0;
		if(colony.buildingType>=0)result.push_back(colony);
	}

	return result;
}


AIMaximaPlacement::DevelopmentLimits Maxima::collect_development_limits(Context& runtime,int excludedAction) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
 using namespace AIMaximaPlacement;using namespace AIMaximaBuildings;
 DevelopmentLimits limits;limits.newConstruction=budget.construction_sites;limits.allowUpgrades=budget.allow_upgrades;
 limits.allowLevel2Upgrades=budget.allow_level2_upgrades;limits.allowRepairs=strategy.repairs.enabled;
 int total[RoleCount]{},operational[RoleCount]{};std::set<int> unavailable;
 for(const auto& [id,a]:development_planner.actions())if(id!=excludedAction&&(a.state==ParcelReserved||a.state==CreateIssued||a.state==SiteObserved)) {
  if(a.type==BuildCampusMember||a.type==BuildStandalone)++limits.activeNewConstruction;
  else {unavailable.insert(a.buildingId);if(a.type==UpgradeBuilding){if(a.fromLevel==1)++limits.activeLevel1Upgrades;else ++limits.activeLevel2Upgrades;}}
 }
 for(const auto& [id,record]:runtime.get_building_register().found()) {
  const auto* b=runtime.get_building_register().get_building(id);if(!b||b->buildingState!=Building::ALIVE||b->constructionResultState==Building::NEW_BUILDING)continue;
  const auto roles=capabilities(runtime.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*b));
  const bool running=!AIEngine::ObservationQueries::buildingType(runtime.observation(),*b).isBuildingSite&&b->constructionResultState==Building::NO_CONSTRUCTION&&!record.upgrading&&!unavailable.count(id);
  for(int role=0;role<RoleCount;++role)if(roles&roleBit(role)){++total[role];operational[role]+=running;}
 }
 const int roles[]={Feeding,Healing,WalkTraining,SwimTraining,CombatTraining};
 const int first[]={budget.upgrade_level1_inn_weight,budget.upgrade_level1_hospital_weight,budget.upgrade_level1_racetrack_weight,budget.upgrade_level1_pool_weight,budget.upgrade_level1_barracks_weight};
 const int later[]={budget.upgrade_level2_inn_weight,budget.upgrade_level2_hospital_weight,budget.upgrade_level2_racetrack_weight,budget.upgrade_level2_pool_weight,budget.upgrade_level2_barracks_weight};
 const int qualification=AIEngine::ObservationQueries::maxBuildLevel(runtime.observation(),runtime.teamNumber());
 const int healingWanted=Labour::hospitalBedsWanted(snapshot.warriors,strategy.military.hospital_beds_per_warrior_percent);
 std::vector<WorldBuilding> physical;
 for(const auto& [id,record]:runtime.get_building_register().found()) {
  const auto* b=runtime.get_building_register().get_building(id);if(!b||b->buildingState!=Building::ALIVE)continue;
  WorldBuilding entry;entry.id=id;entry.buildingType=lineageRoot(runtime.observation(),b->typeNum);entry.level=lineagePosition(runtime.observation(),b->typeNum);physical.push_back(entry);
 }
 const bool healingDeficit=committed_hospital_beds(physical,excludedAction)<healingWanted;
 const bool combatTrainingNeeded=snapshot.trained_warriors<snapshot.warriors;
 const auto* stat=&runtime.observedTeam().statistics;
 bool hasPrestigeProvider=false;
 for(const auto& [id,record]:runtime.get_building_register().found()) {
  const auto* b=runtime.get_building_register().get_building(id);
  if(b && b->buildingState==Building::ALIVE && completed(runtime.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*b))->prestige>0)hasPrestigeProvider=true;
 }
 for(const auto& p:collect_building_profiles())for(const auto& v:p.levels) {
  int priority=-1;for(int r=0;r<5;++r)if(v.roles&roleBit(roles[r]))priority=std::max(priority,roles[r]==Healing&&!healingDeficit?0:v.level==1?first[r]:later[r]);
  const auto* next=p.atLevel(v.level+1);
  if(next && runtime.observation().catalog->at(next->completedType).resolvedType.prestige>
      runtime.observation().catalog->at(v.completedType).resolvedType.prestige) {
   int trained=0;
   for(int level=std::clamp(next->requiredWorkerLevel,0,NB_UNIT_LEVELS-1);level<NB_UNIT_LEVELS;++level)
    trained+=stat->workersByConstructionLevel[level];
   const bool prestigeAllowed=hasPrestigeProvider
    ? trained>=budget.second_prestige_trained_workers && stat->totalUnit>=budget.second_prestige_population_min
    : trained>=budget.first_prestige_trained_workers;
   if(!prestigeAllowed)priority=0;
  }
  if(!next||!next->available||next->requiredWorkerLevel>qualification)priority=0;
  bool serviceLost=false;for(int role=0;role<RoleCount;++role)if((v.roles&roleBit(role))&&(role!=CombatTraining||combatTrainingNeeded)&&total[role]>=2&&operational[role]-1<(total[role]+1)/2)serviceLost=true;
  if(serviceLost){priority=0;limits.upgradePriorities[{p.buildingType,0}]=0;}
  limits.upgradePriorities[{p.buildingType,v.level}]=priority;
 }
 const int can1=stat->workersByConstructionLevel[1]+stat->workersByConstructionLevel[2]+stat->workersByConstructionLevel[3];
 const int can2=stat->workersByConstructionLevel[2]+stat->workersByConstructionLevel[3];
 limits.level1Upgrades=(can1+budget.upgrade_level1_trained_units_per_slot/2)/std::max(1,budget.upgrade_level1_trained_units_per_slot);
 limits.level2Upgrades=limits.allowLevel2Upgrades?(can2+budget.upgrade_level2_trained_units_per_slot/2)/std::max(1,budget.upgrade_level2_trained_units_per_slot):0;
 limits.activeNewConstruction=std::max(limits.activeNewConstruction,snapshot.building_sites);return limits;
}


bool Maxima::issue_development_action(Context& runtime,
	AIMaximaPlacement::DevelopmentAction& action)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	using namespace AIMaximaPlacement;int buildingId=-1;
	if(action.type==BuildCampusMember||action.type==BuildStandalone)
	{
		const auto* shape=profile_variant(action.buildingType,action.targetLevel);if(!shape)return false;
		const auto* placement=&runtime.observation().catalog->at(shape->engineType).resolvedType;if(!placement)return false;
		const int x=runtime.observation().normalizeX(action.centerX+placement->decLeft);
		const int y=runtime.observation().normalizeY(action.centerY+placement->decTop);
		buildingId=runtime.issue_building_at(shape->engineType,action.workers,x,y);
		if(buildingId<0)return false;
		if(profile_serves(action.buildingType,AIMaximaBuildings::Feeding,action.targetLevel))
		{
			ManagementOrder* update=new Notify(RuntimeEvent(RuntimeEvent::UpdateInn,buildingId));
			update->add_condition(new ParticularBuilding(new NotUnderConstruction,buildingId));
			runtime.add_management_order(update);
		}
		else if(profile_serves(action.buildingType,AIMaximaBuildings::Production,action.targetLevel))
		{
			ManagementOrder* update=new Notify(RuntimeEvent(RuntimeEvent::UpdateSwarm,buildingId));
			update->add_condition(new ParticularBuilding(new NotUnderConstruction,buildingId));
			runtime.add_management_order(update);
		}
	}
	else
	{
		buildingId=action.buildingId;
		if(action.type==UpgradeBuilding)
		{
			// A saved reservation may outlive its original director authorization.
			const DevelopmentLimits limits=collect_development_limits(runtime,action.id);
			if(!limits.allowUpgrades
			   || (action.fromLevel==2 && !limits.allowLevel2Upgrades)
			   || limits.upgradePriority(action.buildingType,action.fromLevel)==0)
				return false;
		}
		else if(action.type==RepairBuilding
		        && !collect_development_limits(runtime,action.id).repairAllowed(action.buildingType))
			return false;
		if(!runtime.issue_upgrade_repair(buildingId,action.type==RepairBuilding))return false;
		// Repairs execute the labor allocation used to score their cost and
		// downtime; upgrade staffing still follows the current director plan.
		const int workers=action.type==RepairBuilding ? action.workers
			: (action.fromLevel<=1 ? budget.upgrade_level1_workers
				: budget.upgrade_level2_workers);
		// Wait for a site that can actually take the request. A building that
		// has only just started upgrading is under construction but not yet
		// ALIVE, and the engine drops worker orders in that window without
		// reporting anything; staffing one early would also pin units in place
		// and stop the site from forming at all.
		ManagementOrder* assignment=new AssignWorkers(workers,buildingId);
		assignment->add_condition(
			new ParticularBuilding(new StaffableConstructionSite,buildingId));
		runtime.add_management_order(assignment);
		if(action.type==UpgradeBuilding)
		{
			ManagementOrder* update=new Notify(RuntimeEvent(RuntimeEvent::BuildingUpdated,
				action.buildingType,buildingId));
			update->add_condition(new ParticularBuilding(new NotUnderConstruction,buildingId));
			runtime.add_management_order(update);
			// An upgrade is only useful once it finishes, and a half-upgraded
			// building serves nobody meanwhile. Outrank equals for workers while
			// the site is live, then hand the advantage back on completion.
			ManagementOrder* raise=new ChangePriority(1,buildingId);
			raise->add_condition(new ParticularBuilding(new UnderConstruction,buildingId));
			runtime.add_management_order(raise);
			ManagementOrder* restore=new ChangePriority(0,buildingId);
			restore->add_condition(new ParticularBuilding(new NotUnderConstruction,buildingId));
			runtime.add_management_order(restore);
		}
	}
	development_planner.markIssued(action.id,buildingId,timer);
	return true;
}


void Maxima::emit_placement_diagnostics(Context& runtime,const char* outcome,
	const AIMaximaPlacement::DevelopmentAction* action) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	using namespace AIMaximaPlacement;
	const PlacementDiagnostics& d=development_planner.diagnostics();std::ostringstream fields;
	fields<<"\toutcome="<<outcome<<"\tcandidates="<<d.candidateCount
		<<"\tstrict_candidates="<<d.strictCandidateCount
		<<"\tfallback_candidates="<<d.fallbackCandidateCount
		<<"\twater_tier="<<d.waterTier<<"\treservation="<<d.reservationId;
	for(int r=0;r<RejectionReasonCount;++r)if(d.rejected[r])
		fields<<"\trejected_"<<rejectionName(static_cast<RejectionReason>(r))<<"="<<d.rejected[r];
	if(action)fields<<"\taction="<<action->id<<"\taction_type="<<action->type
		<<"\tbuilding_type="<<action->buildingType
		<<"\tpurpose="<<(action->purpose==ColonySeed ? "colony_seed"
			: action->purpose==Fortification ? "fortification" : "core_capacity")
		<<"\ttemplate="<<action->templateId<<"\tcampus="<<action->campusId
		<<"\tslot="<<action->slotId<<"\tbuilding="<<action->buildingId
		<<"\tx="<<action->centerX<<"\ty="<<action->centerY
		<<"\tutility="<<action->utility.total
		<<"\tu_demand="<<action->utility.unmetDemand
		<<"\tu_service="<<action->utility.serviceGain
		<<"\tu_unlock="<<action->utility.capabilityGain
		<<"\tu_parallelism="<<action->utility.parallelismGain
		<<"\tu_redundancy="<<action->utility.redundancyGain
		<<"\tu_role="<<action->utility.roleLocationQuality
		<<"\tu_defended="<<action->utility.defendedness
		<<"\tu_compact="<<action->utility.compactness
		<<"\tu_farm_loss="<<action->utility.projectedFarmLoss
		<<"\tu_food_zone="<<action->utility.foodZonePressure
		<<"\tu_land="<<action->utility.newlyReservedLand
		<<"\tu_scarcity="<<action->utility.resourceScarcity
		<<"\tu_labor="<<action->utility.constructionLabor
		<<"\tu_downtime="<<action->utility.serviceDowntime
		<<"\tu_threat="<<action->utility.threatExposure
		<<"\tu_artery="<<action->utility.newArteryLength
		<<"\tfriendly_distance="<<action->utility.friendlyDistance
		<<"\tcorn_distance="<<action->utility.cornDistance
		<<"\tcolony_new_food="<<action->utility.frontierGain
		<<"\tcolony_value="<<action->utility.conqueredGain;
	fields<<"\tcolonization_eligible="<<(budget.colony_swarm_requested ? 1 : 0)
		<<"\tcolonization_gate="<<colony_gate_reason
		<<"\tactive_colonial_action="<<development_planner.activeRoleBuildCount(
			AIMaximaBuildings::Production, ColonySeed)
		<<"\thotspot_count="<<cleared_enemy_sites.size();
	emit_telemetry(runtime,"placement_planner",fields.str());
}


void Maxima::development_cycle(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	using namespace AIMaximaPlacement;
	const bool profile=telemetry_enabled();
	const std::chrono::steady_clock::time_point profileStarted=profile
		?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point();
	const bool continuingSelection=development_planner.selectionPending();
	uint32_t worldSignature=0;
	WorldState refreshedWorld;
	const WorldState* worldSnapshot=&refreshedWorld;
	if(continuingSelection)
	{
		// Incremental selection deliberately evaluates one immutable snapshot.
		// Reusing it here avoids rebuilding the world, lifecycle state and scoring
		// inputs on every slice, while keeping the eventual winner exactly equal to
		// an unsliced selection over that snapshot.
		worldSnapshot=&development_planner.selectionWorld();
		worldSignature=development_planner.selectionOccupancySignature();
	}
	else
	{
		initialize_farming_cache(runtime);
		if(!development_planner_initialized)
		{
			configure_development_planner();
			WorldState initial=collect_development_world(runtime);
			development_planner.adoptStartingBuildings(initial);
			development_planner_initialized=true;
		}
		refreshedWorld=collect_development_world(runtime,&worldSignature);
		if (fieldDiagnostics) fieldDiagnostics->capture(refreshedWorld);
		development_planner.observe(refreshedWorld,worldSignature);
		// An under-supplied building is a burden whatever its distance from
		// wheat; with the ledger disabled nothing is retired. Relocation goes
		// first so a building with a better site is rebuilt rather than lost;
		// retirement takes over once the planner has found nowhere better.
		if(budget.food_ledger_enabled)
		{
			update_food_relocation(runtime,refreshedWorld);
			update_food_retirement(runtime,refreshedWorld);
		}
		// Reconcile any starting construction site when it first becomes a completed
		// building. Planner-owned campus and standalone actions are ignored here.
		development_planner.adoptStartingBuildings(refreshedWorld);
		const std::map<int,DevelopmentAction>& observed=development_planner.actions();
		for(std::map<int,DevelopmentAction>::const_iterator i=observed.begin();i!=observed.end();++i)
		{
		std::map<int,ActionLifecycleState>::iterator old=development_reported_states.find(i->first);
		if(old!=development_reported_states.end()&&old->second==i->second.state)continue;
		development_reported_states[i->first]=i->second.state;
		RuntimeEvent::Type event=RuntimeEvent::DevelopmentParcelReserved;
		switch(i->second.state){case ParcelReserved:event=RuntimeEvent::DevelopmentParcelReserved;break;case CreateIssued:event=RuntimeEvent::DevelopmentCreateIssued;break;case SiteObserved:event=RuntimeEvent::DevelopmentSiteObserved;break;case Completed:event=RuntimeEvent::DevelopmentCompleted;break;case InvalidatedBeforeIssue:event=RuntimeEvent::DevelopmentInvalidatedBeforeIssue;break;case CreateTimedOut:event=RuntimeEvent::DevelopmentCreateTimedOut;break;case DestroyedDuringConstruction:event=RuntimeEvent::DevelopmentDestroyedDuringConstruction;break;case UpgradeBlocked:event=RuntimeEvent::DevelopmentUpgradeBlocked;break;case RequiredSourceMissing:event=RuntimeEvent::DevelopmentRequiredSourceMissing;break;case EngineRejected:event=RuntimeEvent::DevelopmentEngineRejected;break;default:break;}
		runtime.dispatch_event(RuntimeEvent(event,i->second.id,i->second.buildingId));
		emit_placement_diagnostics(runtime,lifecycleName(i->second.state),&i->second);
		if(i->second.purpose==Relocation)
		{
			std::ostringstream fields;
			fields<<"\taction_id="<<i->second.id
				<<"\tbuilding_id="<<i->second.buildingId
				<<"\treplaces="<<i->second.replacesBuildingId
				<<"\tx="<<i->second.centerX<<"\ty="<<i->second.centerY
				<<"\tstate="<<lifecycleName(i->second.state);
			emit_telemetry(runtime,"food_relocation_lifecycle",fields.str());
		}
		if(i->second.purpose==ColonySeed)
		{
			std::ostringstream fields;
			fields<<"\taction_id="<<i->second.id
				<<"\tbuilding_id="<<i->second.buildingId
				<<"\tx="<<i->second.centerX<<"\ty="<<i->second.centerY;
			if(i->second.state==Completed
			   && i->second.id>last_colony_accounted_action_id)
			{
				last_colony_accounted_action_id=i->second.id;
				last_colony_completed_tick=timer;
				++established_colonies;
				director.invalidate();
				fields<<"\testablished_colonies="<<established_colonies;
				emit_telemetry(runtime,"colony_swarm_completed",fields.str());
			}
			else if(i->second.state==InvalidatedBeforeIssue
				||i->second.state==CreateTimedOut
				||i->second.state==DestroyedDuringConstruction
				||i->second.state==RequiredSourceMissing
				||i->second.state==EngineRejected)
				emit_telemetry(runtime,"colony_swarm_failed",fields.str());
		}
		}

		// A resource can regrow in a reserved immediate footprint between planning
		// and issue. Keep the reservation while its durable maintenance area weeds the
		// obstruction, rather than abandoning the plan and its promised access route.
		std::vector<int> reservedActions;
		for(std::map<int,DevelopmentAction>::const_iterator i=development_planner.actions().begin();
			i!=development_planner.actions().end();++i)
			if(i->second.state==ParcelReserved)reservedActions.push_back(i->first);
		for(size_t i=0;i<reservedActions.size();++i)
		{
		std::map<int,DevelopmentAction>::const_iterator found=
			development_planner.actions().find(reservedActions[i]);
		if(found==development_planner.actions().end())continue;
		DevelopmentAction pending=found->second;RejectionReason reason=RejectedReservation;
		if((pending.type==UpgradeBuilding || pending.type==RepairBuilding)
		   &&!development_planner.revalidateSelection(refreshedWorld,{},
			collect_development_limits(runtime,pending.id),pending,&reason))
		{
			development_planner.markInvalidated(pending.id,UpgradeBlocked,worldSignature);
			continue;
		}
			if(development_planner.revalidate(refreshedWorld,pending,&reason,true))
			{
				if(!issue_development_action(runtime,pending))
					development_planner.markInvalidated(pending.id,
						pending.type==UpgradeBuilding||pending.type==RepairBuilding
						?UpgradeBlocked:EngineRejected,worldSignature,
						refreshedWorld.index(pending.centerX,pending.centerY));
				else
				{
					development_reported_states[pending.id]=CreateIssued;
					std::map<int,DevelopmentAction>::const_iterator issued=
						development_planner.actions().find(pending.id);
					runtime.dispatch_event(RuntimeEvent(RuntimeEvent::DevelopmentCreateIssued,
						pending.id,issued==development_planner.actions().end()
						? pending.buildingId:issued->second.buildingId));
				}
			}
		else
		{
			if(reason==RejectedClearableResource
			   &&(pending.type==BuildCampusMember
				||pending.type==BuildStandalone||pending.type==UpgradeBuilding))
			{
				record_construction_space_failure();
				continue;
			}
			development_planner.markInvalidated(pending.id,
					pending.type==UpgradeBuilding?UpgradeBlocked:InvalidatedBeforeIssue,
					worldSignature,
					(reason!=RejectedClearableResource
					 &&(pending.type==BuildCampusMember||pending.type==BuildStandalone))
						? refreshedWorld.index(pending.centerX,pending.centerY) : -1);
		}
		}
	}
	const WorldState& world=*worldSnapshot;

	DevelopmentLimits limits=continuingSelection
		? development_planner.selectionLimits() : collect_development_limits(runtime);
	const int totalLimit=limits.totalCapacity();
	int activeDevelopment=0;
	for(std::map<int,DevelopmentAction>::const_iterator i=development_planner.actions().begin();
		i!=development_planner.actions().end();++i)
		if(i->second.state==ParcelReserved||i->second.state==CreateIssued
		   ||i->second.state==SiteObserved)++activeDevelopment;
	activeDevelopment=std::max(activeDevelopment,limits.activeNewConstruction
		+limits.activeLevel1Upgrades+limits.activeLevel2Upgrades);
	bool selectionExhausted=false;
	// A single selector pass may traverse the whole discovered map. Issue at most
	// one development per logical update, then resume the remaining capacity on a
	// following tick. Each pass still uses the complete current-world snapshot and
	// the original total ordering, so only order timing changes—not the selected
	// result for that snapshot.
	const int selectionLimit=std::min(totalLimit,activeDevelopment+1);
	for(int issued=activeDevelopment;issued<selectionLimit;++issued)
	{
		// Recompute live deficits after every issue. The world snapshot does not
		// change inside this loop, but committedBuildingCount includes newly issued
		// planner actions, preventing one unmet building from filling every free site.
		std::vector<DevelopmentIntent> refreshedIntents;
		if(!continuingSelection)
			refreshedIntents=collect_development_intents(world);
		const std::vector<DevelopmentIntent>& intents=continuingSelection
			? development_planner.selectionIntents() : refreshedIntents;
		DevelopmentAction action;
		const SelectionProgress selection=
			development_planner.selectActionIncremental(world,intents,limits,action,
				worldSignature,4096);
		if(selection==SelectionPending)
		{
			development_cycle_pending=true;
			break;
		}
		if(selection==SelectionEmpty)
		{
			selectionExhausted=true;
			const PlacementDiagnostics& diagnostics=
				development_planner.diagnostics();
			if(diagnostics.rejected[RejectedClearableResource])
				record_construction_space_failure();
			if(development_planner.diagnostics().candidateCount
			   ||development_planner.diagnostics().rejected[RejectedNegativeUtility])
				emit_placement_diagnostics(runtime,"waiting",NULL);
			if(budget.colony_swarm_requested)
			{
				const PlacementDiagnostics& diagnostics=
					development_planner.diagnostics();
				int rejected=0;
				for(int reason=0; reason<RejectionReasonCount; ++reason)
					rejected+=diagnostics.rejected[reason];
				if(diagnostics.candidateCount || rejected)
				{
					std::ostringstream fields;
					fields<<"\tstage=placement\tcandidates="
						<<diagnostics.candidateCount<<"\trejected="<<rejected;
					emit_telemetry(runtime,"colony_swarm_failed",fields.str());
				}
			}
			break;
		}
		// Keep selection scores snapshot-stable, but never execute stale authority
		// or stale spatial contracts. Only a finished winner needs this live pass.
		uint32_t issueSignature=0;
		WorldState issueWorld=collect_development_world(runtime,&issueSignature);
		development_planner.observe(issueWorld,issueSignature);
		const DevelopmentLimits issueLimits=collect_development_limits(runtime);
		const std::vector<DevelopmentIntent> issueIntents=
			collect_development_intents(issueWorld);
		RejectionReason issueReason=RejectedAuthorization;
		if(!development_planner.revalidateSelection(issueWorld,issueIntents,
			issueLimits,action,&issueReason))
		{
			action.state=InvalidatedBeforeIssue;
			emit_placement_diagnostics(runtime,"selection_became_invalid",&action);
			// A changed world is not a permanently broken coordinate.
			development_cycle_pending=true;
			break;
		}
		if(!development_planner.reserve(issueWorld,action))
		{emit_placement_diagnostics(runtime,"invalidated_before_issue",&action);continue;}
		development_reported_states[action.id]=ParcelReserved;
		runtime.dispatch_event(RuntimeEvent(RuntimeEvent::DevelopmentParcelReserved,
			action.id,action.buildingId));
		// A development consumes its category quota as soon as its parcel is
		// reserved.
		if(action.type==BuildCampusMember||action.type==BuildStandalone)
			++limits.activeNewConstruction;
		else if(action.type==UpgradeBuilding&&action.fromLevel==1)
			++limits.activeLevel1Upgrades;
		else if(action.type==UpgradeBuilding&&action.fromLevel==2)
			++limits.activeLevel2Upgrades;
		RejectionReason revalidationReason=RejectedReservation;
		if(!development_planner.revalidate(issueWorld,action,&revalidationReason,true))
		{
			if(revalidationReason==RejectedClearableResource
			   &&(action.type==BuildCampusMember
				||action.type==BuildStandalone||action.type==UpgradeBuilding))
			{
				record_construction_space_failure();
				// Keep the selected action and quota while maintenance clears its
				// immediate footprint. Future, unselected expansion stays preserved.
				continue;
			}
			development_planner.markInvalidated(action.id,
				action.type==UpgradeBuilding?UpgradeBlocked:InvalidatedBeforeIssue,
				issueSignature,
				action.type==BuildCampusMember||action.type==BuildStandalone
					? issueWorld.index(action.centerX,action.centerY) : -1);
			emit_placement_diagnostics(runtime,"revalidation_failed",&action);continue;
		}
		if(!issue_development_action(runtime,action))
		{
			development_planner.markInvalidated(action.id,
				action.type==UpgradeBuilding||action.type==RepairBuilding
				?UpgradeBlocked:EngineRejected,
				issueSignature,issueWorld.index(action.centerX,action.centerY));
			emit_placement_diagnostics(runtime,"engine_rejected",&action);continue;
		}
		development_reported_states[action.id]=CreateIssued;
		std::map<int,DevelopmentAction>::const_iterator issuedAction=
			development_planner.actions().find(action.id);
		runtime.dispatch_event(RuntimeEvent(RuntimeEvent::DevelopmentCreateIssued,
			action.id,issuedAction==development_planner.actions().end()
			? action.buildingId:issuedAction->second.buildingId));
		emit_placement_diagnostics(runtime,"selected",&action);
		if(action.purpose==ColonySeed)
		{
			std::ostringstream fields;
			fields<<"\taction_id="<<action.id<<"\tx="<<action.centerX
				<<"\ty="<<action.centerY
				<<"\tfriendly_distance="<<action.utility.friendlyDistance
				<<"\tcorn_distance="<<action.utility.cornDistance
				<<"\tcolony_new_food="<<action.utility.frontierGain
				<<"\tcolony_value="<<action.utility.conqueredGain;
			emit_telemetry(runtime,"colony_swarm_selected",fields.str());
		}
		// Reservations immediately affect subsequent candidates even though the
		// engine has not observed the queued OrderCreate yet.
	}
	if(!selectionExhausted&&selectionLimit<totalLimit)
		development_cycle_pending=true;
	if(profile)
		emit_telemetry(runtime,"placement_cycle_performance",
			"\tmicroseconds="+telemetryText(
				std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now()-profileStarted).count()));
}
void Maxima::update_food_retirement(Context& runtime,
	const AIMaximaPlacement::WorldState& world)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	// Fully qualified: this translation unit has other Result types in scope.
	const AIMaximaFoodLedger::Result& ledger=
		development_planner.evaluateFoodLedger(world);
	const AIMaximaPlacement::PlacementPolicy& policy=development_planner.policy();
	food_ledger_valid=true;

	const std::set<int> establishing=establishing_colony_buildings();

	const int inn_burden=budget.food_inn_burden_percent;
	const int swarm_burden=budget.food_swarm_burden_percent;
	int supported_inns=0, supported_swarms=0;
    long long productionClaim=0;
	std::set<int> present;
	for(size_t i=0;i<ledger.consumers.size();++i)
	{
		const AIMaximaFoodLedger::ConsumerResult& value=ledger.consumers[i];
        const AIMaximaPlacement::BuildingLevelProfile* definition=nullptr;
        if(const auto* building=world.building(value.key))definition=profile_variant(building->buildingType,std::max(1,value.level));
        else if(value.key<0) {
            const auto action=development_planner.actions().find(-value.key-1);
            if(action!=development_planner.actions().end())definition=profile_variant(action->second.buildingType,std::max(1,action->second.targetLevel));
        }
        const bool workerProducer=definition && (definition->productionUnitMask&(1u<<WORKER));
        if(workerProducer && value.demand>0)
            productionClaim+=value.claimedProduction;
		const int burden=value.kind==AIMaximaFoodLedger::InnConsumer ? inn_burden : swarm_burden;
		if(value.kind==AIMaximaFoodLedger::InnConsumer && value.coveragePercent>=burden)
			++supported_inns;
		// Retain viable existing capacity; funding and placement still use claims.
		if(workerProducer && value.uncontestedCoveragePercent>=burden)
			++supported_swarms;
		if(value.key<0) continue;
		present.insert(value.key);
		// Hysteresis: the confirmation survives a dip, and only a real recovery
		// clears it, so two similar buildings cannot trade places forever.
		const int retirementCoverage=value.retirementCoveragePercent();
		if(retirementCoverage>=budget.food_recovered_percent)
			food_burden_since.erase(value.key);
		else if(retirementCoverage<burden && !food_burden_since.count(value.key))
			food_burden_since[value.key]=timer;
	}
	// Only capacity one site could actually collect supports another building.
	// A plain total would add up remnants no single building can ever reach.
	const long long margin=std::max(1,policy.foodMarginPercent);
    const auto* feeding=profile_variant(preferred_profile(AIMaximaBuildings::Feeding));
    const long long inn_cost=feeding?static_cast<long long>(feeding->operatingResources[WHEAT])*margin/100:0;
	const long long swarm_cost=static_cast<long long>(policy.foodSwarmDemand)*margin/100;
    if(inn_cost==0 && feeding && feeding_capacity(feeding->engineType,1)>0) {
        supported_inns=Building::MAX_COUNT; // Wheat cannot constrain a wheat-free service.
    }
	if(inn_cost>0)
		supported_inns+=int(std::min<long long>(INT_MAX,
			ledger.bestSiteResidual/inn_cost));
	if(swarm_cost>0)
		supported_swarms+=int(std::min<long long>(INT_MAX,
			ledger.bestSiteResidual/swarm_cost));
    if(swarm_cost==0) {
        const auto* producer=profile_variant(preferred_profile(AIMaximaBuildings::Production));
        if(producer && (producer->productionUnitMask&(1u<<WORKER)))supported_swarms=Building::MAX_COUNT;
    }
    food_birth_crop_rate=int(std::min<long long>(INT_MAX,productionClaim+ledger.bestSiteResidual));
	food_supported_inns=supported_inns;
	food_supported_swarms=supported_swarms;

	for(std::map<int,int>::iterator i=food_burden_since.begin();
		i!=food_burden_since.end();)
		if(!present.count(i->first))food_burden_since.erase(i++);else ++i;
	for(std::set<int>::iterator i=food_retirement_issued.begin();
		i!=food_retirement_issued.end();)
		if(!present.count(*i))food_retirement_issued.erase(i++);else ++i;

	if(timer%1000<budget.farming_normal_interval)
	{
		std::ostringstream fields;
		fields<<"\tsupply="<<ledger.totalSupply<<"\tdemand="<<ledger.totalDemand
			<<"\tclaimed="<<ledger.totalClaimed
			<<"\tresidual="<<ledger.totalResidual
			<<"\tbest_site="<<ledger.bestSiteResidual
			<<"\tconsumers="<<ledger.consumers.size()
			<<"\tsupported_inns="<<food_supported_inns
			<<"\tsupported_swarms="<<food_supported_swarms
			<<"\tburdened="<<food_burden_since.size();
		emit_telemetry(runtime,"food_ledger",fields.str());
		for(size_t i=0;i<ledger.consumers.size();++i)
		{
			const AIMaximaFoodLedger::ConsumerResult& value=ledger.consumers[i];
			std::ostringstream consumer;
			consumer<<"\tkey="<<value.key
				<<"\tkind="<<(value.kind==AIMaximaFoodLedger::InnConsumer?"inn":"swarm")
				<<"\tcolony="<<(value.colony?1:0)
				<<"\tretirable="<<(value.retirable?1:0)
				<<"\tdemand="<<value.demand<<"\tclaimed="<<value.claimed
                <<"\tproduction_demand="<<value.productionDemand<<"\tproduction_claimed="<<value.claimedProduction
				<<"\tavailable="<<value.available
				<<"\tcoverage="<<value.coveragePercent
				<<"\tuncontested_coverage="<<value.uncontestedCoveragePercent
				<<"\tretirement_coverage="<<value.retirementCoveragePercent()
				<<"\tquality="<<value.quality<<"\tquality_band="<<value.qualityBand
				<<"\torder="<<value.order;
			emit_telemetry(runtime,"food_consumer",consumer.str());
		}
	}

	if(!budget.food_retirement_enabled)return;
	const bool safe=!budget.recovery_active&&snapshot.critical_food==0
		&&snapshot.own_buildings_under_attack==0&&snapshot.own_units_under_attack==0;
	if(!safe)return;
	if(timer-last_food_retirement_tick<budget.food_retirement_cooldown_ticks)
		return;

	int completed_inns=0,completed_swarms=0;
	std::map<int,std::pair<unsigned,long long>> inn_rates;
    std::array<long long,8> remainingRates{};
	for(size_t b=0;b<world.buildings.size();++b)
	{
		const AIMaximaPlacement::WorldBuilding& building=world.buildings[b];
		if(building.site||food_building_pending_deletion(runtime,building.id))continue;
		if(profile_serves(building.buildingType,AIMaximaBuildings::Feeding,building.level))
		{
            const auto* definition=profile_variant(building.buildingType,building.level);
            const unsigned mask=definition->feedingMask&7u;
            const long long rate=static_cast<long long>(development_feeding_visit_rate[definition->completedType])*strategy.economy.reliable_inn_percent/100;
            ++completed_inns;inn_rates[building.id]={mask,rate};remainingRates[mask]+=rate;
        }
		else if(profile_serves(building.buildingType,AIMaximaBuildings::Production,building.level))
			++completed_swarms;
	}

	const AIMaximaPlacement::DevelopmentAction* relocation=current_food_relocation();
	const int replacement=relocation?relocation->buildingId:-1;
	const AIMaximaFoodLedger::ConsumerResult* worst=NULL;
	for(size_t i=0;i<ledger.consumers.size();++i)
	{
		const AIMaximaFoodLedger::ConsumerResult& value=ledger.consumers[i];
		if(value.key<0||!value.retirable)continue;
		if(establishing.count(value.key)||food_building_pending_deletion(runtime,value.key))
			continue;
		// Keep both sides of a handover until it finishes. Counts above exclude
		// every queued deletion, including one just issued by relocation.
		if(value.key==relocation_target_building||value.key==replacement)continue;
		const int burden=value.kind==AIMaximaFoodLedger::InnConsumer ? inn_burden : swarm_burden;
		const int retirementCoverage=value.retirementCoveragePercent();
		if(retirementCoverage>=burden)continue;
		const std::map<int,int>::const_iterator since=
			food_burden_since.find(value.key);
		if(since==food_burden_since.end()
		   ||timer-since->second<budget.food_burden_confirm_ticks)continue;
		if(value.kind==AIMaximaFoodLedger::InnConsumer)
		{
			const auto provider=inn_rates.find(value.key);
			if(provider==inn_rates.end()||completed_inns<=1)continue;
			// The ledger is an accounting view: workers stock whichever inn they
			// reach. Never remove seats the population is still eating from.
			auto without=remainingRates;without[provider->second.first]-=provider->second.second;
            if(aggregate_feeding_capacity(without)<snapshot.population)continue;
		}
		// Only completed buildings are retirable, so a swarm reaching here is
		// physically present; never remove the settlement's last one.
		else if(completed_swarms<=1)continue;
		if(!worst||retirementCoverage<worst->retirementCoveragePercent()
		   ||(retirementCoverage==worst->retirementCoveragePercent()&&value.key<worst->key))
			worst=&value;
	}
	if(!worst)return;
	// One at a time: freeing this building's wheat often clears the others.
	runtime.add_management_order(new DestroyBuilding(worst->key));
	food_retirement_issued.insert(worst->key);
	last_food_retirement_tick=timer;
	std::ostringstream fields;
	fields<<"\tbuilding_id="<<worst->key
		<<"\tkind="<<(worst->kind==AIMaximaFoodLedger::InnConsumer?"inn":"swarm")
		<<"\tcoverage="<<worst->coveragePercent
		<<"\tretirement_coverage="<<worst->retirementCoveragePercent()
		<<"\tdemand="<<worst->demand<<"\tclaimed="<<worst->claimed
		<<"\tquality="<<worst->quality
		<<"\tburden_age="<<(timer-food_burden_since[worst->key])
		<<"\tcompleted_inns="<<completed_inns
		<<"\tcompleted_swarms="<<completed_swarms;
	emit_telemetry(runtime,"food_retirement",fields.str());
}

std::set<int> Maxima::establishing_colony_buildings() const
{
	// A colony swarm settles land that has no protected farm yet, so it cannot
	// be judged a burden until its own settlement is running.
	std::set<int> establishing;
	for(const auto& entry:development_planner.actions())
	{
		const AIMaximaPlacement::DevelopmentAction& action=entry.second;
		if(action.purpose!=AIMaximaPlacement::ColonySeed
		   || action.buildingId<0) continue;
		if(!operating_colonies.count(action.id))
			establishing.insert(action.buildingId);
	}
	return establishing;
}

bool Maxima::food_building_pending_deletion(Context& runtime,int id) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const AIEngine::BuildingView* building=runtime.get_building_register().get_building(id);
	return food_retirement_issued.count(id)||relocation_destroy_issued.count(id)
		||!building||building->buildingState==Building::WAITING_FOR_DESTRUCTION;
}

const AIMaximaPlacement::DevelopmentAction* Maxima::current_food_relocation() const
{
	if(relocation_target_building<0)return NULL;
	const AIMaximaPlacement::DevelopmentAction* action=NULL;
	for(const auto& entry:development_planner.actions())
		if(entry.second.purpose==AIMaximaPlacement::Relocation
		   &&entry.second.replacesBuildingId==relocation_target_building
		   &&(!action||entry.second.id>action->id))
			action=&entry.second;
	return action;
}

void Maxima::finish_food_relocation()
{
	if(relocation_target_building>=0)
		development_planner.finishRelocation(relocation_target_building);
	relocation_target_building=-1;
	relocation_target_since=-1;
	relocation_completed_tick=-1;
	last_food_relocation_tick=timer;
}

void Maxima::update_food_relocation(Context& runtime,
	const AIMaximaPlacement::WorldState& world)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	using namespace AIMaximaPlacement;
	if(!budget.food_relocation_enabled)
	{
		relocation_since.clear();
		if(relocation_target_building>=0)finish_food_relocation();
		return;
	}
	const AIMaximaFoodLedger::Result& ledger=
		development_planner.evaluateFoodLedger(world);
	const std::set<int> establishing=establishing_colony_buildings();
	const PlacementPolicy& policy=development_planner.policy();
	const int threshold=budget.food_relocation_min_quality_tiles*100;
	std::set<int> present;
	const AIMaximaFoodLedger::ConsumerResult* worst=NULL;
	for(size_t i=0;i<ledger.consumers.size();++i)
	{
		const AIMaximaFoodLedger::ConsumerResult& value=ledger.consumers[i];
		if(value.key<0||!value.retirable)continue;
		present.insert(value.key);
		// Quality already charges unreachable demand at the penalty distance,
		// so a starving building looks far even when its wheat is close.
		// Only nominate what could clear a gain floor even at a perfect site.
		const bool distanceRoom=static_cast<long long>(value.quality)
			*policy.relocationDistanceRealisationPercent/100
			>=static_cast<long long>(policy.relocationMinGainTiles)*100;
		const bool coverageRoom=100-value.coveragePercent
			>=policy.relocationMinCoverageGainPercent;
		if(establishing.count(value.key)||value.quality<threshold
		   ||food_building_pending_deletion(runtime,value.key)||!(distanceRoom||coverageRoom))
		{relocation_since.erase(value.key);continue;}
		if(!relocation_since.count(value.key))relocation_since[value.key]=timer;
		if(timer-relocation_since[value.key]<budget.food_relocation_confirm_ticks)
			continue;
		if(!worst||value.quality>worst->quality
		   ||(value.quality==worst->quality&&value.key<worst->key))
			worst=&value;
	}
	for(std::map<int,int>::iterator i=relocation_since.begin();
		i!=relocation_since.end();)
		if(!present.count(i->first))relocation_since.erase(i++);else ++i;
	for(std::set<int>::iterator i=relocation_destroy_issued.begin();
		i!=relocation_destroy_issued.end();)
		if(!present.count(*i))relocation_destroy_issued.erase(i++);else ++i;

	const bool safe=!budget.recovery_active&&snapshot.critical_food==0
		&&snapshot.own_buildings_under_attack==0&&snapshot.own_units_under_attack==0;

	if(relocation_target_building>=0)
	{
		const int target=relocation_target_building;
		if(!present.count(target))
		{
			// The old building is gone, by our order or otherwise: finished.
			emit_telemetry(runtime,"food_relocation_done",
				"\tbuilding_id="+telemetryText(target));
			finish_food_relocation();
			return;
		}
		// A deletion already in flight owns this handover until the old building
		// disappears. Keep protecting its replacement, without queuing it twice.
		if(food_building_pending_deletion(runtime,target))return;
		// Follow the newest planner action for this nomination.
		const DevelopmentAction* action=current_food_relocation();
		const bool underway=action&&(action->state==ParcelReserved
			||action->state==CreateIssued||action->state==SiteObserved);
		if(action&&action->state==Completed)
		{
			const WorldBuilding* replacement=world.building(action->buildingId);
			// Completed actions are historical: observe() no longer updates them.
			// Losing the replacement must release the nomination and permit a retry.
			if(!replacement||food_building_pending_deletion(runtime,action->buildingId))
			{
				emit_telemetry(runtime,"food_relocation_abandoned",
					"\tbuilding_id="+telemetryText(target)
					+"\treason="+(replacement?"replacement_deleting":"replacement_missing"));
				finish_food_relocation();
				return;
			}
			if(replacement&&!replacement->site&&relocation_completed_tick<0)
				relocation_completed_tick=timer;
			// Capacity is preserved by the replacement, so an attack does not
			// block the retirement the way it blocks a plain one; hunger does,
			// because the old building may still hold the stock people need.
			const bool hungry=budget.recovery_active||snapshot.critical_food>0;
			if(replacement&&!replacement->site
			   &&!relocation_destroy_issued.count(target))
			{
				// Never remove seats the population is still eating from. Seats
				// count for what their inn is actually supplied: the inn being
				// replaced is usually starving, and its seats with it.
				bool seatsRemain=true;
				const WorldBuilding* old=world.building(target);
				if(old&&profile_serves(old->buildingType,AIMaximaBuildings::Feeding,old->level))
				{

					std::array<long long,8> rates{};
					for(size_t b=0;b<world.buildings.size();++b)
					{
						const WorldBuilding& building=world.buildings[b];
						if(building.site || building.id==target
						   ||food_building_pending_deletion(runtime,building.id)
						   ||!profile_serves(building.buildingType,AIMaximaBuildings::Feeding,building.level))
							continue;
						const AIMaximaFoodLedger::ConsumerResult* supplied=
							ledger.consumer(building.id);
						const int coverage=supplied
							? std::min(100,std::max(0,supplied->coveragePercent)) : 100;
                        const auto* definition=profile_variant(building.buildingType,building.level);
                        rates[definition->feedingMask&7u]+=static_cast<long long>(development_feeding_visit_rate[definition->completedType])*coverage*strategy.economy.reliable_inn_percent/10000;
                    }
                    seatsRemain=aggregate_feeding_capacity(rates)>=snapshot.population;
				}
				if(hungry||!seatsRemain)
				{
					std::ostringstream fields;
					fields<<"\tbuilding_id="<<target
						<<"\treason="<<(hungry?"hungry":"seats");
					emit_telemetry(runtime,"food_relocation_deferred",fields.str());
					// A pair that cannot be resolved keeps both buildings and
					// frees the slot rather than blocking every later move.
					if(timer-relocation_completed_tick>=budget.food_relocation_cooldown_ticks)
					{
						emit_telemetry(runtime,"food_relocation_abandoned",
							"\tbuilding_id="+telemetryText(target)
							+"\treason=deferred");
						finish_food_relocation();
					}
				}
				else
				{
					runtime.add_management_order(new DestroyBuilding(target));
					relocation_destroy_issued.insert(target);
					std::ostringstream fields;
					fields<<"\tbuilding_id="<<target
						<<"\treplacement_id="<<action->buildingId
						<<"\taction_id="<<action->id;
					emit_telemetry(runtime,"food_relocation_destroy",fields.str());
				}
			}
			return;
		}
		if(underway)return;
		// No live action: the planner refused every site, the build failed, or
		// the planner never got to the offer before its window closed.
		const bool failed=action!=NULL;
		const bool refused=development_planner.relocationRefused(target);
		if(failed||refused
		   ||timer-relocation_target_since>=budget.food_relocation_offer_ticks)
		{
			std::ostringstream fields;
			fields<<"\tbuilding_id="<<target
				<<"\treason="<<(failed?lifecycleName(action->state)
					:refused?"refused":"no_site");
			emit_telemetry(runtime,"food_relocation_abandoned",fields.str());
			finish_food_relocation();
		}
		return;
	}

	if(!safe||!worst)return;
	if(timer-last_food_relocation_tick<budget.food_relocation_cooldown_ticks)return;
	relocation_target_building=worst->key;
	relocation_target_since=timer;
	development_planner.clearRelocationRefusal(worst->key);
	std::ostringstream fields;
	fields<<"\tbuilding_id="<<worst->key
		<<"\tkind="<<(worst->kind==AIMaximaFoodLedger::InnConsumer?"inn":"swarm")
		<<"\tquality="<<worst->quality<<"\tcoverage="<<worst->coveragePercent
		<<"\tclaimed="<<worst->claimed<<"\tdemand="<<worst->demand
		<<"\tconfirmed_ticks="<<(timer-relocation_since[worst->key]);
	emit_telemetry(runtime,"food_relocation_nominated",fields.str());
}

Labour::Policy Maxima::labour_policy() const
{
	// The seam where strategy would shape the labour plan. Every field is at its
	// default today: the one value this used to set, a material haul trip priced
	// from the carrier constants, was never read by the plan, so it was removed
	// rather than left looking meaningful.
	return Labour::Policy();
}


bool Maxima::labour_swimming_matters() const
{
	// Swimming is worth a worker's training time only where water separates the
	// colony from land or resources it could use.  Connected land does not make
	// water resources walk-accessible: Isles' bridges keep its topology score low
	// while most of its discovered algae still needs swimming.
	return environment.mobility_opportunity>=15
		|| known_algae_units>walk_accessible_algae_units;
}


Labour::Observation Maxima::observe_labour(Context& runtime) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	Labour::Observation result;
	const AIEngine::TeamView* team=&runtime.observedTeam();
	const bool swimming=labour_swimming_matters();
	std::vector<const AIEngine::BuildingView*> schools;
	for(int id=0; id<Building::MAX_COUNT; ++id)
	{
		const AIEngine::BuildingView* b=runtime.observation().buildingSlots(team->number)[id];
		if(!b || b->buildingState!=Building::ALIVE) continue;
		const bool site=AIEngine::ObservationQueries::buildingType(runtime.observation(),*b).isBuildingSite;
		const bool swarm=AIMaximaBuildings::serves(context.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*b),AIMaximaBuildings::Production);
		if(swarm && !site)
		{
			++result.swarms;
			result.swarmRequested+=b->maxUnitWorking;
		}
		else if(site)
			result.siteRequested+=std::max(0, b->desiredMaxUnitWorking);
		// A barracks being upgraded keeps counting as two seats: its warriors are
		// still coming, and a pause in births for every upgrade starves the army.
		if(site && AIMaximaBuildings::serves(context.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*b),AIMaximaBuildings::CombatTraining)
		   && b->constructionResultState==Building::UPGRADE)
			result.barracksSeats+=std::min(2,AIMaximaBuildings::completed(runtime.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*b))->maxUnitInside);
		if(site) continue;
		if(AIMaximaBuildings::serves(context.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*b),AIMaximaBuildings::CombatTraining))
			result.barracksSeats+=b->maxUnitInside;
		if(AIMaximaBuildings::serves(context.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*b),AIMaximaBuildings::Healing))
		{
			++result.hospitals;
			result.hospitalSeats+=b->maxUnitInside;
		}
		if(AIMaximaBuildings::serves(context.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*b),AIMaximaBuildings::Feeding))
		{
			++result.inns;
			result.innSeats+=b->maxUnitInside;
		}
		// Idle training reserves cannot pay off when units cannot learn. Existing
		// hospitals and barracks still provide healing, so only training seats go.
		bool trains=false;
		if(!runtime.observation().configuration->isUnitUpgradesDisabled())for(int ability=0;ability<NB_ABILITY;++ability){
		 const auto& t=AIEngine::ObservationQueries::buildingType(runtime.observation(),*b).semantics.training[ability];
		 if(t.enabled&&(t.unitMask&AIEngine::ObservationQueries::buildingType(runtime.observation(),*b).semantics.admittedUnitMask&(1u<<WORKER))&&(ability==WALK||ability==BUILD||ability==HARVEST||t.constructionLevel>0||(swimming&&ability==SWIM)))trains=true;
		}
		if(trains)
		{
			schools.push_back(b);
			result.trainingSlots+=b->maxUnitInside;
		}
	}
	for(int id=0; id<Unit::MAX_COUNT; ++id)
	{
		const AIEngine::UnitView* u=runtime.observation().unitSlots(team->number)[id];
		if(!u || u->isDead) continue;
		if(u->medical==Unit::MED_DAMAGED) ++result.hurtUnits;
		if(!runtime.observation().configuration->isHungerDisabled() && u->medical==Unit::MED_HUNGRY) ++result.hungryUnits;
		if(u->typeNum!=WORKER) continue;
		++result.workers;
		if(u->level[WALK]==0) ++result.untrainedWalkers;
		if(!runtime.observation().configuration->isHungerDisabled() && u->medical==Unit::MED_HUNGRY){++result.eating;continue;}
		if(u->medical==Unit::MED_DAMAGED){++result.hurt;continue;}
		if(u->activity==Unit::ACT_UPGRADING)
		{
			if(u->destinationPurpose==HEAL) ++result.hurt; else ++result.training;
			continue;
		}
		if(u->activity==Unit::ACT_RANDOM) ++result.idle;
		else if(const auto* attached=runtime.observation().building(u->attached))
		{
			const BuildingType* type=&AIEngine::ObservationQueries::buildingType(runtime.observation(),*attached);
			if(type->isBuildingSite) ++result.builders;
			else if(AIMaximaBuildings::serves(context.observation(),*type,AIMaximaBuildings::Production)) ++result.swarmCarriers;
			else if(AIMaximaBuildings::serves(context.observation(),*type,AIMaximaBuildings::Feeding)) ++result.innCarriers;
			else ++result.otherAssigned;
		}
		else ++result.otherAssigned;
		bool canTrain=false;
		for(size_t b=0;b<schools.size() && !canTrain;++b)
			for(int ability=0;ability<NB_ABILITY && !canTrain;++ability)
				if((ability!=SWIM || swimming) && AIEngine::ObservationQueries::needsTraining(*u,AIEngine::ObservationQueries::buildingType(runtime.observation(),*schools[b]).semantics.training[ability],ability))
					canTrain=true;
		if(canTrain) ++result.trainable;
	}
	return result;
}


void Maxima::manage_buildings(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	labour_observation=observe_labour(runtime);
	labour_plan=Labour::plan(labour_observation, labour_policy(), budget.swarm_workers);
	// Births take the labour that subsistence, the training reserve and
	// construction leave. Each swarm's loop still owns its request; the budget
	// only bounds the sum, from the requests the loops held after the last pass.
	// A trimmed swarm is not receiving what it asked for, so its loop holds
	// instead of winding up.
	std::vector<int> swarm_ids;
	std::vector<int> swarm_requests;
	{
		BuildingSearch swarms(runtime);
		swarms.add_condition(new NotUnderConstruction);
		for(building_search_iterator i=swarms.begin(); i!=swarms.end(); ++i)
			if(runtime.get_building_register().has_role(*i,AIMaximaBuildings::Production))
			{
				std::map<int,StaffingControl::State>::const_iterator state=
					staffing_control.find(*i);
				swarm_ids.push_back(*i);
				swarm_requests.push_back(state==staffing_control.end()
					? budget.staffing_new_swarm_workers : state->second.request);
			}
	}
	const int trimmed=labour_plan.uncapped ? 0
		: Labour::trimToCap(swarm_requests, labour_plan.swarmCap,
			std::max(1, budget.staffing_minimum_workers));
	swarm_allowance.clear();
	if(!labour_plan.uncapped)
		for(size_t i=0;i<swarm_ids.size();++i)
			swarm_allowance[swarm_ids[i]]=swarm_requests[i];
	BuildingSearch bs(runtime);
	bs.add_condition(new NotUnderConstruction);
	for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
	{
		if(runtime.get_building_register().has_role(*i,AIMaximaBuildings::Production))
		{
			manage_swarm(runtime, *i);
		}
		else if(runtime.get_building_register().has_role(*i,AIMaximaBuildings::Feeding))
		{
			manage_inn(runtime, *i);
		}
		else if(runtime.get_building_register().has_role(*i,AIMaximaBuildings::ProjectileDefense)
		   && runtime.get_building_register().get_assigned(*i)
			!=(explorer_defense_emergency()
				? strategy.staffing.completed_tower_emergency_workers
				: strategy.staffing.completed_tower_workers))
		{
			// A single carrier could not keep even one shot in the tower during the
			// FourSquares bomber wave. Two workers sustain the deterrent layer; four
			// replenish ammunition while an actual strike is in the colony.
			runtime.add_management_order(new AssignWorkers(explorer_defense_emergency()
				? strategy.staffing.completed_tower_emergency_workers
				: strategy.staffing.completed_tower_workers, *i));
		}
	}
	{
		std::ostringstream fields;
		fields<<"\tworkers="<<labour_observation.workers
			<<"\teating="<<labour_observation.eating
			<<"\thurt="<<labour_observation.hurt
			<<"\ttraining="<<labour_observation.training
			<<"\tidle="<<labour_observation.idle
			<<"\tswarm_carriers="<<labour_observation.swarmCarriers
			<<"\tinn_carriers="<<labour_observation.innCarriers
			<<"\tbuilders="<<labour_observation.builders
			<<"\tother="<<labour_observation.otherAssigned
			<<"\ttrainable="<<labour_observation.trainable
			<<"\ttraining_slots="<<labour_observation.trainingSlots
			<<"\tuntrained_walkers="<<labour_observation.untrainedWalkers
			<<"\tsite_requested="<<labour_observation.siteRequested
			<<"\treserve="<<labour_plan.trainingReserve
			<<"\tassignable="<<labour_plan.assignable
			<<"\tfunded_swarm_workers="<<budget.swarm_workers
			<<"\tswarm_cap="<<labour_plan.swarmCap
			<<"\tswarm_trimmed="<<trimmed;
		emit_telemetry(runtime,"labour_budget",fields.str());
	}
	// A destroyed building must not leave its control loop behind, or a later
	// building reusing the id would inherit a stranger's integral state.
	for(std::map<int,StaffingControl::State>::iterator entry=staffing_control.begin();
		entry!=staffing_control.end();)
	{
		if(!runtime.get_building_register().get_building(entry->first))
			staffing_control.erase(entry++);
		else ++entry;
	}
}


int Maxima::staff_building(Context& runtime, int id)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const int request=update_staffing_request(runtime, id);
	if(request!=runtime.get_building_register().get_assigned(id))
		runtime.add_management_order(new AssignWorkers(request, id));
	return request;
}


int Maxima::update_staffing_request(Context& runtime, int id)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const AIEngine::BuildingView* building=runtime.get_building_register().get_building(id);
	if(!building) return 0;
	StaffingControl::Policy policy;
	policy.windowSamples=budget.staffing_window_samples;
	policy.lowPermille=budget.staffing_low_permille;
	policy.highPermille=budget.staffing_high_permille;
	policy.slack=budget.staffing_slack;
	policy.minimumWorkers=budget.staffing_minimum_workers;
	policy.maximumWorkers=std::min({MAXIMA_MAX_UNIT_WORKING,budget.staffing_maximum_workers,AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).semantics.assignmentLimit});
	policy.minimumWorkers=std::min(policy.minimumWorkers,policy.maximumWorkers);
	policy.cooldownPasses=budget.staffing_cooldown_passes;
	// A building that has just been built starts where it is useful rather
	// than at one carrier: the loop needs several passes to climb, and an inn
	// or swarm is worth nothing until it is actually stocked. From here the
	// control loop owns the number and may raise or lower it normally.
	const bool fresh=staffing_control.find(id)==staffing_control.end();
	StaffingControl::State& state=staffing_control[id];
	if(fresh)
		state.request=AIMaximaBuildings::serves(context.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*building),AIMaximaBuildings::Production)
			? budget.staffing_new_swarm_workers : budget.staffing_new_inn_workers;
	const int previous=state.request;
	// Shared inventory is sampled once. The limiting required resource controls
	// staffing, regardless of which combination of services consumes it.
	int stock=1,capacity=1;bool selected=false;
	const auto* profile=profile_variant(AIMaximaBuildings::lineageRoot(runtime.observation(),building->typeNum),AIMaximaBuildings::lineagePosition(runtime.observation(),building->typeNum));
	if(profile)for(int r=0;r<8;++r)if(profile->operatingResources[r]>0&&AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).maxResource[r]>0){
	 if(!selected||static_cast<long long>(runtime.observation().buildingResources(*building)[r])*capacity<static_cast<long long>(stock)*AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).maxResource[r]){stock=runtime.observation().buildingResources(*building)[r];capacity=AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).maxResource[r];selected=true;}
	}
	if(!selected){state.request=0;return 0;}
	const int request=StaffingControl::update(state, policy,stock,capacity,
		runtime.get_building_register().get_enrolled(id));
	if(request!=previous)
	{
		std::ostringstream fields;
		fields<<"\tbuilding_id="<<id<<"\tworkers="<<request
			<<"\tprevious="<<previous
			<<"\tcorn="<<runtime.observation().buildingResources(*building)[WHEAT]
			<<"\tcapacity="<<AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).maxResource[WHEAT]
			<<"\tfill_average="<<state.cornAverage
			<<"\tenrolled_average="<<state.enrolledAverage;
		emit_telemetry(runtime,"staffing_control",fields.str());
	}
	return request;
}


void Maxima::manage_inn(Context& runtime, int id)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	staff_building(runtime, id);
}


long long Maxima::nearby_farm_capacity(Context& runtime, int id,
	std::set<int>* shared_tiles)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const AIEngine::BuildingView* building=runtime.get_building_register().get_building(id);
	if(!building) return 0;
	initialize_farming_cache(runtime);
	const AIEngine::AIWorldView* map=&runtime.observation();
	return reachableFoodCapacity(map, building, runtime.observedTeam().mask,
		budget.can_swim, budget.swarm_supply_radius, fertility_cache,
		&applied_farm_protection_mask, shared_tiles,
		strategy.farming.wheat_stock_horizon_ticks);
}


void Maxima::manage_swarm(Context& runtime, int id)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	//Get some statistics
	const TeamStat* stat=&runtime.observedTeam().statistics;
	int total_explorers=stat->numberUnitPerType[EXPLORER];
	if(stat->totalUnit == 0)
		return;

	// Staffing is the building's own business: it regulates its carriers from
	// its own wheat stock. The labour budget only bounds the sum over swarms.
	{
		int request=update_staffing_request(runtime, id);
		std::map<int,int>::const_iterator allowed=swarm_allowance.find(id);
		if(allowed!=swarm_allowance.end()) request=std::min(request, allowed->second);
		if(request!=runtime.get_building_register().get_assigned(id))
			runtime.add_management_order(new AssignWorkers(request, id));
	}

	int worker_ratio=budget.worker_ratio;

	// Every swarm shares the explorer mix until the colony-wide target is met.
	int explorer_ratio=total_explorers<budget.desired_explorers
		? budget.explorer_ratio : 0;

	///Warriors are constructed during the war preperation phase
	int warrior_ratio=stat->numberUnitPerType[WARRIOR]<budget.desired_warriors
		? budget.warrior_ratio : 0;

	// Birth funding remains a colony decision even though staffing is local: a
	// zero budget pauses production, which carriers alone would not do.
	if(budget.swarm_workers<=0)
		worker_ratio=explorer_ratio=warrior_ratio=0;

	//Change the ratio of the swarm when its finished
	ManagementOrder* mo_ratios=new ChangeSwarm(worker_ratio, explorer_ratio, warrior_ratio, id);
	runtime.add_management_order(mo_ratios);
}





AIMaximaFruit::Field Maxima::collect_fruit_field(Context& runtime) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	AIMaximaFruit::Field field;
	if(!strategy.fruit.enabled || !runtime.is_fruit_on_map())return field;
	const AIEngine::AIWorldView* map=&runtime.observation();
	field.width=(*map).width;
	field.height=(*map).height;
	field.tiles.resize(field.width*field.height);
	for(int y=0;y<field.height;++y)for(int x=0;x<field.width;++x)
	{
		AIMaximaFruit::Tile& tile=field.tiles[field.index(x,y)];
		const auto cell=AIEngine::ObservationQueries::spatialTile((*map),x,y);
		const auto& terrain=AIEngine::ObservationQueries::terrain((*map),y*field.width+x);
		tile.passable=cell.building==NOGBID && cell.resource.type==NO_RES_TYPE
			&& !(cell.forbidden&runtime.observedTeam().mask)
			&& (terrain.walkable || (budget.can_swim && terrain.swimmable));
		tile.visible=cell.resource.amount>0 && (cell.visible&runtime.observedTeam().allies)!=0;
		if(cell.resource.type>=CHERRY && cell.resource.type<=PRUNE
		   && (cell.discovered&runtime.observedTeam().allies)!=0)
			tile.variety=cell.resource.type-CHERRY;
	}
	bool knownFruit=false;
	for(const auto& tile:field.tiles)knownFruit=knownFruit || tile.variety>=0;
	if(!knownFruit)return AIMaximaFruit::Field();
	// Completed friendly buildings provide persistent vision using the same
	// rectangular footprint as Building::setMapDiscovered.
	for(const auto& entry:runtime.get_building_register().found())
	{
		const AIEngine::BuildingView* building=runtime.get_building_register().get_building(entry.first);
		if(!building || AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).isVirtual || AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).isBuildingSite)continue;
		const int radius=AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).viewingRange;
		for(int dy=-radius;dy<AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).height+radius;++dy)
			for(int dx=-radius;dx<AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).width+radius;++dx)
				field.tiles[field.index(building->posX+dx,building->posY+dy)].buildingVision=true;
	}
	field.build();
	return field;
}

void Maxima::update_fruit_flags(AIMaximaRuntime::Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	// Reconcile per resource against the serialized runtime, rather than trusting
	// a one-shot Boolean. Pending searches count too, including after a load.
	std::set<int> other_flags(explorer_attack_flags.begin(), explorer_attack_flags.end());
	for(const Recon::ReconMission& mission:reconnaissance.report().missions)
		other_flags.insert(mission.flagId);

	const auto field=collect_fruit_field(runtime);
	int selected[3]={-1,-1,-1},varieties[3]={0,0,0};
	bool supply=false;
	for(const auto& entry:runtime.get_building_register().found())
	{
		const AIEngine::BuildingView* inn=runtime.get_building_register().get_building(entry.first);
		if(!inn || !AIMaximaBuildings::serves(context.observation(),AIEngine::ObservationQueries::buildingType(runtime.observation(),*inn),AIMaximaBuildings::Feeding)
		   || AIEngine::ObservationQueries::buildingType(runtime.observation(),*inn).isBuildingSite)continue;
		const auto assessment=field.assessBuilding(inn->posX,inn->posY,
			AIEngine::ObservationQueries::buildingType(runtime.observation(),*inn).width,AIEngine::ObservationQueries::buildingType(runtime.observation(),*inn).height);
		supply=supply || assessment.collectable || assessment.covered;
		for(int v=0;v<3;++v)
		{
			if(!(assessment.available&(1u<<v)))continue;
			supply=supply || runtime.observation().buildingResources(*inn)[CHERRY+v]>0;
			if(!(assessment.covered&(1u<<v)) && assessment.varietyCount>varieties[v])
			{
				selected[v]=assessment.source[v];
				varieties[v]=assessment.varietyCount;
			}
		}
	}
	for(int v=0;v<3;++v)
	{
		auto flags=runtime.resource_flags(CHERRY+v);
		flags.erase(std::remove_if(flags.begin(),flags.end(),
			[&](int id){return other_flags.count(id)!=0;}),flags.end());
		const int source=budget.fruit_active?selected[v]:-1;
		const size_t keep=(source>=0 && strategy.fruit.units_per_flag>0)?1:0;
		for(size_t n=keep;n<flags.size();++n)
		{
			runtime.cancel_or_destroy_building(flags[n],1u<<EXPLORER);
		}
		if(!keep)continue;
		const int x=source%field.width,y=source/field.width;
		if(!flags.empty())
		{
            if(runtime.begin_attraction(flags[0],1u<<EXPLORER)) {
                runtime.add_management_order(new AssignWorkers(budget.fruit_units_per_flag,flags[0]));
                runtime.add_management_order(new ChangeFlagSize(budget.fruit_flag_radius,flags[0]));
            }
			int oldX,oldY;
			if(runtime.get_building_position(flags[0],oldX,oldY) && (x!=oldX || y!=oldY))
				runtime.add_management_order(new ChangeFlagPosition(x,y,flags[0]));
			continue;
		}
		GradientInfo resource;resource.add_source(new Entities::Resource(CHERRY+v));
		BuildingOrder* order=new BuildingOrder(AIMaximaBuildings::ExploreAttraction,
			budget.fruit_units_per_flag);
		order->add_constraint(new SinglePosition(x,y));
		order->add_constraint(new MaximumDistance(resource,0));
		const int id=runtime.add_building_order(order);
		runtime.add_management_order(new ChangeFlagSize(budget.fruit_flag_radius,id));
	}
	// Supply maintained by buildings also merits advertising. mV remains
	// untouched; fV shares the inns that can recruit visitors.
	for(enemy_team_iterator i(runtime);i!=enemy_team_iterator();++i)
		runtime.add_management_order(new ChangeAlliances(*i,KeepValue,KeepValue,
			KeepValue,budget.fruit_active && supply?SetValue:ClearValue,KeepValue));
}

}

std::optional<Uint64> AIMaxima::Maxima::retainedQueryVectorBytes() const
{
    Uint64 bytes = context.get_gradient_manager().retainedVectorBytes()
        + development_planner.retainedQueryVectorBytes()
        + fertility_cache.values().capacity() * sizeof(uint32_t);
    for (const auto* mask : {&farming_shoreline_mask, &farming_cardinal_shoreline_mask,
            &applied_farm_protection_mask, &applied_maintenance_clearing_mask,
            &maintenance_circulation_mask, &wood_firebreak_mask, &farm_protection_mask,
            &wheat_farm_protection_mask})
        bytes += mask->capacity() * sizeof(Uint8);
    return bytes;
}
