/* Maxima private farming primitives. */

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
	const int KERNEL_RADIUS=15;
	const int KERNEL_WIDTH=31;
	const int BOX_WIDTH=16;
	const uint32_t OFFSET_WEIGHT[KERNEL_WIDTH]={
		1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
		15,14,13,12,11,10,9,8,7,6,5,4,3,2,1
	};

	int clamp100(int value)
	{
		return std::max(0, std::min(100, value));
	}

	int toroidalChebyshevDistance(int first, int second, int width, int height)
	{
		int dx=std::abs(first%width-second%width);
		int dy=std::abs(first/width-second/width);
		dx=std::min(dx, width-dx);
		dy=std::min(dy, height-dy);
		return std::max(dx, dy);
	}

}

ExactFertilityCache::ExactFertilityCache()
	: width(0), height(0), waterTiles(0), sandTiles(0),
	  usedPath(AdaptiveFertilityPath)
{
}

bool ExactFertilityCache::validFor(int expectedWidth, int expectedHeight) const
{
	return width==expectedWidth && height==expectedHeight
		&& fertility.size()==size_t(width*height);
}

uint32_t ExactFertilityCache::at(int x, int y) const
{
	assert(validFor(width, height));
	if(x<0 || x>=width) x=(x%width+width)%width;
	if(y<0 || y>=height) y=(y%height+height)%height;
	return fertility[y*width+x];
}

int ExactFertilityCache::wx(int x, int offset) const
{
	return wrappedX[(offset+KERNEL_WIDTH)*width+x];
}

int ExactFertilityCache::wy(int y, int offset) const
{
	return wrappedY[(offset+KERNEL_WIDTH)*height+y];
}

void ExactFertilityCache::buildWrappedIndexes()
{
	// Rolling boxes need +/-16 and mirrored corrections need +/-30.
	wrappedX.resize((KERNEL_WIDTH*2+1)*width);
	wrappedY.resize((KERNEL_WIDTH*2+1)*height);
	for(int offset=-KERNEL_WIDTH; offset<=KERNEL_WIDTH; ++offset)
	{
		for(int x=0; x<width; ++x)
			wrappedX[(offset+KERNEL_WIDTH)*width+x]
				=(x+offset%width+width)%width;
		for(int y=0; y<height; ++y)
			wrappedY[(offset+KERNEL_WIDTH)*height+y]
				=(y+offset%height+height)%height;
	}
}

void ExactFertilityCache::buildWaterConvolution(
	const std::vector<uint8_t>& water)
{
	const int size=width*height;
	first.assign(size, 0);
	second.assign(size, 0);
	fertility.assign(size, 0);

	// A length-16 forward box followed by a length-16 backward box is
	// exactly the triangular 16-|offset| kernel. Repeat vertically.
	for(int y=0; y<height; ++y)
	{
		uint32_t sum=0;
		for(int dx=0; dx<BOX_WIDTH; ++dx)
			sum+=water[y*width+wx(0, dx)];
		for(int x=0; x<width; ++x)
		{
			first[y*width+x]=sum;
			sum-=water[y*width+x];
			sum+=water[y*width+wx(x, BOX_WIDTH)];
		}
	}
	for(int y=0; y<height; ++y)
	{
		uint32_t sum=0;
		for(int dx=-KERNEL_RADIUS; dx<=0; ++dx)
			sum+=first[y*width+wx(0, dx)];
		for(int x=0; x<width; ++x)
		{
			second[y*width+x]=sum;
			sum-=first[y*width+wx(x, -KERNEL_RADIUS)];
			sum+=first[y*width+wx(x, 1)];
		}
	}
	for(int x=0; x<width; ++x)
	{
		uint32_t sum=0;
		for(int dy=0; dy<BOX_WIDTH; ++dy)
			sum+=second[wy(0, dy)*width+x];
		for(int y=0; y<height; ++y)
		{
			first[y*width+x]=sum;
			sum-=second[y*width+x];
			sum+=second[wy(y, BOX_WIDTH)*width+x];
		}
	}
	for(int x=0; x<width; ++x)
	{
		uint32_t sum=0;
		for(int dy=-KERNEL_RADIUS; dy<=0; ++dy)
			sum+=first[wy(0, dy)*width+x];
		for(int y=0; y<height; ++y)
		{
			fertility[y*width+x]=sum;
			sum-=first[wy(y, -KERNEL_RADIUS)*width+x];
			sum+=first[wy(y, 1)*width+x];
		}
	}
}

