// SPDX-License-Identifier: GPL-3.0-or-later
#include "CaptureProbeReplay.h"
#include "GradientPropagation.h"
#include "Glob2Test.h"
#include "ThreadSupport.h"
#include <vector>

namespace
{
using namespace gradient_kernel;
struct Timer {
    std::uint64_t value=100,calls=0,failAt=0,step=100;
    static std::uint64_t read(const void* context) {
        auto& timer=*const_cast<Timer*>(static_cast<const Timer*>(context));
        ++timer.calls;if(timer.calls==timer.failAt)return 0;
        return timer.value+=timer.step;
    }
};
struct Driver {
    std::vector<std::uint16_t> original,output;
    std::shared_ptr<const void> borrowed;
    CostIdentity retainedIdentity;
    unsigned commands=0,polls=0,cancels=0,costCalls=0;
    bool ready=true,quarantine=false,terminal=false,failed=false,pending=false,awaitingEvent=false,retainCosts=false,provideMetrics=false;
    struct Work final : CaptureProbeReplay::Probe {
        Driver& driver;std::shared_ptr<const void> owner;bool canceled=false,quarantined=false;
        Work(Driver& value,std::shared_ptr<const void> keep):driver(value),owner(std::move(keep)) {}
        ~Work(){if(quarantined)driver.borrowed=std::move(owner);}
        OpenCLProbeProgress advance() noexcept override {
            if(canceled)return OpenCLProbeProgress::Declined;
            ++driver.commands;
            if(driver.failed)return OpenCLProbeProgress::Declined;
            if(driver.quarantine){quarantined=true;return OpenCLProbeProgress::Quarantined;}
            if(driver.pending)return OpenCLProbeProgress::Pending;
            return OpenCLProbeProgress::Complete;
        }
        void cancel() noexcept override {canceled=true;++driver.cancels;}
        bool executionFailed() const noexcept override {return driver.failed && !canceled;}
        bool waitingForEvent() const noexcept override {return driver.awaitingEvent;}
        bool metrics(OpenCLProbe::Metrics& out) const noexcept override {
            if(!driver.provideMetrics)return false;
            out.stage=OpenCLProbe::Stage::CheckWait;out.dispatches=driver.commands;out.copiedCells=driver.original.size();return true;
        }
        std::span<const std::uint16_t> result() const noexcept override {return driver.output;}
    };
    static bool isReady(const void* value) noexcept {return static_cast<const Driver*>(value)->ready;}
    static std::unique_ptr<CaptureProbeReplay::Probe> begin(const OpenCLProbeInput& input,Plan,
        std::shared_ptr<const void> owner,const void* value) {
        auto& driver=*const_cast<Driver*>(static_cast<const Driver*>(value));
        CHECK(std::equal(driver.original.begin(),driver.original.end(),input.gradient));
        CHECK(input.identity.allCells);CHECK(input.identity.retainedBytes>0);CHECK(input.capturedPrivateCosts);
        CHECK(bool(input.identity.prechargedOwner));CHECK(retainedCostOwnerBytes(input.identity)==0);
        if(driver.retainCosts)driver.retainedIdentity=input.identity;
        for(std::size_t cell=0;cell<input.grid.cells();++cell) {
            const auto steps=input.costAt(input.context,cell);CHECK(steps.cardinal>0);++driver.costCalls;
        }
        return std::make_unique<Work>(driver,std::move(owner));
    }
    static bool reap(const void* value) noexcept {
        auto& driver=*const_cast<Driver*>(static_cast<const Driver*>(value));++driver.polls;
        if(!driver.terminal)return false;
        driver.borrowed.reset();return true;
    }
};
struct Fixture {
    std::shared_ptr<CpuSavingPolicy> policy=std::make_shared<CpuSavingPolicy>();
    std::shared_ptr<Timer> timer=std::make_shared<Timer>();
    std::shared_ptr<Driver> driver=std::make_shared<Driver>();
    GradientCpuCapture::Input input;
    std::vector<std::uint16_t> seeds=std::vector<std::uint16_t>(9,1);
    std::shared_ptr<GradientCpuCapture::Record> record;
    Fixture() {
        seeds[0]=65535;seeds[3]=0;seeds[7]=65300;
        input.original=seeds;input.key={9,1,BUCKETS,3,1,Family::Materials,255,255};input.key.limit=COST_LIMIT;
        input.tick=1;input.due=17;input.requiredGeneration=91;input.alternativeReady=true;
        input.reservedCpuNs=5000000;input.captureCpuLimitNs=2000000;
        input.slackNs=100000000;input.conservativeElapsedNs=1000000;
        policy->qualify(input.key,input.alternative,true);policy->observeAccepted(input.key,Plan::CPU,100000000000ull,1);
        driver->original=seeds;GradientCpuCapture::Receipt receipt;
        std::unique_ptr<GradientCpuCapture> capture;
        for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !capture;++i)
            capture=GradientCpuCapture::begin(policy,input,receipt,{timer,&Timer::read,sizeof(Timer)});
        REQUIRE(bool(capture));
        GradientWorkspace scratch;propagateFieldCPU(seeds.data(),0,COST_LIMIT,field::Grid(9,1),scratch,[](std::size_t){return false;});
        driver->output=seeds;record=capture->complete(seeds,{91,1000000,true,true},receipt);REQUIRE(bool(record));
    }
    CaptureProbeReplay::Admission admission() const {return {91,1,100000000,false};}
    std::unique_ptr<CaptureProbeReplay> begin(CaptureProbeReplay::Receipt& receipt,
        std::uint64_t allowance=0,bool calibrated=false) {
        const auto bytes=sizeof(Driver)+sizeof(Driver::Work)+256+
            (driver->original.capacity()+driver->output.capacity())*sizeof(std::uint16_t);
        auto offered=admission();offered.requiredOnlyAllowanceCpuNs=allowance;offered.requiredAllowanceQualified=calibrated;
        offered.requiredAllowanceCalibration=calibrated?42:0;
        return CaptureProbeReplay::begin(std::move(record),offered,receipt,
            {timer,&Timer::read,sizeof(Timer)},{driver,bytes,&Driver::isReady,&Driver::begin,&Driver::reap});
    }
};
}

