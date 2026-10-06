#ifndef AI_MAXIMA_FOOD_SUPPLY_H
#define AI_MAXIMA_FOOD_SUPPLY_H
#include "field/UniformTraversal.h"
#include "field/TerrainTravel.h"
#include "Map.h"
#include "shared_runtime/RuntimeObservation.h"
#include <type_traits>
#include "Game.h"
#include "Building.h"
#include "BuildingType.h"
#include "AIMaximaFarming.h"
#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace AIMaxima {
// A shared predicate keeps local and recovery estimates on the same routes.
template<class MapValue>
inline bool foodTileAccessible(MapValue* map,int x,int y,Uint32 teamMask,
    bool canSwim,const std::vector<Uint8>* protectedTiles)
{
    const auto index=map->coordToIndex(x,y);
    if constexpr(std::is_same_v<MapValue,AISharedRuntime::Read::Map>)
    {
        const auto& world=*map->world;
        if(!(world.visibilityAt(index).discovered&teamMask)) return false;
        if((world.areasAt(index).forbidden&teamMask)
            && !(protectedTiles && (*protectedTiles)[index])) return false;
        if(world.occupancyAt(index).building!=NOGBID) return false;
        const auto resource=world.resourceAt(index).resource;
        if(resource.type!=NO_RES_TYPE && resource.type!=WHEAT) return false;
        const auto& terrain=map->terrainPropertiesAt(index);
        return terrain.walkable || (canSwim && terrain.swimmable);
    }
    else
    {
        if(!map->isMapDiscovered(x,y,teamMask)) return false;
        if(map->isForbidden(x,y,teamMask)
            && !(protectedTiles && (*protectedTiles)[index])) return false;
        if(map->getBuilding(x,y)!=NOGBID) return false;
        const auto resource=map->getResource(x,y);
        if(resource.type!=NO_RES_TYPE && resource.type!=WHEAT) return false;
        const auto& terrain=map->terrainPropertiesAt(x,y);
        return terrain.walkable || (canSwim && terrain.swimmable);
    }
}

template<class MapValue>
inline auto foodResourceAt(MapValue* map,int index,int x,int y)
{
    if constexpr(std::is_same_v<MapValue,AISharedRuntime::Read::Map>)
        return map->world->resourceAt(index).resource;
    else
        return map->getResource(x,y);
}

/// A standing stack of wheat is supply as well as regrowth: mined over a
/// planning horizon it is a rate. A full-fertility cell regrows about one unit
/// per growth period, so a stack of `amount` spread over the horizon is worth
/// amount * period / horizon of a full tile. On infertile ground (Locust) this
/// is the only food there is, and a colony that ignores it never grows.
const int WheatGrowthPeriodTicks=186;
inline long long wheatStockFertilityEquivalent(int amount, int stockHorizonTicks)
{
	amount=std::max(0, std::min(8, amount));
	return 65536LL*amount*WheatGrowthPeriodTicks/std::max(1,stockHorizonTicks);
}

// Custom rules change only the renewable contribution: standing grain is still
// real supply. Keep the default arithmetic identical and use the engine's tiers.
template<class MapValue>
inline long long effectiveWheatRegrowth(MapValue* map, long long fertility)
{
    const GameHeader* captured=nullptr;
    if constexpr(std::is_same_v<MapValue,AISharedRuntime::Read::Map>) captured=map->world->configuration.get();
    else if(map->game) captured=&map->game->gameHeader;
    if(!captured)return fertility;
    const auto& rules=*captured;
    if (rules.isResourceGrowthDisabled()) return 0;
    return fertility / (1 << rules.getResourceScarcityLevel());
}

