#include "Glob2Test.h"
#include "AIMaximaFoodLedger.h"
#include "AIMaximaOperatingDemand.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <ctime>
#include <iostream>
#include <utility>

using namespace AIMaximaFoodLedger;

static Input makeInput(int width=16,int height=16)
{
	Input input;input.setDimensions(width,height);
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
	REQUIRE(result.totalSupply==200);
	REQUIRE(result.totalDemand==150);
	REQUIRE(result.consumers.size()==1);
	REQUIRE(result.consumers[0].claimed==150);
	REQUIRE(result.consumers[0].coveragePercent==100);
	REQUIRE(result.totalResidual==50);
	// The unclaimed remainder is still reachable, so the inn reports headroom.
	REQUIRE(result.consumers[0].available==200);
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
	REQUIRE(ledger.residualQuality(input,result,7,8,0,0,1,1,100)==0);
	// A neighbour takes the near cell; the candidate must walk to the far one.
	input.consumers.push_back(makeConsumer(1,InnConsumer,9,8,100));
	ledger.evaluate(input,result);
	REQUIRE(result.consumers[0].claimed==100);
	REQUIRE(result.residual[input.index(8,8)]==0);
	REQUIRE(ledger.residualQuality(input,result,7,8,0,0,1,1,100)==300);
	// Demand beyond the residual is charged at radius plus penalty (10 tiles).
	REQUIRE(ledger.residualQuality(input,result,7,8,0,0,1,1,150)==(300*100+50*10*100)/150);
	REQUIRE(ledger.residualQuality(input,result,7,8,0,0,1,1,0)==0);
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
	REQUIRE((older&&nearer));
	REQUIRE(nearer->quality<older->quality);
	REQUIRE(nearer->order<older->order);
	REQUIRE(nearer->claimed==150);
	REQUIRE(older->claimed==50);
	REQUIRE(older->coveragePercent==33);
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
	REQUIRE(byOrder[0]==InnConsumer);
	REQUIRE(byOrder[1]==SwarmConsumer);
	REQUIRE(byOrder[2]==InnConsumer);
	REQUIRE(byOrder[3]==SwarmConsumer);
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
	REQUIRE(both.totalClaimed==180);
	REQUIRE(both.totalResidual==20);
	input.consumers.pop_back();
	Result single;
	ledger.evaluate(input,single);
	REQUIRE(single.totalClaimed==120);
	REQUIRE(single.totalResidual==80);
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
	REQUIRE(result.consumer(1)->available==200);
	// Re-running with the upgraded demand is the whole upgrade check.
	input.consumers[0].demand=200;
	Result upgraded;
	ledger.evaluate(input,upgraded);
	REQUIRE(upgraded.consumer(1)->claimed==200);
	REQUIRE(upgraded.consumer(1)->coveragePercent==100);
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
			REQUIRE(bound>=exact);
		}
	REQUIRE(result.bestSiteResidual>=0);
	REQUIRE(result.bestSiteResidual<=result.totalResidual);
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
	REQUIRE(result.consumer(1)->claimed==0);
	REQUIRE(result.consumer(1)->coveragePercent==0);
	REQUIRE(result.totalResidual==200);
}

/// Only supply within the harvesting radius counts.
static void supplyBeyondTheRadiusIsNotCounted()
{
	Input input=makeInput(64,16);
	input.yield[input.index(40,8)]=200;
	input.consumers.push_back(makeConsumer(1,InnConsumer,2,8,150));
	Ledger ledger;Result result;
	ledger.evaluate(input,result);
	REQUIRE(result.consumer(1)->claimed==0);
	REQUIRE(result.consumer(1)->available==0);
}

/// Engine-derived rates: a swarm at full output eats one wheat every thirty
/// ticks, and a fully fertile cell produces on its documented period.
static void demandAndYieldMatchEngineRates()
{
	REQUIRE(swarmDemand(5,150,100)==33333);
	REQUIRE(swarmDemand(5,150,50)==16666);
	REQUIRE(innDemand(19,1000,100)==19000);
	REQUIRE(cellYield(65536,8,186)==5376);
	REQUIRE(cellYield(65536,4,186)==2688);
	REQUIRE(cellYield(0,8,186)==0);
	REQUIRE(cellYield(65536,0,186)==0);
}