TEST_CASE("captured GPU replay compares immutable arrays before terminal settlement" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();Fixture fixture;CaptureProbeReplay::Receipt receipt;
    fixture.seeds.assign(9,77);
    auto replay=fixture.begin(receipt);REQUIRE(bool(replay));CHECK_FALSE(fixture.record);
    CHECK(receipt.attemptedCallingCpuNs>0);CHECK_FALSE(receipt.nativeBackend);CHECK(receipt.inferenceSuppressed);
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::Pending);
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::ReadyToSettle);
    CHECK(replay->receipt().exact);CHECK(replay->receipt().comparedCells==9);CHECK(fixture.driver->commands==1);
    auto token=replay->takeSettlement();REQUIRE(bool(token));
    token->cleanup();CHECK(openCLProbeBytes()>baseline);CHECK(fixture.policy->metrics().probeCpuNs==0);
    replay.reset();CHECK_FALSE(token->settle({100,1000,1000,100000000,true,true,true}));
    CHECK(openCLProbeBytes()==baseline);CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
    CHECK(fixture.policy->metrics().promoted==0);
}

TEST_CASE("trusted captured cost owners retain real credits and reject foreign identities" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();Fixture fixture;CaptureProbeReplay::Receipt receipt;
    fixture.driver->retainCosts=true;
    struct Cleanup {std::shared_ptr<Driver> driver;~Cleanup(){driver->retainedIdentity={};}} cleanup{fixture.driver};
    auto replay=fixture.begin(receipt);REQUIRE(bool(replay));
    auto& identity=fixture.driver->retainedIdentity;
    REQUIRE(bool(identity.prechargedOwner));CHECK(retainedCostOwnerBytes(identity)==0);
    std::weak_ptr<const PrechargedProbeCostOwner> weak=identity.prechargedOwner;
    auto foreign=identity;
    foreign.owner=std::shared_ptr<const void>(identity.owner.get(),[](const void*){});
    CHECK(retainedCostOwnerBytes(foreign)==identity.retainedBytes); // Same address, different control.
    foreign.owner=std::shared_ptr<const void>(identity.owner,fixture.driver.get());
    CHECK(retainedCostOwnerBytes(foreign)==identity.retainedBytes); // Same control, different address.
    foreign.prechargedOwner.reset();foreign.owner=fixture.driver;
    CHECK(retainedCostOwnerBytes(foreign)==identity.retainedBytes);foreign={};
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::Pending);
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::ReadyToSettle);
    CHECK_FALSE(replay->takeSettlement());CHECK(fixture.policy->metrics().probeCpuNs==0);
    identity={}; // Simulates final private Plane destruction after terminal proof.
    CHECK(weak.expired());auto token=replay->takeSettlement();REQUIRE(bool(token));
    token->cleanup();replay.reset();token.reset();
    CHECK(openCLProbeBytes()>baseline); // Escaped weak wrapper control remains precharged.
    CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
    weak.reset();CHECK(openCLProbeBytes()==baseline);
}

