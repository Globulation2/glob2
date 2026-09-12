/* Maxima farming and clearing policy. */

#include "AIMaxima.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "Unit.h"
#include "boost/lexical_cast.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <deque>
#include <map>
#include <queue>
#include <tuple>
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

	// Shared spatial scope for farm protection and renewable wood firebreaks.
	// Use the same allied physical anchors and wrapped inclusive distance in
	// both policies; zero keeps the optimizer's unlimited control setting.
	std::vector<Uint8> farm_management_area(Context& echo, int radius)
	{
		Map* map=echo.player->map;
		const int w=map->getW(), h=map->getH();
		if(radius<=0)return std::vector<Uint8>(w*h,1);
		std::vector<Uint8> nearby(w*h,0);
		for(int team=0;team<Team::MAX_COUNT;++team)
		{
			if(!((echo.player->team->allies|echo.player->team->me)&(Uint32(1)<<team)))continue;
			Team* ally=echo.player->game->teams[team];
			if(!ally)continue;
			for(int b=0;b<Building::MAX_COUNT;++b)
			{
				Building* building=ally->myBuildings[b];
				if(!building || building->type->isVirtual)continue;
				for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx)
					if(dx*dx+dy*dy<=radius*radius)
						nearby[map->normalizeY(building->posY+dy)*w
							+map->normalizeX(building->posX+dx)]=1;
			}
		}
		return nearby;
	}

	std::vector<Uint8> shoreline_backing(Map* map)
	{
		const int w=map->getW(), h=map->getH();
		std::vector<Uint8> backing(w*h, 0);
		std::vector<int> queue;
		queue.reserve(w*h);
		for(int y=0; y<h; ++y)
			for(int x=0; x<w; ++x)
				if(map->isWater(x, y))
				{
					backing[y*w+x]=1;
					queue.push_back(y*w+x);
				}
		// A beach can be several sand tiles wide. Follow touching sand from
		// actual water, including blended tiles and diagonals, but never grass.
		// This excludes isolated inland sand without depending on crop density.
		for(size_t head=0; head<queue.size(); ++head)
		{
			const int x=queue[head]%w, y=queue[head]/w;
			for(int dy=-1; dy<=1; ++dy)
				for(int dx=-1; dx<=1; ++dx)
				{
					if(!dx && !dy) continue;
					const int nx=(x+dx+w)%w, ny=(y+dy+h)%h;
					const int next=ny*w+nx;
					if(!backing[next] && map->hasSand(nx, ny))
					{
						backing[next]=1;
						queue.push_back(next);
					}
				}
		}
		return backing;
	}

	bool touches_shoreline_backing(const std::vector<Uint8>& backing,
		int w, int h, int x, int y, bool cardinal_only=false)
	{
		for(int dy=-1; dy<=1; ++dy)
			for(int dx=-1; dx<=1; ++dx)
			{
				if((!dx && !dy) || (cardinal_only && std::abs(dx)+std::abs(dy)!=1))
					continue;
				if(backing[((y+dy+h)%h)*w+(x+dx+w)%w]) return true;
			}
		return false;
	}

	bool is_empty_growth_cell(const Tile& cell)
	{
		return cell.terrain<16 && cell.canResourcesGrow
			&& cell.resource.type==NO_RES_TYPE && cell.building==NOGBID;
	}

	std::vector<Uint8> waterward_exposure(Map* map, Gradient& water_gradient)
	{
		const int w=map->getW(), h=map->getH();
		std::vector<Uint8> exposed(w*h, 0);
		std::vector<int> queue;
		queue.reserve(w*h);
		for(int y=0; y<h; ++y)
			for(int x=0; x<w; ++x)
				if(map->isWater(x, y))
				{
					exposed[y*w+x]=1;
					queue.push_back(y*w+x);
				}
		// Reverse a strictly waterward route through resource-free terrain.
		// A harvested hole behind the live farm cannot borrow the water
		// gradient through those crops and masquerade as its outer boundary.
		for(size_t head=0; head<queue.size(); ++head)
		{
			const int x=queue[head]%w, y=queue[head]/w;
			const int distance=water_gradient.get_height(x, y);
			for(int dy=-1; dy<=1; ++dy)
				for(int dx=-1; dx<=1; ++dx)
				{
					if(!dx && !dy) continue;
					const int nx=(x+dx+w)%w, ny=(y+dy+h)%h;
					const int next=ny*w+nx;
					const Tile& cell=map->getTile(nx, ny);
					if(!exposed[next] && cell.resource.type==NO_RES_TYPE
					   && cell.building==NOGBID
					   && water_gradient.get_height(nx, ny)>distance)
					{
						exposed[next]=1;
						queue.push_back(next);
					}
				}
		}
		return exposed;
	}

	enum ClearingCampaignKind
	{
		///Saved games may still hold a flag from the retired barrier policy.
		RetiredGateClearingCampaign,
		LegacyBoundaryClearingCampaign,
		WoodClearingCampaign
	};

	ClearingCampaignKind clearing_campaign_kind(int initial_wood)
	{
		if(initial_wood==0) return RetiredGateClearingCampaign;
		if(initial_wood==1) return LegacyBoundaryClearingCampaign;
		return WoodClearingCampaign;
	}

	///Semantic roles a single resource tile can play in the shared wheat/wood
	///layout. Classification is descriptive; policy priority is applied later.
	struct FarmTileClassification
	{
		bool frontier;
		bool edge;
		bool bootstrap;
		bool edge_candidate;
		bool interior_seed;
		FarmTileClassification(): frontier(false), edge(false), bootstrap(false),
			edge_candidate(false),
			interior_seed(false) {}

		bool protected_tile() const
		{
			return frontier || edge || interior_seed || bootstrap;
		}
	};

	FarmTileClassification classify_farm_tile(MapInfo& mi, Map* map,
		Gradient& water_gradient, const Farming::ExactFertilityCache& fertility_cache,
		const std::vector<Uint8>& water_exposure,
		bool shoreline_backed, int x, int y, int resource_type, Uint32 minimum_fertility)
	{
		FarmTileClassification pattern;
		const int w=map->getW();
		const int h=map->getH();
		const int index=y*w+x;
		const Tile& cell=map->getTile(x, y);
		const bool seed_lattice=Farming::isInteriorSeed(x, y);
		const bool expansion_lattice=Farming::isExpansionCell(x, y);
		const Uint32 fertility=fertility_cache.at(x, y);
		const bool resource=mi.is_resource(x, y, resource_type);
		// A coastal wheat wall must cross local fertility dips. Only the
		// immediate frontier of a live crop qualifies below, so this cannot
		// reserve unrelated empty coast. Keep the exemption after growth too:
		// protecting an empty tile then exposing its new wheat would defeat it.
		const bool fertile=fertility>=minimum_fertility
			|| (resource_type==CORN && shoreline_backed);
		if(is_empty_growth_cell(cell)
		   && fertile)
		{
			bool adjacent_resource=false;
			for(int dy=-1; dy<=1; ++dy)
				for(int dx=-1; dx<=1; ++dx)
				{
					if(!dx && !dy) continue;
					if(mi.is_resource(x+dx, y+dy, resource_type))
						adjacent_resource=true;
				}
			// Protection follows the expansion lattice only. A shoreline run
			// must stay porous by construction; it is never a sealed contour.
			pattern.frontier=adjacent_resource && expansion_lattice;
		}
		if(resource && fertile)
		{
			for(int dy=-1; dy<=1; ++dy)
				for(int dx=-1; dx<=1; ++dx)
				{
					if(!dx && !dy) continue;
					const Tile& neighbor=map->getTile(x+dx, y+dy);
					const bool eligible=is_empty_growth_cell(neighbor)
						&& fertility_cache.at(x+dx, y+dy)>=minimum_fertility;
					pattern.edge_candidate=pattern.edge_candidate || eligible;
				}
			pattern.edge=pattern.edge_candidate && expansion_lattice;
			pattern.interior_seed=seed_lattice && !pattern.edge_candidate;
			if(!seed_lattice)
			{
				bool nearby_lattice_resource=false;
				int local_anchor=index;
				for(int dy=-1; dy<=1; ++dy)
					for(int dx=-1; dx<=1; ++dx)
					{
						const int nx=(x+dx+w)%w;
						const int ny=(y+dy+h)%h;
						if(!mi.is_resource(nx, ny, resource_type)) continue;
						nearby_lattice_resource=nearby_lattice_resource
							|| Farming::isInteriorSeed(nx, ny);
						local_anchor=std::min(local_anchor, ny*w+nx);
					}
				pattern.bootstrap=!nearby_lattice_resource && local_anchor==index;
			}
		}
		return pattern;
	}
}

