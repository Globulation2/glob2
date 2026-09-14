#include "../src/AIMaximaFoodLedger.h"

#include <cassert>
#include <iostream>

using namespace AIMaximaFoodLedger;

static Input makeInput(int width=16,int height=16)
{
	Input input;input.width=width;input.height=height;
	input.yield.assign(size_t(width*height),0);
	input.traversable.assign(size_t(width*height),1);
	return input;
}

static ConsumerInput makeConsumer(int key,ConsumerKind kind,int x,int y,int demand)
{
	ConsumerInput consumer;consumer.key=key;consumer.kind=kind;
	consumer.centerX=x;consumer.centerY=y;consumer.demand=demand;
	consumer.left=0;consumer.top=0;consumer.width=1;consumer.height=1;
	return consumer;
}

/// A farm supplies the claimers that can actually reach it, and nothing else.
static void supplyIsClaimedNearestFirst()
{
	Input input=makeInput();
	input.yield[input.index(8,8)]=100;
	input.yield[input.index(9,8)]=100;
	input.consumers.push_back(makeConsumer(1,InnConsumer,6,8,150));
	Ledger ledger;Result result;
	ledger.evaluate(input,result);
	assert(result.totalSupply==200);
	assert(result.totalDemand==150);
	assert(result.consumers.size()==1);
	assert(result.consumers[0].claimed==150);
	assert(result.consumers[0].coveragePercent==100);
	assert(result.totalResidual==50);
	// The unclaimed remainder is still reachable, so the inn reports headroom.
	assert(result.consumers[0].available==200);
}

/// A relocated building is judged against what is left once everyone else has
/// claimed: wheat another claimant already holds does not shorten its routes,
/// and demand nobody can cover is charged as unreachable, exactly as evaluate
/// charges it.
static void residualQualityFollowsUnclaimedSupply()
{
	Input input=makeInput();
	input.policy.supplyRadius=6;input.policy.unreachablePenaltyTiles=4;
	input.yield[input.index(8,8)]=100;
	input.yield[input.index(11,8)]=100;
	Ledger ledger;Result result;
	// Nothing claimed: the adjacent cell covers the whole demand from the door,
	// which the walk counts as distance zero like evaluate does.
	ledger.evaluate(input,result);
	assert(ledger.residualQuality(input,result,7,8,0,0,1,1,100)==0);
	// A neighbour takes the near cell; the candidate must walk to the far one.
	input.consumers.push_back(makeConsumer(1,InnConsumer,9,8,100));
	ledger.evaluate(input,result);
	assert(result.consumers[0].claimed==100);
	assert(result.residual[input.index(8,8)]==0);
	assert(ledger.residualQuality(input,result,7,8,0,0,1,1,100)==300);
	// Demand beyond the residual is charged at radius plus penalty (10 tiles).
	assert(ledger.residualQuality(input,result,7,8,0,0,1,1,150)==(300*100+50*10*100)/150);
	assert(ledger.residualQuality(input,result,7,8,0,0,1,1,0)==0);
}

/// Placement quality decides who keeps the wheat. An early building in a poor
/// position must lose its claim to a later one that is genuinely better sited.
static void qualityOutranksBuildingAge()
{
	Input input=makeInput();
	input.yield[input.index(8,8)]=100;
	input.yield[input.index(9,8)]=100;
	// Key 1 is the older building, but it sits eight tiles from the farm.
	input.consumers.push_back(makeConsumer(1,InnConsumer,1,8,150));
	input.consumers.push_back(makeConsumer(50,InnConsumer,7,8,150));
	Ledger ledger;Result result;
	ledger.evaluate(input,result);
	const ConsumerResult* older=result.consumer(1);
	const ConsumerResult* nearer=result.consumer(50);
	assert(older&&nearer);
	assert(nearer->quality<older->quality);
	assert(nearer->order<older->order);
	assert(nearer->claimed==150);
	assert(older->claimed==50);
	assert(older->coveragePercent==33);
}

/// Inns and swarms alternate so neither class is starved out of the order.
static void innsAndSwarmsInterleave()
{
	Input input=makeInput();
	for(int x=6;x<12;++x)input.yield[input.index(x,8)]=100;
	input.consumers.push_back(makeConsumer(1,InnConsumer,8,7,100));
	input.consumers.push_back(makeConsumer(2,InnConsumer,9,7,100));
	input.consumers.push_back(makeConsumer(3,SwarmConsumer,8,9,100));
	input.consumers.push_back(makeConsumer(4,SwarmConsumer,9,9,100));
	Ledger ledger;Result result;
	ledger.evaluate(input,result);
	ConsumerKind byOrder[4]={InnConsumer,InnConsumer,InnConsumer,InnConsumer};
	for(size_t i=0;i<result.consumers.size();++i)
		byOrder[result.consumers[i].order]=result.consumers[i].kind;
	assert(byOrder[0]==InnConsumer);
	assert(byOrder[1]==SwarmConsumer);
	assert(byOrder[2]==InnConsumer);
	assert(byOrder[3]==SwarmConsumer);
}