// Independent cell-by-cell oracle for the wrapped square prefilter, including
// rectangular maps and windows that cover an entire map dimension.
static long long bruteBound(const Input& input,const Result& result,
	int x,int y,int left,int top,int width,int height)
{
	const int reach=input.policy.supplyRadius+1;
	long long total=0;
	for(int cy=0;cy<input.height;++cy)
		for(int cx=0;cx<input.width;++cx)
			if(input.normalizeX(cx-(x+left-reach))<std::min(input.width,width+2*reach)
			   &&input.normalizeY(cy-(y+top-reach))<std::min(input.height,height+2*reach))
				total+=result.residual[input.index(cx,cy)];
	return total;
}

static void preparedBoundsMatchWrappedOracle()
{
	for(int radius : {0,2,12,40})
	{
		Input input=makeInput(32,16);input.policy.supplyRadius=radius;
		for(size_t i=0;i<input.yield.size();++i)input.yield[i]=(i*37)%101;
		input.consumers.push_back(makeConsumer(1,InnConsumer,0,15,300));
		Ledger ledger;Result result;ledger.evaluate(input,result);
		long long best=0;
		for(int y=0;y<input.height;++y)for(int x=0;x<input.width;++x)
		{
			best=std::max(best,bruteBound(input,result,x,y,0,0,1,1));
			REQUIRE(ledger.residualUpperBound(input,result,x,y,-2,-1,4,3)
				==bruteBound(input,result,x,y,-2,-1,4,3));
		}
		REQUIRE(result.bestSiteResidual==best);
	}
}

static void equalTotalChangesRefreshTheSnapshot()
{
	Input input=makeInput(32,16);input.policy.supplyRadius=1;
	input.yield[input.index(3,3)]=100;
	Ledger ledger;Result result;ledger.evaluate(input,result);
	REQUIRE(ledger.residualUpperBound(input,result,3,3,0,0,1,1)==100);
	Result original=result;
	// Reuse the same result address and total while moving all the supply.
	input.yield[input.index(3,3)]=0;input.yield[input.index(20,10)]=100;
	ledger.evaluate(input,result);
	REQUIRE(result.totalResidual==original.totalResidual);
	REQUIRE(ledger.residualUpperBound(input,result,3,3,0,0,1,1)==0);
	REQUIRE(ledger.residualUpperBound(input,result,20,10,0,0,1,1)==100);
	// Copies retain their own prepared tables, even after another evaluation
	// and when queried through a different ledger instance.
	Ledger other;
	REQUIRE(other.residualUpperBound(input,original,3,3,0,0,1,1)==100);
	REQUIRE(ledger.residualUpperBound(input,result,3,3,0,0,1,1)==0);
	result=original;
	REQUIRE(ledger.residualUpperBound(input,result,3,3,0,0,1,1)==100);
	Input resized=makeInput(16,32);resized.policy.supplyRadius=1;
	resized.yield[resized.index(0,0)]=50;
	ledger.evaluate(resized,result);
	REQUIRE(ledger.residualUpperBound(resized,result,0,0,0,0,1,1)==50);
	const size_t capacity=result.residual.capacity();
	ledger.evaluate(Input(),result);
	REQUIRE((result.residual.empty()&&result.consumers.empty()));
	REQUIRE((result.totalSupply==0&&result.totalResidual==0&&result.bestSiteResidual==0));
	REQUIRE(result.residual.capacity()==capacity);
	REQUIRE(ledger.residualUpperBound(resized,result,0,0,0,0,1,1)==0);
	REQUIRE(other.residualUpperBound(input,original,3,3,0,0,1,1)==100);
}

