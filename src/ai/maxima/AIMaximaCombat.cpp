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

// Combat control for Maxima: objectives, waves, and defensive flags.
#include "AIMaxima.h"
#include "AIMaximaWorldHelpers.h"
#include "AITelemetryFields.h"
#include "Game.h"
#include "Utilities.h"
#include "boost/lexical_cast.hpp"
#include <algorithm>
#include <climits>
#include <cstring>
#include <functional>
#include <sstream>

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
	template<typename T>
	std::string diagnostic_value(const T& value)
	{
		std::ostringstream stream;
		stream<<value;
		return stream.str();
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
		const Building* continuingFlag, int minimumLevel,
		const std::vector<const Building*>* offenseFlags=NULL)
	{
		// Match the engine's flag subscription rule: the lower of the two combat
		// abilities must reach the flag's minimum level (user level minus one).
		if(!warrior || warrior->typeNum!=WARRIOR || warrior->isDead
		   || warrior->medical!=Unit::MED_FREE
		   || std::min(warrior->level[ATTACK_SPEED],
			warrior->level[ATTACK_STRENGTH])<minimumLevel-1)
			return false;
		if(offenseFlags && warrior->attachedBuilding
		   && std::find(offenseFlags->begin(),offenseFlags->end(),warrior->attachedBuilding)!=offenseFlags->end())
			return true;
		if(continuingFlag && warrior->attachedBuilding==continuingFlag)
			return true;
		return !warrior->attachedBuilding && warrior->activity==Unit::ACT_RANDOM
			&& warrior->movement!=Unit::MOV_ATTACKING_TARGET;
	}

	// Label each traversable component once per decision. A route from an
	// outpost proves nothing about warriors stranded in another component.
	class TacticalReachability
	{
	public:
		TacticalReachability(Map* map, int minimumLevel,
			const std::vector<const Building*>* offenseFlags=NULL)
			: map(map), minimumLevel(minimumLevel), offenseFlags(offenseFlags) {}
		std::vector<int> powersAt(Team* team, const Building* continuingFlag,
			int x, int y, int cap, bool swimmersOnly=false,
			std::map<std::string, int>* diagnostics=NULL)
		{
			std::vector<int> powers;
			const int target=map->normalizeY(y)*map->getW()+map->normalizeX(x);
			for(int id=0; id<Unit::MAX_COUNT; ++id)
			{
				const Unit* warrior=team->myUnits[id];
				if(diagnostics && warrior && warrior->typeNum==WARRIOR && !warrior->isDead)
				{
					if(std::min(warrior->level[ATTACK_SPEED],warrior->level[ATTACK_STRENGTH])<minimumLevel-1)
						++(*diagnostics)["untrained"];
					else if(warrior->medical!=Unit::MED_FREE)
						++(*diagnostics)["medical"];
					else if(!tactical_warrior_available(warrior,continuingFlag,minimumLevel,offenseFlags))
					{
						++(*diagnostics)["busy"];
						if(warrior->attachedBuilding)
							++(*diagnostics)["busy_attached_type"+diagnostic_value(
								warrior->attachedBuilding->type->shortTypeNum)];
						else if(warrior->activity!=Unit::ACT_RANDOM)
							++(*diagnostics)["busy_activity"+diagnostic_value(int(warrior->activity))];
						else
							++(*diagnostics)["busy_attacking"];
					}
				}
				if(!tactical_warrior_available(warrior,continuingFlag,minimumLevel,offenseFlags)) continue;
				const bool swimming=warrior->performance[SWIM]>0;
				if(swimmersOnly && !swimming)
				{
					if(diagnostics) ++(*diagnostics)["no_swim"];
					continue;
				}
				if(components[swimming].empty()) label(swimming);
				const std::vector<int>& field=components[swimming];
				const int origin=map->normalizeY(warrior->posY)*map->getW()
					+map->normalizeX(warrior->posX);
				if(field[target]>=0 && field[origin]==field[target])
					powers.push_back(warrior_power(warrior));
				else if(diagnostics) ++(*diagnostics)["disconnected"];

			}
			if(diagnostics) (*diagnostics)["reachable_before_reserve_cap"]=int(powers.size());
			std::sort(powers.begin(),powers.end(),std::greater<int>());
			if(int(powers.size())>cap) powers.resize(std::max(0,cap));
			return powers;
		}
	private:
		Map* map;
		int minimumLevel;
		const std::vector<const Building*>* offenseFlags;
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

	bool rally_walkable(const Map* map, Uint32 team, int x, int y)
	{
		return map->getBuilding(x,y)==NOGBID && !map->isResource(x,y)
			&& !map->isWater(x,y) && !map->isForbidden(x,y,team);
	}

	// Count distinct connected standing tiles inside the engine's circular flag
	// range. Mobile units do not make a permanent obstacle to their own rally.
	int rally_space(Map* map, Uint32 team, int x, int y, int radius, int needed)
	{
		if(!rally_walkable(map,team,x,y))return 0;
		std::set<int> visited;
		std::vector<std::pair<int,int>> queue;
		queue.push_back(std::make_pair(x,y));visited.insert(map->coordToIndex(x,y));
		for(size_t head=0;head<queue.size();++head)
		{
			const auto point=queue[head];
			for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
			{
				const int px=map->normalizeX(point.first+dx),py=map->normalizeY(point.second+dy);
				if(map->warpDistSquare(px,py,x,y)>radius*radius
				   || !visited.insert(map->coordToIndex(px,py)).second)continue;
				if(rally_walkable(map,team,px,py))
				{
					queue.push_back(std::make_pair(px,py));
					if(int(queue.size())>=needed)return needed;
				}
			}
		}
		return int(queue.size());
	}

	struct PreemptiveBuilding
	{
		PreemptiveBuilding(int team, Building* building)
			: team(team), x(building->posX), y(building->posY),
			  width(building->type->width), height(building->type->height),
			  gid(building->gid)
		{
		}

		int team;
		int x;
		int y;
		int width;
		int height;
		int gid;
	};

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

}

// Offensive objectives, assault waves, and defensive flag placement.

namespace
{
	int wrapped_center(int origin, int size, int extent)
	{
		return (origin+size/2+extent)%extent;
	}
}

void Maxima::OffenseDiagnostics::reset(int currentTick)
{
	tick=currentTick;
	gate="not evaluated";
	decision="none";
	eligibleWarriors=0;
	openTrainingSlots=0;
	buildingCandidates=0;
	viableBuildings=0;
	clusterCandidates=0;
	viableClusters=0;
	bestScore=INT_MIN;
	rejections.clear();
}

