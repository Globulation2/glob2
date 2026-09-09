/* Maxima farming, barrier, and clearing policy. */

#include "AIMaxima.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "Unit.h"
#include "boost/lexical_cast.hpp"

#include <algorithm>
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
    // Redundancy belongs to each local settlement perimeter. Coast fragments
    // share a pair; allocating a pair per fragment would perforate the defence.
    const int STRATEGIC_GATE_COUNT=2;
    const int STRATEGIC_GATE_WIDTH=3;
    const int FARMING_UNREACHABLE=-1;

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

	// Terrain-only classification: do not use resources, forbidden areas or
	// discovery here. Those change during play and must not make the access
	// policy alternate between mainland and island behavior.
	std::vector<Uint8> home_growing_region(Map* map, int start_x, int start_y,
		bool has_start)
	{
		const int w=map->getW(), h=map->getH(), size=w*h;
		// Mainland farming is not an island-access problem. Use terrain alone,
		// anchored at the team's starting location. Sand does not join separate
		// growing areas, even though workers can cross it. Harvesting, discovery and
		// construction cannot change which growing area gets exceptional handling.
		std::vector<Uint8> home_land(size, 0);
		std::vector<int> home_queue;
		if(has_start)
		{
			const int start=map->normalizeY(start_y)*w
				+map->normalizeX(start_x);
			if(map->getTile(start%w,start/w).terrain<16)
			{home_land[start]=1;home_queue.push_back(start);}
		}
		for(size_t head=0;head<home_queue.size();++head)
			for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
			{
				const int index=home_queue[head];
				const int next=((index/w+dy+h)%h)*w+(index%w+dx+w)%w;
				if(!home_land[next] && map->getTile(next%w,next/w).terrain<16)
				{home_land[next]=1;home_queue.push_back(next);}
			}
		return home_land;
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

    void add_preemptive_hash(Uint32& signature, Uint32 value)
    {
        signature^=value;
        signature*=16777619u;
    }

    void compute_preemptive_distance_field(int w, int h,
        const std::vector<Uint8>& walkable, const std::vector<int>& sources,
        std::vector<int>& distances)
    {
        distances.assign(w*h, FARMING_UNREACHABLE);
        std::deque<int> queue;
        for(std::vector<int>::const_iterator source=sources.begin();
            source!=sources.end(); ++source)
        {
            if(*source<0 || *source>=w*h || !walkable[*source]
               || distances[*source]!=FARMING_UNREACHABLE)
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
                for(int dx=-1; dx<=1; ++dx)
                {
                    if(!dx && !dy) continue;
                    const int neighbor=((y+dy+h)%h)*w+((x+dx+w)%w);
                    if(walkable[neighbor]
                       && distances[neighbor]==FARMING_UNREACHABLE)
                    {
                        distances[neighbor]=distances[index]+1;
                        queue.push_back(neighbor);
                    }
                }
        }
    }

	struct GateChannel
	{
		std::vector<int> clearing_tiles;
		std::vector<int> defense_points;
	};

	GateChannel build_gate_channel(const std::vector<int>& gate, int w, int h,
		const std::vector<Uint8>& walkable,
		const std::vector<int>& home_distance)
	{
		GateChannel channel;
		channel.clearing_tiles=gate;
		int cursor=gate[0];
		for(size_t i=1; i<gate.size(); ++i)
			if(home_distance[gate[i]]<home_distance[cursor])
				cursor=gate[i];
		// The old seven-tile limit left the tail inside deep farms. Descending
		// to a building entrance makes the passage a complete access contract.
		for(int step=1; step<=w*h; ++step)
		{
			int best=cursor;
			const int x=cursor%w;
			const int y=cursor/w;
			for(int dy=-1; dy<=1; ++dy)
				for(int dx=-1; dx<=1; ++dx)
				{
					if(!dx && !dy) continue;
					const int next=((y+dy+h)%h)*w+((x+dx+w)%w);
					if(walkable[next] && home_distance[next]>=0
					   && home_distance[next]<home_distance[best])
						best=next;
				}
			if(best==cursor) break;
			const int best_x=best%w;
			const int best_y=best/w;
			int move_x=best_x-x;
			int move_y=best_y-y;
			if(move_x>w/2) move_x-=w;
			if(move_x<-w/2) move_x+=w;
			if(move_y>h/2) move_y-=h;
			if(move_y<-h/2) move_y+=h;
			cursor=best;
			for(int offset=-1; offset<=1; ++offset)
			{
				const int lane_x=(best_x-offset*move_y+w)%w;
				const int lane_y=(best_y+offset*move_x+h)%h;
				const int lane=lane_y*w+lane_x;
				if(walkable[lane]) channel.clearing_tiles.push_back(lane);
			}
			if(step>=2 && step<=4)
				channel.defense_points.push_back(cursor);
		}
		std::sort(channel.clearing_tiles.begin(), channel.clearing_tiles.end());
		channel.clearing_tiles.erase(std::unique(channel.clearing_tiles.begin(),
			channel.clearing_tiles.end()), channel.clearing_tiles.end());
		return channel;
	}

	// Tower fire uses a square range around its 2x2 footprint in Building::shoot.
	// Measure from those cells rather than a circular distance to its centre.
	// A candidate pad must cover the whole mouth, not just one convenient tile.
	bool tower_covers(Map* map,int center,int target,int range)
	{
		const int w=map->getW();
		for(int dy=-1;dy<=0;++dy)for(int dx=-1;dx<=0;++dx)
			if(map->warpDistMax(center%w+dx,center/w+dy,target%w,target/w)<=range)return true;
		return false;
	}

	std::vector<int> gate_tower_sites(Map* map,const std::vector<int>& gate,
		const GateChannel& channel,const std::vector<int>& home_distance)
	{
		std::vector<int> sites;
		const int w=map->getW();
		const int range=globalContainer->buildingsTypes.getByType("defencetower",0,false)->shootingRange;
		for(int dy=-range;dy<=range;++dy)for(int dx=-range;dx<=range;++dx)
		{
			const int x=map->normalizeX(gate[0]%w+dx),y=map->normalizeY(gate[0]/w+dy);
			const int center=y*w+x;
			// The firing position belongs on the settlement side. Keeping towers
			// off the channel prevents the defence itself from plugging the exit.
			if(home_distance[center]<0||home_distance[center]>home_distance[gate[0]])continue;
			bool valid=true;
			for(int oy=-1;oy<=0;++oy)for(int ox=-1;ox<=0;++ox)
			{
				const int tile=map->normalizeY(y+oy)*w+map->normalizeX(x+ox);
				const Tile& cell=map->getTile(tile%w,tile/w);
				valid&=cell.terrain<16&&cell.building==NOGBID&&cell.resource.type==NO_RES_TYPE
					&&std::find(channel.clearing_tiles.begin(),channel.clearing_tiles.end(),tile)==channel.clearing_tiles.end();
			}
			for(int tile:gate)valid&=tower_covers(map,center,tile,range);
			// Cover the approach behind the mouth too, giving ranged defenders
			// time to fire while enemy warriors advance through the entrance.
			for(int tile:channel.defense_points)valid&=tower_covers(map,center,tile,range);
			if(valid)sites.push_back(center);
		}
		std::sort(sites.begin(),sites.end());
		sites.erase(std::unique(sites.begin(),sites.end()),sites.end());
		return sites;
	}

	int gate_resource_burden(Map* map, int w, const GateChannel& channel)
	{
		int burden=0;
		for(size_t i=0; i<channel.clearing_tiles.size(); ++i)
		{
			const int index=channel.clearing_tiles[i];
			const Tile& cell=map->getTile(index%w, index/w);
			if(cell.resource.type!=NO_RES_TYPE)
				burden+=std::max(1, int(cell.resource.amount));
		}
		return burden;
	}

	enum ClearingCampaignKind
	{
		GateClearingCampaign,
		LegacyBoundaryClearingCampaign,
		WoodClearingCampaign
	};

	ClearingCampaignKind clearing_campaign_kind(int initial_wood)
	{
		if(initial_wood==0) return GateClearingCampaign;
		if(initial_wood==1) return LegacyBoundaryClearingCampaign;
		return WoodClearingCampaign;
	}

	int gate_clearing_radius(Map* map, const std::vector<int>& gate,
		int configured_radius)
	{
		int radius=configured_radius;
		const int w=map->getW();
		// Gate neighbors use eight-way adjacency; clearing uses a circular range.
		// Expand only as far as needed to include every tile of this gate.
		for(size_t cell=0; cell<gate.size(); ++cell)
			while(radius*radius<map->warpDistSquare(gate[0]%w, gate[0]/w,
				gate[cell]%w, gate[cell]/w))
				++radius;
		return radius;
	}

	void select_gate_resources(Map* map, int w, const std::vector<int>& gate,
		bool clearing_resources[BASIC_COUNT])
	{
		for(int resource=0; resource<BASIC_COUNT; ++resource)
			clearing_resources[resource]=false;
		for(size_t cell=0; cell<gate.size(); ++cell)
		{
			const Uint8 resource=map->getTile(gate[cell]%w, gate[cell]/w)
				.resource.type;
			if(resource<BASIC_COUNT && resource!=STONE)
				clearing_resources[resource]=true;
		}
	}

	bool gate_has_costly_resource(Map* map, int w,
		const std::vector<int>& gate)
	{
		for(size_t cell=0; cell<gate.size(); ++cell)
		{
			const Uint8 resource=map->getTile(gate[cell]%w, gate[cell]/w)
				.resource.type;
			if(resource<BASIC_COUNT && resource!=WOOD && resource!=STONE)
				return true;
		}
		return false;
	}

	struct StrategicGateOption
	{
		std::vector<int> tiles;
		int resource_burden;
		int relocation_distance;
		int home_distance;
		bool retained;
		std::vector<int> tower_sites;
		GateChannel channel;
		StrategicGateOption(): resource_burden(0), relocation_distance(0),
			home_distance(0), retained(false) {}
	};

	std::vector<int> collect_connected_component(int start, int w, int h,
		const std::vector<Uint8>& candidates, std::vector<Uint8>& visited)
	{
		std::vector<int> component;
		std::deque<int> queue;
		queue.push_back(start);
		visited[start]=1;
		while(!queue.empty())
		{
			const int index=queue.front();
			queue.pop_front();
			component.push_back(index);
			const int x=index%w;
			const int y=index/w;
			for(int dy=-1; dy<=1; ++dy)
				for(int dx=-1; dx<=1; ++dx)
				{
					if(!dx && !dy) continue;
					const int neighbor=((y+dy+h)%h)*w+((x+dx+w)%w);
					if(candidates[neighbor] && !visited[neighbor])
					{
						visited[neighbor]=1;
						queue.push_back(neighbor);
					}
				}
		}
		return component;
	}

	std::vector<std::vector<int> > enumerate_gate_tiles(
		const std::vector<int>& component, const std::vector<Uint8>& candidates,
		int w, int h)
	{
		std::vector<std::vector<int> > gates;
		for(size_t c=0; c<component.size(); ++c)
		{
			std::vector<int> gate(1, component[c]);
			const int x=component[c]%w;
			const int y=component[c]/w;
			for(int dy=-1; dy<=1 && gate.size()<size_t(STRATEGIC_GATE_WIDTH); ++dy)
				for(int dx=-1; dx<=1 && gate.size()<size_t(STRATEGIC_GATE_WIDTH); ++dx)
				{
					if(!dx && !dy) continue;
					const int neighbor=((y+dy+h)%h)*w+((x+dx+w)%w);
					if(candidates[neighbor]) gate.push_back(neighbor);
				}
			if(gate.size()==size_t(STRATEGIC_GATE_WIDTH)) gates.push_back(gate);
		}
		return gates;
	}

	bool gate_touches_both_sides(Map* map, const StrategicGateOption& option,
		int w, int h, const std::vector<Uint8>& shoreline,
		const std::vector<Uint8>& home_source,
		const std::vector<Uint8>& outside_route,
		const std::vector<int>& outside_distance)
	{
		const std::vector<int>& gate=option.tiles;
		std::vector<Uint8> reached(gate.size(), 0);
		for(size_t i=0; i<gate.size(); ++i)
		{
			if(home_source[gate[i]]) reached[i]=1;
			const int x=gate[i]%w;
			const int y=gate[i]/w;
			for(int dy=-1; dy<=1 && !reached[i]; ++dy)
				for(int dx=-1; dx<=1; ++dx)
				{
					if(!dx && !dy) continue;
					const int next=((y+dy+h)%h)*w+((x+dx+w)%w);
					if(outside_route[next] && outside_distance[next]>=0)
					{
						reached[i]=1;
						break;
					}
				}
		}
		bool progress=true;
		while(progress)
		{
			progress=false;
			for(size_t i=0; i<gate.size(); ++i) if(reached[i])
				for(size_t j=0; j<gate.size(); ++j) if(!reached[j])
				{
					int dx=std::abs(gate[i]%w-gate[j]%w);
					int dy=std::abs(gate[i]/w-gate[j]/w);
					dx=std::min(dx, w-dx);
					dy=std::min(dy, h-dy);
					if(dx<=1 && dy<=1)
					{
						reached[j]=1;
						progress=true;
					}
				}
		}
		for(size_t i=0; i<gate.size(); ++i)
			if(!reached[i]) return false;

		for(size_t i=0; i<gate.size(); ++i)
		{
			const int x=gate[i]%w;
			const int y=gate[i]/w;
			for(int dy=-1; dy<=1; ++dy)
				for(int dx=-1; dx<=1; ++dx)
				{
					if(!dx && !dy) continue;
					const int nx=(x+dx+w)%w;
					const int ny=(y+dy+h)%h;
					const int next=ny*w+nx;
					// Coastal exits need an actual clearable beach outlet. A larger
					// home distance alone can still terminate inside the same farm.
					const Tile& outlet=map->getTile(nx,ny);
					if(map->hasSand(nx,ny) && shoreline[next] && !map->isWater(nx,ny)
					   && outlet.building==NOGBID
					   && (outlet.resource.type==NO_RES_TYPE
						|| !globalContainer->resourcesTypes.get(outlet.resource.type)->eternal))
						return true;
				}
		}
		return false;
	}

	bool gate_options_overlap(const StrategicGateOption& first,
		const StrategicGateOption& second)
	{
		for(size_t a=0; a<first.tiles.size(); ++a)
			for(size_t b=0; b<second.tiles.size(); ++b)
				if(first.tiles[a]==second.tiles[b]) return true;
		return false;
	}

	int gate_option_separation(const StrategicGateOption& first,
		const StrategicGateOption& second, int w, int h)
	{
		const int ax=first.tiles[0]%w;
		const int ay=first.tiles[0]/w;
		const int bx=second.tiles[0]%w;
		const int by=second.tiles[0]/w;
		int dx=std::abs(ax-bx);
		int dy=std::abs(ay-by);
		dx=std::min(dx, w-dx);
		dy=std::min(dy, h-dy);
		return dx*dx+dy*dy;
	}

	///Semantic roles a single resource tile can play in the shared wheat/wood
	///layout. Classification is descriptive; policy priority is applied later.
	struct FarmTileClassification
	{
		bool frontier;
		bool edge;
		bool bootstrap;
		bool outer_frontier;
		bool outer_edge;
		bool edge_candidate;
		bool interior_seed;
		FarmTileClassification(): frontier(false), edge(false), bootstrap(false),
			outer_frontier(false), outer_edge(false), edge_candidate(false),
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
		const int water_distance=water_gradient.get_height(x, y);
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
			bool toward_water=false;
			bool toward_land=false;
			for(int dy=-1; dy<=1; ++dy)
				for(int dx=-1; dx<=1; ++dx)
				{
					if(!dx && !dy) continue;
					if(!mi.is_resource(x+dx, y+dy, resource_type)) continue;
					adjacent_resource=true;
					const int neighbor_water=water_gradient.get_height(x+dx, y+dy);
					toward_water=toward_water || (water_distance>=0
						&& neighbor_water>=0 && water_distance<neighbor_water);
					toward_land=toward_land || (water_distance>=0
						&& neighbor_water>=0 && water_distance>neighbor_water);
				}
			pattern.outer_frontier=adjacent_resource
				&& (shoreline_backed
					|| (water_exposure[index] && toward_water && !toward_land));
			pattern.frontier=adjacent_resource
				&& (pattern.outer_frontier || expansion_lattice);
		}
		if(resource && fertile)
		{
			bool toward_water=false;
			bool toward_land=false;
			for(int dy=-1; dy<=1; ++dy)
				for(int dx=-1; dx<=1; ++dx)
				{
					if(!dx && !dy) continue;
					const Tile& neighbor=map->getTile(x+dx, y+dy);
					const int neighbor_water=water_gradient.get_height(x+dx, y+dy);
					if(!mi.is_resource(x+dx, y+dy, resource_type)
					   && water_distance>=0 && neighbor_water>=0)
					{
						const int next=((y+dy+h)%h)*w+((x+dx+w)%w);
						toward_water=toward_water || (neighbor_water<water_distance
							&& water_exposure[next]);
						// Another crop or a sand beach is not an opening into the
						// farm interior. Only empty growing land makes this porous.
						toward_land=toward_land || (neighbor_water>water_distance
							&& is_empty_growth_cell(neighbor));
					}
					const bool eligible=is_empty_growth_cell(neighbor)
						&& fertility_cache.at(x+dx, y+dy)>=minimum_fertility;
					pattern.edge_candidate=pattern.edge_candidate || eligible;
				}
			pattern.outer_edge=shoreline_backed
				|| (toward_water && !toward_land);
			pattern.edge=(pattern.edge_candidate || pattern.outer_edge)
				&& (pattern.outer_edge || expansion_lattice);
			pattern.interior_seed=seed_lattice
				&& !pattern.edge_candidate && !pattern.outer_edge;
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
	std::vector<Uint8> coastal_envelope;
	Farming::CoastalBarrierPorosityResult porosity;
	int passive_opening_infrastructure_overlap;
	int protected_seeds;
	int protected_frontier;
	int protected_wheat_edges;
	int protected_wheat_bootstraps;
	int protected_wood_edges;
	int protected_wood_bootstraps;
	int protected_outer_frontier;
	int protected_outer_edges;
	int protected_interior_seeds;
	int blocked_directions;
	Uint64 expected_capacity;
	int wood_pressure;
	Uint32 wood_fertility;

	explicit FarmProtectionPlan(int size): forbidden(size, 0),
		protected_wheat(size, 0), coastal_envelope(size, 0),
		passive_opening_infrastructure_overlap(0), protected_seeds(0),
		protected_frontier(0), protected_wheat_edges(0),
		protected_wheat_bootstraps(0), protected_wood_edges(0),
		protected_wood_bootstraps(0), protected_outer_frontier(0),
		protected_outer_edges(0), protected_interior_seeds(0),
		blocked_directions(0), expected_capacity(0), wood_pressure(0),
		wood_fertility(0) {}
};

///One decision emitted by the strategic-gate evaluator. This replaces the
///previous implicit combination of three vectors and a sentinel boolean.
struct Maxima::GateClearingIntent
{
	std::vector<int> target;
	bool emergency;
	bool deferred;
	bool costly_resource;
	GateClearingIntent(): emergency(false), deferred(false),
		costly_resource(false) {}
};

struct Maxima::GateRouteStatus
{
	bool clear;
	bool resource_clearable;
};

Maxima::GateClearingIntent Maxima::select_gate_clearing_intent(
	Context& echo) const
{
	GateClearingIntent intent;
	if(!budget.farming_gate_clearing_enabled) return intent;
	Map* map=echo.player->map;
	const int w=map->getW();
	TeamStat* stat=echo.player->team->stats.getLatestStat();
	const bool wood_economy_ready=!budget.recovery_active
		&& stat->numberUnitPerType[WORKER]>=budget.farming_min_workers_for_clearing;
	const bool costly_economy_ready=!budget.recovery_active
		&& stat->numberUnitPerType[WORKER]
			>=strategy.farming.gate_clearing_workers_min
		&& snapshot.free_workers>=strategy.staffing.clearing_workers
		&& snapshot.hospitals>0 && snapshot.hungry==0
		&& snapshot.critical_food==0
		&& environment.food_headroom>=strategy.economy.mature_food_headroom_min;
	std::vector<int> routine;
	bool internal_branch=false;
	for(size_t component=0; component+1<strategic_gates.size();
		component+=STRATEGIC_GATE_COUNT)
	{
		const std::vector<int>& first=strategic_gates[component];
		const std::vector<int>& second=strategic_gates[component+1];
		const GateRouteStatus first_status=inspect_gate_route(echo, first);
		const GateRouteStatus second_status=inspect_gate_route(echo, second);
		const bool first_blocked=!first_status.clear;
		const bool second_blocked=!second_status.clear;
		const bool first_clearable=first_status.resource_clearable;
		const bool second_clearable=second_status.resource_clearable;
		if(first_blocked && second_blocked && (first_clearable || second_clearable))
		{
			intent.target=first_clearable ? first : second;
			int first_distance=INT_MAX;
			int second_distance=INT_MAX;
			for(int id=0; id<Building::MAX_COUNT; ++id)
			{
				Building* building=echo.player->team->myBuildings[id];
				if(!building || building->type->isVirtual) continue;
				first_distance=std::min(first_distance, map->warpDistSquare(
					building->posX, building->posY, first[0]%w, first[0]/w));
				second_distance=std::min(second_distance, map->warpDistSquare(
					building->posX, building->posY, second[0]%w, second[0]/w));
			}
			if(second_clearable && (!first_clearable
			   || second_distance<first_distance))
				intent.target=second;
			intent.emergency=true;
			break;
		}
		const bool first_ready=first_clearable
			&& (gate_has_costly_resource(map, w, barrier_gate_route(first))
				? costly_economy_ready : wood_economy_ready);
		const bool second_ready=second_clearable
			&& (gate_has_costly_resource(map, w, barrier_gate_route(second))
				? costly_economy_ready : wood_economy_ready);
		if(first_blocked && first_ready) routine=first;
		else if(second_blocked && second_ready) routine=second;
		else if((first_blocked && first_clearable)
			||(second_blocked && second_clearable))
			intent.deferred=true;
	}
	if(!intent.emergency) intent.target=routine;
	// First restore a primary exit. A fertile branch at a remote outpost must
	// not monopolize the only directed clearing campaign while an entire
	// settlement's two gates remain blocked. Internal branches follow next.
	if(!intent.emergency)for(const auto& route:settlement_access_routes)
	{
		bool blocked=false;
		for(int tile:route)blocked|=map->getTile(tile%w,tile/w).resource.type!=NO_RES_TYPE;
		if(blocked){intent.emergency=true;intent.target=route;internal_branch=true;break;}
	}

	intent.costly_resource=gate_has_costly_resource(map, w,
		internal_branch ? intent.target : barrier_gate_route(intent.target));
	if(!intent.target.empty())
		intent.target=gate_clearing_target(echo, intent.target, internal_branch);
	return intent;
}

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

	proactive_clearing_flag=-1;
}