// Capped queries must retain the whole threshold-crossing cell and the same
// nearest-first order, including across the toroidal seam. A short query must
// not leave scratch state that truncates a later larger or uncapped query.
static void cappedQueriesPreserveOrderAndScratchReuse()
{
	Input input=makeInput(8,8);
	input.policy.supplyRadius=3;
	input.yield[input.index(7,7)]=70;
	input.yield[input.index(0,7)]=40;
	input.yield[input.index(1,7)]=30;
	input.yield[input.index(3,0)]=100;
	Ledger ledger;Result result;
	ledger.evaluate(input,result);
	for(int repeat=0;repeat<3;++repeat)
	{
		REQUIRE(ledger.reachableResidual(input,result,0,0,0,0,1,1,1)==70);
		REQUIRE(ledger.reachableResidual(input,result,0,0,0,0,1,1,70)==70);
		REQUIRE(ledger.reachableResidual(input,result,0,0,0,0,1,1,71)==110);
		REQUIRE(ledger.reachableResidual(input,result,0,0,0,0,1,1,111)==140);
		REQUIRE(ledger.residualQuality(input,result,0,0,0,0,1,1,100)==0);
		REQUIRE(ledger.residualQuality(input,result,0,0,0,0,1,1,240)==83);
		REQUIRE(ledger.residualQuality(input,result,0,0,0,0,1,1,300)==286);
		REQUIRE(ledger.reachableResidual(input,result,0,0,0,0,1,1,1000)==240);
		REQUIRE(ledger.reachableResidual(input,result,0,0,0,0,1,1,0)==240);
		REQUIRE(ledger.reachableResidual(input,result,0,0,0,0,1,1,-1)==240);
	}
	input.traversable[input.index(7,7)]=0;
	ledger.evaluate(input,result);
	REQUIRE(ledger.reachableResidual(input,result,0,0,0,0,1,1,1)==40);
	REQUIRE(ledger.reachableResidual(input,result,0,0,0,0,1,1,0)==170);
}

// Opt-in measurement, never a timing assertion in CI. Compare the same driver
// against before/after sources; the digest makes output changes visible.
static void benchmark()
{
	std::cout<<"side,evaluate_cpu_ms,queries_cpu_ms,digest\n";
	for(int side : {128,256,512})
	{
		Input input=makeInput(side,side);
		for(size_t i=0;i<input.yield.size();++i)input.yield[i]=(i*37)%101;
		Ledger ledger;Result result;
		const std::clock_t start=std::clock();
		ledger.evaluate(input,result);
		const std::clock_t evaluated=std::clock();
		uint64_t digest=uint64_t(result.bestSiteResidual);
		for(int y=0;y<side;++y)for(int x=0;x<side;++x)
			digest=digest*1099511628211ULL+uint64_t(
				ledger.residualUpperBound(input,result,x,y,-2,-1,4,3));
		const std::clock_t queried=std::clock();
		std::cout<<side<<','<<1000.0*(evaluated-start)/CLOCKS_PER_SEC<<','
			<<1000.0*(queried-evaluated)/CLOCKS_PER_SEC<<','<<digest<<std::endl;
	}
}

TEST_SUITE("Maxima.FoodLedger")
{
	TEST_CASE("capped queries preserve order and scratch reuse") { cappedQueriesPreserveOrderAndScratchReuse(); }
	TEST_CASE("supply is claimed nearest first") { supplyIsClaimedNearestFirst(); }
	TEST_CASE("quality outranks building age") { qualityOutranksBuildingAge(); }
	TEST_CASE("inns and swarms interleave") { innsAndSwarmsInterleave(); }
	TEST_CASE("removing a consumer releases its claim") { removingAConsumerReleasesItsClaim(); }
	TEST_CASE("upgrade demand uses available supply") { upgradeDemandUsesAvailableSupply(); }
	TEST_CASE("upper bound never understates reach") { upperBoundNeverUnderstatesReach(); }
	TEST_CASE("untraversable ground blocks supply") { untraversableGroundBlocksSupply(); }
	TEST_CASE("supply beyond the radius is not counted") { supplyBeyondTheRadiusIsNotCounted(); }
	TEST_CASE("demand and yield match engine rates") { demandAndYieldMatchEngineRates(); }
	TEST_CASE("residual quality follows unclaimed supply") { residualQualityFollowsUnclaimedSupply(); }
	TEST_CASE("prepared bounds match wrapped oracle") { preparedBoundsMatchWrappedOracle(); }
	TEST_CASE("equal total changes refresh the snapshot") { equalTotalChangesRefreshTheSnapshot(); }
	TEST_CASE("timing benchmark [benchmark][slow]") { benchmark(); }
}

