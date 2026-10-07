/* Maxima farming and clearing policy. */

#include "Material.h"
#include "AIResourcePolicy.h"
#include "field/UniformTraversal.h"
#include "AITelemetryFields.h"
#include "AIMaxima.h"
#include "AIMaximaFarmGeometry.h"
#include "AIMaximaBuildings.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "Unit.h"

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
	std::vector<Uint8> farm_management_area(Context& runtime, int radius)
	{
		const AIEngine::AIWorldView* map=&runtime.observation();
		const int w=(*map).width, h=(*map).height;
		if(radius<=0)return std::vector<Uint8>(w*h,1);
		std::vector<Uint8> nearby(w*h,0);
		for(int team=0;team<Team::MAX_COUNT;++team)
		{
			if(!((runtime.observedTeam().allies|runtime.observedTeam().mask)&(Uint32(1)<<team)))continue;
			const auto* ally=std::size_t(team)<runtime.observation().teams.size() ? &runtime.observation().teams[team] : nullptr;
			if(!ally)continue;
			for(int b=0;b<Building::MAX_COUNT;++b)
			{
				const AIEngine::BuildingView* building=runtime.observation().buildingSlots(team)[b];
				if(!building || !AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).semantics.occupiesGround)continue;
				for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx)
					if(dx*dx+dy*dy<=radius*radius)
						nearby[(*map).normalizeY(building->posY+dy)*w
							+(*map).normalizeX(building->posX+dx)]=1;
			}
		}
		return nearby;
	}

	std::vector<Uint8> shoreline_backing(const AIEngine::AIWorldView* map)
	{
		const int w=(*map).width, h=(*map).height;
		std::vector<Uint8> backing(w*h, 0);
		std::vector<int> queue;
		queue.reserve(w*h);
		for(int y=0; y<h; ++y)
			for(int x=0; x<w; ++x)
				if(terrainProvidesFertility(AIEngine::ObservationQueries::terrain((*map),x, y)))
				{
					backing[y*w+x]=1;
					queue.push_back(y*w+x);
				}
		// A beach can be several sand tiles wide. Follow touching sand from
		// actual water, including blended tiles and diagonals, but never grass.
		// This excludes isolated inland sand without depending on crop density.
		field::traverse(queue,{w,h},field::Surrounding,[](int){return field::Visit::Expand;},
			[&](int,int x,int y) {
				const int nx=(x+w)%w,ny=(y+h)%h,next=ny*w+nx;
				if(!backing[next] && AIEngine::ObservationQueries::terrain((*map),nx,ny).shoreline){backing[next]=1;queue.push_back(next);}
			});
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

    // Whole-tile seed protection only helps finite yields that can reproduce
    // into another tile. Infinite and in-place-only producers remain harvestable;
    // FarmArea's per-material seed reserve is a separate engine policy.
    bool is_spreading_seed(const AIEngine::AIWorldView& map, int x, int y, int material)
    {
        return AIResourcePolicy::needsSeedReserve(map.state(),map.tileIndex(x,y),static_cast<MaterialId>(material));
    }

    bool uses_land_fertility(const AIEngine::AIWorldView& map,int x,int y)
    {
        return map.state().resourceProperties(map.resourceAt(map.tileIndex(x,y)).resource.type).ecology==ResourceEcology::Land;
    }

    bool is_empty_growth_cell(const AIEngine::AIWorldView& map, int x, int y)
    {
        return AIResourcePolicy::emptyGrowthCell(map.state(),map.tileIndex(x,y));
    }

    bool cached_seed(const std::vector<Uint8>& eligibility,const AIEngine::AIWorldView& map,int x,int y,int material)
    {
        return eligibility[map.tileIndex(x,y)] & (material==materialIndex(MaterialId::Food)?1:2);
    }

    bool can_seed_target(const std::vector<Uint8>& eligibility,const AIEngine::AIWorldView& map,
        int sx,int sy,int tx,int ty,int material)
    {
        return cached_seed(eligibility,map,sx,sy,material)
            && AIResourcePolicy::emptyGrowthCell(map.state(),map.tileIndex(tx,ty))
            && MapState::terrainSupportsResourceSlot(map.state(),map.tileIndex(tx,ty),map.resourceAt(map.tileIndex(sx,sy)).resource.type);
    }

	bool is_empty_growth_cell(const AIEngine::AIWorldView* map, std::size_t index)
	{
		const auto resource=map->resourceAt(index);
		const auto& terrain=AIEngine::ObservationQueries::terrain((*map),index);
		return (terrain.allowedResources & (1u<<WHEAT)) && terrain.resourcesGrow && resource.mayGrow
			&& resource.resource.type==NO_RES_TYPE && map->occupancyAt(index).building==NOGBID;
	}

	// Close one-cell harvest gaps before identifying the outer farm boundary.
	// Otherwise routine harvesting would turn the entire interior into an edge.
	std::vector<Uint8> wheat_farm_exterior(const AIEngine::AIWorldView* map)
	{
        return FarmGeometry::foodExterior(map->width,map->height,[map](int index) {
            return MapState::materialAmountAt(map->state(),index,MaterialId::Food)>0;
        });
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

	FarmTileClassification classify_farm_tile(MapInfo& mi, const AIEngine::AIWorldView* map,
		const Farming::ExactFertilityCache& fertility_cache,
		const std::vector<Uint8>& wheat_exterior, const std::vector<Uint8>& seedEligibility,
		bool shoreline_backed, int x, int y, int resource_type, Uint32 minimum_fertility)
	{
		FarmTileClassification pattern;
		const int w=(*map).width;
		const int h=(*map).height;
		const int index=y*w+x;
		const bool seed_lattice=Farming::isInteriorSeed(x, y);
		const bool expansion_lattice=Farming::isExpansionCell(x, y);
		const Uint32 fertility=fertility_cache.at(x, y);
		const bool resource=cached_seed(seedEligibility,*map,x,y,resource_type);
		// A coastal wheat wall must cross local fertility dips. Only the
		// immediate frontier of a live crop qualifies below, so this cannot
		// reserve unrelated empty coast. Keep the exemption after growth too:
		// protecting an empty tile then exposing its new wheat would defeat it.
		const bool fertile=fertility>=minimum_fertility
			|| (resource_type==materialIndex(MaterialId::Food) && shoreline_backed);
		if(is_empty_growth_cell(*map,x,y))
		{
			bool adjacent_resource=false;
			for(int dy=-1; dy<=1; ++dy)
				for(int dx=-1; dx<=1; ++dx)
				{
					if(!dx && !dy) continue;
					if(can_seed_target(seedEligibility,*map,x+dx,y+dy,x,y,resource_type)
                        && (fertile || !uses_land_fertility(*map,x+dx,y+dy)))
						adjacent_resource=true;
				}
			// Protection follows the expansion lattice only. A shoreline run
			// must stay porous by construction; it is never a sealed contour.
			pattern.frontier=adjacent_resource && expansion_lattice
				&& (resource_type!=materialIndex(MaterialId::Food) || wheat_exterior[index] || seed_lattice);
		}
		if(resource && (fertile || !uses_land_fertility(*map,x,y)))
		{
			for(int dy=-1; dy<=1; ++dy)
				for(int dx=-1; dx<=1; ++dx)
				{
					if(!dx && !dy) continue;
					const auto neighborIndex=map->tileIndex(x+dx, y+dy);
					const bool eligible=can_seed_target(seedEligibility,*map,x,y,x+dx,y+dy,resource_type)
						&& (fertility_cache.at(x+dx,y+dy)>=minimum_fertility || !uses_land_fertility(*map,x,y))
						&& (resource_type!=materialIndex(MaterialId::Food)
							|| wheat_exterior[map->normalizeY(y+dy)*w+map->normalizeX(x+dx)]);
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
						if(!cached_seed(seedEligibility,*map,nx,ny,resource_type)) continue;
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
	WoodReserve wood_reserve;
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
		protected_wheat(size, 0), wood_reserve(size), protected_seeds(0),
		protected_frontier(0), protected_wheat_edges(0),
		protected_wheat_bootstraps(0), protected_wood_edges(0),
		protected_wood_bootstraps(0), protected_interior_seeds(0),
		blocked_directions(0), expected_capacity(0), wood_pressure(0),
		wood_fertility(0) {}
};

void Maxima::record_construction_space_failure()
{
	telemetry.count(AITrace::AI7::Maxima_record_construction_space_failure_calls);
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

Maxima::WoodClearingTarget Maxima::select_wood_clearing_target(Context& runtime) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	MapInfo mi(runtime);
	const AIEngine::AIWorldView* map=&runtime.observation();
	const int w=(*map).width;
	AIMaximaRuntime::Gradients::GradientInfo settlement_info;
	settlement_info.add_source(new Entities::AnyTeamBuilding(
		runtime.teamNumber(), CompletedBuildings));
	Gradient& settlement=runtime.get_gradient_manager().get_gradient(settlement_info);
	const WoodReserve wood_reserve=select_wood_reserve(runtime);
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
				|| !mi.is_crop_habitat(x, y))
				continue;
			const int distance=settlement.get_height(x, y);
			if(distance<0 || distance>14)
				continue;
			int wood=0;
			bool reserve_overlap=false;
			int maintenance_wood=0;
			for(int dx=-4; dx<=4; ++dx)
			{
				for(int dy=-4; dy<=4; ++dy)
				{
					if(dx*dx+dy*dy<=16 && wood_reserve.cells[(*map).normalizeY(y+dy)*w+(*map).normalizeX(x+dx)])
						reserve_overlap=true;
					if(mi.is_resource(x+dx, y+dy, materialIndex(MaterialId::Wood)))
					{
						const int resource_index=((y+dy+(*map).height)
							%(*map).height)*w+((x+dx+w)%w);
						wood+=1;
						if(resource_index<int(maintenance_circulation_mask.size())
						   && maintenance_circulation_mask[resource_index])
							maintenance_wood+=1;
					}
				}
			}
			if(wood<2 || reserve_overlap)
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

void Maxima::retire_clearing_campaign(Context& runtime, const char* reason,
	const std::string& details)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	telemetry.count(AITrace::AI7::Maxima_retire_clearing_campaign_calls);
	runtime.add_management_order(new RetireAttraction(proactive_clearing_flag,1u<<WORKER));
	emit_telemetry(runtime, "land_clearing_finished",
		"\tflag="+telemetryText(proactive_clearing_flag)
		+details+"\treason="+reason);
	proactive_clearing_flag=-1;
}

bool Maxima::continue_clearing_campaign(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	telemetry.count(AITrace::AI7::Maxima_continue_clearing_campaign_calls);
	MapInfo mi(runtime);
	if(proactive_clearing_flag!=-1)
	{
		if(runtime.get_building_register().is_building_found(proactive_clearing_flag))
		{
			const AIEngine::BuildingView* flag=runtime.get_building_register().get_building(proactive_clearing_flag);
			bool clearing_resources[MaterialCount]={false};
			clearing_resources[materialIndex(MaterialId::Wood)]=true;
			runtime.push_order(std::shared_ptr<Order>(new OrderModifyClearingFlag(
				flag->gid, clearing_resources)));

			const WoodReserve wood_reserve=select_wood_reserve(runtime);
			bool reserve_overlap=false;
			for(size_t i=0;i<wood_reserve.cells.size();++i)
				if(wood_reserve.cells[i] && runtime.observation().distanceSquared(
					int(i)%runtime.observation().width,int(i)/runtime.observation().width,
					flag->posX,flag->posY)<=16) reserve_overlap=true;
			int nearby_wood=0;
			for(int dx=-4; dx<=4; ++dx)
				for(int dy=-4; dy<=4; ++dy)
					if(mi.is_resource(flag->posX+dx, flag->posY+dy, materialIndex(MaterialId::Wood)))
						nearby_wood+=1;
			const bool clearing_quota_met=
				proactive_clearing_initial_wood-nearby_wood
					>=budget.farming_clearing_quota;
			const bool clearing_timed_out=
				timer-proactive_clearing_started_tick
					>=budget.farming_clearing_duration;
			if(reserve_overlap || budget.recovery_active || nearby_wood<=1 || clearing_quota_met
				|| clearing_timed_out)
			{
				const char* reason=reserve_overlap ? "wood_reserve" : budget.recovery_active ? "starvation"
					: nearby_wood<=1 ? "cleared" : clearing_quota_met ? "quota" : "timeout";
				retire_clearing_campaign(runtime, reason,
					"\twood_remaining="+telemetryText(nearby_wood));
				last_proactive_clearing_tick=timer;
				recent_construction_failures=0;
				return telemetry.returnedBool(
					AITrace::AI7::Maxima_continue_clearing_campaign_result,
					AITrace::AI7::Maxima_continue_clearing_campaign_true, true);
			}
			runtime.add_management_order(new AssignWorkers(
				strategy.staffing.clearing_workers, proactive_clearing_flag));
			return telemetry.returnedBool(AITrace::AI7::Maxima_continue_clearing_campaign_result,
										  AITrace::AI7::Maxima_continue_clearing_campaign_true,
										  true);
		}
		if(runtime.get_building_register().is_building_pending(proactive_clearing_flag))
			return telemetry.returnedBool(AITrace::AI7::Maxima_continue_clearing_campaign_result,
										  AITrace::AI7::Maxima_continue_clearing_campaign_true,
										  true);
		proactive_clearing_flag=-1;
		last_proactive_clearing_tick=timer;
	}

	return telemetry.returnedBool(AITrace::AI7::Maxima_continue_clearing_campaign_result,
								  AITrace::AI7::Maxima_continue_clearing_campaign_true, false);
}

void Maxima::manage_land_clearing(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	telemetry.count(AITrace::AI7::Maxima_manage_land_clearing_calls);
	if(!budget.farming_enabled)
	{
		if(proactive_clearing_flag!=-1
		   && (runtime.get_building_register().is_building_found(
				proactive_clearing_flag)
			||runtime.get_building_register().is_building_pending(
				proactive_clearing_flag)))
			runtime.add_management_order(new RetireAttraction(proactive_clearing_flag,1u<<WORKER));
		proactive_clearing_flag=-1;
		farming_urgent=false;
		return;
	}
	MapInfo mi(runtime);
	const AIEngine::AIWorldView* map=&runtime.observation();
	const int w=(*map).width;
	const TeamStat* stat=&runtime.observedTeam().statistics;
	if(continue_clearing_campaign(runtime)) return;

	int maintenance_wood_tiles=0;
	for(int index=0; index<w*(*map).height; ++index)
		if(index<int(maintenance_circulation_mask.size())
		   && maintenance_circulation_mask[index]
		   && mi.is_resource(index%w, index/w, materialIndex(MaterialId::Wood)))
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

	const WoodClearingTarget target=select_wood_clearing_target(runtime);
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
		AIMaximaBuildings::WorkerAttraction, 0);
	flag_order->add_constraint(new Construction::SinglePosition(best_x, best_y));
	proactive_clearing_flag=runtime.add_building_order(flag_order);
	proactive_clearing_started_tick=timer;
	proactive_clearing_initial_wood=best_wood;
	proactive_clearing_campaigns+=1;
	RemoveArea* release_wood=new RemoveArea(ForbiddenArea);
	for(int dx=-4; dx<=4; ++dx)
	{
		for(int dy=-4; dy<=4; ++dy)
		{
			if(mi.is_resource(best_x+dx, best_y+dy, materialIndex(MaterialId::Wood))
				&& !mi.is_resource(best_x+dx, best_y+dy, materialIndex(MaterialId::Food)))
				release_wood->add_location(best_x+dx, best_y+dy);
		}
	}
	runtime.add_management_order(release_wood);
	runtime.add_management_order(new ChangeFlagSize(4, proactive_clearing_flag));
	emit_telemetry(runtime, "land_clearing_started",
		"\tflag="+telemetryText(proactive_clearing_flag)
		+"\tx="+telemetryText(best_x)
		+"\ty="+telemetryText(best_y)
		+"\twood="+telemetryText(best_wood)
		+"\tworkers="+telemetryText(clearing_workers)
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

std::vector<Uint8> Maxima::worker_reachable_circulation(Context& runtime, bool after_harvest) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const AIEngine::AIWorldView* map=&runtime.observation();
	const int w=(*map).width, h=(*map).height, size=w*h;
	// Empty pockets beside a building are not necessarily connected to workers.
	// Ignore transient units. Reserve selection may include approaches workers
	// can open by harvesting; circulation clearing needs currently empty routes.
	std::vector<Uint8> reachable(size,0);
	for(int swimming=0;swimming<=1;++swimming)
	{
		std::vector<Uint8> visited(size,0);
		std::vector<int> queue;
		for(int id=0;id<Unit::MAX_COUNT;++id)
		{
			const AIEngine::UnitView* worker=runtime.observation().unitSlots(runtime.teamNumber())[id];
			if(!worker||worker->typeNum!=WORKER
			   ||int(worker->performance[SWIM]>0)!=swimming)continue;
			const int index=(*map).normalizeY(worker->posY)*w+(*map).normalizeX(worker->posX);
			if(!visited[index]){visited[index]=1;queue.push_back(index);}
		}
		field::traverse(queue,{w,h},field::Surrounding,
			[&](int index) {
				const int x=index%w,y=index/w;
				const auto current=AIEngine::ObservationQueries::spatialTile((*map),x,y);
				const bool current_farm_area=after_harvest
					&& index<int(applied_farm_protection_mask.size())
					&& applied_farm_protection_mask[index];
				if(current.building==NOGBID && (!MapState::resourceBlocksGround(map->state(),index)
				   || current_farm_area
				   || (after_harvest && (MapState::hasMaterial(map->state(),map->tileIndex(x,y),MaterialId::Wood) || MapState::hasMaterial(map->state(),map->tileIndex(x,y),MaterialId::Food)))))
					reachable[index]=1;

				return field::Visit::Expand;
			},[&](int,int px,int py) {
				const int nx=(*map).normalizeX(px),ny=(*map).normalizeY(py),next=ny*w+nx;
				const auto tile=AIEngine::ObservationQueries::spatialTile((*map),nx,ny);
				const bool farm_area=after_harvest
					&& next<int(applied_farm_protection_mask.size())
					&& applied_farm_protection_mask[next];
				if(visited[next]||tile.building!=NOGBID
				   ||(MapState::resourceBlocksGround(map->state(),next) && !farm_area && !(after_harvest
				      && (MapState::hasMaterial(map->state(),map->tileIndex(nx,ny),MaterialId::Wood) || MapState::hasMaterial(map->state(),map->tileIndex(nx,ny),MaterialId::Food))))
				   ||(!map->state().terrainProperties(map->tileIndex(nx,ny)).walkable && !(swimming && map->state().terrainProperties(map->tileIndex(nx,ny)).swimmable))
				   ||!((map->visibilityAt(map->tileIndex(nx,ny)).discovered&(runtime.observedTeam().allies))!=0)
				   ||(((map->areasAt(map->tileIndex(nx,ny)).forbidden&(runtime.observedTeam().mask))!=0)
				      && !farm_area && !development_planner.isCirculationReserved(next)))return;
				visited[next]=1;queue.push_back(next);
			});
	}

	return reachable;
}

std::vector<std::vector<int> > Maxima::reservation_member_footprints(
	Context& runtime, const AIMaximaPlacement::Reservation& contract) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const AIEngine::AIWorldView* map=&runtime.observation();
	const int w=(*map).width;
	std::vector<std::vector<int> > members;
	auto add_member=[&](int buildingId,
		const AIMaximaPlacement::DevelopmentAction* action)
	{
		const AIEngine::BuildingView* building=runtime.get_building_register().get_building(buildingId);
		int x,y,width,height;
		if(building&&action&&action->type==AIMaximaPlacement::UpgradeBuilding
		   &&action->state==AIMaximaPlacement::ParcelReserved)
		{
			const int targetId=AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).nextLevel;
			if(targetId<0)return;
			const BuildingType* target=&runtime.observation().catalog->at(targetId).resolvedType;
			x=action->centerX+target->decLeft;y=action->centerY+target->decTop;
			width=target->width;height=target->height;
		}
		else if(building)
		{
			x=building->posX;y=building->posY;
			width=AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).width;height=AIEngine::ObservationQueries::buildingType(runtime.observation(),*building).height;
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
			footprint.push_back((*map).normalizeY(y+dy)*w+(*map).normalizeX(x+dx));
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
	Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const AIEngine::AIWorldView* map=&runtime.observation();
	const int w=(*map).width;
	const int h=(*map).height;
	const int size=w*h;
	MaintenanceClearingPlan plan(size);
	const WoodReserve wood_reserve=select_wood_reserve(runtime);
	const bool enabled=budget.farming_enabled
		&& budget.farming_maintenance_clearing_enabled;

	// Snapshot resources only when a circulation contract starts. Once the
	// resource is consumed, the applied mask latches that tile clear.
	std::vector<Uint8> grandfathered_resource(size, 0);
	std::vector<int> resource_burden(size, 0);
	for(int index=0; index<size; ++index)
	{
		const auto resource=map->resourceAt(index).resource;
		grandfathered_resource[index]=!applied_maintenance_clearing_mask[index]
			&& ((MapState::materialAmountAt(map->state(),index,MaterialId::Food)>0) || (MapState::materialAmountAt(map->state(),index,MaterialId::Wood)>0));
		resource_burden[index]=std::max(1, int(resource.amount));
	}
	auto retain_circulation=[&](const std::vector<int>& tiles)
	{
		for(size_t i=0; i<tiles.size(); ++i)
			if(tiles[i]>=0 && tiles[i]<size)
				plan.circulation[tiles[i]]=1;
	};

	const std::vector<Uint8> reachable=enabled
		&& budget.farming_resource_preserving_circulation_enabled
		? worker_reachable_circulation(runtime) : std::vector<Uint8>(size, 0);

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
				reservation_member_footprints(runtime, contract);
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
					const int neighbor=(*map).normalizeY(index/w+dy)*w+(*map).normalizeX(index%w+dx);
					if(std::binary_search(contract.footprintTiles.begin(),
						contract.footprintTiles.end(),neighbor))circulation.push_back(neighbor);
				}
			}
			std::sort(circulation.begin(),circulation.end());
			circulation.erase(std::unique(circulation.begin(),circulation.end()),circulation.end());
			circulation.erase(std::remove_if(circulation.begin(),circulation.end(),
				[&](int index){const auto cellType=map->resourceAt(index).resource.type;
					return memberMask[index]||map->occupancyAt(index).building!=NOGBID||!map->state().terrainProperties(index).walkable
						||!((map->visibilityAt(map->tileIndex(index%w,index/w)).discovered&(runtime.observedTeam().allies))!=0)
						||(MapState::resourceBlocksGround(map->state(),index)
						   &&!map->state().resourceProperties(cellType).clearable);
				}),circulation.end());
			if(budget.farming_resource_preserving_circulation_enabled)
			{
				std::vector<Uint8> preserved=grandfathered_resource;
				// Old saves may have maintained the whole future footprint. Do not
				// grandfather those destructive clearing obligations into this plan.
				for(int index:contract.footprintTiles)if(!memberMask[index])
				{
					preserved[index]=(MapState::materialMaskAt(map->state(),index)&((1u<<materialIndex(MaterialId::Food))|(1u<<materialIndex(MaterialId::Wood))))!=0;
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

	const std::vector<Uint8> managed=farm_management_area(runtime,
		budget.farming_management_radius);
	for(int index=0; index<size; ++index)
	{
		const int x=index%w;
		const int y=index/w;
		const auto cell=AIEngine::ObservationQueries::spatialTile((*map),x, y);
		const bool discovered=AIEngine::ObservationQueries::discovered((*map),x, y,
			runtime.observedTeam().mask);
		if(wheat_invasion_clearing_required(runtime, index,
			wheat_farm_protection_mask, wood_reserve))
		{
			plan.circulation[index]=1;
			++plan.wheat_invasion_wood;
		}
		const bool permanent_resource=cell.resource.type!=NO_RES_TYPE
			&& !map->state().resourceProperties(cell.resource.type).clearable;
		const bool wants_firebreak=enabled
			&& !wood_reserve.cells[index]
			&& managed[index]
			&& budget.farming_wood_firebreak_enabled
			&& discovered && MapState::terrainSupportsMaterial(map->state(),map->tileIndex(index%w,index/w),MaterialId::Wood)
			&& cell.building==NOGBID
			&& !permanent_resource && !MapState::materialAmountAt(map->state(),size_t(index),MaterialId::Food)
            && MapState::materialAmountAt(map->state(),size_t(index),MaterialId::Wood)>0
            && MapState::materialExpansionRate(map->state(),size_t(index),MaterialId::Wood)>0
			&& Farming::fertilityWithinPercentBand(fertility_cache.at(x, y),
				strategy.farming.wood_firebreak_fertility_min_percent,
				strategy.farming.wood_firebreak_fertility_max_percent);
		plan.firebreak[index]=wants_firebreak;
		if(wants_firebreak)
		{
			++plan.firebreak_tiles;
			plan.firebreak_wood+=(MapState::materialAmountAt(map->state(),size_t(index),MaterialId::Wood)>0);
		}
	}
	return plan;
}

void Maxima::apply_maintenance_clearing_plan(Context& runtime,
	const MaintenanceClearingPlan& plan)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const AIEngine::AIWorldView* map=&runtime.observation();
	const int w=(*map).width;
	const int size=w*(*map).height;
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
		const bool discovered=AIEngine::ObservationQueries::discovered((*map),x, y,
			runtime.observedTeam().mask);
		const bool building_footprint=(*map).occupancyAt((*map).tileIndex(x, y)).building!=NOGBID;
		const bool contract_desired=plan.circulation[index]
			&& !building_footprint && discovered;
		const bool desired=!building_footprint
			&& (contract_desired || plan.firebreak[index]);
		const bool actual=AIEngine::ObservationQueries::clearing(*map,x, y, runtime.observedTeam().mask);
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
			if(contract_desired && AIEngine::ObservationQueries::forbidden(*map,x, y,
				runtime.observedTeam().mask))
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
	if(added) runtime.add_management_order(additions); else delete additions;
	if(removed) runtime.add_management_order(removals); else delete removals;
	if(released) runtime.add_management_order(forbidden_releases);
	else delete forbidden_releases;
	if(added || removed || released || timer%1000<budget.farming_normal_interval)
		emit_telemetry(runtime, "maintenance_clearing",
			"\tretained="+telemetryText(retained)
			+"\tadded="+telemetryText(added)
			+"\tremoved="+telemetryText(removed)
			+"\tfirebreak="+telemetryText(plan.firebreak_tiles)
			+"\tfirebreak_wood="+telemetryText(plan.firebreak_wood)
			+"\twheat_invasion_wood="+telemetryText(
				plan.wheat_invasion_wood)
			+"\treservation_resources_preserved="
				+telemetryText(
					plan.reservation_resources_preserved)
			+"\treservation_fallback_entrances="
				+telemetryText(
					plan.reservation_fallback_entrances)
			+"\tforbidden_released="
				+telemetryText(released));
}

void Maxima::update_maintenance_clearing_areas(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	initialize_farming_cache(runtime);
	const MaintenanceClearingPlan plan=build_maintenance_clearing_plan(runtime);
	apply_maintenance_clearing_plan(runtime, plan);
}
void Maxima::initialize_farming_cache(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const AIEngine::AIWorldView* map=&runtime.observation();
	const int w=(*map).width;
	const int h=(*map).height;
	if(fertility_cache.validFor(w, h, (*map).terrainRevision)
	   && farming_shoreline_mask.size()==size_t(w*h)
	   && farming_cardinal_shoreline_mask.size()==size_t(w*h)
	   && applied_farm_protection_mask.size()==size_t(w*h)
	   && applied_maintenance_clearing_mask.size()==size_t(w*h)
	   && wood_firebreak_mask.size()==size_t(w*h)
	   && wheat_farm_protection_mask.size()==size_t(w*h))
		return;
	const std::chrono::steady_clock::time_point started=
		std::chrono::steady_clock::now();
    fertility_cache.assign(map->growth->landField(), (*map).terrainRevision);
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
			if(AIEngine::ObservationQueries::discovered((*map),x, y, runtime.observedTeam().mask)
			   && AIEngine::ObservationQueries::forbidden(*map,x, y, runtime.observedTeam().mask))
				applied_farm_protection_mask[y*w+x]=1;
			if(AIEngine::ObservationQueries::discovered((*map),x, y, runtime.observedTeam().mask)
			   && AIEngine::ObservationQueries::clearing(*map,x, y, runtime.observedTeam().mask))
				applied_maintenance_clearing_mask[y*w+x]=1;
		}
	const long long elapsed=std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now()-started).count();
	emit_telemetry(runtime, "farming_fertility_cache",
		"\tmicroseconds="+telemetryText(elapsed)
		+"\tpath="+(fertility_cache.pathUsed()==Farming::SandCorrectionFertilityPath
			? "sand_correction" : "water_splat")
		+"\twater="+telemetryText(fertility_cache.waterCount())
		+"\tsand="+telemetryText(fertility_cache.sandCount()));
}