/// Choose the target the offensive flag should sit on. Fills the tactical
/// budget fields and the diagnostics; the executor moves the flag.
void Maxima::plan_offense(Context& echo)
{
	offense_diagnostics.reset(timer);
	budget.tactical_kind=Tactics::MissionNone;
	budget.tactical_target_team=-1;
	budget.tactical_dig_out_team=-1;
	budget.tactical_target_gid=-1;
	budget.tactical_target_x=0;
	budget.tactical_target_y=0;
	budget.tactical_candidate_score=INT_MIN;
	budget.tactical_requested_force=0;
	if(!strategy.tactics.enabled)
	{
		offense_diagnostics.gate="blocked: warrior tactics disabled";
		return;
	}
	if(severe_colony_emergency())
	{
		offense_diagnostics.gate="blocked: colony emergency keeps the army home";
		return;
	}
	Map* map=echo.player->map;
	const Building* flag=tactical_mission.flagId>=0
		&& echo.get_building_register().is_building_found(tactical_mission.flagId)
		? echo.get_building_register().get_building(tactical_mission.flagId) : NULL;
	const bool active=tactical_mission.flagId>=0;
	int believed_defenders=0;
	for(int team=0; team<Team::MAX_COUNT; ++team)
		if(opponents[team].alive)
			believed_defenders=std::max(believed_defenders,
				opponents[team].estimated_warriors);
	int believed_power=0;
	bool learned_power=false;
	if(strategy.reconnaissance.force_memory_enabled)
		for(const auto& entry:force_beliefs)
			if(entry.second.initialized && opponents[entry.first].alive)
			{
				learned_power=true;
				believed_power=std::max(believed_power,entry.second.prediction.rounded(ForceModel::Power));
			}
	const auto strength_sufficient=[&](long long own_power) {
		return learned_power ? own_power>=believed_power
			: Labour::attackStrengthSufficient(own_power,believed_defenders);
	};
	std::vector<const Building*> offenseFlags;
	for(const auto& wave:offense_waves)
		if(echo.get_building_register().is_building_found(wave.flagId))
			offenseFlags.push_back(echo.get_building_register().get_building(wave.flagId));
	for(int id:attack_flags)
		if(echo.get_building_register().is_building_found(id))
			offenseFlags.push_back(echo.get_building_register().get_building(id));
	int eligible=0;
	long long eligible_damage_rate=0;
	// Warriors eating, healing or training are away from the flag but not lost
	// to the offense: they come back, and a siege dropped every time its army
	// cycles through the inns is a siege that never finishes.
	int recovering=0;
	std::vector<const Unit*> trainees;
	const auto muster=[&](int level) {
		eligible=0;eligible_damage_rate=0;recovering=0;trainees.clear();
		for(int id=0; id<Unit::MAX_COUNT; ++id)
		{
			const Unit* warrior=echo.player->team->myUnits[id];
			if(tactical_warrior_available(warrior, flag, level, &offenseFlags))
			{
				++eligible;
				eligible_damage_rate+=learned_power ? warrior_power(warrior) : Labour::WarriorDamageRate[std::max(0, std::min(3,
					std::min(warrior->level[ATTACK_SPEED], warrior->level[ATTACK_STRENGTH])))];
				trainees.push_back(warrior);
			}
			else if(warrior && warrior->typeNum==WARRIOR && !warrior->isDead
				&& (warrior->medical!=Unit::MED_FREE
					|| warrior->activity==Unit::ACT_UPGRADING))
				++recovering;
		}
	};
	// The configured flag level takes only warriors trained in both combat
	// abilities. Training is a long queue, so most of an army sits below that
	// bar; when the trained few are not strong enough for the defenders but
	// the whole army is, the flag is raised for everyone. Its strength, not its
	// paperwork, is what the gate measures.
	// A pending flag uses the saved requested level until the engine creates it.
	// Once found, its recruitment rule is authoritative (stored zero-based).
	int flag_level=active ? (flag ? flag->minLevelToFlag+1
		: budget.tactical_flag_level) : strategy.tactics.flag_minimum_level;
	muster(flag_level);
	if(!active && flag_level>1
	   && !strength_sufficient(eligible_damage_rate))
	{
		muster(1);
		if(strength_sufficient(eligible_damage_rate))
			flag_level=1;
		else
		{
			flag_level=strategy.tactics.flag_minimum_level;
			muster(flag_level);
		}
	}
	budget.tactical_flag_level=flag_level;
	TacticalReachability reachability(map, flag_level, &offenseFlags);
	offense_diagnostics.eligibleWarriors=eligible;
	// Reserve training only for available warriors who can learn there.
	// Match trainees to capacity so overlapping barracks do not reserve the
	// same soldier twice or hold fully trained soldiers back indefinitely.
	std::vector<const Building*> barracks;
	std::vector<int> capacities;
	for(int id=0; id<Building::MAX_COUNT; ++id)
	{
		const Building* b=echo.player->team->myBuildings[id];
		if(!b || b->type->shortTypeNum!=IntBuildingType::ATTACK_BUILDING
		   || b->type->isBuildingSite)continue;
		const int capacity=b->maxUnitInside-int(b->unitsInside.size());
		if(capacity>0){barracks.push_back(b);capacities.push_back(capacity);}
	}
	std::vector<std::vector<int>> assignments(barracks.size());
	std::vector<std::vector<int>> choices(trainees.size());
	for(size_t u=0;u<trainees.size();++u)
		for(size_t b=0;b<barracks.size();++b)
			for(int ability=WALK;ability<ARMOR;++ability)
				if(trainees[u]->canLearn[ability] && barracks[b]->type->upgrade[ability]
				   && trainees[u]->level[ability]<=barracks[b]->type->level)
				{choices[u].push_back(int(b));break;}
	std::function<bool(int,std::vector<bool>&)> assignTraining=
		[&](int unit,std::vector<bool>& visited) {
			for(int b:choices[unit])
			{
				if(visited[b])continue;
				visited[b]=true;
				if(int(assignments[b].size())<capacities[b])
				{assignments[b].push_back(unit);return true;}
				for(int& previous:assignments[b])
					if(assignTraining(previous,visited))
					{previous=unit;return true;}
			}
			return false;
		};
	int open_training_slots=0;
	for(size_t u=0;u<trainees.size();++u)
	{
		std::vector<bool> visited(barracks.size(),false);
		if(assignTraining(int(u),visited))++open_training_slots;
	}
	offense_diagnostics.openTrainingSlots=open_training_slots;
	const int surplus=eligible-open_training_slots;
	// The minimum force only gates raising a new flag. An existing flag keeps
	// its target while any warrior can still reach it, so that warriors cycling
	// through inns and training halls do not make the offense flap.
	// A new attack needs the configured minimum and the strength to clear the
	// believed defenders; an army that trained needs fewer heads for that.
	const int minimum=active ? 1 : std::max(1, strategy.tactics.min_force);
	if(!active && !strength_sufficient(eligible_damage_rate))
	{
		offense_diagnostics.gate="blocked: "+diagnostic_value(eligible)
			+" eligible warriors are not strong enough for "
			+(learned_power ? diagnostic_value(believed_power)+" estimated enemy combat power"
				: diagnostic_value(believed_defenders)+" believed defenders");
		return;
	}
	// An active siege is judged on the army it still has, recovering warriors
	// included, and does not yield to open training slots: those are filled by
	// warriors between fights, not by abandoning the target.
	const int committed=active ? eligible+recovering : surplus;
	if(committed<minimum)
	{
		offense_diagnostics.gate="blocked: "+diagnostic_value(eligible)
			+" eligible warriors, "+diagnostic_value(open_training_slots)
			+" training reservations; "+diagnostic_value(committed)
			+" deployable is below "+diagnostic_value(minimum);
		return;
	}
	offense_diagnostics.gate="open";

	GradientInfo land_info;
	land_info.add_source(new Entities::AnyTeamBuilding(
		echo.player->team->teamNumber, CompletedBuildings));
	land_info.add_obstacle(new Entities::AnyResource);
	land_info.add_obstacle(new Entities::Water);
	Gradient& land_route=echo.get_gradient_manager().get_gradient(land_info);
	GradientInfo swim_info;
	swim_info.add_source(new Entities::AnyTeamBuilding(
		echo.player->team->teamNumber, CompletedBuildings));
	swim_info.add_obstacle(new Entities::AnyResource);
	Gradient& swim_route=echo.get_gradient_manager().get_gradient(swim_info);

	// Route length to a point, or -1 when it is unreachable or too few of the
	// surplus can get there. Walkers go first; swimmers carry the crossing when
	// the only route is over water.
	const int cap=strategy.military.attack_unit_cap
		*strategy.assault.max_waves;
	std::map<std::string,int>& why=offense_diagnostics.rejections;
	const auto route_to=[&](int x, int y)
	{
		int distance=land_route.get_height(x,y);
		const bool amphibious=distance<0;
		if(amphibious)
			distance=swim_route.get_height(x,y);
		if(distance<0)
		{
			++why["no_route"];
			return -1;
		}
		if(int(reachability.powersAt(echo.player->team, flag, x, y, cap,
			amphibious).size())<minimum)
		{
			++why["too_few_reachable"];
			return -1;
		}
		return distance;
	};

	int best_score=INT_MIN;
	Tactics::MissionKind best_kind=Tactics::MissionNone;
	int best_team=-1, best_gid=-1, best_x=0, best_y=0;
	// A young objective that is still alive is kept whatever else appears, so
	// the army actually arrives somewhere instead of chasing every sighting.
	const bool dwelling=active
		&& timer-tactical_mission.phaseSinceTick<strategy.tactics.dwell_ticks;
	bool current_alive=false;
	int current_score=INT_MIN, current_x=0, current_y=0;
	bool known_unreachable=false;
	int unreachable_team=-1;
	int unreachable_score=INT_MIN;

	if(strategy.tactics.siege_enabled)
	{
		const Recon::ReconReport& report=reconnaissance.report();
		for(std::map<int, Recon::OpponentIntel>::const_iterator opponent=
			report.opponents.begin(); opponent!=report.opponents.end(); ++opponent)
		{
			if(!opponent->second.alive) continue;
			for(std::map<int, Recon::BuildingSighting>::const_iterator building=
				opponent->second.buildings.begin();
				building!=opponent->second.buildings.end(); ++building)
			{
				const Recon::BuildingSighting& sighting=building->second;
				++offense_diagnostics.buildingCandidates;
				if(Tactics::Program::targetQuarantined(sighting.gid, timer,
					strategy.tactics.failed_target_quarantine_enabled,
					attack_target_quarantine_until))
				{
					++why["quarantined"];
					continue;
				}
				const int x=wrapped_center(sighting.x, sighting.width, map->getW());
				const int y=wrapped_center(sighting.y, sighting.height, map->getH());
				const int distance=route_to(x, y);
				if(distance<0)
				{
					if(opponents[opponent->first].score>unreachable_score)
					{
						known_unreachable=true;
						unreachable_team=opponent->first;
						unreachable_score=opponents[opponent->first].score;
					}
					continue;
				}
				++offense_diagnostics.viableBuildings;
				int nearby_towers=0;
				for(std::map<int, Recon::BuildingSighting>::const_iterator tower=
					opponent->second.buildings.begin();
					tower!=opponent->second.buildings.end(); ++tower)
					if(tower->second.type==IntBuildingType::DEFENSE_BUILDING
					   && map->warpDistSquare(sighting.x, sighting.y,
						tower->second.x, tower->second.y)<=64)
						++nearby_towers;
				int score=tactical_building_value(sighting.type, strategy.tactics)
					+std::max(0, opponents[opponent->first].score)
					+(sighting.construction ? strategy.tactics.target_construction_bonus : 0)
					-nearby_towers*strategy.tactics.target_tower_penalty
					-distance*strategy.tactics.route_distance_weight;
				if(active && sighting.gid==tactical_mission.targetGid)
				{
					score+=strategy.tactics.retarget_margin;
					current_alive=true;
					current_score=score;
					current_x=x;
					current_y=y;
				}
				if(score>best_score)
				{
					best_score=score;
					best_kind=Tactics::MissionSiege;
					best_team=opponent->first;
					best_gid=sighting.gid;
					best_x=x;
					best_y=y;
				}
			}
		}
	}
	if(strategy.raiding.enabled)
	{
		for(std::vector<Tactics::RaidCandidate>::const_iterator candidate=
			tactics.raidCandidates().begin(); candidate!=tactics.raidCandidates().end();
			++candidate)
		{
			++offense_diagnostics.clusterCandidates;
			const int distance=route_to(candidate->x, candidate->y);
			if(distance<0) continue;
			++offense_diagnostics.viableClusters;
			int score=candidate->score
				-distance*strategy.raiding.route_distance_weight;
			// A cluster that drifted within the threat radius of the current
			// raid is the same objective moving, not a new one.
			if(active && tactical_mission.kind==Tactics::MissionRaid
			   && candidate->team==tactical_mission.targetTeam
			   && map->warpDistSquare(candidate->x, candidate->y,
				tactical_mission.targetX, tactical_mission.targetY)
				<=strategy.raiding.threat_radius*strategy.raiding.threat_radius)
			{
				score+=strategy.tactics.retarget_margin;
				if(!current_alive || score>current_score)
				{
					current_alive=true;
					current_score=score;
					current_x=candidate->x;
					current_y=candidate->y;
				}
			}
			if(score>best_score)
			{
				best_score=score;
				best_kind=Tactics::MissionRaid;
				best_team=candidate->team;
				best_gid=-1;
				best_x=candidate->x;
				best_y=candidate->y;
			}
		}
	}
	if(dwelling && current_alive)
	{
		best_kind=tactical_mission.kind;
		best_team=tactical_mission.targetTeam;
		best_gid=tactical_mission.targetGid;
		best_x=current_x;
		best_y=current_y;
		best_score=current_score;
	}
	offense_diagnostics.bestScore=best_score;
	if(best_kind!=Tactics::MissionNone)
	{
		budget.tactical_kind=best_kind;
		budget.tactical_target_team=best_team;
		budget.tactical_target_gid=best_gid;
		budget.tactical_target_x=best_x;
		budget.tactical_target_y=best_y;
		budget.tactical_candidate_score=best_score;
		budget.tactical_requested_force=std::min(cap, surplus);
		offense_diagnostics.decision=std::string(best_kind==Tactics::MissionSiege
			? "siege " : "raid ")+diagnostic_value(best_score);
		return;
	}
	if(known_unreachable && strategy.tactics.dig_out_enabled
	   && budget.attack_clearing_workers>0)
	{
		budget.tactical_dig_out_team=unreachable_team;
		offense_diagnostics.decision="opening route to sealed team "
			+diagnostic_value(unreachable_team);
		return;
	}
	offense_diagnostics.decision="no reachable target";
}