TEST_CASE("operating claims price the shared reachable supply curve without contested feedback" * doctest::test_suite("Maxima.FoodLedger"))
{
    Input input=makeInput(64,64);input.policy.supplyRadius=12;input.policy.unreachablePenaltyTiles=8;
    input.yield[input.index(9,8)]=1000;input.yield[input.index(18,8)]=40000;
    auto consumer=makeConsumer(1,SwarmConsumer,8,8,11000);
    consumer.operating.carriers=2;consumer.operating.fixedTicks=100;consumer.operating.ticksPerTile=50;
    consumer.operating.independent[1]=1000;consumer.operating.production[1]=10000;
    consumer.operating.trips.fill(300);
    Ledger ledger;Result result;input.consumers.push_back(consumer);ledger.evaluate(input,result);
    REQUIRE(result.consumer(1));
    CHECK(result.consumer(1)->demand==2900);CHECK(result.consumer(1)->productionDemand==1900);
    CHECK(result.consumer(1)->coveragePercent==100);
    input.consumers.clear();ledger.evaluate(input,result);
    const auto query=ledger.operatingQuery(input,result,consumer,120);
    CHECK(query.demand==2900);CHECK(query.productionDemand==1900);
    // Another claimant changes availability, never the uncontested work curve.
    input.consumers.push_back(makeConsumer(2,InnConsumer,8,8,41000));ledger.evaluate(input,result);
    const auto contested=ledger.operatingQuery(input,result,consumer,120);
    CHECK(contested.demand==query.demand);CHECK(contested.residual==0);
    input.consumers.clear();consumer.operating.independent[0]=4000;
    input.consumers.push_back(consumer);ledger.evaluate(input,result);
    CHECK(result.consumer(1)->productionDemand==700);CHECK(result.consumer(1)->demand==1700);
}

TEST_CASE("operating supply tails retain positive demand and zero coverage for unreachable producers" * doctest::test_suite("Maxima.FoodLedger"))
{
    Input input=makeInput();input.policy.supplyRadius=12;input.policy.unreachablePenaltyTiles=8;
    auto consumer=makeConsumer(1,SwarmConsumer,8,8,10000);
    consumer.operating.carriers=2;consumer.operating.fixedTicks=100;consumer.operating.ticksPerTile=50;
    consumer.operating.production[1]=10000;consumer.operating.trips.fill(300);
    input.consumers.push_back(consumer);Ledger ledger;Result result;ledger.evaluate(input,result);
    REQUIRE(result.consumer(1));CHECK(result.consumer(1)->demand==952);
    CHECK(result.consumer(1)->claimed==0);CHECK(result.consumer(1)->coveragePercent==0);
    input.consumers[0].operating.independent[1]=1000;ledger.evaluate(input,result);
    CHECK(result.consumer(1)->demand==1000);CHECK(result.consumer(1)->productionDemand==0);
    CHECK(result.consumer(1)->coveragePercent==0);
    auto& plan=input.consumers[0].operating;plan.carriers=1024;
    plan.independent[1]=plan.production[1]=INT_MAX;
    plan.fixedTicks=INT_MAX;plan.ticksPerTile=INT_MAX;ledger.evaluate(input,result);
    CHECK(result.consumer(1)->demand==INT_MAX);CHECK(result.consumer(1)->productionDemand==0);
}

TEST_CASE("operating fractions preserve positive rates beneath a saturated recipe ceiling" * doctest::test_suite("Maxima.FoodLedger"))
{
    std::array<int,MaterialCount> independent{},production{},trips{};
    production[1]=INT_MAX;trips.fill(1000);
    const auto result=AIMaxima::operatingClaimWithWheatWork(independent,production,1,trips,
        [](long long q){return q*1000;});
    CHECK(result.production[1]==1000);CHECK(result.total[1]==1000);
}