TEST_CASE("isolated trusted storage admits exact weighted 1024 replay and declines 2048 capture" * doctest::test_suite("OpenCLGradient"))
{
    const auto* activated=std::getenv("GLOB2_TEST_OPENCL_CAPTURE_STORAGE");
    if(!activated || std::strcmp(activated,"1"))return;
    REQUIRE(initializeOpenCL());const auto configured=openCLStatus();
    REQUIRE_FALSE(configured.parityBound);REQUIRE_FALSE(configured.directSeedUpload);
    REQUIRE(configured.apiCpuMode==0);REQUIRE_FALSE(configured.uniformMetadata);
    REQUIRE_FALSE(configured.probeColdCosts);
    REQUIRE((readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))!=0);
    const auto baseline=openCLProbeBytes(),initializationCpu=configured.initializationThreadCPUNs;
    const field::Grid grid(1024,1024);
    std::vector<std::uint16_t> seeds(grid.cells(),GRADIENT_UNREACHABLE);
    std::vector<std::uint8_t> water(grid.cells());
    seeds[0]=GRADIENT_AT_GOAL;seeds[101]=GRADIENT_FORBIDDEN;seeds[709]=GRADIENT_AT_GOAL-127;
    for(std::size_t i=0;i<water.size();++i)water[i]=i%7==0?1:0;
    const auto originals=seeds;
    auto timer=std::make_shared<Timer>();auto policy=std::make_shared<CpuSavingPolicy>();
    GradientCpuCapture::Input input;input.original=seeds;input.water=water;input.swim=1;input.maxCost=700;
    input.key={1024,1024,BUCKETS,3,1,Family::Materials,255,255};input.key.limit=700;input.key.movement=1;
    input.tick=1;input.due=1001;input.requiredGeneration=91;input.alternativeReady=true;
    input.reservedCpuNs=8000000;input.captureCpuLimitNs=2000000;
    input.slackNs=10000000000ull;input.conservativeElapsedNs=1000000;
    policy->qualify(input.key,Plan::Frozen8,true);policy->observeAccepted(input.key,Plan::CPU,100000000000ull,1);
    GradientCpuCapture::Receipt capturedReceipt;std::unique_ptr<GradientCpuCapture> capture;
    for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !capture;++i)
        capture=GradientCpuCapture::begin(policy,input,capturedReceipt,{timer,&Timer::read,sizeof(Timer)});
    REQUIRE(bool(capture));CHECK(capturedReceipt.reservedBytes<6*grid.cells());
    GradientWorkspace scratch;
    const auto requiredStart=glob2::threadCpuNs();
    propagateFieldCPU(seeds.data(),1,700,grid,scratch,[&](std::size_t cell){return water[cell]!=0;});
    const auto requiredEnd=glob2::threadCpuNs();REQUIRE(requiredStart!=0);REQUIRE(requiredEnd>requiredStart);
    auto record=capture->complete(seeds,{91,requiredEnd-requiredStart,true,true},capturedReceipt);REQUIRE(bool(record));
    struct Context {
        std::unique_ptr<OpenCLProbeInput> input;
        std::vector<std::uint16_t> output;
        struct Work : CaptureProbeReplay::Probe {
            Context& context;std::shared_ptr<const void> owner;bool canceled=false;
            Work(Context& context,std::shared_ptr<const void> keep):context(context),owner(std::move(keep)) {}
            OpenCLProbeProgress advance() noexcept override {return canceled?OpenCLProbeProgress::Declined:OpenCLProbeProgress::Complete;}
            void cancel() noexcept override {canceled=true;}
            std::span<const std::uint16_t> result() const noexcept override {return context.output;}
        };
        static bool ready(const void*) noexcept {return true;}
        static bool reap(const void*) noexcept {return false;}
        static std::unique_ptr<CaptureProbeReplay::Probe> begin(const OpenCLProbeInput& input,Plan,
            std::shared_ptr<const void> owner,const void* value) {
            auto& context=*const_cast<Context*>(static_cast<const Context*>(value));
            context.input=std::make_unique<OpenCLProbeInput>(input);
            return std::make_unique<Work>(context,std::move(owner));
        }
    };
    auto context=std::make_shared<Context>();context->output=seeds;
    struct Cleanup {std::shared_ptr<Context> context;~Cleanup(){context->input.reset();}} cleanup{context};
    CaptureProbeReplay::Receipt attempted;const CaptureProbeReplay::Admission admission{91,1,input.slackNs,false};
    const auto contextBytes=sizeof(Context)+sizeof(Context::Work)+sizeof(OpenCLProbeInput)+512+
        context->output.capacity()*sizeof(std::uint16_t);
    auto replay=CaptureProbeReplay::begin(std::move(record),admission,attempted,
        {timer,&Timer::read,sizeof(Timer)},{context,contextBytes,&Context::ready,&Context::begin,&Context::reap});
    REQUIRE(bool(replay));REQUIRE(bool(context->input));CHECK_FALSE(attempted.nativeBackend);
    auto control=*context->input;control.identity.prechargedOwner.reset();
    const auto reserved=openCLProbeBytes();
    auto duplicate=beginOpenCLProbe(control,Plan::Frozen8,control.identity.owner);REQUIRE(bool(duplicate));
    CHECK(duplicate->advance()==OpenCLProbeProgress::Pending);
    CHECK(duplicate->metrics().stage==OpenCLProbe::Stage::CostsReserve);
    CHECK(duplicate->advance()==OpenCLProbeProgress::Declined);duplicate.reset();
    CHECK(openCLProbeBytes()==reserved);CHECK(readyPlans.load()!=0);control.identity={};
    auto native=beginOpenCLProbe(*context->input,Plan::Frozen8,context->input->identity.owner);REQUIRE(bool(native));
    auto progress=OpenCLProbeProgress::Pending;
    for(unsigned attempt=0;attempt<1000000 && progress==OpenCLProbeProgress::Pending;++attempt){
        progress=native->advance();if(progress==OpenCLProbeProgress::Pending)std::this_thread::yield();
    }
    REQUIRE(progress==OpenCLProbeProgress::Complete);
    CHECK(std::equal(native->result().begin(),native->result().end(),seeds.begin()));
    CHECK(native->metrics().coldCostPlane);CHECK(native->metrics().costCellsPrepared==grid.cells());
    CHECK(native->metrics().dispatches>0);CHECK(native->metrics().invalidCpuClocks==0);
    CHECK(std::equal(originals.begin(),originals.end(),context->input->gradient));
    CHECK(openCLProbeBytes()<=OpenCLProbeBudget);native.reset();CHECK(openCLProbeBytes()==reserved);
    context->input.reset();
    auto replayProgress=CaptureProbeReplay::Progress::Pending;
    for(unsigned attempt=0;attempt<1000 && replayProgress==CaptureProbeReplay::Progress::Pending;++attempt)
        replayProgress=replay->advance(admission);
    REQUIRE(replayProgress==CaptureProbeReplay::Progress::ReadyToSettle);CHECK(replay->receipt().exact);
    auto token=replay->takeSettlement();REQUIRE(bool(token));token->cleanup();replay.reset();token.reset();
    CHECK(openCLProbeBytes()==baseline);CHECK(policy->metrics().promoted==0);
    std::vector<std::uint16_t> large(2048*2048,GRADIENT_UNREACHABLE);large[0]=GRADIENT_AT_GOAL;
    auto oversized=input;oversized.original=large;oversized.water={};oversized.swim=0;
    oversized.key.width=oversized.key.height=2048;oversized.key.movement=0;
    const auto reads=timer->calls;
    CHECK_FALSE(GradientCpuCapture::begin(policy,oversized,capturedReceipt,{timer,&Timer::read,sizeof(Timer)}));
    CHECK(timer->calls==reads);CHECK(large.front()==GRADIENT_AT_GOAL);CHECK(large.back()==GRADIENT_UNREACHABLE);
    CHECK(openCLProbeBytes()==baseline);CHECK(openCLStatus().initializationThreadCPUNs==initializationCpu);
}