void Maxima::end_offense(Context& echo, const char* reason)
{
	std::set<int> flags(attack_flags.begin(),attack_flags.end());
	for(const auto& wave:offense_waves)flags.insert(wave.flagId);
	flags.erase(tactical_mission.flagId);
	for(int id:flags)echo.cancel_or_destroy_building(id);
	offense_waves.clear();
	if(tactical_mission.flagId>=0)
	{
		emit_telemetry(echo, "mission_finished",
			"\tkind="+std::string(Tactics::missionKindName(tactical_mission.kind))
			+"\ttarget_team="+diagnostic_value(tactical_mission.targetTeam)
			+"\ttarget_gid="+diagnostic_value(tactical_mission.targetGid)
			+"\treason="+reason
			+"\tduration="+diagnostic_value(timer-tactical_mission.startedTick));
		attack_flag_end_reasons[tactical_mission.flagId]=reason;
		echo.cancel_or_destroy_building(tactical_mission.flagId);
	}
	attack_flags.clear();
	tactical_mission.reset();
	campaign.state=CampaignIdle;
	director.invalidate();
}

void Maxima::observe_wave_delivery()
{
	if(failed_waves>=4)return;
	const auto score=[&](Tactics::WaveDelivery& delivery) {
		if(delivery.scored || delivery.launched==0 || failed_waves>=4)return;
		delivery.scored=true;
		failed_waves=delivery.arrived*100<delivery.launched*33 ? failed_waves+1 : 0;
	};
	for(const auto& wave:offense_waves)
	{
		if(wave.phase!=Tactics::WaveAdvance)continue;
		auto& registry=context.get_building_register();
		Building* flag=registry.is_building_found(wave.flagId)
			? registry.get_building(wave.flagId) : NULL;
		if(!flag)continue;
		auto inserted=wave_delivery.emplace(wave.flagId,Tactics::WaveDelivery{});
		auto& delivery=inserted.first->second;
		const int enrolled=flag->unitsWorking.size();
		if(inserted.second)delivery.launched=enrolled;
		if(delivery.scored)continue;
		int arrived=0;
		for(const Unit* warrior:flag->unitsWorking)
			if(warrior && !warrior->isDead && warrior->medical==Unit::MED_FREE
			   && context.player->map->warpDistMax(warrior->posX,warrior->posY,
				wave.targetX,wave.targetY)<=budget.tactical_siege_radius)++arrived;
		delivery.arrived=std::max(delivery.arrived,arrived);
		// Assess a spent wave once, retaining its peak simultaneous delivery.
		if(enrolled*4<=delivery.launched)score(delivery);
	}
	for(auto it=wave_delivery.begin();it!=wave_delivery.end();)
		if(std::none_of(offense_waves.begin(),offense_waves.end(),
			[&](const Tactics::Wave& wave){return wave.flagId==it->first;}))
		{
			score(it->second);
			it=wave_delivery.erase(it);
		}
		else ++it;
	if(failed_waves>=4)fall_back_to_streaming();
}

