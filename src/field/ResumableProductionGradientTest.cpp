// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ResumableProductionGradient.h"
#include <queue>
#include <random>

namespace
{
using namespace gradient_kernel;
using Production=ResumableProductionGradient;
EntrySteps costAt(const Production::Input& input,std::size_t cell) {
    if(input.movement) return input.movement->profiles[input.classAt(input.costsOwner.get(),cell)];
    return weightedClass(input.swim)&&input.isWater(input.costsOwner.get(),cell) ? entrySteps(WATER_STEP[input.swim]) : LAND_STEPS;
}
std::vector<std::uint16_t> oracle(const Production::Input& input) {
    auto result=*input.seeds;const auto limit=std::min(input.maxCost,COST_LIMIT);
    using Entry=std::pair<unsigned,std::size_t>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
    for(std::size_t cell=0;cell<result.size();++cell)
        if(result[cell]>1) queue.emplace(65535-result[cell],cell);
    while(!queue.empty()) {
        const auto [cost,cell]=queue.top();queue.pop();
        if(limit<0 || cost>unsigned(limit) || cost!=unsigned(65535-result[cell])) continue;
        const auto step=costAt(input,cell);
        for(auto offset:field::Surrounding) {
            const auto next=input.grid.index(int(cell%input.grid.width())+offset.x,int(cell/input.grid.width())+offset.y);
            const auto total=cost+(offset.x&&offset.y?step.diagonal:step.cardinal);
            if(result[next] && total<=unsigned(limit) && 65535-total>result[next]) {
                result[next]=std::uint16_t(65535-total);queue.emplace(total,std::size_t(next));
            }
        }
    }
    return result;
}
std::vector<std::uint16_t> eager(const Production::Input& input) {
    auto result=*input.seeds;GradientWorkspace workspace;
    if(!input.movement) propagateFieldCPU(result.data(),input.swim,input.maxCost,input.grid,workspace,
        [&](std::size_t cell){return input.isWater&&input.isWater(input.costsOwner.get(),cell);});
    else {
        const auto at=[&](std::size_t cell){return input.classAt(input.costsOwner.get(),cell);};
        if(input.buckets==64) runtime_terrain::propagate<64,true,false>(result.data(),input.maxCost,input.grid,workspace,at,*input.movement);
        else if(input.buckets==128) runtime_terrain::propagate<128,true,false>(result.data(),input.maxCost,input.grid,workspace,at,*input.movement);
        else runtime_terrain::propagate<256,true,false>(result.data(),input.maxCost,input.grid,workspace,at,*input.movement);
    }
    return result;
}
void check(Production::Input input,unsigned entries) {
    const auto original=*input.seeds,expected=oracle(input);
    CHECK(eager(input)==expected);
    // Exactness is checked independently of allocator/OS CPU overshoots. Real
    // clock/budget declines have separate tests and must never qualify a plan.
    auto reference=Production::tryCreate(input,{nullptr,[](void*)->std::uint64_t{return 1;}});REQUIRE(reference);
    for(unsigned attempts=0;reference->status()==Production::Status::Pending;++attempts) {
        REQUIRE(attempts<1000000);const auto before=reference->metrics().entries;
        reference->advance(Production::MaxChunkCpuNs,entries);
        CHECK(reference->metrics().entries-before<=entries);CHECK(*input.seeds==original);
    }
    REQUIRE(reference->status()==Production::Status::Complete);
    CHECK(std::vector<std::uint16_t>(reference->result().begin(),reference->result().end())==expected);
}
struct PreparedInput {
    TerrainRegistry::Movement movement;
    std::vector<std::uint8_t> classes;
};
}
TEST_CASE("production probe slices preserve classic SIMD output arbitrary seeds and wrapping" * doctest::test_suite("GradientProbe"))
{
    std::mt19937 random(104509);
    for(int width:{1,3,16,31}) for(int height:{1,5,17}) for(int swim:{0,1,3,6}) for(int cap:{-1,0,42,700,COST_LIMIT}) {
        const field::Grid grid(width,height);
        auto seeds=std::make_shared<std::vector<std::uint16_t>>(grid.cells(),1);
        auto water=std::make_shared<std::vector<std::uint8_t>>(grid.cells());
        for(std::size_t cell=0;cell<grid.cells();++cell) {
            (*water)[cell]=random()%2;
            if(random()%6==0) (*seeds)[cell]=0;
            else if(random()%7==0) (*seeds)[cell]=std::uint16_t(65535-random()%1200);
        }
        (*seeds)[0]=65535;
        Production::Input input{seeds,water,grid,cap,swim};
        input.isWater=[](const void* p,std::size_t cell){return (*static_cast<const std::vector<std::uint8_t>*>(p))[cell]!=0;};
        input.retainedCostBytes=sizeof(*water)+water->capacity();
        for(unsigned entries:{1,7,64,4096}) check(input,entries);
    }
}
TEST_CASE("production probe slices preserve prepared uniform and shared-slot SIMD paths" * doctest::test_suite("GradientProbe"))
{
    std::mt19937 random(104511);
    for(unsigned ring:{64,128,256}) for(bool uniform:{false,true}) for(int width:{1,7,16}) for(int height:{1,13}) {
        const field::Grid grid(width,height);
        auto owner=std::make_shared<PreparedInput>();
        owner->movement.profiles={{5,7},{11,11},{ring-3,ring-1}};
        std::array<EntrySteps,8> steps;steps.fill(owner->movement.profiles[0]);
        std::copy(owner->movement.profiles.begin(),owner->movement.profiles.end(),steps.begin());
        owner->movement.prepared=PreparedTerrainCosts<8>(steps);
        owner->classes.resize(grid.cells());
        auto seeds=std::make_shared<std::vector<std::uint16_t>>(grid.cells(),1);
        for(std::size_t cell=0;cell<grid.cells();++cell) {
            owner->classes[cell]=uniform?1:random()%3;
            if(random()%5==0) (*seeds)[cell]=0;
            else if(random()%11==0) (*seeds)[cell]=std::uint16_t(65535-random()%1000);
        }
        (*seeds)[0]=65535;
        Production::Input input{seeds,owner,grid,700};input.buckets=ring;input.movement=&owner->movement;
        input.classAt=[](const void* p,std::size_t cell){return unsigned(static_cast<const PreparedInput*>(p)->classes[cell]);};
        input.retainedCostBytes=sizeof(*owner)+owner->classes.capacity()+owner->movement.profiles.capacity()*sizeof(EntrySteps);
        for(unsigned entries:{1,7,64,4096}) check(input,entries);
    }
}
TEST_CASE("production probe pauses within source layers and rejects unpaid storage or deferred work" * doctest::test_suite("GradientProbe"))
{
    const auto before=openCLProbeBytes();
    const field::Grid grid(129,17);
    Production::Input input;input.grid=grid;
    input.seeds=std::make_shared<const std::vector<std::uint16_t>>(grid.cells(),65535);
    auto reference=Production::tryCreate(input);REQUIRE(reference);
    for(unsigned i=0;i<10;++i) {reference->advance(Production::MaxChunkCpuNs,7);CHECK(reference->status()==Production::Status::Pending);}
    CHECK(reference->metrics().reservedHostBytes>0);reference->requestCancel();
    CHECK(reference->advance()==Production::Status::Canceled);CHECK(reference->result().empty());
    CHECK(openCLProbeBytes()==before);
    input.seeds=std::make_shared<const std::vector<std::uint16_t>>(grid.cells(),65535-1000);
    reference=Production::tryCreate(input);REQUIRE(reference);
    while(reference->status()==Production::Status::Pending) reference->advance();
    CHECK(reference->status()==Production::Status::Declined);CHECK(openCLProbeBytes()==before);
    input.retainedCostBytes=OpenCLProbeBudget;CHECK_FALSE(Production::tryCreate(input));CHECK(openCLProbeBytes()==before);
    auto clock=[](void*)->std::uint64_t{return 0;};input.retainedCostBytes=0;
    Production::Metrics declined;
    reference=Production::tryCreate(input,{nullptr,clock},&declined);CHECK_FALSE(reference);
    CHECK_FALSE(declined.cpuClockAvailable);CHECK(declined.invalidCpuClocks==1);
    CHECK(declined.requiresFullReservedCharge);CHECK(openCLProbeBytes()==before);
}