int Maxima::available_expansion_neighbors(Context& runtime, int x, int y) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const AIEngine::AIWorldView* map=&runtime.observation();
	int available=0;
	for(int dy=-1; dy<=1; ++dy)
		for(int dx=-1; dx<=1; ++dx)
		{
			if(!dx && !dy) continue;
			const auto cellIndex=map->tileIndex(x+dx, y+dy);
			if(map->resourceAt(map->tileIndex(x,y)).resource.type!=NO_RES_TYPE
               && MapState::terrainSupportsResource(map->state(),map->tileIndex(x+dx,y+dy),static_cast<ResourceId>(map->resourceAt(map->tileIndex(x,y)).resource.type)) && MapState::resourcesMayGrow(map->state(),map->tileIndex(x+dx,y+dy))
			   && map->resourceAt(cellIndex).resource.type==NO_RES_TYPE && map->occupancyAt(cellIndex).building==NOGBID
			   && map->occupancyAt(cellIndex).groundUnit==NOGUID && map->occupancyAt(cellIndex).airUnit==NOGUID)
				available+=1;
		}
	return available;
}

int Maxima::growth_absorbing_neighbors(Context& runtime, int x, int y) const
{
	const AIEngine::AIWorldView* map=&runtime.observation();

    const auto& donor=map->resourceAt(map->tileIndex(x,y)).resource;
    if(donor.type==NO_RES_TYPE || !AIResourcePolicy::canPropagate(map->state(),map->tileIndex(x,y),MaterialId::Food)) return 0;
    const auto id=static_cast<ResourceId>(donor.type);
    const auto& yield=(*map->state().resourceRegistry).yields(id)[materialIndex(MaterialId::Food)];

	int available=0;
	for(int dy=-1; dy<=1; ++dy)
		for(int dx=-1; dx<=1; ++dx)
		{
			if(!dx && !dy) continue;
			const auto cellIndex=map->tileIndex(x+dx, y+dy);
			if(!MapState::terrainSupportsResource(map->state(),map->tileIndex(x+dx,y+dy),id) || !MapState::resourcesMayGrow(map->state(),map->tileIndex(x+dx,y+dy))
			   || map->occupancyAt(cellIndex).building!=NOGBID) continue;
			// Growth either seeds empty ground or tops up a partly harvested
			// stack beside it. A full stack absorbs nothing, so a protected
			// cell hemmed in by full wheat yields nothing either.
			// Passing units are deliberately ignored: they move every tick and
			// would make a standing supply estimate flicker.
			if(map->resourceAt(cellIndex).resource.type==NO_RES_TYPE
			   || (map->resourceAt(cellIndex).resource.type==donor.type && yield.growthRate>0
                   && MapState::materialAmountAt(map->state(),map->tileIndex(x+dx,y+dy),MaterialId::Food)<yield.capacity))
				available+=1;
		}
	return available;
}