void Maxima::fall_back_to_streaming()
{
	end_offense(context,"wave_failed");
	wave_delivery.clear();
	emit_telemetry(context,"wave_fallback","\tfailed_waves=4");
}

// The existing streaming executor handles amphibious objectives and the
// permanent fallback after repeated poor wave delivery.
bool Maxima::control_offense_waves(Context& echo)
{
	if(failed_waves>=4)return false;
	if(!budget.tactics_enabled || severe_colony_emergency())
	{
		end_offense(echo,"wave_emergency");
		return true;
	}
	if(budget.tactical_kind==Tactics::MissionNone)
	{
		if(!offense_waves.empty())end_offense(echo,"no_wave_target");
		return false;
	}
	Map* map=echo.player->map;
	GradientInfo routeInfo;
	routeInfo.add_source(new Entities::Position(budget.tactical_target_x,budget.tactical_target_y));
	routeInfo.add_obstacle(new Entities::AnyResource);
	routeInfo.add_obstacle(new Entities::Water);
	Gradient& route=echo.get_gradient_manager().get_gradient(routeInfo);
	const auto& policy=strategy.assault;
	const int capacity=strategy.military.attack_unit_cap;
	int rallyX=-1,rallyY=-1,bestDistance=INT_MAX,bestSpace=-1;
	bool landHome=false;
	// Search connected ground around each food building, rather than using its
	// occupied origin. Prefer room for two cohorts' worth of standing tiles so
	// arrivals and workers can pass one another, then proximity to the objective.
	for(int id=0;id<Building::MAX_COUNT;++id)
	{
		const Building* home=echo.player->team->myBuildings[id];
		if(!home || home->type->isBuildingSite
		   || (home->type->shortTypeNum!=IntBuildingType::SWARM_BUILDING
			&& home->type->shortTypeNum!=IntBuildingType::FOOD_BUILDING))continue;
		landHome=landHome || route.get_height(map->normalizeX(home->posX),map->normalizeY(home->posY))>=0;
		const int margin=std::max(6,policy.muster_radius+2);
		const int width=home->type->width+2*margin,height=home->type->height+2*margin;
		std::vector<unsigned char> visited(width*height,0);
		std::vector<std::pair<int,int>> queue;
		const auto add=[&](int px,int py) {
			if(px<0 || py<0 || px>=width || py>=height || visited[py*width+px])return;
			visited[py*width+px]=1;
			const int x=map->normalizeX(home->posX+px-margin);
			const int y=map->normalizeY(home->posY+py-margin);
			if(rally_walkable(map,echo.player->team->me,x,y))queue.push_back(std::make_pair(px,py));
		};
		for(int y=margin-1;y<=margin+home->type->height;++y)
			for(int x=margin-1;x<=margin+home->type->width;++x)add(x,y);
		for(size_t head=0;head<queue.size();++head)
		{
			const auto point=queue[head];
			for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)add(point.first+dx,point.second+dy);
			const int x=map->normalizeX(home->posX+point.first-margin);
			const int y=map->normalizeY(home->posY+point.second-margin);
			const int distance=route.get_height(x,y);
			if(distance<0)continue;
			landHome=true;
			if(bestSpace==2*capacity && distance>=bestDistance)continue;
			const int space=rally_space(map,echo.player->team->me,x,y,policy.muster_radius,2*capacity);
			if(space<capacity || space<bestSpace || (space==bestSpace && distance>=bestDistance))continue;
			rallyX=x;rallyY=y;bestDistance=distance;bestSpace=space;
		}
	}
	if(rallyX<0 && !landHome)
	{
		if(!offense_waves.empty())end_offense(echo,"amphibious_handoff");
		return false;
	}
	if(offense_waves.empty() && tactical_mission.flagId>=0)
		end_offense(echo,"wave_handoff");

	const int minimum=std::max(1,strategy.tactics.min_force);
	const int targetX=budget.tactical_target_x,targetY=budget.tactical_target_y;
	const bool changed=tactical_mission.kind!=budget.tactical_kind
		|| tactical_mission.targetGid!=budget.tactical_target_gid
		|| tactical_mission.targetTeam!=budget.tactical_target_team
		|| tactical_mission.targetX!=targetX || tactical_mission.targetY!=targetY;
	if(changed)
	{
		tactical_mission.lastTargetHp=-1;
		tactical_mission.lastProgressTick=timer;
		tactical_mission.phaseSinceTick=timer;
	}
	tactical_mission.kind=budget.tactical_kind;
	tactical_mission.targetTeam=budget.tactical_target_team;
	tactical_mission.targetGid=budget.tactical_target_gid;
	tactical_mission.targetX=targetX;tactical_mission.targetY=targetY;
	tactical_mission.candidateScore=budget.tactical_candidate_score;
	int advancing=0;
	for(const auto& wave:offense_waves)
		if(wave.phase==Tactics::WaveAdvance)advancing+=wave.requestedForce;
	int committed=0;
	bool mustering=false,atTarget=false,assemblyFailed=false;
	for(auto it=offense_waves.begin();it!=offense_waves.end();)
	{
		Tactics::Wave& wave=*it;
		Building* flag=echo.get_building_register().is_building_found(wave.flagId)
			? echo.get_building_register().get_building(wave.flagId) : NULL;
		if(!flag && !echo.get_building_register().is_building_pending(wave.flagId))
		{
			it=offense_waves.erase(it);
			continue;
		}
		if(flag && wave.phase==Tactics::WaveMuster
		   && rally_space(map,echo.player->team->me,flag->posX,flag->posY,policy.muster_radius,capacity)<capacity
		   && rallyX>=0)
		{
			wave.rallyX=rallyX;wave.rallyY=rallyY;wave.bestArrived=0;
			echo.add_management_order(new ChangeFlagPosition(rallyX,rallyY,wave.flagId));
			++it;
			committed+=wave.requestedForce;mustering=true;
			continue;
		}
		int arrived=0;
		// Nearby warriors can join the departing wave without first squeezing
		// into the flag itself. Advancing waves retain their existing arrival test.
		const int arrivalRadius=flag ? flag->unitStayRange
			+(wave.phase==Tactics::WaveMuster ? 2 : 0) : 0;
		const int cohort=flag ? int(flag->unitsWorking.size()) : 0;
		if(flag)
			for(const Unit* warrior:flag->unitsWorking)
				if(warrior && !warrior->isDead && warrior->medical==Unit::MED_FREE
				   && (wave.phase==Tactics::WaveMuster
					? map->warpDistSquare(warrior->posX,warrior->posY,flag->posX,flag->posY)
						<=arrivalRadius*arrivalRadius
					: map->warpDistMax(warrior->posX,warrior->posY,flag->posX,flag->posY)
						<=flag->unitStayRange))++arrived;
		bool retire=false;
		if(wave.phase==Tactics::WaveMuster)
		{
			// Continue filling a rally as the deployable army grows. Its initial
			// budget is often only four warriors and must not freeze its capacity.
			const int request=std::min(strategy.military.attack_unit_cap,
				std::max(wave.requestedForce,budget.tactical_requested_force-advancing));
			if(request>wave.requestedForce)
			{
				wave.requestedForce=request;
				echo.add_management_order(new AssignWorkers(request,wave.flagId));
			}
			if(flag && Tactics::waveReady(wave,arrived,timer,policy.muster_ready_percent,
				policy.muster_stall_ticks,policy.muster_max_ticks,minimum,cohort,strategy.military.attack_unit_cap))
			{
				wave.phase=Tactics::WaveAdvance;
				wave.progressTick=timer;
				wave.requestedForce=cohort;
				wave.targetX=targetX;wave.targetY=targetY;
				echo.add_management_order(new ChangePriority(-1,wave.flagId));
				echo.add_management_order(new AssignWorkers(cohort,wave.flagId));
				echo.add_management_order(new ChangeFlagSize(budget.tactical_kind==Tactics::MissionRaid
					? budget.raid_flag_radius : budget.tactical_siege_radius,wave.flagId));
				echo.add_management_order(new ChangeFlagPosition(targetX,targetY,wave.flagId));
			}
			else if(timer-wave.startedTick>=policy.muster_max_ticks)
			{
				retire=true;assemblyFailed=true;
				if(++failed_waves>=4)
				{
					fall_back_to_streaming();
					return false;
				}
			}
			else mustering=true;
		}
		else
		{
			if(wave.targetX!=targetX || wave.targetY!=targetY)
			{
				wave.targetX=targetX;wave.targetY=targetY;
				echo.add_management_order(new ChangeFlagPosition(targetX,targetY,wave.flagId));
				wave.progressTick=timer;
			}
			// Reduce recruitment after departures. The engine can still fill a
			// vacancy between reviews; normal-priority rallies get first choice.
			if(flag && cohort<wave.requestedForce)
			{
				wave.requestedForce=cohort;
				echo.add_management_order(new AssignWorkers(cohort,wave.flagId));
			}
			atTarget=atTarget || (arrived>0 && flag
				&& map->warpDistMax(flag->posX,flag->posY,targetX,targetY)
					<=budget.tactical_siege_radius);
			if(cohort>0)wave.progressTick=timer;
			retire=flag && cohort==0 && timer-wave.progressTick>=policy.muster_stall_ticks;
		}
		if(retire)
		{
			echo.cancel_or_destroy_building(wave.flagId);
			it=offense_waves.erase(it);
			continue;
		}
		committed+=wave.requestedForce;
		++it;
	}
	const int available=budget.tactical_requested_force-committed;
	if(rallyX>=0 && !mustering && !assemblyFailed && int(offense_waves.size())<policy.max_waves
	   && available>=minimum)
	{
		Tactics::Wave wave;
		wave.requestedForce=std::min(strategy.military.attack_unit_cap,available);
		wave.startedTick=wave.progressTick=timer;
		wave.rallyX=rallyX;wave.rallyY=rallyY;
		wave.targetX=targetX;wave.targetY=targetY;
		BuildingOrder* order=new BuildingOrder(IntBuildingType::WAR_FLAG,wave.requestedForce);
		order->add_constraint(new Construction::SinglePosition(rallyX,rallyY));
		wave.flagId=echo.add_building_order(order);
		echo.add_management_order(new ChangeFlagMinimumLevel(budget.tactical_flag_level,wave.flagId));
		echo.add_management_order(new ChangeFlagSize(policy.muster_radius,wave.flagId));
		echo.add_management_order(new ChangePriority(0,wave.flagId));
		ManagementOrder* deleted=new Notify(RuntimeEvent(RuntimeEvent::AttackFinished,wave.flagId));
		deleted->add_condition(new BuildingDestroyed(wave.flagId));
		echo.add_management_order(deleted);
		offense_waves.push_back(wave);
		attack_flag_started_ticks[wave.flagId]=timer;
	}
	attack_flags.clear();
	for(const auto& wave:offense_waves)attack_flags.push_back(wave.flagId);
	tactical_mission.flagId=offense_waves.empty() ? -1 : offense_waves.front().flagId;
	tactical_mission.phase=offense_waves.empty() ? Tactics::PhaseIdle : Tactics::PhaseEngage;
	tactical_mission.requestedForce=budget.tactical_requested_force;
	campaign.state=offense_waves.empty() ? CampaignIdle : CampaignActive;
	campaign.target_team=budget.tactical_target_team;
	// Travel and assembly consume no siege stall allowance.
	if(!atTarget)tactical_mission.lastProgressTick=timer;
	if(atTarget && tactical_mission.kind==Tactics::MissionSiege)
	{
		const int team=tactical_mission.targetTeam;
		const int local=Building::GIDtoID(tactical_mission.targetGid);
		const Building* objective=team>=0 && team<Team::MAX_COUNT
            && echo.player->game->teams[team] && local>=0 && local<Building::MAX_COUNT
            ? echo.player->game->teams[team]->myBuildings[local] : NULL;
		if(objective && building_currently_visible(echo.player,objective))
		{
			if(tactical_mission.lastTargetHp<0 || objective->hp<tactical_mission.lastTargetHp)
				tactical_mission.lastProgressTick=timer;
			tactical_mission.lastTargetHp=objective->hp;
			if(budget.tactical_quarantine_enabled
			   && timer-tactical_mission.lastProgressTick>=budget.tactical_stall_ticks)
			{
				attack_target_quarantine_until[tactical_mission.targetGid]=timer+budget.tactical_quarantine_ticks;
				director.invalidate();
			}
		}
	}
	return true;
}

