#ifndef AI_MAXIMA_FOOD_SUPPLY_H
#define AI_MAXIMA_FOOD_SUPPLY_H
#include "PowerOfTwo.h"
#include "Material.h"
#include "field/UniformTraversal.h"
#include "field/TerrainTravel.h"
#include "ai/observation/ObservationQueries.h"
#include "MaterialSources.h"
#include "Building.h"
#include "BuildingType.h"
#include "AIMaximaFarming.h"
#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace AIMaxima {
inline int foodWidth(const AIEngine::AIWorldView* world) {return world->width;}
inline int foodHeight(const AIEngine::AIWorldView* world) {return world->height;}
inline bool foodMovementModifiers(const AIEngine::AIWorldView* world) {return world->terrainMovementModifiers;}
inline const TerrainProperties& foodTerrain(const AIEngine::AIWorldView* world,std::size_t index) {return AIEngine::ObservationQueries::terrain(*world,index);}
inline const BuildingType& foodBuildingType(const AIEngine::AIWorldView* world,const AIEngine::BuildingView& building) {return AIEngine::ObservationQueries::buildingType(*world,building);}

// A shared predicate keeps local and recovery estimates on the same routes.
inline bool foodTileAccessible(const AIEngine::AIWorldView* map,int x,int y,Uint32 teamMask,
    bool canSwim,const std::vector<Uint8>* protectedTiles)
{
    const auto tileIndex=map->tileIndex(x,y);
    const int index=y*map->width+x;
    return ((map->visibilityAt(map->tileIndex(x,y)).discovered&(teamMask))!=0)
        && (!(map->areasAt(tileIndex).forbidden&teamMask) || (protectedTiles && (*protectedTiles)[index]))
        && map->occupancyAt(tileIndex).building==NOGBID
        && (!MapState::resourceBlocksGround(map->state(),index) || MapState::hasMaterial(map->state(),map->tileIndex(x,y),MaterialId::Food))
        && (map->state().terrainProperties(map->tileIndex(x,y)).walkable || (canSwim && map->state().terrainProperties(map->tileIndex(x,y)).swimmable));
}

/// A standing food stock is supply as well as regrowth: mined over a
/// planning horizon it is a rate. A full-fertility cell regrows about one unit
/// per growth period, so a stack of `amount` spread over the horizon is worth
/// amount * period / horizon of a full tile. On infertile ground (Locust) this
/// is the only food there is, and a colony that ignores it never grows.
const int FoodGrowthPeriodTicks=186;
inline long long foodStockCapacityEquivalent(int amount, int stockHorizonTicks)
{
	amount=std::max(0, amount);
	return 65536LL*amount*FoodGrowthPeriodTicks/std::max(1,stockHorizonTicks);
}

// Custom rules change only the renewable contribution: standing food is still
// real supply. Keep the default arithmetic identical and use the engine's tiers.
inline long long effectiveNaturalGrowth(const AIEngine::AIWorldView* map, long long fertility)
{
    const GameHeader* captured=nullptr;
    captured=map->configuration.get();
    if(!captured)return fertility;
    const auto& rules=*captured;
    if (rules.isResourceGrowthDisabled()) return 0;
    return fertility / (1 << rules.getResourceScarcityLevel());
}