void Maxima::release_farming_protection(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	MapInfo map_info(runtime);
	const AIEngine::AIWorldView* map=&runtime.observation();
	std::fill(farm_protection_mask.begin(), farm_protection_mask.end(), 0);
	std::fill(wheat_farm_protection_mask.begin(),
		wheat_farm_protection_mask.end(), 0);
	RemoveArea* removals=new RemoveArea(ForbiddenArea);
	RemoveArea* farm_removals=map->farmAreasEnabled ? new RemoveArea(FarmArea) : nullptr;
	int farms_removed=0;
	int removed=0;
	for(int index=0; index<(*map).width*(*map).height; ++index)
	{
		const int x=index%(*map).width;
		const int y=index/(*map).width;
		if(applied_farm_protection_mask[index] && map_info.is_forbidden_area(x, y))
		{
			removals->add_location(x, y);
			++removed;
		}
		if(farm_removals && AIEngine::ObservationQueries::farmed(*map,x, y, runtime.observedTeam().mask))
		{
			farm_removals->add_location(x, y);
			++farms_removed;
		}
		applied_farm_protection_mask[index]=0;
	}
	if(removed) runtime.add_management_order(removals); else delete removals;
	if(farms_removed) runtime.add_management_order(farm_removals); else delete farm_removals;
	farming_urgent=false;
}

