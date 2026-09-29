#ifndef AI_MAXIMA_FOOD_SUPPLY_H
#define AI_MAXIMA_FOOD_SUPPLY_H
#include "Map.h"
#include "Building.h"
#include "BuildingType.h"
#include "AIMaximaFarming.h"
#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace AIMaxima {
// A shared predicate keeps local and recovery estimates on the same routes.
inline bool foodTileAccessible(Map* map,int x,int y,Uint32 teamMask,
    bool canSwim,const std::vector<Uint8>* protectedTiles)
{
    const Tile& tile=map->getTile(x,y);
    const int index=y*map->getW()+x;
    return map->isMapDiscovered(x,y,teamMask)
        && (!(tile.forbidden&teamMask) || (protectedTiles && (*protectedTiles)[index]))
        && tile.building==NOGBID
        && (tile.resource.type==NO_RES_TYPE || tile.resource.type==WHEAT)
        && (canSwim || !map->isWater(x,y));
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

// Recovery estimate used only when all local catchments are empty. Search once
// from every completed food building, stopping one local radius beyond the
// nearest growing wheat. Discount distant supply for the longer carrier trip.
inline long long distantFoodCapacity(Map* map,const std::vector<Building*>& buildings,
    Uint32 teamMask,bool canSwim,int radius,const Farming::ExactFertilityCache& fertility,
    const std::vector<Uint8>* protectedTiles, int stockHorizonTicks)
{
    const int width=map->getW(),size=width*map->getH();
    std::vector<int> distance(size,-1),queue;
    const auto add=[&](int x,int y,int steps) {
        x=map->normalizeX(x);y=map->normalizeY(y);const int index=y*width+x;
        if(distance[index]<0 && foodTileAccessible(map,x,y,teamMask,canSwim,protectedTiles))
        {distance[index]=steps;queue.push_back(index);}
    };
    for(const Building* b:buildings)
        for(int dy=-1;dy<=b->type->height;++dy)
            for(int dx=-1;dx<=b->type->width;++dx)
                if(dx==-1 || dx==b->type->width || dy==-1 || dy==b->type->height)
                    add(b->posX+dx,b->posY+dy,0);
    long long capacity=0;int stop=size;
    const int localRadius=std::max(1,radius);
    for(size_t next=0;next<queue.size();++next)
    {
        const int index=queue[next],x=index%width,y=index/width,steps=distance[index];
        if(steps>stop)break;
        const Tile& tile=map->getTile(x,y);
        if(map->isGrass(x,y) && tile.resource.type==WHEAT && tile.resource.amount>0)
        {
            if(stop==size)stop=std::min(size,steps+localRadius);
            capacity+=(static_cast<long long>(fertility.at(x,y))
                +wheatStockFertilityEquivalent(tile.resource.amount,stockHorizonTicks))*localRadius
                /std::max(localRadius,steps);
        }
        if(steps>=stop)continue;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            if(dx || dy)add(x+dx,y+dy,steps+1);
    }
    return capacity;
}

// Shared by policy and read-only tournament observations. Corn is the engine's
// resource name for wheat; fertility measures its recurring growing capacity.
inline long long reachableFoodCapacity(Map* map, Building* building,
    Uint32 teamMask, bool canSwim, int radius,
    const Farming::ExactFertilityCache& fertility,
    const std::vector<Uint8>* protectedTiles, std::set<int>* shared_tiles,
    int stockHorizonTicks)
{
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
	for(size_t next=0;next<queue.size();++next)
	{
		const int index=queue[next], x=index%width, y=index/width;
		const Tile& tile=map->getTile(x,y);
		if(map->isGrass(x,y) && tile.resource.type==WHEAT
		   && tile.resource.amount>0
		   && (!shared_tiles || shared_tiles->insert(index).second))
			capacity+=fertility.at(x,y)
				+wheatStockFertilityEquivalent(tile.resource.amount,stockHorizonTicks);
		if(distance[index]>=radius) continue;
		for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
		{
			if(!dx && !dy) continue;
			const int nx=map->normalizeX(x+dx), ny=map->normalizeY(y+dy);
			const int adjacent=ny*width+nx;
			if(accessible(nx,ny)
			   && distance.insert(std::make_pair(adjacent,distance[index]+1)).second)
				queue.push_back(adjacent);
		}
	}
	return capacity;
}
}
#endif
