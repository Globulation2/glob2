// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ResumableGradientReference.h"
#include "GradientPropagation.h"
#include <queue>
#include <random>

namespace
{
using namespace gradient_kernel;
using Reference=ResumableGradientReference;
Reference::Input input(field::Grid grid,std::vector<std::uint16_t> seeds,std::vector<EntrySteps> costs,int cap) {
    return {std::make_shared<const std::vector<std::uint16_t>>(std::move(seeds)),
        std::make_shared<const std::vector<EntrySteps>>(std::move(costs)),
        [](const void* value,std::size_t cell){return static_cast<const std::vector<EntrySteps>*>(value)->at(cell);},
        grid,cap,grid.cells()*sizeof(EntrySteps)+sizeof(std::vector<EntrySteps>)};
}
std::vector<std::uint16_t> oracle(const Reference::Input& value) {
    auto result=*value.seeds;
    const int cap=std::min(value.maxCost,COST_LIMIT);
    using Entry=std::pair<int,int>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> pending;
    for(std::size_t cell=0;cell<result.size();++cell)
        if(result[cell]>GRADIENT_UNREACHABLE) pending.emplace(GRADIENT_AT_GOAL-result[cell],int(cell));
    while(!pending.empty()) {
        const auto [cost,cell]=pending.top();pending.pop();
        if(cost>cap || cost!=GRADIENT_AT_GOAL-result[cell]) continue;
        const auto entry=value.costAt(value.costsOwner.get(),std::size_t(cell));
        const int x=cell%value.grid.width(),y=cell/value.grid.width();
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            if(!dx && !dy) continue;
            const auto next=value.grid.index(x+dx,y+dy);
            const auto nextCost=std::uint64_t(cost)+(dx && dy ? entry.diagonal : entry.cardinal);
            if(result[next] && nextCost<=std::uint64_t(cap) && GRADIENT_AT_GOAL-nextCost>result[next]) {
                result[next]=std::uint16_t(GRADIENT_AT_GOAL-nextCost);pending.emplace(int(nextCost),next);
            }
        }
    }
    return result;
}
void finish(Reference& reference,unsigned entries) {
    unsigned calls=0;
    while(reference.status()==Reference::Status::Pending) {
        REQUIRE(++calls<1000000);
        reference.advance(Reference::MaxChunkCpuNs,entries);
    }
    REQUIRE(reference.status()==Reference::Status::Complete);
}
std::vector<std::uint16_t> result(const Reference& reference) {
    return {reference.result().begin(),reference.result().end()};
}
}

TEST_CASE("resumable probe reference preserves arbitrary seeds and agrees with an independent oracle" * doctest::test_suite("GradientProbe"))
{
    std::mt19937 random(104503);
    for(int width:{1,2,3,16}) for(int height:{1,2,5,17}) for(int cap:{-1,0,1,42,257,COST_LIMIT}) {
        CAPTURE(width);CAPTURE(height);CAPTURE(cap);
        const field::Grid grid(width,height);
        std::vector<std::uint16_t> seeds(grid.cells(),GRADIENT_UNREACHABLE);
        std::vector<EntrySteps> costs(grid.cells());
        for(std::size_t cell=0;cell<grid.cells();++cell) {
            costs[cell]={unsigned(1+random()%70000),unsigned(1+random()%70000)};
            switch(random()%7) {
            case 0:seeds[cell]=GRADIENT_FORBIDDEN;break;
            case 1:seeds[cell]=GRADIENT_AT_GOAL;break;
            case 2:seeds[cell]=std::uint16_t(GRADIENT_AT_GOAL-random()%65534);break;
            case 3:seeds[cell]=GRADIENT_FORBIDDEN_BORDER;break;
            default:break;
            }
        }
        auto value=input(grid,std::move(seeds),std::move(costs),cap);
        const auto original=*value.seeds,expected=oracle(value);
        for(unsigned entries:{1,7,64,4096}) {
            auto reference=Reference::tryCreate(value);REQUIRE(reference);
            CHECK(reference->result().empty());finish(*reference,entries);
            CHECK(result(*reference)==expected);CHECK(*value.seeds==original);
            CHECK(reference->metrics().heapPeak<=grid.cells());
        }
    }
}

TEST_CASE("resumable probe reference matches original production CPU propagation including deferred sources" * doctest::test_suite("GradientProbe"))
{
    std::mt19937 random(104507);
    GradientWorkspace workspace;
    for(int width:{1,3,16}) for(int height:{1,5,8}) for(int swim:{0,1,4,6}) for(int cap:{0,42,700,COST_LIMIT}) {
        const field::Grid grid(width,height);
        std::vector<std::uint16_t> seeds(grid.cells(),1);
        std::vector<std::uint8_t> water(grid.cells());
        std::vector<EntrySteps> costs(grid.cells());
        for(std::size_t cell=0;cell<grid.cells();++cell) {
            water[cell]=random()%2;costs[cell]=weightedClass(swim) && water[cell] ? entrySteps(WATER_STEP[swim]) : LAND_STEPS;
            if(random()%7==0) seeds[cell]=0;
            else if(random()%4==0) seeds[cell]=std::uint16_t(GRADIENT_AT_GOAL-random()%1200);
        }
        if(seeds[0]) seeds[0]=GRADIENT_AT_GOAL;
        auto expected=seeds;
        propagateFieldCPU(expected.data(),swim,cap,grid,workspace,[&](std::size_t cell){return water[cell]!=0;});
        auto reference=Reference::tryCreate(input(grid,seeds,std::move(costs),cap));REQUIRE(reference);
        finish(*reference,3);CHECK(result(*reference)==expected);
    }
}