TEST_CASE("captured replay schedules bounded CPU work immediately and waits only for unproved events" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();Fixture fixture;CaptureProbeReplay::Receipt receipt;
    fixture.driver->pending=true;
    auto replay=fixture.begin(receipt);REQUIRE(bool(replay));
    CHECK(replay->nextAdvanceDelayNs()==0);
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::Pending);
    CHECK(replay->nextAdvanceDelayNs()==0);
    fixture.driver->awaitingEvent=true;
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::Pending);
    CHECK(replay->nextAdvanceDelayNs()==100000);
    fixture.driver->pending=false;
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::Pending);
    // Comparison is immediate even when the fake adapter retains its old hint.
    CHECK(replay->nextAdvanceDelayNs()==0);
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::ReadyToSettle);
    CHECK(replay->nextAdvanceDelayNs()==0);CHECK(replay->receipt().exact);
    auto token=replay->takeSettlement();REQUIRE(bool(token));
    replay.reset();token.reset();CHECK(openCLProbeBytes()==baseline);
    CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
}

TEST_CASE("immediate replay turns still cancel before work when required backlog appears" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();Fixture fixture;CaptureProbeReplay::Receipt receipt;
    fixture.driver->pending=true;
    auto replay=fixture.begin(receipt);REQUIRE(bool(replay));
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::Pending);
    CHECK(replay->nextAdvanceDelayNs()==0);CHECK(fixture.driver->commands==1);
    auto admission=fixture.admission();admission.requiredBacklog=true;
    CHECK(replay->advance(admission)==CaptureProbeReplay::Progress::Declined);
    CHECK(fixture.driver->commands==1);CHECK(fixture.driver->cancels==1);
    replay.reset();CHECK(openCLProbeBytes()==baseline);
    CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
}