bool Maxima::has_hard_farming_contract(int index) const
{
	return development_planner.isFootprintReserved(index)
		|| development_planner.isCirculationReserved(index);
}

Maxima::WoodReserve Maxima::select_wood_reserve(Context& runtime, const std::vector<Uint8>* seedEligibility) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const AIEngine::AIWorldView* map=&runtime.observation();
	const int w=(*map).width, h=(*map).height;
	WoodReserve reserve(w*h);
	// No regrowth: protecting seed cells would permanently withhold finite wheat.
	if(runtime.observation().configuration->isResourceGrowthDisabled()
		|| !budget.farming_enabled || !budget.farming_protection_enabled) return reserve;
	// Placement can query before the farming cache has been rebuilt on load.
	// Derive the same exact fertility without retaining new simulation state.
	Farming::ExactFertilityCache rebuilt;
	const Farming::ExactFertilityCache* fertility=&fertility_cache;
	if(!fertility_cache.validFor(w,h,(*map).terrainRevision))
	{
        rebuilt.assign(map->growth->landField(), (*map).terrainRevision);
        fertility=&rebuilt;
	}
	const auto managed=farm_management_area(runtime,budget.farming_management_radius);
	const auto reachable=worker_reachable_circulation(runtime,true);
	auto eligible=[&](int i,bool donor=false)
	{
		const auto cellIndex=map->tileIndex(i%w,i/w);
		return managed[i] && MapState::terrainSupportsMaterial(map->state(),map->tileIndex(i%w,i/w),MaterialId::Wood) && (donor || MapState::resourcesMayGrow(map->state(),map->tileIndex(i%w,i/w)))
			&& map->occupancyAt(cellIndex).building==NOGBID && !has_hard_farming_contract(i)
			&& ((map->visibilityAt(map->tileIndex(i%w,i/w)).discovered&(runtime.observedTeam().mask))!=0)
			&& (map->resourceAt(cellIndex).resource.type==NO_RES_TYPE || (MapState::materialAmountAt(map->state(),size_t(i),MaterialId::Wood)>0)
				|| (MapState::materialAmountAt(map->state(),size_t(i),MaterialId::Food)>0));
	};
	auto externally_forbidden=[&](int i)
	{
		return AIEngine::ObservationQueries::forbidden(*map,i%w,i/w,runtime.observedTeam().mask)
			&& !applied_farm_protection_mask[i];
	};
	std::vector<int> candidates;
	for(int i=0;i<w*h;++i)
		if(eligible(i,true) && MapState::hasMaterial(map->state(),map->tileIndex(i%w,i/w),MaterialId::Wood)
		   && (seedEligibility ? ((*seedEligibility)[i]&2)!=0 : is_spreading_seed(*map,i%w,i/w,materialIndex(MaterialId::Wood)))
		   && !externally_forbidden(i) && (fertility->at(i%w,i/w)>0 || !uses_land_fertility(*map,i%w,i/w)))
			candidates.push_back(i);
	// Fixed terrain scores avoid moving the reserve on every harvest or refill.
	// A better candidate must already contain live wood before replacing one.
	std::sort(candidates.begin(),candidates.end(),[&](int a,int b) {
		const bool al=Farming::isExpansionCell(a%w,a/w),bl=Farming::isExpansionCell(b%w,b/w);
		if(al!=bl) return al;
		const Uint32 af=fertility->at(a%w,a/w),bf=fertility->at(b%w,b/w);
		return af!=bf ? af>bf : a<b;
	});
	for(int seed:candidates)
	{
		if(reserve.seeds==2) break;
		if(reserve.cells[seed]) continue;
		uint16_t ring=0;
		for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
		{
			const int next=(*map).normalizeY(seed/w+dy)*w+(*map).normalizeX(seed%w+dx);
			if(reachable[next] && reserve.cells[next]!=1) ring|=1<<((dy+1)*3+dx+1);
		}
		if(!Farming::canProtectWithoutSplittingAccess(ring)) continue;
		int outlet=-1;
		for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
		{
			if(!dx && !dy) continue;
			const int x=(*map).normalizeX(seed%w+dx),y=(*map).normalizeY(seed/w+dy),i=y*w+x;
			// Keep harvest outlets off the seed lattice. This also prevents
			// an outlet growing wood from displacing its own aligned donor.
			if(Farming::isExpansionCell(x,y) || reserve.cells[i]
			   || !eligible(i) || externally_forbidden(i)
                   || !MapState::terrainSupportsResource(map->state(),size_t(i),static_cast<ResourceId>(map->resourceAt(size_t(seed)).resource.type))) continue;
			bool access=false;
			for(int ay=-1;ay<=1;++ay)for(int ax=-1;ax<=1;++ax)
				if(ax || ay)
				{
					const int next=(*map).normalizeY(y+ay)*w+(*map).normalizeX(x+ax);
					if(next!=seed && reserve.cells[next]!=1 && reachable[next]) access=true;
				}
			if(access && (outlet<0 || i<outlet)) outlet=i;
		}
		if(outlet<0) continue;
		reserve.cells[seed]=1;
		reserve.cells[outlet]=2;
		++reserve.seeds;
	}
	return reserve;
}