TEST_CASE("equivalent feeders retain funded meal reservations before discretionary production" * doctest::test_suite("Maxima.FoodLedger"))
{
    Input input=makeInput();input.yield[input.index(8,8)]=10000;
    auto feeder=makeConsumer(1,InnConsumer,7,8,8000);
    auto producer=makeConsumer(2,SwarmConsumer,7,8,8000);producer.productionDemand=8000;
    input.consumers={feeder,producer};Ledger ledger;Result baseline,redistributed,installed;
    ledger.evaluate(input,baseline);
    CHECK(baseline.consumer(1)->claimed==8000);CHECK(baseline.consumer(2)->claimedProduction==2000);
    input.consumers[0].demand=4000;ledger.evaluate(input,redistributed);
    auto candidate=makeConsumer(3,InnConsumer,7,8,4000);candidate.operating.independent[1]=4000;
    const auto query=ledger.operatingQuery(input,redistributed,candidate,150,&baseline,4000);
    CHECK(query.feasible);CHECK(query.residual==4000);CHECK(query.transferred==4000);
    input.consumers.push_back(candidate);ledger.evaluate(input,installed);
    CHECK(installed.consumer(1)->claimed==4000);CHECK(installed.consumer(3)->claimed==4000);
    CHECK(installed.consumer(2)->claimedProduction==2000);CHECK(installed.independentClaimed==8000);
    // The margin is charged only on new demand, even with no spare capacity.
    input.yield[input.index(8,8)]=8000;input.consumers.pop_back();input.consumers[0].demand=8000;
    ledger.evaluate(input,baseline);input.consumers[0].demand=4000;ledger.evaluate(input,redistributed);
    CHECK(ledger.operatingQuery(input,redistributed,candidate,150,&baseline,4000).feasible);
    CHECK_FALSE(ledger.operatingQuery(input,redistributed,candidate,150).feasible);
}

TEST_CASE("feeding transfer credits are funded local and limited to the meal component" * doctest::test_suite("Maxima.FoodLedger"))
{
    Input input=makeInput(64,64);input.policy.supplyRadius=4;
    input.yield[input.index(8,8)]=8000;input.yield[input.index(40,8)]=4000;
    input.consumers.push_back(makeConsumer(1,InnConsumer,7,8,8000));
    Ledger ledger;Result baseline,redistributed;ledger.evaluate(input,baseline);
    input.consumers[0].demand=4000;ledger.evaluate(input,redistributed);
    auto candidate=makeConsumer(2,InnConsumer,39,8,4000);candidate.operating.independent[1]=4000;
    auto query=ledger.operatingQuery(input,redistributed,candidate,150,&baseline,4000);
    CHECK(query.residual==4000);CHECK(query.transferred==0);CHECK_FALSE(query.feasible);
    // A shortage is not an already funded reservation that can be transferred.
    input.yield[input.index(8,8)]=4000;input.consumers[0].demand=8000;ledger.evaluate(input,baseline);
    input.consumers[0].demand=4000;ledger.evaluate(input,redistributed);
    candidate.centerX=7;query=ledger.operatingQuery(input,redistributed,candidate,150,&baseline,4000);
    CHECK(query.transferred==0);CHECK_FALSE(query.feasible);
    // New hybrid service cost cannot inherit the feeding component's waiver.
    input.yield[input.index(8,8)]=8000;input.consumers[0].demand=8000;ledger.evaluate(input,baseline);
    input.consumers[0].demand=4000;ledger.evaluate(input,redistributed);
    query=ledger.operatingQuery(input,redistributed,candidate,150,&baseline,2000);
    CHECK(query.transferred==2000);CHECK_FALSE(query.feasible);
}

TEST_CASE("hybrid service and production claims use separate spatial pools without double spending" * doctest::test_suite("Maxima.FoodLedger"))
{
    Input input=makeInput();input.yield[input.index(8,8)]=7000;
    auto hybrid=makeConsumer(1,SwarmConsumer,7,8,10000);hybrid.productionDemand=6000;
    input.consumers={hybrid,makeConsumer(2,InnConsumer,7,8,2000)};
    Ledger ledger;Result result;ledger.evaluate(input,result);
    CHECK(result.consumer(1)->claimed==5000);CHECK(result.consumer(1)->claimedProduction==1000);
    CHECK(result.consumer(2)->claimed==2000);CHECK(result.independentClaimed==6000);
    auto incumbent=makeConsumer(1,SwarmConsumer,7,8,2000);incumbent.productionDemand=2000;
    input.consumers={incumbent};input.yield[input.index(8,8)]=6000;ledger.evaluate(input,result);
    auto candidate=makeConsumer(3,InnConsumer,7,8,8000);
    candidate.operating.independent[1]=4000;candidate.operating.production[1]=4000;
    auto query=ledger.operatingQuery(input,result,candidate,100);
    CHECK(query.residual==6000);CHECK_FALSE(query.feasible); // six available packets cannot pay eight total
    input.yield[input.index(8,8)]=16000;ledger.evaluate(input,result);
    CHECK(ledger.operatingQuery(input,result,candidate,125).feasible);
    // Free feeding grants no credit to independently priced production.
    candidate.operating.independent[1]=0;input.yield[input.index(8,8)]=5000;ledger.evaluate(input,result);
    query=ledger.operatingQuery(input,result,candidate,100,&result,4000);
    CHECK(query.transferred==0);CHECK_FALSE(query.feasible);
}

