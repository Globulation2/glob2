#ifndef AI_MAXIMA_FOOD_SUPPLY_H
#define AI_MAXIMA_FOOD_SUPPLY_H
#include "Map.h"
#include "Building.h"
#include "BuildingType.h"
#include "AIMaximaFarming.h"
#include <map>
#include <set>
#include <vector>

namespace AIMaxima {
// Shared by policy and read-only tournament observations. Corn is the engine's
// resource name for wheat; fertility measures its recurring growing capacity.
inline long long reachableFoodCapacity(Map* map, Building* building,
    Uint32 teamMask, bool canSwim, int radius,
    const Farming::ExactFertilityCache& fertility,
    const std::vector<Uint8>* protectedTiles, std::set<int>* shared_tiles)
{
	const int width=map->getW();
	
	// Empty ground remains traversable, but only existing corn contributes
	// food capacity. Fertility alone does not imply a food supply.
	const auto accessible=[&](int x, int y) {
		const Tile& tile=map->getTile(x,y);
		const int index=y*width+x;
		return map->isMapDiscovered(x,y,teamMask)
			&& (!(tile.forbidden&teamMask) || (protectedTiles && (*protectedTiles)[index]))
			&& tile.building==NOGBID
			&& (tile.resource.type==NO_RES_TYPE || tile.resource.type==CORN)
			&& (canSwim || !map->isWater(x,y));
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
		if(map->isGrass(x,y) && tile.resource.type==CORN
		   && tile.resource.amount>0
		   && (!shared_tiles || shared_tiles->insert(index).second))
			capacity+=fertility.at(x,y);
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