/// Keep the offensive flag on the planned target. Runs every review tick.
void Maxima::control_offense(Context& echo)
{
	if(control_offense_waves(echo))
		return;
	auto& registry=echo.get_building_register();
	const auto live=[&](int id) {
		return registry.is_building_found(id) || registry.is_building_pending(id);
	};
	attack_flags.erase(std::remove_if(attack_flags.begin(),attack_flags.end(),
		[&](int id){return !live(id);}),attack_flags.end());
	if(tactical_mission.flagId>=0 && live(tactical_mission.flagId)
	   && std::find(attack_flags.begin(),attack_flags.end(),tactical_mission.flagId)==attack_flags.end())
		attack_flags.insert(attack_flags.begin(),tactical_mission.flagId);
	if(!attack_flags.empty() && tactical_mission.flagId!=attack_flags.front())
	{
		tactical_mission.flagId=attack_flags.front();
		if(tactical_mission.targetGid>=0)
			attack_flag_targets[tactical_mission.flagId]=tactical_mission.targetGid;
	}

	if(!budget.tactics_enabled || severe_colony_emergency())
	{
		end_offense(echo, budget.tactics_enabled
			? "colony_emergency" : "tactics_disabled");
		return;
	}
	const bool active=tactical_mission.flagId>=0;
	if(active && !echo.get_building_register().is_building_found(tactical_mission.flagId)
	   && !echo.get_building_register().is_building_pending(tactical_mission.flagId))
	{
		// The engine already removed the flag; AttackFinished did the bookkeeping.
		tactical_mission.reset();
		campaign.state=CampaignIdle;
	}
	if(budget.tactical_kind==Tactics::MissionNone)
	{
		if(tactical_mission.flagId>=0)
			end_offense(echo, "no_target");
		else if(!is_digging_out && budget.tactical_dig_out_team>=0)
		{
			target=budget.tactical_dig_out_team;
			dig_out_enemy(echo);
		}
		return;
	}
	const bool same_target=tactical_mission.flagId>=0
		&& tactical_mission.kind==budget.tactical_kind
		&& tactical_mission.targetTeam==budget.tactical_target_team
		&& tactical_mission.targetGid==budget.tactical_target_gid
		&& tactical_mission.targetX==budget.tactical_target_x
		&& tactical_mission.targetY==budget.tactical_target_y;
	const bool same_raid_moved=!same_target && tactical_mission.flagId>=0
		&& tactical_mission.kind==Tactics::MissionRaid
		&& budget.tactical_kind==Tactics::MissionRaid
		&& tactical_mission.targetTeam==budget.tactical_target_team
		&& echo.player->map->warpDistSquare(tactical_mission.targetX,
			tactical_mission.targetY, budget.tactical_target_x, budget.tactical_target_y)
			<=budget.raid_flag_radius*budget.raid_flag_radius*16;
	const int radius=budget.tactical_kind==Tactics::MissionRaid
		? budget.raid_flag_radius : budget.tactical_siege_radius;
	const bool new_mission=tactical_mission.flagId<0;
	const int capacity=std::max(1,strategy.military.attack_unit_cap);
	const int requested=std::max(0,budget.tactical_requested_force);
	const int wanted=std::max(1,(requested+capacity-1)/capacity);
	const bool reallocate=requested!=tactical_mission.requestedForce
		|| int(attack_flags.size())!=wanted;
	while(int(attack_flags.size())>wanted)
	{
		echo.cancel_or_destroy_building(attack_flags.back());
		attack_flags.pop_back();
	}
	const int retained=attack_flags.size();
	while(int(attack_flags.size())<wanted)
	{
		const int force=std::min(capacity,requested-int(attack_flags.size())*capacity);
		BuildingOrder* order=new BuildingOrder(IntBuildingType::WAR_FLAG,force);
		// Creation cannot overlap another flag. Use the nearest free anchor;
		// every flag's attack radius still covers the planned objective.
		if(attack_flags.empty())
			order->add_constraint(new Construction::SinglePosition(
				budget.tactical_target_x,budget.tactical_target_y));
		else
		{
			GradientInfo objective;
			objective.add_source(new Entities::Position(budget.tactical_target_x,budget.tactical_target_y));
			order->add_constraint(new Construction::MaximumDistance(objective,std::max(1,radius/2)));
			order->add_constraint(new Construction::MinimizedDistance(objective,1));
		}
		const int flag=echo.add_building_order(order);
		echo.add_management_order(new ChangeFlagMinimumLevel(budget.tactical_flag_level,flag));
		echo.add_management_order(new ChangeFlagSize(radius,flag));
		ManagementOrder* deleted=new Notify(RuntimeEvent(RuntimeEvent::AttackFinished,flag));
		deleted->add_condition(new BuildingDestroyed(flag));
		echo.add_management_order(deleted);
		attack_flags.push_back(flag);
		attack_flag_started_ticks[flag]=timer;
	}
	for(int i=0;i<retained;++i)
	{
		const int flag=attack_flags[i];
		const int force=std::min(capacity,requested-i*capacity);
		if(reallocate || (registry.is_building_found(flag) && registry.get_assigned(flag)!=force))
			echo.add_management_order(new AssignWorkers(force,flag));
		if(!same_target)
		{
			echo.add_management_order(new ChangeFlagSize(radius,flag));
			echo.add_management_order(new ChangeFlagPosition(budget.tactical_target_x,
				budget.tactical_target_y,flag));
		}
	}
	if(same_raid_moved)
	{
		tactical_mission.targetX=budget.tactical_target_x;
		tactical_mission.targetY=budget.tactical_target_y;
		tactical_mission.candidateScore=budget.tactical_candidate_score;
		tactical_mission.requestedForce=requested;
		return;
	}
	if(new_mission)
	{
		tactical_mission.reset();
		tactical_mission.flagId=attack_flags.front();
		tactical_mission.phase=Tactics::PhaseEngage;
		tactical_mission.startedTick=timer;
	}
	else if(same_target)
	{
		// The request follows the training surplus, releasing warriors to the
		// barracks or admitting newly free ones.
		tactical_mission.requestedForce=requested;
		// Progress watch: a visible target that stops losing hit points for
		// the stall period is quarantined so the next plan looks elsewhere.
		if(tactical_mission.kind==Tactics::MissionSiege)
		{
			const int team=tactical_mission.targetTeam;
			const int local=Building::GIDtoID(tactical_mission.targetGid);
			Building* building=team>=0 && team<Team::MAX_COUNT
				&& echo.player->game->teams[team] && local>=0 && local<Building::MAX_COUNT
				? echo.player->game->teams[team]->myBuildings[local] : NULL;
			if(building && building->gid==tactical_mission.targetGid
			   && building_currently_visible(echo.player, building)
			   && (tactical_mission.lastTargetHp<0
				|| building->hp<tactical_mission.lastTargetHp))
			{
				tactical_mission.lastTargetHp=building->hp;
				tactical_mission.lastProgressTick=timer;
				campaign.last_progress_tick=timer;
			}
			int enrolled=0;
			for(int id:attack_flags)enrolled+=registry.get_enrolled(id);
			if(enrolled>0 && budget.tactical_quarantine_enabled
			   && timer-tactical_mission.lastProgressTick
				>=budget.tactical_stall_ticks)
			{
				attack_target_quarantine_until[tactical_mission.targetGid]=
					timer+budget.tactical_quarantine_ticks;
				emit_telemetry(echo, "target_quarantined",
					"\tbuilding="+diagnostic_value(tactical_mission.targetGid)
					+"\treason=stalled\tcooldown="
					+diagnostic_value(budget.tactical_quarantine_ticks));
				tactical_mission.lastProgressTick=timer;
				director.invalidate();
			}
		}
		return;
	}
	else
	{
		// Recon drops a remembered building only after seeing its footprint
		// empty, so a vanished siege target counts as destroyed.
		if(tactical_mission.kind==Tactics::MissionSiege && tactical_mission.targetGid>=0)
		{
			const Recon::OpponentIntel* intel=
				reconnaissance.opponent(tactical_mission.targetTeam);
			if(intel && intel->buildings.count(tactical_mission.targetGid)==0)
			{
				++campaign.buildings_destroyed;
				campaign.last_progress_tick=timer;
			}
		}
		emit_telemetry(echo, "mission_retargeted",
			"\tkind="+std::string(Tactics::missionKindName(budget.tactical_kind))
			+"\ttarget_team="+diagnostic_value(budget.tactical_target_team)
			+"\ttarget_gid="+diagnostic_value(budget.tactical_target_gid)
			+"\tx="+diagnostic_value(budget.tactical_target_x)
			+"\ty="+diagnostic_value(budget.tactical_target_y));
	}
	const int flag=tactical_mission.flagId;
	tactical_mission.kind=budget.tactical_kind;
	tactical_mission.targetTeam=budget.tactical_target_team;
	tactical_mission.targetGid=budget.tactical_target_gid;
	tactical_mission.targetX=budget.tactical_target_x;
	tactical_mission.targetY=budget.tactical_target_y;
	tactical_mission.requestedForce=budget.tactical_requested_force;
	tactical_mission.candidateScore=budget.tactical_candidate_score;
	tactical_mission.lastTargetHp=-1;
	tactical_mission.lastProgressTick=timer;
	tactical_mission.phaseSinceTick=timer;
	tactical_mission.phase=Tactics::PhaseEngage;
	if(tactical_mission.targetGid>=0)
		attack_flag_targets[flag]=tactical_mission.targetGid;
	else
		attack_flag_targets.erase(flag);
	target=tactical_mission.targetTeam;
	if(tactical_mission.kind==Tactics::MissionSiege)
	{
		if(campaign.target_team!=tactical_mission.targetTeam)
		{
			campaign.buildings_destroyed=0;
			campaign.started_tick=timer;
		}
		campaign.state=CampaignActive;
		campaign.target_team=tactical_mission.targetTeam;
		campaign.last_progress_tick=timer;
	}
	else
		campaign.state=CampaignIdle;
	emit_telemetry(echo, "mission_selected",
		"\tkind="+std::string(Tactics::missionKindName(tactical_mission.kind))
		+"\ttarget_team="+diagnostic_value(tactical_mission.targetTeam)
		+"\ttarget_gid="+diagnostic_value(tactical_mission.targetGid)
		+"\tflag="+diagnostic_value(flag)
		+"\trequested="+diagnostic_value(tactical_mission.requestedForce)
		+"\tx="+diagnostic_value(tactical_mission.targetX)
		+"\ty="+diagnostic_value(tactical_mission.targetY)
		+"\tscore="+diagnostic_value(tactical_mission.candidateScore));
	director.invalidate();
}