///Complete desired output of one farming-policy evaluation. Nothing in the
///classification stage mutates map areas; only apply_farming_protection does.
struct Maxima::FarmProtectionPlan
{
	std::vector<Uint8> forbidden;
	std::vector<Uint8> protected_wheat;
	int protected_seeds;
	int protected_frontier;
	int protected_wheat_edges;
	int protected_wheat_bootstraps;
	int protected_wood_edges;
	int protected_wood_bootstraps;
	int protected_interior_seeds;
	int blocked_directions;
	Uint64 expected_capacity;
	int wood_pressure;
	Uint32 wood_fertility;

	explicit FarmProtectionPlan(int size): forbidden(size, 0),
		protected_wheat(size, 0), protected_seeds(0),
		protected_frontier(0), protected_wheat_edges(0),
		protected_wheat_bootstraps(0), protected_wood_edges(0),
		protected_wood_bootstraps(0), protected_interior_seeds(0),
		blocked_directions(0), expected_capacity(0), wood_pressure(0),
		wood_fertility(0) {}
};

void Maxima::record_construction_space_failure()
{
	if(recent_construction_failures
		<strategy.farming.proactive_failure_threshold)
		++recent_construction_failures;
	last_construction_failure_tick=timer;
	opening_space_constrained=true;
	director.invalidate();
}

struct Maxima::WoodClearingTarget
{
	int x=-1;
	int y=-1;
	int wood=0;
	int maintenance_wood=0;
};

Maxima::WoodClearingTarget Maxima::select_wood_clearing_target(Context& echo) const
{
	MapInfo mi(echo);
	Map* map=echo.player->map;
	const int w=map->getW();
	AIMaximaRuntime::Gradients::GradientInfo settlement_info;
	settlement_info.add_source(new Entities::AnyTeamBuilding(
		echo.player->team->teamNumber, CompletedBuildings));
	Gradient& settlement=echo.get_gradient_manager().get_gradient(settlement_info);
	WoodClearingTarget best;
	int best_score=INT_MIN;
	for(int x=0; x<mi.get_width(); ++x)
	{
		for(int y=0; y<mi.get_height(); ++y)
		{
			const int index=y*w+x;
			const bool owned_maintenance=index<int(
				maintenance_circulation_mask.size())
				&& maintenance_circulation_mask[index];
			if(!mi.is_discovered(x, y)
				|| (mi.is_clearing_area(x, y) && !owned_maintenance)
				|| !mi.is_grass(x, y))
				continue;
			const int distance=settlement.get_height(x, y);
			if(distance<0 || distance>14)
				continue;
			int wood=0;
			int maintenance_wood=0;
			for(int dx=-4; dx<=4; ++dx)
			{
				for(int dy=-4; dy<=4; ++dy)
				{
					if(mi.is_resource(x+dx, y+dy, WOOD))
					{
						const int resource_index=((y+dy+map->getH())
							%map->getH())*w+((x+dx+w)%w);
						wood+=1;
						if(resource_index<int(maintenance_circulation_mask.size())
						   && maintenance_circulation_mask[resource_index])
							maintenance_wood+=1;
					}
				}
			}
			if(wood<2)
				continue;
			const int score=wood*20+maintenance_wood*80-distance*2;
			if(score>best_score)
			{
				best_score=score;
				best.x=x;
				best.y=y;
				best.wood=wood;
				best.maintenance_wood=maintenance_wood;
			}
		}
	}
	return best;
}

void Maxima::retire_clearing_campaign(Context& echo, const char* reason,
	const std::string& details)
{
	echo.add_management_order(new DestroyBuilding(proactive_clearing_flag));
	emit_telemetry(echo, "land_clearing_finished",
		"\tflag="+boost::lexical_cast<std::string>(proactive_clearing_flag)
		+details+"\treason="+reason);
	proactive_clearing_flag=-1;
}