TEST_CASE("hybrid queries reserve constrained production before flexible services in either route order" * doctest::test_suite("Maxima.FoodLedger"))
{
    Input input=makeInput(64,64);input.policy.supplyRadius=6;
    input.yield[input.index(8,8)]=5000;input.yield[input.index(11,8)]=5000;
    auto incumbent=makeConsumer(1,SwarmConsumer,12,8,5000);incumbent.productionDemand=5000;
    input.consumers={incumbent};Ledger ledger;Result result;ledger.evaluate(input,result);
    CHECK(result.residual[input.index(8,8)]==5000);CHECK(result.residual[input.index(11,8)]==0);
    for(const auto [x,y]:{std::pair<int,int>{7,8},{12,7}}) {
        auto candidate=makeConsumer(2,InnConsumer,x,y,10000);
        candidate.operating.independent[1]=5000;candidate.operating.production[1]=5000;
        const auto query=ledger.operatingQuery(input,result,candidate,100);
        CHECK(query.feasible);CHECK(query.residual==10000);
        CHECK_FALSE(ledger.operatingQuery(input,result,candidate,120).feasible);
    }
}

TEST_CASE("candidate ledger snapshots omit only final economic bounds" * doctest::test_suite("Maxima.FoodLedger"))
{
    Input input=makeInput(32,32);
    input.yield[input.index(0,8)]=5000;input.yield[input.index(3,8)]=7000;
    auto hybrid=makeConsumer(1,InnConsumer,1,8,4000);hybrid.productionDemand=2000;
    input.consumers={hybrid};Ledger ledger;Result full,query;
    ledger.evaluate(input,full);REQUIRE(full.bestSiteResidual>0);
    query=full; // exercises releasing a full snapshot's inherited final SAT
    ledger.evaluate(input,query,EvaluationMode::CandidateQuery);
    CHECK(query.bestSiteResidual==0);CHECK(query.residual==full.residual);
    CHECK(query.serviceResidual==full.serviceResidual);
    CHECK(query.independentClaimed==full.independentClaimed);
    CHECK(query.totalSupply==full.totalSupply);CHECK(query.totalDemand==full.totalDemand);
    CHECK(query.totalClaimed==full.totalClaimed);CHECK(query.totalResidual==full.totalResidual);
    REQUIRE(query.consumer(1));CHECK(query.consumer(1)->claimed==full.consumer(1)->claimed);
    CHECK(query.consumer(1)->claimedProduction==full.consumer(1)->claimedProduction);
    const Result retained=query;
    for(int x:{31,1,5})for(int production:{0,2000,20000})for(int margin:{100,150}) {
        auto candidate=makeConsumer(2,InnConsumer,x,8,2000+production);
        candidate.operating.independent[1]=2000;candidate.operating.production[1]=production;
        const auto a=ledger.operatingQuery(input,full,candidate,margin,&full,2000);
        const auto b=ledger.operatingQuery(input,retained,candidate,margin,&full,2000);
        CHECK(a.demand==b.demand);CHECK(a.productionDemand==b.productionDemand);
        CHECK(a.residual==b.residual);CHECK(a.quality==b.quality);
        CHECK(a.transferred==b.transferred);CHECK(a.feasible==b.feasible);
        CHECK(ledger.residualUpperBound(input,retained,x,8,0,0,1,1,true)==
              ledger.residualUpperBound(input,full,x,8,0,0,1,1,true));
        CHECK(ledger.residualUpperBound(input,retained,x,8,0,0,1,1)==retained.totalResidual);
        CHECK(ledger.residualUpperBound(input,retained,x,8,0,0,1,1)>=
              ledger.residualUpperBound(input,full,x,8,0,0,1,1));
    }
    // Returning to full evaluation restores economic metadata; copies still
    // carry their own service bounds after the reusable result changes mode.
    ledger.evaluate(input,query);
    CHECK(query.bestSiteResidual==full.bestSiteResidual);
    CHECK(ledger.residualUpperBound(input,query,1,8,0,0,1,1)==
          ledger.residualUpperBound(input,full,1,8,0,0,1,1));
    CHECK(ledger.residualUpperBound(input,retained,1,8,0,0,1,1,true)==
          ledger.residualUpperBound(input,full,1,8,0,0,1,1,true));
    query.clear();CHECK(ledger.residualUpperBound(input,query,1,8,0,0,1,1,true)==0);
}