void Maxima::choose_enemy_target(Context& echo)
{
	telemetry.count(AITrace::AI7::Maxima_choose_enemy_target_calls);
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
	const bool current_valid=target>=0 && target<Team::MAX_COUNT
		&& echo.player->game->teams[target]
		&& echo.player->game->teams[target]->isAlive
		&& opponents[target].score!=INT_MIN;
	const bool campaign_committed=!attack_flags.empty()
		|| campaign.state==CampaignActive || campaign.state==CampaignPaused;
	const bool materially_better=current_valid && best_target!=-1
		&& best_target!=target
		&& best_score>=opponents[target].score+budget.target_switch_margin;
	if(!current_valid || (!campaign_committed && materially_better))
	{
		const int previous=target;
		target=best_target;
		if(target!=previous)
			emit_telemetry(echo, "target_changed",
				"\tprevious="+boost::lexical_cast<std::string>(previous)
				+"\ttarget="+boost::lexical_cast<std::string>(target)
				+"\tscore="+boost::lexical_cast<std::string>(best_score));
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
	emit_telemetry(echo, "dig_out_started",
		"\ttarget_team="+boost::lexical_cast<std::string>(target)
		+"\ttarget_building="+boost::lexical_cast<std::string>(building)
		+"\tflags="+boost::lexical_cast<std::string>(flags_created)
		+"\tworkers_per_flag="+boost::lexical_cast<std::string>(
			budget.attack_clearing_workers));

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
	emit_telemetry(echo, "preemptive_defense_cleared",
		"\ttiles="+boost::lexical_cast<std::string>(removed));
}


void Maxima::update_preemptive_defense(Context& echo)
{
	if(!budget.preemptive_defense_active)
	{
		clear_preemptive_defense(echo);
		last_preemptive_defense_tick=-1000000;
		last_preemptive_effective_zone_max=-1;
		last_preemptive_amphibious_active=false;
		if(echo.player && echo.player->map)
		{
		}
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
	if(!additions.empty() || !removals.empty())
		emit_telemetry(echo, "preemptive_defense_updated",
			"\tchokes="+boost::lexical_cast<std::string>(plan.selectedCount)
			+"\tdesired_tiles="+boost::lexical_cast<std::string>(desired.size())
			+"\tadded="+boost::lexical_cast<std::string>(additions.size())
			+"\tremoved="+boost::lexical_cast<std::string>(removals.size()));
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
		   && buildingGID[echo.player->map->normalizeX(building->posX) * h
		       + echo.player->map->normalizeY(building->posY)] == NOGBID)
		{
			int nx = (building->posX - building->type->decLeft + w) %w;
			int ny = (building->posY - building->type->decTop + h) %h;
			// Building origins can cross the toroidal seam during upgrades.
			// Normalize before indexing, as we already do for units.
			buildingGID[echo.player->map->normalizeX(building->posX) * h
			    + echo.player->map->normalizeY(building->posY)] = building->gid;
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
	int moved_flags=0;
	int deadband_flags=0;
	int locally_retained_flags=0;
	int reassigned_flags=0;
	int destroyed_flags=0;
	int created_flags=0;
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
				moved_flags+=1;
			}
			else if(min_dist>0)
				deadband_flags+=1;
			if(flag->unitStayRange!=RADIUS)
				echo.add_management_order(new ChangeFlagSize(RADIUS, id_flag));
			if(min_enemy != echo.get_building_register().get_assigned(id_flag))
			{
				ManagementOrder* mo_assign=new AssignWorkers(min_enemy, id_flag);
				echo.add_management_order(mo_assign);
				reassigned_flags+=1;
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
					reassigned_flags+=1;
				}
				locally_retained_flags+=1;
			}
			else
			{
				echo.add_management_order(new DestroyBuilding(*i));
				destroyed_flags+=1;
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
		created_flags+=1;

		ManagementOrder* mo_minimum=new ChangeFlagMinimumLevel(1, id_flag);
		echo.add_management_order(mo_minimum);
		ManagementOrder* mo_completion=new ChangeFlagSize(
			budget.reactive_defense_flag_radius, id_flag);
		echo.add_management_order(mo_completion);

		ManagementOrder* mo_destroyed=new Notify(RuntimeEvent(RuntimeEvent::GuardFlagDeleted, id_flag));
		mo_destroyed->add_condition(new BuildingDestroyed(id_flag));
		echo.add_management_order(mo_destroyed);
	}
	if(moved_flags || deadband_flags || locally_retained_flags
	   || reassigned_flags || destroyed_flags || created_flags)
		emit_telemetry(echo, "reactive_defense_updated",
			"\tmoved="+boost::lexical_cast<std::string>(moved_flags)
			+"\tdeadband="+boost::lexical_cast<std::string>(deadband_flags)
			+"\tretained="+boost::lexical_cast<std::string>(locally_retained_flags)
			+"\treassigned="+boost::lexical_cast<std::string>(reassigned_flags)
			+"\tdestroyed="+boost::lexical_cast<std::string>(destroyed_flags)
			+"\tcreated="+boost::lexical_cast<std::string>(created_flags));

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
	const bool following_offense=tactical_mission.flagId>=0
		&& (tactical_mission.kind==Tactics::MissionRaid
			|| tactical_mission.kind==Tactics::MissionSiege);
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
	const int trained_explorers=echo.player->team->stats.getLatestStat()
		->upgradeStatePerType[EXPLORER][MAGIC_ATTACK_GROUND][3];
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
		emit_telemetry(echo, "explorer_strike_launched",
			"\tflag="+boost::lexical_cast<std::string>(id_flag)
			+"\ttarget_team="+boost::lexical_cast<std::string>(strike_target)
			+"\ttarget_score="+boost::lexical_cast<std::string>(groupInfo.get<0>())
			+"\tx="+boost::lexical_cast<std::string>(groupInfo.get<1>())
			+"\ty="+boost::lexical_cast<std::string>(groupInfo.get<2>())
			+"\tassigned="+boost::lexical_cast<std::string>(
				budget.explorer_campaign_units_per_flag)
			+"\ttrained_explorers="+boost::lexical_cast<std::string>(trained_explorers));

		ManagementOrder* mo_destroyed=new Notify(
			RuntimeEvent(RuntimeEvent::ExplorerAttackFlagDeleted, id_flag));
		mo_destroyed->add_condition(new BuildingDestroyed(id_flag));
		echo.add_management_order(mo_destroyed);
	}
}



}