bool Maxima::continue_clearing_campaign(Context& echo,
	const GateClearingIntent& gate_intent)
{
	MapInfo mi(echo);
	Map* map=echo.player->map;
	const int w=map->getW();
	const std::vector<int>& gate_target=gate_intent.target;
	const bool boxed_in=gate_intent.emergency;
	if(proactive_clearing_flag!=-1)
	{
		if(echo.get_building_register().is_building_found(proactive_clearing_flag))
		{
			Building* flag=echo.get_building_register().get_building(proactive_clearing_flag);
			const ClearingCampaignKind campaign=
				clearing_campaign_kind(proactive_clearing_initial_wood);
			if(campaign==GateClearingCampaign && !gate_target.empty())
			{
				bool clearing_resources[BASIC_COUNT];
				select_gate_resources(map, w, gate_target, clearing_resources);
				echo.push_order(std::shared_ptr<Order>(
					new OrderModifyClearingFlag(flag->gid, clearing_resources)));
				echo.add_management_order(new ChangeFlagSize(
					gate_clearing_radius(map, gate_target,
						budget.farming_gate_clearing_radius),
					proactive_clearing_flag));
				const int target_position=gate_target[0];
				if(map->warpDistSquare(flag->posX, flag->posY,
					target_position%w, target_position/w)>0)
					echo.add_management_order(new ChangeFlagPosition(
						target_position%w, target_position/w,
						proactive_clearing_flag));
				// One worker can lose a race against fertile wood regrowth. During
				// an escape emergency, staff up to the three-cell mouth width;
				// the population bound protects the smallest starting colonies.
				const int workers=boxed_in ? std::max(strategy.staffing.clearing_workers,
					std::min(STRATEGIC_GATE_WIDTH,std::max(1,snapshot.workers/8)))
					: strategy.staffing.clearing_workers;
				echo.add_management_order(new AssignWorkers(workers,
					proactive_clearing_flag));
				RemoveArea* release=new RemoveArea(ForbiddenArea);
				for(size_t cell=0; cell<gate_target.size(); ++cell)
					release->add_location(gate_target[cell]%w,
						gate_target[cell]/w);
				echo.add_management_order(release);
				farming_urgent=true;
				director.invalidate();
				return true;
			}
			if(campaign==GateClearingCampaign)
			{
				retire_clearing_campaign(echo, "gate_reopened");
				farming_urgent=false;
				director.invalidate();
				return true;
			}
			if(boxed_in)
			{
				// Being boxed in outranks an ordinary space-clearing campaign. Retire
				// that flag now and dedicate the next pass to reopening a gate.
				retire_clearing_campaign(echo, "boxed_in_priority");
				farming_urgent=true;
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
	const GateClearingIntent gate_intent=select_gate_clearing_intent(echo);
	const std::vector<int>& gate_target=gate_intent.target;

	if(continue_clearing_campaign(echo, gate_intent)) return;

	if(!gate_target.empty())
	{
		int position=gate_target[0];
		// Start unstaffed. The next management pass first narrows the resource
		// selector to the actual gate blockers, then assigns the normal crew
		// or the larger escape crew. An unconfigured flag could cut farm seeds.
		BuildingOrder* order=new BuildingOrder(IntBuildingType::CLEARING_FLAG, 0);
		order->add_constraint(new Construction::SinglePosition(position%w,
			position/w));
		proactive_clearing_flag=echo.add_building_order(order);
		proactive_clearing_started_tick=timer;
		proactive_clearing_initial_wood=0;
		echo.add_management_order(new ChangeFlagSize(
			gate_clearing_radius(map, gate_target,
				budget.farming_gate_clearing_radius), proactive_clearing_flag));
		RemoveArea* release=new RemoveArea(ForbiddenArea);
		for(size_t cell=0; cell<gate_target.size(); ++cell)
			release->add_location(gate_target[cell]%w, gate_target[cell]/w);
		echo.add_management_order(release);

		farming_urgent=true;
		director.invalidate();
		return;
	}

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

	if(best_x==-1)
		return;

	// Start unstaffed so the WOOD-only selector is installed before a worker can
	// touch an overlapping wheat farm.
	BuildingOrder* flag_order=new BuildingOrder(
		IntBuildingType::CLEARING_FLAG, 0);
	flag_order->add_constraint(new Construction::SinglePosition(best_x, best_y));
	proactive_clearing_flag=echo.add_building_order(flag_order);
	proactive_clearing_started_tick=timer;
	proactive_clearing_initial_wood=best_wood;
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
		for(int index=0; index<size; ++index)
			if((index<int(strategic_gate_mask.size()) && strategic_gate_mask[index])
			   || (index<int(emergency_escape_mask.size())
				&& emergency_escape_mask[index]))
				plan.circulation[index]=1;
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
	strategic_barrier_mask.assign(w*h, 0);
	strategic_gate_mask.assign(w*h, 0);
	strategic_gate_routes.clear();
	emergency_escape_mask.assign(w*h, 0);
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

}

Uint32 Maxima::compute_farming_topology_signature(Context& echo) const
{
	Map* map=echo.player->map;
	Uint32 signature=compute_preemptive_building_signature(echo);
	for(int y=0; y<map->getH(); ++y)
		for(int x=0; x<map->getW(); ++x)
		{
			const Tile& cell=map->getTile(x, y);
			add_preemptive_hash(signature, Uint32(y*map->getW()+x));
			add_preemptive_hash(signature, cell.terrain);
			const bool permanent_resource=cell.resource.type!=NO_RES_TYPE
				&& globalContainer->resourcesTypes.get(
					cell.resource.type)->eternal;
			add_preemptive_hash(signature, permanent_resource
				? Uint32(cell.resource.type)+1u : 0u);
		}
	// Military guard areas consume gate geometry. Feeding them back into the
	// farm contour makes its own guard posts move the gates on the next pass.
	// Inland tactical choke analysis remains independent of coastal farm walls.
	return signature;
}

void Maxima::update_barrier_topology(Context& echo)
{
	Map* map=echo.player->map;
	const int w=map->getW();
	const int h=map->getH();
	const int size=w*h;
	// Growth cannot change the identity of a defended entrance. Replan only
	// when terrain, buildings, or the settlement scope changes.
	Uint32 geometry=compute_farming_topology_signature(echo);
	add_preemptive_hash(geometry,budget.farming_economic_envelope_radius);
	if(budget.farming_enabled && budget.farming_barrier_enabled
	   && geometry==barrier_geometry_signature && !strategic_gates.empty())return;
	barrier_geometry_signature=geometry;
	settlement_access_routes.clear();
	const std::vector<std::vector<int> > all_previous_gates=strategic_gates;
	const std::map<int,std::vector<int> > previous_routes=strategic_gate_routes;
	strategic_barrier_mask.assign(size, 0);
	strategic_gate_mask.assign(size, 0);
	emergency_escape_mask.assign(size, 0);
	strategic_gates.clear();
	strategic_gate_routes.clear();
	strategic_gate_approaches.clear();
	barrier_tower_quality.assign(size,0);
	gate_defense_demand=0;
	barrier_defense_points.clear();
	if(!budget.farming_enabled || !budget.farming_barrier_enabled)
		return;

	// Use the same water-connected beach definition for candidate discovery
	// and gate outlets. Immediate water contact excludes every wide beach.
	initialize_farming_cache(echo);
	// For a sand tile, touching the connected beach also means belonging to
	// it. Reuse the terrain-only contact cache instead of flood-filling again.
	const std::vector<Uint8>& shoreline=farming_shoreline_mask;

	std::vector<Uint8> walkable(size, 0);
	std::vector<int> home_sources;
	for(int y=0; y<h; ++y)
		for(int x=0; x<w; ++x)
		{
			const Tile& cell=map->getTile(x, y);
			const bool permanent_resource=cell.resource.type!=NO_RES_TYPE
				&& globalContainer->resourcesTypes.get(
					cell.resource.type)->eternal;
			walkable[y*w+x]=cell.terrain<16
				&& cell.building==NOGBID && !permanent_resource;
		}
	for(int id=0; id<Building::MAX_COUNT; ++id)
	{
		Building* building=echo.player->team->myBuildings[id];
		if(!building || building->type->isVirtual
		   || building->type->isBuildingSite)
			continue;
		for(int dy=-1; dy<=building->type->height; ++dy)
			for(int dx=-1; dx<=building->type->width; ++dx)
			{
				if(dx!=-1 && dx!=building->type->width
				   && dy!=-1 && dy!=building->type->height)
					continue;
				const int x=(building->posX+dx+w)%w;
				const int y=(building->posY+dy+h)%h;
				if(walkable[y*w+x])
					home_sources.push_back(y*w+x);
			}
	}
	std::vector<int> home_distance;
	compute_preemptive_distance_field(w, h, walkable, home_sources,
		home_distance);
	// A coastline can be fragmented by a beach corner or a stone deposit.
	// Those fragments do not each deserve two new holes. Label local economic
	// cores first, then assign coast cells to their nearest core. Unlike a
	// Euclidean cluster, this cannot join colonies across water or stone.
	std::vector<Uint8> core(size,0),core_visited(size,0);
	for(int i=0;i<size;++i)core[i]=walkable[i] && home_distance[i]>=0
		&& home_distance[i]<=std::max(1,budget.farming_economic_envelope_radius/2);
	std::vector<int> settlement(size,-1),settlement_queue;
	int settlement_count=0;
	for(int i=0;i<size;++i)if(core[i]&&!core_visited[i])
	{
		const auto cells=collect_connected_component(i,w,h,core,core_visited);
		for(int tile:cells){settlement[tile]=settlement_count;settlement_queue.push_back(tile);}
		++settlement_count;
	}
	for(size_t head=0;head<settlement_queue.size();++head)
	{
		int i=settlement_queue[head];
		for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
		{
			int next=map->normalizeY(i/w+dy)*w+map->normalizeX(i%w+dx);
			if(walkable[next]&&settlement[next]<0)
			{settlement[next]=settlement[i];settlement_queue.push_back(next);}
		}
	}
	std::vector<Uint8> candidates(size, 0);

	for(int y=0; y<h; ++y)
		for(int x=0; x<w; ++x)
		{
			const int index=y*w+x;
			if(!walkable[index] || home_distance[index]<0
			   || home_distance[index]>budget.farming_economic_envelope_radius)
				continue;

			bool coast=false;
			for(int dy=-1; dy<=1 && !coast; ++dy)
				for(int dx=-1; dx<=1; ++dx)
					if((dx || dy) && map->hasSand(x+dx, y+dy)
					   && shoreline[((y+dy+h)%h)*w+(x+dx+w)%w])
					{
						coast=true;
						break;
					}

			candidates[index]=coast;

		}

	std::vector<Uint8> visited(size, 0);

	std::vector<int> settlement_weight(settlement_count,0),starts(settlement_count,-1);
	for(int tile:home_sources)if(settlement[tile]>=0)++settlement_weight[settlement[tile]];
	for(int tile=0;tile<size;++tile)if(candidates[tile]&&starts[settlement[tile]]<0)
		starts[settlement[tile]]=tile;
	starts.erase(std::remove(starts.begin(),starts.end(),-1),starts.end());
	std::sort(starts.begin(),starts.end(),[&](int a,int b){
		int aw=settlement_weight[settlement[a]],bw=settlement_weight[settlement[b]];
		return aw!=bw?aw>bw:a<b;});
	for(int start:starts)
	{
		if(!candidates[start] || visited[start])
			continue;
		std::vector<int> component;
		for(int tile=0;tile<size;++tile)
			if(candidates[tile]&&settlement[tile]==settlement[start])
			{component.push_back(tile);visited[tile]=1;}
		if(component.size()<size_t(STRATEGIC_GATE_COUNT*STRATEGIC_GATE_WIDTH))
		{

			continue;
		}

		const std::vector<std::vector<int> > gate_tiles=enumerate_gate_tiles(
			component, candidates, w, h);
		if(gate_tiles.size()<2)
		{

			continue;
		}
		// Associate history with this perimeter, never with a distant colony.
		std::vector<std::vector<int> > previous_gates;
		for(size_t g=0;g+1<all_previous_gates.size();g+=STRATEGIC_GATE_COUNT)
			if(std::find(component.begin(),component.end(),all_previous_gates[g][0])!=component.end()
			   && std::find(component.begin(),component.end(),all_previous_gates[g+1][0])!=component.end())
			{previous_gates={all_previous_gates[g],all_previous_gates[g+1]};break;}
		const bool retain_previous_gates=previous_gates.size()==2
			&& budget.farming_gate_relocation_penalty_cap>0;
		// Each entrance must stand on its own: close every other mouth and
		// route back to this settlement's sources. Using the unrestricted home
		// distance here could cut along the wall and quietly widen the breach.
		std::vector<Uint8> outside_route=walkable,local_home_mask(size,0);
		std::vector<int> local_home_sources;
		for(int tile=0;tile<size;++tile)if(candidates[tile])outside_route[tile]=0;
		for(int tile:home_sources)if(settlement[tile]==settlement[start])
		{local_home_sources.push_back(tile);local_home_mask[tile]=1;}
		std::vector<int> outside_distance;
		compute_preemptive_distance_field(w,h,outside_route,local_home_sources,outside_distance);
		std::vector<int> channel_distance=outside_distance;
		std::vector<Uint8> channel_walkable=outside_route;
		std::vector<StrategicGateOption> gate_options(gate_tiles.size());
		for(size_t option=0; option<gate_tiles.size(); ++option)
		{
			StrategicGateOption& candidate=gate_options[option];
			candidate.tiles=gate_tiles[option];
			// Only three cells differ from the closed-wall distance field. Relax
			// that tiny set instead of flood-filling the map for every candidate.
			for(int tile:candidate.tiles){channel_walkable[tile]=1;
				channel_distance[tile]=local_home_mask[tile]?0:-1;}
			for(size_t pass=0;pass<candidate.tiles.size();++pass)
				for(int tile:candidate.tiles)
					for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
					{
						int next=map->normalizeY(tile/w+dy)*w+map->normalizeX(tile%w+dx);
						if(channel_walkable[next]&&channel_distance[next]>=0
						   &&(channel_distance[tile]<0||channel_distance[next]+1<channel_distance[tile]))
							channel_distance[tile]=channel_distance[next]+1;
					}
			candidate.channel=build_gate_channel(candidate.tiles,w,h,channel_walkable,channel_distance);
			candidate.resource_burden=gate_resource_burden(map,w,candidate.channel);
			for(int tile:candidate.tiles){channel_walkable[tile]=outside_route[tile];
				channel_distance[tile]=outside_distance[tile];}
			candidate.relocation_distance=retain_previous_gates
				? Farming::gateRelocationDistance(previous_gates,
					candidate.tiles, w, h) : 0;
			for(size_t tile=0; tile<candidate.tiles.size(); ++tile)
				candidate.home_distance+=home_distance[candidate.tiles[tile]];
		}
		auto gate_better=[this](const StrategicGateOption& a,
			const StrategicGateOption& b)
			{
				if(a.retained!=b.retained)return a.retained;
				const int ascore=a.resource_burden+std::min(a.relocation_distance,
					budget.farming_gate_relocation_penalty_cap);
				const int bscore=b.resource_burden+std::min(b.relocation_distance,
					budget.farming_gate_relocation_penalty_cap);
				if(ascore!=bscore) return ascore<bscore;
				if(a.relocation_distance!=b.relocation_distance)
					return a.relocation_distance<b.relocation_distance;
				return a.home_distance!=b.home_distance
					? a.home_distance<b.home_distance : a.tiles[0]<b.tiles[0];
			};
		// Retained gates must survive candidate shortlisting even after regrowth.
		if(retain_previous_gates)
			for(auto& option:gate_options)
				if(option.tiles==previous_gates[0]||option.tiles==previous_gates[1])
					option.retained=true;
		if(gate_options.size()>96)
		{
			std::partial_sort(gate_options.begin(),gate_options.begin()+96,
				gate_options.end(),gate_better);
			gate_options.resize(96);
		}
		else
			std::sort(gate_options.begin(),gate_options.end(),gate_better);

		for(auto& option:gate_options)
			option.tower_sites=gate_tower_sites(map,option.tiles,option.channel,home_distance);

		std::vector<Uint8> valid_gate(gate_options.size(), 0);
		for(size_t option=0; option<gate_options.size(); ++option)
			valid_gate[option]=gate_touches_both_sides(map, gate_options[option],
				w, h, shoreline, local_home_mask, outside_route, outside_distance);
		int best_first=-1;
		int best_second=-1;
		int best_separation=-1;
		int best_pair_score=INT_MAX;
		int best_relocation_distance=INT_MAX;
		int best_defense=-1;
		for(size_t first=0; first<gate_options.size(); ++first)
		{
			if(!valid_gate[first])
				continue;
			for(size_t second=first+1; second<gate_options.size(); ++second)
			{
				if(gate_options_overlap(gate_options[first], gate_options[second])
				   || !valid_gate[second])
					continue;
				const int separation=gate_option_separation(gate_options[first],
					gate_options[second], w, h);
				const int pair_burden=gate_options[first].resource_burden
					+gate_options[second].resource_burden;
				const int relocation_distance=retain_previous_gates
					? Farming::gatePairRelocationDistance(previous_gates,
						gate_options[first].tiles, gate_options[second].tiles,
						w, h) : 0;
				const int relocation_penalty=std::min(relocation_distance,
					budget.farming_gate_relocation_penalty_cap);
				const int pair_score=pair_burden+relocation_penalty;
				const auto& first_sites=gate_options[first].tower_sites;
				const auto& second_sites=gate_options[second].tower_sites;
				int defense=int(!first_sites.empty())+int(!second_sites.empty());
				for(int site:first_sites)if(std::binary_search(second_sites.begin(),second_sites.end(),site))
				{defense+=2;break;}
				// Resource preservation comes first. Among equally costly cuts, prefer
				// defendable mouths and a shared firing position before maximizing
				// separation. Opposite ends of an island rarely share a kill zone.
				if(pair_score<best_pair_score
				   || (pair_score==best_pair_score
					&& relocation_distance<best_relocation_distance)
				   || (pair_score==best_pair_score
					&& relocation_distance==best_relocation_distance
					&& (defense>best_defense
						||(defense==best_defense&&separation>best_separation))))
				{
					best_pair_score=pair_score;
					best_defense=defense;
					best_relocation_distance=relocation_distance;
					best_separation=separation;
					best_first=first;
					best_second=second;
				}
			}
		}
		if(best_first<0)
		{

			continue;
		}

		// A standing passage is an infrastructure contract. Resource growth
		// alone cannot move it and expose a different set of permanent seeds.
		if(retain_previous_gates)
		{
			int retained[2]={-1,-1};
			for(size_t option=0;option<gate_options.size();++option)
				for(int g=0;g<2;++g)
					if(valid_gate[option] && gate_options[option].tiles==previous_gates[g])
						retained[g]=int(option);
			if(retained[0]>=0 && retained[1]>=0)
			{best_first=retained[0];best_second=retained[1];}
		}

		for(int tile:component)strategic_barrier_mask[tile]=1;
		const std::vector<int> selected[STRATEGIC_GATE_COUNT]={
			gate_options[best_first].tiles,gate_options[best_second].tiles};
		for(int gate_number=0;gate_number<STRATEGIC_GATE_COUNT;++gate_number)
		{
			strategic_gates.push_back(selected[gate_number]);
			for(int gate_tile:selected[gate_number])
			{strategic_gate_mask[gate_tile]=1;strategic_barrier_mask[gate_tile]=0;}
			GateChannel channel=gate_options[gate_number==0?best_first:best_second].channel;
			// Include the actual beach outlet. A nominal grass opening is not
			// complete when a resource on its sand-side edge still blocks it.
			int outlet=-1,outlet_burden=INT_MAX;
			for(int tile:selected[gate_number])
				for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
				{
					int next=map->normalizeY(tile/w+dy)*w+map->normalizeX(tile%w+dx);
					const Tile& cell=map->getTile(next%w,next/w);
					if(!map->hasSand(next%w,next/w)||!shoreline[next]
					   ||cell.building!=NOGBID || (cell.resource.type!=NO_RES_TYPE
					   &&globalContainer->resourcesTypes.get(cell.resource.type)->eternal))continue;
					int burden=cell.resource.type==NO_RES_TYPE?0:int(cell.resource.amount);
					if(burden<outlet_burden){outlet=next;outlet_burden=burden;}
				}
			if(outlet>=0)channel.clearing_tiles.push_back(outlet);
			const auto old=previous_routes.find(selected[gate_number][0]);
			if(retain_previous_gates && old!=previous_routes.end())
			{
				bool valid=true;
				for(int tile:old->second)
				{
					const Tile& cell=map->getTile(tile%w,tile/w);
					valid&=!map->isWater(tile%w,tile/w)&&cell.building==NOGBID
						&&(!candidates[tile]||std::find(selected[gate_number].begin(),
							selected[gate_number].end(),tile)!=selected[gate_number].end())
						&&(cell.resource.type==NO_RES_TYPE
						||!globalContainer->resourcesTypes.get(cell.resource.type)->eternal);
				}
				if(valid){channel.clearing_tiles=old->second;
					channel.defense_points.erase(std::remove_if(channel.defense_points.begin(),
						channel.defense_points.end(),[&](int tile){return std::find(old->second.begin(),
							old->second.end(),tile)==old->second.end();}),channel.defense_points.end());}
			}
			strategic_gate_routes[selected[gate_number][0]]=channel.clearing_tiles;
			strategic_gate_approaches[selected[gate_number][0]]=channel.defense_points;
			for(int tile:channel.clearing_tiles)emergency_escape_mask[tile]=1;
			barrier_defense_points.insert(barrier_defense_points.end(),
				channel.defense_points.begin(),channel.defense_points.end());

		}
	}

	std::sort(barrier_defense_points.begin(), barrier_defense_points.end());
	barrier_defense_points.erase(std::unique(barrier_defense_points.begin(),
		barrier_defense_points.end()), barrier_defense_points.end());

}

const std::vector<int>& Maxima::barrier_gate_route(const std::vector<int>& gate) const
{
	if(!gate.empty())
	{
		const auto route=strategic_gate_routes.find(gate[0]);
		if(route!=strategic_gate_routes.end()) return route->second;
	}
	return gate;
}

std::vector<int> Maxima::gate_clearing_target(Context& echo,
	const std::vector<int>& gate, bool complete_route) const
{
	Map* map=echo.player->map;
	const int w=map->getW(), h=map->getH();
	// A branch is already a complete route, not a three-cell mouth. Its first
	// tile may even equal a gate's lookup key. Preserve that distinction so we
	// neither replace the branch with the gate route nor clear its entire length
	// with a large-radius flag (which could harvest unrelated adjacent seeds).
	const std::vector<int>& route=complete_route ? gate : barrier_gate_route(gate);
	bool approach_obstructed=complete_route;
	for(int tile:route)
		if(map->getTile(tile%w,tile/w).resource.type!=NO_RES_TYPE
		   && std::find(gate.begin(),gate.end(),tile)==gate.end())
			approach_obstructed=true;
	if(!approach_obstructed) return gate;

	// Find the first resource workers can reach, rather than asking a flag at
	// the gate to harvest through an obstructed approach. Buildings
	// seed their entrances too, covering workers temporarily inside them.
	std::vector<Uint8> walkable(w*h,0);
	std::vector<int> sources;
	for(int index=0;index<w*h;++index)
	{
		const Tile& cell=map->getTile(index%w,index/w);
		walkable[index]=cell.terrain<16 && cell.building==NOGBID
			&& cell.resource.type==NO_RES_TYPE
			&& map->isMapDiscovered(index%w,index/w,echo.player->team->allies)
			&& !map->isForbidden(index%w,index/w,echo.player->team->me);
	}
	for(int id=0;id<Unit::MAX_COUNT;++id)
	{
		const Unit* worker=echo.player->team->myUnits[id];
		if(worker && worker->typeNum==WORKER)
			sources.push_back(map->normalizeY(worker->posY)*w+map->normalizeX(worker->posX));
	}
	for(int id=0;id<Building::MAX_COUNT;++id)
	{
		const Building* building=echo.player->team->myBuildings[id];
		if(!building || building->type->isVirtual || building->type->isBuildingSite) continue;
		for(int dy=-1;dy<=building->type->height;++dy)
			for(int dx=-1;dx<=building->type->width;++dx)
				if(dx==-1 || dy==-1 || dx==building->type->width || dy==building->type->height)
					sources.push_back(map->normalizeY(building->posY+dy)*w
						+map->normalizeX(building->posX+dx));
	}
	std::vector<int> distance;
	compute_preemptive_distance_field(w,h,walkable,sources,distance);
	int target=-1, best_distance=INT_MAX;
	for(int tile:route)
	{
		if(map->getTile(tile%w,tile/w).resource.type==NO_RES_TYPE) continue;
		for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
		{
			const int next=map->normalizeY(tile/w+dy)*w+map->normalizeX(tile%w+dx);
			if(distance[next]>=0 && distance[next]<best_distance)
			{
				best_distance=distance[next];
				target=tile;
			}
		}
	}
	if(target<0)
	{
		if(!complete_route) return gate;
		// Even if no worker can currently approach, keep the request local.
		// Movement may become possible after pending forbidden releases apply.
		for(int tile:route)
			if(map->getTile(tile%w,tile/w).resource.type!=NO_RES_TYPE)
				return std::vector<int>(1,tile);
		return std::vector<int>();
	}
	// Keep directed work local. A radius covering the entire channel could
	// consume unrelated harvestable wheat beside the route.
	std::vector<int> result(1,target);
	for(int tile:route)
		if(tile!=target && map->getTile(tile%w,tile/w).resource.type!=NO_RES_TYPE
		   && map->warpDistMax(tile%w,tile/w,target%w,target/w)<=1)
			result.push_back(tile);
	return result;
}

Maxima::GateRouteStatus Maxima::inspect_gate_route(Context& echo,
	const std::vector<int>& gate) const
{
	Map* map=echo.player->map;
	const int w=map->getW();
	bool has_resource=false;
	bool permanent_obstruction=false;
	for(int index:barrier_gate_route(gate))
	{
		const Tile& cell=map->getTile(index%w, index/w);
		// Units and resolved virtual flags are transient gate users. An unknown
		// building ID is conservatively treated as a physical obstruction.
		if(cell.building!=NOGBID)
		{
			const int team=Building::GIDtoTeam(cell.building);
			const int id=Building::GIDtoID(cell.building);
			Building* building=team>=0 && team<Team::MAX_COUNT
				&& echo.player->game->teams[team] && id>=0 && id<Building::MAX_COUNT
				? echo.player->game->teams[team]->myBuildings[id] : NULL;
			permanent_obstruction|=!building || !building->type->isVirtual;
		}
		// The mouth itself remains on growing grass. Its complete route may
		// include the sand outlet; sand is walkable by ordinary ground units.
		permanent_obstruction|=map->isWater(index%w,index/w)
			|| (std::find(gate.begin(),gate.end(),index)!=gate.end()&&cell.terrain>=16);
		if(cell.resource.type!=NO_RES_TYPE)
		{
			has_resource=true;
			permanent_obstruction|=cell.resource.type>=BASIC_COUNT
				|| cell.resource.type==STONE;
		}
	}
	const bool valid=gate.size()==size_t(STRATEGIC_GATE_WIDTH)
		&& !permanent_obstruction;
	return {valid && !has_resource, valid && has_resource};
}

bool Maxima::barrier_gate_is_clear(Context& echo,
	const std::vector<int>& gate) const
{
	return inspect_gate_route(echo, gate).clear;
}

bool Maxima::barrier_gate_is_resource_clearable(Context& echo,
	const std::vector<int>& gate) const
{
	return inspect_gate_route(echo, gate).resource_clearable;
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

void Maxima::release_farming_protection(Context& echo)
{
	MapInfo map_info(echo);
	Map* map=echo.player->map;
	std::fill(farm_protection_mask.begin(), farm_protection_mask.end(), 0);
	std::fill(wheat_farm_protection_mask.begin(),
		wheat_farm_protection_mask.end(), 0);
	update_barrier_topology(echo);
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
		|| development_planner.isCirculationReserved(index)
		|| strategic_gate_mask[index] || emergency_escape_mask[index];
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
			const bool strategic_barrier=budget.farming_barrier_enabled
				&& strategic_barrier_mask[index] && cell.terrain<16
				&& cell.building==NOGBID && (wheat || wood || wheat_farm || wood_farm);
			if(!wheat && !wood && !wheat_farm && !wood_farm
			   && !strategic_barrier) continue;

			// This priority table is the policy. Classification above only says
			// what a tile is; this block says which spatial contract wins.
			bool protect=false;
			if(has_hard_farming_contract(index))
				protect=false;
			else if(strategic_barrier)
				protect=true;
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
			plan.protected_wheat[index]=protect
				&& (wheat_farm || (strategic_barrier && wheat));
			plan.coastal_envelope[index]=protect
				&& (strategic_barrier || wheat_role.outer_edge || wheat_role.outer_frontier
					|| wood_role.outer_edge || wood_role.outer_frontier);
			if(!protect) continue;

			plan.protected_frontier+=wheat_role.frontier+wood_role.frontier;
			plan.protected_wheat_edges+=wheat_role.edge;
			plan.protected_wheat_bootstraps+=wheat_role.bootstrap;
			plan.protected_wood_edges+=wood_role.edge;
			plan.protected_wood_bootstraps+=wood_role.bootstrap;
			plan.protected_outer_frontier+=wheat_role.outer_frontier
				+wood_role.outer_frontier;
			plan.protected_outer_edges+=wheat_role.outer_edge+wood_role.outer_edge;
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

void Maxima::restore_passive_coastal_access(Context& echo,
	FarmProtectionPlan& plan)
{
	if(!budget.farming_coastal_porosity_enabled) return;
	Map* map=echo.player->map;
	MapInfo map_info(echo);
	const int w=map->getW();
	const int h=map->getH();
	const int size=w*h;
	GradientInfo water_info;
	water_info.add_source(new Entities::Water);
	Gradient& water_gradient=echo.get_gradient_manager().get_gradient(water_info);
	// Stage 1: identify exceptional growing islands from terrain, never crops.
	const std::vector<Uint8> home_land=home_growing_region(map,
		echo.player->team->startPosX, echo.player->team->startPosY,
		echo.player->team->startPosSet!=0);
	std::vector<Uint8> land(size, 0);
	std::vector<Uint8> shore(size, 0);
	std::vector<Uint8> interior(size, 0);
	for(int y=0; y<h; ++y)
		for(int x=0; x<w; ++x)
		{
			const int index=y*w+x;
			const Tile& cell=map->getTile(x, y);
			const bool permanent_resource=cell.resource.type!=NO_RES_TYPE
				&& globalContainer->resourcesTypes.get(
					cell.resource.type)->eternal;
			// Workers cross sand beaches too. Omitting that land leaves a wide
			// beach with no shore sources and falsely diagnoses a sealed farm.
			land[index]=!home_land[index] && !map->isWater(x, y)
				&& cell.building==NOGBID && !permanent_resource;
			if(land[index]) shore[index]=farming_cardinal_shoreline_mask[index];
			// Every harvestable crop pocket needs access, even at a flat or
			// concave water-distance contour with no gradient-derived marker.
			if(land[index] && !plan.forbidden[index]
			   && (cell.resource.type==CORN || cell.resource.type==WOOD))
				interior[index]=1;
		}
	for(int index=0; index<size; ++index)
	{
		if(!plan.coastal_envelope[index]) continue;
		const int x=index%w;
		const int y=index/w;
		const int barrier_distance=water_gradient.get_height(x, y);
		for(int dy=-1; dy<=1; ++dy)
			for(int dx=-1; dx<=1; ++dx)
			{
				if(std::abs(dx)+std::abs(dy)!=1) continue;
				const int nx=(x+dx+w)%w;
				const int ny=(y+dy+h)%h;
				const int next=ny*w+nx;
				const int next_distance=water_gradient.get_height(nx, ny);
				if(land[next] && !plan.forbidden[next]
				   && barrier_distance>=0 && next_distance>barrier_distance)
					interior[next]=1;
			}
	}

	const std::vector<Uint8> originally_forbidden=plan.forbidden;
	// A fully protected wheat patch has no open pocket for the repair below
	// to discover. Give it the same stable openings used by coastal repair,
	// then verify their access normally. Crop growth must not be the trigger.
	int fully_protected_openings=0;
	std::vector<Uint8> wheat_visited(size, 0);
	for(int start=0; start<size; ++start)
	{
		if(wheat_visited[start] || !land[start]
		   || !map->isResourceTakeable(start%w,start/w,CORN)) continue;
		std::vector<int> patch(1,start);
		wheat_visited[start]=1;
		bool has_open_wheat=false, coastal_patch=false;
		for(size_t head=0; head<patch.size(); ++head)
		{
			const int index=patch[head];
			has_open_wheat|=!plan.forbidden[index];
			coastal_patch|=plan.coastal_envelope[index]!=0;
			for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
			{
				const int next=((index/w+dy+h)%h)*w+(index%w+dx+w)%w;
				if(!wheat_visited[next] && land[next]
				   && map->isResourceTakeable(next%w,next/w,CORN))
				{
					wheat_visited[next]=1;
					patch.push_back(next);
				}
			}
		}
		if(has_open_wheat || !coastal_patch || patch.size()<2) continue;
		bool opened=false;
		for(size_t tile=0; tile<patch.size(); ++tile)
		{
			const int index=patch[tile];
			if(!Farming::isCoastalOpeningCell(index%w,index/w)) continue;
			plan.forbidden[index]=plan.protected_wheat[index]=0;
			interior[index]=1;
			++fully_protected_openings;
			opened=true;
		}
		// Stage 2 fallback: a multi-tile patch can miss the opening lattice.
		// Never open its fixed odd/odd seeds. Singletons were excluded above:
		// opening one would make access repair destroy the last regrowth seed.
		if(!opened)
		{
			for(size_t tile=0;tile<patch.size();++tile)
			{
				const int index=patch[tile];
				if(Farming::isInteriorSeed(index%w,index/w)) continue;
				plan.forbidden[index]=plan.protected_wheat[index]=0;
				interior[index]=1;
				++fully_protected_openings;
				break;
			}
		}
	}

	// Prefer openings on the farm's inland side before cutting its coastal
	// wall. The old repair envelope allowed only the outer wall to be opened.
	std::vector<Uint8> inland_envelope(size, 0);
	for(int index=0; index<size; ++index)
		inland_envelope[index]=plan.forbidden[index]
			&& !plan.coastal_envelope[index]
			&& !Farming::isInteriorSeed(index%w,index/w);
	const Farming::CoastalBarrierPorosityResult inland_porosity=
		Farming::makeSealedCoastalFarmBarriersPorous(w, h,
			land, shore, interior, inland_envelope, inland_envelope,
			plan.forbidden, plan.protected_wheat);
	// Stage 3: route repair may remove walls, never the fixed seed reserve.
	// A shortest-path tie must not expose a different odd/odd seed next pass.
	std::vector<Uint8> coastal_openings=plan.coastal_envelope;
	for(int index=0;index<size;++index)
		if(Farming::isInteriorSeed(index%w,index/w))coastal_openings[index]=0;
	plan.porosity=Farming::makeSealedCoastalFarmBarriersPorous(w, h,
		land, shore, interior, plan.coastal_envelope, coastal_openings,
		plan.forbidden, plan.protected_wheat);
	plan.porosity.openedTiles+=inland_porosity.openedTiles+fully_protected_openings;
	plan.porosity.restoredComponents+=inland_porosity.restoredComponents;
	plan.porosity.sealedComponents+=inland_porosity.restoredComponents;
	for(int index=0; index<size; ++index)
		if(originally_forbidden[index] && !plan.forbidden[index])
		{
			// A passive opening is only absence of ForbiddenArea. It is not a
			// strategic gate and must never request clearing infrastructure.
			plan.passive_opening_infrastructure_overlap+=
				strategic_gate_mask[index] || emergency_escape_mask[index];

		}
}

void Maxima::refresh_gate_defense(Context& echo)
{
	Map* map=echo.player->map;const int w=map->getW(),size=w*map->getH();
	barrier_tower_quality.assign(size,0);
	gate_defense_demand=0;
	// Distinguish possible tower positions from coverage by an actual tower.
	// Existing coverage suppresses the placement reward so a second tower
	// improves another entrance instead of piling onto the first one.
	for(size_t g=0;g<strategic_gates.size();++g)
	{
		const auto& gate=strategic_gates[g];
		std::vector<int> targets=gate;
		const auto approach=strategic_gate_approaches.find(gate[0]);
		if(approach!=strategic_gate_approaches.end())
			targets.insert(targets.end(),approach->second.begin(),approach->second.end());
		bool covered=false;
		for(int id=0;id<Building::MAX_COUNT;++id)
		{
			Building* tower=echo.player->team->myBuildings[id];
			if(!tower||tower->type->isBuildingSite||!tower->type->shootingRange)continue;
			const int center=map->normalizeY(tower->posY-tower->type->decTop)*w
				+map->normalizeX(tower->posX-tower->type->decLeft);
			bool covers=true;
			for(int tile:targets)covers&=tower_covers(map,center,tile,tower->type->shootingRange);
			covered|=covers;
		}
		if(covered)continue;
		const int range=globalContainer->buildingsTypes.getByType("defencetower",0,false)->shootingRange;
		bool feasible=false;
		for(int dy=-range;dy<=range;++dy)for(int dx=-range;dx<=range;++dx)
		{
			const int x=map->normalizeX(gate[0]%w+dx),y=map->normalizeY(gate[0]/w+dy),center=y*w+x;
			bool legal=true;
			for(int oy=-1;oy<=0;++oy)for(int ox=-1;ox<=0;++ox)
			{
				const int tile=map->normalizeY(y+oy)*w+map->normalizeX(x+ox);
				const Tile& cell=map->getTile(tile%w,tile/w);
				legal&=cell.terrain<16&&cell.building==NOGBID&&cell.resource.type==NO_RES_TYPE
					&&!emergency_escape_mask[tile]&&!strategic_gate_mask[tile];
			}
			if(!legal)continue;
			// Require an interior firing position near the approach, never a
			// beach-side tower that is itself the enemy's first contact.
			bool interior=false;
			if(approach!=strategic_gate_approaches.end())for(int tile:approach->second)
				interior|=map->warpDistMax(x,y,tile%w,tile/w)<=3;
			if(!interior)continue;
			int hits=0;for(int tile:targets)hits+=tower_covers(map,center,tile,range);
			if(hits!=int(targets.size()))continue;
			feasible=true;
			barrier_tower_quality[center]=std::min(100,int(barrier_tower_quality[center])+50);
		}
		gate_defense_demand+=feasible;
	}
}

int Maxima::connect_settlements_to_gates(Context& echo, FarmProtectionPlan& plan)
{
	if(!budget.farming_barrier_enabled)return 0;
	int unresolved=0;
	Map* map=echo.player->map;const int w=map->getW(),h=map->getH(),size=w*h;
	// Keep previously chosen internal connections open. Removing a contract
	// as soon as its resources disappear would let the farm grow across it.
	auto reserve=[&](int tile){emergency_escape_mask[tile]=1;
		plan.forbidden[tile]=plan.protected_wheat[tile]=0;};
	for(const auto& route:settlement_access_routes)for(int tile:route)reserve(tile);

	std::vector<Uint8> possible(size,0),open(size,0),outside(size,0);
	std::vector<int> queue;
	for(int i=0;i<size;++i)
	{
		const Tile& cell=map->getTile(i%w,i/w);
		possible[i]=!map->isWater(i%w,i/w)&&cell.building==NOGBID
			&&(cell.resource.type==NO_RES_TYPE
			||!globalContainer->resourcesTypes.get(cell.resource.type)->eternal);
		open[i]=possible[i]&&!plan.forbidden[i]
			&&(cell.resource.type==NO_RES_TYPE||emergency_escape_mask[i]);
		if(open[i]&&map->hasSand(i%w,i/w)&&farming_shoreline_mask[i])
		{outside[i]=1;queue.push_back(i);}
	}
	auto flood=[&](){for(size_t head=0;head<queue.size();++head)
	{
		int i=queue[head];
		for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
		{
			int next=map->normalizeY(i/w+dy)*w+map->normalizeX(i%w+dx);
			if(open[next]&&!outside[next]){outside[next]=1;queue.push_back(next);}
		}
	}};
	flood();
	// Repair towards the existing entrance network, not arbitrary coastline.
	// Protected coastal cells outside a designated gate remain hard walls.
	// The resource cost precedes length, so empty forbidden frontier holes are
	// released before crop destruction is considered. A seed is more expensive
	// than an ordinary crop, but an explicit necessary access contract can win.
	for(int id=0;id<Building::MAX_COUNT;++id)
	{
		Building* b=echo.player->team->myBuildings[id];
		if(!b||b->type->isVirtual||b->type->isBuildingSite)continue;
		std::vector<int> entrances;bool connected=false;
		for(int dy=-1;dy<=b->type->height;++dy)for(int dx=-1;dx<=b->type->width;++dx)
			if(dx==-1||dy==-1||dx==b->type->width||dy==b->type->height)
			{
				int i=map->normalizeY(b->posY+dy)*w+map->normalizeX(b->posX+dx);
				connected|=outside[i];
				if(possible[i] && (!farming_shoreline_mask[i]||emergency_escape_mask[i]))
					entrances.push_back(i);
			}
		if(connected)continue;
		if(entrances.empty()){++unresolved;continue;}
		using Cost=std::tuple<int,int,int>; // resource burden, steps, tile (stable tie)
		std::priority_queue<Cost,std::vector<Cost>,std::greater<Cost> > pending;
		std::vector<int> cost(size,INT_MAX),length(size,INT_MAX),parent(size,-1);
		auto burden=[&](int i){const Tile& cell=map->getTile(i%w,i/w);
			if(emergency_escape_mask[i]||cell.resource.type==NO_RES_TYPE)return 0;
			return 1+(cell.resource.type==CORN&&plan.protected_wheat[i]?4:0);};
		for(int i:entrances){cost[i]=burden(i);length[i]=0;pending.emplace(cost[i],0,i);}
		int goal=-1;
		while(!pending.empty())
		{
			int c,l,i;std::tie(c,l,i)=pending.top();pending.pop();
			if(c!=cost[i]||l!=length[i])continue;
			if(outside[i]&&emergency_escape_mask[i]){goal=i;break;}
			for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
			{
				int next=map->normalizeY(i/w+dy)*w+map->normalizeX(i%w+dx);
				// An internal connection must not silently create a third opening
				// around the firing lanes. Only designated gates cross the coast.
				if(!possible[next] || (farming_shoreline_mask[next]
					&&!emergency_escape_mask[next]))continue;
				int nc=c+burden(next),nl=l+1;
				if(nc<cost[next]||(nc==cost[next]&&nl<length[next]))
				{cost[next]=nc;length[next]=nl;parent[next]=i;pending.emplace(nc,nl,next);}
			}
		}
		// A missing route is observable, not a successful repair. This can
		// mean permanent terrain, no suitable gate pair, or a coastal building
		// with no inland entrance. Do not invent an unplanned wall breach.
		if(goal<0){++unresolved;continue;}
		std::vector<int> route;
		for(int i=goal;i>=0;i=parent[i]){route.push_back(i);reserve(i);open[i]=1;}
		settlement_access_routes.push_back(route);
		// The next building sees this connection; nearby buildings share one
		// branch instead of each acquiring their own breach through the farm.
		queue.clear();for(int i:route){outside[i]=1;queue.push_back(i);}flood();
	}
	return unresolved;
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
		{
			plan.forbidden[index]=0;
			plan.coastal_envelope[index]=0;
		}
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

	for(int index=0; index<size; ++index)
	{
		const int x=index%w;
		const int y=index/w;
		if(!map_info.is_discovered(x, y)) continue;
		// Audit the temporal contract as well as today's mask. Building/path
		// contracts are explicit overrides; harvesting a neighbor is not.
		{}
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

	farm_protection_mask=plan.forbidden;
	wheat_farm_protection_mask=plan.protected_wheat;
}

void Maxima::update_farming(Context& echo)
{

	initialize_farming_cache(echo);
	if(!budget.farming_enabled || !budget.farming_protection_enabled)
	{
		release_farming_protection(echo);
		return;
	}
	const Uint32 signature=compute_farming_topology_signature(echo);
	if(signature!=farming_topology_signature
	   || timer-last_barrier_topology_tick>=budget.barrier_topology_interval)
	{
		farming_topology_signature=signature;
		last_barrier_topology_tick=timer;
		update_barrier_topology(echo);
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
			plan.coastal_envelope[i]=0;
		}
	}

	restore_passive_coastal_access(echo, plan);
	// Passive porosity preserves farm pockets; it deliberately excludes the
	// starting mainland and does not promise resource clearing. Audit building
	// access after that pass, then expose the same durable routes to defence and
	// placement. Reversing this order can re-forbid an exit we just reserved.
	connect_settlements_to_gates(echo, plan);
	refresh_gate_defense(echo);
	resolve_wheat_invasion_clearing(echo, plan);
	int added=0;
	int removed=0;
	apply_farming_protection(echo, plan, added, removed);
	int blocked_gates=0;
	for(size_t i=0; i<strategic_gates.size(); ++i)
		blocked_gates+=!barrier_gate_is_clear(echo, strategic_gates[i]);
	int blocked_access=0;
	for(const auto& route:settlement_access_routes)
		for(int tile:route)if(echo.player->map->getTile(tile%echo.player->map->getW(),
			tile/echo.player->map->getW()).resource.type!=NO_RES_TYPE){++blocked_access;break;}
	const bool constraint_urgent=blocked_gates>0||blocked_access>0;
	if(farming_urgent!=constraint_urgent)
	{
		farming_urgent=constraint_urgent;
		director.invalidate();
	}


}

}