// Only modified terrain needs a priority queue. Preserve the ordinary-map BFS
// and its exact distances below. This reverse field measures a carrier's return
// to a building in neutral Chebyshev tile equivalents, just like AI travel fields.
template<class Visit>
inline void traverseWeightedFoodSupply(const AIEngine::AIWorldView* map,const std::vector<const AIEngine::BuildingView*>& buildings,
    Uint32 teamMask,bool canSwim,const std::vector<Uint8>* protectedTiles,Visit visit)
{
    const int width=foodWidth(map),size=width*foodHeight(map);
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
        for(int dy=-1;dy<=foodBuildingType(map,*building).height;++dy)
            for(int dx=-1;dx<=foodBuildingType(map,*building).width;++dx)
                if(dx==-1 || dx==foodBuildingType(map,*building).width || dy==-1 || dy==foodBuildingType(map,*building).height)
                    add(building->posX+dx,building->posY+dy,0);
    while(!queue.empty())
    {
        const auto [cost,index]=queue.top();queue.pop();
        if(distance[index]!=cost)continue;
        const auto action=visit(index,int((cost+GRADIENT_STEP-1)/GRADIENT_STEP));
        if(action==field::Visit::Stop)break;
        if(action==field::Visit::Skip)continue;
        const unsigned candidate=cost+field::terrainTravelCost(foodTerrain(map,index),
            canSwim?field::TerrainTravel::Swim:field::TerrainTravel::Walk);
        const int x=powerOfTwoRemainder(index, width),y=index/width;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            if(dx || dy)add(x+dx,y+dy,candidate);
    }
}

// Recovery estimate used only when all local catchments are empty. Search once
// from every completed food building, stopping one local radius beyond the
// nearest renewable food source. Discount distant supply for the longer carrier trip.
inline long long distantFoodCapacity(const AIEngine::AIWorldView* map,const std::vector<const AIEngine::BuildingView*>& buildings,
    Uint32 teamMask,bool canSwim,int radius,const Farming::ExactFertilityCache& fertility,
    const std::vector<Uint8>* protectedTiles, int stockHorizonTicks)
{
    if(foodMovementModifiers(map))
    {
        const int width=foodWidth(map);
        long long capacity=0;int stop=std::numeric_limits<int>::max();
        const int localRadius=std::max(1,radius);
        traverseWeightedFoodSupply(map,buildings,teamMask,canSwim,protectedTiles,
            [&](int index,int steps) {
                if(steps>stop)return field::Visit::Stop;
                const int x=powerOfTwoRemainder(index, width),y=index/width;
                const auto tileIndex=map->tileIndex(x,y);
                if(MapState::hasMaterial(map->state(),map->tileIndex(x,y),MaterialId::Food) && map->resourceAt(tileIndex).resource.amount>0)
                {
                    if(stop==std::numeric_limits<int>::max())stop=steps+localRadius;
                    capacity+=(AIResourceSources::renewablePotential(map->state(),map->tileIndex(x,y),MaterialId::Food)
                        +foodStockCapacityEquivalent(MapState::materialAmountAt(map->state(),map->tileIndex(x,y),MaterialId::Food),stockHorizonTicks))*localRadius
                        /std::max(localRadius,steps);
                }
                return field::Visit::Expand;
            });
        return capacity;
    }
    const int width=foodWidth(map),size=width*foodHeight(map);
    std::vector<int> distance(size,-1),queue;
    const auto add=[&](int x,int y,int steps) {
        x=map->normalizeX(x);y=map->normalizeY(y);const int index=y*width+x;
        if(distance[index]<0 && foodTileAccessible(map,x,y,teamMask,canSwim,protectedTiles))
        {distance[index]=steps;queue.push_back(index);}
    };
    for(const auto* b:buildings)
        for(int dy=-1;dy<=foodBuildingType(map,*b).height;++dy)
            for(int dx=-1;dx<=foodBuildingType(map,*b).width;++dx)
                if(dx==-1 || dx==foodBuildingType(map,*b).width || dy==-1 || dy==foodBuildingType(map,*b).height)
                    add(b->posX+dx,b->posY+dy,0);
    long long capacity=0;int stop=size;
    const int localRadius=std::max(1,radius);
    field::traverse(queue,{width,foodHeight(map)},field::Surrounding,
        [&](int index) {
            const int x=powerOfTwoRemainder(index, width),y=index/width,steps=distance[index];
            if(steps>stop)return field::Visit::Stop;
            const auto tileIndex=map->tileIndex(x,y);
            if(MapState::terrainSupportsMaterial(map->state(),map->tileIndex(x,y),MaterialId::Food)&&MapState::hasMaterial(map->state(),map->tileIndex(x,y),MaterialId::Food)&&map->resourceAt(tileIndex).resource.amount>0)
            {
                if(stop==size)stop=std::min(size,steps+localRadius);
                capacity+=(AIResourceSources::renewablePotential(map->state(),map->tileIndex(x,y),MaterialId::Food)
                    +foodStockCapacityEquivalent(MapState::materialAmountAt(map->state(),map->tileIndex(x,y),MaterialId::Food),stockHorizonTicks))*localRadius
                    /std::max(localRadius,steps);
            }
            return steps>=stop?field::Visit::Skip:field::Visit::Expand;
        },[&](int index,int px,int py){add(px,py,distance[index]+1);});
    return capacity;
}