TEST_CASE("captured replay mismatch and required backlog decline without publication" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();
    for(const bool backlog:{false,true}) {
        Fixture fixture;CaptureProbeReplay::Receipt receipt;fixture.driver->output[2]^=1;
        auto replay=fixture.begin(receipt);REQUIRE(bool(replay));
        auto admission=fixture.admission();admission.requiredBacklog=backlog;
        auto progress=replay->advance(admission);
        if(!backlog)progress=replay->advance(admission);
        CHECK(progress==CaptureProbeReplay::Progress::Declined);CHECK_FALSE(replay->receipt().exact);
        CHECK_FALSE(replay->receipt().executionFailed);
        CHECK(fixture.driver->commands==(backlog?0:1));
        if(!backlog){CHECK(replay->receipt().firstMismatch==2);CHECK(replay->receipt().comparedCells==3);}
        replay.reset();CHECK(openCLProbeBytes()==baseline);
        CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
    }
}

TEST_CASE("quarantined captured replay retains global credit and storage through repeated proof failure" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();Fixture fixture;CaptureProbeReplay::Receipt receipt;
    fixture.driver->quarantine=true;auto replay=fixture.begin(receipt);REQUIRE(bool(replay));
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::Quarantined);
    CHECK(replay->nextAdvanceDelayNs()==100000000);
    CHECK_FALSE(replay->takeSettlement());CHECK(bool(fixture.driver->borrowed));
    for(unsigned retry=0;retry<4;++retry)
        CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::Quarantined);
    CHECK(fixture.policy->metrics().probeCpuNs==0);CHECK(openCLProbeBytes()>baseline);
    fixture.driver->terminal=true;
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::Declined);
    auto token=replay->takeSettlement();REQUIRE(bool(token));CHECK_FALSE(fixture.driver->borrowed);
    replay.reset();token.reset();CHECK(openCLProbeBytes()==baseline);
    CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
}

