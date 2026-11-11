/* Maxima private farming primitives. */

#include "PowerOfTwo.h"
#include "field/PriorityTraversal.h"
#include "AIMaximaFarming.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <deque>
#include <queue>
#include <tuple>

namespace AIMaxima
{
namespace Farming
{

namespace
{

	int clamp100(int value)
	{
		return std::max(0, std::min(100, value));
	}

	int toroidalChebyshevDistance(int first, int second, int width, int height)
	{
		int dx=std::abs(dimensionRemainder(first, width)-dimensionRemainder(second, width));
		int dy=std::abs(first/width-second/width);
		dx=std::min(dx, width-dx);
		dy=std::min(dy, height-dy);
		return std::max(dx, dy);
	}

}

void ExactFertilityCache::rebuild(int width, int height,
	const std::vector<uint8_t>& water, const std::vector<uint8_t>& sand,
	FertilityCalculationPath path)
{
	field.rebuild(width,height,water,sand,static_cast<Fertility::Path>(path));
	generation=0;
}

uint32_t usefulExpansionCapacity(uint32_t fertility, int amount,
	int availableNeighbors, bool wheat)
{
	return Fertility::usefulExpansionCapacity(fertility,amount,availableNeighbors,wheat);
}

bool fertilityWithinPercentBand(uint32_t fertility, int minimumPercent,
	int maximumPercent)
{
	minimumPercent=std::max(0, std::min(100, minimumPercent));
	maximumPercent=std::max(0, std::min(100, maximumPercent));
	if(minimumPercent>maximumPercent)
		std::swap(minimumPercent, maximumPercent);
	const uint64_t scaled=uint64_t(fertility)*100u;
	return scaled>=uint64_t(minimumPercent)*65536u
		&& scaled<=uint64_t(maximumPercent)*65536u;
}

bool canProtectWithoutSplittingAccess(uint16_t openNeighborhood)
{
	const uint16_t ring=openNeighborhood & 0x1ef;
	if(!ring) return false;
	uint16_t reached=ring & -ring;
	for(int pass=0; pass<8; ++pass)
		for(int i=0; i<9; ++i)
			if(reached & (1<<i))
				for(int j=0; j<9; ++j)
					if((ring & (1<<j)) && std::abs(i%3-j%3)<=1
					   && std::abs(i/3-j/3)<=1)
						reached|=1<<j;
	return reached==ring;
}

bool hasAdjacentProtectedWheat(const std::vector<uint8_t>& protectedWheat,
	int width, int height, int x, int y)
{
	assert(width>0 && height>0);
	assert(protectedWheat.size()==size_t(width*height));
	for(int dy=-1; dy<=1; ++dy)
		for(int dx=-1; dx<=1; ++dx)
		{
			if(!dx && !dy)
				continue;
			const int nx=dimensionRemainder(x+dimensionRemainder(dx, width)+width, width);
			const int ny=dimensionRemainder(y+dimensionRemainder(dy, height)+height, height);
			if(protectedWheat[ny*width+nx])
				return true;
		}
	return false;
}

ReservationClearingSelection selectResourcePreservingCirculation(
	int width, int height, const std::vector<int>& footprint,
	const std::vector<int>& circulation,
	const std::vector<uint8_t>& preservedResources,
	const std::vector<int>& resourceBurden,
	const std::vector<uint8_t>& reachable)
{
	assert(width>0 && height>0);
	const int size=width*height;
	assert(preservedResources.size()==size_t(size));
	assert(resourceBurden.size()==size_t(size));
	assert(reachable.size()==size_t(size));
	ReservationClearingSelection result;
	// Called per reservation member every maintenance plan: per-thread
	// generation-stamped scratch replaces five full-map arrays per call. An
	// unstamped cell reads as the former default (0, UINT64_MAX, INT_MAX, -1).
	struct Scratch
	{
		std::vector<uint32_t> footprint,allowed,node;
		std::vector<uint64_t> cost;std::vector<int> length,parent;
		uint32_t generation=0;
	};
	thread_local Scratch s;
	if(int(s.node.size())!=size)
	{
		s.footprint.assign(size,0);s.allowed.assign(size,0);s.node.assign(size,0);
		s.cost.assign(size,UINT64_MAX);s.length.assign(size,INT_MAX);s.parent.assign(size,-1);
		s.generation=0;
	}
	if(++s.generation==0)
	{
		std::fill(s.footprint.begin(),s.footprint.end(),0);std::fill(s.allowed.begin(),s.allowed.end(),0);
		std::fill(s.node.begin(),s.node.end(),0);s.generation=1;
	}
	const uint32_t g=s.generation;
	const auto footprintMask=[&](int index){return s.footprint[index]==g;};
	const auto allowed=[&](int index){return s.allowed[index]==g;};
	const auto touch=[&](int index)
	{if(s.node[index]!=g){s.node[index]=g;s.cost[index]=UINT64_MAX;s.length[index]=INT_MAX;s.parent[index]=-1;}};
	const auto cost=[&](int index){return s.node[index]==g?s.cost[index]:UINT64_MAX;};
	const auto length=[&](int index){return s.node[index]==g?s.length[index]:INT_MAX;};
	const auto parent=[&](int index){return s.node[index]==g?s.parent[index]:-1;};
	for(int index:footprint)if(index>=0&&index<size)s.footprint[index]=g;
	for(int index:circulation)if(index>=0&&index<size&&!footprintMask(index))
	{
		s.allowed[index]=g;
		if(!preservedResources[index])result.tiles.push_back(index);
		else ++result.preservedResourceTiles;
	}

	// Sources are actual worker-reachable cells, not arbitrary empty pockets
	// beside the building. Search only within the promised circulation land.
	typedef std::tuple<uint64_t,int,int> Entry;
	std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry> > queue;
	auto resourceCost=[&](int index)->uint64_t
	{return preservedResources[index]?std::max(1,resourceBurden[index]):0;};
	for(int index:circulation)if(index>=0&&index<size&&allowed(index))
	{
		bool source=reachable[index]!=0;
		const int x=dimensionRemainder(index, width),y=index/width;
		for(int dy=-1;dy<=1&&!source;++dy)for(int dx=-1;dx<=1&&!source;++dx)
		{
			const int next=(dimensionRemainder(y+dy+height, height))*width+dimensionRemainder(x+dx+width, width);
			source=!footprintMask(next)&&reachable[next];
		}
		if(source)
		{touch(index);s.cost[index]=resourceCost(index);s.length[index]=1;queue.push(Entry(s.cost[index],1,index));}
	}
	int entrance=-1;
	field::traversePriority(queue,{width,height},field::Surrounding,
		[](const Entry& entry){return std::get<2>(entry);},
		[&](const Entry& entry) {
			const int index=std::get<2>(entry),x=dimensionRemainder(index, width),y=index/width;
			if(std::get<0>(entry)!=cost(index) || std::get<1>(entry)!=length(index))return field::Visit::Skip;
			const int neighbors[4]={y*width+dimensionRemainder(x+width-1, width),y*width+dimensionRemainder(x+1, width),
				(dimensionRemainder(y+height-1, height))*width+x,(dimensionRemainder(y+1, height))*width+x};
			for(int next:neighbors)if(footprintMask(next)){entrance=index;break;}
			return entrance>=0?field::Visit::Stop:field::Visit::Expand;
		},[&](const Entry& entry,int px,int py) {
			const int index=std::get<2>(entry);
			const int next=(dimensionRemainder(py+height, height))*width+dimensionRemainder(px+width, width);
			if(!allowed(next))return;
			const uint64_t nextCost=cost(index)+resourceCost(next);
			const int nextLength=length(index)+1;
			if(nextCost<cost(next)||(nextCost==cost(next)&&nextLength<length(next)))
			{
				touch(next);s.cost[next]=nextCost;s.length[next]=nextLength;s.parent[next]=index;
				queue.push(Entry(nextCost,nextLength,next));
			}
		});
	if(entrance>=0)
	{
		if(cost(entrance)>0)result.fallbackEntranceTile=entrance;
		for(int index=entrance;index>=0;index=parent(index))
			result.tiles.push_back(index);
	}
	std::sort(result.tiles.begin(),result.tiles.end());
	result.tiles.erase(std::unique(result.tiles.begin(),result.tiles.end()),result.tiles.end());
	return result;
}




int woodSupplyScore(int accessibleWood, int population,
	int scale, int populationOffset)
{
	return clamp100(std::max(0, accessibleWood)*std::max(1, scale)
		/(std::max(0, population)+std::max(1, populationOffset)));
}

int woodClearPressure(int spaceCapacity, int woodSupply,
	int recentConstructionFailures, int growthDemand, int base,
	int spaceDivisor, int supplyDivisor, int constructionDivisor,
	int growthDivisor, int constructionFailurePressure)
{
	spaceDivisor=std::max(1, spaceDivisor);
	supplyDivisor=std::max(1, supplyDivisor);
	constructionDivisor=std::max(1, constructionDivisor);
	growthDivisor=std::max(1, growthDivisor);
	const int constructionPressure=clamp100(
		std::max(0, recentConstructionFailures)
			*std::max(0, constructionFailurePressure));
	return clamp100(base+(50-clamp100(spaceCapacity))/spaceDivisor
		+(clamp100(woodSupply)-50)/supplyDivisor
		+constructionPressure/constructionDivisor
		-(clamp100(growthDemand)-50)/growthDivisor);
}

uint32_t minimumWoodFertility(int pressure, int basePercent,
	int pressurePercent)
{
	const int percent=std::max(0, basePercent)
		+(std::max(0, pressurePercent)*clamp100(pressure)+50)/100;
	return uint32_t(std::min(100, percent))*65536u/100u;
}

bool openingSpaceConstrained(int spaceCapacity, int urgentSpaceThreshold,
	int recentConstructionFailures, int failureThreshold,
	int ticksSinceConstructionFailure, int failureWindowTicks)
{
	return spaceCapacity<urgentSpaceThreshold
		||(recentConstructionFailures>=failureThreshold
			&&ticksSinceConstructionFailure<=failureWindowTicks);
}

}
}