// Shared by policy and read-only tournament observations. Capacity combines
// renewable material supply with standing stock over the planning horizon.
inline long long reachableFoodCapacity(const AIEngine::AIWorldView* map, const AIEngine::BuildingView* building,
    Uint32 teamMask, bool canSwim, int radius,
    const Farming::ExactFertilityCache& fertility,
    const std::vector<Uint8>* protectedTiles, std::set<int>* shared_tiles,
    int stockHorizonTicks)
{
    if(foodMovementModifiers(map))
    {
        long long capacity=0;const int width=foodWidth(map);
        traverseWeightedFoodSupply(map,std::vector<const AIEngine::BuildingView*>{building},teamMask,canSwim,protectedTiles,
            [&](int index,int steps) {
                if(steps>radius)return field::Visit::Stop;
                const int x=powerOfTwoRemainder(index, width),y=index/width;
                const auto tileIndex=map->tileIndex(x,y);
                if(MapState::hasMaterial(map->state(),map->tileIndex(x,y),MaterialId::Food) && map->resourceAt(tileIndex).resource.amount>0
                    && (!shared_tiles || shared_tiles->insert(index).second))
                    capacity+=AIResourceSources::renewablePotential(map->state(),map->tileIndex(x,y),MaterialId::Food)
                        +foodStockCapacityEquivalent(MapState::materialAmountAt(map->state(),map->tileIndex(x,y),MaterialId::Food),stockHorizonTicks);
                return field::Visit::Expand;
            });
        return capacity;
    }
	const int width=foodWidth(map);

	// Empty ground remains traversable, but only existing food sources contribute
	// food capacity. Fertility alone does not imply a food supply.
	const auto accessible=[&](int x, int y) {
		return foodTileAccessible(map,x,y,teamMask,canSwim,protectedTiles);
	};
	std::map<int,int> distance;
	std::vector<int> queue;
	for(int dy=-1;dy<=foodBuildingType(map,*building).height;++dy)
		for(int dx=-1;dx<=foodBuildingType(map,*building).width;++dx)
		{
			if(dx!=-1 && dx!=foodBuildingType(map,*building).width
			   && dy!=-1 && dy!=foodBuildingType(map,*building).height) continue;
			const int x=map->normalizeX(building->posX+dx);
			const int y=map->normalizeY(building->posY+dy);
			const int index=y*width+x;
			if(accessible(x,y) && distance.insert(std::make_pair(index,0)).second)
				queue.push_back(index);
		}
	long long capacity=0;
	field::traverse(queue,{width,foodHeight(map)},field::Surrounding,
		[&](int index) {
			const int x=powerOfTwoRemainder(index, width),y=index/width;
			const auto tileIndex=map->tileIndex(x,y);
			if(MapState::terrainSupportsMaterial(map->state(),map->tileIndex(x,y),MaterialId::Food)&&MapState::hasMaterial(map->state(),map->tileIndex(x,y),MaterialId::Food)&&map->resourceAt(tileIndex).resource.amount>0
			   &&(!shared_tiles||shared_tiles->insert(index).second))
				capacity+=AIResourceSources::renewablePotential(map->state(),map->tileIndex(x,y),MaterialId::Food)+foodStockCapacityEquivalent(MapState::materialAmountAt(map->state(),map->tileIndex(x,y),MaterialId::Food),stockHorizonTicks);
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
