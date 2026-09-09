#include "AIMaximaContinuation.h"
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

#include "AIMaxima.h"
#include "AIMaximaStaffing.h"
#include "AIMaximaSwarmController.h"
#include "AIMaximaFoodSupply.h"
#include "GlobalContainer.h"
#include "FormatableString.h"
#include "boost/lexical_cast.hpp"
#include "Utilities.h"
#include "Game.h"
#include "Unit.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <deque>
#include <set>
#include <sstream>
#include <vector>

using namespace AIMaximaRuntime;
using namespace AIMaximaRuntime::Gradients;
using namespace AIMaximaRuntime::Construction;
using namespace AIMaximaRuntime::Management;
using namespace AIMaximaRuntime::Conditions;
using namespace AIMaximaRuntime::SearchTools;

namespace AIMaxima
{

namespace
{
	// Let default explorer movement reveal most of the map before active scouting.
	const int ACTIVE_RECON_MIN_EXPLORED_PERCENT=80;
	const int PREEMPTIVE_UNREACHABLE=-1;

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

	bool building_currently_visible(Player* player, const Building* building)
	{
		if(!player || !player->map || !building)
			return false;
		for(int dy=0; dy<building->type->height; ++dy)
			for(int dx=0; dx<building->type->width; ++dx)
				if(player->map->isFOWDiscovered(building->posX+dx,
					building->posY+dy, player->team->me))
					return true;
		return false;
	}

	bool remembered_footprint_currently_visible(Player* player,
		const Recon::BuildingSighting& building)
	{
		if(!player || !player->map)
			return false;
		for(int dy=0; dy<building.height; ++dy)
			for(int dx=0; dx<building.width; ++dx)
				if(!player->map->isFOWDiscovered(building.x+dx,
					building.y+dy, player->team->me))
					return false;
		return true;
	}

	int reconnaissance_building_value(int type,
		const MaximaStrategy::Reconnaissance& policy)
	{
		switch(type)
		{
			case IntBuildingType::SWARM_BUILDING: return policy.swarm_building_value;
			case IntBuildingType::FOOD_BUILDING: return policy.inn_building_value;
			case IntBuildingType::ATTACK_BUILDING: return policy.barracks_building_value;
			case IntBuildingType::SCIENCE_BUILDING: return policy.school_building_value;
			default: return policy.default_building_value;
		}
	}

	int tactical_building_value(int type, const MaximaStrategy::Tactics& policy)
	{
		switch(type)
		{
			case IntBuildingType::SWARM_BUILDING: return policy.target_swarm_value;
			case IntBuildingType::FOOD_BUILDING: return policy.target_food_value;
			case IntBuildingType::ATTACK_BUILDING: return policy.target_barracks_value;
			case IntBuildingType::SCIENCE_BUILDING: return policy.target_school_value;
			case IntBuildingType::HEAL_BUILDING: return policy.target_hospital_value;
			default: return policy.target_default_value;
		}
	}

	bool tactical_warrior_available(const Unit* warrior,
		const Building* continuingFlag)
	{
		// Tactical flags require level 1 in both combat abilities (the runtime's
		// ChangeFlagMinimumLevel(2)). Match the engine's subscription rules.
		if(!warrior || warrior->typeNum!=WARRIOR || warrior->isDead
		   || warrior->medical!=Unit::MED_FREE
		   || std::min(warrior->level[ATTACK_SPEED],
			warrior->level[ATTACK_STRENGTH])<1)
			return false;
		if(continuingFlag && warrior->attachedBuilding==continuingFlag)
			return true;
		return !warrior->attachedBuilding && warrior->activity==Unit::ACT_RANDOM
			&& warrior->movement!=Unit::MOV_ATTACKING_TARGET;
	}

	int warrior_power(const Unit* warrior)
	{
		return std::max(1, warrior->getRealAttackStrength()
			*warrior->performance[ATTACK_SPEED]*warrior->hp
			/std::max(1, warrior->performance[HP]));
	}

	// Label each traversable component once per decision. A route from an
	// outpost proves nothing about warriors stranded in another component.
	class TacticalReachability
	{
	public:
		explicit TacticalReachability(Map* map) : map(map) {}
		std::vector<int> powersAt(Team* team, const Building* continuingFlag,
			int x, int y, int cap, bool swimmersOnly=false)
		{
			std::vector<int> powers;
			const int target=map->normalizeY(y)*map->getW()+map->normalizeX(x);
			for(int id=0; id<Unit::MAX_COUNT; ++id)
			{
				const Unit* warrior=team->myUnits[id];

				if(!tactical_warrior_available(warrior,continuingFlag)) continue;
				const bool swimming=warrior->performance[SWIM]>0;
				if(swimmersOnly && !swimming)
				{
					continue;
				}
				if(components[swimming].empty()) label(swimming);
				const std::vector<int>& field=components[swimming];
				const int origin=map->normalizeY(warrior->posY)*map->getW()
					+map->normalizeX(warrior->posX);
				if(field[target]>=0 && field[origin]==field[target])
					powers.push_back(warrior_power(warrior));

			}
			std::sort(powers.begin(),powers.end(),std::greater<int>());
			if(int(powers.size())>cap) powers.resize(std::max(0,cap));
			return powers;
		}
	private:
		Map* map;
		std::vector<int> components[2];
		void label(bool swimming)
		{
			const int w=map->getW(), h=map->getH();
			std::vector<int>& field=components[swimming];
			field.assign(w*h,-1);
			for(int i=0; i<w*h; ++i)
				if(map->isResource(i%w,i/w) || (!swimming && map->isWater(i%w,i/w)))
					field[i]=-2;
			std::vector<int> queue;
			for(int start=0; start<w*h; ++start)
			{
				if(field[start]!=-1) continue;
				queue.clear(); queue.push_back(start); field[start]=start;
				for(size_t head=0; head<queue.size(); ++head)
				{
					const int x=queue[head]%w, y=queue[head]/w;
					for(int dy=-1; dy<=1; ++dy) for(int dx=-1; dx<=1; ++dx)
					{
						const int next=((y+dy+h)%h)*w+(x+dx+w)%w;
						if(field[next]==-1) { field[next]=start; queue.push_back(next); }
					}
				}
			}
		}
	};

	struct AlliedPressure
	{
		AlliedPressure() : warriors(0), power(0) {}
		int warriors;
		int power;
	};

	struct ReliefCandidate
	{
		ReliefCandidate()
			: team(-1), x(0), y(0), score(INT_MIN), requested(0),
			  enemies(0), enemyPower(0), allies(0), alliedPower(0), route(-1)
		{}
		int team;
		int x;
		int y;
		int score;
		int requested;
		int enemies;
		int enemyPower;
		int allies;
		int alliedPower;
		int route;
	};