bool Maxima::continue_clearing_campaign(Context& echo)
{
	MapInfo mi(echo);
	if(proactive_clearing_flag!=-1)
	{
		if(echo.get_building_register().is_building_found(proactive_clearing_flag))
		{
			Building* flag=echo.get_building_register().get_building(proactive_clearing_flag);
			const ClearingCampaignKind campaign=
				clearing_campaign_kind(proactive_clearing_initial_wood);
			// A saved game can still carry a gate flag from the retired barrier
			// policy. Retire it rather than resume a policy that no longer exists.
			if(campaign==RetiredGateClearingCampaign)
			{
				retire_clearing_campaign(echo, "retired_gate_campaign");
				farming_urgent=false;
				director.invalidate();
				return true;
			}

			// Version 89 used initial_wood=1 for wheat/wood boundary conversion.
			// Retire such a flag when loading an older game instead of resuming it.
			if(campaign==LegacyBoundaryClearingCampaign)
			{
				retire_clearing_campaign(echo, "retired_boundary_conversion");
				last_proactive_clearing_tick=timer;
				return true;
			}

			bool clearing_resources[BASIC_COUNT]={false};
			clearing_resources[WOOD]=true;
			echo.push_order(std::shared_ptr<Order>(new OrderModifyClearingFlag(
				flag->gid, clearing_resources)));

			int nearby_wood=0;
			for(int dx=-4; dx<=4; ++dx)
				for(int dy=-4; dy<=4; ++dy)
					if(mi.is_resource(flag->posX+dx, flag->posY+dy, WOOD))
						nearby_wood+=1;
			const bool clearing_quota_met=
				proactive_clearing_initial_wood-nearby_wood
					>=budget.farming_clearing_quota;
			const bool clearing_timed_out=
				timer-proactive_clearing_started_tick
					>=budget.farming_clearing_duration;
			if(budget.recovery_active || nearby_wood<=1 || clearing_quota_met
				|| clearing_timed_out)
			{
				const char* reason=budget.recovery_active ? "starvation"
					: nearby_wood<=1 ? "cleared" : clearing_quota_met ? "quota" : "timeout";
				retire_clearing_campaign(echo, reason,
					"\twood_remaining="+boost::lexical_cast<std::string>(nearby_wood));
				last_proactive_clearing_tick=timer;
				recent_construction_failures=0;
				return true;
			}
			echo.add_management_order(new AssignWorkers(
				strategy.staffing.clearing_workers, proactive_clearing_flag));
			return true;
		}
		if(echo.get_building_register().is_building_pending(proactive_clearing_flag))
			return true;
		proactive_clearing_flag=-1;
		last_proactive_clearing_tick=timer;
	}

	return false;
}

void Maxima::manage_land_clearing(Context& echo)
{
	if(!budget.farming_enabled)
	{
		if(proactive_clearing_flag!=-1
		   && (echo.get_building_register().is_building_found(
				proactive_clearing_flag)
			||echo.get_building_register().is_building_pending(
				proactive_clearing_flag)))
			echo.add_management_order(new DestroyBuilding(proactive_clearing_flag));
		proactive_clearing_flag=-1;
		farming_urgent=false;
		return;
	}
	MapInfo mi(echo);
	Map* map=echo.player->map;
	const int w=map->getW();
	TeamStat* stat=echo.player->team->stats.getLatestStat();
	if(continue_clearing_campaign(echo)) return;

	int maintenance_wood_tiles=0;
	for(int index=0; index<w*map->getH(); ++index)
		if(index<int(maintenance_circulation_mask.size())
		   && maintenance_circulation_mask[index]
		   && mi.is_resource(index%w, index/w, WOOD))
			maintenance_wood_tiles+=1;
	const bool maintenance_clearing_allowed=
		budget.farming_maintenance_clearing_enabled
		&& maintenance_wood_tiles>0
		&& timer>=strategy.farming.proactive_start_tick && !budget.recovery_active
		&& timer-last_proactive_clearing_tick>=budget.farming_clearing_cooldown
		&& stat->numberUnitPerType[WORKER]
			>=budget.farming_min_workers_for_clearing;
	if(!(budget.farming_proactive_clearing_enabled
		&& budget.farming_allow_proactive_clearing)
	   && !maintenance_clearing_allowed)
		return;
	// Clearing inflicts 10 HP per harvested resource. Use one worker and stop
	// after a bounded gain; unrelated injured units must not disable maintenance.
	if(stat->numberUnitPerType[WORKER]<budget.farming_min_workers_for_clearing)
		return;

	const WoodClearingTarget target=select_wood_clearing_target(echo);
	const int best_x=target.x;
	const int best_y=target.y;
	const int best_wood=target.wood;
	const int best_maintenance_wood=target.maintenance_wood;
	if(best_x==-1)
		return;

	const int clearing_workers=strategy.staffing.clearing_workers;
	// Start unstaffed so the WOOD-only selector is installed before a worker can
	// touch an overlapping wheat farm.
	BuildingOrder* flag_order=new BuildingOrder(
		IntBuildingType::CLEARING_FLAG, 0);
	flag_order->add_constraint(new Construction::SinglePosition(best_x, best_y));
	proactive_clearing_flag=echo.add_building_order(flag_order);
	proactive_clearing_started_tick=timer;
	proactive_clearing_initial_wood=best_wood;
	proactive_clearing_campaigns+=1;
	RemoveArea* release_wood=new RemoveArea(ForbiddenArea);
	for(int dx=-4; dx<=4; ++dx)
	{
		for(int dy=-4; dy<=4; ++dy)
		{
			if(mi.is_resource(best_x+dx, best_y+dy, WOOD)
				&& !mi.is_resource(best_x+dx, best_y+dy, CORN))
				release_wood->add_location(best_x+dx, best_y+dy);
		}
	}
	echo.add_management_order(release_wood);
	echo.add_management_order(new ChangeFlagSize(4, proactive_clearing_flag));
	emit_telemetry(echo, "land_clearing_started",
		"\tflag="+boost::lexical_cast<std::string>(proactive_clearing_flag)
		+"\tx="+boost::lexical_cast<std::string>(best_x)
		+"\ty="+boost::lexical_cast<std::string>(best_y)
		+"\twood="+boost::lexical_cast<std::string>(best_wood)
		+"\tworkers="+boost::lexical_cast<std::string>(clearing_workers)
		+"\treason="+(best_maintenance_wood>0 ? "maintenance_overgrowth"
			: (budget.farming_clearing_for_placement
				? "construction_pressure" : "wood_pressure")));
	farming_urgent=true;
	director.invalidate();
}

///Desired clearing contracts for one pass. `circulation` represents hard
///building/access obligations; `firebreak` is a renewable wood-only policy.
///Keeping them separate makes their different ForbiddenArea precedence explicit.
struct Maxima::MaintenanceClearingPlan
{
	std::vector<Uint8> circulation;
	std::vector<Uint8> firebreak;
	int wheat_invasion_wood;
	int firebreak_tiles;
	int firebreak_wood;
	int reservation_resources_preserved;
	int reservation_fallback_entrances;

	explicit MaintenanceClearingPlan(int size): circulation(size, 0),
		firebreak(size, 0), wheat_invasion_wood(0), firebreak_tiles(0),
		firebreak_wood(0), reservation_resources_preserved(0),
		reservation_fallback_entrances(0) {}
};