bool Maxima::wheat_invasion_clearing_required(Context& runtime, int index,
	const std::vector<Uint8>& protected_wheat, const WoodReserve& wood_reserve) const
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const AIEngine::AIWorldView* map=&runtime.observation();
	const int w=(*map).width, h=(*map).height;
	const int x=index%w, y=index/w;
    if(!budget.farming_enabled || !budget.farming_maintenance_clearing_enabled
        || !budget.farming_wheat_invasion_clearing_enabled
        || !((map->visibilityAt(map->tileIndex(x,y)).discovered&(runtime.observedTeam().mask))!=0)
        || !MapState::hasMaterial(map->state(),map->tileIndex(x,y),MaterialId::Wood) || wood_reserve.cells[index]
        || !MapState::materialExpansionRate(map->state(),size_t(index),MaterialId::Wood)) return false;
    const auto id=static_cast<ResourceId>(map->resourceAt(map->tileIndex(x,y)).resource.type);
    if(!map->state().resourceProperties(resourceIndex(id)).clearable
        || MapState::materialAmountAt(map->state(),size_t(index),MaterialId::Food)) return false;
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
        if((dx || dy) && protected_wheat[map->tileIndex(x+dx,y+dy)]
            && MapState::resourcesMayGrow(map->state(),map->tileIndex(x+dx,y+dy))
            && MapState::terrainSupportsResource(map->state(),map->tileIndex(x+dx,y+dy),id)) return true;
    return false;
}