TEST_CASE("invalid captured replay clocks stop new commands and rejected readiness preserves input" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();
    {
        Fixture fixture;CaptureProbeReplay::Receipt receipt;fixture.driver->ready=false;
        auto replay=fixture.begin(receipt);CHECK_FALSE(replay);CHECK(bool(fixture.record));
        CHECK(receipt.setupDeclined);CHECK(fixture.driver->commands==0);CHECK(fixture.policy->metrics().probeCpuNs==0);
        fixture.record.reset();CHECK(openCLProbeBytes()==baseline);
    }
    {
        Fixture fixture;CaptureProbeReplay::Receipt receipt;
        auto replay=fixture.begin(receipt);REQUIRE(bool(replay));fixture.timer->failAt=fixture.timer->calls+1;
        CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::Declined);
        CHECK_FALSE(replay->receipt().clockValid);CHECK(fixture.driver->commands==0);
        replay.reset();CHECK(openCLProbeBytes()==baseline);
        CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
    }
}

TEST_CASE("migrated captured replay never advances or reaps a pending backend" * doctest::test_suite("OpenCLGradient"))
{
    if constexpr(!GAGCore::ThreadSupport::available)return;
    const auto baseline=openCLProbeBytes();Fixture fixture;CaptureProbeReplay::Receipt receipt;
    fixture.driver->quarantine=true;auto replay=fixture.begin(receipt);REQUIRE(bool(replay));
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::Quarantined);
    const auto commands=fixture.driver->commands,cancels=fixture.driver->cancels,polls=fixture.driver->polls;
    std::thread migrated([&]{
        CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::Quarantined);
        CHECK_FALSE(replay->takeSettlement());
    });migrated.join();
    CHECK(fixture.driver->commands==commands);CHECK(fixture.driver->cancels==cancels);CHECK(fixture.driver->polls==polls);
    CHECK_FALSE(replay->receipt().clockValid);CHECK(bool(fixture.driver->borrowed));
    fixture.driver->terminal=true;
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::Declined);
    auto token=replay->takeSettlement();REQUIRE(bool(token));replay.reset();token.reset();
    CHECK(openCLProbeBytes()==baseline);CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
}

TEST_CASE("captured replay distinguishes backend failure from cancellation and malformed output" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();
    for(unsigned scenario=0;scenario<3;++scenario) {
        Fixture fixture;CaptureProbeReplay::Receipt receipt;
        fixture.driver->failed=scenario!=1;
        if(scenario==2){fixture.driver->failed=false;fixture.driver->output.pop_back();}
        auto replay=fixture.begin(receipt);REQUIRE(bool(replay));
        if(scenario==1)replay->cancel();
        auto progress=replay->advance(fixture.admission());
        if(scenario==2)progress=replay->advance(fixture.admission());
        CHECK(progress==CaptureProbeReplay::Progress::Declined);
        CHECK(replay->receipt().executionFailed==(scenario!=1));
        CHECK_FALSE(replay->receipt().exact);
        CHECK(replay->receipt().firstMismatch==SIZE_MAX);
        CHECK(fixture.driver->commands==(scenario==1?0:1));
        auto token=replay->takeSettlement();REQUIRE(bool(token));
        replay.reset();token.reset();CHECK(openCLProbeBytes()==baseline);
        CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
    }
}