std::vector<Uint8> Maxima::worker_reachable_circulation(Context& echo) const
{
	Map* map=echo.player->map;
	const int w=map->getW(), h=map->getH(), size=w*h;
	// Empty pockets beside a building are not necessarily connected to workers.
	// Ignore transient unit occupancy, but respect resources and land mobility.
	std::vector<Uint8> reachable(size,0);
	for(int swimming=0;swimming<=1;++swimming)
	{
		std::vector<Uint8> visited(size,0);
		std::vector<int> queue;
		for(int id=0;id<Unit::MAX_COUNT;++id)
		{
			const Unit* worker=echo.player->team->myUnits[id];
			if(!worker||worker->typeNum!=WORKER
			   ||int(worker->performance[SWIM]>0)!=swimming)continue;
			const int index=map->normalizeY(worker->posY)*w+map->normalizeX(worker->posX);
			if(!visited[index]){visited[index]=1;queue.push_back(index);}
		}
		for(size_t head=0;head<queue.size();++head)
		{
			const int index=queue[head],x=index%w,y=index/w;
			const Tile& current=map->getTile(x,y);
			if(current.building==NOGBID&&current.resource.type==NO_RES_TYPE)
				reachable[index]=1;
			for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
			{
				const int nx=map->normalizeX(x+dx),ny=map->normalizeY(y+dy),next=ny*w+nx;
				const Tile& tile=map->getTile(nx,ny);
				if(visited[next]||tile.building!=NOGBID||tile.resource.type!=NO_RES_TYPE
				   ||(!swimming&&map->isWater(nx,ny))
				   ||!map->isMapDiscovered(nx,ny,echo.player->team->allies)
				   ||(map->isForbidden(nx,ny,echo.player->team->me)
				      &&!development_planner.isCirculationReserved(next)))continue;
				visited[next]=1;queue.push_back(next);
			}
		}
	}

	return reachable;
}

std::vector<std::vector<int> > Maxima::reservation_member_footprints(
	Context& echo, const AIMaximaPlacement::Reservation& contract) const
{
	Map* map=echo.player->map;
	const int w=map->getW();
	std::vector<std::vector<int> > members;
	auto add_member=[&](int buildingId,
		const AIMaximaPlacement::DevelopmentAction* action)
	{
		Building* building=echo.get_building_register().get_building(buildingId);
		int x,y,width,height;
		if(building&&action&&action->type==AIMaximaPlacement::UpgradeBuilding
		   &&action->state==AIMaximaPlacement::ParcelReserved)
		{
			const BuildingType* target=globalContainer->buildingsTypes.getByType(
				building->type->type,action->targetLevel-1,true);
			if(!target)return;
			x=action->centerX+target->decLeft;y=action->centerY+target->decTop;
			width=target->width;height=target->height;
		}
		else if(building)
		{
			x=building->posX;y=building->posY;
			width=building->type->width;height=building->type->height;
		}
		else if(action)
		{
			x=action->centerX+action->initialFootprint.left;
			y=action->centerY+action->initialFootprint.top;
			width=action->initialFootprint.width;height=action->initialFootprint.height;
		}
		else return;
		std::vector<int> footprint;
		for(int dy=0;dy<height;++dy)for(int dx=0;dx<width;++dx)
			footprint.push_back(map->normalizeY(y+dy)*w+map->normalizeX(x+dx));
		std::sort(footprint.begin(),footprint.end());
		if(std::find(members.begin(),members.end(),footprint)==members.end())
			members.push_back(footprint);
	};
	if(contract.campusId>=0)
		for(const auto& campus:development_planner.campuses())
			if(campus.id==contract.campusId)
				for(const auto& slot:campus.slots)
					if(slot.buildingId>=0)add_member(slot.buildingId,NULL);
	if(contract.buildingId>=0)add_member(contract.buildingId,NULL);
	for(const auto& entry:development_planner.actions())
	{
		const auto& action=entry.second;
		if(action.state!=AIMaximaPlacement::ParcelReserved
		   &&action.state!=AIMaximaPlacement::CreateIssued
		   &&action.state!=AIMaximaPlacement::SiteObserved)continue;
		if(action.reservationId==contract.id
		   ||(contract.campusId>=0&&action.campusId==contract.campusId))
			add_member(action.buildingId,&action);
	}
	return members;
}