void ExactFertilityCache::rebuild(int newWidth, int newHeight,
	const std::vector<uint8_t>& water, const std::vector<uint8_t>& sand,
	FertilityCalculationPath requestedPath)
{
	assert(newWidth>0 && newHeight>0);
	assert(water.size()==size_t(newWidth*newHeight));
	assert(sand.size()==water.size());
	width=newWidth;
	height=newHeight;
	waterTiles=0;
	sandTiles=0;
	for(size_t i=0; i<water.size(); ++i)
	{
		waterTiles+=water[i]!=0;
		sandTiles+=sand[i]!=0;
	}
	buildWrappedIndexes();
	usedPath=requestedPath;
	if(usedPath==AdaptiveFertilityPath)
	{
		const uint32_t sandCost=uint32_t(4*water.size())+961u*uint32_t(sandTiles);
		const uint32_t waterCost=961u*uint32_t(waterTiles);
		usedPath=sandCost<=waterCost
			? SandCorrectionFertilityPath : WaterSplatFertilityPath;
	}

	if(usedPath==SandCorrectionFertilityPath)
	{
		buildWaterConvolution(water);
		for(int sy=0; sy<height; ++sy)
		{
			for(int sx=0; sx<width; ++sx)
			{
				if(!sand[sy*width+sx])
					continue;
				const int* const xWrap=&wrappedX[KERNEL_WIDTH*width+sx];
				const int* const yWrap=&wrappedY[KERNEL_WIDTH*height+sy];
				for(int dy=-KERNEL_RADIUS; dy<=KERNEL_RADIUS; ++dy)
				{
					const uint32_t yWeight=OFFSET_WEIGHT[dy+KERNEL_RADIUS];
					uint32_t* const targetRow=&fertility[
						yWrap[dy*height]*width];
					const uint8_t* const waterRow=&water[
						yWrap[2*dy*height]*width];
					for(int dx=-KERNEL_RADIUS; dx<=KERNEL_RADIUS; ++dx)
					{
						if(waterRow[xWrap[2*dx*width]])
							targetRow[xWrap[dx*width]]-=
								yWeight*OFFSET_WEIGHT[dx+KERNEL_RADIUS];
					}
				}
			}
		}
	}
	else
	{
		fertility.assign(width*height, 0);
		for(int waterY=0; waterY<height; ++waterY)
		{
			for(int waterX=0; waterX<width; ++waterX)
			{
				if(!water[waterY*width+waterX])
					continue;
				const int* const xWrap=&wrappedX[KERNEL_WIDTH*width+waterX];
				const int* const yWrap=&wrappedY[KERNEL_WIDTH*height+waterY];
				for(int dy=-KERNEL_RADIUS; dy<=KERNEL_RADIUS; ++dy)
				{
					const uint32_t yWeight=OFFSET_WEIGHT[dy+KERNEL_RADIUS];
					uint32_t* const targetRow=&fertility[
						yWrap[-dy*height]*width];
					const uint8_t* const sandRow=&sand[
						yWrap[-2*dy*height]*width];
					for(int dx=-KERNEL_RADIUS; dx<=KERNEL_RADIUS; ++dx)
					{
						if(!sandRow[xWrap[-2*dx*width]])
							targetRow[xWrap[-dx*width]]+=
								yWeight*OFFSET_WEIGHT[dx+KERNEL_RADIUS];
					}
				}
			}
		}
	}
}

uint32_t usefulExpansionCapacity(uint32_t fertility, int amount,
	int availableNeighbors, bool wheat)
{
	amount=std::max(0, std::min(8, amount));
	availableNeighbors=std::max(0, std::min(8, availableNeighbors));
	const uint32_t divisor=wheat ? 192u : 64u;
	return fertility*uint32_t(amount)*uint32_t(availableNeighbors)/divisor;
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
			const int nx=(x+dx%width+width)%width;
			const int ny=(y+dy%height+height)%height;
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
	std::vector<uint8_t> footprintMask(size,0),allowed(size,0);
	for(int index:footprint)if(index>=0&&index<size)footprintMask[index]=1;
	for(int index:circulation)if(index>=0&&index<size&&!footprintMask[index])
	{
		allowed[index]=1;
		if(!preservedResources[index])result.tiles.push_back(index);
		else ++result.preservedResourceTiles;
	}

	// Sources are actual worker-reachable cells, not arbitrary empty pockets
	// beside the building. Search only within the promised circulation land.
	typedef std::tuple<uint64_t,int,int> Entry;
	std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry> > queue;
	std::vector<uint64_t> cost(size,UINT64_MAX);
	std::vector<int> length(size,INT_MAX),parent(size,-1);
	auto resourceCost=[&](int index)->uint64_t
	{return preservedResources[index]?std::max(1,resourceBurden[index]):0;};
	for(int index:circulation)if(index>=0&&index<size&&allowed[index])
	{
		bool source=reachable[index]!=0;
		const int x=index%width,y=index/width;
		for(int dy=-1;dy<=1&&!source;++dy)for(int dx=-1;dx<=1&&!source;++dx)
		{
			const int next=((y+dy+height)%height)*width+(x+dx+width)%width;
			source=!footprintMask[next]&&reachable[next];
		}
		if(source)
		{cost[index]=resourceCost(index);length[index]=1;queue.push(Entry(cost[index],1,index));}
	}
	int entrance=-1;
	while(!queue.empty())
	{
		const Entry entry=queue.top();queue.pop();
		const int index=std::get<2>(entry),x=index%width,y=index/width;
		if(std::get<0>(entry)!=cost[index]||std::get<1>(entry)!=length[index])continue;
		const int neighbors[4]={y*width+(x+width-1)%width,y*width+(x+1)%width,
			((y+height-1)%height)*width+x,((y+1)%height)*width+x};
		for(int next:neighbors)if(footprintMask[next]){entrance=index;break;}
		if(entrance>=0)break;
		for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
		{
			const int next=((y+dy+height)%height)*width+(x+dx+width)%width;
			if(!allowed[next])continue;
			const uint64_t nextCost=cost[index]+resourceCost(next);
			const int nextLength=length[index]+1;
			if(nextCost<cost[next]||(nextCost==cost[next]&&nextLength<length[next]))
			{
				cost[next]=nextCost;length[next]=nextLength;parent[next]=index;
				queue.push(Entry(nextCost,nextLength,next));
			}
		}
	}
	if(entrance>=0)
	{
		if(cost[entrance]>0)result.fallbackEntranceTile=entrance;
		for(int index=entrance;index>=0;index=parent[index])
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