TEST_CASE("probe chunks check CPU every at most 64 entries and pause within a large source layer" * doctest::test_suite("GradientProbe"))
{
    struct Clock {std::uint64_t value=1,reads=0;};
    Clock clock;
    auto read=[](void* context) {auto& value=*static_cast<Clock*>(context);++value.reads;return value.value+=100000;};
    const field::Grid grid(129,33);
    auto reference=Reference::tryCreate(input(grid,std::vector<std::uint16_t>(grid.cells(),GRADIENT_AT_GOAL),
        std::vector<EntrySteps>(grid.cells(),LAND_STEPS),COST_LIMIT),{&clock,read});
    REQUIRE(reference);
    while(reference->status()==Reference::Status::Pending) {
        const auto previous=reference->metrics();
        const auto reads=clock.reads;
        reference->advance(1,4096);
        CHECK(reference->metrics().entries-previous.entries<=Reference::ClockCheckEntries);
        CHECK(clock.reads-reads>=2);
        CHECK(reference->metrics().lastChunkCpuNs<=Reference::MaxChunkCpuNs);
    }
    CHECK(reference->status()==Reference::Status::Complete);
    CHECK(reference->metrics().chunks>grid.cells()/64);
}

TEST_CASE("probe cancellation and invalid input release bounded retained leases" * doctest::test_suite("GradientProbe"))
{
    const auto baseline=ProbeHostLease::currentBytes();
    auto value=input({5,3},std::vector<std::uint16_t>(15,GRADIENT_AT_GOAL),std::vector<EntrySteps>(15,LAND_STEPS),COST_LIMIT);
    std::weak_ptr<const void> seeds=value.seeds,costs=value.costsOwner;
    auto reference=Reference::tryCreate(value);REQUIRE(reference);
    value={};CHECK_FALSE(seeds.expired());CHECK_FALSE(costs.expired());
    reference->advance(Reference::MaxChunkCpuNs,1);
    CHECK(ProbeHostLease::currentBytes()>baseline);
    reference->requestCancel();CHECK(reference->advance()==Reference::Status::Canceled);
    CHECK(reference->result().empty());CHECK(seeds.expired());CHECK(costs.expired());
    CHECK(ProbeHostLease::currentBytes()==baseline);
    CHECK_FALSE(Reference::tryCreate({}));
    auto oversized=input({1,1},{GRADIENT_AT_GOAL},{LAND_STEPS},COST_LIMIT);
    oversized.retainedCostBytes=ProbeHostLease::Budget;
    CHECK_FALSE(Reference::tryCreate(oversized));CHECK(ProbeHostLease::currentBytes()==baseline);
    // A zero edge is invalid only if the cell is actually expanded; the
    // original immutable input survives the failed optional computation.
    auto invalid=input({1,1},{GRADIENT_AT_GOAL},{{0,14}},COST_LIMIT);
    reference=Reference::tryCreate(invalid);REQUIRE(reference);
    while(reference->status()==Reference::Status::Pending)reference->advance();
    CHECK(reference->status()==Reference::Status::Failed);CHECK(reference->error()!=nullptr);
    CHECK(ProbeHostLease::currentBytes()==baseline);CHECK((*invalid.seeds)[0]==GRADIENT_AT_GOAL);
}

TEST_CASE("probe admission counts every simultaneous source and output lease" * doctest::test_suite("GradientProbe"))
{
    const auto baseline=ProbeHostLease::currentBytes();
    auto value=input({1,1},{GRADIENT_AT_GOAL},{LAND_STEPS},0);
    value.retainedCostBytes=ProbeHostLease::Budget/2;
    auto first=Reference::tryCreate(value);REQUIRE(first);
    CHECK_FALSE(Reference::tryCreate(value));
    first.reset();CHECK(ProbeHostLease::currentBytes()==baseline);
    auto recovered=Reference::tryCreate(value);REQUIRE(recovered);
    // A missing native thread clock falls back to at most 64 entries per call.
    auto noClock=[](void*)->std::uint64_t{return 0;};
    recovered.reset();
    auto bounded=Reference::tryCreate(input({100,1},std::vector<std::uint16_t>(100,1),
        std::vector<EntrySteps>(100,LAND_STEPS),COST_LIMIT),{nullptr,noClock});REQUIRE(bounded);
    bounded->advance();CHECK(bounded->metrics().entries<=64);CHECK_FALSE(bounded->metrics().cpuClockAvailable);
}