Maxima::MaintenanceClearingPlan Maxima::build_maintenance_clearing_plan(
	Context& echo)
{
	Map* map=echo.player->map;
	const int w=map->getW();
	const int h=map->getH();
	const int size=w*h;
	MaintenanceClearingPlan plan(size);
	const bool enabled=budget.farming_enabled
		&& budget.farming_maintenance_clearing_enabled;

	// Snapshot resources only when a circulation contract starts. Once the
	// resource is consumed, the applied mask latches that tile clear.
	std::vector<Uint8> grandfathered_resource(size, 0);
	std::vector<int> resource_burden(size, 0);
	for(int index=0; index<size; ++index)
	{
		const Tile& cell=map->getTile(index%w, index/w);
		grandfathered_resource[index]=!applied_maintenance_clearing_mask[index]
			&& (cell.resource.type==CORN || cell.resource.type==WOOD);
		resource_burden[index]=std::max(1, int(cell.resource.amount));
	}
	auto retain_circulation=[&](const std::vector<int>& tiles)
	{
		for(size_t i=0; i<tiles.size(); ++i)
			if(tiles[i]>=0 && tiles[i]<size)
				plan.circulation[tiles[i]]=1;
	};

	const std::vector<Uint8> reachable=enabled
		&& budget.farming_resource_preserving_circulation_enabled
		? worker_reachable_circulation(echo) : std::vector<Uint8>(size, 0);

	if(enabled)
	{
		const std::map<int,AIMaximaPlacement::Reservation>& reservations=
			development_planner.reservations();
		for(std::map<int,AIMaximaPlacement::Reservation>::const_iterator reservation=
			reservations.begin(); reservation!=reservations.end(); ++reservation)
		{
			const AIMaximaPlacement::Reservation& contract=reservation->second;
			// Empty transient reservations are backed by a permanent contract.
			if(contract.footprintTiles.empty()&&contract.circulationTiles.empty())
				continue;
			const std::vector<std::vector<int> > members=
				reservation_member_footprints(echo, contract);
			// Only actual/pending members need their footprint kept clear. Future
			// upgrade land and unused campus slots remain resource-preserving.
			std::vector<Uint8> memberMask(size,0);
			std::vector<int> circulation=contract.circulationTiles;
			for(const auto& member:members)
			{
				retain_circulation(member);
				for(int index:member)memberMask[index]=1;
				// A small initial building can sit inside its terminal footprint;
				// protect an entrance at its current boundary as well as the outer ring.
				for(int index:member)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
				{
					const int neighbor=map->normalizeY(index/w+dy)*w+map->normalizeX(index%w+dx);
					if(std::binary_search(contract.footprintTiles.begin(),
						contract.footprintTiles.end(),neighbor))circulation.push_back(neighbor);
				}
			}
			std::sort(circulation.begin(),circulation.end());
			circulation.erase(std::unique(circulation.begin(),circulation.end()),circulation.end());
			circulation.erase(std::remove_if(circulation.begin(),circulation.end(),
				[&](int index){const Tile& cell=map->getTile(index%w,index/w);
					return memberMask[index]||cell.building!=NOGBID||cell.terrain>=16
						||!map->isMapDiscovered(index%w,index/w,echo.player->team->allies)
						||(cell.resource.type!=NO_RES_TYPE
						   &&globalContainer->resourcesTypes.get(cell.resource.type)->eternal);
				}),circulation.end());
			if(budget.farming_resource_preserving_circulation_enabled)
			{
				std::vector<Uint8> preserved=grandfathered_resource;
				// Old saves may have maintained the whole future footprint. Do not
				// grandfather those destructive clearing obligations into this plan.
				for(int index:contract.footprintTiles)if(!memberMask[index])
				{
					const int resource=map->getTile(index%w,index/w).resource.type;
					preserved[index]=resource==CORN||resource==WOOD;
				}
				for(const auto& member:members)
				{
					const Farming::ReservationClearingSelection selection=
						Farming::selectResourcePreservingCirculation(w,h,member,
							circulation,preserved,resource_burden,reachable);
					retain_circulation(selection.tiles);
					for(int index:selection.tiles)preserved[index]=0;
					if(selection.fallbackEntranceTile>=0)
					{
						++plan.reservation_fallback_entrances;
					}
				}
				for(int index:circulation)
					plan.reservation_resources_preserved+=preserved[index]!=0;
			}
			else
				retain_circulation(circulation);
		}
	}

	const std::vector<Uint8> managed=farm_management_area(echo,
		budget.farming_management_radius);
	for(int index=0; index<size; ++index)
	{
		const int x=index%w;
		const int y=index/w;
		const Tile& cell=map->getTile(x, y);
		const bool discovered=map->isMapDiscovered(x, y,
			echo.player->team->me);
		if(wheat_invasion_clearing_required(echo, index,
			wheat_farm_protection_mask))
		{
			plan.circulation[index]=1;
			++plan.wheat_invasion_wood;
		}
		const bool permanent_resource=cell.resource.type!=NO_RES_TYPE
			&& globalContainer->resourcesTypes.get(cell.resource.type)->eternal;
		const bool wants_firebreak=enabled
			&& managed[index]
			&& budget.farming_wood_firebreak_enabled
			&& discovered && cell.terrain<16
			&& cell.canResourcesGrow && cell.building==NOGBID
			&& !permanent_resource && cell.resource.type==WOOD
			&& Farming::fertilityWithinPercentBand(fertility_cache.at(x, y),
				strategy.farming.wood_firebreak_fertility_min_percent,
				strategy.farming.wood_firebreak_fertility_max_percent);
		plan.firebreak[index]=wants_firebreak;
		if(wants_firebreak)
		{
			++plan.firebreak_tiles;
			plan.firebreak_wood+=cell.resource.type==WOOD;
		}
	}
	return plan;
}

void Maxima::apply_maintenance_clearing_plan(Context& echo,
	const MaintenanceClearingPlan& plan)
{
	Map* map=echo.player->map;
	const int w=map->getW();
	const int size=w*map->getH();
	maintenance_circulation_mask=plan.circulation;
	wood_firebreak_mask=plan.firebreak;
	AddArea* additions=new AddArea(ClearingArea);
	RemoveArea* removals=new RemoveArea(ClearingArea);
	RemoveArea* forbidden_releases=new RemoveArea(ForbiddenArea);
	int added=0;
	int removed=0;
	int released=0;
	int retained=0;
	for(int index=0; index<size; ++index)
	{
		const int x=index%w;
		const int y=index/w;
		const bool discovered=map->isMapDiscovered(x, y,
			echo.player->team->me);
		const bool building_footprint=map->getTile(x, y).building!=NOGBID;
		const bool contract_desired=plan.circulation[index]
			&& !building_footprint && discovered;
		const bool desired=!building_footprint
			&& (contract_desired || plan.firebreak[index]);
		const bool actual=map->isClearArea(x, y, echo.player->team->me);
		if(desired)
		{
			++retained;
			if(!actual)
			{
				additions->add_location(x, y);
				++added;
			}
			// Hard circulation contracts outrank farming. A firebreak alone
			// never removes a farm's ForbiddenArea protection.
			if(contract_desired && map->isForbidden(x, y,
				echo.player->team->me))
			{
				forbidden_releases->add_location(x, y);
				++released;
			}
		}
		else if(applied_maintenance_clearing_mask[index] && actual)
		{
			removals->add_location(x, y);
			++removed;
		}
		applied_maintenance_clearing_mask[index]=desired;
	}
	if(added) echo.add_management_order(additions); else delete additions;
	if(removed) echo.add_management_order(removals); else delete removals;
	if(released) echo.add_management_order(forbidden_releases);
	else delete forbidden_releases;
	if(added || removed || released || timer%1000<budget.farming_normal_interval)
		emit_telemetry(echo, "maintenance_clearing",
			"\tretained="+boost::lexical_cast<std::string>(retained)
			+"\tadded="+boost::lexical_cast<std::string>(added)
			+"\tremoved="+boost::lexical_cast<std::string>(removed)
			+"\tfirebreak="+boost::lexical_cast<std::string>(plan.firebreak_tiles)
			+"\tfirebreak_wood="+boost::lexical_cast<std::string>(plan.firebreak_wood)
			+"\twheat_invasion_wood="+boost::lexical_cast<std::string>(
				plan.wheat_invasion_wood)
			+"\treservation_resources_preserved="
				+boost::lexical_cast<std::string>(
					plan.reservation_resources_preserved)
			+"\treservation_fallback_entrances="
				+boost::lexical_cast<std::string>(
					plan.reservation_fallback_entrances)
			+"\tforbidden_released="
				+boost::lexical_cast<std::string>(released));
}