namespace
{
Input producerAllocationInput()
{
	Input input=makeInput(64,64);
	input.yield[input.index(8,8)]=1000;
	input.consumers.push_back(makeConsumer(1,InnConsumer,7,8,800));
	auto producer=makeConsumer(2,SwarmConsumer,8,7,500);
	producer.productionDemand=500;
	input.consumers.push_back(producer);
	return input;
}
}

TEST_CASE("feeding priority does not make a reachable producer unviable" * doctest::test_suite("Maxima.FoodLedger"))
{
	const Input input=producerAllocationInput();
	Ledger ledger;Result result;ledger.evaluate(input,result);
	const auto* allocation=result.consumer(2);
	REQUIRE(allocation);
	CHECK(allocation->claimedProduction==200);
	CHECK(allocation->coveragePercent==40);
	CHECK(allocation->uncontestedCoveragePercent==100);
	CHECK(allocation->retirementCoveragePercent()==100);
	CHECK(result.totalClaimed==1000); // Viability is not additional funding.
}

TEST_CASE("unreachable production remains a retirement candidate" * doctest::test_suite("Maxima.FoodLedger"))
{
	Input input=producerAllocationInput();
	input.consumers[1].centerX=40;
	input.consumers[1].centerY=40;
	Ledger ledger;Result result;ledger.evaluate(input,result);
	REQUIRE(result.consumer(2));
	CHECK(result.consumer(2)->uncontestedCoveragePercent==0);
	CHECK(result.consumer(2)->retirementCoveragePercent()==0);
}

TEST_CASE("services without production retain allocation-based coverage" * doctest::test_suite("Maxima.FoodLedger"))
{
	Input input=producerAllocationInput();
	input.consumers[1]=makeConsumer(2,InnConsumer,8,7,500);
	Ledger ledger;Result result;ledger.evaluate(input,result);
	REQUIRE(result.consumer(2));
	CHECK(result.consumer(2)->retirementCoveragePercent()==40);
	CHECK(result.totalClaimed==1000);
}

TEST_CASE("mixed services use production viability without double funding" * doctest::test_suite("Maxima.FoodLedger"))
{
	Input input=producerAllocationInput();
	input.consumers[1].kind=InnConsumer;
	input.consumers[1].productionDemand=200;
	Ledger ledger;Result result;ledger.evaluate(input,result);
	REQUIRE(result.consumer(2));
	CHECK(result.consumer(2)->claimedProduction==0);
	CHECK(result.consumer(2)->coveragePercent==40);
	CHECK(result.consumer(2)->retirementCoveragePercent()==100);
	CHECK(result.totalClaimed==1000);
}

TEST_CASE("zero-demand viability needs no food" * doctest::test_suite("Maxima.FoodLedger"))
{
	Input input=producerAllocationInput();
	input.consumers[1].demand=0;
	input.consumers[1].productionDemand=0;
	Ledger ledger;Result result;ledger.evaluate(input,result);
	const auto* allocation=result.consumer(2);
	REQUIRE(allocation);
	CHECK(allocation->claimed==0);
	CHECK(allocation->uncontestedCoveragePercent>=100);
	CHECK(allocation->retirementCoveragePercent()==allocation->coveragePercent);
	CHECK(result.totalClaimed==800);
}