namespace {
struct ScriptedProductionClock {
    std::vector<std::uint64_t> values;
    std::size_t reads=0,throwAt=std::numeric_limits<std::size_t>::max();
    static std::uint64_t read(void* p) {
        auto& clock=*static_cast<ScriptedProductionClock*>(p);
        const auto at=clock.reads++;
        if(at==clock.throwAt)throw std::runtime_error("injected CPU clock failure");
        return at<clock.values.size()?clock.values[at]:100000+at;
    }
    Production::Clock hook(){return {this,read};}
};
}
TEST_CASE("production probe setup clock failures release leases without seed mutation" * doctest::test_suite("GradientProbe"))
{
    const auto before=openCLProbeBytes();
    Production::Input input;input.grid={17,5};
    input.seeds=std::make_shared<const std::vector<std::uint16_t>>(input.grid.cells(),1);
    const auto original=*input.seeds;
    for(unsigned scenario=0;scenario<5;++scenario) {
        CAPTURE(scenario);
        ScriptedProductionClock clock{{100,200}};
        if(scenario==0)clock.values[0]=0;
        if(scenario==1)clock.throwAt=0;
        if(scenario==2)clock.values[1]=0;
        if(scenario==3)clock.values[1]=99;
        if(scenario==4)clock.throwAt=1;
        Production::Metrics declined;
        auto reference=Production::tryCreate(input,clock.hook(),&declined);
        CHECK_FALSE(reference);CHECK_FALSE(declined.cpuClockAvailable);
        CHECK(declined.invalidCpuClocks==1);CHECK(declined.requiresFullReservedCharge);
        CHECK(declined.reservedHostBytes==0);CHECK(openCLProbeBytes()==before);
        CHECK(*input.seeds==original);CHECK(clock.reads==(scenario<2?1:2));
    }
}
TEST_CASE("production probe clocks retain a floor across checkpoints and advances" * doctest::test_suite("GradientProbe"))
{
    const auto before=openCLProbeBytes();
    Production::Input input;input.grid={33,5};
    input.seeds=std::make_shared<const std::vector<std::uint16_t>>(input.grid.cells(),1);
    const auto original=*input.seeds;
    for(unsigned scenario=0;scenario<6;++scenario) {
        CAPTURE(scenario);
        ScriptedProductionClock clock{{1,2,100,500,200,600}};
        std::size_t expectedReads=5;
        if(scenario==1) {clock.values={1,2,100,110,120,130,90,200};expectedReads=7;}
        if(scenario==2) {clock.values={1,2,0,100};expectedReads=3;}
        if(scenario==3) {clock.throwAt=3;expectedReads=4;}
        if(scenario==4) {clock.values={1,2,100,110,120,0};expectedReads=6;}
        if(scenario==5) {clock.values={1,2,100,110,120,130,140,150,0,180};expectedReads=9;}
        auto reference=Production::tryCreate(input,clock.hook());REQUIRE(bool(reference));
        CHECK(reference->metrics().cpuClockAvailable);
        if(scenario==1 || scenario==5) {
            REQUIRE(reference->advance(Production::MaxChunkCpuNs,1)==Production::Status::Pending);
            CHECK(reference->metrics().entries==1);
        }
        CHECK(reference->advance(Production::MaxChunkCpuNs,scenario==4?1:128)==Production::Status::Declined);
        const auto measured=reference->metrics();
        CHECK_FALSE(measured.cpuClockAvailable);CHECK(measured.invalidCpuClocks==1);
        CHECK(measured.requiresFullReservedCharge);CHECK(measured.reservedHostBytes==0);
        CHECK(reference->result().empty());CHECK(openCLProbeBytes()==before);
        CHECK(*input.seeds==original);CHECK(clock.reads==expectedReads);
        CHECK(reference->advance()==Production::Status::Declined);
        CHECK(clock.reads==expectedReads);CHECK(reference->metrics().entries==measured.entries);
    }
}