void Maxima::update_maintenance_clearing_areas(Context& echo)
{
	initialize_farming_cache(echo);
	const MaintenanceClearingPlan plan=build_maintenance_clearing_plan(echo);
	apply_maintenance_clearing_plan(echo, plan);
}
void Maxima::initialize_farming_cache(Context& echo)
{
	Map* map=echo.player->map;
	const int w=map->getW();
	const int h=map->getH();
	if(fertility_cache.validFor(w, h)
	   && farming_shoreline_mask.size()==size_t(w*h)
	   && farming_cardinal_shoreline_mask.size()==size_t(w*h)
	   && applied_farm_protection_mask.size()==size_t(w*h)
	   && applied_maintenance_clearing_mask.size()==size_t(w*h)
	   && wood_firebreak_mask.size()==size_t(w*h)
	   && wheat_farm_protection_mask.size()==size_t(w*h))
		return;
	const std::chrono::steady_clock::time_point started=
		std::chrono::steady_clock::now();
	std::vector<Uint8> water(w*h, 0);
	std::vector<Uint8> sand(w*h, 0);
	for(int y=0; y<h; ++y)
		for(int x=0; x<w; ++x)
		{
			const Uint16 terrain=map->getTile(x, y).terrain;
			water[y*w+x]=terrain>=256 && terrain<272;
			sand[y*w+x]=terrain>=128 && terrain<144;
		}
	fertility_cache.rebuild(w, h, water, sand);
	// Terrain does not change during a game. Compute the whole connected
	// beach once, then keep final adjacency masks for constant-time queries.
	const std::vector<Uint8> backing=shoreline_backing(map);
	farming_shoreline_mask.resize(w*h);
	farming_cardinal_shoreline_mask.resize(w*h);
	for(int y=0; y<h; ++y)
		for(int x=0; x<w; ++x)
		{
			farming_shoreline_mask[y*w+x]=touches_shoreline_backing(backing, w, h, x, y);
			farming_cardinal_shoreline_mask[y*w+x]=touches_shoreline_backing(backing, w, h, x, y, true);
		}
	// Saved protection includes empty pre-growth frontier cells. Reclaim them
	// too, so a changed or disabled farming plan can release the old area.
	applied_farm_protection_mask.assign(w*h, 0);
	applied_maintenance_clearing_mask.assign(w*h, 0);
	maintenance_circulation_mask.assign(w*h, 0);
	wood_firebreak_mask.assign(w*h, 0);
	farm_protection_mask.assign(w*h, 0);
	wheat_farm_protection_mask.assign(w*h, 0);
	for(int y=0; y<h; ++y)
		for(int x=0; x<w; ++x)
		{
			if(map->isMapDiscovered(x, y, echo.player->team->me)
			   && map->isForbidden(x, y, echo.player->team->me))
				applied_farm_protection_mask[y*w+x]=1;
			if(map->isMapDiscovered(x, y, echo.player->team->me)
			   && map->isClearArea(x, y, echo.player->team->me))
				applied_maintenance_clearing_mask[y*w+x]=1;
		}
	const long long elapsed=std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now()-started).count();
	emit_telemetry(echo, "farming_fertility_cache",
		"\tmicroseconds="+boost::lexical_cast<std::string>(elapsed)
		+"\tpath="+(fertility_cache.pathUsed()==Farming::SandCorrectionFertilityPath
			? "sand_correction" : "water_splat")
		+"\twater="+boost::lexical_cast<std::string>(fertility_cache.waterCount())
		+"\tsand="+boost::lexical_cast<std::string>(fertility_cache.sandCount()));
}








int Maxima::available_expansion_neighbors(Context& echo, int x, int y) const
{
	Map* map=echo.player->map;
	int available=0;
	for(int dy=-1; dy<=1; ++dy)
		for(int dx=-1; dx<=1; ++dx)
		{
			if(!dx && !dy) continue;
			const Tile& cell=map->getTile(x+dx, y+dy);
			if(cell.terrain<16 && cell.canResourcesGrow
			   && cell.resource.type==NO_RES_TYPE && cell.building==NOGBID
			   && cell.groundUnit==NOGUID && cell.airUnit==NOGUID)
				available+=1;
		}
	return available;
}

int Maxima::growth_absorbing_neighbors(Context& echo, int x, int y) const
{
	Map* map=echo.player->map;
	const ResourceType* corn=globalContainer->resourcesTypes.get(CORN);
	const int full=corn ? corn->sizesCount : 0;
	int available=0;
	for(int dy=-1; dy<=1; ++dy)
		for(int dx=-1; dx<=1; ++dx)
		{
			if(!dx && !dy) continue;
			const Tile& cell=map->getTile(x+dx, y+dy);
			if(cell.terrain>=16 || !cell.canResourcesGrow
			   || cell.building!=NOGBID) continue;
			// Growth either seeds empty ground or tops up a partly harvested
			// stack beside it. A full stack absorbs nothing, so a protected
			// cell hemmed in by full wheat yields nothing either.
			// Passing units are deliberately ignored: they move every tick and
			// would make a standing supply estimate flicker.
			if(cell.resource.type==NO_RES_TYPE
			   || (cell.resource.type==CORN && cell.resource.amount<full))
				available+=1;
		}
	return available;
}

void Maxima::release_farming_protection(Context& echo)
{
	MapInfo map_info(echo);
	Map* map=echo.player->map;
	std::fill(farm_protection_mask.begin(), farm_protection_mask.end(), 0);
	std::fill(wheat_farm_protection_mask.begin(),
		wheat_farm_protection_mask.end(), 0);
	RemoveArea* removals=new RemoveArea(ForbiddenArea);
	int removed=0;
	for(int index=0; index<map->getW()*map->getH(); ++index)
	{
		const int x=index%map->getW();
		const int y=index/map->getW();
		if(applied_farm_protection_mask[index] && map_info.is_forbidden_area(x, y))
		{
			removals->add_location(x, y);
			++removed;
		}
		applied_farm_protection_mask[index]=0;
	}
	if(removed) echo.add_management_order(removals); else delete removals;
	farming_urgent=false;
}

bool Maxima::has_hard_farming_contract(int index) const
{
	return development_planner.isFootprintReserved(index)
		|| development_planner.isCirculationReserved(index);
}

bool Maxima::wheat_invasion_clearing_required(Context& echo, int index,
	const std::vector<Uint8>& protected_wheat) const
{
	Map* map=echo.player->map;
	const int w=map->getW(), h=map->getH();
	const int x=index%w, y=index/w;
	return budget.farming_enabled && budget.farming_maintenance_clearing_enabled
		&& budget.farming_wheat_invasion_clearing_enabled
		&& map->isMapDiscovered(x, y, echo.player->team->me)
		&& map->getTile(x, y).resource.type==WOOD
		&& Farming::hasAdjacentProtectedWheat(protected_wheat, w, h, x, y);
}

