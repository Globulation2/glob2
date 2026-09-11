/*
  Protected-wheat food ledger. See AIMaximaFoodLedger.h for the model.
 */

#include "AIMaximaFoodLedger.h"

#include <algorithm>

namespace AIMaximaFoodLedger
{

namespace
{
	/// The engine's fertility denominator: growth succeeds with probability
	/// fertility/65536 whenever a cell is sampled.
	const int fertilityScale=65536;
	/// Demand and supply are carried as wheat per tick scaled by one million,
	/// which keeps a single low-fertility cell from rounding away to nothing.
	const long long rateScale=1000000;
	/// A cell's growth picks one of its eight neighbours, so a cell whose
	/// neighbours are blocked wastes that share of its growth.
	const int growthDirections=8;
	const int percentScale=100;
	/// Coverage percentages are reported, not used for arithmetic, so a bound
	/// keeps a tiny demand from producing an unbounded ratio.
	const int maximumReportedPercent=1000;
	/// Quality is a mean distance in hundredths of a tile.
	const int qualityScale=100;

	bool betterClaimant(const ConsumerResult& a, const ConsumerResult& b,
		const ConsumerInput& ai, const ConsumerInput& bi)
	{
		// Placement quality first: an early building in a poor position must not
		// keep its wheat merely because it was built first. Level, lifecycle and
		// identity only break ties inside one quality band.
		if(a.qualityBand!=b.qualityBand)return a.qualityBand<b.qualityBand;
		if(ai.level!=bi.level)return ai.level>bi.level;
		if(ai.stage!=bi.stage)return ai.stage<bi.stage;
		const int leftKey=ai.key<0?-ai.key:ai.key;
		const int rightKey=bi.key<0?-bi.key:bi.key;
		return leftKey<rightKey;
	}
}

ConsumerInput::ConsumerInput()
	: key(0), kind(InnConsumer), stage(CompletedStage), demand(0), level(1),
	  centerX(0), centerY(0), left(0), top(0), width(1), height(1),
	  colony(false), retirable(true)
{
}

Policy::Policy()
	: supplyRadius(12), qualityBandTiles(2), unreachablePenaltyTiles(8)
{
}

ConsumerResult::ConsumerResult()
	: key(0), kind(InnConsumer), colony(false), retirable(true), demand(0),
	  claimed(0), available(0), coveragePercent(0), availablePercent(0),
	  quality(0), qualityBand(0), order(0)
{
}

Input::Input(): width(0), height(0)
{
}

int Input::normalizeX(int x) const
{
	if(width<=0)return 0;
	const int value=x%width;
	return value<0?value+width:value;
}

int Input::normalizeY(int y) const
{
	if(height<=0)return 0;
	const int value=y%height;
	return value<0?value+height:value;
}

int Input::index(int x, int y) const
{
	return normalizeY(y)*width+normalizeX(x);
}

Result::Result()
	: totalSupply(0), totalDemand(0), totalClaimed(0), totalResidual(0),
	  bestSiteResidual(0)
{
}

const ConsumerResult* Result::consumer(int key) const
{
	for(size_t i=0;i<consumers.size();++i)
		if(consumers[i].key==key)return &consumers[i];
	return NULL;
}

uint32_t cellYield(uint32_t fertility, int openNeighbors, int growthPeriodTicks)
{
	if(openNeighbors<=0||growthPeriodTicks<=0)return 0;
	const long long open=std::min(openNeighbors,growthDirections);
	const long long denominator=static_cast<long long>(fertilityScale)
		*growthPeriodTicks*growthDirections;
	return uint32_t(static_cast<long long>(fertility)*open*rateScale/denominator);
}

int swarmDemand(int resourceForOneUnit, int unitProductionTime, int percent)
{
	if(resourceForOneUnit<=0||unitProductionTime<=0||percent<=0)return 0;
	return int(static_cast<long long>(resourceForOneUnit)*rateScale*percent
		/(static_cast<long long>(unitProductionTime)*percentScale));
}

int innDemand(int servedUnits, int ticksPerMeal, int percent)
{
	if(servedUnits<=0||ticksPerMeal<=0||percent<=0)return 0;
	return int(static_cast<long long>(servedUnits)*rateScale*percent
		/(static_cast<long long>(ticksPerMeal)*percentScale));
}

Ledger::Ledger()
	: generation(0), residualSumsWidth(0), residualSumsHeight(0),
	  residualSumsSource(NULL), residualSumsTotal(-1)
{
}

void Ledger::walk(const Input& input, int centerX, int centerY, int left,
	int top, int width, int height, std::vector<ReachCell>& reach) const
{
	reach.clear();
	const int size=input.width*input.height;
	if(size<=0||int(input.traversable.size())!=size
	   ||int(input.yield.size())!=size)return;
	if(int(distanceScratch.size())!=size)
	{
		distanceScratch.assign(size,0);
		distanceGeneration.assign(size,0);
		generation=0;
	}
	if(++generation==0)
	{
		std::fill(distanceGeneration.begin(),distanceGeneration.end(),0);
		generation=1;
	}
	// The hypothetical building occupies its own footprint, so harvesting routes
	// must leave from the ring around it rather than crossing it.
	const int originX=centerX+left;
	const int originY=centerY+top;
	std::vector<ReachCell>& queue=queryScratch;
	queue.clear();
	const auto visit=[&](int x,int y,int distance)
	{
		const int index=input.index(x,y);
		if(distanceGeneration[index]==generation)return;
		if(!input.traversable[index])return;
		const int localX=input.normalizeX(x-originX);
		const int localY=input.normalizeY(y-originY);
		if(localX<width&&localY<height)return;
		distanceGeneration[index]=generation;
		distanceScratch[index]=distance;
		ReachCell cell;cell.index=index;cell.distance=distance;
		queue.push_back(cell);
	};
	for(int dy=-1;dy<=height;++dy)
		for(int dx=-1;dx<=width;++dx)
			if(dx==-1||dy==-1||dx==width||dy==height)
				visit(originX+dx,originY+dy,0);
	// Breadth-first expansion visits cells in nondecreasing distance, so the
	// claim pass below consumes the nearest wheat first without sorting.
	for(size_t head=0;head<queue.size();++head)
	{
		const ReachCell current=queue[head];
		if(input.yield[current.index]>0)reach.push_back(current);
		if(current.distance>=input.policy.supplyRadius)continue;
		const int x=current.index%input.width;
		const int y=current.index/input.width;
		for(int dy=-1;dy<=1;++dy)
			for(int dx=-1;dx<=1;++dx)
				if(dx||dy)visit(x+dx,y+dy,current.distance+1);
	}
}

void Ledger::evaluate(const Input& input, Result& result) const
{
	result=Result();
	const int size=input.width*input.height;
	if(size<=0||int(input.yield.size())!=size
	   ||int(input.traversable.size())!=size)return;
	result.residual=input.yield;
	for(int i=0;i<size;++i)
		result.totalSupply+=input.yield[i];

	// Quality is measured alone, before any claim exists. Making it independent
	// of the claim order keeps the order from feeding back into itself.
	const size_t count=input.consumers.size();
	std::vector<std::vector<ReachCell> > reachByConsumer(count);
	std::vector<ConsumerResult> values(count);
	for(size_t i=0;i<count;++i)
	{
		const ConsumerInput& consumer=input.consumers[i];
		walk(input,consumer.centerX,consumer.centerY,consumer.left,consumer.top,
			consumer.width,consumer.height,reachScratch);
		reachByConsumer[i]=reachScratch;
		ConsumerResult& value=values[i];
		value.key=consumer.key;value.kind=consumer.kind;
		value.colony=consumer.colony;value.retirable=consumer.retirable;
		value.demand=std::max(0,consumer.demand);
		result.totalDemand+=value.demand;
		long long remaining=value.demand;
		long long weighted=0,filled=0;
		for(size_t r=0;r<reachByConsumer[i].size()&&remaining>0;++r)
		{
			const ReachCell& cell=reachByConsumer[i][r];
			const long long take=std::min<long long>(remaining,input.yield[cell.index]);
			weighted+=take*cell.distance;filled+=take;remaining-=take;
		}
		// Demand a site cannot reach at all is charged beyond the supply radius,
		// so an unreachable position ranks behind every reachable one.
		const long long penaltyDistance=input.policy.supplyRadius
			+input.policy.unreachablePenaltyTiles;
		weighted+=remaining*penaltyDistance;filled+=remaining;
		value.quality=filled>0?int(weighted*qualityScale/filled):0;
		const int band=std::max(1,input.policy.qualityBandTiles)*qualityScale;
		value.qualityBand=value.quality/band;
	}

	std::vector<size_t> inns,swarms;
	for(size_t i=0;i<count;++i)
		(input.consumers[i].kind==InnConsumer?inns:swarms).push_back(i);
	const auto rank=[&](const std::vector<size_t>& group)
	{
		std::vector<size_t> sorted=group;
		std::sort(sorted.begin(),sorted.end(),[&](size_t a,size_t b)
		{
			return betterClaimant(values[a],values[b],input.consumers[a],
				input.consumers[b]);
		});
		return sorted;
	};
	const std::vector<size_t> rankedInns=rank(inns);
	const std::vector<size_t> rankedSwarms=rank(swarms);
	// Inns and swarms alternate: a colony needs food for the citizens it has
	// and births for the citizens it wants, and neither class may starve the
	// other out of the ordering.
	std::vector<size_t> order;order.reserve(count);
	for(size_t i=0;i<std::max(rankedInns.size(),rankedSwarms.size());++i)
	{
		if(i<rankedInns.size())order.push_back(rankedInns[i]);
		if(i<rankedSwarms.size())order.push_back(rankedSwarms[i]);
	}

	for(size_t position=0;position<order.size();++position)
	{
		const size_t i=order[position];
		ConsumerResult& value=values[i];
		value.order=int(position);
		long long remaining=value.demand;
		for(size_t r=0;r<reachByConsumer[i].size()&&remaining>0;++r)
		{
			const int index=reachByConsumer[i][r].index;
			const long long take=std::min<long long>(remaining,result.residual[index]);
			if(take<=0)continue;
			result.residual[index]=uint32_t(result.residual[index]-take);
			value.claimed+=int(take);remaining-=take;
		}
		result.totalClaimed+=value.claimed;
	}

	for(size_t i=0;i<count;++i)
	{
		ConsumerResult& value=values[i];
		long long spare=0;
		for(size_t r=0;r<reachByConsumer[i].size();++r)
			spare+=result.residual[reachByConsumer[i][r].index];
		value.available=value.claimed+spare;
		if(value.demand>0)
		{
			value.coveragePercent=std::min<long long>(maximumReportedPercent,
				static_cast<long long>(value.claimed)*percentScale/value.demand);
			value.availablePercent=int(std::min<long long>(maximumReportedPercent,
				value.available*percentScale/value.demand));
		}
		else
		{
			value.coveragePercent=maximumReportedPercent;
			value.availablePercent=maximumReportedPercent;
		}
	}
	result.consumers.swap(values);
	for(int i=0;i<size;++i)
		result.totalResidual+=result.residual[i];

	// The largest unclaimed supply any single site could reach bounds how many
	// more food buildings the map can support. A plain total would add up
	// scattered remnants that no one building can ever collect.
	prepareResidualSums(input,result);
	for(int y=0;y<input.height;++y)
		for(int x=0;x<input.width;++x)
			result.bestSiteResidual=std::max(result.bestSiteResidual,
				residualUpperBound(input,result,x,y,0,0,1,1));
}

void Ledger::prepareResidualSums(const Input& input, const Result& result) const
{
	const int size=input.width*input.height;
	long long total=0;
	for(int i=0;i<size;++i)total+=result.residual[i];
	if(residualSumsSource==&result&&residualSumsWidth==input.width
	   &&residualSumsHeight==input.height&&residualSumsTotal==total)return;
	residualSumsSource=&result;residualSumsWidth=input.width;
	residualSumsHeight=input.height;residualSumsTotal=total;
	const int stride=input.width+1;
	residualSums.assign(size_t(input.height+1)*stride,0);
	for(int y=0;y<input.height;++y)
	{
		long long row=0;
		for(int x=0;x<input.width;++x)
		{
			row+=result.residual[y*input.width+x];
			residualSums[size_t(y+1)*stride+x+1]=
				residualSums[size_t(y)*stride+x+1]+row;
		}
	}
}

long long Ledger::residualUpperBound(const Input& input, const Result& result,
	int centerX, int centerY, int left, int top, int width, int height) const
{
	if(input.width<=0||input.height<=0)return 0;
	prepareResidualSums(input,result);
	const int stride=input.width+1;
	const auto rect=[&](int x0,int y0,int w,int h)->long long
	{
		if(w<=0||h<=0)return 0;
		return residualSums[size_t(y0+h)*stride+x0+w]
			-residualSums[size_t(y0)*stride+x0+w]
			-residualSums[size_t(y0+h)*stride+x0]
			+residualSums[size_t(y0)*stride+x0];
	};
	// Every cell a harvesting route can touch lies inside this square, so the
	// sum over it can only overstate what the exact walk would find.
	const int reach=input.policy.supplyRadius+1;
	int spanX=width+2*reach;
	int spanY=height+2*reach;
	if(spanX>input.width)spanX=input.width;
	if(spanY>input.height)spanY=input.height;
	const int startX=input.normalizeX(centerX+left-reach);
	const int startY=input.normalizeY(centerY+top-reach);
	long long total=0;
	// A window crossing the map seam is summed as its wrapped pieces.
	const int firstWidth=std::min(spanX,input.width-startX);
	const int firstHeight=std::min(spanY,input.height-startY);
	total+=rect(startX,startY,firstWidth,firstHeight);
	total+=rect(0,startY,spanX-firstWidth,firstHeight);
	total+=rect(startX,0,firstWidth,spanY-firstHeight);
	total+=rect(0,0,spanX-firstWidth,spanY-firstHeight);
	return total;
}

long long Ledger::reachableResidual(const Input& input, const Result& result,
	int centerX, int centerY, int left, int top, int width, int height,
	long long cap) const
{
	walk(input,centerX,centerY,left,top,width,height,reachScratch);
	long long total=0;
	for(size_t i=0;i<reachScratch.size();++i)
	{
		total+=result.residual[reachScratch[i].index];
		if(cap>0&&total>=cap)break;
	}
	return total;
}

}