Maxima::FarmProtectionPlan Maxima::build_farming_protection_plan(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	const AIEngine::AIWorldView* map=&runtime.observation();
	MapInfo map_info(runtime);
	const int w=(*map).width;
	const int h=(*map).height;
	FarmProtectionPlan plan(w*h);
	// The plan is read-only with respect to deposits and ecology. Snapshot the
    // two relevant eligibility bits once, rather than querying growth for every
    // neighboring classifier and component edge. This cache never survives a plan.
    std::vector<Uint8> seedEligibility(w*h,0);
    for(int index=0;index<w*h;++index)
    {
        const auto& deposit=map->resourceAt(size_t(index)).resource;
        if(deposit.type==NO_RES_TYPE || !deposit.amount) continue;
        const auto& properties=map->state().resourceProperties(deposit.type);
        if(!properties.spreadRate) continue;
        if((properties.materialMask&materialBit(MaterialId::Food))
            && is_spreading_seed(*map,index%w,index/w,materialIndex(MaterialId::Food))) seedEligibility[index]|=1;
        if((properties.materialMask&materialBit(MaterialId::Wood))
            && is_spreading_seed(*map,index%w,index/w,materialIndex(MaterialId::Wood))) seedEligibility[index]|=2;
    }
    plan.wood_reserve=select_wood_reserve(runtime,&seedEligibility);
	plan.wood_pressure=budget.farming_wood_pressure;
	plan.wood_fertility=budget.farming_minimum_wood_fertility;
	int clearing_x=0, clearing_y=0;
	const bool clearing_wood=!budget.recovery_active
		&& proactive_clearing_flag>=0
		&& runtime.get_building_position(proactive_clearing_flag,clearing_x,clearing_y);

	const std::vector<Uint8> wheat_exterior=wheat_farm_exterior(map);

	// Record which empty tiles can extend a live wheat or wood resource. This
	// avoids running the more expensive classifier for unrelated map cells.
    const auto adjacent_resource_mask=FarmGeometry::adjacentSeeds(seedEligibility,w,h);

	for(int y=0; y<h; ++y)
		for(int x=0; x<w; ++x)
		{
			const int index=y*w+x;
            // Cells without either a seed or an adjacent seed cannot acquire a
            // role below. Reject them before touching tile/ecology properties.
            const Uint8 seeds=seedEligibility[index];
            const Uint8 adjacent=adjacent_resource_mask[index];
            if(!(seeds|adjacent)) continue;
            const bool wheat=seeds&1;
            const bool wood=seeds&2;
            const bool empty_growth=adjacent && is_empty_growth_cell(*map,x,y);

			FarmTileClassification wheat_role;
			FarmTileClassification wood_role;
            if(wheat || (empty_growth && (adjacent&1)))
                wheat_role=classify_farm_tile(map_info,map,fertility_cache,wheat_exterior,seedEligibility,
                    farming_shoreline_mask[index]!=0,x,y,materialIndex(MaterialId::Food),
                    Uint32(budget.farming_wheat_fertility_min));
            if(wood || (empty_growth && (adjacent&2)))
                wood_role=classify_farm_tile(map_info,map,fertility_cache,wheat_exterior,seedEligibility,
                    farming_shoreline_mask[index]!=0,x,y,materialIndex(MaterialId::Wood),plan.wood_fertility);
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
				&& (*map).distanceSquared(x,y,clearing_x,clearing_y)<=4*4)
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
				const int available=available_expansion_neighbors(runtime, x, y);
				plan.blocked_directions+=8-available;
				plan.expected_capacity+=std::uint64_t(MapState::materialExpansionRate(map->state(),map->tileIndex(x,y),wheat?materialIndex(MaterialId::Food):materialIndex(MaterialId::Wood)))*available/24;
			}
		}
	// Empty frontier protection cannot regrow a patch after its last live crop
	// is harvested. The sparse pattern may leave a boundary cell open
	// while that same cell suppresses its neighbor's local bootstrap. Select one
	// eligible anchor only in components with no protected live resource.
	std::vector<Uint8> visited(w*h,0);
	for(const int resource:{materialIndex(MaterialId::Food),materialIndex(MaterialId::Wood)})
    for(int start=0;start<w*h;++start)
	{
        const Uint8 visitBit=resource==materialIndex(MaterialId::Food)?1:2;
        if((visited[start]&visitBit) || !cached_seed(seedEligibility,*map,start%w,start/w,resource)) continue;
		std::vector<int> component(1,start);
		visited[start]|=visitBit;
		bool protected_live=false;
		int anchor=-1;
		field::traverse(component,{w,h},field::Surrounding,
			[&](int index) {
				const int x=index%w,y=index/w;
				const auto cell=AIEngine::ObservationQueries::spatialTile((*map),x,y);
				if(cell.resource.amount>0)
				{
					protected_live|=plan.forbidden[index]!=0;
					const Uint32 minimum=resource==materialIndex(MaterialId::Food)
						? Uint32(budget.farming_wheat_fertility_min) : plan.wood_fertility;
					if((fertility_cache.at(x,y)>=minimum || !uses_land_fertility(*map,x,y))
					   && !has_hard_farming_contract(index)
					   && !(resource==materialIndex(MaterialId::Wood) && ((budget.farming_wood_firebreak_enabled
						   && wood_firebreak_mask[index]) || (clearing_wood
						   && (*map).distanceSquared(x,y,clearing_x,clearing_y)<=4*4)))
					   && (anchor<0 || index<anchor))
						anchor=index;
				}

				return field::Visit::Expand;
			},[&](int,int px,int py) {
				const int nx=(*map).normalizeX(px), ny=(*map).normalizeY(py);
				const int next=ny*w+nx;
				if(!(visited[next]&visitBit)
				   && cached_seed(seedEligibility,*map,nx,ny,resource))
				{
					visited[next]|=visitBit;
					component.push_back(next);
				}
			});
		if(protected_live || anchor<0) continue;
		const int x=anchor%w, y=anchor/w;
		plan.forbidden[anchor]=1;
		plan.protected_wheat[anchor]=resource==materialIndex(MaterialId::Food);
		if(resource==materialIndex(MaterialId::Food)) ++plan.protected_wheat_bootstraps;
		else ++plan.protected_wood_bootstraps;
		plan.protected_seeds+=(x&1) && (y&1);
		const int available=available_expansion_neighbors(runtime,x,y);
		plan.blocked_directions+=8-available;
		plan.expected_capacity+=std::uint64_t(MapState::materialExpansionRate(map->state(),map->tileIndex(x,y),resource))*available/24;
	}
	// The reserve precedes ordinary wheat/wood patterns and clearing campaigns.
	for(int i=0;i<w*h;++i)if(plan.wood_reserve.cells[i])
	{
		plan.forbidden[i]=plan.wood_reserve.cells[i]==1;
		plan.protected_wheat[i]=0;
	}
	return plan;
}