Maxima::FarmProtectionPlan Maxima::build_farming_protection_plan(Context& echo)
{
	Map* map=echo.player->map;
	MapInfo map_info(echo);
	const int w=map->getW();
	const int h=map->getH();
	FarmProtectionPlan plan(w*h);
	plan.wood_pressure=budget.farming_wood_pressure;
	plan.wood_fertility=budget.farming_minimum_wood_fertility;
	int clearing_x=0, clearing_y=0;
	const bool clearing_wood=!budget.recovery_active
		&& proactive_clearing_flag>=0
		&& clearing_campaign_kind(proactive_clearing_initial_wood)==WoodClearingCampaign
		&& echo.get_building_position(proactive_clearing_flag,clearing_x,clearing_y);

	GradientInfo water_info;
	water_info.add_source(new Entities::Water);
	Gradient& water_gradient=echo.get_gradient_manager().get_gradient(water_info);
	const std::vector<Uint8> water_exposure=waterward_exposure(map, water_gradient);

	// Record which empty tiles can extend a live wheat or wood resource. This
	// avoids running the more expensive classifier for unrelated map cells.
	std::vector<Uint8> adjacent_resource_mask(w*h, 0);
	for(int y=0; y<h; ++y)
		for(int x=0; x<w; ++x)
		{
			const Tile& resource=map->getTile(x, y);
			Uint8 bit=0;
			if(resource.resource.amount>0)
			{
				if(resource.resource.type==CORN) bit=1;
				else if(resource.resource.type==WOOD) bit=2;
			}
			if(!bit) continue;
			for(int dy=-1; dy<=1; ++dy)
				for(int dx=-1; dx<=1; ++dx)
					if(dx || dy)
						adjacent_resource_mask[((y+dy+h)%h)*w
							+((x+dx+w)%w)]|=bit;
		}

	for(int y=0; y<h; ++y)
		for(int x=0; x<w; ++x)
		{
			const int index=y*w+x;
			const Tile& cell=map->getTile(x, y);
			const bool wheat=cell.resource.type==CORN
				&& cell.resource.amount>0;
			const bool wood=cell.resource.type==WOOD
				&& cell.resource.amount>0;
			const bool empty_growth=is_empty_growth_cell(cell);

			FarmTileClassification wheat_role;
			FarmTileClassification wood_role;
			if(wheat || (empty_growth && (adjacent_resource_mask[index]&1)
			   && (farming_shoreline_mask[index]
				|| fertility_cache.at(x, y)>=
				Uint32(budget.farming_wheat_fertility_min))))
				wheat_role=classify_farm_tile(map_info, map, water_gradient,
					fertility_cache, water_exposure, farming_shoreline_mask[index]!=0, x, y, CORN,
					Uint32(budget.farming_wheat_fertility_min));
			if(wood || (empty_growth && (adjacent_resource_mask[index]&2)
			   && fertility_cache.at(x, y)>=plan.wood_fertility))
				wood_role=classify_farm_tile(map_info, map, water_gradient,
					fertility_cache, water_exposure, farming_shoreline_mask[index]!=0, x, y, WOOD, plan.wood_fertility);
			const bool wheat_farm=wheat_role.protected_tile();
			const bool wood_farm=wood_role.protected_tile();
			if(!wheat && !wood && !wheat_farm && !wood_farm) continue;

			// This priority table is the policy. Classification above only says
			// what a tile is; this block says which spatial contract wins.
			bool protect=false;
			if(has_hard_farming_contract(index))
				protect=false;
			else if(wheat_farm)
				protect=true;
			// Keep the authorized wood campaign's working area open for its
			// entire lifetime, including while its exact-position flag is queued.
			else if(wood && clearing_wood
				&& map->warpDistSquare(x,y,clearing_x,clearing_y)<=4*4)
				protect=false;
			else if(budget.farming_wood_firebreak_enabled && wood
			   && index<int(wood_firebreak_mask.size())
			   && wood_firebreak_mask[index])
				protect=false;
			else if(wood_farm)
				protect=true;
			else if(wheat || wood)
				protect=false;

			plan.forbidden[index]=protect;
			plan.protected_wheat[index]=protect && wheat_farm;
			if(!protect) continue;

			plan.protected_frontier+=wheat_role.frontier+wood_role.frontier;
			plan.protected_wheat_edges+=wheat_role.edge;
			plan.protected_wheat_bootstraps+=wheat_role.bootstrap;
			plan.protected_wood_edges+=wood_role.edge;
			plan.protected_wood_bootstraps+=wood_role.bootstrap;
			plan.protected_interior_seeds+=
				(wheat_role.interior_seed && !wheat_role.edge)
				+(wood_role.interior_seed && !wood_role.edge);
			if(wheat || wood)
			{
				plan.protected_seeds+=(x&1) && (y&1);
				const int available=available_expansion_neighbors(echo, x, y);
				plan.blocked_directions+=8-available;
				plan.expected_capacity+=Farming::usefulExpansionCapacity(
					fertility_cache.at(x, y),
					map_info.get_ammount_resource(x, y), available, wheat);
			}
		}
	// Empty frontier protection cannot regrow a patch after its last live seed
	// is harvested. The sparse pattern may leave a boundary lattice seed open
	// while that same seed suppresses its neighbor's local bootstrap. Retain one
	// eligible anchor only in components with no protected live resource.
	std::vector<Uint8> visited(w*h,0);
	for(int start=0;start<w*h;++start)
	{
		const int resource=map->getTile(start%w,start/w).resource.type;
		if(visited[start] || (resource!=CORN && resource!=WOOD)) continue;
		std::vector<int> component(1,start);
		visited[start]=1;
		bool protected_live=false;
		int anchor=-1;
		for(size_t head=0;head<component.size();++head)
		{
			const int index=component[head], x=index%w, y=index/w;
			const Tile& cell=map->getTile(x,y);
			if(cell.resource.amount>0)
			{
				protected_live|=plan.forbidden[index]!=0;
				const Uint32 minimum=resource==CORN
					? Uint32(budget.farming_wheat_fertility_min) : plan.wood_fertility;
				if(fertility_cache.at(x,y)>=minimum
				   && !has_hard_farming_contract(index)
				   && !(resource==WOOD && ((budget.farming_wood_firebreak_enabled
					   && wood_firebreak_mask[index]) || (clearing_wood
					   && map->warpDistSquare(x,y,clearing_x,clearing_y)<=4*4)))
				   && (anchor<0 || index<anchor))
					anchor=index;
			}
			for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
			{
				const int nx=map->normalizeX(x+dx), ny=map->normalizeY(y+dy);
				const int next=ny*w+nx;
				if(!visited[next]
				   && map->getTile(nx,ny).resource.type==resource)
				{
					visited[next]=1;
					component.push_back(next);
				}
			}
		}
		if(protected_live || anchor<0) continue;
		const int x=anchor%w, y=anchor/w;
		plan.forbidden[anchor]=1;
		plan.protected_wheat[anchor]=resource==CORN;
		if(resource==CORN) ++plan.protected_wheat_bootstraps;
		else ++plan.protected_wood_bootstraps;
		plan.protected_seeds+=(x&1) && (y&1);
		const int available=available_expansion_neighbors(echo,x,y);
		plan.blocked_directions+=8-available;
		plan.expected_capacity+=Farming::usefulExpansionCapacity(
			fertility_cache.at(x,y),map_info.get_ammount_resource(x,y),available,resource==CORN);
	}
	return plan;
}