/// Claims are never stored: removing a consumer and rebuilding releases exactly
/// its wheat, which is how destruction and upgrades are handled.
static void removingAConsumerReleasesItsClaim()
{
	Input input=makeInput();
	input.yield[input.index(8,8)]=100;
	input.yield[input.index(9,8)]=100;
	input.consumers.push_back(makeConsumer(1,InnConsumer,7,8,120));
	input.consumers.push_back(makeConsumer(2,SwarmConsumer,10,8,60));
	Ledger ledger;Result both;
	ledger.evaluate(input,both);
	assert(both.totalClaimed==180);
	assert(both.totalResidual==20);
	input.consumers.pop_back();
	Result single;
	ledger.evaluate(input,single);
	assert(single.totalClaimed==120);
	assert(single.totalResidual==80);
}

/// An upgrade is judged on what it could reach, not only on what it holds.
static void upgradeDemandUsesAvailableSupply()
{
	Input input=makeInput();
	input.yield[input.index(8,8)]=100;
	input.yield[input.index(9,8)]=100;
	input.consumers.push_back(makeConsumer(1,InnConsumer,7,8,80));
	Ledger ledger;Result result;
	ledger.evaluate(input,result);
	assert(result.consumer(1)->available==200);
	// Re-running with the upgraded demand is the whole upgrade check.
	input.consumers[0].demand=200;
	Result upgraded;
	ledger.evaluate(input,upgraded);
	assert(upgraded.consumer(1)->claimed==200);
	assert(upgraded.consumer(1)->coveragePercent==100);
}

/// The summed-area prefilter may overstate reachable supply, never understate
/// it; otherwise it would reject candidates the exact walk would accept.
static void upperBoundNeverUnderstatesReach()
{
	Input input=makeInput();
	for(int x=4;x<12;++x)for(int y=4;y<12;++y)input.yield[input.index(x,y)]=50;
	input.consumers.push_back(makeConsumer(1,InnConsumer,8,8,100));
	Ledger ledger;Result result;
	ledger.evaluate(input,result);
	for(int x=0;x<input.width;x+=3)
		for(int y=0;y<input.height;y+=3)
		{
			const long long exact=ledger.reachableResidual(input,result,x,y,
				0,0,1,1,0);
			const long long bound=ledger.residualUpperBound(input,result,x,y,
				0,0,1,1);
			assert(bound>=exact);
		}
	assert(result.bestSiteResidual>=0);
	assert(result.bestSiteResidual<=result.totalResidual);
}

/// Water, buildings and blocked ground genuinely separate farms from claimers.
static void untraversableGroundBlocksSupply()
{
	Input input=makeInput();
	input.yield[input.index(8,8)]=200;
	// The map wraps, so isolating the claimer takes a wall on both sides of it.
	for(int y=0;y<input.height;++y)
	{
		input.traversable[input.index(6,y)]=0;
		input.traversable[input.index(15,y)]=0;
	}
	input.consumers.push_back(makeConsumer(1,InnConsumer,2,8,150));
	Ledger ledger;Result result;
	ledger.evaluate(input,result);
	assert(result.consumer(1)->claimed==0);
	assert(result.consumer(1)->coveragePercent==0);
	assert(result.totalResidual==200);
}

/// Only supply within the harvesting radius counts.
static void supplyBeyondTheRadiusIsNotCounted()
{
	Input input=makeInput(64,16);
	input.yield[input.index(40,8)]=200;
	input.consumers.push_back(makeConsumer(1,InnConsumer,2,8,150));
	Ledger ledger;Result result;
	ledger.evaluate(input,result);
	assert(result.consumer(1)->claimed==0);
	assert(result.consumer(1)->available==0);
}

/// Engine-derived rates: a swarm at full output eats one wheat every thirty
/// ticks, and a fully fertile cell produces on its documented period.
static void demandAndYieldMatchEngineRates()
{
	assert(swarmDemand(5,150,100)==33333);
	assert(swarmDemand(5,150,50)==16666);
	assert(innDemand(19,1000,100)==19000);
	assert(cellYield(65536,8,186)==5376);
	assert(cellYield(65536,4,186)==2688);
	assert(cellYield(0,8,186)==0);
	assert(cellYield(65536,0,186)==0);
}

int main()
{
	supplyIsClaimedNearestFirst();
	qualityOutranksBuildingAge();
	innsAndSwarmsInterleave();
	removingAConsumerReleasesItsClaim();
	upgradeDemandUsesAvailableSupply();
	upperBoundNeverUnderstatesReach();
	untraversableGroundBlocksSupply();
	supplyBeyondTheRadiusIsNotCounted();
	demandAndYieldMatchEngineRates();
	residualQualityFollowsUnclaimedSupply();
	std::cout<<"MaximaFoodLedgerStandaloneTest: PASS"<<std::endl;
	return 0;
}