// Only modified terrain needs a priority queue. Preserve the ordinary-map BFS
// and its exact distances below. This reverse field measures a carrier's return
// to a building in neutral Chebyshev tile equivalents, just like AI travel fields.
template<class Visit,class MapValue,class BuildingValue>
inline void traverseWeightedFoodSupply(MapValue* map,const std::vector<BuildingValue*>& buildings,
    Uint32 teamMask,bool canSwim,const std::vector<Uint8>* protectedTiles,Visit visit)
{
    const int width=map->getW(),size=width*map->getH();
    constexpr unsigned infinity=std::numeric_limits<unsigned>::max();
    std::vector<unsigned> distance(size,infinity);
    using Entry=std::pair<unsigned,int>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
    auto add=[&](int x,int y,unsigned cost) {
        x=map->normalizeX(x);y=map->normalizeY(y);const int index=y*width+x;
        if(cost<distance[index] && foodTileAccessible(map,x,y,teamMask,canSwim,protectedTiles))
        {distance[index]=cost;queue.emplace(cost,index);}
    };
    for(const auto* building:buildings)
        for(int dy=-1;dy<=building->type->height;++dy)
            for(int dx=-1;dx<=building->type->width;++dx)
                if(dx==-1 || dx==building->type->width || dy==-1 || dy==building->type->height)
                    add(building->posX+dx,building->posY+dy,0);
    while(!queue.empty())
    {
        const auto [cost,index]=queue.top();queue.pop();
        if(distance[index]!=cost)continue;
        const auto action=visit(index,int((cost+GRADIENT_STEP-1)/GRADIENT_STEP));
        if(action==field::Visit::Stop)break;
        if(action==field::Visit::Skip)continue;
        const unsigned candidate=cost+field::terrainTravelCost(map->terrainPropertiesAt(index),
            canSwim?field::TerrainTravel::Swim:field::TerrainTravel::Walk);
        const int x=index%width,y=index/width;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            if(dx || dy)add(x+dx,y+dy,candidate);
    }
}

// Recovery estimate used only when all local catchments are empty. Search once
// from every completed food building, stopping one local radius beyond the
// nearest growing wheat. Discount distant supply for the longer carrier trip.
template<class MapValue,class BuildingValue>
inline long long distantFoodCapacity(MapValue* map,const std::vector<BuildingValue*>& buildings,
    Uint32 teamMask,bool canSwim,int radius,const Farming::ExactFertilityCache& fertility,
    const std::vector<Uint8>* protectedTiles, int stockHorizonTicks)
{
    if(map->hasTerrainMovementModifiers())
    {
        const int width=map->getW();
        long long capacity=0;int stop=std::numeric_limits<int>::max();
        const int localRadius=std::max(1,radius);
        traverseWeightedFoodSupply(map,buildings,teamMask,canSwim,protectedTiles,
            [&](int index,int steps) {
                if(steps>stop)return field::Visit::Stop;
                const int x=index%width,y=index/width;
                const auto resource=foodResourceAt(map,index,x,y);
                if(resource.type==WHEAT && resource.amount>0)
                {
                    if(stop==std::numeric_limits<int>::max())stop=steps+localRadius;
                    capacity+=(effectiveWheatRegrowth(map,fertility.at(x,y))
                        +wheatStockFertilityEquivalent(resource.amount,stockHorizonTicks))*localRadius
                        /std::max(localRadius,steps);
                }
                return field::Visit::Expand;
            });
        return capacity;
    }
    const int width=map->getW(),size=width*map->getH();
    std::vector<int> distance(size,-1),queue;
    const auto add=[&](int x,int y,int steps) {
        x=map->normalizeX(x);y=map->normalizeY(y);const int index=y*width+x;
        if(distance[index]<0 && foodTileAccessible(map,x,y,teamMask,canSwim,protectedTiles))
        {distance[index]=steps;queue.push_back(index);}
    };
    for(const auto* b:buildings)
        for(int dy=-1;dy<=b->type->height;++dy)
            for(int dx=-1;dx<=b->type->width;++dx)
                if(dx==-1 || dx==b->type->width || dy==-1 || dy==b->type->height)
                    add(b->posX+dx,b->posY+dy,0);
    long long capacity=0;int stop=size;
    const int localRadius=std::max(1,radius);
    field::traverse(queue,{width,map->getH()},field::Surrounding,
        [&](int index) {
            const int x=index%width,y=index/width,steps=distance[index];
            if(steps>stop)return field::Visit::Stop;
            const auto resource=foodResourceAt(map,index,x,y);
            if((map->terrainPropertiesAt(index).allowedResources & (1u<<WHEAT))&&resource.type==WHEAT&&resource.amount>0)
            {
                if(stop==size)stop=std::min(size,steps+localRadius);
                capacity+=(effectiveWheatRegrowth(map,fertility.at(x,y))
                    +wheatStockFertilityEquivalent(resource.amount,stockHorizonTicks))*localRadius
                    /std::max(localRadius,steps);
            }
            return steps>=stop?field::Visit::Skip:field::Visit::Expand;
        },[&](int index,int px,int py){add(px,py,distance[index]+1);});
    return capacity;
}