TEST_CASE("replay terminal diagnostics freeze reference and distinguish budget required and explicit cancellation" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();
    for(unsigned scenario=0;scenario<3;++scenario) {
        Fixture fixture;CaptureProbeReplay::Receipt attempted;
        fixture.driver->pending=true;fixture.driver->provideMetrics=true;
        auto replay=fixture.begin(attempted);REQUIRE(bool(replay));
        CHECK(attempted.acceptedReferenceCpuNs==1000000);CHECK(attempted.commonPreparationCpuNs==0);
        CHECK(attempted.reservedCpuNs==fixture.input.reservedCpuNs);
        auto admission=fixture.admission();
        if(scenario==0)fixture.timer->step=250000; // Bounded 250 us calls, no overshoot.
        if(scenario==1)admission.requiredBacklog=true;
        if(scenario==2)replay->cancel();
        auto progress=CaptureProbeReplay::Progress::Pending;
        for(unsigned turn=0;turn<32 && progress==CaptureProbeReplay::Progress::Pending;++turn)
            progress=replay->advance(admission);
        REQUIRE(progress==CaptureProbeReplay::Progress::Declined);
        const auto receipt=replay->receipt();
        CHECK(receipt.budgetCanceled==(scenario==0));CHECK(receipt.requiredCanceled==(scenario==1));
        CHECK(receipt.explicitCanceled==(scenario==2));CHECK(receipt.overshoots==0);
        CHECK(receipt.nativeMetricsAvailable);CHECK(receipt.nativeMetrics.stage==OpenCLProbe::Stage::CheckWait);
        CHECK(receipt.nativeMetrics.copiedCells==9);CHECK_FALSE(receipt.nativeBackend);
        CHECK_FALSE(receipt.exact);CHECK(receipt.comparedCells==0);
        auto token=replay->takeSettlement();REQUIRE(bool(token));
        // Metadata survives State/native destruction, independently of storage.
        CHECK(replay->receipt().acceptedReferenceCpuNs==1000000);CHECK(replay->receipt().reservedCpuNs==fixture.input.reservedCpuNs);
        replay.reset();token.reset();CHECK(openCLProbeBytes()==baseline);
        CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
    }
}

TEST_CASE("injected replay cannot attest deployed native candidate coverage" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();Fixture fixture;CaptureProbeReplay::Receipt attempted;
    fixture.driver->provideMetrics=true;
    auto replay=fixture.begin(attempted,15000,true);REQUIRE(bool(replay));
    CHECK(attempted.requiredOnlyAllowanceCpuNs==15000);CHECK(attempted.requiredAllowanceQualified);
    CHECK(attempted.requiredAllowanceCalibration==42);CHECK(attempted.captureCpuLimitNs==fixture.input.captureCpuLimitNs);
    CHECK_FALSE(attempted.nativeClock);CHECK_FALSE(attempted.nativeBackend);
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::Pending);
    CHECK(replay->advance(fixture.admission())==CaptureProbeReplay::Progress::ReadyToSettle);
    CHECK(replay->receipt().exact);CHECK_FALSE(replay->candidateReceipt().complete());
    // Changing later Admission does not rewrite the pre-alternative calibration.
    auto changed=fixture.admission();changed.requiredOnlyAllowanceCpuNs=1;changed.requiredAllowanceQualified=false;
    replay->advance(changed);CHECK(replay->receipt().requiredOnlyAllowanceCpuNs==15000);
    auto token=replay->takeSettlement();REQUIRE(bool(token));CHECK_FALSE(replay->candidateReceipt().complete());
    replay.reset();token.reset();CHECK(openCLProbeBytes()==baseline);
    CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
}