void Maxima::resolve_wheat_invasion_clearing(Context& echo,
	FarmProtectionPlan& plan)
{
	// Use this pass's final wheat mask, including fallback seeds and coastal
	// openings. Reading last pass's maintenance mask would lag both new and
	// revoked obligations and allow the two reconcilers to undo each other.
	if(!budget.farming_maintenance_clearing_enabled
	   || !budget.farming_wheat_invasion_clearing_enabled) return;
	for(size_t index=0; index<plan.forbidden.size(); ++index)
		if(wheat_invasion_clearing_required(echo, int(index), plan.protected_wheat))
			plan.forbidden[index]=0;
}

void Maxima::apply_farming_protection(Context& echo,
	const FarmProtectionPlan& plan, int& added, int& removed)
{
	MapInfo map_info(echo);
	Map* map=echo.player->map;
	const int w=map->getW();
	const int size=w*map->getH();
	AddArea* additions=new AddArea(ForbiddenArea);
	RemoveArea* removals=new RemoveArea(ForbiddenArea);
	added=0;
	removed=0;
	int seed_revocations=0;
	for(int index=0; index<size; ++index)
	{
		const int x=index%w;
		const int y=index/w;
		if(!map_info.is_discovered(x, y)) continue;
		// Audit the temporal contract as well as today's mask. Building/path
		// contracts are explicit overrides; harvesting a neighbor is not.
		if(Farming::isInteriorSeed(x,y) && farm_protection_mask[index]
		   && !plan.forbidden[index] && map->isResourceTakeable(x,y,CORN)
		   && !has_hard_farming_contract(index)) ++seed_revocations;
		const bool actual=map_info.is_forbidden_area(x, y);
		if(plan.forbidden[index] && !actual)
		{
			additions->add_location(x, y);
			++added;
		}
		else if(!plan.forbidden[index] && applied_farm_protection_mask[index] && actual)
		{
			removals->add_location(x, y);
			++removed;
		}
		applied_farm_protection_mask[index]=plan.forbidden[index];
	}
	if(added) echo.add_management_order(additions); else delete additions;
	if(removed) echo.add_management_order(removals); else delete removals;
	if(seed_revocations)
		emit_telemetry(echo,"farming_seed_stability_violation",
			"\tcount="+boost::lexical_cast<std::string>(seed_revocations));
	farm_protection_mask=plan.forbidden;
	wheat_farm_protection_mask=plan.protected_wheat;
}

void Maxima::update_farming(Context& echo)
{
	const std::chrono::steady_clock::time_point started=
		std::chrono::steady_clock::now();
	initialize_farming_cache(echo);
	if(!budget.farming_enabled || !budget.farming_protection_enabled)
	{
		release_farming_protection(echo);
		return;
	}
	FarmProtectionPlan plan=build_farming_protection_plan(echo);
	// Establish/expand farms near allied infrastructure, never around flags.
	// Recompute proximity as buildings change, but retain established odd/odd
	// wheat seeds after losing an anchor; distance must not cause seed churn.
	// This is an establishment limit, not an override for tactical clearing.
	if(budget.farming_management_radius>0)
	{
		Map* map=echo.player->map;
		const int w=map->getW(), h=map->getH();
		const int radius=budget.farming_management_radius;
		const std::vector<Uint8> nearby=farm_management_area(echo,radius);
		for(int i=0;i<w*h;++i)
		{
			if(nearby[i])continue;
			const bool established= farm_protection_mask[i]
				&& Farming::isInteriorSeed(i%w,i/w)
				&& map->isResourceTakeable(i%w,i/w,CORN);
			if(established)continue;
			plan.forbidden[i]=0;
			plan.protected_wheat[i]=0;
		}
	}

	resolve_wheat_invasion_clearing(echo, plan);
	int added=0;
	int removed=0;
	apply_farming_protection(echo, plan, added, removed);
	// Urgency used to mean a blocked gate or access route. Without the barrier
	// there is no such standing obligation, so the policy keeps its ordinary
	// cadence; the clearing campaign still raises this flag on its own.
	if(farming_urgent)
	{
		farming_urgent=false;
		director.invalidate();
	}
	const long long elapsed=std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now()-started).count();
	// Preserve every applied-area change and one idle sample per strategic
	// interval; unchanged urgent evaluations add no diagnostic information.
	if(added || removed || timer%1000<budget.farming_normal_interval)
		emit_telemetry(echo, "farming_policy",
		"\tmicroseconds="+boost::lexical_cast<std::string>(elapsed)
		+"\tprotected_seeds="+boost::lexical_cast<std::string>(plan.protected_seeds)
		+"\tprotected_frontier="+boost::lexical_cast<std::string>(plan.protected_frontier)
		+"\tprotected_wheat_edges="+boost::lexical_cast<std::string>(plan.protected_wheat_edges)
		+"\tprotected_wheat_bootstraps="+boost::lexical_cast<std::string>(plan.protected_wheat_bootstraps)
		+"\tprotected_wood_edges="+boost::lexical_cast<std::string>(plan.protected_wood_edges)
		+"\tprotected_wood_bootstraps="+boost::lexical_cast<std::string>(plan.protected_wood_bootstraps)
		+"\tprotected_interior_seeds="+boost::lexical_cast<std::string>(plan.protected_interior_seeds)
		+"\texpected_capacity="+boost::lexical_cast<std::string>(plan.expected_capacity)
		+"\tblocked_directions="+boost::lexical_cast<std::string>(plan.blocked_directions)
		+"\twood_pressure="+boost::lexical_cast<std::string>(plan.wood_pressure)
		+"\twood_fertility="+boost::lexical_cast<std::string>(plan.wood_fertility)
		+"\tadded="+boost::lexical_cast<std::string>(added)
		+"\tremoved="+boost::lexical_cast<std::string>(removed));
}

}