// Shared by policy and read-only tournament observations. Corn is the engine's
// resource name for wheat; fertility measures its recurring growing capacity.
template<class MapValue,class BuildingValue>
inline long long reachableFoodCapacity(MapValue* map, BuildingValue* building,
    Uint32 teamMask, bool canSwim, int radius,
    const Farming::ExactFertilityCache& fertility,
    const std::vector<Uint8>* protectedTiles, std::set<int>* shared_tiles,
    int stockHorizonTicks)
{
    if(map->hasTerrainMovementModifiers())
    {
        long long capacity=0;const int width=map->getW();
        traverseWeightedFoodSupply(map,std::vector<BuildingValue*>{building},teamMask,canSwim,protectedTiles,
            [&](int index,int steps) {
                if(steps>radius)return field::Visit::Stop;
                const int x=index%width,y=index/width;
                const auto resource=foodResourceAt(map,index,x,y);
                if(resource.type==WHEAT && resource.amount>0
                    && (!shared_tiles || shared_tiles->insert(index).second))
                    capacity+=effectiveWheatRegrowth(map,fertility.at(x,y))
                        +wheatStockFertilityEquivalent(resource.amount,stockHorizonTicks);
                return field::Visit::Expand;
            });
        return capacity;
    }
	const int width=map->getW();

	// Empty ground remains traversable, but only existing corn contributes
	// food capacity. Fertility alone does not imply a food supply.
	const auto accessible=[&](int x, int y) {
		return foodTileAccessible(map,x,y,teamMask,canSwim,protectedTiles);
	};
	std::map<int,int> distance;
	std::vector<int> queue;
	for(int dy=-1;dy<=building->type->height;++dy)
		for(int dx=-1;dx<=building->type->width;++dx)
		{
			if(dx!=-1 && dx!=building->type->width
			   && dy!=-1 && dy!=building->type->height) continue;
			const int x=map->normalizeX(building->posX+dx);
			const int y=map->normalizeY(building->posY+dy);
			const int index=y*width+x;
			if(accessible(x,y) && distance.insert(std::make_pair(index,0)).second)
				queue.push_back(index);
		}
	long long capacity=0;
	field::traverse(queue,{width,map->getH()},field::Surrounding,
		[&](int index) {
			const int x=index%width,y=index/width;
			const auto resource=foodResourceAt(map,index,x,y);
			if((map->terrainPropertiesAt(index).allowedResources & (1u<<WHEAT))&&resource.type==WHEAT&&resource.amount>0
			   &&(!shared_tiles||shared_tiles->insert(index).second))
				capacity+=effectiveWheatRegrowth(map,fertility.at(x,y))+wheatStockFertilityEquivalent(resource.amount,stockHorizonTicks);
			return distance[index]>=radius?field::Visit::Skip:field::Visit::Expand;
		},[&](int index,int px,int py) {
			const int nx=map->normalizeX(px),ny=map->normalizeY(py),adjacent=ny*width+nx;
			if(accessible(nx,ny)&&distance.insert(std::make_pair(adjacent,distance[index]+1)).second)
				queue.push_back(adjacent);
		});
	return capacity;
}
}
#endif