TEST_CASE("isolated real native replay attests exact identity bound candidate coverage" * doctest::test_suite("OpenCLGradient"))
{
    const auto* enabled=std::getenv("GLOB2_TEST_OPENCL_CANDIDATE_RECEIPT");
    if(!enabled || std::strcmp(enabled,"1"))return;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    std::thread coordinator([] {
        REQUIRE(initializeOpenCL());const auto configured=openCLStatus();
        REQUIRE_FALSE(configured.parityBound);REQUIRE_FALSE(configured.directSeedUpload);REQUIRE(configured.apiCpuMode==0);
        REQUIRE((readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))!=0);REQUIRE(glob2::threadCpuNs()!=0);
        const auto baseline=openCLProbeBytes(),initializationCpu=configured.initializationThreadCPUNs;
        auto policy=std::make_shared<CpuSavingPolicy>();
        std::array<std::uint16_t,9> seeds{65535,1,1,0,1,1,1,65300,1};const auto original=seeds;
        GradientCpuCapture::Input input;input.original=seeds;
        input.key={9,1,BUCKETS,3,1,Family::Materials,255,255};input.key.limit=COST_LIMIT;
        input.tick=1;input.due=17;input.requiredGeneration=91;input.alternativeReady=true;
        input.reservedCpuNs=8000000;input.captureCpuLimitNs=2000000;
        input.slackNs=100000000;input.conservativeElapsedNs=1000000;
        REQUIRE(policy->qualify(input.key,Plan::Frozen8,true));
        // Synthetic credits only enable the isolated API gate. All capture,
        // eager reference and native producer clocks are real thread CPU clocks.
        policy->observeAccepted(input.key,Plan::CPU,100000000000ull,1);
        GradientCpuCapture::Receipt captured;std::unique_ptr<GradientCpuCapture> capture;
        for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !capture;++i)
            capture=GradientCpuCapture::begin(policy,input,captured);
        REQUIRE(bool(capture));GradientWorkspace workspace;
        const auto start=glob2::threadCpuNs();
        propagateFieldCPU(seeds.data(),0,COST_LIMIT,field::Grid(9,1),workspace,[](std::size_t){return false;});
        const auto end=glob2::threadCpuNs();REQUIRE(start!=0);REQUIRE(end>=start);REQUIRE(end-start>0);
        auto record=capture->complete(seeds,{91,end-start,true,true},captured);REQUIRE(bool(record));
        CHECK(std::equal(record->original().begin(),record->original().end(),original.begin()));
        CaptureProbeReplay::Admission admission{91,1,input.slackNs,false};
        // This explicit fixture calibration identifies the API receipt only;
        // no complete process envelope authorizes performance inference here.
        admission.requiredOnlyAllowanceCpuNs=15000;admission.requiredAllowanceQualified=true;
        admission.requiredAllowanceCalibration=0x43505254455354ull;
        CaptureProbeReplay::Receipt attempted;
        auto replay=CaptureProbeReplay::begin(std::move(record),admission,attempted);REQUIRE(bool(replay));
        CHECK(attempted.nativeClock);
        auto progress=CaptureProbeReplay::Progress::Pending;
        for(unsigned i=0;i<1000000 && progress==CaptureProbeReplay::Progress::Pending;++i) {
            progress=replay->advance(admission);
            if(progress==CaptureProbeReplay::Progress::Pending)std::this_thread::yield();
        }
        REQUIRE(progress==CaptureProbeReplay::Progress::ReadyToSettle);
        const auto receipt=replay->receipt();CHECK(receipt.exact);CHECK(receipt.nativeBackend);CHECK(receipt.nativeMetricsAvailable);
        CHECK(receipt.nativeMetrics.dispatches>0);CHECK(receipt.clockValid);CHECK(receipt.nativeMetrics.invalidCpuClocks==0);
        CHECK(receipt.overshoots==0);CHECK(receipt.comparedCells==seeds.size());
        const auto candidate=replay->candidateReceipt();CHECK(candidate.complete());
        CHECK(candidate.nativeCallingCpuNs()>=receipt.nativeMetrics.threadCpuNs);
        CHECK(candidate.requiredAllowanceCpuNs()==15000);CHECK(candidate.calibration()==admission.requiredAllowanceCalibration);
        CHECK(receipt.acceptedReferenceCpuNs==end-start);CHECK(receipt.commonPreparationCpuNs==0);
        auto token=replay->takeSettlement();REQUIRE(bool(token));CHECK(replay->candidateReceipt().complete());
        token->cleanup();replay.reset();CHECK_FALSE(token->settle({},{}));token.reset();
        CHECK(policy->metrics().probeCpuNs>=input.reservedCpuNs);CHECK(policy->metrics().promoted==0);
        CHECK(openCLProbeBytes()==baseline);CHECK(openCLStatus().initializationThreadCPUNs==initializationCpu);
    });coordinator.join();
}