void Maxima::resolve_wheat_invasion_clearing(Context& runtime,
	FarmProtectionPlan& plan)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	// Use this pass's final wheat mask, including fallback seeds and coastal
	// openings. Reading last pass's maintenance mask would lag both new and
	// revoked obligations and allow the two reconcilers to undo each other.
	if(!budget.farming_maintenance_clearing_enabled
	   || !budget.farming_wheat_invasion_clearing_enabled) return;
	for(size_t index=0; index<plan.forbidden.size(); ++index)
		if(wheat_invasion_clearing_required(runtime, int(index), plan.protected_wheat, plan.wood_reserve))
			plan.forbidden[index]=0;
}

void Maxima::apply_farming_protection(Context& runtime,
	const FarmProtectionPlan& plan, int& added, int& removed)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	MapInfo map_info(runtime);
	const AIEngine::AIWorldView* map=&runtime.observation();
	const int w=(*map).width;
	const int size=w*(*map).height;
	AddArea* additions=new AddArea(ForbiddenArea);
	RemoveArea* removals=new RemoveArea(ForbiddenArea);
	const bool farms=map->farmAreasEnabled;
	AddArea* farm_additions=farms ? new AddArea(FarmArea) : nullptr;
	RemoveArea* farm_removals=farms ? new RemoveArea(FarmArea) : nullptr;
	bool farm_added=false, farm_removed=false, forbidden_added=false, forbidden_removed=false;
	added=0;
	removed=0;
	for(int index=0; index<size; ++index)
	{
		const int x=index%w;
		const int y=index/w;
		if(!map_info.is_discovered(x, y)) continue;
		bool wheat=false, preserveWood=false;
        if(farms && plan.protected_wheat[index])
        {
            const auto& deposit=map->resourceAt(map->tileIndex(x,y)).resource;
            const bool empty=deposit.type==NO_RES_TYPE;
            const auto foodYield=empty ? YieldProperties{} : (*map->state().resourceRegistry).yields(static_cast<ResourceId>(deposit.type))[materialIndex(MaterialId::Food)];
            wheat=(empty || (map->state().resourceProperties(deposit.type).farmable
                && foodYield.consumption==ResourceConsumption::One && !foodYield.destroysDeposit))
                && MapState::canPaintFarmArea(map->state(),x,y);
            preserveWood=wheat && is_spreading_seed(*map,x,y,materialIndex(MaterialId::Wood));
        }
		if(farms)
		{
			const bool wanted=plan.forbidden[index] && wheat && map->canPaintFarmAt(map->tileIndex(x,y));
			const bool farmed=AIEngine::ObservationQueries::farmed(*map,x, y, runtime.observedTeam().mask);
			if(wanted && !farmed)
			{
				farm_additions->add_location(x, y);
				farm_added=true;
				++added;
			}
			else if(!wanted && farmed)
			{
				farm_removals->add_location(x, y);
				farm_removed=true;
				++removed;
			}
		}
		const bool actual=map_info.is_forbidden_area(x, y);
		const bool forbidden=plan.forbidden[index]
			&& !(farms && wheat && !preserveWood);
		if(forbidden && !actual)
		{
			additions->add_location(x, y);
			forbidden_added=true;
			++added;
		}
		else if(!forbidden && applied_farm_protection_mask[index] && actual)
		{
			removals->add_location(x, y);
			forbidden_removed=true;
			++removed;
		}
		applied_farm_protection_mask[index]=forbidden;
	}
	if(forbidden_added) runtime.add_management_order(additions); else delete additions;
	if(forbidden_removed) runtime.add_management_order(removals); else delete removals;
	if(farms)
	{
		if(farm_added) runtime.add_management_order(farm_additions); else delete farm_additions;
		if(farm_removed) runtime.add_management_order(farm_removals); else delete farm_removals;
	}
	farm_protection_mask=plan.forbidden;
	wheat_farm_protection_mask=plan.protected_wheat;
}