TEST_CASE("coordinate masks match modulo including negative and extreme inputs" * doctest::test_suite("Maxima.FoodLedger"))
{
	std::vector<int> sizes={INT_MIN,-1,0,3,5,127,129,1023,INT_MAX};
	for(int shift=0;shift<31;++shift)sizes.push_back(1<<shift);
	const auto reference=[](int coordinate,int size) {
		if(size<=0)return 0;
		const int remainder=coordinate%size;
		return remainder<0?remainder+size:remainder;
	};
	for(int size:sizes) {
		Input input;input.setDimensions(size,size);
		for(int coordinate:{INT_MIN,INT_MIN+1,-65537,-1025,-129,-128,-17,-1,0,1,17,127,128,1025,65537,INT_MAX}) {
			CAPTURE(size);CAPTURE(coordinate);
			CHECK(input.normalizeX(coordinate)==reference(coordinate,size));
			CHECK(input.normalizeY(coordinate)==reference(coordinate,size));
		}
	}
}

TEST_CASE("direct dimension changes cannot leave a stale wrapping mask" * doctest::test_suite("Maxima.FoodLedger"))
{
	Input input;input.setDimensions(16,32);
	for(int size:{3,64,1,0,-1,16,32}) {
		input.width=size;input.height=size;
		for(int coordinate=-130;coordinate<=130;++coordinate) {
			const int expected=size>0 ? (coordinate%size+size)%size : 0;
			CHECK(input.normalizeX(coordinate)==expected);
			CHECK(input.normalizeY(coordinate)==expected);
		}
	}
	input.setDimensions(64,8);
	CHECK(input.normalizeX(-65)==63);
	CHECK(input.normalizeY(-9)==7);
}

TEST_CASE("masked food walks preserve modulo claims and query results" * doctest::test_suite("Maxima.FoodLedger"))
{
	for(const auto dimensions:{std::pair<int,int>{16,8},{8,16},{3,5},{1,4}}) {
		Input masked=makeInput(dimensions.first,dimensions.second);
		masked.policy.supplyRadius=6;
		for(size_t i=0;i<masked.yield.size();++i) {
			masked.yield[i]=i%3==0 ? 100 : 0;
			masked.traversable[i]=i%7!=2;
		}
		masked.consumers.push_back(makeConsumer(1,InnConsumer,-1,0,150));
		masked.consumers.push_back(makeConsumer(2,SwarmConsumer,masked.width,masked.height-1,250));
		Input modulo;modulo.width=masked.width;modulo.height=masked.height;
		modulo.policy=masked.policy;modulo.yield=masked.yield;
		modulo.traversable=masked.traversable;modulo.consumers=masked.consumers;
		Ledger fast,reference;Result a,b;fast.evaluate(masked,a);reference.evaluate(modulo,b);
		CHECK(a.residual==b.residual);CHECK(a.serviceResidual==b.serviceResidual);
		CHECK(a.totalSupply==b.totalSupply);CHECK(a.totalDemand==b.totalDemand);
		CHECK(a.totalClaimed==b.totalClaimed);CHECK(a.totalResidual==b.totalResidual);
		CHECK(a.bestSiteResidual==b.bestSiteResidual);
		REQUIRE(a.consumers.size()==b.consumers.size());
		for(size_t i=0;i<a.consumers.size();++i) {
			CHECK(a.consumers[i].key==b.consumers[i].key);
			CHECK(a.consumers[i].order==b.consumers[i].order);
			CHECK(a.consumers[i].claimed==b.consumers[i].claimed);
			CHECK(a.consumers[i].available==b.consumers[i].available);
			CHECK(a.consumers[i].quality==b.consumers[i].quality);
		}
		for(int y=-1;y<=masked.height;++y)for(int x=-1;x<=masked.width;++x) {
			CHECK(fast.reachableResidual(masked,a,x,y,0,0,1,1,150)==reference.reachableResidual(modulo,b,x,y,0,0,1,1,150));
			CHECK(fast.residualQuality(masked,a,x,y,0,0,1,1,150)==reference.residualQuality(modulo,b,x,y,0,0,1,1,150));
		}
	}
}