	AlliedPressure visible_allied_pressure_near(Player* player, int x, int y,
		int radius)
	{
		AlliedPressure result;
		if(!player || !player->game || !player->map || !player->team)
			return result;
		const int radius_square=radius*radius;
		for(int team=0; team<Team::MAX_COUNT; ++team)
		{
			Team* allied_team=player->game->teams[team];
			if(!allied_team || allied_team==player->team
			   || !(player->team->allies&allied_team->me))
				continue;
			for(int unit_id=0; unit_id<Unit::MAX_COUNT; ++unit_id)
			{
				Unit* allied=allied_team->myUnits[unit_id];
				if(!allied || allied->typeNum!=WARRIOR
				   || !player->map->isFOWDiscovered(allied->posX, allied->posY,
					player->team->me)
				   || player->map->warpDistSquare(x, y, allied->posX, allied->posY)
					>radius_square)
					continue;
				++result.warriors;
				result.power+=warrior_power(allied);
			}
		}
		return result;
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

	struct PreemptiveBuilding
	{
		PreemptiveBuilding(int team, Building* building)
			: team(team), x(building->posX), y(building->posY),
			  width(building->type->width), height(building->type->height),
			  gid(building->gid)
		{}

		int team;
		int x;
		int y;
		int width;
		int height;
		int gid;
	};

	void add_preemptive_hash(Uint32& signature, Uint32 value)
	{
		signature^=value;
		signature*=16777619u;
	}

	void mark_building_footprint(const PreemptiveBuilding& building, int w,
		int h, std::vector<Uint8>& walkable)
	{
		for(int dy=0; dy<building.height; ++dy)
			for(int dx=0; dx<building.width; ++dx)
				walkable[((building.y+dy+h)%h)*w
					+((building.x+dx+w)%w)]=0;
	}

	void collect_building_perimeter(const PreemptiveBuilding& building, int w,
		int h, const std::vector<Uint8>& walkable, std::vector<int>& sources)
	{
		std::set<int> unique;
		for(int dy=-1; dy<=building.height; ++dy)
		{
			for(int dx=-1; dx<=building.width; ++dx)
			{
				if(dx!=-1 && dx!=building.width
				   && dy!=-1 && dy!=building.height)
					continue;
				const int x=(building.x+dx+w)%w;
				const int y=(building.y+dy+h)%h;
				const int index=y*w+x;
				if(walkable[index])
					unique.insert(index);
			}
		}
		sources.insert(sources.end(), unique.begin(), unique.end());
	}

	void compute_preemptive_distance_field(int w, int h,
		const std::vector<Uint8>& walkable, const std::vector<int>& sources,
		std::vector<int>& distances)
	{
		distances.assign(w*h, PREEMPTIVE_UNREACHABLE);
		std::deque<int> queue;
		for(std::vector<int>::const_iterator source=sources.begin();
			source!=sources.end(); ++source)
		{
			if(*source<0 || *source>=w*h || !walkable[*source]
			   || distances[*source]!=PREEMPTIVE_UNREACHABLE)
				continue;
			distances[*source]=0;
			queue.push_back(*source);
		}
		while(!queue.empty())
		{
			const int index=queue.front();
			queue.pop_front();
			const int x=index%w;
			const int y=index/w;
			for(int dy=-1; dy<=1; ++dy)
			{
				for(int dx=-1; dx<=1; ++dx)
				{
					if(dx==0 && dy==0)
						continue;
					const int neighbor=((y+dy+h)%h)*w+((x+dx+w)%w);
					if(walkable[neighbor]
					   && distances[neighbor]==PREEMPTIVE_UNREACHABLE)
					{
						distances[neighbor]=distances[index]+1;
						queue.push_back(neighbor);
					}
				}
			}
		}
	}

	struct ResourceAccessObservation
	{
		ResourceAccessObservation()
			: knownAlgaeUnits(0), walkingAlgaeUnits(0), swimmingAlgaeUnits(0),
			  accessibleAlgaeTiles(0), accessibleAlgaeUnits(0),
			  accessibleCornTiles(0), accessibleWoodTiles(0), accessibleStoneTiles(0)
		{}

		int knownAlgaeUnits;
		int walkingAlgaeUnits;
		int swimmingAlgaeUnits;
		int accessibleAlgaeTiles;
		int accessibleAlgaeUnits;
		int accessibleCornTiles;
		int accessibleWoodTiles;
		int accessibleStoneTiles;
	};

	void collect_worker_sources(Player* player, bool swimming,
		const std::vector<Uint8>& walkable, std::vector<int>& sources)
	{
		Map* map=player->map;
		const int width=map->getW();
		const int height=map->getH();
		for(int id=0; id<Unit::MAX_COUNT; ++id)
		{
			Unit* worker=player->team->myUnits[id];
			if(!worker || worker->typeNum!=WORKER
			   || (swimming && worker->performance[SWIM]<=0))
				continue;
			const int x=map->normalizeX(worker->posX);
			const int y=map->normalizeY(worker->posY);
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

	ResourceAccessObservation observe_resource_access(Player* player,
		const std::vector<Uint8>& localTiles)
	{
		ResourceAccessObservation result;
		Map* map=player->map;
		const int width=map->getW();
		const int height=map->getH();
		std::vector<Uint8> walking(width*height, 0);
		std::vector<Uint8> swimming(width*height, 0);
		for(int y=0; y<height; ++y)
			for(int x=0; x<width; ++x)
			{
				const int index=y*width+x;
				const Tile& tile=map->getTile(x, y);
				const bool clear=map->isMapDiscovered(x, y, player->team->me)
					&& !(tile.forbidden&player->team->me)
					&& tile.building==NOGBID
					&& tile.resource.type==NO_RES_TYPE;
				swimming[index]=clear;
				walking[index]=clear && !map->isWater(x, y);
			}

		std::vector<int> walkingSources;
		std::vector<int> swimmingSources;
		collect_worker_sources(player, false, walking, walkingSources);
		collect_worker_sources(player, true, swimming, swimmingSources);
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
				const Tile& tile=map->getTile(x, y);
				if(!localTiles[index]
				   || !map->isMapDiscovered(x, y, player->team->me)
				   || tile.resource.amount<=0
				   || (tile.resource.type!=ALGA && tile.resource.type!=CORN
					   && tile.resource.type!=WOOD && tile.resource.type!=STONE))
					continue;
				if(tile.resource.type==ALGA)
					result.knownAlgaeUnits+=tile.resource.amount;
				if(tile.forbidden&player->team->me)
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
				if(tile.resource.type==ALGA && walkingReach)
					result.walkingAlgaeUnits+=tile.resource.amount;
				if(tile.resource.type==ALGA && swimmingReach)
					result.swimmingAlgaeUnits+=tile.resource.amount;
				if(walkingReach || swimmingReach)
				{
					switch(tile.resource.type)
					{
						case CORN: ++result.accessibleCornTiles; break;
						case WOOD: ++result.accessibleWoodTiles; break;
						case STONE: ++result.accessibleStoneTiles; break;
						case ALGA:
							++result.accessibleAlgaeTiles;
							result.accessibleAlgaeUnits+=tile.resource.amount;
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
{}

Maxima::StrategicTrends::StrategicTrends()
	: population(0), workers(0), warriors(0), food_pressure(0), colony_pressure(0)
{}

Maxima::ClearedEnemySite::ClearedEnemySite()
	: x(0), y(0), confirmedTick(0)
{}

Maxima::ClearedEnemySite::ClearedEnemySite(int siteX, int siteY, int tick)
	: x(siteX), y(siteY), confirmedTick(tick)
{}

Maxima::DirectorPlan::DirectorPlan()
	: construction_sites(1), desired_inns(2), desired_swarms(1),
	  desired_barracks(0), desired_schools(0), desired_pools(0),
	  desired_racetracks(0), desired_hospitals(0), desired_towers(0), swarm_workers(3),
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
	  second_prestige_population_min(0), inn_adaptive_staffing_enabled(true),
	  swarm_retirement_enabled(true),
	  resource_tracker_samples(1), swarm_supply_radius(12),
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
	  campaign_stall_ticks(1), campaign_retreat_cooldown_ticks(1),
	  target_switch_margin(0), tower_barrier_bonus(0),
	  preemptive_recompute_ticks(1), preemptive_inner_distance(0),
	  preemptive_band_width(0), preemptive_path_slack(0),
	  preemptive_probe_radius(0), preemptive_cross_section_max(0),
	  preemptive_zone_radius(0), preemptive_zone_max(0),
	  reconnaissance_suspended(false), reconnaissance_flag_radius(1),
	  reconnaissance_review_interval(1), reconnaissance_economic_watch_revisit(1),
	  farming_normal_interval(1), farming_urgent_interval(1),
	  farming_enabled(true), farming_protection_enabled(true),
	  farming_barrier_enabled(true), farming_coastal_porosity_enabled(true),
	  farming_gate_clearing_enabled(true), farming_maintenance_clearing_enabled(true),
	  farming_resource_preserving_circulation_enabled(true),
	  farming_wheat_invasion_clearing_enabled(true),
	  farming_wood_firebreak_enabled(true),
	  farming_proactive_clearing_enabled(true),
	  farming_urgent(false), farming_wood_pressure(0),
	  farming_minimum_wood_fertility(0), farming_allow_proactive_clearing(false),
	  farming_clearing_for_placement(false),
	  farming_min_workers_for_clearing(0),
	  farming_clearing_cooldown(1), farming_clearing_duration(1),
	  farming_clearing_quota(0), farming_gate_clearing_radius(1),
	  farming_management_radius(0), farming_wheat_fertility_min(0), farming_wood_fertility_base_percent(0),
	  farming_wood_fertility_pressure_percent(0), farming_wood_pressure_base(0),
	  farming_wood_pressure_space_divisor(1), farming_wood_pressure_supply_divisor(1),
	  farming_wood_pressure_construction_divisor(1),
	  farming_wood_pressure_growth_divisor(1), farming_economic_envelope_radius(0),
	  barrier_topology_interval(1), farming_gate_relocation_penalty_cap(0),
	  priority_inns(50), priority_swarms(40), priority_barracks(30), priority_schools(25),
	  priority_pools(25), priority_racetracks(20), priority_hospitals(30),
	  priority_towers(20), tactics_enabled(true), raid_enabled(true),
	  siege_enabled(true), siege_target_lock_enabled(true), teamplay_enabled(true),
	  teamplay_defense_enabled(true), tactical_kind(Tactics::MissionNone),
	  tactical_target_team(-1), tactical_dig_out_team(-1),
	  tactical_target_gid(-1), tactical_target_x(0),
	  tactical_target_y(0), tactical_candidate_score(INT_MIN),
	  tactical_requested_force(0), tactical_minimum_force(0),
	  tactical_contact_visible(false), tactical_allied_player_pressure(false),
	  tactical_allied_target_pressure(false), tactical_visible_enemy_warriors(0),
	  tactical_visible_enemy_power(0), tactical_allied_warriors(0),
	  tactical_allied_power(0), tactical_route_distance(-1),
	  tactical_review_interval(100), tactical_rally_radius(2),
	  tactical_siege_radius(6), tactical_siege_muster_percent(50),
	  tactical_siege_strength_percent(125), tactical_siege_casualty_percent(40),
	  tactical_siege_threat_radius(12), tactical_target_lock_ticks(15000),
	  raid_flag_radius(2), raid_muster_percent(50), raid_muster_timeout(1500),
	  raid_contact_ttl(300), raid_max_engagement(2400),
	  raid_casualty_percent(25), raid_survivor_min(4), raid_cooldown(2000),
	  raid_defender_min(2), raid_defender_percent(50), raid_building_buffer(3),
	  raid_tower_buffer(2), raid_retarget_margin(60), relief_contact_ttl(500),
	  relief_max_engagement(2400), relief_cooldown(1000),
	  relief_follow_radius(6), relief_retarget_margin(60)
{
	for(int level=0; level<3; ++level)
	{
		inn_low_corn_threshold[level]=0;
		inn_normal_workers[level]=0;
		inn_low_corn_workers[level]=0;
	}
}

Maxima::PolicyBid::PolicyBid()
	: utility(0), construction_sites(0), desired_inns(0), desired_swarms(0),
	  desired_barracks(0), desired_schools(0), desired_pools(0),
	  desired_racetracks(0), desired_hospitals(0), desired_towers(0),
	  swarm_workers(0), worker_ratio(0), explorer_ratio(0), warrior_ratio(0),
	  desired_explorers(0), desired_warriors(0), defense_reserve(0),
	  attack_flags(0), attack_units(0), request_upgrades(false)
{}

Maxima::EnvironmentModel::EnvironmentModel()
	: known_tiles(0), accessible_corn(0), accessible_wood(0),
	  accessible_stone(0), accessible_algae(0), buildable_tiles(0),
	  water_tiles(0), feeding_capacity(0), food_headroom(70), resource_capacity(50), space_capacity(50),
	  food_security(70), abundance(50), terrain_abundance(50),
	  connected_abundance(50), mobility_opportunity(0),
	  economic_momentum(50), mobility_constraint(0),
	  topology_complexity(0), threat_pressure(0), confidence(0)
{}

Maxima::StrategicDemands::StrategicDemands()
	: survival(0), food(30), growth(50), expansion(50), access(20), technology(30),
	  mobility(0), military(20), aggression(0)
{}

 Maxima::OpponentAssessment::OpponentAssessment()
	: alive(false), visible_warriors(0), estimated_warriors(0),
	  last_observed_warriors(0), visible_explorers(0), visible_buildings(0),
	  known_buildings(0), reachable_buildings(0), strategic_value(0),
	  nearest_building(INT_MAX), score(INT_MIN), last_seen_tick(-1000000),
	  last_force_seen_tick(-1000000), last_building_seen_tick(-1000000),
	  intel_confidence(0)
{}

Maxima::CampaignPlan::CampaignPlan()
	: state(CampaignIdle), target_team(-1), started_tick(0),
	  last_progress_tick(0), last_target_buildings(0), buildings_destroyed(0),
	  cooldown_until(0)
{}

void Maxima::StrategyDirector::evaluate(Maxima& owner, Context& echo)
{
	if(initialized && !dirty
	   && owner.snapshot.tick==owner.timer)
		return;
	owner.evaluate_strategy(echo);
	committed();
}



Maxima::StrategicSnapshot Maxima::collect_snapshot(Context& echo)
{
	StrategicSnapshot state;
	state.tick=timer;
	TeamStat* stat=echo.player->team->stats.getLatestStat();
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
	state.need_heal=stat->needHeal;
	state.buildings=stat->totalBuilding;
	state.swarms=stat->numberBuildingPerType[IntBuildingType::SWARM_BUILDING];
	state.inns=stat->numberBuildingPerType[IntBuildingType::FOOD_BUILDING];
	state.inn_level1=stat->numberBuildingPerTypePerLevel[IntBuildingType::FOOD_BUILDING][1];
	state.inn_level2=stat->numberBuildingPerTypePerLevel[IntBuildingType::FOOD_BUILDING][3];
	state.inn_level3=stat->numberBuildingPerTypePerLevel[IntBuildingType::FOOD_BUILDING][5];
	state.barracks=stat->numberBuildingPerType[IntBuildingType::ATTACK_BUILDING];
	state.schools=stat->numberBuildingPerType[IntBuildingType::SCIENCE_BUILDING];
	state.school_level1=stat->numberBuildingPerTypePerLevel[IntBuildingType::SCIENCE_BUILDING][1];
	state.school_level2=stat->numberBuildingPerTypePerLevel[IntBuildingType::SCIENCE_BUILDING][3];
	state.school_level3=stat->numberBuildingPerTypePerLevel[IntBuildingType::SCIENCE_BUILDING][5];
	state.pools=stat->numberBuildingPerType[IntBuildingType::SWIMSPEED_BUILDING];
	state.hospitals=stat->numberBuildingPerType[IntBuildingType::HEAL_BUILDING];
	state.racetracks=stat->numberBuildingPerType[IntBuildingType::WALKSPEED_BUILDING];
	state.towers=stat->numberBuildingPerType[IntBuildingType::DEFENSE_BUILDING];
	state.total_hp=stat->totalHP;
	state.attack_power=stat->totalAttackPower;
	state.prestige=echo.player->team->prestige;
	for(enemy_team_iterator enemy(echo); enemy!=enemy_team_iterator(); ++enemy)
	{
		const Team* hostile=echo.player->game->teams[*enemy];
		if(hostile && hostile->isAlive)
			state.enemy_prestige+=hostile->prestige;
	}

	for(int level=1; level<4; ++level)
	{
		state.trained_workers+=stat->upgradeStatePerType[WORKER][BUILD][level];
		if(level>=2)
			state.trained_workers_level2+=
				stat->upgradeStatePerType[WORKER][BUILD][level];
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
		Unit* unit=echo.player->team->myUnits[i];
		if(unit && unit->underAttackTimer)
			state.own_units_under_attack+=1;
		if(unit && unit->typeNum==EXPLORER
		   && unit->performance[SWIM]>0
		   && unit->performance[MAGIC_ATTACK_GROUND]>0)
			state.amphibious_attack_explorers+=1;
	}

	for(int i=0; i<Building::MAX_COUNT; ++i)
	{
		Building* building=echo.player->team->myBuildings[i];
		if(!building)
			continue;
		// Only a level-zero site is new construction. Upgrade sites retain the
		// building's previous level and are governed by the separate quotas.
		if(building->type->isBuildingSite&&building->type->level==0)
			state.building_sites+=1;
		if(building->type->shortTypeNum==IntBuildingType::SWARM_BUILDING
		   && !building->type->isBuildingSite
		   && building->constructionResultState==Building::NO_CONSTRUCTION)
			++state.completed_swarms;
		if(building->underAttackTimer)
			state.own_buildings_under_attack+=1;
		if(!building->type->isBuildingSite
		   && building->type->shortTypeNum==IntBuildingType::DEFENSE_BUILDING)
		{
			state.tower_stone+=building->resources[STONE];
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
	return state;
}

void Maxima::update_trends()
{
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

void Maxima::initialize_topology_profile(Context& echo)
{
	if(topology_initialized)
		return;
	MapInfo map(echo);
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
			land+=here ? 0 : 1;
			grass+=map.is_grass(x, y) ? 1 : 0;
			buildable+=map.is_grass(x, y) && !resource ? 1 : 0;
			corn+=map.is_resource(x, y, CORN) ? 1 : 0;
			wood+=map.is_resource(x, y, WOOD) ? 1 : 0;
			stone+=map.is_resource(x, y, STONE) ? 1 : 0;
			algae+=map.is_resource(x, y, ALGA) ? 1 : 0;
			fruit+=map.is_resource(x, y, CHERRY)
				|| map.is_resource(x, y, ORANGE)
				|| map.is_resource(x, y, PRUNE) ? 1 : 0;
			shoreline_edges+=here!=map.is_water((x+1)%width, y) ? 1 : 0;
			shoreline_edges+=here!=map.is_water(x, (y+1)%height) ? 1 : 0;
			if(!here)
			{
				const bool horizontal=!map.is_water((x+width-1)%width, y)
					&& !map.is_water((x+1)%width, y);
				const bool vertical=!map.is_water(x, (y+height-1)%height)
					&& !map.is_water(x, (y+1)%height);
				const bool blocked_horizontal=map.is_water((x+width-1)%width, y)
					&& map.is_water((x+1)%width, y);
				const bool blocked_vertical=map.is_water(x, (y+height-1)%height)
					&& map.is_water(x, (y+1)%height);
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
			if(map.is_water(x, y) || component[origin]>=0)
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
					if(component[next]<0 && !map.is_water(nx[direction], ny[direction]))
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
			component_corn[label]+=map.is_resource(x, y, CORN) ? 1 : 0;
			component_wood[label]+=map.is_resource(x, y, WOOD) ? 1 : 0;
			component_stone[label]+=map.is_resource(x, y, STONE) ? 1 : 0;
			component_fruit[label]+=map.is_resource(x, y, CHERRY)
				|| map.is_resource(x, y, ORANGE)
				|| map.is_resource(x, y, PRUNE) ? 1 : 0;
		}
	int start_label=largest_label;
	for(int id=0; id<Building::MAX_COUNT; ++id)
	{
		Building* building=echo.player->team->myBuildings[id];
		if(!building || building->type->isVirtual)
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
		echo.player->game->gameHeader.getNumberOfPlayers());
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

void Maxima::update_environment_model(Context& echo)
{
	initialize_topology_profile(echo);
	const MaximaStrategy::Environment& policy=strategy.environment;
	EnvironmentModel observed;
	observed.known_tiles=0;
	observed.accessible_corn=0;
	observed.accessible_wood=0;
	observed.accessible_stone=0;
	observed.accessible_algae=0;
	observed.buildable_tiles=0;
	observed.water_tiles=0;
	observed.feeding_capacity=snapshot.inn_level1*strategy.model.inn_capacity_level1
		+snapshot.inn_level2*strategy.model.inn_capacity_level2
		+snapshot.inn_level3*strategy.model.inn_capacity_level3;
	observed.terrain_abundance=global_terrain_abundance;
	observed.connected_abundance=global_connected_abundance;
	observed.mobility_opportunity=global_mobility_opportunity;

	// Measure the discovered working territory around the current colony.  This
	// naturally expands as the colony and its knowledge expand, so an island can
	// be reclassified after swimming opens new resources without knowing the map
	// name or peeking through fog of war.
	MapInfo map(echo);
	const int map_width=map.get_width(),map_height=map.get_height();
	std::vector<Uint8> local_tiles(map_width*map_height,0);
	for(int id=0;id<Building::MAX_COUNT;++id)
	{
		Building* building=echo.player->team->myBuildings[id];
		if(!building||building->type->isVirtual)continue;
		for(int dy=-policy.local_territory_radius;
			dy<=policy.local_territory_radius;++dy)
			for(int dx=-policy.local_territory_radius;
				dx<=policy.local_territory_radius;++dx)
			if(dx*dx+dy*dy<=policy.local_territory_radius
				*policy.local_territory_radius)
			{
				const int x=echo.player->map->normalizeX(building->posX+dx);
				const int y=echo.player->map->normalizeY(building->posY+dy);
				local_tiles[y*map_width+x]=1;
			}
	}
	// Use the same worker connectivity for food and materials as for algae.
	// Nearby deposits across water or behind blocked routes are not live supply.
	const ResourceAccessObservation resources=
		observe_resource_access(echo.player, local_tiles);
	known_algae_units=resources.knownAlgaeUnits;
	walk_accessible_algae_units=resources.walkingAlgaeUnits;
	swim_accessible_algae_units=resources.swimmingAlgaeUnits;
	accessible_algae_units=resources.accessibleAlgaeUnits;
	observed.accessible_algae=resources.accessibleAlgaeTiles;
	std::set<int> food_tiles;
	long long food_capacity=0;
	BuildingSearch food_buildings(echo);
	food_buildings.add_condition(new NotUnderConstruction);
	for(building_search_iterator i=food_buildings.begin();i!=food_buildings.end();++i)
		if(echo.get_building_register().get_type(*i)==IntBuildingType::FOOD_BUILDING
		   || echo.get_building_register().get_type(*i)==IntBuildingType::SWARM_BUILDING)
			food_capacity+=nearby_farm_capacity(echo,*i,&food_tiles);
	observed.accessible_corn=int(food_capacity/65536);
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

	TeamStat* stat=echo.player->team->stats.getLatestStat();
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
	// Counts are current observations.  Strategic axes are
	// smoothed to prevent one unlucky harvest or sighting from thrashing policy.
	environment.known_tiles=observed.known_tiles;
	environment.accessible_corn=observed.accessible_corn;
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
	Context& echo) const
{
	Map* map=echo.player->map;
	const int width=map->getW();
	const int height=map->getH();
	std::vector<unsigned char> discovered(width*height, 0);
	for(int y=0; y<height; ++y)
		for(int x=0; x<width; ++x)
			discovered[y*width+x]=map->isMapDiscovered(
				x, y, echo.player->team->me) ? 1 : 0;
	return discovered;
}

void Maxima::remember_cleared_enemy_site(Context& echo,
	const Recon::BuildingSighting& sighting)
{
	if(sighting.construction)
		return;
	Map* map=echo.player->map;
	const int x=map->normalizeX(sighting.x+sighting.width/2);
	const int y=map->normalizeY(sighting.y+sighting.height/2);
	const int width=map->getW();
	const int height=map->getH();
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

void Maxima::sample_reconnaissance_forces(Context& echo)
{
	std::vector<int> living;
	for(enemy_team_iterator enemy(echo); enemy!=enemy_team_iterator(); ++enemy)
	{
		Team* enemy_team=echo.player->game->teams[*enemy];
		if(enemy_team && enemy_team->isAlive)
			living.push_back(*enemy);
	}
	reconnaissance.beginForceObservation(timer, living);
	std::vector<Tactics::ThreatSighting> threats;
	std::vector<Building*> own_buildings;
	for(int b=0; b<Building::MAX_COUNT; ++b)
	{
		Building* building=echo.player->team->myBuildings[b];
		if(building && !building->type->isVirtual)
			own_buildings.push_back(building);
	}

	for(std::vector<int>::const_iterator team=living.begin(); team!=living.end(); ++team)
	{
		Team* enemy_team=echo.player->game->teams[*team];
		for(int i=0; i<Unit::MAX_COUNT; ++i)
		{
			Unit* unit=enemy_team->myUnits[i];
			if(!unit || !echo.player->map->isFOWDiscovered(
				unit->posX, unit->posY, echo.player->team->me))
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
					std::max(1, unit->getRealAttackStrength()
						*unit->performance[ATTACK_SPEED]*unit->hp
						/std::max(1, unit->performance[HP]))));
			bool colony_threat=false;
			bool colony_explorer_threat=false;
			if(warrior || attack_explorer)
			{
				const int threat_radius=warrior ? 144 : 324;
				for(std::vector<Building*>::const_iterator building=
					own_buildings.begin(); building!=own_buildings.end(); ++building)
				{
					if(echo.player->map->warpDistSquare(unit->posX, unit->posY,
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
	tactics.replaceThreats(timer, threats);
}

void Maxima::update_reconnaissance(Context& echo)
{
	std::vector<int> living;
	for(enemy_team_iterator enemy(echo); enemy!=enemy_team_iterator(); ++enemy)
	{
		Team* enemy_team=echo.player->game->teams[*enemy];
		if(enemy_team && enemy_team->isAlive)
			living.push_back(*enemy);
	}
	reconnaissance.beginObservation(timer, living);
	tactics.beginObservation(timer);

	for(std::vector<int>::const_iterator team=living.begin(); team!=living.end(); ++team)
	{
		Team* enemy_team=echo.player->game->teams[*team];
		for(int i=0; i<Unit::MAX_COUNT; ++i)
		{
			Unit* unit=enemy_team->myUnits[i];
			if(!unit || !echo.player->map->isFOWDiscovered(
				unit->posX, unit->posY, echo.player->team->me))
				continue;
			const bool warrior=unit->typeNum==WARRIOR;
			const bool explorer=unit->typeNum==EXPLORER;
			const bool attack_explorer=explorer
				&& unit->performance[MAGIC_ATTACK_GROUND]>0;
			if(unit->typeNum==WORKER)
			{
				reconnaissance.observeEconomicActivity(*team,
					unit->posX, unit->posY);
				int economic_value=strategy.raiding.other_resource_value;
				if(unit->carriedResource==CORN)
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
					std::max(1, unit->getRealAttackStrength()
						*unit->performance[ATTACK_SPEED]*unit->hp
						/std::max(1, unit->performance[HP]))));
			bool colony_threat=false;
			bool colony_explorer_threat=false;
			if(warrior || attack_explorer)
			{
				const int threat_radius=warrior ? 144 : 324;
				for(int b=0; b<Building::MAX_COUNT; ++b)
				{
					Building* own=echo.player->team->myBuildings[b];
					if(own && !own->type->isVirtual
					   && echo.player->map->warpDistSquare(unit->posX, unit->posY,
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
			Building* building=enemy_team->myBuildings[i];
			if(!building || building->type->isVirtual
			   || !building_currently_visible(echo.player, building))
				continue;
			reconnaissance.observeBuilding(Recon::BuildingSighting(
				building->gid, *team, building->type->shortTypeNum,
				building->posX, building->posY, building->type->width,
				building->type->height, building->type->isBuildingSite, timer));
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
						echo.player, building->second))
					continue;
				const int id=Building::GIDtoID(building->first);
				Building* actual=id>=0 && id<Building::MAX_COUNT
					? enemy_team->myBuildings[id] : NULL;
				if(!actual || actual->gid!=building->first
				   || echo.player->map->getBuilding(building->second.x,
					building->second.y)!=building->first)
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
					remember_cleared_enemy_site(echo, sighting->second);
			}
			reconnaissance.confirmBuildingAbsent(*team, *gid);
		}
	}
	reconnaissance.finishObservation();
	Tactics::RaidRules raid_rules;
	raid_rules.width=echo.player->map->getW();
	raid_rules.height=echo.player->map->getH();
	raid_rules.tick=timer;
	raid_rules.clusterRadius=strategy.raiding.cluster_radius;
	raid_rules.threatRadius=strategy.raiding.threat_radius;
	raid_rules.workerMinimum=strategy.raiding.worker_min;
	raid_rules.workerWeight=strategy.raiding.worker_weight;
	raid_rules.harvestingBonus=strategy.raiding.harvesting_bonus;
	raid_rules.carryingBonus=strategy.raiding.carrying_bonus;
	raid_rules.resourceWeight=strategy.raiding.resource_weight;
	raid_rules.defenderPenalty=strategy.raiding.defender_penalty;
	tactics.finishObservation(raid_rules);

	const std::vector<unsigned char> discovered=reconnaissance_discovery_map(echo);
	int explored=0;
	for(std::vector<unsigned char>::const_iterator tile=discovered.begin();
		tile!=discovered.end(); ++tile)
		explored+=*tile ? 1 : 0;
	reconnaissance.setExploredPercent(discovered.empty()
		? 0 : explored*100/int(discovered.size()));
}

void Maxima::update_opponent_models(Context& echo)
{
	AIMaximaRuntime::Gradients::GradientInfo home_info;
	home_info.add_source(new Entities::AnyTeamBuilding(
		echo.player->team->teamNumber, CompletedBuildings));
	home_info.add_obstacle(new Entities::AnyResource);
	if(snapshot.swimming_warriors<6)
		home_info.add_obstacle(new Entities::Water);
	Gradient& home=echo.get_gradient_manager().get_gradient(home_info);

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
			model.strategic_value+=reconnaissance_building_value(
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

void Maxima::remove_reconnaissance_missions(Context& echo,
	const char* reason)
{
	const std::vector<Recon::ReconMission> missions=
		reconnaissance.report().missions;
	for(std::vector<Recon::ReconMission>::const_iterator mission=missions.begin();
		mission!=missions.end(); ++mission)
	{
		if(echo.get_building_register().is_building_found(mission->flagId)
		   || echo.get_building_register().is_building_pending(mission->flagId))
			echo.add_management_order(new DestroyBuilding(mission->flagId));

	}
	reconnaissance.clearMissions();
}

void Maxima::update_reconnaissance_missions(Context& echo)
{
	Recon::ReconReport& report=reconnaissance.mutableReport();
	for(std::vector<Recon::ReconMission>::iterator mission=report.missions.begin();
		mission!=report.missions.end();)
	{
		if(echo.get_building_register().is_building_found(mission->flagId)
		   || echo.get_building_register().is_building_pending(mission->flagId))
		{
			++mission;
			continue;
		}

		mission=report.missions.erase(mission);
	}

	if(budget.reconnaissance_suspended || report.exploredPercent<ACTIVE_RECON_MIN_EXPLORED_PERCENT)
	{

		reconnaissance_suspended=true;
		if(!report.missions.empty())
			remove_reconnaissance_missions(echo,
				report.exploredPercent<ACTIVE_RECON_MIN_EXPLORED_PERCENT ? "initial_exploration" : "emergency");
		last_recon_mission_tick=-1000000;
		return;
	}
	if(reconnaissance_suspended)
	{
		reconnaissance_suspended=false;
		last_recon_mission_tick=-1000000;

	}
	if(budget.reconnaissance_objectives.empty())
	{
		if(!report.missions.empty())
			remove_reconnaissance_missions(echo, "no_living_enemy");
		return;
	}
	if(timer-last_recon_mission_tick<budget.reconnaissance_review_interval)
		return;
	last_recon_mission_tick=timer;

	Map* map=echo.player->map;
	const std::vector<unsigned char> discovered=reconnaissance_discovery_map(echo);
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
				IntBuildingType::EXPLORATION_FLAG, 1);
			order->add_constraint(new Construction::SinglePosition(
				objective.x, objective.y));
			const int flag=echo.add_building_order(order);
			echo.add_management_order(new ChangeFlagSize(
				budget.reconnaissance_flag_radius, flag));
			ManagementOrder* removed=new Notify(
				RuntimeEvent(RuntimeEvent::ReconFlagDeleted, flag));
			removed->add_condition(new BuildingDestroyed(flag));
			echo.add_management_order(removed);
			missions.push_back(Recon::ReconMission(flag, objective.targetTeam,
				objective.frontier, objective.x, objective.y, timer,
				objective.economicWatch));

			continue;
		}

		Recon::ReconMission& mission=missions[index];
		const bool assignment_changed=mission.frontier!=objective.frontier
			|| mission.economicWatch!=objective.economicWatch
			|| mission.targetTeam!=objective.targetTeam;
		const bool location_changed=mission.x!=objective.x || mission.y!=objective.y;
		const int current_score=Recon::Program::frontierScore(
			mission.x, mission.y, map->getW(), map->getH(), discovered,
			budget.reconnaissance_flag_radius);
		const bool saturated=objective.frontier
			&& Recon::Program::exploredPercentInRadius(
			mission.x, mission.y, map->getW(), map->getH(), discovered,
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
			echo.add_management_order(new ChangeFlagPosition(
				objective.x, objective.y, mission.flagId));

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
		if(echo.get_building_register().is_building_found(mission.flagId)
		   || echo.get_building_register().is_building_pending(mission.flagId))
			echo.add_management_order(new DestroyBuilding(mission.flagId));

		missions.pop_back();
	}
}

void Maxima::plan_reconnaissance_objectives(Context& echo)
{
	Recon::ReconReport& report=reconnaissance.mutableReport();
	if(!strategy.reconnaissance.enabled
	   || !strategy.reconnaissance.scouting_missions_enabled
	   || report.exploredPercent<ACTIVE_RECON_MIN_EXPLORED_PERCENT)
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

	Map* map=echo.player->map;
	const std::vector<unsigned char> discovered=reconnaissance_discovery_map(echo);
	budget.reconnaissance_objectives=Recon::Program::planObjectives(
		report, contact_desired, map->getW(), map->getH(), discovered,
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
							const int x=((center_x+dx)%map->getW()
								+map->getW())%map->getW();
							const int y=((center_y+dy)%map->getH()
								+map->getH())%map->getH();
							const int index=y*map->getW()+x;
							if(!discovered[index] || map->isWater(x, y)
							   || map->getResource(x, y).type==NO_RES_TYPE
							   || !seen_sites.insert(index).second)
								continue;
							EconomicWatchSite site;
							site.x=x;
							site.y=y;
							const int resource=map->getResource(x, y).type;
							site.score=(resource==CHERRY || resource==ORANGE
								|| resource==PRUNE)
								? strategy.raiding.fruit_resource_value
								: (resource==CORN
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
	budget=DirectorPlan();
	int largest_enemy_force=0;
	for(int team=0; team<Team::MAX_COUNT; ++team)
		if(opponents[team].alive)
			largest_enemy_force=std::max(largest_enemy_force, opponents[team].estimated_warriors);
	const bool direct_threat=snapshot.visible_colony_threat>0
		|| snapshot.own_buildings_under_attack>0
		|| explorer_defense_emergency();
	// Economy, production and construction are owned by the policy bidders.
	// This provisional reserve survives only to gate campaign deployment before
	// the defense bidder produces the executable reserve.
	budget.defense_reserve=std::max(strategy.military.defense_reserve_min,
		std::max(largest_enemy_force*strategy.military.defense_enemy_percent/100
				+strategy.military.reserve_enemy_bonus,
			snapshot.trained_warriors/strategy.military.reserve_force_divisor));
	const int deployable=snapshot.trained_warriors-budget.defense_reserve;
	budget.attack_flags=0;
	bool known_target=false;
	for(int team=0; team<Team::MAX_COUNT; ++team)
		if(opponents[team].alive && opponents[team].score!=INT_MIN)
			known_target=true;
	const bool endgame=snapshot.alive_enemies<=strategy.military.endgame_enemy_count;
	const int autonomous_campaign_force=endgame
		? std::max(strategy.military.endgame_force_floor,
			largest_enemy_force+budget.defense_reserve
			+std::max(0, strategy.military.campaign_force_margin-1))
		: std::max(strategy.military.campaign_force_floor,
			largest_enemy_force+budget.defense_reserve
			+strategy.military.campaign_force_margin);
	// The economic director can restrict births and construction, but existing
	// warriors are deployed according to military readiness alone.
	const bool counterattack_window=strategy.military.counterattack_enabled
		&& direct_threat && !severe_colony_emergency()
		&& deployable>=largest_enemy_force+strategy.military.counterattack_force_margin;
	const bool campaign_ordered=strategy.tactics.enabled
		&& strategy.tactics.siege_enabled && known_target
		&& !severe_colony_emergency()
		&& (!direct_threat || counterattack_window)
		&& timer>=campaign.cooldown_until
		&& (snapshot.trained_warriors>=autonomous_campaign_force
			|| campaign.state==CampaignActive || campaign.state==CampaignPaused);
	if(campaign_ordered && deployable>=strategy.military.campaign_deployable_min)
		budget.attack_flags=1;
	// Concentrate the deployable force on one objective. A paired screen showed
	// that even a heavily gated second flag consumed economic strength without
	// improving conversion on the island map it was meant to accelerate.
	budget.attack_units=budget.attack_flags>0
		? std::min(strategy.military.attack_unit_cap, deployable) : 0;

	// Campaign readiness feeds the offense bid; the arbiter owns the executable
	// economy and production budget.
	build_policy_bids();
	arbitrate_policy_bids();
}



void Maxima::build_policy_bids()
{
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
	// Nominal inn storage is an optimistic service bound: travel time, uneven
	// corn distribution and staffing all reduce the population that a real inn
	// can feed. Plan against 80% of nominal capacity, but derive the target from
	// population rather than the current inn count so a temporary queue cannot
	// ratchet construction upward forever.
	const int reliable_inn_capacity=std::max(1,
		strategy.model.inn_capacity_level1*strategy.economy.reliable_inn_percent/100);
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
	const int sustainable_inns=std::max(strategy.economy.sustainable_inn_floor,
		environment.accessible_corn/strategy.economy.sustainable_inn_corn_divisor
			+strategy.economy.sustainable_inn_offset);
	int demographic_inns=(snapshot.population
		+strategy.economy.inn_population_offset)
		/strategy.economy.inn_population_divisor+1;
	demographic_inns+=(environment.food_headroom
		<strategy.economy.food_headroom_warning ? 1 : 0);
	demographic_inns+=(environment.food_headroom
		<strategy.economy.food_headroom_critical ? 1 : 0);
	demographic_inns+=(demands.food>=strategy.construction.survival_utility_high
		? 1 : 0);
	survival.desired_inns=std::min(sustainable_inns,
		std::min(strategy.economy.inn_target_cap,
			std::max(strategy.economy.inn_target_floor,
			std::max(service_required_inns, demographic_inns))));
	if(abundance_surge)
		survival.desired_inns=std::min(sustainable_inns,
			std::min(strategy.economy.abundance_inn_target_cap,
				std::max(survival.desired_inns,
					(snapshot.population
						+strategy.economy.abundance_inn_population_offset)
						/strategy.economy.abundance_inn_population_divisor+1)));
	survival.construction_sites=
		survival.utility>=strategy.construction.survival_utility_high
			? strategy.construction.policy_high_sites
			: strategy.construction.policy_low_sites;
	survival.request_upgrades=snapshot.schools>0
		&& (demands.food>=strategy.economy.food_upgrade_utility_min
			|| environment.food_headroom<strategy.economy.food_upgrade_headroom_max);

	PolicyBid& growth=policy_bids[PolicyGrowth];
	growth.utility=demands.growth;
	const SwarmController::Plan birth=SwarmController::plan(snapshot.workers,
		snapshot.population, snapshot.critical_food, snapshot.unserved_food,
		environment.accessible_corn, strategy.economy.swarm_labor_scale_percent,
		strategy.economy.swarm_food_per_worker_percent,
		strategy.economy.swarm_pressure_sensitivity,
		strategy.economy.swarm_workers_per_building);
	growth.desired_swarms=birth.swarms;
	growth.swarm_workers=birth.workers;
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
	// Shoreline algae can be collected by walkers; deep-water algae only becomes
	// eligible after a swimming worker can actually reach it.
	technology.desired_schools=snapshot.population>=school_population_min
		&& accessible_algae_units>=school_algae_requirement()
		&& technology.utility>=strategy.economy.school_utility_min
		? (technology.utility>=second_school_utility_min
			? strategy.economy.second_school_target
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
	defense.desired_warriors=std::min(strategy.military.warrior_cap,
		std::max(strategy.military.warrior_floor,
			std::max(largest_enemy_force+strategy.military.warrior_enemy_margin,
				snapshot.population*(strategy.military.warrior_population_percent
					+defense.utility/strategy.military.warrior_utility_divisor)/100)));
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
	defense.desired_hospitals=(snapshot.need_heal>0
		|| snapshot.warriors>=strategy.military.hospital_warrior_min)
		? std::min(strategy.military.hospital_cap, std::max(1,
			(snapshot.warriors+strategy.military.hospital_units_per_building-1)
				/strategy.military.hospital_units_per_building)) : 0;
	const int active_towers=strategy.military.tower_active_count;
	const int emergency_towers=active_towers+strategy.military.tower_emergency_increment;
	const int bomb_towers=emergency_towers+strategy.military.tower_bomb_increment;
	defense.desired_towers=explorer_defense_active() ? active_towers : 0;
	// Gate geometry previously never requested a tower against ground armies.
	// Require a mature workforce and a local threat: early experiments spent
	// food/construction labour on towers merely because a remote army was seen.
	// Cap at the active target plus one (normally two); emergency demands win.
	// The shared-coverage placement reward favours defending two mouths with
	// one tower. Lack of a legal firing pad must not generate a futile demand.
	if(strategy.military.preemptive_defense_enabled && strategy.farming.enabled
	   && strategy.farming.barrier_topology_enabled && gate_defense_demand>0
	   && snapshot.workers>=strategy.farming.gate_clearing_workers_min
	   && snapshot.visible_colony_threat>=strategy.military.emergency_barracks_threat_min
	   && snapshot.hungry==0
	   && snapshot.critical_food==0
	   && environment.food_headroom>=strategy.economy.mature_food_headroom_min)
		defense.desired_towers=std::max(defense.desired_towers,
			std::min(active_towers+1,snapshot.towers+(gate_defense_demand+1)/2));
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
	offense.desired_warriors=std::min(strategy.military.warrior_cap,
		std::max(strategy.military.offense_warrior_floor, snapshot.population
			*(strategy.military.offense_warrior_base_percent
				+offense.utility/strategy.military.offense_utility_divisor)/100));
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
	DirectorPlan result;
	const bool abundance_surge=abundance_surge_active();
	const PolicyBid& survival=policy_bids[PolicySurvival];
	const PolicyBid& growth=policy_bids[PolicyGrowth];
	const PolicyBid& access=policy_bids[PolicyAccess];
	const PolicyBid& technology=policy_bids[PolicyTechnology];
	const PolicyBid& defense=policy_bids[PolicyDefense];
	const PolicyBid& offense=policy_bids[PolicyOffense];

	result.desired_inns=survival.desired_inns;
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
	result.desired_hospitals=defense.desired_hospitals;
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
	// Training capacity constrains the combined birth stream, regardless of
	// which policy requested the warriors.
	const int untrained_warriors=std::max(0,
		snapshot.warriors-snapshot.trained_warriors);
	const int training_backlog_limit=std::max(
		strategy.military.training_backlog_floor,
		std::max(1, snapshot.barracks)
			*strategy.military.training_backlog_per_barracks);
	if(strategy.military.warrior_training_backlog_throttle_enabled
	   && untrained_warriors>=training_backlog_limit)
		result.warrior_ratio=0;
	result.desired_explorers=std::max(access.desired_explorers,
		std::max(defense.desired_explorers, offense.desired_explorers));
	result.desired_warriors=std::max(defense.desired_warriors,
		offense.utility>=strategy.economy.offense_bid_utility_min
			? offense.desired_warriors : 0);
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
	result.priority_barracks=std::max(defense.utility, offense.utility)
		+std::min(strategy.scoring.priority_threat_cap,
			snapshot.visible_colony_threat*strategy.scoring.priority_threat_weight);
	result.priority_hospitals=defense.utility+(snapshot.need_heal>0
		? strategy.scoring.priority_healing_bonus : 0);
	result.priority_towers=defense.utility
		+(large_economy_established() && snapshot.enemy_prestige>0
			? strategy.scoring.priority_large_tower_bonus : 0)
		+(explorer_defense_active()
			? strategy.scoring.priority_tower_active_bonus : 0)
		+(explorer_defense_emergency()
			? strategy.scoring.priority_tower_emergency_bonus : 0);

	budget=result;
}

void Maxima::finalize_director_plan(Context& echo)
{
	// Copy every executor-facing threshold into the immutable plan. From this
	// point until the next strategic cadence, tactical code does not reinterpret
	// configuration or posture.
	budget.allow_upgrades=strategy.upgrades.enabled && budget.allow_upgrades
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
	budget.first_prestige_trained_workers=
		strategy.upgrades.first_prestige_trained_workers;
	budget.second_prestige_trained_workers=
		strategy.upgrades.second_prestige_trained_workers;
	budget.second_prestige_population_min=
		strategy.upgrades.second_prestige_population_min;
	budget.inn_adaptive_staffing_enabled=
		strategy.staffing.inn_adaptive_staffing_enabled;
	budget.swarm_retirement_enabled=
		strategy.economy.swarm_retirement_enabled;

	budget.inn_low_corn_threshold[0]=strategy.staffing.inn_level1_low_corn_threshold;
	budget.inn_low_corn_threshold[1]=strategy.staffing.inn_level2_low_corn_threshold;
	budget.inn_low_corn_threshold[2]=strategy.staffing.inn_level3_low_corn_threshold;
	budget.inn_normal_workers[0]=strategy.staffing.inn_level1_normal_workers;
	budget.inn_normal_workers[1]=strategy.staffing.inn_level2_normal_workers;
	budget.inn_normal_workers[2]=strategy.staffing.inn_level3_normal_workers;
	budget.inn_low_corn_workers[0]=strategy.staffing.inn_level1_low_corn_workers;
	budget.inn_low_corn_workers[1]=strategy.staffing.inn_level2_low_corn_workers;
	budget.inn_low_corn_workers[2]=strategy.staffing.inn_level3_low_corn_workers;
	budget.resource_tracker_samples=strategy.staffing.resource_tracker_samples;
	budget.swarm_supply_radius=
		strategy.staffing.swarm_supply_radius;
	budget.attack_clearing_workers=strategy.staffing.attack_clearing_workers;

	budget.recovery_active=posture==PostureRecover;
	budget.food_emergency=severe_food_emergency();
	budget.colony_emergency=severe_colony_emergency();
	// Expansion buys access to new food, independently of the birth-capacity
	// target. It still shares real builders and the normal swarm staffing pool.
	const bool colony_active=development_planner.activeBuildCount(
		IntBuildingType::SWARM_BUILDING, AIMaximaPlacement::ColonySeed)>0;
	bool colony_starting=false;
	for(const auto& entry:development_planner.actions())
	{
		const auto& action=entry.second;
		if(action.purpose!=AIMaximaPlacement::ColonySeed
		   || action.state!=AIMaximaPlacement::Completed) continue;
		Building* building=echo.get_building_register().get_building(action.buildingId);
		// Completion alone is not success: do not chain empty, unstaffed outposts.
		// Once provisioned, remember startup so a later pause cannot block expansion.
		if(building && !operating_colonies.count(action.id))
		{
			if(!building->unitsWorking.empty()
			   && building->resources[CORN]>=building->type->resourceForOneUnit)
			{
				operating_colonies.insert(action.id);

			}
			else colony_starting=true;
		}
	}
	budget.colony_swarm_requested=strategy.colonization.enabled
        && !colony_active && !colony_starting
        && snapshot.free_workers-snapshot.worker_jobs_open
            >=strategy.staffing.construction_swarm_workers;
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
	budget.fruit_active=strategy.fruit.enabled && echo.is_fruit_on_map()
		&& snapshot.population>=strategy.fruit.population_min
		&& posture!=PostureRecover && posture!=PostureDefend;
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
	budget.tactical_rally_radius=strategy.tactics.rally_flag_radius;
	budget.tactical_siege_radius=strategy.tactics.siege_flag_radius;
	budget.tactical_siege_muster_percent=strategy.tactics.siege_muster_percent;
	budget.tactical_siege_strength_percent=strategy.tactics.siege_strength_percent;
	budget.tactical_siege_casualty_percent=strategy.tactics.siege_casualty_percent;
	budget.tactical_siege_threat_radius=strategy.tactics.siege_local_threat_radius;
	budget.tactical_target_lock_ticks=strategy.tactics.siege_target_lock_ticks;
	budget.raid_flag_radius=strategy.raiding.flag_radius;
	budget.raid_muster_percent=strategy.raiding.muster_percent;
	budget.raid_muster_timeout=strategy.raiding.muster_timeout_ticks;
	budget.raid_contact_ttl=strategy.raiding.contact_ttl_ticks;
	budget.raid_max_engagement=strategy.raiding.max_engagement_ticks;
	budget.raid_casualty_percent=strategy.raiding.casualty_percent;
	budget.raid_survivor_min=strategy.raiding.survivor_min;
	budget.raid_cooldown=strategy.raiding.cooldown_ticks;
	budget.raid_defender_min=strategy.raiding.defender_min;
	budget.raid_defender_percent=strategy.raiding.defender_percent;
	budget.raid_building_buffer=strategy.raiding.building_buffer;
	budget.raid_tower_buffer=strategy.raiding.tower_buffer;
	budget.raid_retarget_margin=strategy.raiding.retarget_margin;
	budget.relief_contact_ttl=strategy.teamplay.defense_contact_ttl_ticks;
	budget.relief_max_engagement=strategy.teamplay.defense_max_engagement_ticks;
	budget.relief_cooldown=strategy.teamplay.defense_cooldown_ticks;
	budget.relief_follow_radius=strategy.teamplay.defense_follow_radius;
	budget.relief_retarget_margin=strategy.teamplay.defense_retarget_margin;
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
	budget.campaign_stall_ticks=strategy.scheduling.campaign_stall_ticks;
	budget.campaign_retreat_cooldown_ticks=
		strategy.scheduling.campaign_retreat_cooldown_ticks;
	budget.target_switch_margin=strategy.scoring.target_switch_margin;
	budget.tower_barrier_bonus=strategy.military.tower_barrier_bonus;
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
	budget.tactics_enabled=strategy.tactics.enabled;
	budget.raid_enabled=strategy.raiding.enabled;
	budget.siege_enabled=strategy.tactics.siege_enabled;
	budget.siege_target_lock_enabled=
		strategy.tactics.siege_target_lock_enabled;
	budget.teamplay_enabled=strategy.teamplay.enabled;
	budget.teamplay_defense_enabled=strategy.teamplay.defense_enabled;
	budget.farming_normal_interval=strategy.farming.normal_interval_ticks;
	budget.farming_urgent_interval=strategy.farming.urgent_interval_ticks;
	budget.farming_enabled=strategy.farming.enabled;
	budget.farming_protection_enabled=strategy.farming.farm_protection_enabled;
	budget.farming_barrier_enabled=strategy.farming.barrier_topology_enabled;
	budget.farming_coastal_porosity_enabled=
		strategy.farming.coastal_porosity_enabled;
	budget.farming_gate_clearing_enabled=strategy.farming.gate_clearing_enabled;
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
	budget.barrier_topology_interval=
		strategy.scheduling.barrier_topology_interval_ticks;
	budget.farming_gate_relocation_penalty_cap=
		strategy.farming.gate_relocation_penalty_cap;
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
	budget.farming_gate_clearing_radius=strategy.farming.gate_clearing_radius;
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

bool Maxima::raid_candidate_safe(Context& echo,
	const Tactics::RaidCandidate& candidate, int raidForce, int& routeDistance,
	int& nearestBuildingDistance) const
{
	routeDistance=-1;
	nearestBuildingDistance=INT_MAX;
	if(Tactics::Program::raidUnsafe(candidate.defenders,
		raidForce, budget.raid_defender_min,
		budget.raid_defender_percent))
		return false;
	const Recon::OpponentIntel* intel=reconnaissance.opponent(candidate.team);
	if(!intel)
		return false;
	Map* map=echo.player->map;
	for(enemy_team_iterator enemy(echo); enemy!=enemy_team_iterator(); ++enemy)
	{
		const Recon::OpponentIntel* hostile=reconnaissance.opponent(*enemy);
		if(!hostile)
			continue;
		for(std::map<int, Recon::BuildingSighting>::const_iterator building=
			hostile->buildings.begin(); building!=hostile->buildings.end(); ++building)
		{
			const Recon::BuildingSighting& sighting=building->second;
			// Preserve the target team's economic-building buffer and outskirts
			// score, but towers belonging to any hostile team can shoot the raiders.
			if(*enemy!=candidate.team && sighting.type!=IntBuildingType::DEFENSE_BUILDING)
				continue;
			int distance=INT_MAX;
			for(int dx=0; dx<sighting.width; ++dx)
				for(int dy=0; dy<sighting.height; ++dy)
					distance=std::min(distance, map->warpDistSquare(candidate.x,
						candidate.y, sighting.x+dx, sighting.y+dy));
			if(*enemy==candidate.team)
				nearestBuildingDistance=std::min(nearestBuildingDistance, distance);
			int buffer=budget.raid_building_buffer+budget.raid_flag_radius;
			if(sighting.type==IntBuildingType::DEFENSE_BUILDING)
			{
				BuildingType* tower=globalContainer->buildingsTypes.getByType(
					IntBuildingType::typeFromShortNumber(sighting.type), 1, false);
				const int range=tower ? tower->shootingRange : buffer;
				buffer=range+budget.raid_tower_buffer+budget.raid_flag_radius;
			}
			if(distance<=buffer*buffer)
				return false;
		}
	}

	GradientInfo land_info;
	land_info.add_source(new Entities::AnyTeamBuilding(
		echo.player->team->teamNumber, CompletedBuildings));
	land_info.add_obstacle(new Entities::AnyResource);
	land_info.add_obstacle(new Entities::Water);
	Gradient& land=echo.get_gradient_manager().get_gradient(land_info);
	routeDistance=land.get_height(candidate.x, candidate.y);
	if(routeDistance>=0)
		return true;
	const Building* continuing_flag=tactical_mission.kind==Tactics::MissionRaid
		? echo.get_building_register().get_building(tactical_mission.flagId) : NULL;
	int available_swimmers=0;
	for(int id=0; id<Unit::MAX_COUNT; ++id)
	{
		const Unit* warrior=echo.player->team->myUnits[id];
		if(tactical_warrior_available(warrior, continuing_flag)
		   && warrior->performance[SWIM]>0)
			++available_swimmers;
	}
	if(available_swimmers<raidForce)
		return false;
	GradientInfo swim_info;
	swim_info.add_source(new Entities::AnyTeamBuilding(
		echo.player->team->teamNumber, CompletedBuildings));
	swim_info.add_obstacle(new Entities::AnyResource);
	Gradient& swim=echo.get_gradient_manager().get_gradient(swim_info);
	routeDistance=swim.get_height(candidate.x, candidate.y);
	return routeDistance>=0;
}

bool Maxima::choose_tactical_rally(Context& echo, int targetX, int targetY,
	int& rallyX, int& rallyY) const
{
	Map* map=echo.player->map;
	GradientInfo target_land_info;
	target_land_info.add_source(new Entities::Position(
		map->normalizeX(targetX), map->normalizeY(targetY)));
	target_land_info.add_obstacle(new Entities::AnyResource);
	target_land_info.add_obstacle(new Entities::Water);
	GradientInfo target_swim_info;
	target_swim_info.add_source(new Entities::Position(
		map->normalizeX(targetX), map->normalizeY(targetY)));
	target_swim_info.add_obstacle(new Entities::AnyResource);
	GradientManager target_gradients(echo.player);
	Gradient& target_land=target_gradients.get_gradient(target_land_info);
	Gradient& target_swim=target_gradients.get_gradient(target_swim_info);

	std::vector<const Unit*> available;
	for(int i=0; i<Unit::MAX_COUNT; ++i)
	{
		const Unit* unit=echo.player->team->myUnits[i];
		if(tactical_warrior_available(unit, NULL)
		   && (unit->performance[SWIM]>0 ? target_swim : target_land)
			.get_height(unit->posX,unit->posY)>=0)
			available.push_back(unit);
	}
	const int radius=budget.tactical_rally_radius;
	const int requested=std::max(1, budget.tactical_requested_force);
	const int search_radius=std::max(12, radius+1);
	int best=INT_MAX;
	std::set<int> considered;
	for(int i=0; i<Building::MAX_COUNT; ++i)
	{
		Building* home=echo.player->team->myBuildings[i];
		if(!home || home->type->isVirtual || home->type->isBuildingSite
		   || home->underAttackTimer)
			continue;
		const int type=home->type->shortTypeNum;
		if(type!=IntBuildingType::FOOD_BUILDING
		   && type!=IntBuildingType::HEAL_BUILDING
		   && type!=IntBuildingType::SWARM_BUILDING)
			continue;
		const bool amphibious=target_land.get_height(home->posX,home->posY)<0;
		for(int dy=-search_radius; dy<=search_radius; ++dy)
			for(int dx=-search_radius; dx<=search_radius; ++dx)
			{
				if(dx*dx+dy*dy>search_radius*search_radius) continue;
				const int x=map->normalizeX(home->posX+dx);
				const int y=map->normalizeY(home->posY+dy);
				// Assemble by our supply buildings, not on the enemy island.
				// A water rally recruits swimmers without a new engine flag type.
				if(!map->isFOWDiscovered(x,y,echo.player->team->me)
				   || map->isWater(x,y)!=amphibious
				   || map->getBuilding(x,y)!=NOGBID
				   || map->getResource(x,y).type!=NO_RES_TYPE
				   || map->isForbidden(x,y,echo.player->team->me)
				   || (amphibious ? target_swim : target_land).get_height(x,y)<0)
					continue;
				if(!considered.insert(y*map->getW()+x).second) continue;
				bool safe=true;
				for(std::vector<Tactics::ThreatSighting>::const_iterator threat=
					tactics.threats().begin(); threat!=tactics.threats().end(); ++threat)
					if(map->warpDistSquare(x,y,threat->x,threat->y)
						<=(radius+4)*(radius+4)) { safe=false; break; }
				if(!safe) continue;
				int capacity=0;
				for(int ry=-radius; ry<=radius && safe; ++ry)
					for(int rx=-radius; rx<=radius; ++rx)
					{
						if(rx*rx+ry*ry>radius*radius) continue;
						const int px=map->normalizeX(x+rx),py=map->normalizeY(y+ry);
						if(map->getBuilding(px,py)!=NOGBID
						   || map->getResource(px,py).type!=NO_RES_TYPE
						   || map->isForbidden(px,py,echo.player->team->me)) continue;
						// The engine recruits against the entire flag radius. Never
						// admit walkers from a shore that cannot reach the target.
						if(!map->isWater(px,py) && target_land.get_height(px,py)<0)
						{ safe=false; break; }
						if((amphibious ? target_swim : target_land).get_height(px,py)>=0)
							++capacity;
					}
				if(!safe || capacity<requested) continue;
				std::vector<int> distances;
				for(size_t unit=0; unit<available.size(); ++unit)
					if(!amphibious || available[unit]->performance[SWIM]>0)
						distances.push_back(map->warpDistSquare(x,y,
							available[unit]->posX,available[unit]->posY));
				if(int(distances.size())<requested) continue;
				std::sort(distances.begin(),distances.end());
				int score=dx*dx+dy*dy;
				for(int unit=0; unit<std::min(requested,int(distances.size())); ++unit)
					score+=distances[unit];
				if(score<best)
				{ best=score; rallyX=x; rallyY=y; }
			}
	}
	return best!=INT_MAX;
}

void Maxima::plan_tactical_authorization(Context& echo)
{

	budget.tactical_kind=Tactics::MissionNone;
	budget.tactical_target_team=-1;
	budget.tactical_dig_out_team=-1;
	budget.tactical_target_gid=-1;
	budget.tactical_candidate_score=INT_MIN;
	budget.tactical_requested_force=0;
	budget.tactical_minimum_force=0;
	budget.tactical_contact_visible=false;
	budget.tactical_allied_player_pressure=false;
	budget.tactical_allied_target_pressure=false;
	budget.tactical_visible_enemy_warriors=0;
	budget.tactical_visible_enemy_power=0;
	budget.tactical_allied_warriors=0;
	budget.tactical_allied_power=0;
	budget.tactical_route_distance=-1;
	if(!strategy.tactics.enabled)
	{

		return;
	}
	if(budget.colony_emergency)
	{

		return;
	}
	int replacement_siege_team=-1;
	bool active_relief=false;
	if(tactical_mission.phase!=Tactics::PhaseIdle
	   && tactical_mission.phase!=Tactics::PhaseCooldown)
	{
		budget.tactical_kind=tactical_mission.kind;
		budget.tactical_target_team=tactical_mission.targetTeam;
		budget.tactical_target_gid=tactical_mission.targetGid;
		budget.tactical_target_x=tactical_mission.targetX;
		budget.tactical_target_y=tactical_mission.targetY;
		budget.tactical_candidate_score=tactical_mission.candidateScore;
		budget.tactical_requested_force=tactical_mission.requestedForce;
		budget.tactical_minimum_force=tactical_mission.minimumForce;
		if(tactical_mission.kind==Tactics::MissionRaid)
		{

			return;
		}
		if(tactical_mission.kind==Tactics::MissionRelief)
			active_relief=true;
		else
		{
			const Recon::OpponentIntel* active_intel=
				reconnaissance.opponent(tactical_mission.targetTeam);
			if(active_intel && active_intel->buildings.find(tactical_mission.targetGid)
				!=active_intel->buildings.end())
			{

				return;
			}
			replacement_siege_team=tactical_mission.targetTeam;
			// The copied target is no longer valid. A candidate selected below may
			// replace it; otherwise withdraw instead of waiting indefinitely.
			budget.tactical_target_gid=-1;
		}
	}
	if(timer<tactical_mission.cooldownUntil)
	{

		return;
	}

	const int deployable=std::max(0, snapshot.trained_warriors-budget.defense_reserve);
	int raid_score=INT_MIN;
	Tactics::RaidCandidate raid;
	const bool can_raid=strategy.raiding.enabled
		&& !active_relief && replacement_siege_team<0
		&& deployable>=strategy.raiding.min_force;

	if(can_raid)
	{
		for(std::vector<Tactics::RaidCandidate>::const_iterator candidate=
			tactics.raidCandidates().begin(); candidate!=tactics.raidCandidates().end();
			++candidate)
		{
			const int requested=Tactics::Program::desiredRaidForce(candidate->workers,
				strategy.raiding.force_bonus, strategy.raiding.min_force,
				std::min(strategy.raiding.max_force, deployable));
			int route=-1;
			int nearest=INT_MAX;
			if(!raid_candidate_safe(echo, *candidate, requested, route, nearest))
				continue;
			if(requested<strategy.raiding.min_force)
				continue;

			const int route_penalty=route*strategy.raiding.route_distance_weight;
			int outskirts_bonus=0;
			int allied_bonus=0;
			int ffa_penalty=0;
			int score=candidate->score-route_penalty;
			if(nearest!=INT_MAX)
			{
				outskirts_bonus=std::min(100, nearest/16)
					*strategy.raiding.outskirts_weight;
				score+=outskirts_bonus;
			}
		if(strategy.teamplay.enabled
		   && strategy.teamplay.pressure_coordination_enabled
		   && visible_allied_pressure_near(echo.player, candidate->x, candidate->y,
			strategy.teamplay.allied_pressure_radius).warriors>0)
			{
				allied_bonus=strategy.raiding.allied_pressure_bonus;
				score+=allied_bonus;
			}
			if(snapshot.alive_enemies>1 && snapshot.visible_colony_threat>0)
			{
				ffa_penalty=strategy.raiding.ffa_third_party_penalty;
				score-=ffa_penalty;
			}
			if(score>raid_score)
			{
				raid_score=score;
				raid=*candidate;

			}
		}

	}

	int siege_score=INT_MIN;
	int siege_gid=-1;
	int siege_team=-1;
	int siege_x=0;
	int siege_y=0;
	int siege_requested=0;
	bool siege_player_pressure=false;
	bool siege_target_pressure=false;
	std::vector<int> available_powers;
	// Mission IDs belong to the runtime register, not the engine GID space.
	// Reuse the deployed force when refreshing relief or replacing a siege target.
	Building* continuing_flag=(active_relief || replacement_siege_team>=0)
		? echo.get_building_register().get_building(tactical_mission.flagId) : NULL;
	for(int unit_id=0; unit_id<Unit::MAX_COUNT; ++unit_id)
	{
		Unit* warrior=echo.player->team->myUnits[unit_id];
		if(!tactical_warrior_available(warrior, continuing_flag))
			continue;
		const int power=std::max(1, warrior->getRealAttackStrength()
			*warrior->performance[ATTACK_SPEED]*warrior->hp
			/std::max(1, warrior->performance[HP]));
		available_powers.push_back(power);
	}
	std::sort(available_powers.begin(), available_powers.end(), std::greater<int>());
	if(static_cast<int>(available_powers.size())>deployable)
		available_powers.resize(deployable);
	const int representative_power=available_powers.empty() ? 1
		: available_powers[available_powers.size()/2];
	TacticalReachability reachability(echo.player->map);

	ReliefCandidate relief;
	bool has_living_ally=false;
	for(int team=0; team<Team::MAX_COUNT && !has_living_ally; ++team)
	{
		Team* possible=echo.player->game->teams[team];
		has_living_ally=possible && possible!=echo.player->team && possible->isAlive
			&& (echo.player->team->allies&possible->me);
	}
	const bool can_relieve=strategy.teamplay.enabled
		&& strategy.teamplay.defense_enabled
		&& has_living_ally
		&& replacement_siege_team<0
		&& deployable>=strategy.teamplay.defense_min_force;

	if(can_relieve)
	{
		GradientInfo land_route_info;
		land_route_info.add_source(new Entities::AnyTeamBuilding(
			echo.player->team->teamNumber, CompletedBuildings));
		land_route_info.add_obstacle(new Entities::AnyResource);
		land_route_info.add_obstacle(new Entities::Water);
		Gradient& land_route=echo.get_gradient_manager().get_gradient(land_route_info);
		GradientInfo swim_route_info;
		swim_route_info.add_source(new Entities::AnyTeamBuilding(
			echo.player->team->teamNumber, CompletedBuildings));
		swim_route_info.add_obstacle(new Entities::AnyResource);
		Gradient& swim_route=echo.get_gradient_manager().get_gradient(swim_route_info);
		const int radius_square=strategy.teamplay.allied_pressure_radius
			*strategy.teamplay.allied_pressure_radius;

		const auto consider_relief=[&](int team, int x, int y, int asset_value,
			bool under_attack)
		{

			int enemies=0;
			int enemy_power=0;
			for(std::vector<Tactics::ThreatSighting>::const_iterator threat=
				tactics.threats().begin(); threat!=tactics.threats().end(); ++threat)
				if(echo.player->map->warpDistSquare(x, y, threat->x, threat->y)
				   <=radius_square)
				{
					++enemies;
					enemy_power+=threat->power;
				}
			if(enemies==0)
				return;
			const AlliedPressure pressure=visible_allied_pressure_near(echo.player,
				x, y, strategy.teamplay.allied_pressure_radius);
			const int required_combined=(enemy_power
				*strategy.teamplay.defense_strength_percent+99)/100;
			const int required_power=std::max(0, required_combined-pressure.power);
			if(required_power==0)
				return;
			int route=land_route.get_height(x, y);
			int requested=Tactics::Program::forceForPower(reachability.powersAt(
				echo.player->team,continuing_flag,x,y,deployable),
				required_power, strategy.teamplay.defense_min_force,
				strategy.military.attack_unit_cap);
			if(route<0 || requested==0)
			{
				route=swim_route.get_height(x, y);
				requested=Tactics::Program::forceForPower(reachability.powersAt(
					echo.player->team,continuing_flag,x,y,deployable,true),
					required_power, strategy.teamplay.defense_min_force,
					strategy.military.attack_unit_cap);
			}
			if(route<0 || requested==0)
				return;
			const int score=strategy.teamplay.defense_base_score+asset_value
				+enemies*strategy.teamplay.defense_threat_weight
				+(under_attack ? strategy.teamplay.defense_under_attack_bonus : 0)
				-route*strategy.teamplay.defense_route_distance_weight;
			if(score<=0)
				return;

			if(score<=relief.score)
				return;
			relief.team=team;
			relief.x=x;
			relief.y=y;
			relief.score=score;
			relief.requested=requested;
			relief.enemies=enemies;
			relief.enemyPower=enemy_power;
			relief.allies=pressure.warriors;
			relief.alliedPower=pressure.power;
			relief.route=route;

		};

		for(int team=0; team<Team::MAX_COUNT; ++team)
		{
			Team* allied_team=echo.player->game->teams[team];
			if(!allied_team || allied_team==echo.player->team
			   || !(echo.player->team->allies&allied_team->me))
				continue;
			for(int building_id=0; building_id<Building::MAX_COUNT; ++building_id)
			{
				Building* building=allied_team->myBuildings[building_id];
				if(!building || !building_currently_visible(echo.player, building)
				   || building->type->isVirtual || building->type->isBuildingSite)
					continue;
				consider_relief(team, building->posX+building->type->width/2,
					building->posY+building->type->height/2,
					tactical_building_value(building->type->shortTypeNum,
						strategy.tactics), building->underAttackTimer!=0);
			}
			for(int unit_id=0; unit_id<Unit::MAX_COUNT; ++unit_id)
			{
				Unit* unit=allied_team->myUnits[unit_id];
				if(!unit || unit->typeNum==EXPLORER
				   || !echo.player->map->isFOWDiscovered(unit->posX, unit->posY,
					echo.player->team->me)
				   || !unit->underAttackTimer)
					continue;
				consider_relief(team, unit->posX, unit->posY,
					strategy.teamplay.defense_unit_value, true);
			}
		}

	}

	// Progress commits us to a living opponent, never an eliminated one.
	// Share this gate with dig-out selection so a stale campaign cannot block
	// both direct attacks and opening routes to the remaining enemies.
	const Recon::OpponentIntel* campaign_intel=
		reconnaissance.opponent(campaign.target_team);
	const bool progress_lock=strategy.tactics.siege_target_lock_enabled
		&& campaign_intel && campaign_intel->alive
		&& campaign.buildings_destroyed>0
		&& timer-campaign.last_progress_tick<=strategy.tactics.siege_target_lock_ticks;
	const bool can_siege=strategy.tactics.siege_enabled && !active_relief
		&& deployable>=strategy.tactics.siege_min_force;

	if(can_siege)
	{
		const Recon::ReconReport& report=reconnaissance.report();
		for(std::map<int, Recon::OpponentIntel>::const_iterator opponent=
			report.opponents.begin(); opponent!=report.opponents.end(); ++opponent)
		{
			if(!opponent->second.alive)
				continue;
			if(progress_lock && opponent->first!=campaign.target_team)
			{

				continue;
			}
			if(replacement_siege_team>=0
			   && opponent->first!=replacement_siege_team)
			{

				continue;
			}

			const int unseen=std::max(0, opponent->second.estimatedWarriors
				-opponent->second.visibleWarriors);
			const int uncertainty=unseen*(100-opponent->second.confidence)
				*strategy.tactics.uncertainty_percent/10000;
			GradientInfo land_route_info;
			land_route_info.add_source(new Entities::AnyTeamBuilding(
				echo.player->team->teamNumber, CompletedBuildings));
			land_route_info.add_obstacle(new Entities::AnyResource);
			land_route_info.add_obstacle(new Entities::Water);
			Gradient& land_route=echo.get_gradient_manager().get_gradient(land_route_info);
			GradientInfo swim_route_info;
			swim_route_info.add_source(new Entities::AnyTeamBuilding(
				echo.player->team->teamNumber, CompletedBuildings));
			swim_route_info.add_obstacle(new Entities::AnyResource);
			Gradient& swim_route=echo.get_gradient_manager().get_gradient(swim_route_info);
			bool allied_player_pressure=false;
			for(std::map<int, Recon::BuildingSighting>::const_iterator pressured=
				opponent->second.buildings.begin();
				pressured!=opponent->second.buildings.end() && !allied_player_pressure;
				++pressured)
				allied_player_pressure=strategy.teamplay.enabled
					&& strategy.teamplay.pressure_coordination_enabled
					&& visible_allied_pressure_near(echo.player,
					pressured->second.x+pressured->second.width/2,
					pressured->second.y+pressured->second.height/2,
					strategy.teamplay.allied_pressure_radius).warriors>0;
			for(std::map<int, Recon::BuildingSighting>::const_iterator building=
				opponent->second.buildings.begin();
				building!=opponent->second.buildings.end(); ++building)
			{
				const Recon::BuildingSighting& sighting=building->second;

				if(Tactics::Program::targetQuarantined(sighting.gid, timer,
					strategy.tactics.failed_target_quarantine_enabled,
					attack_target_quarantine_until))
				{

					continue;
				}
				int local_power=0;
				// Threat observations already contain only hostile teams. Their
				// warriors can defend this location regardless of who owns it.
				for(std::vector<Tactics::ThreatSighting>::const_iterator threat=
					tactics.threats().begin(); threat!=tactics.threats().end(); ++threat)
					if(echo.player->map->warpDistSquare(sighting.x, sighting.y,
						threat->x, threat->y)<=strategy.tactics.siege_local_threat_radius
						*strategy.tactics.siege_local_threat_radius)
					{
						local_power+=threat->power;
					}
				int nearby_towers=0;
				for(enemy_team_iterator enemy(echo); enemy!=enemy_team_iterator(); ++enemy)
				{
					const Recon::OpponentIntel* hostile=reconnaissance.opponent(*enemy);
					if(!hostile || !hostile->alive) continue;
					for(std::map<int, Recon::BuildingSighting>::const_iterator tower=
						hostile->buildings.begin();
						tower!=hostile->buildings.end(); ++tower)
						if(tower->second.type==IntBuildingType::DEFENSE_BUILDING
						   && echo.player->map->warpDistSquare(sighting.x, sighting.y,
							tower->second.x, tower->second.y)<=strategy.tactics.siege_local_threat_radius
							*strategy.tactics.siege_local_threat_radius)
							++nearby_towers;
				}
				const int required_power=(local_power
					+(uncertainty+nearby_towers*2)*representative_power)
					*strategy.tactics.siege_strength_percent/100;
				int distance=land_route.get_height(sighting.x, sighting.y);
				const bool amphibious=distance<0;
				if(amphibious) distance=swim_route.get_height(sighting.x,sighting.y);
				const std::vector<int> reachable_powers=reachability.powersAt(
					echo.player->team,continuing_flag,
					sighting.x,sighting.y,deployable,amphibious);
				const int required=Tactics::Program::forceForPower(
					reachable_powers,
					required_power,strategy.tactics.siege_min_force,
					strategy.military.attack_unit_cap);


				if(distance<0 || required==0) continue;

				int value=tactical_building_value(sighting.type, strategy.tactics);
				if(sighting.construction)
					value+=strategy.tactics.target_construction_bonus;
				const bool allied_target_pressure=strategy.teamplay.enabled
					&& strategy.teamplay.pressure_coordination_enabled
					&& visible_allied_pressure_near(
					echo.player, sighting.x+sighting.width/2,
					sighting.y+sighting.height/2,
					strategy.teamplay.allied_pressure_radius).warriors>0;
				const int score=value+opponents[opponent->first].score
					+(allied_player_pressure
						? strategy.teamplay.siege_player_pressure_bonus : 0)
					+(allied_target_pressure
						? strategy.teamplay.siege_building_pressure_bonus : 0)
					-nearby_towers*strategy.tactics.target_tower_penalty
					-distance*strategy.tactics.route_distance_weight;
				if(score>siege_score)
				{
					siege_score=score;
					siege_gid=sighting.gid;
					siege_team=opponent->first;
					siege_x=echo.player->map->normalizeX(sighting.x+sighting.width/2);
					siege_y=echo.player->map->normalizeY(sighting.y+sighting.height/2);
					siege_requested=std::min(strategy.military.attack_unit_cap,
						std::max(required, strategy.tactics.siege_min_force));
					siege_player_pressure=allied_player_pressure;
					siege_target_pressure=allied_target_pressure;

				}
			}
		}

	}

	const bool choose_siege=siege_gid!=-1
		&& (replacement_siege_team>=0 || raid_score==INT_MIN
			|| siege_score>=raid_score);
	const int offense_score=choose_siege ? siege_score : raid_score;
	const bool choose_relief=relief.team>=0
		&& (active_relief || relief.score>offense_score);
	if(choose_relief)
	{
		budget.tactical_kind=Tactics::MissionRelief;
		budget.tactical_target_team=relief.team;
		budget.tactical_target_gid=-1;
		budget.tactical_target_x=relief.x;
		budget.tactical_target_y=relief.y;
		budget.tactical_candidate_score=relief.score;
		budget.tactical_requested_force=relief.requested;
		budget.tactical_minimum_force=strategy.teamplay.defense_min_force;
		budget.tactical_contact_visible=true;
		budget.tactical_visible_enemy_warriors=relief.enemies;
		budget.tactical_visible_enemy_power=relief.enemyPower;
		budget.tactical_allied_warriors=relief.allies;
		budget.tactical_allied_power=relief.alliedPower;
		budget.tactical_route_distance=relief.route;

	}
	else if(active_relief)
	{
		// Preserve the active mission while its short contact TTL expires. The
		// executor owns withdrawal and cooldown transitions.
		{}

		return;
	}
	else if(choose_siege)
	{
		budget.tactical_kind=Tactics::MissionSiege;
		budget.tactical_target_team=siege_team;
		budget.tactical_target_gid=siege_gid;
		budget.tactical_target_x=siege_x;
		budget.tactical_target_y=siege_y;
		budget.tactical_candidate_score=siege_score;
		budget.tactical_requested_force=siege_requested;
		budget.tactical_minimum_force=strategy.tactics.siege_min_force;
		budget.tactical_allied_player_pressure=siege_player_pressure;
		budget.tactical_allied_target_pressure=siege_target_pressure;

	}
	else if(raid_score!=INT_MIN)
	{
		budget.tactical_kind=Tactics::MissionRaid;
		budget.tactical_target_team=raid.team;
		budget.tactical_target_x=raid.x;
		budget.tactical_target_y=raid.y;
		// The mission stores the raw cluster score so later retarget hysteresis
		// compares like with like. Route and outskirts modifiers above decide
		// whether a raid beats a siege, but are not properties of a moving cluster.
		budget.tactical_candidate_score=raid.score;
		budget.tactical_requested_force=Tactics::Program::desiredRaidForce(
			raid.workers, strategy.raiding.force_bonus, strategy.raiding.min_force,
			std::min(strategy.raiding.max_force, deployable));
		budget.tactical_minimum_force=strategy.raiding.min_force;

	}
	else
	{
		int dig_out_team=-1;
		int dig_out_score=INT_MIN;
		if(strategy.tactics.dig_out_enabled && can_siege && budget.attack_flags>0
		   && budget.attack_clearing_workers>0)
		{
			for(int team=0; team<Team::MAX_COUNT; ++team)
			{
				const OpponentAssessment& opponent=opponents[team];
				if(!opponent.alive || opponent.known_buildings<=0
				   || opponent.reachable_buildings!=0 || opponent.score==INT_MIN
				   || (progress_lock && team!=campaign.target_team)
				   || (replacement_siege_team>=0
					&& team!=replacement_siege_team))
					continue;
				if(opponent.score>dig_out_score)
				{
					dig_out_team=team;
					dig_out_score=opponent.score;
				}
			}
		}
		if(dig_out_team>=0)
		{
			budget.tactical_dig_out_team=dig_out_team;

		}

	}
}

void Maxima::evaluate_strategy(Context& echo)
{
	if(strategy.reconnaissance.enabled)
		update_reconnaissance(echo);
	else
	{
		if(!reconnaissance.report().missions.empty())
			remove_reconnaissance_missions(echo, "disabled");
		reconnaissance.reset();
		tactics.reset();
	}
	prune_cleared_enemy_sites();
	StrategicSnapshot next=collect_snapshot(echo);
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
	update_environment_model(echo);
	const bool bomb_visible=strategy.military.explorer_defense_enabled
		&& (snapshot.visible_enemy_attack_explorers
			>=strategy.reconnaissance.explorer_attack_warning_threshold
		|| snapshot.visible_colony_explorer_threat
			>=strategy.reconnaissance.explorer_colony_warning_threshold);
	const bool bomb_at_colony=strategy.military.explorer_defense_enabled
		&& snapshot.visible_colony_explorer_threat
			>=strategy.reconnaissance.explorer_colony_warning_threshold;

	if(bomb_visible)
		explorer_threat_until=timer
			+strategy.scheduling.explorer_warning_duration_ticks;
	if(bomb_at_colony)
		explorer_colony_threat_until=timer
			+strategy.scheduling.explorer_colony_warning_duration_ticks;

	update_opponent_models(echo);
	score_demands();
	score_postures();

	select_posture();
	allocate_resources();
	finalize_director_plan(echo);
	plan_tactical_authorization(echo);
	// Explorer strikes share the offensive target. Keep the existing strategic
	// target selector available when there is no warrior mission to follow.
	if(budget.tactical_kind==Tactics::MissionRaid
	   || budget.tactical_kind==Tactics::MissionSiege)
		target=budget.tactical_target_team;
	else
		choose_enemy_target(echo);
	plan_reconnaissance_objectives(echo);

}

Maxima::Maxima(Player *player)
	: context(player)
{
	assert(player && player->game);
	const ResolvedStrategy& resolved=player->game->resolveMaximaStrategy(
		player->number);
	strategy=resolved.values;
	budget.resource_tracker_samples=strategy.staffing.resource_tracker_samples;
	budget.swarm_supply_radius=
		strategy.staffing.swarm_supply_radius;
	budget.attack_clearing_workers=strategy.staffing.attack_clearing_workers;
	reconnaissance.configure(strategy.reconnaissance.memory_horizon_ticks,
		strategy.reconnaissance.force_memory_hold_ticks,
		strategy.reconnaissance.stale_contact_age_ticks,
		strategy.reconnaissance.force_memory_enabled);
	timer=0;
	posture=PostureExpand;
	posture_since=0;
	director=StrategyDirector();
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
	dynamic_plan_weight=0;
	target_dynamic_plan_weight=0;
	growth_phase=false;
	skilled_work_phase=false;
	upgrading_phase_1=false;
	upgrading_phase_2=false;
	war_preperation=false;
	war=false;
	fruit_phase=false;
	starving_recovery=false;
	no_workers_phase=false;
	can_swim=false;
	defend_explorers=false;
	explorer_attack_preperation_phase=false;
	explorer_attack_phase=false;
	starving_recovery_inns=0;
	buildings_under_construction=0;
	for(int n=0; n<LegacyPlacementSize; ++n)
		buildings_under_construction_per_type[n]=0;
	development_planner_initialized=false;
	development_reported_states.clear();
	remote_swarm_since.clear();
	remote_swarms_ready.clear();
	remote_swarm_deletion_issued.clear();
	recent_construction_failures=0;
	last_construction_failure_tick=-1000000;
	opening_space_constrained=false;
	proactive_clearing_flag=-1;
	proactive_clearing_started_tick=-1000000;
	proactive_clearing_initial_wood=0;

	last_proactive_clearing_tick=-1000000;
	exploration_on_fruit=false;
	target=-1;
	attack_flags.clear();
	is_digging_out=false;
	preemptive_guard_tiles.clear();
	last_preemptive_defense_tick=-1000000;
	preemptive_building_signature=0;
	last_preemptive_effective_zone_max=-1;
	last_preemptive_amphibious_active=false;
	farming_topology_signature=0;
	applied_maintenance_clearing_mask.clear();
	maintenance_circulation_mask.clear();
	wood_firebreak_mask.clear();
	wheat_farm_protection_mask.clear();
	farming_shoreline_mask.clear();
	farming_cardinal_shoreline_mask.clear();
	last_farming_tick=-1000000;
	last_barrier_topology_tick=-1000000;
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

	last_colony_accounted_action_id=0;


}

Maxima::Maxima(GAGCore::InputStream *stream, Player *player,
	Sint32 versionMinor)
	: Maxima(player)
{
	const bool loaded=load(stream, player, versionMinor);
	assert(loaded);
}

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
	a("budget.desired_hospitals",budget.desired_hospitals);
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
	a("budget.inn_adaptive_staffing_enabled",budget.inn_adaptive_staffing_enabled);
	a("budget.swarm_retirement_enabled",budget.swarm_retirement_enabled);
	a("budget.inn_low_corn_threshold",budget.inn_low_corn_threshold);
	a("budget.inn_normal_workers",budget.inn_normal_workers);
	a("budget.inn_low_corn_workers",budget.inn_low_corn_workers);
	a("budget.resource_tracker_samples",budget.resource_tracker_samples);
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
	a("budget.campaign_stall_ticks",budget.campaign_stall_ticks);
	a("budget.campaign_retreat_cooldown_ticks",budget.campaign_retreat_cooldown_ticks);
	a("budget.target_switch_margin",budget.target_switch_margin);
	a("budget.tower_barrier_bonus",budget.tower_barrier_bonus);
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
	a("budget.farming_barrier_enabled",budget.farming_barrier_enabled);
	a("budget.farming_coastal_porosity_enabled",budget.farming_coastal_porosity_enabled);
	a("budget.farming_gate_clearing_enabled",budget.farming_gate_clearing_enabled);
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
	a("budget.farming_gate_clearing_radius",budget.farming_gate_clearing_radius);
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
	a("budget.barrier_topology_interval",budget.barrier_topology_interval);
	a("budget.farming_gate_relocation_penalty_cap",budget.farming_gate_relocation_penalty_cap);
	a("budget.priority_inns",budget.priority_inns);
	a("budget.priority_swarms",budget.priority_swarms);
	a("budget.priority_barracks",budget.priority_barracks);
	a("budget.priority_schools",budget.priority_schools);
	a("budget.priority_pools",budget.priority_pools);
	a("budget.priority_racetracks",budget.priority_racetracks);
	a("budget.priority_hospitals",budget.priority_hospitals);
	a("budget.priority_towers",budget.priority_towers);
	a("budget.tactics_enabled",budget.tactics_enabled);
	a("budget.raid_enabled",budget.raid_enabled);
	a("budget.siege_enabled",budget.siege_enabled);
	a("budget.siege_target_lock_enabled",budget.siege_target_lock_enabled);
	a("budget.teamplay_enabled",budget.teamplay_enabled);
	a("budget.teamplay_defense_enabled",budget.teamplay_defense_enabled);
	a("budget.tactical_target_team",budget.tactical_target_team);
	a("budget.tactical_dig_out_team",budget.tactical_dig_out_team);
	a("budget.tactical_target_gid",budget.tactical_target_gid);
	a("budget.tactical_target_x",budget.tactical_target_x);
	a("budget.tactical_target_y",budget.tactical_target_y);
	a("budget.tactical_candidate_score",budget.tactical_candidate_score);
	a("budget.tactical_requested_force",budget.tactical_requested_force);
	a("budget.tactical_minimum_force",budget.tactical_minimum_force);
	a("budget.tactical_contact_visible",budget.tactical_contact_visible);
	a("budget.tactical_allied_player_pressure",budget.tactical_allied_player_pressure);
	a("budget.tactical_allied_target_pressure",budget.tactical_allied_target_pressure);
	a("budget.tactical_visible_enemy_warriors",budget.tactical_visible_enemy_warriors);
	a("budget.tactical_visible_enemy_power",budget.tactical_visible_enemy_power);
	a("budget.tactical_allied_warriors",budget.tactical_allied_warriors);
	a("budget.tactical_allied_power",budget.tactical_allied_power);
	a("budget.tactical_route_distance",budget.tactical_route_distance);
	a("budget.tactical_review_interval",budget.tactical_review_interval);
	a("budget.tactical_rally_radius",budget.tactical_rally_radius);
	a("budget.tactical_siege_radius",budget.tactical_siege_radius);
	a("budget.tactical_siege_muster_percent",budget.tactical_siege_muster_percent);
	a("budget.tactical_siege_strength_percent",budget.tactical_siege_strength_percent);
	a("budget.tactical_siege_casualty_percent",budget.tactical_siege_casualty_percent);
	a("budget.tactical_siege_threat_radius",budget.tactical_siege_threat_radius);
	a("budget.tactical_target_lock_ticks",budget.tactical_target_lock_ticks);
	a("budget.raid_flag_radius",budget.raid_flag_radius);
	a("budget.raid_muster_percent",budget.raid_muster_percent);
	a("budget.raid_muster_timeout",budget.raid_muster_timeout);
	a("budget.raid_contact_ttl",budget.raid_contact_ttl);
	a("budget.raid_max_engagement",budget.raid_max_engagement);
	a("budget.raid_casualty_percent",budget.raid_casualty_percent);
	a("budget.raid_survivor_min",budget.raid_survivor_min);
	a("budget.raid_cooldown",budget.raid_cooldown);
	a("budget.raid_defender_min",budget.raid_defender_min);
	a("budget.raid_defender_percent",budget.raid_defender_percent);
	a("budget.raid_building_buffer",budget.raid_building_buffer);
	a("budget.raid_tower_buffer",budget.raid_tower_buffer);
	a("budget.raid_retarget_margin",budget.raid_retarget_margin);
	a("budget.relief_contact_ttl",budget.relief_contact_ttl);
	a("budget.relief_max_engagement",budget.relief_max_engagement);
	a("budget.relief_cooldown",budget.relief_cooldown);
	a("budget.relief_follow_radius",budget.relief_follow_radius);
	a("budget.relief_retarget_margin",budget.relief_retarget_margin);
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
	a("remote_swarm_since",remote_swarm_since);
	a("remote_swarms_ready",remote_swarms_ready);
	a("remote_swarm_deletion_issued",remote_swarm_deletion_issued);
	a("operating_colonies",operating_colonies);
	a("last_preemptive_effective_zone_max",last_preemptive_effective_zone_max);
	a("last_preemptive_amphibious_active",last_preemptive_amphibious_active);
	a("applied_farm_protection_mask",applied_farm_protection_mask);
	a("applied_maintenance_clearing_mask",applied_maintenance_clearing_mask);
	a("maintenance_circulation_mask",maintenance_circulation_mask);
	a("wood_firebreak_mask",wood_firebreak_mask);
	a("strategic_barrier_mask",strategic_barrier_mask);
	a("strategic_gate_mask",strategic_gate_mask);
	a("emergency_escape_mask",emergency_escape_mask);
	a("farm_protection_mask",farm_protection_mask);
	a("wheat_farm_protection_mask",wheat_farm_protection_mask);
	a("strategic_gates",strategic_gates);
	a("strategic_gate_routes",strategic_gate_routes);
	a("barrier_defense_points",barrier_defense_points);
	a("barrier_geometry_signature",barrier_geometry_signature);
	a("strategic_gate_approaches",strategic_gate_approaches);
	a("barrier_tower_quality",barrier_tower_quality);
	a("gate_defense_demand",gate_defense_demand);
	a("settlement_access_routes",settlement_access_routes);
	a("farming_topology_signature",farming_topology_signature);
	a("last_farming_tick",last_farming_tick);
	a("last_barrier_topology_tick",last_barrier_topology_tick);
	a("farming_urgent",farming_urgent);
	a("land_clearing_pending",land_clearing_pending);
	a("maintenance_clearing_pending",maintenance_clearing_pending);
	a("development_cycle_pending",development_cycle_pending);
	a("preemptive_defense_pending",preemptive_defense_pending);
	a("reactive_defense_pending",reactive_defense_pending);
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
    if(!StrategyResolver::restoreValues(savedStrategy,strategy,error))
        throw std::runtime_error(error);
    // Reconfigure derived policies before restoring incremental work. Local
    // configuration may have changed since this game was saved.
    configure_development_planner();
    reconnaissance.configure(strategy.reconnaissance.memory_horizon_ticks,
        strategy.reconnaissance.force_memory_hold_ticks,
        strategy.reconnaissance.stale_contact_age_ticks,
        strategy.reconnaissance.force_memory_enabled);
    AIMaximaContinuation::Reader archive(stream);
    executionState(archive);
    development_planner.loadExecutionState(stream);
    context.loadExecutionState(stream, versionMinor);
    stream->readLeaveSection();

}

std::shared_ptr<Order> Maxima::getOrder()
{
	return context.getOrder(*this);
}

void Maxima::saveDirector(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("Director");
#define V3_SNAPSHOT_FIELDS(DO) DO(tick) DO(population) DO(workers) DO(free_warriors) DO(explorers) DO(trained_explorers) DO(warriors) DO(trained_workers) DO(trained_workers_level2) DO(trained_warriors) DO(swimming_workers) DO(swimming_explorers) DO(swimming_warriors) DO(amphibious_attack_explorers) DO(free_workers) DO(worker_jobs_open) DO(hungry) DO(critical_food) DO(unserved_food) DO(need_heal) DO(buildings) DO(building_sites) DO(swarms) DO(inns) DO(inn_level1) DO(inn_level2) DO(inn_level3) DO(barracks) DO(schools) DO(school_level1) DO(school_level2) DO(school_level3) DO(pools) DO(hospitals) DO(racetracks) DO(towers) DO(own_buildings_under_attack) DO(own_units_under_attack) DO(visible_enemy_warriors) DO(visible_enemy_explorers) DO(visible_enemy_attack_explorers) DO(visible_colony_explorer_threat) DO(visible_colony_threat) DO(alive_enemies) DO(total_hp) DO(attack_power) DO(prestige) DO(enemy_prestige) DO(tower_stone) DO(tower_bullets)
	stream->writeEnterSection("snapshot");
#define WRITE_SNAPSHOT(field) stream->writeSint32(snapshot.field,#field);
	V3_SNAPSHOT_FIELDS(WRITE_SNAPSHOT)
#undef WRITE_SNAPSHOT
	stream->writeLeaveSection();stream->writeEnterSection("previous_snapshot");
#define WRITE_PREVIOUS(field) stream->writeSint32(previous_snapshot.field,#field);
	V3_SNAPSHOT_FIELDS(WRITE_PREVIOUS)
#undef WRITE_PREVIOUS
	stream->writeLeaveSection();
	stream->writeEnterSection("trends");stream->writeSint32(trends.population,"population");stream->writeSint32(trends.workers,"workers");stream->writeSint32(trends.warriors,"warriors");stream->writeSint32(trends.food_pressure,"food_pressure");stream->writeSint32(trends.colony_pressure,"colony_pressure");stream->writeLeaveSection();
#define V3_ENV_FIELDS(DO) DO(known_tiles) DO(accessible_corn) DO(accessible_wood) DO(accessible_stone) DO(accessible_algae) DO(buildable_tiles) DO(water_tiles) DO(feeding_capacity) DO(food_headroom) DO(resource_capacity) DO(space_capacity) DO(food_security) DO(abundance) DO(terrain_abundance) DO(connected_abundance) DO(mobility_opportunity) DO(economic_momentum) DO(mobility_constraint) DO(topology_complexity) DO(threat_pressure) DO(confidence)
	stream->writeEnterSection("environment");
#define WRITE_ENV(field) stream->writeSint32(environment.field,#field);
	V3_ENV_FIELDS(WRITE_ENV)
#undef WRITE_ENV
	stream->writeLeaveSection();
	stream->writeEnterSection("demands");stream->writeSint32(demands.survival,"survival");stream->writeSint32(demands.food,"food");stream->writeSint32(demands.growth,"growth");stream->writeSint32(demands.expansion,"expansion");stream->writeSint32(demands.access,"access");stream->writeSint32(demands.technology,"technology");stream->writeSint32(demands.mobility,"mobility");stream->writeSint32(demands.military,"military");stream->writeSint32(demands.aggression,"aggression");stream->writeLeaveSection();
	stream->writeEnterSection("opponents");for(int i=0;i<Team::MAX_COUNT;++i){stream->writeEnterSection(i);const OpponentAssessment& o=opponents[i];stream->writeUint8(o.alive,"alive");stream->writeSint32(o.visible_warriors,"visible_warriors");stream->writeSint32(o.estimated_warriors,"estimated_warriors");stream->writeSint32(o.last_observed_warriors,"last_observed_warriors");stream->writeSint32(o.visible_explorers,"visible_explorers");stream->writeSint32(o.visible_buildings,"visible_buildings");stream->writeSint32(o.known_buildings,"known_buildings");stream->writeSint32(o.reachable_buildings,"reachable_buildings");stream->writeSint32(o.strategic_value,"strategic_value");stream->writeSint32(o.nearest_building,"nearest_building");stream->writeSint32(o.score,"score");stream->writeSint32(o.last_seen_tick,"last_seen_tick");stream->writeSint32(o.last_force_seen_tick,"last_force_seen_tick");stream->writeSint32(o.last_building_seen_tick,"last_building_seen_tick");stream->writeSint32(o.intel_confidence,"intel_confidence");stream->writeLeaveSection();}stream->writeLeaveSection();
	stream->writeEnterSection("campaign");stream->writeSint32(campaign.state,"state");stream->writeSint32(campaign.target_team,"target_team");stream->writeSint32(campaign.started_tick,"started_tick");stream->writeSint32(campaign.last_progress_tick,"last_progress_tick");stream->writeSint32(campaign.last_target_buildings,"last_target_buildings");stream->writeSint32(campaign.buildings_destroyed,"buildings_destroyed");stream->writeSint32(campaign.cooldown_until,"cooldown_until");stream->writeLeaveSection();
	stream->writeEnterSection("Tactics");stream->writeSint32(tactical_mission.kind,"kind");stream->writeSint32(tactical_mission.phase,"phase");stream->writeSint32(tactical_mission.flagId,"flag_id");stream->writeSint32(tactical_mission.targetTeam,"target_team");stream->writeSint32(tactical_mission.targetGid,"target_gid");stream->writeSint32(tactical_mission.targetX,"target_x");stream->writeSint32(tactical_mission.targetY,"target_y");stream->writeSint32(tactical_mission.rallyX,"rally_x");stream->writeSint32(tactical_mission.rallyY,"rally_y");stream->writeSint32(tactical_mission.requestedForce,"requested_force");stream->writeSint32(tactical_mission.minimumForce,"minimum_force");stream->writeSint32(tactical_mission.launchedForce,"launched_force");stream->writeSint32(tactical_mission.startedTick,"started_tick");stream->writeSint32(tactical_mission.phaseSinceTick,"phase_since_tick");stream->writeSint32(tactical_mission.lastContactTick,"last_contact_tick");stream->writeSint32(tactical_mission.lastProgressTick,"last_progress_tick");stream->writeSint32(tactical_mission.cooldownUntil,"cooldown_until");stream->writeSint32(tactical_mission.initialTargetWorkers,"initial_target_workers");stream->writeSint32(tactical_mission.candidateScore,"candidate_score");stream->writeSint32(tactical_mission.lastTargetHp,"last_target_hp");stream->writeLeaveSection();
	stream->writeSint32(posture,"posture");stream->writeSint32(posture_since,"posture_since");for(int i=0;i<PostureCount;++i)stream->writeSint32(posture_utilities[i],FormattableString("posture_utility_%0").arg(i).c_str());
#define WRITE_SCALAR(field) stream->writeSint32(field,#field);
	WRITE_SCALAR(explorer_threat_until) WRITE_SCALAR(explorer_colony_threat_until) WRITE_SCALAR(global_water_percent) WRITE_SCALAR(global_shoreline_density) WRITE_SCALAR(global_land_components) WRITE_SCALAR(global_largest_land_percent) WRITE_SCALAR(global_start_land_percent) WRITE_SCALAR(global_chokepoint_density) WRITE_SCALAR(global_water_tiles) WRITE_SCALAR(global_grass_tiles) WRITE_SCALAR(global_land_tiles) WRITE_SCALAR(global_buildable_tiles) WRITE_SCALAR(global_corn_tiles) WRITE_SCALAR(global_wood_tiles) WRITE_SCALAR(global_stone_tiles) WRITE_SCALAR(global_algae_tiles) WRITE_SCALAR(global_fruit_tiles) WRITE_SCALAR(global_terrain_abundance) WRITE_SCALAR(global_connected_abundance) WRITE_SCALAR(global_mobility_opportunity) WRITE_SCALAR(recent_construction_failures) WRITE_SCALAR(last_construction_failure_tick) WRITE_SCALAR(proactive_clearing_flag) WRITE_SCALAR(proactive_clearing_started_tick) WRITE_SCALAR(proactive_clearing_initial_wood) WRITE_SCALAR(last_proactive_clearing_tick) WRITE_SCALAR(last_preemptive_defense_tick)
#undef WRITE_SCALAR
	stream->writeUint8(large_economy_committed,"large_economy_committed");stream->writeUint8(topology_initialized,"topology_initialized");stream->writeUint8(opening_space_constrained,"opening_space_constrained");stream->writeUint32(preemptive_building_signature,"preemptive_building_signature");
	auto writeIntMap=[stream](const char* name,const std::map<int,int>& values){stream->writeEnterSection(name);stream->writeUint32(values.size(),"size");size_t n=0;for(std::map<int,int>::const_iterator i=values.begin();i!=values.end();++i,++n){stream->writeEnterSection(n);stream->writeSint32(i->first,"key");stream->writeSint32(i->second,"value");stream->writeLeaveSection();}stream->writeLeaveSection();};
	writeIntMap("attack_flag_targets",attack_flag_targets);writeIntMap("attack_flag_started_ticks",attack_flag_started_ticks);writeIntMap("attack_flag_last_hp",attack_flag_last_hp);writeIntMap("attack_flag_last_progress",attack_flag_last_progress);writeIntMap("attack_target_quarantine_until",attack_target_quarantine_until);
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
#undef V3_ENV_FIELDS
#undef V3_SNAPSHOT_FIELDS
}

bool Maxima::loadDirector(GAGCore::InputStream* stream,
	Sint32 versionMinor)
{
	stream->readEnterSection("Director");
#define V3_SNAPSHOT_FIELDS(DO) DO(tick) DO(population) DO(workers) DO(free_warriors) DO(explorers) DO(trained_explorers) DO(warriors) DO(trained_workers) DO(trained_workers_level2) DO(trained_warriors) DO(swimming_workers) DO(swimming_explorers) DO(swimming_warriors) DO(amphibious_attack_explorers) DO(free_workers) DO(worker_jobs_open) DO(hungry) DO(critical_food) DO(unserved_food) DO(need_heal) DO(buildings) DO(building_sites) DO(swarms) DO(inns) DO(inn_level1) DO(inn_level2) DO(inn_level3) DO(barracks) DO(schools) DO(school_level1) DO(school_level2) DO(school_level3) DO(pools) DO(hospitals) DO(racetracks) DO(towers) DO(own_buildings_under_attack) DO(own_units_under_attack) DO(visible_enemy_warriors) DO(visible_enemy_explorers) DO(visible_enemy_attack_explorers) DO(visible_colony_explorer_threat) DO(visible_colony_threat) DO(alive_enemies) DO(total_hp) DO(attack_power) DO(prestige) DO(enemy_prestige) DO(tower_stone) DO(tower_bullets)
	stream->readEnterSection("snapshot");
#define READ_SNAPSHOT(field) snapshot.field=stream->readSint32(#field);
	V3_SNAPSHOT_FIELDS(READ_SNAPSHOT)
#undef READ_SNAPSHOT
	stream->readLeaveSection();stream->readEnterSection("previous_snapshot");
#define READ_PREVIOUS(field) previous_snapshot.field=stream->readSint32(#field);
	V3_SNAPSHOT_FIELDS(READ_PREVIOUS)
#undef READ_PREVIOUS
	stream->readLeaveSection();

	stream->readEnterSection("trends");trends.population=stream->readSint32("population");trends.workers=stream->readSint32("workers");trends.warriors=stream->readSint32("warriors");trends.food_pressure=stream->readSint32("food_pressure");trends.colony_pressure=stream->readSint32("colony_pressure");stream->readLeaveSection();
#define V3_ENV_FIELDS(DO) DO(known_tiles) DO(accessible_corn) DO(accessible_wood) DO(accessible_stone) DO(accessible_algae) DO(buildable_tiles) DO(water_tiles) DO(feeding_capacity) DO(food_headroom) DO(resource_capacity) DO(space_capacity) DO(food_security) DO(abundance) DO(terrain_abundance) DO(connected_abundance) DO(mobility_opportunity) DO(economic_momentum) DO(mobility_constraint) DO(topology_complexity) DO(threat_pressure) DO(confidence)
	stream->readEnterSection("environment");
#define READ_ENV(field) environment.field=stream->readSint32(#field);
	V3_ENV_FIELDS(READ_ENV)
#undef READ_ENV
	stream->readLeaveSection();
	stream->readEnterSection("demands");demands.survival=stream->readSint32("survival");demands.food=stream->readSint32("food");demands.growth=stream->readSint32("growth");demands.expansion=stream->readSint32("expansion");demands.access=stream->readSint32("access");demands.technology=stream->readSint32("technology");demands.mobility=stream->readSint32("mobility");demands.military=stream->readSint32("military");demands.aggression=stream->readSint32("aggression");stream->readLeaveSection();
	stream->readEnterSection("opponents");for(int i=0;i<Team::MAX_COUNT;++i){stream->readEnterSection(i);OpponentAssessment& o=opponents[i];o.alive=stream->readUint8("alive");o.visible_warriors=stream->readSint32("visible_warriors");o.estimated_warriors=stream->readSint32("estimated_warriors");o.last_observed_warriors=stream->readSint32("last_observed_warriors");o.visible_explorers=stream->readSint32("visible_explorers");o.visible_buildings=stream->readSint32("visible_buildings");o.known_buildings=stream->readSint32("known_buildings");o.reachable_buildings=stream->readSint32("reachable_buildings");o.strategic_value=stream->readSint32("strategic_value");o.nearest_building=stream->readSint32("nearest_building");o.score=stream->readSint32("score");o.last_seen_tick=stream->readSint32("last_seen_tick");o.last_force_seen_tick=stream->readSint32("last_force_seen_tick");{o.last_building_seen_tick=stream->readSint32("last_building_seen_tick");o.intel_confidence=stream->readSint32("intel_confidence");}stream->readLeaveSection();}stream->readLeaveSection();
	stream->readEnterSection("campaign");campaign.state=static_cast<CampaignState>(stream->readSint32("state"));campaign.target_team=stream->readSint32("target_team");campaign.started_tick=stream->readSint32("started_tick");campaign.last_progress_tick=stream->readSint32("last_progress_tick");campaign.last_target_buildings=stream->readSint32("last_target_buildings");campaign.buildings_destroyed=stream->readSint32("buildings_destroyed");campaign.cooldown_until=stream->readSint32("cooldown_until");stream->readLeaveSection();
	{stream->readEnterSection("Tactics");tactical_mission.kind=static_cast<Tactics::MissionKind>(stream->readSint32("kind"));tactical_mission.phase=static_cast<Tactics::MissionPhase>(stream->readSint32("phase"));tactical_mission.flagId=stream->readSint32("flag_id");tactical_mission.targetTeam=stream->readSint32("target_team");tactical_mission.targetGid=stream->readSint32("target_gid");tactical_mission.targetX=stream->readSint32("target_x");tactical_mission.targetY=stream->readSint32("target_y");tactical_mission.rallyX=stream->readSint32("rally_x");tactical_mission.rallyY=stream->readSint32("rally_y");tactical_mission.requestedForce=stream->readSint32("requested_force");tactical_mission.minimumForce=stream->readSint32("minimum_force");tactical_mission.launchedForce=stream->readSint32("launched_force");tactical_mission.startedTick=stream->readSint32("started_tick");tactical_mission.phaseSinceTick=stream->readSint32("phase_since_tick");tactical_mission.lastContactTick=stream->readSint32("last_contact_tick");tactical_mission.lastProgressTick=stream->readSint32("last_progress_tick");tactical_mission.cooldownUntil=stream->readSint32("cooldown_until");tactical_mission.initialTargetWorkers=stream->readSint32("initial_target_workers");tactical_mission.candidateScore=stream->readSint32("candidate_score");tactical_mission.lastTargetHp=stream->readSint32("last_target_hp");stream->readLeaveSection();}
	posture=static_cast<StrategicPosture>(stream->readSint32("posture"));posture_since=stream->readSint32("posture_since");for(int i=0;i<PostureCount;++i)posture_utilities[i]=stream->readSint32(FormattableString("posture_utility_%0").arg(i).c_str());
#define READ_SCALAR(field) field=stream->readSint32(#field);
	READ_SCALAR(explorer_threat_until) READ_SCALAR(explorer_colony_threat_until) READ_SCALAR(global_water_percent) READ_SCALAR(global_shoreline_density) READ_SCALAR(global_land_components) READ_SCALAR(global_largest_land_percent) READ_SCALAR(global_start_land_percent) READ_SCALAR(global_chokepoint_density) READ_SCALAR(global_water_tiles) READ_SCALAR(global_grass_tiles) READ_SCALAR(global_land_tiles) READ_SCALAR(global_buildable_tiles) READ_SCALAR(global_corn_tiles) READ_SCALAR(global_wood_tiles) READ_SCALAR(global_stone_tiles) READ_SCALAR(global_algae_tiles) READ_SCALAR(global_fruit_tiles) READ_SCALAR(global_terrain_abundance) READ_SCALAR(global_connected_abundance) READ_SCALAR(global_mobility_opportunity) READ_SCALAR(recent_construction_failures) READ_SCALAR(last_construction_failure_tick) READ_SCALAR(proactive_clearing_flag) READ_SCALAR(proactive_clearing_started_tick) READ_SCALAR(proactive_clearing_initial_wood) READ_SCALAR(last_proactive_clearing_tick) READ_SCALAR(last_preemptive_defense_tick)
#undef READ_SCALAR

	large_economy_committed=stream->readUint8("large_economy_committed");topology_initialized=stream->readUint8("topology_initialized");opening_space_constrained=stream->readUint8("opening_space_constrained");preemptive_building_signature=stream->readUint32("preemptive_building_signature");
	auto readIntMap=[stream](const char* name,std::map<int,int>& values){values.clear();stream->readEnterSection(name);const Uint32 size=stream->readUint32("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);const int key=stream->readSint32("key");values[key]=stream->readSint32("value");stream->readLeaveSection();}stream->readLeaveSection();};
	readIntMap("attack_flag_targets",attack_flag_targets);readIntMap("attack_flag_started_ticks",attack_flag_started_ticks);readIntMap("attack_flag_last_hp",attack_flag_last_hp);readIntMap("attack_flag_last_progress",attack_flag_last_progress);readIntMap("attack_target_quarantine_until",attack_target_quarantine_until);
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
	reconnaissance.reset();Recon::ReconReport& recon=reconnaissance.mutableReport();
		stream->readEnterSection("Recon");recon.tick=stream->readSint32("tick");recon.visibleWarriors=stream->readSint32("visible_warriors");recon.visibleExplorers=stream->readSint32("visible_explorers");recon.visibleAttackExplorers=stream->readSint32("visible_attack_explorers");recon.visibleColonyThreat=stream->readSint32("visible_colony_threat");recon.visibleColonyExplorerThreat=stream->readSint32("visible_colony_explorer_threat");recon.aliveEnemies=stream->readSint32("alive_enemies");recon.exploredPercent=stream->readSint32("explored_percent");recon.desiredMissions=stream->readSint32("desired_missions");last_recon_mission_tick=stream->readSint32("last_mission_tick");reconnaissance_suspended=stream->readUint8("suspended");
		stream->readEnterSection("opponents");size=stream->readUint32("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);const int team=stream->readSint32("team");Recon::OpponentIntel& intel=recon.opponents[team];intel.alive=stream->readUint8("alive");intel.visibleWarriors=stream->readSint32("visible_warriors");intel.visibleExplorers=stream->readSint32("visible_explorers");intel.visibleAttackExplorers=stream->readSint32("visible_attack_explorers");intel.visibleBuildings=stream->readSint32("visible_buildings");intel.lastObservedWarriors=stream->readSint32("last_observed_warriors");intel.lastObservedExplorers=stream->readSint32("last_observed_explorers");intel.estimatedWarriors=stream->readSint32("estimated_warriors");intel.estimatedExplorers=stream->readSint32("estimated_explorers");intel.knownBuildings=stream->readSint32("known_buildings");intel.strategicValue=stream->readSint32("strategic_value");intel.reachableBuildings=stream->readSint32("reachable_buildings");intel.nearestBuilding=stream->readSint32("nearest_building");intel.lastSeenTick=stream->readSint32("last_seen_tick");intel.lastForceSeenTick=stream->readSint32("last_force_seen_tick");intel.lastWarriorSeenTick=stream->readSint32("last_warrior_seen_tick");intel.lastExplorerSeenTick=stream->readSint32("last_explorer_seen_tick");intel.lastBuildingSeenTick=stream->readSint32("last_building_seen_tick");{intel.lastEconomicSeenTick=stream->readSint32("last_economic_seen_tick");intel.lastEconomicX=stream->readSint32("last_economic_x");intel.lastEconomicY=stream->readSint32("last_economic_y");}intel.confidence=stream->readSint32("confidence");stream->readEnterSection("buildings");const Uint32 building_count=stream->readUint32("size");for(Uint32 b=0;b<building_count;++b){stream->readEnterSection(b);Recon::BuildingSighting sighting;sighting.gid=stream->readSint32("gid");sighting.team=stream->readSint32("team");sighting.type=stream->readSint32("type");sighting.x=stream->readSint32("x");sighting.y=stream->readSint32("y");sighting.width=stream->readSint32("width");sighting.height=stream->readSint32("height");sighting.construction=stream->readUint8("construction");sighting.lastSeenTick=stream->readSint32("last_seen_tick");sighting.currentlyVisible=stream->readUint8("currently_visible");intel.buildings[sighting.gid]=sighting;stream->readLeaveSection();}stream->readLeaveSection();stream->readLeaveSection();}stream->readLeaveSection();
		stream->readEnterSection("missions");size=stream->readUint32("size");for(Uint32 n=0;n<size;++n){stream->readEnterSection(n);Recon::ReconMission mission;mission.flagId=stream->readSint32("flag_id");mission.targetTeam=stream->readSint32("target_team");mission.frontier=stream->readUint8("frontier");mission.economicWatch=stream->readUint8("economic_watch");mission.x=stream->readSint32("x");mission.y=stream->readSint32("y");mission.createdTick=stream->readSint32("created_tick");mission.lastRetaskTick=stream->readSint32("last_retask_tick");recon.missions.push_back(mission);stream->readLeaveSection();}stream->readLeaveSection();stream->readLeaveSection();
	}
	stream->readLeaveSection();
#undef V3_ENV_FIELDS
#undef V3_BUDGET_FIELDS
#undef V3_SNAPSHOT_FIELDS
	// All saved plans are derived. Preserve durable campaign and tactical
	// ownership, then force a fresh v2 plan before any executor can run.
	director=StrategyDirector();
	return true;
}

bool Maxima::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("AIMaxima");
	context.load(stream, versionMinor);
	const bool loaded=loadState(stream, player, versionMinor);
	stream->readLeaveSection();
	return loaded;
}

bool Maxima::loadState(GAGCore::InputStream *stream, Player *player,
	Sint32 versionMinor)
{
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

	last_proactive_clearing_tick=-1000000;
	attack_flag_targets.clear();
	attack_flag_started_ticks.clear();
	attack_flag_last_hp.clear();
	attack_flag_last_progress.clear();
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
	strategic_barrier_mask.clear();
	strategic_gate_mask.clear();
	emergency_escape_mask.clear();
	farm_protection_mask.clear();
	wheat_farm_protection_mask.clear();
	farming_shoreline_mask.clear();
	farming_cardinal_shoreline_mask.clear();
	strategic_gates.clear();
	strategic_gate_routes.clear();
	strategic_gate_approaches.clear();
	settlement_access_routes.clear();
	barrier_tower_quality.clear();
	barrier_geometry_signature=0;
	gate_defense_demand=0;
	barrier_defense_points.clear();
	farming_topology_signature=0;
	last_farming_tick=-1000000;
	last_barrier_topology_tick=-1000000;
	farming_urgent=false;
	land_clearing_pending=false;
	maintenance_clearing_pending=false;
	development_cycle_pending=false;
	preemptive_defense_pending=false;
	reactive_defense_pending=false;
	reconnaissance.reset();
	last_recon_mission_tick=-1000000;
	reconnaissance_suspended=false;
	cleared_enemy_sites.clear();
	operating_colonies.clear();

	last_colony_accounted_action_id=0;


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

		exploration_on_fruit=stream->readUint8("exploration_on_fruit");
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

	stream->writeUint8(exploration_on_fruit, "exploration_on_fruit");
	saveDirector(stream);
	development_planner.save(stream);
	saveExecutionState(stream);
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}

void Maxima::tick(Context& echo)
{
	timer++;
	const int team=echo.player->team->teamNumber;
	// A new game and every loaded save start with no executable plan. Likewise,
	// completion events invalidate director assumptions. Replan before any
	// scheduled executor observes the plan.
	if(!director.initialized || director.dirty)
	{
		director.evaluate(*this, echo);
		update_reconnaissance_missions(echo);
	}
	if(timer==1)
	{
		initialize(echo);

	}
	if(strategy.reconnaissance.enabled && timer>1
	   && timer%strategy.reconnaissance.force_sample_interval_ticks
		==staggered_phase(
			strategy.reconnaissance.force_sample_phase_offset_ticks,
			strategy.reconnaissance.force_sample_interval_ticks,team))
		sample_reconnaissance_forces(echo);
	if(timer%strategy.scheduling.strategy_interval_ticks
		==staggered_phase(strategy.scheduling.strategy_phase_offset_ticks,
			strategy.scheduling.strategy_interval_ticks,team))
	{
		check_phases(echo);
		update_reconnaissance_missions(echo);
	}
	if(timer%strategy.scheduling.building_interval_ticks
		==staggered_phase(strategy.scheduling.building_phase_offset_ticks,
			strategy.scheduling.building_interval_ticks,team))
	{
		manage_buildings(echo);
	}
	if(budget.tactical_review_interval>0
	   && timer%budget.tactical_review_interval
		==staggered_phase(0,budget.tactical_review_interval,team))
	{
		control_attacks(echo);
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
		update_fruit_flags(echo);
	}
	if(timer%strategy.scheduling.explorer_attack_interval_ticks
		==staggered_phase(strategy.scheduling.explorer_attack_phase_offset_ticks,
			strategy.scheduling.explorer_attack_interval_ticks,team))
	{
		compute_explorer_flag_attack_positioning(echo);
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
		manage_land_clearing(echo);
	}
	else if(maintenance_clearing_pending)
	{
		maintenance_clearing_pending=false;
		update_maintenance_clearing_areas(echo);
	}
	else if(preemptive_defense_pending)
	{
		preemptive_defense_pending=false;
		update_preemptive_defense(echo);
	}
	else if(reactive_defense_pending)
	{
		reactive_defense_pending=false;
		compute_defense_flag_positioning(echo);
	}
	else if(farmingDue)
	{
		update_farming(echo);
		last_farming_tick=timer;
		land_clearing_pending=true;
		maintenance_clearing_pending=true;
	}
	else if(development_cycle_pending)
	{
		development_cycle_pending=false;
		const uint32_t previousSpatialRevision=
			development_planner.spatialRevision();
		development_cycle(echo);
		// Reservations are the placement state that changes clearing contracts.
		// Refresh immediately for those changes; otherwise the farming cadence is
		// sufficient and avoids another complete map pass after every review.
		maintenance_clearing_pending=maintenance_clearing_pending
			||previousSpatialRevision!=development_planner.spatialRevision();
	}
}

void Maxima::handle_event(Context& echo, const RuntimeEvent& event)
{
	if(event.type==RuntimeEvent::BuildingResolved
	   || event.type==RuntimeEvent::BuildingUpdated
	   || event.type==RuntimeEvent::AttackFinished
	   || event.type==RuntimeEvent::DigOutFinished)
		director.invalidate();
	if(event.type == RuntimeEvent::UpdateSwarm)
	{
		// Completing a swarm changes every share of the colony-wide budget.
		// Reconcile the existing producers as well as the completed building.
		BuildingSearch swarms(echo);
		swarms.add_condition(new SpecificBuildingType(IntBuildingType::SWARM_BUILDING));
		swarms.add_condition(new NotUnderConstruction);
		for(building_search_iterator swarm=swarms.begin(); swarm!=swarms.end(); ++swarm)
			manage_swarm(echo, *swarm);
	}
	if(event.type == RuntimeEvent::UpdateInn)
	{
		manage_inn(echo, event.first);
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

		if(target_it!=attack_flag_targets.end())
		{
			const int gid=target_it->second;
			finished_target=gid;
			const int team=Building::GIDtoTeam(gid);

			const int local=Building::GIDtoID(gid);
			if(team>=0 && team<Team::MAX_COUNT && echo.player->game->teams[team]
			   && local>=0 && local<Building::MAX_COUNT
			   && echo.player->game->teams[team]->myBuildings[local]==NULL)
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
		if(strategy.tactics.failed_target_quarantine_enabled
		   && finished_target!=-1 && duration>=0
		   && duration<=strategy.tactics.failed_target_max_duration_ticks
		   && (reason=="flag_removed" || reason=="unreachable"))
		{
			const int quarantine_until=
				timer+strategy.tactics.failed_target_quarantine_ticks;
			attack_target_quarantine_until[finished_target]=quarantine_until;
			campaign.cooldown_until=std::max(campaign.cooldown_until,
				timer+budget.campaign_retreat_cooldown_ticks);

		}

		attack_flag_targets.erase(id);
		attack_flag_started_ticks.erase(id);
		attack_flag_last_hp.erase(id);
		attack_flag_last_progress.erase(id);
		attack_flag_end_reasons.erase(id);
		if(tactical_flag)
		{
			const Tactics::MissionKind interrupted_kind=tactical_mission.kind;
			const int cooldown=tactical_mission.kind==Tactics::MissionRaid
				? budget.raid_cooldown
				: (tactical_mission.kind==Tactics::MissionRelief
					? budget.relief_cooldown : budget.campaign_retreat_cooldown_ticks);
			tactical_mission.reset();
			tactical_mission.phase=Tactics::PhaseCooldown;
			tactical_mission.phaseSinceTick=timer;
			tactical_mission.cooldownUntil=timer+cooldown;
			if(interrupted_kind==Tactics::MissionSiege)
				campaign.state=CampaignIdle;
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

void Maxima::initialize(Context& echo)
{
	initialize_farming_cache(echo);
	BuildingSearch bs(echo);
	for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
	{
		if(echo.get_building_register().get_type(*i)==IntBuildingType::SWARM_BUILDING)
		{
			ManagementOrder* mo_tracker=new AddResourceTracker(
				strategy.staffing.resource_tracker_samples, CORN, *i);
			mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, *i));
			echo.add_management_order(mo_tracker);
		}
		if(echo.get_building_register().get_type(*i)==IntBuildingType::FOOD_BUILDING)
		{
			ManagementOrder* mo_tracker=new AddResourceTracker(
				strategy.staffing.resource_tracker_samples, CORN, *i);
			mo_tracker->add_condition(new ParticularBuilding(new NotUnderConstruction, *i));
			echo.add_management_order(mo_tracker);
		}
	}

	manage_buildings(echo);
}

void Maxima::check_phases(Context& echo)
{
	director.evaluate(*this, echo);
}

const std::vector<AIMaximaPlacement::BuildingProfile>&
Maxima::collect_building_profiles() const
{
	using namespace AIMaximaPlacement;
	if(!development_building_profiles.empty())
		return development_building_profiles;
	const int types[]={IntBuildingType::SWARM_BUILDING,
		IntBuildingType::FOOD_BUILDING,IntBuildingType::HEAL_BUILDING,
		IntBuildingType::WALKSPEED_BUILDING,IntBuildingType::SWIMSPEED_BUILDING,
		IntBuildingType::ATTACK_BUILDING,IntBuildingType::SCIENCE_BUILDING,
		IntBuildingType::DEFENSE_BUILDING};
	for(size_t typeIndex=0;typeIndex<sizeof(types)/sizeof(types[0]);++typeIndex)
	{
		BuildingProfile profile;profile.buildingType=types[typeIndex];
		for(int level=1;level<=3;++level)
		{
			BuildingType* complete=globalContainer->buildingsTypes.getByType(
				IntBuildingType::typeFromShortNumber(profile.buildingType),level-1,false);
			BuildingType* site=globalContainer->buildingsTypes.getByType(
				IntBuildingType::typeFromShortNumber(profile.buildingType),level-1,true);
			if(!complete)break;
			BuildingLevelProfile value;value.level=level;
			value.engineType=site?globalContainer->buildingsTypes.getTypeNum(
				complete->type,level-1,true):-1;
			value.footprint=Footprint(complete->decLeft,complete->decTop,
				complete->width,complete->height);
			if(site)for(int resource=0;resource<5;++resource)
				value.constructionResources[resource]=site->maxResource[resource];
			if(complete->canFeedUnit&&complete->timeToFeedUnit>0)
				value.serviceThroughput=complete->canFeedUnit*100/complete->timeToFeedUnit;
			else if(complete->canHealUnit&&complete->timeToHealUnit>0)
				value.serviceThroughput=complete->canHealUnit*100/complete->timeToHealUnit;
			else if(profile.buildingType==IntBuildingType::SWARM_BUILDING
			   &&complete->unitProductionTime>0)
				value.serviceThroughput=1000/complete->unitProductionTime;
			else if(profile.buildingType==IntBuildingType::DEFENSE_BUILDING)
				value.serviceThroughput=complete->shootingRange
					+complete->shootDamage*std::max(1,complete->shootRhythm)/32
					+complete->maxBullets/4;
			else
				for(int ability=0;ability<NB_ABILITY;++ability)
					if(complete->upgrade[ability]>0&&complete->upgradeTime[ability]>0)
						value.serviceThroughput+=complete->upgrade[ability]*100
							/complete->upgradeTime[ability];
			value.durability=complete->hpMax;
			value.capability=complete->prestige;
			for(int ability=0;ability<NB_ABILITY;++ability)
				value.capability+=complete->upgrade[ability]>0;
			profile.levels.push_back(value);
			if(complete->nextLevel<0)
				break;
		}
		if(!profile.levels.empty())development_building_profiles.push_back(profile);
	}
	return development_building_profiles;
}

int Maxima::school_algae_requirement() const
{
	const std::vector<AIMaximaPlacement::BuildingProfile>& profiles=
		collect_building_profiles();
	for(size_t index=0; index<profiles.size(); ++index)
		if(profiles[index].buildingType==IntBuildingType::SCIENCE_BUILDING)
		{
			const AIMaximaPlacement::BuildingLevelProfile* initial=
				profiles[index].atLevel(1);
			return initial ? initial->constructionResources[ALGA] : INT_MAX;
		}
	return INT_MAX;
}

void Maxima::configure_development_planner()
{
	using namespace AIMaximaPlacement;
	development_planner.configure(collect_building_profiles(),
		IntBuildingType::FOOD_BUILDING, IntBuildingType::HEAL_BUILDING,
		IntBuildingType::SCIENCE_BUILDING, IntBuildingType::ATTACK_BUILDING,
		IntBuildingType::DEFENSE_BUILDING, IntBuildingType::SWARM_BUILDING);
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
	policy.towerGateWeight=strategy.placement.defensive_siting_enabled
		? strategy.military.tower_barrier_bonus : 0;
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
}

AIMaximaPlacement::WorldState Maxima::collect_development_world(
	Context& echo, uint32_t* signature) const
{
	using namespace AIMaximaPlacement;
	Map* map=echo.player->map;WorldState world;
	world.reset(map->getW(),map->getH());world.tick=timer;
	Uint32 worldSignature=2166136261u;
	add_preemptive_hash(worldSignature,Uint32(world.width));
	add_preemptive_hash(worldSignature,Uint32(world.height));
	// Final placement validation must see losses since the last strategy tick.
	for(int id=0; id<Unit::MAX_COUNT; ++id)
	{
		const Unit* worker=echo.player->team->myUnits[id];
		if(worker && worker->typeNum==WORKER && worker->performance[SWIM]>0)
			++world.swimmingBuilders;
	}
	world.accessibleSupplies[WOOD]=environment.accessible_wood;
	world.accessibleSupplies[CORN]=environment.accessible_corn;
	world.accessibleSupplies[STONE]=environment.accessible_stone;
	world.accessibleSupplies[ALGA]=accessible_algae_units;
	world.profiles=collect_building_profiles();
	const bool cachedFertility=fertility_cache.validFor(world.width,world.height);
	const std::vector<uint32_t>& fertilityValues=fertility_cache.values();
	for(int y=0;y<world.height;++y)for(int x=0;x<world.width;++x)
	{
		const int index=y*world.width+x;
		WorldTile& tile=world.tiles[index];const Tile& cell=map->getTile(x,y);
		tile.discovered=map->isMapDiscovered(x,y,echo.player->team->allies);
		// Terrain is immutable during a match. Classifying the already-fetched
		// cell avoids three wrapped MapInfo calls per tile on every planner scan.
		tile.water=cell.terrain>=256 && cell.terrain<272;
		tile.sand=cell.terrain>=128 && cell.terrain<144;
		tile.grass=cell.terrain<16;tile.occupied=cell.building!=NOGBID;
		tile.foodTraversable=!(cell.forbidden&echo.player->team->me)
			|| applied_farm_protection_mask[index];
		tile.gateCorridor=index<int(emergency_escape_mask.size())
			&&(emergency_escape_mask[index]||strategic_gate_mask[index]);
		tile.gateDefense=index<int(barrier_tower_quality.size())?barrier_tower_quality[index]:0;
		tile.ownOccupied=tile.occupied
			&&Building::GIDtoTeam(cell.building)==echo.player->team->teamNumber;
		if(cell.resource.type!=NO_RES_TYPE)
		{
			tile.resourceType=cell.resource.type;tile.resourceAmount=cell.resource.amount;
			tile.permanentResource=globalContainer->resourcesTypes.get(
				cell.resource.type)->eternal;
			tile.clearableResource=!tile.permanentResource;
		}
		const Uint32 flags=(tile.discovered?1u:0u)|(tile.grass?2u:0u)
			|(tile.water?4u:0u)|(tile.sand?8u:0u)
			|(tile.permanentResource?16u:0u)
			|(tile.clearableResource?32u:0u)|(tile.occupied?64u:0u)|(tile.foodTraversable?128u:0u)
			|(tile.gateCorridor?256u:0u);
		add_preemptive_hash(worldSignature,flags);
		add_preemptive_hash(worldSignature,Uint32(tile.gateDefense));
		add_preemptive_hash(worldSignature,Uint32(tile.resourceType+1));
		// Resource amounts fluctuate on virtually every harvest. Placement routes,
		// legality and blocked intents depend on resource presence, not stack size;
		// keep live amounts for scoring without invalidating topology caches.
		tile.fertility=cachedFertility?fertilityValues[index]:cell.fertility;
		int expansionNeighbors=0;
		if((tile.resourceType==CORN||tile.resourceType==WOOD)&&tile.resourceAmount>0)
		{
			expansionNeighbors=available_expansion_neighbors(echo,x,y);
			tile.farmCapacity=Farming::usefulExpansionCapacity(tile.fertility,
				tile.resourceAmount,expansionNeighbors,
				tile.resourceType==CORN);
		}
		if(tile.discovered && tile.grass && !tile.occupied
		   && tile.resourceType==CORN && tile.resourceAmount>0)
		{
			tile.foodOpportunity=tile.fertility;
			tile.farmCapacity=tile.fertility;
		}
		tile.protectedness=map->isGuardArea(x,y,echo.player->team->me)
			? strategy.placement.guard_area_protectedness
			: strategy.placement.baseline_protectedness;
	}
	// Gate coverage is represented by tile.gateDefense above. Do not mark
	// ordinary economic buildings as safe merely because they sit in an
	// enemy's intended approach corridor.

	for(int team=0;team<Team::MAX_COUNT;++team)
	{
		if(!echo.player->game->teams[team]
		   || !(echo.player->team->enemies&echo.player->game->teams[team]->me))continue;
		for(int id=0;id<Building::MAX_COUNT;++id)
		{
			Building* enemy=echo.player->game->teams[team]->myBuildings[id];
			if(!enemy||enemy->type->isVirtual
			   ||!(enemy->seenByMask&echo.player->team->allies))continue;
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
	const std::map<int,BuildingRecord>& found=echo.get_building_register().found();
	for(std::map<int,BuildingRecord>::const_iterator i=found.begin();i!=found.end();++i)
	{
		Building* building=echo.get_building_register().get_building(i->first);
		if(!building||building->type->isVirtual)continue;
		WorldBuilding value;value.id=i->first;value.gid=building->gid;
		value.buildingType=building->type->shortTypeNum;
		value.level=building->type->level+1;
		value.centerX=map->normalizeX(building->posX-building->type->decLeft);
		value.centerY=map->normalizeY(building->posY-building->type->decTop);
		value.hp=building->hp;value.hpMax=building->type->hpMax;
		value.age=i->second.age;value.site=building->type->isBuildingSite;
		value.upgrading=echo.get_building_register().is_building_upgrading(i->first);
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
		add_preemptive_hash(worldSignature,Uint32(world.index(
			building.centerX,building.centerY)));
	}
	if(signature)*signature=worldSignature;
	return world;
}

std::vector<AIMaximaPlacement::DevelopmentIntent>
Maxima::collect_development_intents(
	const AIMaximaPlacement::WorldState& world) const
{
	using namespace AIMaximaPlacement;
	std::vector<DevelopmentIntent> result;
	struct Demand {int type,desired,priority,workers;};
	const Demand demands[]={
		{IntBuildingType::FOOD_BUILDING,budget.desired_inns,
			budget.priority_inns,strategy.staffing.construction_inn_workers},
		{IntBuildingType::SWARM_BUILDING,budget.desired_swarms,
			budget.priority_swarms,strategy.staffing.construction_swarm_workers},
		{IntBuildingType::WALKSPEED_BUILDING,budget.desired_racetracks,
			budget.priority_racetracks,
			strategy.staffing.construction_large_workers},
		{IntBuildingType::SWIMSPEED_BUILDING,budget.desired_pools,
			budget.priority_pools,strategy.staffing.construction_large_workers},
		{IntBuildingType::SCIENCE_BUILDING,budget.desired_schools,
			budget.priority_schools,strategy.staffing.construction_training_workers},
		{IntBuildingType::ATTACK_BUILDING,budget.desired_barracks,
			budget.priority_barracks,strategy.staffing.construction_training_workers},
		{IntBuildingType::HEAL_BUILDING,budget.desired_hospitals,
			budget.priority_hospitals,strategy.staffing.construction_hospital_workers},
		{IntBuildingType::DEFENSE_BUILDING,budget.desired_towers,
			budget.priority_towers,strategy.staffing.construction_large_workers}};
	for(size_t i=0;i<sizeof(demands)/sizeof(demands[0]);++i)
	{
		int current=development_planner.committedBuildingCount(
			world,demands[i].type);
		if(demands[i].type==IntBuildingType::SWARM_BUILDING)
			for(size_t b=0;b<world.buildings.size();++b)
				if(!world.buildings[b].site
				   &&remote_swarms_ready.count(world.buildings[b].id))
					current=std::max(0,current-1);
		if(demands[i].desired>current)
		{
			DevelopmentIntent intent;intent.buildingType=demands[i].type;
			intent.unmetCount=demands[i].desired-current;
			intent.priority=clamp_score(demands[i].priority);
			intent.workers=demands[i].workers;
			if(demands[i].type==IntBuildingType::SCIENCE_BUILDING)
				intent.requiredResourceType=ALGA;
			intent.emergency=demands[i].type==IntBuildingType::FOOD_BUILDING
				?budget.recovery_active:demands[i].type==IntBuildingType::DEFENSE_BUILDING
				&&explorer_defense_active();
			result.push_back(intent);
		}
	}
	if(budget.colony_swarm_requested
	   && development_planner.activeBuildCount(IntBuildingType::SWARM_BUILDING,ColonySeed)==0)
	{
		DevelopmentIntent colony;
		colony.buildingType=IntBuildingType::SWARM_BUILDING;
		colony.purpose=ColonySeed;
		colony.requiredResourceType=CORN;
		colony.unmetCount=1;
		colony.workers=strategy.staffing.construction_swarm_workers;
		// Colony priority comes from new food / establishment cost in placement.
		colony.priority=0;
		result.push_back(colony);
	}

	return result;
}

AIMaximaPlacement::DevelopmentLimits Maxima::collect_development_limits(
	Context& echo) const
{
	using namespace AIMaximaPlacement;DevelopmentLimits limits;
	limits.newConstruction=budget.construction_sites;
	limits.allowUpgrades=budget.allow_upgrades;
	limits.allowLevel2Upgrades=budget.allow_level2_upgrades;
	limits.allowRepairs=strategy.repairs.enabled;
	const int weightedTypes[]={IntBuildingType::FOOD_BUILDING,
		IntBuildingType::HEAL_BUILDING,IntBuildingType::WALKSPEED_BUILDING,
		IntBuildingType::SWIMSPEED_BUILDING,IntBuildingType::ATTACK_BUILDING};
	const int firstWeights[]={budget.upgrade_level1_inn_weight,
		budget.upgrade_level1_hospital_weight,budget.upgrade_level1_racetrack_weight,
		budget.upgrade_level1_pool_weight,budget.upgrade_level1_barracks_weight};
	const int secondWeights[]={budget.upgrade_level2_inn_weight,
		budget.upgrade_level2_hospital_weight,budget.upgrade_level2_racetrack_weight,
		budget.upgrade_level2_pool_weight,budget.upgrade_level2_barracks_weight};
	for(size_t i=0;i<sizeof(weightedTypes)/sizeof(weightedTypes[0]);++i)
	{
		limits.upgradePriorities[std::make_pair(weightedTypes[i],1)]=firstWeights[i];
		limits.upgradePriorities[std::make_pair(weightedTypes[i],2)]=secondWeights[i];
	}
	TeamStat* stat=echo.player->team->stats.getLatestStat();
	const int can1=stat->upgradeState[BUILD][1]+stat->upgradeState[BUILD][2]
		+stat->upgradeState[BUILD][3];
	const int can2=stat->upgradeState[BUILD][2]+stat->upgradeState[BUILD][3];
	// Prestige comes from level-three schools. Preserve the workforce needed
	// for that upgrade, and apply the additional population gate after the first.
	bool hasPrestigeSchool=false;
	for(const auto& record:echo.get_building_register().found())
	{
		const Building* building=echo.get_building_register().get_building(record.first);
		if(building && building->type->shortTypeNum==IntBuildingType::SCIENCE_BUILDING
		   && building->type->level>=2)
			hasPrestigeSchool=true;
	}
	const bool prestigeAllowed=hasPrestigeSchool
		? can2>=budget.second_prestige_trained_workers
			&& stat->totalUnit>=budget.second_prestige_population_min
		: can2>=budget.first_prestige_trained_workers;
	if(!prestigeAllowed)
		limits.upgradePriorities[std::make_pair(IntBuildingType::SCIENCE_BUILDING,2)]=0;
	limits.level1Upgrades=(can1+budget.upgrade_level1_trained_units_per_slot/2)
		/std::max(1,budget.upgrade_level1_trained_units_per_slot);
	limits.level2Upgrades=limits.allowLevel2Upgrades
		?(can2+budget.upgrade_level2_trained_units_per_slot/2)
		/std::max(1,budget.upgrade_level2_trained_units_per_slot):0;
	const std::map<int,DevelopmentAction>& actions=development_planner.actions();
	for(std::map<int,DevelopmentAction>::const_iterator i=actions.begin();i!=actions.end();++i)
	{
		const ActionLifecycleState state=i->second.state;
		if(state!=ParcelReserved&&state!=CreateIssued&&state!=SiteObserved)continue;
		if(i->second.type==BuildCampusMember||i->second.type==BuildStandalone)
			++limits.activeNewConstruction;
		else if(i->second.type==UpgradeBuilding&&i->second.fromLevel==1)
			++limits.activeLevel1Upgrades;
		else if(i->second.type==UpgradeBuilding&&i->second.fromLevel==2)
			++limits.activeLevel2Upgrades;
	}
	limits.activeNewConstruction=std::max(limits.activeNewConstruction,
		snapshot.building_sites);
	return limits;
}

bool Maxima::issue_development_action(Context& echo,
	AIMaximaPlacement::DevelopmentAction& action)
{
	using namespace AIMaximaPlacement;int buildingId=-1;
	if(action.type==BuildCampusMember||action.type==BuildStandalone)
	{
		const int x=echo.player->map->normalizeX(action.centerX+action.initialFootprint.left);
		const int y=echo.player->map->normalizeY(action.centerY+action.initialFootprint.top);
		buildingId=echo.issue_building_at(action.buildingType,action.workers,x,y);
		if(buildingId<0)return false;
		if(action.buildingType==IntBuildingType::FOOD_BUILDING)
		{
			// The completion callback needs the tracker on its first invocation.
			ManagementOrder* tracker=new AddResourceTracker(
				strategy.staffing.resource_tracker_samples,CORN,buildingId);
			tracker->add_condition(new ParticularBuilding(new NotUnderConstruction,buildingId));
			echo.add_management_order(tracker);
			ManagementOrder* update=new Notify(RuntimeEvent(RuntimeEvent::UpdateInn,buildingId));
			update->add_condition(new ParticularBuilding(new NotUnderConstruction,buildingId));
			echo.add_management_order(update);
		}
		else if(action.buildingType==IntBuildingType::SWARM_BUILDING)
		{
			ManagementOrder* update=new Notify(RuntimeEvent(RuntimeEvent::UpdateSwarm,buildingId));
			update->add_condition(new ParticularBuilding(new NotUnderConstruction,buildingId));
			echo.add_management_order(update);
			ManagementOrder* tracker=new AddResourceTracker(
				strategy.staffing.resource_tracker_samples,CORN,buildingId);
			tracker->add_condition(new ParticularBuilding(new NotUnderConstruction,buildingId));
			echo.add_management_order(tracker);
		}
	}
	else
	{
		buildingId=action.buildingId;
		if(action.type==UpgradeBuilding)
		{
			// A saved reservation may outlive its original director authorization.
			const DevelopmentLimits limits=collect_development_limits(echo);
			if(!limits.allowUpgrades
			   || (action.fromLevel==2 && !limits.allowLevel2Upgrades)
			   || limits.upgradePriority(action.buildingType,action.fromLevel)==0)
				return false;
		}
		if(!echo.issue_upgrade_repair(buildingId,action.type==RepairBuilding))return false;
		// Repairs execute the labor allocation used to score their cost and
		// downtime; upgrade staffing still follows the current director plan.
		const int workers=action.type==RepairBuilding ? action.workers
			: (action.fromLevel<=1 ? budget.upgrade_level1_workers
				: budget.upgrade_level2_workers);
		ManagementOrder* assignment=new AssignWorkers(workers,buildingId);
		assignment->add_condition(new ParticularBuilding(new UnderConstruction,buildingId));
		echo.add_management_order(assignment);
		if(action.type==UpgradeBuilding)
		{
			ManagementOrder* update=new Notify(RuntimeEvent(RuntimeEvent::BuildingUpdated,
				action.buildingType,buildingId));
			update->add_condition(new ParticularBuilding(new NotUnderConstruction,buildingId));
			echo.add_management_order(update);
		}
	}
	development_planner.markIssued(action.id,buildingId,timer);
	return true;
}

void Maxima::development_cycle(Context& echo)
{
	using namespace AIMaximaPlacement;
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
		initialize_farming_cache(echo);
		if(!development_planner_initialized)
		{
			configure_development_planner();
			WorldState initial=collect_development_world(echo);
			development_planner.adoptStartingBuildings(initial);
			development_planner_initialized=true;
		}
		refreshedWorld=collect_development_world(echo,&worldSignature);
		development_planner.observe(refreshedWorld,worldSignature);
		update_swarm_retirement(echo);
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
		echo.dispatch_event(RuntimeEvent(event,i->second.id,i->second.buildingId));

		if(i->second.purpose==ColonySeed)
		{

			if(i->second.state==Completed
			   && i->second.id>last_colony_accounted_action_id)
			{
				last_colony_accounted_action_id=i->second.id;


				director.invalidate();

			}

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
		if(pending.type==UpgradeBuilding
		   &&!development_planner.revalidateSelection(refreshedWorld,{},
			collect_development_limits(echo),pending,&reason))
		{
			development_planner.markInvalidated(pending.id,UpgradeBlocked,worldSignature);
			continue;
		}
			if(development_planner.revalidate(refreshedWorld,pending,&reason,true))
			{
				if(!issue_development_action(echo,pending))
					development_planner.markInvalidated(pending.id,
						pending.type==UpgradeBuilding||pending.type==RepairBuilding
						?UpgradeBlocked:EngineRejected,worldSignature,
						refreshedWorld.index(pending.centerX,pending.centerY));
				else
				{
					development_reported_states[pending.id]=CreateIssued;
					std::map<int,DevelopmentAction>::const_iterator issued=
						development_planner.actions().find(pending.id);
					echo.dispatch_event(RuntimeEvent(RuntimeEvent::DevelopmentCreateIssued,
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
		? development_planner.selectionLimits() : collect_development_limits(echo);
	const int totalLimit=limits.newConstruction+limits.level1Upgrades+limits.level2Upgrades;
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
				worldSignature,1024);
		if(selection==SelectionPending)
		{
			development_cycle_pending=true;
			break;
		}
		if(selection==SelectionEmpty)
		{
			selectionExhausted=true;
			const SelectionSummary& summary=
				development_planner.selectionSummary();
			if(summary.rejected[RejectedClearableResource])
				record_construction_space_failure();


			break;
		}
		// Keep selection scores snapshot-stable, but never execute stale authority
		// or stale spatial contracts. Only a finished winner needs this live pass.
		uint32_t issueSignature=0;
		WorldState issueWorld=collect_development_world(echo,&issueSignature);
		development_planner.observe(issueWorld,issueSignature);
		const DevelopmentLimits issueLimits=collect_development_limits(echo);
		const std::vector<DevelopmentIntent> issueIntents=
			collect_development_intents(issueWorld);
		RejectionReason issueReason=RejectedAuthorization;
		if(!development_planner.revalidateSelection(issueWorld,issueIntents,
			issueLimits,action,&issueReason))
		{
			action.state=InvalidatedBeforeIssue;

			// A changed world is not a permanently broken coordinate.
			development_cycle_pending=true;
			break;
		}
		if(!development_planner.reserve(issueWorld,action))
		{;continue;}
		development_reported_states[action.id]=ParcelReserved;
		echo.dispatch_event(RuntimeEvent(RuntimeEvent::DevelopmentParcelReserved,
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
			;continue;
		}
		if(!issue_development_action(echo,action))
		{
			development_planner.markInvalidated(action.id,
				action.type==UpgradeBuilding||action.type==RepairBuilding
				?UpgradeBlocked:EngineRejected,
				issueSignature,issueWorld.index(action.centerX,action.centerY));
			;continue;
		}
		development_reported_states[action.id]=CreateIssued;
		std::map<int,DevelopmentAction>::const_iterator issuedAction=
			development_planner.actions().find(action.id);
		echo.dispatch_event(RuntimeEvent(RuntimeEvent::DevelopmentCreateIssued,
			action.id,issuedAction==development_planner.actions().end()
			? action.buildingId:issuedAction->second.buildingId));

		// Reservations immediately affect subsequent candidates even though the
		// engine has not observed the queued OrderCreate yet.
	}
	if(!selectionExhausted&&selectionLimit<totalLimit)
		development_cycle_pending=true;

}
void Maxima::update_swarm_retirement(Context& echo)
{
	if(!budget.swarm_retirement_enabled)
	{
		remote_swarm_since.clear();
		remote_swarms_ready.clear();
		remote_swarm_deletion_issued.clear();
		return;
	}
	const int probation_ticks=3000;

	std::set<int> present;
	int useful=0;
	BuildingSearch swarms(echo);
	swarms.add_condition(new SpecificBuildingType(IntBuildingType::SWARM_BUILDING));
	swarms.add_condition(new NotUnderConstruction);
	for(building_search_iterator i=swarms.begin();i!=swarms.end();++i)
	{
		const int id=*i;present.insert(id);
		Building* building=echo.get_building_register().get_building(id);
		if(!building)continue;
		const bool productive=nearby_farm_capacity(echo,id)>0;
		if(productive)++useful;
		if(productive)
		{
			remote_swarm_since.erase(id);
			remote_swarms_ready.erase(id);
			continue;
		}
		if(!remote_swarm_since.count(id))remote_swarm_since[id]=timer;
		if(timer-remote_swarm_since[id]>=probation_ticks)
			remote_swarms_ready.insert(id);

	}
	for(std::map<int,int>::iterator i=remote_swarm_since.begin();
		i!=remote_swarm_since.end();)
		if(!present.count(i->first))remote_swarm_since.erase(i++);else ++i;
	for(std::set<int>::iterator i=remote_swarms_ready.begin();
		i!=remote_swarms_ready.end();)
		if(!present.count(*i))remote_swarms_ready.erase(i++);else ++i;

	const bool safe=!budget.recovery_active&&snapshot.critical_food==0
		&&snapshot.own_buildings_under_attack==0&&snapshot.own_units_under_attack==0;

	if(!safe||useful<budget.desired_swarms)return;
	for(std::set<int>::const_iterator i=remote_swarms_ready.begin();
		i!=remote_swarms_ready.end();++i)
		if(!remote_swarm_deletion_issued.count(*i))
		{
			echo.add_management_order(new DestroyBuilding(*i));
			remote_swarm_deletion_issued.insert(*i);
			;;

			break;
		}
}

void Maxima::manage_buildings(Context& echo)
{
	BuildingSearch bs(echo);
	bs.add_condition(new NotUnderConstruction);
	for(building_search_iterator i = bs.begin(); i!=bs.end(); ++i)
	{
		if(echo.get_building_register().get_type(*i)==IntBuildingType::SWARM_BUILDING)
		{
			manage_swarm(echo, *i);
		}
		if(echo.get_building_register().get_type(*i)==IntBuildingType::FOOD_BUILDING)
		{
			manage_inn(echo, *i);
		}
		if(echo.get_building_register().get_type(*i)==IntBuildingType::DEFENSE_BUILDING
		   && echo.get_building_register().get_assigned(*i)
			!=(explorer_defense_emergency()
				? strategy.staffing.completed_tower_emergency_workers
				: strategy.staffing.completed_tower_workers))
		{
			// A single carrier could not keep even one shot in the tower during the
			// FourSquares bomber wave. Two workers sustain the deterrent layer; four
			// replenish ammunition while an actual strike is in the colony.
			echo.add_management_order(new AssignWorkers(explorer_defense_emergency()
				? strategy.staffing.completed_tower_emergency_workers
				: strategy.staffing.completed_tower_workers, *i));
		}
	}
}

void Maxima::manage_inn(Context& echo, int id)
{
	int level=echo.get_building_register().get_level(id);
	int assigned=echo.get_building_register().get_assigned(id);

	const long long capacity=nearby_farm_capacity(echo,id);

	int to_assign = 0;
	if(level>=1 && level<=3)
	{
		const int index=level-1;
		const int maximum=std::max(budget.inn_normal_workers[index],
			budget.inn_low_corn_workers[index]);
		// Fertility uses 65536 units per fully productive tile. Staffing rises
		// smoothly toward the level's service envelope as nearby capacity grows.
		const long long scale=65536LL*std::max(1,budget.inn_normal_workers[index]);
		to_assign=!budget.inn_adaptive_staffing_enabled
			? budget.inn_normal_workers[index]
			: int((maximum*capacity+capacity+scale-1)/(capacity+scale));
	}

	///The number of units assigned to an Inn depends entirely on its level
	if(to_assign != assigned)
	{
		ManagementOrder* mo_assign=new AssignWorkers(to_assign, id);
		echo.add_management_order(mo_assign);
	}
}

long long Maxima::nearby_farm_capacity(Context& echo, int id,
	std::set<int>* shared_tiles)
{
	Building* building=echo.get_building_register().get_building(id);
	if(!building) return 0;
	initialize_farming_cache(echo);
	Map* map=echo.player->map;
	return reachableFoodCapacity(map, building, echo.player->team->me,
		budget.can_swim, budget.swarm_supply_radius, fertility_cache,
		&applied_farm_protection_mask, shared_tiles);
}

void Maxima::manage_swarm(Context& echo, int id)
{
	//Get some statistics
	TeamStat* stat=echo.player->team->stats.getLatestStat();
	int total_explorers=stat->numberUnitPerType[EXPLORER];
	if(stat->totalUnit == 0)
		return;

	int assigned=echo.get_building_register().get_assigned(id);
	int to_assign=0;

	int worker_ratio=0;
	int explorer_ratio=0;
	int warrior_ratio=0;

	// The director's swarm-worker figure is a colony-wide birth budget. Applying
	// it independently to every swarm multiplies growth as new swarms complete,
	// drains the productive workforce and creates a self-induced starvation
	// cycle. Distribute it deterministically across all completed swarms.
	BuildingSearch swarms(echo);
	swarms.add_condition(new SpecificBuildingType(IntBuildingType::SWARM_BUILDING));
	swarms.add_condition(new NotUnderConstruction);
	std::vector<int> completed_swarms;
	for(building_search_iterator swarm=swarms.begin(); swarm!=swarms.end(); ++swarm)
		completed_swarms.push_back(*swarm);
	std::sort(completed_swarms.begin(), completed_swarms.end());
	std::vector<SwarmStaffing::Candidate> staffing_candidates;

	for(std::vector<int>::const_iterator swarm=completed_swarms.begin();
		swarm!=completed_swarms.end(); ++swarm)
	{
		const long long supply=nearby_farm_capacity(echo,*swarm);
		staffing_candidates.push_back(SwarmStaffing::Candidate(*swarm,supply));

	}
	int total_to_assign=budget.swarm_workers;

	const std::vector<SwarmStaffing::Assignment> staffing_assignments=
		SwarmStaffing::distribute(staffing_candidates, total_to_assign,
			Building::MAX_UNIT_WORKING);
	for(std::vector<SwarmStaffing::Assignment>::const_iterator assignment=
		staffing_assignments.begin(); assignment!=staffing_assignments.end();
		++assignment)
	{
		if(assignment->id==id)
			to_assign=assignment->workers;
	}

	worker_ratio=budget.worker_ratio;

	// Every swarm shares the explorer mix until the colony-wide target is met.
	if(total_explorers<budget.desired_explorers)
		explorer_ratio=budget.explorer_ratio;

	///Warriors are constructed during the war preperation phase
	warrior_ratio=stat->numberUnitPerType[WARRIOR]<budget.desired_warriors
		? budget.warrior_ratio : 0;

	// Removing carriers alone still permits births from stored corn. A zero
	// colony birth budget pauses production too; the next funded plan restores
	// the normal ratios. Individual zero-worker shares do not change the mix.
	if(total_to_assign<=0)
		worker_ratio=explorer_ratio=warrior_ratio=0;

	if(assigned != to_assign)
	{
		ManagementOrder* mo_assign=new AssignWorkers(to_assign, id);
		echo.add_management_order(mo_assign);

	}

	//Change the ratio of the swarm when its finished
	ManagementOrder* mo_ratios=new ChangeSwarm(worker_ratio, explorer_ratio, warrior_ratio, id);
	echo.add_management_order(mo_ratios);
}

int Maxima::choose_building_to_attack(Context& echo)
{
	if(target<0 || target>=Team::MAX_COUNT || !echo.player->game->teams[target])
		return -1;
	AIMaximaRuntime::Gradients::GradientInfo gi_building;
	gi_building.add_source(new Entities::AnyTeamBuilding(echo.player->team->teamNumber, CompletedBuildings));
	gi_building.add_obstacle(new Entities::AnyResource);
	if(snapshot.swimming_warriors<6)
		gi_building.add_obstacle(new Entities::Water);
	Gradient& gradient=echo.get_gradient_manager().get_gradient(gi_building);

	int best_building=-1;
	int best_score=INT_MIN;
	int ties=0;
	for(enemy_building_iterator ebi(echo, target, -1, -1, AnyConstruction); ebi!=enemy_building_iterator(); ++ebi)
	{
		std::map<int, int>::const_iterator quarantined=attack_target_quarantine_until.find(*ebi);
		if(quarantined!=attack_target_quarantine_until.end()
		   && quarantined->second>timer)
			continue;
		Building* b=echo.player->game->teams[target]->myBuildings[Building::GIDtoID(*ebi)];
		if(!b || b->type->isVirtual)
			continue;
		const int distance=gradient.get_height(b->posX, b->posY);
		if(distance<0)
			continue;
		int value=8;
		switch(b->type->shortTypeNum)
		{
			case IntBuildingType::SWARM_BUILDING: value=42; break;
			case IntBuildingType::FOOD_BUILDING: value=36; break;
			case IntBuildingType::ATTACK_BUILDING: value=32; break;
			case IntBuildingType::SCIENCE_BUILDING: value=22; break;
			case IntBuildingType::HEAL_BUILDING: value=18; break;
			default: break;
		}
		if(b->type->isBuildingSite)
			value+=8;
		const int score=value+std::max(0, 70-distance);
		if(score>best_score)
		{
			best_score=score;
			best_building=*ebi;
			ties=1;
		}
		else if(score==best_score && ++ties>0 && syncRand()%ties==0)
			best_building=*ebi;
	}
	return best_building;
}

void Maxima::attack_building(Context& echo)
{
	int building=choose_building_to_attack(echo);
	if(building==-1)
	{
		// A reachable target may merely be cooling down after an invalid flag.
		// Wait for another target-selection cycle rather than treating that as
		// proof that the enemy must be dug out.
		if(target>=0 && target<Team::MAX_COUNT
		   && opponents[target].reachable_buildings>0)
			return;
		if(!is_digging_out)
			if(!dig_out_enemy(echo))
			{
				target = -1;
			}
		return;
	}
	Building* enemy=echo.player->game->teams[target]
		->myBuildings[Building::GIDtoID(building)];
	BuildingOrder* bo = new BuildingOrder(IntBuildingType::WAR_FLAG, budget.attack_units);
	bo->add_constraint(new CenterOfBuilding(building));
	unsigned int id=echo.add_building_order(bo);

	ManagementOrder* mo_minimum=new ChangeFlagMinimumLevel(2,id);
	echo.add_management_order(mo_minimum);

	ManagementOrder* mo_destroyed_1=new DestroyBuilding(id);
	mo_destroyed_1->add_condition(new EnemyBuildingDestroyed(echo, building));
	echo.add_management_order(mo_destroyed_1);

	ManagementOrder* mo_destroyed_2=new Notify(RuntimeEvent(RuntimeEvent::AttackFinished, id));
	mo_destroyed_2->add_condition(new BuildingDestroyed(id));
	echo.add_management_order(mo_destroyed_2);

	attack_flags.push_back(id);
	attack_flag_targets[id]=building;
	attack_flag_started_ticks[id]=timer;
	attack_flag_last_progress[id]=timer;
	if(enemy)
		attack_flag_last_hp[id]=enemy->hp;
	if(campaign.state==CampaignIdle || campaign.state==CampaignPreparing)
	{
		const bool continuing_progress=campaign.target_team==target
			&& campaign.buildings_destroyed>0
			&& timer-campaign.last_progress_tick<=15000;
		if(!continuing_progress)
			campaign.buildings_destroyed=0;
		campaign.state=CampaignActive;
		campaign.target_team=target;
		campaign.started_tick=timer;
		campaign.last_progress_tick=timer;
		campaign.last_target_buildings=opponents[target].known_buildings;
	}

}

void Maxima::transition_tactical_mission(Context& echo,
	Tactics::MissionPhase phase, const char* reason)
{

	tactical_mission.phase=phase;
	tactical_mission.phaseSinceTick=timer;
	if(tactical_mission.kind==Tactics::MissionSiege)
	{
		if(phase==Tactics::PhaseTransit || phase==Tactics::PhaseEngage)
			campaign.state=CampaignActive;
		else if(phase==Tactics::PhaseWithdraw)
			campaign.state=CampaignRetreating;
	}

}

void Maxima::finish_tactical_mission(Context& echo, const char* reason)
{
	const int flag=tactical_mission.flagId;

	const Tactics::MissionKind completed_kind=tactical_mission.kind;

	if(flag>=0)
	{
		attack_flag_end_reasons[flag]=reason;
		echo.cancel_or_destroy_building(flag);
	}
	const int cooldown=tactical_mission.kind==Tactics::MissionRaid
		? budget.raid_cooldown
		: (tactical_mission.kind==Tactics::MissionRelief
			? budget.relief_cooldown : budget.campaign_retreat_cooldown_ticks);
	tactical_mission.reset();
	tactical_mission.phase=Tactics::PhaseCooldown;
	tactical_mission.phaseSinceTick=timer;
	tactical_mission.cooldownUntil=timer+cooldown;
	if(completed_kind==Tactics::MissionSiege)
	{
		campaign.cooldown_until=std::max(campaign.cooldown_until,
			tactical_mission.cooldownUntil);
		campaign.state=CampaignIdle;
	}
	director.invalidate();
}

void Maxima::withdraw_tactical_mission(Context& echo, const char* reason,
	bool immediate)
{
	if(tactical_mission.phase==Tactics::PhaseIdle
	   || tactical_mission.phase==Tactics::PhaseCooldown)
		return;
	if(immediate || tactical_mission.flagId<0
	   || !echo.get_building_register().is_building_found(tactical_mission.flagId))
	{
		finish_tactical_mission(echo, reason);
		return;
	}
	echo.add_management_order(new ChangeFlagSize(budget.tactical_rally_radius,
		tactical_mission.flagId));
	echo.add_management_order(new ChangeFlagPosition(tactical_mission.rallyX,
		tactical_mission.rallyY, tactical_mission.flagId));
	transition_tactical_mission(echo, Tactics::PhaseWithdraw, reason);
}

void Maxima::begin_tactical_mission(Context& echo)
{
	if(budget.tactical_kind==Tactics::MissionNone)
		return;
	int rally_x=0;
	int rally_y=0;
	if(!choose_tactical_rally(echo, budget.tactical_target_x,
		budget.tactical_target_y, rally_x, rally_y))
	{

		return;
	}
	BuildingOrder* flag_order=new BuildingOrder(IntBuildingType::WAR_FLAG,
		budget.tactical_requested_force);
	flag_order->add_constraint(new Construction::SinglePosition(rally_x, rally_y));
	const int flag=echo.add_building_order(flag_order);
	echo.add_management_order(new ChangeFlagMinimumLevel(2, flag));
	echo.add_management_order(new ChangeFlagSize(budget.tactical_rally_radius, flag));
	ManagementOrder* deleted=new Notify(RuntimeEvent(RuntimeEvent::AttackFinished, flag));
	deleted->add_condition(new BuildingDestroyed(flag));
	echo.add_management_order(deleted);

	tactical_mission.reset();
	tactical_mission.kind=budget.tactical_kind;
	tactical_mission.flagId=flag;
	tactical_mission.targetTeam=budget.tactical_target_team;
	tactical_mission.targetGid=budget.tactical_target_gid;
	tactical_mission.targetX=budget.tactical_target_x;
	tactical_mission.targetY=budget.tactical_target_y;
	tactical_mission.rallyX=rally_x;
	tactical_mission.rallyY=rally_y;
	tactical_mission.requestedForce=budget.tactical_requested_force;
	tactical_mission.minimumForce=budget.tactical_minimum_force;
	tactical_mission.startedTick=timer;
	tactical_mission.phaseSinceTick=timer;
	tactical_mission.lastContactTick=timer;
	tactical_mission.lastProgressTick=timer;
	tactical_mission.candidateScore=budget.tactical_candidate_score;
	const Tactics::RaidCandidate* raid=tactical_mission.kind==Tactics::MissionRaid
		? tactics.bestRaidForTeam(tactical_mission.targetTeam) : NULL;
	tactical_mission.initialTargetWorkers=raid ? raid->workers : 0;
	attack_flags.push_back(flag);
	attack_flag_started_ticks[flag]=timer;
	if(tactical_mission.targetGid>=0)
		attack_flag_targets[flag]=tactical_mission.targetGid;
	if(tactical_mission.kind==Tactics::MissionSiege)
	{
		const bool continuing_progress=campaign.target_team==tactical_mission.targetTeam
			&& timer-campaign.last_progress_tick<=budget.tactical_target_lock_ticks;
		if(!continuing_progress)
			campaign.buildings_destroyed=0;
		campaign.state=CampaignPreparing;
		campaign.target_team=tactical_mission.targetTeam;
		campaign.started_tick=timer;
		campaign.last_progress_tick=timer;
		const Recon::OpponentIntel* intel=
			reconnaissance.opponent(tactical_mission.targetTeam);
		campaign.last_target_buildings=intel ? intel->knownBuildings : 0;
	}
	transition_tactical_mission(echo, Tactics::PhaseMuster,
		"mission_selected");

}

bool Maxima::retarget_tactical_siege(Context& echo)
{
	const int flag=tactical_mission.flagId;
	const int previousRequested=tactical_mission.requestedForce;
	const int previousMinimum=tactical_mission.minimumForce;
	const Recon::OpponentIntel* previous_intel=
		reconnaissance.opponent(tactical_mission.targetTeam);
	const std::map<int, int>::const_iterator previous_target=attack_flag_targets.find(flag);
	// Recon only removes remembered buildings after confirming their footprint
	// is empty. Credit that success before replacing the flag's target record;
	// AttackFinished can then credit only the final target, without double counts.
	if(tactical_mission.targetGid>=0
	   && tactical_mission.targetGid!=budget.tactical_target_gid
	   && previous_target!=attack_flag_targets.end()
	   && previous_target->second==tactical_mission.targetGid
	   && previous_intel
	   && previous_intel->buildings.count(tactical_mission.targetGid)==0)
	{
		++campaign.buildings_destroyed;
		campaign.last_progress_tick=timer;
	}
	tactical_mission.retargetSiege(budget.tactical_target_gid,
		echo.player->map->normalizeX(budget.tactical_target_x),
		echo.player->map->normalizeY(budget.tactical_target_y), timer);
	tactical_mission.targetTeam=budget.tactical_target_team;
	tactical_mission.requestedForce=budget.tactical_requested_force;
	tactical_mission.minimumForce=budget.tactical_minimum_force;
	tactical_mission.candidateScore=budget.tactical_candidate_score;
	attack_flag_targets[flag]=tactical_mission.targetGid;
	echo.add_management_order(new AssignWorkers(tactical_mission.requestedForce, flag));
	if(tactical_mission.phase==Tactics::PhaseMuster)
	{
		if(tactical_mission.requestedForce>previousRequested
		   || tactical_mission.minimumForce>previousMinimum)
			tactical_mission.phaseSinceTick=timer;
		return true;
	}
	const bool ready=Tactics::Program::musterLaunchAllowed(Tactics::MissionSiege,
		echo.get_building_register().get_enrolled(flag),
		echo.get_building_register().get_on_site(flag),
		tactical_mission.requestedForce, tactical_mission.minimumForce,
		budget.tactical_siege_muster_percent);
	if(!ready)
	{
		// Reinforcements must assemble before a stronger replacement is attacked.
		echo.add_management_order(new ChangeFlagSize(budget.tactical_rally_radius, flag));
		echo.add_management_order(new ChangeFlagPosition(tactical_mission.rallyX,
			tactical_mission.rallyY, flag));
		tactical_mission.launchedForce=0;
		campaign.state=CampaignPreparing;
		transition_tactical_mission(echo, Tactics::PhaseMuster, "replacement_requires_muster");
		return true;
	}
	// Reducing the assignment intentionally releases units; it is not a casualty.
	tactical_mission.launchedForce=std::min(tactical_mission.launchedForce,
		tactical_mission.requestedForce);
	echo.add_management_order(new ChangeFlagPosition(tactical_mission.targetX,
		tactical_mission.targetY, flag));
	return false;
}

void Maxima::control_attacks(Context& echo)
{
	if(!budget.tactics_enabled)
	{
		if(tactical_mission.phase!=Tactics::PhaseIdle
		   && tactical_mission.phase!=Tactics::PhaseCooldown
		   && tactical_mission.phase!=Tactics::PhaseWithdraw)
			withdraw_tactical_mission(echo, "tactics_disabled", true);
		else if(tactical_mission.phase==Tactics::PhaseCooldown)
			tactical_mission.reset();
		for(std::vector<int>::const_iterator legacy=attack_flags.begin();
			legacy!=attack_flags.end(); ++legacy)
			if(echo.get_building_register().is_building_found(*legacy)
			   ||echo.get_building_register().is_building_pending(*legacy))
				echo.add_management_order(new DestroyBuilding(*legacy));
		attack_flags.clear();
		return;
	}
	const bool active_tactic_disabled=
		(tactical_mission.kind==Tactics::MissionRaid
			&& !budget.raid_enabled)
		||(tactical_mission.kind==Tactics::MissionSiege
			&& !budget.siege_enabled)
		||(tactical_mission.kind==Tactics::MissionRelief
			&& (!budget.teamplay_enabled
				|| !budget.teamplay_defense_enabled));
	if(active_tactic_disabled
	   && tactical_mission.phase!=Tactics::PhaseIdle
	   && tactical_mission.phase!=Tactics::PhaseCooldown
	   && tactical_mission.phase!=Tactics::PhaseWithdraw)
	{
		withdraw_tactical_mission(echo, "tactic_disabled", true);
		return;
	}
	if(budget.colony_emergency
	   && tactical_mission.phase!=Tactics::PhaseIdle
	   && tactical_mission.phase!=Tactics::PhaseCooldown
	   && tactical_mission.phase!=Tactics::PhaseWithdraw)
	{
		withdraw_tactical_mission(echo, "colony_emergency", true);
		return;
	}
	if(tactical_mission.phase==Tactics::PhaseCooldown)
	{
		if(timer<tactical_mission.cooldownUntil)
			return;
		tactical_mission.reset();
	}
	if(tactical_mission.phase==Tactics::PhaseIdle)
	{
		if(!attack_flags.empty())
		{
			for(std::vector<int>::const_iterator legacy=attack_flags.begin();
				legacy!=attack_flags.end(); ++legacy)
				if(echo.get_building_register().is_building_found(*legacy))
					echo.add_management_order(new DestroyBuilding(*legacy));
			attack_flags.clear();
			return;
		}
		if(budget.tactical_kind!=Tactics::MissionNone)
		{
			begin_tactical_mission(echo);
			return;
		}
		// A sealed colony produces no reachable siege candidate, so it cannot be
		// represented by a warrior mission yet. Once the director has authorized
		// an offensive campaign, open a resource corridor and let the regular
		// tactical planner launch the siege as soon as that corridor is usable.
		if(!is_digging_out && budget.tactical_dig_out_team>=0)
		{
			target=budget.tactical_dig_out_team;
			dig_out_enemy(echo);
		}
		return;
	}
	const int flag=tactical_mission.flagId;
	if(flag<0 || (!echo.get_building_register().is_building_found(flag)
	   && !echo.get_building_register().is_building_pending(flag)))
	{
		finish_tactical_mission(echo, "flag_missing");
		return;
	}
	if(!echo.get_building_register().is_building_found(flag))
		return;
	const int enrolled=echo.get_building_register().get_enrolled(flag);
	const int on_site=echo.get_building_register().get_on_site(flag);
	// Withdrawal is terminal for every mission kind. Handle it before any
	// mission-specific timeout or retarget logic so the rally command and its
	// completion timer cannot be restarted on every strategy cycle.
	if(tactical_mission.phase==Tactics::PhaseWithdraw)
	{
		if(on_site*100>=std::max(1, enrolled)*75
		   || timer-tactical_mission.phaseSinceTick>=budget.raid_muster_timeout)
			finish_tactical_mission(echo, "withdrawn");
		return;
	}
	if(tactical_mission.kind==Tactics::MissionRelief)
	{
		bool contact_refreshed=false;
		if(budget.tactical_contact_visible)
		{
			const bool same_team=budget.tactical_target_team
				==tactical_mission.targetTeam;
			const bool changed=budget.tactical_target_team
				!=tactical_mission.targetTeam
				|| budget.tactical_target_x!=tactical_mission.targetX
				|| budget.tactical_target_y!=tactical_mission.targetY;
			const int retarget_distance=echo.player->map->warpDistSquare(
				tactical_mission.targetX, tactical_mission.targetY,
				budget.tactical_target_x, budget.tactical_target_y);
			const bool acceptable=Tactics::Program::reliefRetargetAllowed(
				changed, same_team, retarget_distance,
				budget.relief_follow_radius, budget.tactical_candidate_score,
				tactical_mission.candidateScore, budget.relief_retarget_margin);
			if(acceptable)
			{
				contact_refreshed=true;
				tactical_mission.lastContactTick=timer;
				const int requested=budget.tactical_requested_force;
				const int minimum=budget.tactical_minimum_force;
				if(requested!=tactical_mission.requestedForce
				   || minimum!=tactical_mission.minimumForce)
				{
					if(tactical_mission.phase==Tactics::PhaseMuster
					   && (requested>tactical_mission.requestedForce
						|| minimum>tactical_mission.minimumForce))
						tactical_mission.phaseSinceTick=timer;
					tactical_mission.requestedForce=requested;
					tactical_mission.minimumForce=minimum;
					echo.add_management_order(new AssignWorkers(requested, flag));
				}
				// Count reinforcements only once they actually enroll, and do not
				// mistake units deliberately released by a smaller request for deaths.
				if(tactical_mission.phase!=Tactics::PhaseMuster)
					tactical_mission.launchedForce=std::min(requested,
						std::max(tactical_mission.launchedForce, enrolled));
			}
			if(changed && acceptable)
			{
				tactical_mission.targetTeam=budget.tactical_target_team;
				tactical_mission.targetX=budget.tactical_target_x;
				tactical_mission.targetY=budget.tactical_target_y;
				tactical_mission.candidateScore=budget.tactical_candidate_score;
				if(tactical_mission.phase!=Tactics::PhaseMuster)
					echo.add_management_order(new ChangeFlagPosition(
						tactical_mission.targetX, tactical_mission.targetY, flag));

			}
		}
		if(!contact_refreshed && timer-tactical_mission.lastContactTick
		   >=budget.relief_contact_ttl)
		{
			if(tactical_mission.phase==Tactics::PhaseMuster)
				finish_tactical_mission(echo, "ally_threat_lost_before_launch");
			else
				withdraw_tactical_mission(echo, "ally_threat_resolved", false);
			return;
		}
		if(timer-tactical_mission.startedTick>=budget.relief_max_engagement)
		{
			withdraw_tactical_mission(echo, "relief_timeout", false);
			return;
		}
	}
	if(tactical_mission.phase==Tactics::PhaseMuster)
	{

		if(tactical_mission.kind==Tactics::MissionSiege)
		{
			const Recon::OpponentIntel* intel=
				reconnaissance.opponent(tactical_mission.targetTeam);
			const bool target_remembered=intel
				&& intel->buildings.find(tactical_mission.targetGid)
					!=intel->buildings.end();
			const Tactics::SiegeTargetContinuity continuity=
				Tactics::Program::siegeTargetContinuity(target_remembered,
					tactical_mission.targetGid, budget.tactical_target_gid);
			if(continuity==Tactics::SiegeTargetLost)
			{
				finish_tactical_mission(echo,
					"target_lost_from_recon_during_muster");
				return;
			}
			if(continuity==Tactics::SiegeTargetReplacementAvailable)
			{
				retarget_tactical_siege(echo);
			}
		}
		if(tactical_mission.kind==Tactics::MissionRaid)
		{
			const Tactics::RaidCandidate* current=NULL;
			bool saw_cluster=false;
			for(std::vector<Tactics::RaidCandidate>::const_iterator candidate=
				tactics.raidCandidates().begin();
				candidate!=tactics.raidCandidates().end(); ++candidate)
			{
				if(candidate->team!=tactical_mission.targetTeam)
					continue;
				saw_cluster=true;
				int route=-1;
				int nearest=INT_MAX;
				if(raid_candidate_safe(echo, *candidate,
					tactical_mission.requestedForce, route, nearest))
				{
					current=&*candidate;
					break;
				}
			}
			// The original workers may move under protection during muster. Keep
			// the assembled force useful by switching to the best safe visible
			// cluster, including another opponent, before giving up the mission.
			if(!current)
				for(std::vector<Tactics::RaidCandidate>::const_iterator candidate=
					tactics.raidCandidates().begin();
					candidate!=tactics.raidCandidates().end(); ++candidate)
				{
					saw_cluster=true;
					int route=-1;
					int nearest=INT_MAX;
					if(raid_candidate_safe(echo, *candidate,
						tactical_mission.requestedForce, route, nearest))
					{
						current=&*candidate;
						break;
					}
				}
			if(current)
			{

				tactical_mission.targetTeam=current->team;
				tactical_mission.lastContactTick=timer;
				tactical_mission.targetX=current->x;
				tactical_mission.targetY=current->y;
				tactical_mission.candidateScore=current->score;

			}
			else
			{
				if(timer-tactical_mission.lastContactTick>=budget.raid_contact_ttl)
					finish_tactical_mission(echo, saw_cluster
						? "unsafe_before_launch" : "workers_lost_before_launch");
				// Contact grace keeps the mission alive, not its launch permission.
				// Wait for a safe cluster even if the force has fully assembled.
				return;
			}
		}
		const int percent=tactical_mission.kind==Tactics::MissionRaid
			? budget.raid_muster_percent : budget.tactical_siege_muster_percent;
		const int timeout=tactical_mission.kind==Tactics::MissionRaid
			? budget.raid_muster_timeout : budget.raid_muster_timeout*2;
		// Relief is time-sensitive: once enough warriors subscribe, move the flag
		// immediately and let them converge on the ally while travelling. Raids and
		// sieges still assemble on site before committing.
		const bool timed_out=timer-tactical_mission.phaseSinceTick>=timeout;
		const bool ready=Tactics::Program::musterLaunchAllowed(
			tactical_mission.kind, enrolled, on_site,
			tactical_mission.requestedForce, tactical_mission.minimumForce,
			percent);
		if(!ready && !timed_out)
			return;
		if(!ready)
		{
			finish_tactical_mission(echo, "muster_underfilled");
			return;
		}
		tactical_mission.launchedForce=enrolled;
		echo.add_management_order(new ChangeFlagSize(
			tactical_mission.kind==Tactics::MissionRaid
				? budget.raid_flag_radius : budget.tactical_siege_radius, flag));
		echo.add_management_order(new ChangeFlagPosition(tactical_mission.targetX,
			tactical_mission.targetY, flag));
		transition_tactical_mission(echo, Tactics::PhaseTransit,
			"muster_ready");
		return;
	}
	if(tactical_mission.kind==Tactics::MissionRaid)
	{
		const Tactics::RaidCandidate* current=NULL;
		bool saw_cluster=false;
		bool saw_safe_cluster=false;
		for(std::vector<Tactics::RaidCandidate>::const_iterator candidate=
			tactics.raidCandidates().begin();
			candidate!=tactics.raidCandidates().end(); ++candidate)
		{
			if(candidate->team!=tactical_mission.targetTeam)
				continue;
			saw_cluster=true;
			int route=-1;
			int nearest=INT_MAX;
			if(raid_candidate_safe(echo, *candidate,
				tactical_mission.launchedForce, route, nearest))
			{
				saw_safe_cluster=true;
				if(!Tactics::Program::raidRetargetAllowed(true,
					echo.player->map->warpDistSquare(tactical_mission.targetX,
						tactical_mission.targetY, candidate->x, candidate->y),
					budget.raid_flag_radius, candidate->score,
					tactical_mission.candidateScore, budget.raid_retarget_margin))
					continue;
				current=&*candidate;
				break;
			}
		}
		// If the selected workers move under protection, preserve the assembled
		// raiding force by looking for a safe cluster belonging to another enemy.
		if(!current)
			for(std::vector<Tactics::RaidCandidate>::const_iterator candidate=
				tactics.raidCandidates().begin();
				candidate!=tactics.raidCandidates().end(); ++candidate)
			{
				if(candidate->team==tactical_mission.targetTeam)
					continue;
				saw_cluster=true;
				int route=-1;
				int nearest=INT_MAX;
				if(raid_candidate_safe(echo, *candidate,
					tactical_mission.launchedForce, route, nearest))
				{
					current=&*candidate;
					break;
				}
			}
		if(current)
		{
			const bool moved=current->x!=tactical_mission.targetX
				|| current->y!=tactical_mission.targetY;
			tactical_mission.targetTeam=current->team;
			tactical_mission.lastContactTick=timer;
			tactical_mission.targetX=current->x;
			tactical_mission.targetY=current->y;
			tactical_mission.candidateScore=current->score;
			if(moved)
			{
				echo.add_management_order(new ChangeFlagPosition(current->x,
					current->y, flag));

			}
		}
		else if(saw_cluster && !saw_safe_cluster)
		{
			withdraw_tactical_mission(echo, "raid_zone_became_unsafe", false);
			return;
		}
		if(timer-tactical_mission.lastContactTick>=budget.raid_contact_ttl)
		{
			withdraw_tactical_mission(echo, "workers_lost", false);
			return;
		}
		if(timer-tactical_mission.startedTick>=budget.raid_max_engagement)
		{
			withdraw_tactical_mission(echo, "engagement_timeout", false);
			return;
		}
	}
	else if(tactical_mission.kind==Tactics::MissionSiege)
	{
		const int team=tactical_mission.targetTeam;
		const Recon::OpponentIntel* intel=reconnaissance.opponent(team);
		const bool target_remembered=intel
			&& intel->buildings.find(tactical_mission.targetGid)
				!=intel->buildings.end();
		const Tactics::SiegeTargetContinuity continuity=
			Tactics::Program::siegeTargetContinuity(target_remembered,
				tactical_mission.targetGid, budget.tactical_target_gid);
		if(continuity==Tactics::SiegeTargetLost)
		{
			withdraw_tactical_mission(echo, "target_lost_from_recon", false);
			return;
		}
		if(budget.tactical_target_gid>=0
		   && budget.tactical_target_gid!=tactical_mission.targetGid)
		{
			if(retarget_tactical_siege(echo)) return;

		}
		const int local=Building::GIDtoID(tactical_mission.targetGid);
		Building* target_building=team>=0 && team<Team::MAX_COUNT
			&& echo.player->game->teams[team] && local>=0 && local<Building::MAX_COUNT
			? echo.player->game->teams[team]->myBuildings[local] : NULL;
		if(target_building && target_building->gid==tactical_mission.targetGid
		   && building_currently_visible(echo.player, target_building))
		{
			if(tactical_mission.lastTargetHp<0
			   || target_building->hp<tactical_mission.lastTargetHp)
			{
				tactical_mission.lastTargetHp=target_building->hp;
				tactical_mission.lastProgressTick=timer;
				campaign.last_progress_tick=timer;
			}
		}
		if(timer-tactical_mission.lastProgressTick>=budget.campaign_stall_ticks)
		{
			withdraw_tactical_mission(echo, "stalled", false);
			return;
		}
	}
	const int casualty_percent=tactical_mission.kind==Tactics::MissionRaid
		? budget.raid_casualty_percent : budget.tactical_siege_casualty_percent;
	const int survivors=tactical_mission.kind==Tactics::MissionRaid
		? budget.raid_survivor_min : std::max(1,
			tactical_mission.minimumForce*(100-casualty_percent)/100);
	if(Tactics::Program::casualtiesRequireWithdrawal(enrolled,
		tactical_mission.launchedForce,
		std::min(survivors,tactical_mission.launchedForce), casualty_percent))
	{
		withdraw_tactical_mission(echo, "casualties", false);
		return;
	}
	const int arrival_percent=tactical_mission.kind==Tactics::MissionRaid
		? budget.raid_muster_percent : budget.tactical_siege_muster_percent;
	if(tactical_mission.phase==Tactics::PhaseTransit
	   && (tactical_mission.kind==Tactics::MissionRelief
		? on_site>=tactical_mission.minimumForce
		: Tactics::Program::musterReady(enrolled,on_site,
			tactical_mission.requestedForce,arrival_percent)))
		transition_tactical_mission(echo, Tactics::PhaseEngage, "force_arrived");
}

void Maxima::control_legacy_attacks(Context& echo)
{
	choose_enemy_target(echo);
	const bool must_retreat=budget.colony_emergency
		|| snapshot.trained_warriors<budget.defense_reserve;
	for(std::vector<int>::const_iterator flag=attack_flags.begin();
		flag!=attack_flags.end(); ++flag)
	{
		std::map<int, int>::const_iterator target_it=attack_flag_targets.find(*flag);
		if(target_it==attack_flag_targets.end())
			continue;
		const int gid=target_it->second;
		const int team=Building::GIDtoTeam(gid);
		const int local=Building::GIDtoID(gid);
		Building* building=(team>=0 && team<Team::MAX_COUNT
			&& echo.player->game->teams[team] && local>=0 && local<Building::MAX_COUNT)
			? echo.player->game->teams[team]->myBuildings[local] : NULL;
		if(!building)
		{
			campaign.last_progress_tick=timer;
			continue;
		}
		// Once a known target drops out of vision, retain its last-known identity
		// but do not inspect live health through fog of war.
		if(!building_currently_visible(echo.player, building))
			continue;
		std::map<int, int>::iterator last_hp=attack_flag_last_hp.find(*flag);
		if(last_hp==attack_flag_last_hp.end() || building->hp<last_hp->second)
		{
			attack_flag_last_hp[*flag]=building->hp;
			attack_flag_last_progress[*flag]=timer;
			campaign.last_progress_tick=timer;
		}
		std::map<int, int>::const_iterator progress=attack_flag_last_progress.find(*flag);
		if(progress!=attack_flag_last_progress.end()
		   && timer-progress->second>=budget.campaign_stall_ticks)
		{
			attack_flag_end_reasons[*flag]="stalled";
			if(echo.get_building_register().is_building_found(*flag))
				echo.add_management_order(new DestroyBuilding(*flag));
		}
	}

	if(must_retreat && !attack_flags.empty())
	{
		if(campaign.state!=CampaignRetreating)
		{
			campaign.state=CampaignRetreating;
			campaign.cooldown_until=std::max(campaign.cooldown_until,
				timer+budget.campaign_retreat_cooldown_ticks);
			for(std::vector<int>::const_iterator flag=attack_flags.begin();
				flag!=attack_flags.end(); ++flag)
			{
				attack_flag_end_reasons[*flag]=budget.colony_emergency
					? "colony_emergency" : "insufficient_force";
				if(echo.get_building_register().is_building_found(*flag))
					echo.add_management_order(new DestroyBuilding(*flag));
			}

		}
	}
	else if(!attack_flags.empty())
		campaign.state=(budget.attack_flags>0) ? CampaignActive : CampaignPaused;
	else if(budget.attack_flags>0 && target!=-1)
		campaign.state=CampaignPreparing;
	else
		campaign.state=CampaignIdle;

	if(!must_retreat && target!=-1 && budget.attack_flags>0
	   && attack_flags.size()<static_cast<unsigned>(budget.attack_flags))
		attack_building(echo);

	// Target selection already rejects buildings without a navigable path. Testing
	// the war flag's own tile here produced false "unreachable" results whenever
	// it overlapped the enemy structure, so valid campaigns repeatedly deleted
	// and recreated their flag. The stalled-progress watchdog above remains the
	// fallback for genuinely bad paths.
}

void Maxima::choose_enemy_target(Context& echo)
{
	int best_target=-1;
	int best_score=INT_MIN;
	for(enemy_team_iterator candidate(echo); candidate!=enemy_team_iterator(); ++candidate)
	{
		const OpponentAssessment& model=opponents[*candidate];
		if(model.alive && model.score>best_score)
		{
			best_score=model.score;
			best_target=*candidate;
		}
	}
	const bool progress_target_valid=budget.siege_target_lock_enabled
		&& campaign.target_team>=0
		&& campaign.target_team<Team::MAX_COUNT
		&& echo.player->game->teams[campaign.target_team]
		&& echo.player->game->teams[campaign.target_team]->isAlive
		&& opponents[campaign.target_team].score!=INT_MIN
		&& campaign.buildings_destroyed>0
		&& timer-campaign.last_progress_tick<=budget.tactical_target_lock_ticks;
	// Commitment is earned by destroying something, not merely by picking the
	// first visible opponent.  Once an assault proves productive, keep converting
	// that damage toward an elimination instead of repeatedly softening all three
	// rivals for somebody else.
	if(progress_target_valid)
	{
		best_target=campaign.target_team;
		best_score=opponents[best_target].score;
	}
	const bool current_valid=target>=0 && target<Team::MAX_COUNT
		&& echo.player->game->teams[target]
		&& echo.player->game->teams[target]->isAlive
		&& opponents[target].score!=INT_MIN;
	const bool campaign_committed=!attack_flags.empty()
		|| campaign.state==CampaignActive || campaign.state==CampaignPaused
		|| progress_target_valid;
	const bool materially_better=current_valid && best_target!=-1
		&& best_target!=target
		&& best_score>=opponents[target].score+budget.target_switch_margin;
	if(!current_valid || (progress_target_valid && target!=best_target)
		|| (!campaign_committed && materially_better))
	{

		target=best_target;

	}
}

bool Maxima::dig_out_enemy(Context& echo)
{
	///First choose an enemy building to dig out
	std::vector<int> buildings_to_attack;
	buildings_to_attack.reserve(100);

	MapInfo mi(echo);

	AIMaximaRuntime::Gradients::GradientInfo gi_building;
	gi_building.add_source(new Entities::AnyTeamBuilding(echo.player->team->teamNumber, CompletedBuildings));
	gi_building.add_obstacle(new Entities::AnyResource);
	Gradient& gradient=echo.get_gradient_manager().get_gradient(gi_building);

	for(enemy_building_iterator ebi(echo, target, -1, -1, AnyConstruction); ebi!=enemy_building_iterator(); ++ebi)
	{
		Building* b=echo.player->game->teams[target]->myBuildings[Building::GIDtoID(*ebi)];
		int bx = (b->posX + mi.get_width()) % mi.get_width();
		int by = (b->posY + mi.get_height()) % mi.get_height();
		if(gradient.get_height(bx, by) == -2)
			buildings_to_attack.push_back(*ebi);
	}

	if(buildings_to_attack.size() == 0)
		return false;

	int num=syncRand() % buildings_to_attack.size();

	int building=buildings_to_attack[num];
	const int bx=(echo.player->game->teams[target]->myBuildings[Building::GIDtoID(building)]->posX) % mi.get_width();
	const int by=(echo.player->game->teams[target]->myBuildings[Building::GIDtoID(building)]->posY) % mi.get_height();

	AIMaximaRuntime::Gradients::GradientInfo gi_pathfind;
	gi_pathfind.add_source(new Entities::Position(bx, by));
	gi_pathfind.add_obstacle(new Entities::Resource(STONE));
	Gradient& gradient_pathfind=echo.get_gradient_manager().get_gradient(gi_pathfind);

	///Next, find the closest point manhattan distance wise, to the building that is accessible
	int closest_x=-1;
	int closest_y=-1;
	int closest_distance=INT_MAX;
	for(int x=0; x<mi.get_width(); ++x)
	{
		for(int y=0; y<mi.get_height(); ++y)
		{
			if(gradient.get_height(x, y) >= 0)
			{
				int dist=gradient_pathfind.get_height(x, y);
				if(dist>=0 && dist < closest_distance)
				{
					closest_x=x;
					closest_y=y;
					closest_distance=dist;
				}
			}
		}
	}
	if(closest_x<0 || closest_y<0)
		return false;

	///Next, follow a path arround stone between the closest point and the buildings position,
	///placing Clearing flags as you go

	int xpos=closest_x;
	int ypos=closest_y;

	int flag_dist_count=3;

	int w=mi.get_width();
	int h=mi.get_height();
	int path_steps=0;
	int flags_created=0;

	while((xpos != bx || ypos!=by) && path_steps<w*h)
	{
		int nxpos = xpos;
		int nypos = ypos;
		int rx=(xpos+1+w) % w;
		int lx=(xpos-1+w) % w;
		int dy=(ypos+1+h) % h;
		int uy=(ypos-1+h) % h;
		int lowest_entity=gradient_pathfind.get_height(xpos, ypos)+2;

		if(lowest_entity == 0)
			break;

		//Test diagnols first, then the horizontals and verticals.
		if(gradient_pathfind.get_height(lx, uy) < lowest_entity && gradient_pathfind.get_height(lx, uy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(lx, uy);
			nxpos=lx;
			nypos=uy;
		}
		if(gradient_pathfind.get_height(rx, uy) < lowest_entity && gradient_pathfind.get_height(rx, uy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(rx, uy);
			nxpos=rx;
			nypos=uy;
		}
		if(gradient_pathfind.get_height(lx, dy) < lowest_entity && gradient_pathfind.get_height(lx, dy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(lx, dy);
			nxpos=lx;
			nypos=dy;
		}
		if(gradient_pathfind.get_height(rx, dy) < lowest_entity && gradient_pathfind.get_height(rx, dy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(rx, dy);
			nxpos=rx;
			nypos=dy;
		}

		if(gradient_pathfind.get_height(xpos, uy) < lowest_entity && gradient_pathfind.get_height(xpos, uy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(xpos, uy);
			nxpos=xpos;
			nypos=uy;
		}
		if(gradient_pathfind.get_height(lx, ypos) < lowest_entity && gradient_pathfind.get_height(lx, ypos)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(lx, ypos);
			nxpos=lx;
			nypos=ypos;
		}
		if(gradient_pathfind.get_height(rx, ypos) < lowest_entity && gradient_pathfind.get_height(rx, ypos)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(rx, ypos);
			nxpos=rx;
			nypos=ypos;
		}
		if(gradient_pathfind.get_height(xpos, dy) < lowest_entity && gradient_pathfind.get_height(xpos, dy)>=0)
		{
			lowest_entity=gradient_pathfind.get_height(xpos, dy);
			nxpos=xpos;
			nypos=dy;
		}

		if(nxpos==xpos && nypos==ypos)
			break;

		flag_dist_count+=1;

		if(flag_dist_count>3)
		{
			flag_dist_count=0;
			//The main order for the clearing flag
			BuildingOrder* bo_flag = new BuildingOrder(IntBuildingType::CLEARING_FLAG,
				budget.attack_clearing_workers);
			//Place it on the current point
			bo_flag->add_constraint(new Construction::SinglePosition(xpos, ypos));
			//Add the building order to the list of orders
			unsigned int id_flag=echo.add_building_order(bo_flag);
			flags_created+=1;

			ManagementOrder* mo_destroyed=new DestroyBuilding(id_flag);
			mo_destroyed->add_condition(new EnemyBuildingDestroyed(echo, building));
			echo.add_management_order(mo_destroyed);

			ManagementOrder* mo_completion=new ChangeFlagSize(3, id_flag);
			echo.add_management_order(mo_completion);
		}
		xpos = nxpos;
		ypos = nypos;
		path_steps+=1;

	}
	if(flags_created==0)
		return false;

	ManagementOrder* mo_destroyed=new Notify(RuntimeEvent(RuntimeEvent::DigOutFinished));
	mo_destroyed->add_condition(new EnemyBuildingDestroyed(echo, building));
	echo.add_management_order(mo_destroyed);

	is_digging_out=true;
	campaign.state=CampaignPreparing;
	campaign.target_team=target;
	campaign.started_tick=timer;
	campaign.last_progress_tick=timer;

	return true;
}

Uint32 Maxima::compute_preemptive_building_signature(Context& echo) const
{
	Uint32 signature=2166136261u;
	for(int id=0; id<Building::MAX_COUNT; ++id)
	{
		Building* building=echo.player->team->myBuildings[id];
		if(!building || building->type->isVirtual
		   || building->type->isBuildingSite)
			continue;
		add_preemptive_hash(signature, 0x4f574e00u);
		add_preemptive_hash(signature, building->gid);
		add_preemptive_hash(signature, building->posX);
		add_preemptive_hash(signature, building->posY);
		add_preemptive_hash(signature, building->type->width);
		add_preemptive_hash(signature, building->type->height);
	}
	for(enemy_team_iterator enemy(echo); enemy!=enemy_team_iterator(); ++enemy)
	{
		Team* enemy_team=echo.player->game->teams[*enemy];
		if(!enemy_team || !enemy_team->isAlive)
			continue;
		for(enemy_building_iterator item(echo, *enemy, -1, -1, CompletedBuildings);
			item!=enemy_building_iterator(); ++item)
		{
			Building* building=
				enemy_team->myBuildings[Building::GIDtoID(*item)];
			if(!building || building->type->isVirtual
			   || building->type->isBuildingSite)
				continue;
			add_preemptive_hash(signature, 0x454e4d59u);
			add_preemptive_hash(signature, *enemy);
			add_preemptive_hash(signature, building->gid);
			add_preemptive_hash(signature, building->posX);
			add_preemptive_hash(signature, building->posY);
			add_preemptive_hash(signature, building->type->width);
			add_preemptive_hash(signature, building->type->height);
		}
	}
	return signature;
}

void Maxima::clear_preemptive_defense(Context& echo)
{
	if(preemptive_guard_tiles.empty())
		return;
	MapInfo map(echo);
	const int w=map.get_width();
	RemoveArea* remove=new RemoveArea(GuardArea);
	int removed=0;
	for(std::set<int>::const_iterator tile=preemptive_guard_tiles.begin();
		tile!=preemptive_guard_tiles.end(); ++tile)
	{
		const int x=*tile%w;
		const int y=*tile/w;
		if(map.is_guard_area(x, y))
		{
			remove->add_location(x, y);
			removed+=1;
		}
	}
	if(removed>0)
		echo.add_management_order(remove);
	else
		delete remove;
	preemptive_guard_tiles.clear();

}

void Maxima::update_preemptive_defense(Context& echo)
{
	if(!budget.preemptive_defense_active)
	{
		clear_preemptive_defense(echo);
		last_preemptive_defense_tick=-1000000;
		last_preemptive_effective_zone_max=-1;
		last_preemptive_amphibious_active=false;
		return;
	}

	const Uint32 signature=compute_preemptive_building_signature(echo);
	if(!AIMaxima::Defense::topologyRefreshRequired(signature,
		preemptive_building_signature, budget.preemptive_effective_zone_max,
		last_preemptive_effective_zone_max,
		budget.preemptive_amphibious_active,
		last_preemptive_amphibious_active, timer,
		last_preemptive_defense_tick, budget.preemptive_recompute_ticks,
		last_preemptive_defense_tick>-500000))
		return;
	preemptive_building_signature=signature;
	last_preemptive_defense_tick=timer;
	last_preemptive_effective_zone_max=budget.preemptive_effective_zone_max;
	last_preemptive_amphibious_active=budget.preemptive_amphibious_active;

	MapInfo map(echo);
	const int w=map.get_width();
	const int h=map.get_height();
	const int map_size=w*h;
	std::vector<Uint8> land_walkable(map_size, 0);
	std::vector<Uint8> amphibious_walkable(map_size, 0);
	for(int y=0; y<h; ++y)
	{
		for(int x=0; x<w; ++x)
		{
			const int index=y*w+x;
			amphibious_walkable[index]=map.is_discovered(x, y)
				&& !map.is_resource(x, y) && !map.is_forbidden_area(x, y);
			land_walkable[index]=amphibious_walkable[index]
				&& !map.is_water(x, y);
		}
	}

	std::vector<PreemptiveBuilding> own_buildings;
	std::map<int, std::vector<PreemptiveBuilding> > enemy_buildings;
	for(int id=0; id<Building::MAX_COUNT; ++id)
	{
		Building* building=echo.player->team->myBuildings[id];
		if(building && !building->type->isVirtual
		   && !building->type->isBuildingSite)
			own_buildings.push_back(PreemptiveBuilding(
				echo.player->team->teamNumber, building));
	}
	for(enemy_team_iterator enemy(echo); enemy!=enemy_team_iterator(); ++enemy)
	{
		Team* enemy_team=echo.player->game->teams[*enemy];
		if(!enemy_team || !enemy_team->isAlive)
			continue;
		for(enemy_building_iterator item(echo, *enemy, -1, -1, CompletedBuildings);
			item!=enemy_building_iterator(); ++item)
		{
			Building* building=
				enemy_team->myBuildings[Building::GIDtoID(*item)];
			if(building && !building->type->isVirtual
			   && !building->type->isBuildingSite)
				enemy_buildings[*enemy].push_back(
					PreemptiveBuilding(*enemy, building));
		}
	}

	for(std::vector<PreemptiveBuilding>::const_iterator building=
		own_buildings.begin(); building!=own_buildings.end(); ++building)
	{
		mark_building_footprint(*building, w, h, land_walkable);
		mark_building_footprint(*building, w, h, amphibious_walkable);
	}
	for(std::map<int, std::vector<PreemptiveBuilding> >::const_iterator team=
		enemy_buildings.begin(); team!=enemy_buildings.end(); ++team)
		for(std::vector<PreemptiveBuilding>::const_iterator building=
			team->second.begin(); building!=team->second.end(); ++building)
		{
			mark_building_footprint(*building, w, h, land_walkable);
			mark_building_footprint(*building, w, h, amphibious_walkable);
		}

	AIMaxima::Defense::Policy policy;
	policy.innerDistance=budget.preemptive_inner_distance;
	policy.bandWidth=budget.preemptive_band_width;
	policy.pathSlack=budget.preemptive_path_slack;
	policy.probeRadius=budget.preemptive_probe_radius;
	policy.maximumCrossSection=budget.preemptive_cross_section_max;
	policy.zoneRadius=budget.preemptive_zone_radius;

	AIMaxima::Defense::ModeInput land;
	land.mode=AIMaxima::Defense::LandMode;
	land.width=w;
	land.height=h;
	land.walkable.assign(land_walkable.begin(), land_walkable.end());
	AIMaxima::Defense::ModeInput amphibious;
	amphibious.mode=AIMaxima::Defense::AmphibiousMode;
	amphibious.width=w;
	amphibious.height=h;
	amphibious.walkable.assign(amphibious_walkable.begin(),
		amphibious_walkable.end());
	for(std::vector<PreemptiveBuilding>::const_iterator building=
		own_buildings.begin(); building!=own_buildings.end(); ++building)
	{
		collect_building_perimeter(*building, w, h, land_walkable,
			land.homeSources);
		collect_building_perimeter(*building, w, h, amphibious_walkable,
			amphibious.homeSources);
	}
	for(std::map<int, std::vector<PreemptiveBuilding> >::const_iterator team=
		enemy_buildings.begin(); team!=enemy_buildings.end(); ++team)
	{
		AIMaxima::Defense::EnemySources land_enemy(team->first);
		AIMaxima::Defense::EnemySources amphibious_enemy(team->first);
		for(std::vector<PreemptiveBuilding>::const_iterator building=
			team->second.begin(); building!=team->second.end(); ++building)
		{
			collect_building_perimeter(*building, w, h, land_walkable,
				land_enemy.sources);
			collect_building_perimeter(*building, w, h, amphibious_walkable,
				amphibious_enemy.sources);
		}
		land.enemies.push_back(land_enemy);
		amphibious.enemies.push_back(amphibious_enemy);
	}

	std::vector<AIMaxima::Defense::ModeResult> modes;
	modes.push_back(AIMaxima::Defense::analyzeMode(land, policy));
	if(budget.preemptive_amphibious_active)
		modes.push_back(AIMaxima::Defense::analyzeMode(amphibious, policy));
	const AIMaxima::Defense::PlanResult plan=AIMaxima::Defense::combineModes(
		modes, budget.preemptive_effective_zone_max,
		budget.preemptive_zone_radius);

	std::set<int> desired;
	for(int index=0; index<map_size; ++index)
		if(index<int(plan.desired.size()) && plan.desired[index])
			desired.insert(index);

	// Warriors already assigned to preemptive defence hold the inner end of
	// maintained gates. Interior staging supports the planned firing lane
	// without requiring a beach advance. No extra units are requested here.
	for(int tile:barrier_defense_points)
		if(tile>=0&&tile<map_size&&land_walkable[tile])desired.insert(tile);

	std::vector<int> removals;
	std::vector<int> additions;
	std::set<int> next_owned;
	for(std::set<int>::const_iterator tile=preemptive_guard_tiles.begin();
		tile!=preemptive_guard_tiles.end(); ++tile)
	{
		if(desired.find(*tile)!=desired.end())
		{
			next_owned.insert(*tile);
			if(!map.is_guard_area(*tile%w, *tile/w))
				additions.push_back(*tile);
		}
		else if(map.is_guard_area(*tile%w, *tile/w))
			removals.push_back(*tile);
	}
	for(std::set<int>::const_iterator tile=desired.begin(); tile!=desired.end();
		++tile)
	{
		if(preemptive_guard_tiles.find(*tile)!=preemptive_guard_tiles.end())
			continue;
		if(!map.is_guard_area(*tile%w, *tile/w))
		{
			additions.push_back(*tile);
			next_owned.insert(*tile);
		}
	}
	if(!removals.empty())
	{
		RemoveArea* remove=new RemoveArea(GuardArea);
		for(std::vector<int>::const_iterator tile=removals.begin();
			tile!=removals.end(); ++tile)
			remove->add_location(*tile%w, *tile/w);
		echo.add_management_order(remove);
	}
	if(!additions.empty())
	{
		AddArea* add=new AddArea(GuardArea);
		for(std::vector<int>::const_iterator tile=additions.begin();
			tile!=additions.end(); ++tile)
			add->add_location(*tile%w, *tile/w);
		echo.add_management_order(add);
	}
	preemptive_guard_tiles.swap(next_owned);

}

void Maxima::compute_defense_flag_positioning(AIMaximaRuntime::Context& echo)
{
	if(!budget.reactive_defense_enabled)
	{
		for(std::vector<int>::const_iterator flag=defense_flags.begin();
			flag!=defense_flags.end(); ++flag)
			if(echo.get_building_register().is_building_found(*flag)
			   ||echo.get_building_register().is_building_pending(*flag))
				echo.add_management_order(new DestroyBuilding(*flag));
		defense_flags.clear();
		return;
	}
	//This algorithm works by finding all units and buildings under attack, and creating a potential
	//field by adding 1 to all squares within range of the units or buildings under attack. The result
	//will be that the highest square will have the largest number of buildings or units that need
	//defending within range. A flag is put onto the highest square, and the same concept is repeated,
	//except that all under-attack units or buildings that are within range of a placed defense flag
	//are ignored.

	//This algorithm does that, except optimized. A list is maintained to keep track of squares
	//that have a value other than 0 as these are the only ones we want to place a flag on, and
	//when a defense flag position is chosen, all units or buildings within range of the flag
	//have all squares within their range -1, effectivly doing the same as recalculating all
	//squares excluding those units now covered by a defense flag
	MapInfo     mi(echo);
	const int   w      = mi.get_width();
	const int   h      = mi.get_height();
	const int   RADIUS = budget.reactive_defense_flag_radius;
	int remaining_defense_allocation=std::max(0, budget.defense_reserve);

	Uint16* counts = new Uint16[w * h];
	Uint16* buildingGID = new Uint16[w * h];
	Uint16* unitGID = new Uint16[w * h];
	Uint16* enemyGID = new Uint16[w * h];
	memset(counts, 0, sizeof(Uint16) * w * h);
	memset(buildingGID, NOGBID, sizeof(Uint16) * w * h);
	memset(unitGID, NOGUID, sizeof(Uint16) * w * h);
	memset(enemyGID, NOGUID, sizeof(Uint16) * w * h);
	std::list<int> locations;

	//For every unit thats under attack, increment in the squares surrounding it.
	//Use the 'locations' list to keep track of non-zero squares
	for(int i=0; i<Unit::MAX_COUNT; ++i)
	{
		Unit* unit = echo.player->team->myUnits[i];
		if(unit && unit->underAttackTimer && unit->movement != Unit::MOV_ATTACKING_TARGET && unit->typeNum != EXPLORER && unitGID[(unit->posX+w)%w * h + (unit->posY+h)%h] == NOGUID)
		{
			unitGID[(unit->posX+w)%w * h + (unit->posY+h)%h] = unit->gid;
			modify_points(counts, w, h, (unit->posX+w)%w, (unit->posY+h)%h, RADIUS, 1, locations);
		}
	}
	for(int i=0; i<Building::MAX_COUNT; ++i)
	{
		Building* building = echo.player->team->myBuildings[i];
		if(building && building->underAttackTimer
		   && buildingGID[building->posX * h + building->posY] == NOGBID)
		{
			int nx = (building->posX - building->type->decLeft + w) %w;
			int ny = (building->posY - building->type->decTop + h) %h;
			buildingGID[building->posX * h + building->posY] = building->gid;
			modify_points(counts, w, h, nx, ny, RADIUS, 1, locations);
		}
	}

	// An under-attack callback is too late on water-heavy maps: by the time a
	// worker or pool is hit, the invaders already occupy the economic core and
	// the swimming/training pipeline starts disappearing. Muster around visible
	// warriors as soon as they enter the colony envelope. This remains a wholly
	// local response--it neither selects an allied target nor shares allied
	// information--and keeps mobile warriors, rather than early towers, as the
	// answer to an ordinary army.
	for(enemy_team_iterator enemy(echo); enemy!=enemy_team_iterator(); ++enemy)
	{
		Team* enemy_team=echo.player->game->teams[*enemy];
		if(!enemy_team || !enemy_team->isAlive)
			continue;
		for(int i=0; i<Unit::MAX_COUNT; ++i)
		{
			Unit* unit=enemy_team->myUnits[i];
			if(!unit || unit->typeNum!=WARRIOR
			   || !echo.player->map->isFOWDiscovered(
				unit->posX, unit->posY, echo.player->team->me))
				continue;
			bool near_colony=false;
			for(int b=0; b<Building::MAX_COUNT; ++b)
			{
				Building* own=echo.player->team->myBuildings[b];
				if(own && !own->type->isVirtual
				   && echo.player->map->warpDistSquare(unit->posX, unit->posY,
					own->posX, own->posY)<=144)
				{
					near_colony=true;
					break;
				}
			}
			if(!near_colony)
				continue;
			const int pos=(unit->posX+w)%w*h+(unit->posY+h)%h;
			if(enemyGID[pos]==NOGUID)
			{
				enemyGID[pos]=unit->gid;
				modify_points(counts, w, h, (unit->posX+w)%w,
					(unit->posY+h)%h, RADIUS, 1, locations);
			}
		}
	}

	///Choose the highest location, remove all units and buildings within a flags radius of that location,
	///and add that location to the list
	std::vector<int> flagLocations;
	std::vector<int> enemyUnits;
	std::map<int, std::vector<position> > flagCoverage;
	while(!locations.empty())
	{
		//Find the square with the highest value, a flag is put here
		int max = 0;
		int maxPos = 0;
		for(std::list<int>::iterator i = locations.begin(); i!=locations.end(); ++i)
		{
			int pos = *i;
			int n = counts[pos];
			if(n > max)
			{
				maxPos = pos;
				max = n;
			}
		}

		// Inserting twice the same flag is a bug and may lead to an
		// infinite loop. The most probable cause is an insufficient
		// margin in the loop on all units and buildings below.
		bool duplicatePosition = (max == 0);
		for (std::vector<int>::const_iterator i = flagLocations.begin();
		     i != flagLocations.end();
		     ++i)
			if (*i == maxPos)
			{
				duplicatePosition = true;
				break;
			}
		if (duplicatePosition)
			break;
		flagLocations.push_back(maxPos);
		std::vector<position>& covered_points=flagCoverage[maxPos];

		int max_x = maxPos / h;
		int max_y = maxPos % h;

		//test(echo, counts, w, h, squareProtected, locations);

		//For all units and buildings that are under attack and within the radius of the flag,
		//decrement the values surrounding them. At the same time, count the number of enemy
		//warriors in this zone
		int enemy_count = 0;
		// We need to loop over an area slightly bigger than RADIUS
		// because buildings are taken into account in an offset
		// location
		for(int px = -RADIUS-3; px <= RADIUS+3; ++px)
		{
			int nx = (max_x + px + w)%w;
			for(int py = -RADIUS-3; py<=RADIUS+3; ++py)
			{
				int ny = (max_y + py + h)%h;
				const bool covered=echo.player->map->warpDistSquare(
					max_x, max_y, nx, ny)<=RADIUS*RADIUS;
				if(covered && unitGID[nx * h + ny] != NOGUID)
				{
					Unit* unit = echo.player->team->myUnits[Unit::GIDtoID(unitGID[nx * h + ny])];
					covered_points.push_back(position(nx, ny));
					modify_points(counts, w, h, (unit->posX+w)%w, (unit->posY+h)%h, RADIUS, -1, locations);
					unitGID[nx * h + ny] = NOGUID;
				}
				if(buildingGID[nx * h + ny] != NOGBID)
				{
					Building* building = echo.player->team->myBuildings[Building::GIDtoID(buildingGID[nx * h + ny])];
					int nx2 = (building->posX - building->type->decLeft + w) %w;
					int ny2 = (building->posY - building->type->decTop + h) %h;
					if(echo.player->map->warpDistSquare(max_x, max_y, nx2, ny2)
					   <=RADIUS*RADIUS)
					{
						covered_points.push_back(position(nx2, ny2));
						modify_points(counts, w, h, nx2, ny2, RADIUS, -1, locations);
						buildingGID[nx * h + ny] = NOGBID;
					}
				}
				if(covered && enemyGID[nx * h + ny] != NOGUID)
				{
					covered_points.push_back(position(nx, ny));
					const Uint16 gid=enemyGID[nx * h + ny];
					Unit* enemy=echo.player->game->teams[Unit::GIDtoTeam(gid)]
						->myUnits[Unit::GIDtoID(gid)];
					if(enemy)
						modify_points(counts, w, h, (enemy->posX+w)%w,
							(enemy->posY+h)%h, RADIUS, -1, locations);
					enemyGID[nx * h + ny] = NOGUID;
				}

				// Take enemy units into account only if they are
				// within RADIUS of the flag (remember that we loop
				// over a bigger area).
				if (covered) {
					Uint16 guid = echo.player->map->getGroundUnit(nx, ny);
					if(guid != NOGUID && (1<<Unit::GIDtoTeam(guid)) & echo.player->team->enemies)
					{
						Unit* unit = echo.player->game->teams[Unit::GIDtoTeam(guid)]->myUnits[Unit::GIDtoID(guid)];
						if(unit && unit->typeNum == WARRIOR)
						{
							covered_points.push_back(position(nx, ny));
							enemy_count += 1;
						}
					}
				}
			}
		}
		// Defending at exact numerical parity loses repeatedly to trained attack
		// waves. Concentrate a modest local advantage without draining the entire
		// reserve into every skirmish.
		const int desired=std::min(budget.reactive_defense_unit_cap,
			enemy_count+std::max(budget.reactive_defense_advantage_min,
				enemy_count*budget.reactive_defense_advantage_percent/100));
		const int allocated=std::min(remaining_defense_allocation, desired);
		enemyUnits.push_back(allocated);
		remaining_defense_allocation-=allocated;
	}

	//Remove all flags with an enemy_count of 0
	for(std::vector<int>::iterator i=flagLocations.begin(); i!=flagLocations.end();)
	{
		int n = i-flagLocations.begin();
		if(enemyUnits[n] == 0)
		{
			i = flagLocations.erase(i);
			enemyUnits.erase(enemyUnits.begin() + n);
		}
		else
		{
			++i;
		}
	}

	//Take all existing defense flags, and move them to the nearest new flag position
	{}

	std::vector<int> existing_defense_flags(defense_flags);
	while(!existing_defense_flags.empty())
	{
		int min_dist = INT_MAX;
		int min_flag = 0;
		int min_pos = 0;
		int min_pos_x = 0;
		int min_pos_y = 0;
		int min_enemy = 0;
		///Choose the flag <-> flag location combination that has the lowest distance, start from it
		for(std::vector<int>::iterator i = existing_defense_flags.begin(); i!=existing_defense_flags.end(); ++i)
		{
			if(echo.get_building_register().is_building_found(*i))
			{
				Building* b = echo.get_building_register().get_building(*i);
				for(std::vector<int>::iterator j = flagLocations.begin(); j!=flagLocations.end(); ++j)
				{
					int flag_x = (*j) / h;
					int flag_y = (*j) % h;
					int d = echo.player->map->warpDistSquare(flag_x, flag_y, b->posX, b->posY);
					if(d < min_dist)
					{
						min_dist = d;
						min_flag = i - existing_defense_flags.begin();
						min_pos = j - flagLocations.begin();
						min_pos_x = flag_x;
						min_pos_y = flag_y;
						min_enemy = enemyUnits[j - flagLocations.begin()];
					}
				}
			}
		}
		// The director owns the maximum tactical retask distance.
		if(min_dist<budget.reactive_defense_move_radius
			*budget.reactive_defense_move_radius)
		{
			int id_flag = existing_defense_flags[min_flag];
			Building* flag=echo.get_building_register().get_building(id_flag);
			const std::vector<position>& covered_points=flagCoverage[flagLocations[min_pos]];
			bool coverage_preserved=true;
			for(std::vector<position>::const_iterator point=covered_points.begin();
				point!=covered_points.end(); ++point)
				if(echo.player->map->warpDistSquare(flag->posX, flag->posY,
					point->x, point->y)>RADIUS*RADIUS)
				{
					coverage_preserved=false;
					break;
				}
			existing_defense_flags.erase(existing_defense_flags.begin() + min_flag);
			flagLocations.erase(flagLocations.begin() + min_pos);
			enemyUnits.erase(enemyUnits.begin() + min_pos);

			const int deadband=budget.reactive_defense_move_deadband;
			// Suppress jitter only while the unchanged flag covers every point
			// this cluster was selected to defend.
			if(min_dist>deadband*deadband || !coverage_preserved)
			{
				ManagementOrder* mo_move=new ChangeFlagPosition(min_pos_x, min_pos_y, id_flag);
				echo.add_management_order(mo_move);

			}

			if(flag->unitStayRange!=RADIUS)
				echo.add_management_order(new ChangeFlagSize(RADIUS, id_flag));
			if(min_enemy != echo.get_building_register().get_assigned(id_flag))
			{
				ManagementOrder* mo_assign=new AssignWorkers(min_enemy, id_flag);
				echo.add_management_order(mo_assign);

			}
		}
		else
		{
			break;
		}
	}
	// Unmatched flags normally disappear, but keep one in place while it still
	// covers a local warrior threat. This prevents a moving cluster from making
	// the same useful flag alternate between destruction and recreation.
	for(std::vector<int>::iterator i = existing_defense_flags.begin(); i!=existing_defense_flags.end(); ++i)
	{
		if(echo.get_building_register().is_building_found(*i))
		{
			Building* flag=echo.get_building_register().get_building(*i);
			int local_enemy_count=0;
			for(int px=-RADIUS; px<=RADIUS; ++px)
			{
				for(int py=-RADIUS; py<=RADIUS; ++py)
				{
					if(px*px+py*py>RADIUS*RADIUS)
						continue;
					const int nx=(flag->posX+px+w)%w;
					const int ny=(flag->posY+py+h)%h;
					const Uint16 guid=echo.player->map->getGroundUnit(nx, ny);
					if(guid==NOGUID
					   || !((1<<Unit::GIDtoTeam(guid))
						& echo.player->team->enemies))
						continue;
					Unit* enemy=echo.player->game->teams[Unit::GIDtoTeam(guid)]
						->myUnits[Unit::GIDtoID(guid)];
					if(enemy && enemy->typeNum==WARRIOR)
						local_enemy_count+=1;
				}
			}
			if(local_enemy_count>0 && remaining_defense_allocation>0)
			{
				const int desired=std::min(remaining_defense_allocation,
					std::min(budget.reactive_defense_unit_cap,
					local_enemy_count+std::max(
						budget.reactive_defense_advantage_min,
						local_enemy_count
							*budget.reactive_defense_advantage_percent/100)));
				remaining_defense_allocation-=desired;
				if(desired!=echo.get_building_register().get_assigned(*i))
				{
					echo.add_management_order(new AssignWorkers(desired, *i));

				}

			}
			else
			{
				echo.add_management_order(new DestroyBuilding(*i));

			}
		}
	}

	//If there are remaining positions on the map, it is because we didn't have enough existing
	//flags to cover them, so create new ones
	for(std::vector<int>::iterator i = flagLocations.begin(); i!=flagLocations.end(); ++i)
	{
		int enemy = enemyUnits[i - flagLocations.begin()];
		int flag_x = *i / h;
		int flag_y = *i % h;

		//The main order for the war flag
		BuildingOrder* bo_flag = new BuildingOrder(IntBuildingType::WAR_FLAG, enemy);
		bo_flag->add_constraint(new Construction::SinglePosition(flag_x, flag_y));
		unsigned int id_flag=echo.add_building_order(bo_flag);
		defense_flags.push_back(id_flag);

		ManagementOrder* mo_minimum=new ChangeFlagMinimumLevel(1, id_flag);
		echo.add_management_order(mo_minimum);
		ManagementOrder* mo_completion=new ChangeFlagSize(
			budget.reactive_defense_flag_radius, id_flag);
		echo.add_management_order(mo_completion);

		ManagementOrder* mo_destroyed=new Notify(RuntimeEvent(RuntimeEvent::GuardFlagDeleted, id_flag));
		mo_destroyed->add_condition(new BuildingDestroyed(id_flag));
		echo.add_management_order(mo_destroyed);
	}

	delete[] counts;
	delete[] unitGID;
	delete[] buildingGID;
	delete[] enemyGID;
}

void Maxima::modify_points(Uint16* counts, int w, int h, int x, int y, int dist, int value, std::list<int>& locations)
{
	for(int px = -dist; px <= dist; ++px)
	{
		int nx = (x + px + w)%w;
		for(int py = -dist; py <= dist; ++py)
		{
			int ny = (y + py + h)%h;
			if(px * px + py * py <= dist * dist)
			{
				if(value>0)
				{
					if(counts[nx * h + ny] == 0)
						locations.push_back(nx * h + ny);
					counts[nx * h + ny] += value;
				}
				else if(value<0)
				{
					counts[nx * h + ny] += value;
					if(counts[nx * h + ny] == 0)
						locations.remove(nx * h + ny);
				}
			}
		}
	}
}

void Maxima::compute_explorer_flag_attack_positioning(AIMaximaRuntime::Context& echo)
{
	//The algorithm here is interesting. Bassically, an enemy unit is selected. Every enemy unit within 4 squares of this unit
	//is counted as part of the larger group, and every unit 4 squares from those and so on, as long as it doesn't go past
	//6 squares from the average. Flags are put on the average x and y of largest groups
	MapInfo mi(echo);
	const int w = mi.get_width();
	const int h = mi.get_height();

	std::vector<boost::tuple<int, int, int> > groups;
	// Keep explorer and warrior pressure concentrated on the same opponent. The
	// lower-threshold independent raids tested here produced weaker, fragmented
	// strikes and lost the G2 gate that the concentrated version had won.
	const bool following_offense=(tactical_mission.kind==Tactics::MissionRaid
		|| tactical_mission.kind==Tactics::MissionSiege)
		&& tactical_mission.phase!=Tactics::PhaseIdle
		&& tactical_mission.phase!=Tactics::PhaseCooldown
		&& tactical_mission.phase!=Tactics::PhaseWithdraw;
	const int strike_target=following_offense ? tactical_mission.targetTeam : target;

	if(budget.explorer_campaign_active && strike_target>=0
	   && strike_target<Team::MAX_COUNT && echo.player->game->teams[strike_target]
	   && echo.player->game->teams[strike_target]->isAlive
	   && (echo.player->team->enemies&echo.player->game->teams[strike_target]->me))
	{
		Unit** units = new Unit*[Unit::MAX_COUNT];
		Unit* first = NULL;
		for(int i=0; i<Unit::MAX_COUNT; ++i)
		{
			Unit* unit = echo.player->game->teams[strike_target]->myUnits[i];
			if(unit && echo.player->map->isFOWDiscovered(unit->posX, unit->posY,
				echo.player->team->me) && unit->typeNum==WARRIOR
			   && unit->activity != Unit::ACT_UPGRADING)
			{
				if(!first)
					first = unit;
				units[i] = unit;
			}
			else
			{
				units[i] = NULL;
			}
		}

		while(true)
		{
			int group_x = 0;
			int group_y = 0;
			int group_size = 0;

			std::queue<Unit*> proccess;
			std::queue<int> xposs;
			std::queue<int> yposs;
			for(int i=0; i<Unit::MAX_COUNT; ++i)
			{
				if(units[i])
				{
					group_x += units[i]->posX;
					group_y += units[i]->posY;
					proccess.push(units[i]);
					xposs.push(units[i]->posX);
					yposs.push(units[i]->posY);
					units[i] = NULL;
					group_size+=1;
					break;
				}
			}

			if(group_size == 0)
				break;

			while(!proccess.empty())
			{
				Unit* top = proccess.front();
				int ix = xposs.front();
				int iy = yposs.front();
				proccess.pop();
				xposs.pop();
				yposs.pop();
				for(int dx = -4; dx<=4; ++dx)
				{
					int nx = (top->posX + dx + w) % w;
					for(int dy = -4; dy<=4; ++dy)
					{
						int ny = (top->posY + dy + h) % h;
						if(echo.player->map->warpDistSquare(group_x / group_size, group_y / group_size, nx, ny) < (6*6))
						{
							Uint16 guid = echo.player->map->getGroundUnit(nx, ny);
							if(guid != NOGUID && Unit::GIDtoTeam(guid) == strike_target)
							{
								int id = Unit::GIDtoID(guid);
								if(units[id])
								{
									group_x += ix + dx;
									group_y += iy + dy;
									proccess.push(units[id]);
									xposs.push(ix + dx);
									yposs.push(iy + dy);
									units[id] = NULL;
									group_size+=1;
								}
							}
						}
					}
				}
			}
			group_x = (group_x / group_size + w)%w;
			group_y = (group_y / group_size + h)%h;

			groups.push_back(boost::make_tuple(group_size, group_x, group_y));
		}
		delete[] units;

	}

	std::sort(groups.begin(), groups.end(), std::greater<boost::tuple<int, int, int> >());

	int total_attacks=budget.explorer_campaign_flags;

	//Go through existing flags and see if they can be moved to be on top of new groups
	std::vector<int> existing_explorer_attack_flags(explorer_attack_flags);
	while(total_attacks && !existing_explorer_attack_flags.empty())
	{
		int min_dist = INT_MAX;
		int min_flag = 0;
		int min_pos = 0;
		int min_pos_x = 0;
		int min_pos_y = 0;
		///Choose the flag <-> flag location combination that has the lowest distance, start from it
		for(std::vector<int>::iterator i = existing_explorer_attack_flags.begin(); i!=existing_explorer_attack_flags.end(); ++i)
		{
			if(echo.get_building_register().is_building_found(*i))
			{
				Building* b = echo.get_building_register().get_building(*i);
				for(std::vector<boost::tuple<int, int, int> >::iterator j = groups.begin(); j!=groups.end(); ++j)
				{
					int flag_x = j->get<1>();
					int flag_y = j->get<2>();
					int d = echo.player->map->warpDistSquare(flag_x, flag_y, b->posX, b->posY);
					if(d < min_dist)
					{
						min_dist = d;
						min_flag = i - existing_explorer_attack_flags.begin();
						min_pos = j - groups.begin();
						min_pos_x = flag_x;
						min_pos_y = flag_y;
					}
				}
			}
		}

		if(min_dist != INT_MAX)
		{
			total_attacks-=1;
			int id_flag = existing_explorer_attack_flags[min_flag];

			existing_explorer_attack_flags.erase(existing_explorer_attack_flags.begin() + min_flag);
			groups.erase(groups.begin() + min_pos);

			if(min_dist != 0)
			{
				ManagementOrder* mo_move=new ChangeFlagPosition(min_pos_x, min_pos_y, id_flag);
				echo.add_management_order(mo_move);
			}
		}
		else
		{
			break;
		}
	}

	//If there are remaining flags, its because these flags don't have a new position
	//on the map to go to, so delete them
	for(std::vector<int>::iterator i = existing_explorer_attack_flags.begin(); i!=existing_explorer_attack_flags.end(); ++i)
	{
		if(echo.get_building_register().is_building_found(*i))
		{
			ManagementOrder* mo_destroyed=new DestroyBuilding(*i);
			echo.add_management_order(mo_destroyed);
		}
	}

	while(total_attacks && !groups.empty())
	{
		boost::tuple<int, int, int> groupInfo = *groups.begin();
		groups.erase(groups.begin());
		total_attacks -= 1;

		BuildingOrder* bo_flag = new BuildingOrder(IntBuildingType::EXPLORATION_FLAG,
			budget.explorer_campaign_units_per_flag);
		bo_flag->add_constraint(new Construction::SinglePosition(groupInfo.get<1>(), groupInfo.get<2>()));
		unsigned int id_flag=echo.add_building_order(bo_flag);

		ManagementOrder* mo_completion=new ChangeFlagSize(6, id_flag);
		echo.add_management_order(mo_completion);

		ManagementOrder* mo_level=new ChangeFlagMinimumLevel(4, id_flag);
		echo.add_management_order(mo_level);

		explorer_attack_flags.push_back(id_flag);

		ManagementOrder* mo_destroyed=new Notify(
			RuntimeEvent(RuntimeEvent::ExplorerAttackFlagDeleted, id_flag));
		mo_destroyed->add_condition(new BuildingDestroyed(id_flag));
		echo.add_management_order(mo_destroyed);
	}
}

void Maxima::update_fruit_flags(AIMaximaRuntime::Context& echo)
{
	// Reconcile per resource against the serialized runtime, rather than trusting
	// a one-shot Boolean. Pending searches count too, including after a load.
	std::set<int> other_flags(explorer_attack_flags.begin(), explorer_attack_flags.end());
	for(const Recon::ReconMission& mission:reconnaissance.report().missions)
		other_flags.insert(mission.flagId);
	const int fruits[]={CHERRY, ORANGE, PRUNE};
	exploration_on_fruit=false;
	for(int fruit:fruits)
	{
		std::vector<int> flags=echo.resource_flags(fruit);
		flags.erase(std::remove_if(flags.begin(), flags.end(),
			[&](int id){return other_flags.count(id)!=0;}), flags.end());
		if(!budget.fruit_active)
		{
			for(int id:flags) echo.cancel_or_destroy_building(id);
			continue;
		}
		if(!flags.empty())
		{
			exploration_on_fruit=true;
			continue;
		}
		GradientInfo source;
		source.add_source(new Entities::Resource(fruit));
		// Some maps contain only one or two fruit species. Retry failed flags,
		// but do not keep queuing full-map searches for an absent resource.
		if(!echo.get_gradient_manager().get_gradient(source).has_sources()) continue;
		exploration_on_fruit=true;
		GradientInfo home;
		home.add_source(new Entities::AnyTeamBuilding(
			echo.player->team->teamNumber, CompletedBuildings));
		BuildingOrder* order=new BuildingOrder(IntBuildingType::EXPLORATION_FLAG,
			budget.fruit_units_per_flag);
		order->add_constraint(new MinimizedDistance(home, 1));
		order->add_constraint(new MaximumDistance(source, 0));
		const int id=echo.add_building_order(order);
		echo.add_management_order(new ChangeFlagSize(budget.fruit_flag_radius, id));
	}
	update_fruit_alliances(echo);
}

void Maxima::update_fruit_alliances(AIMaximaRuntime::Context& echo)
{
	bool activated=budget.fruit_active;

	for(enemy_team_iterator i(echo); i!=enemy_team_iterator(); ++i)
	{
		ManagementOrder* mo_alliance=new ChangeAlliances(*i, KeepValue,
			KeepValue, KeepValue, activated ? SetValue : ClearValue, KeepValue);
		echo.add_management_order(mo_alliance);
	}
}

}