void Maxima::update_farming(Context& runtime)
{
    auto ownerObservation=runtime.scopeOwnerObservation();
	telemetry.count(AITrace::AI7::Maxima_update_farming_calls);
	const std::chrono::steady_clock::time_point started=
		std::chrono::steady_clock::now();
	initialize_farming_cache(runtime);
	// No regrowth: protecting seed cells would permanently withhold finite wheat.
	if(runtime.observation().configuration->isResourceGrowthDisabled()
		|| !budget.farming_enabled || !budget.farming_protection_enabled)
	{
		release_farming_protection(runtime);
		return;
	}
	FarmProtectionPlan plan=build_farming_protection_plan(runtime);
	// Establish/expand farms near allied infrastructure, never around flags.
	// Recompute proximity as buildings change, but retain established odd/odd
	// wheat cells after losing an anchor, matching the existing radius rule.
	// This is an establishment limit, not an override for tactical clearing.
	if(budget.farming_management_radius>0)
	{
		const AIEngine::AIWorldView* map=&runtime.observation();
		const int w=(*map).width, h=(*map).height;
		const int radius=budget.farming_management_radius;
		const std::vector<Uint8> nearby=farm_management_area(runtime,radius);
		for(int i=0;i<w*h;++i)
		{
			if(nearby[i])continue;
			const bool established= farm_protection_mask[i]
				&& Farming::isInteriorSeed(i%w,i/w)
				&& MapState::hasMaterial(map->state(),map->tileIndex(i%w,i/w),MaterialId::Food);
			if(established)continue;
			plan.forbidden[i]=0;
			plan.protected_wheat[i]=0;
		}
	}

	resolve_wheat_invasion_clearing(runtime, plan);
	int added=0;
	int removed=0;
	apply_farming_protection(runtime, plan, added, removed);
	// This pass satisfies the clearing campaign's urgent farming request;
	// subsequent evaluations resume the ordinary cadence.
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
		emit_telemetry(runtime, "farming_policy",
		"\tmicroseconds="+telemetryText(elapsed)
		+"\twood_reserve_seeds="+telemetryText(plan.wood_reserve.seeds)
		+"\tprotected_seeds="+telemetryText(plan.protected_seeds)
		+"\tprotected_frontier="+telemetryText(plan.protected_frontier)
		+"\tprotected_wheat_edges="+telemetryText(plan.protected_wheat_edges)
		+"\tprotected_wheat_bootstraps="+telemetryText(plan.protected_wheat_bootstraps)
		+"\tprotected_wood_edges="+telemetryText(plan.protected_wood_edges)
		+"\tprotected_wood_bootstraps="+telemetryText(plan.protected_wood_bootstraps)
		+"\tprotected_interior_seeds="+telemetryText(plan.protected_interior_seeds)
		+"\texpected_capacity="+telemetryText(plan.expected_capacity)
		+"\tblocked_directions="+telemetryText(plan.blocked_directions)
		+"\twood_pressure="+telemetryText(plan.wood_pressure)
		+"\twood_fertility="+telemetryText(plan.wood_fertility)
		+"\tadded="+telemetryText(added)
		+"\tremoved="+telemetryText(removed));
}

}
