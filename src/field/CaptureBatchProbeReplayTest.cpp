// SPDX-License-Identifier: GPL-3.0-or-later
#include "CaptureBatchProbeReplay.h"
#include "GradientPropagation.h"
#include "Glob2Test.h"
#include "ThreadSupport.h"
#include <vector>

namespace
{
using namespace gradient_kernel;
struct Timer {
    std::uint64_t value=100,calls=0,step=100,failAt=0;
    static std::uint64_t read(const void* owner) {
        auto& timer=*const_cast<Timer*>(static_cast<const Timer*>(owner));
        if(++timer.calls==timer.failAt)return 0;return timer.value+=timer.step;
    }
};
struct Driver {
    std::vector<std::vector<std::uint16_t>> original,output;
    std::shared_ptr<const void> borrowed;
    CostIdentity retainedCost;
    unsigned commands=0,cancels=0,polls=0,count=0;
    bool ready=true,failed=false,quarantine=false,terminal=false,pending=false,waiting=false,failSetup=false,retainCost=false;
    struct Work final : CaptureBatchProbeReplay::Probe {
        Driver& driver;std::shared_ptr<const void> owner;bool canceled=false,quarantined=false;
        Work(Driver& value,std::shared_ptr<const void> keep):driver(value),owner(std::move(keep)) {}
        ~Work(){if(quarantined)driver.borrowed=std::move(owner);}
        OpenCLProbeProgress advance() noexcept override {
            if(canceled)return OpenCLProbeProgress::Declined;
            ++driver.commands;
            if(driver.failed)return OpenCLProbeProgress::Declined;
            if(driver.quarantine){quarantined=true;return OpenCLProbeProgress::Quarantined;}
            return driver.pending?OpenCLProbeProgress::Pending:OpenCLProbeProgress::Complete;
        }
        void cancel() noexcept override {canceled=true;++driver.cancels;}
        bool executionFailed() const noexcept override {return driver.failed && !canceled;}
        bool waitingForEvent() const noexcept override {return driver.waiting;}
        bool metrics(OpenCLProbe::Metrics& out) const noexcept override {
            // Deliberately plausible fake coverage can never attest deployment.
            out.stage=OpenCLProbe::Stage::OutputWait;out.batchFields=driver.count;
            out.retiredFields=driver.count;out.dispatches=driver.commands;out.hostChecks=1;
            out.threadCpuNs=100;return true;
        }
        std::span<const std::uint16_t> result(unsigned i) const noexcept override {
            return i<driver.output.size()?std::span<const std::uint16_t>(driver.output[i]):std::span<const std::uint16_t>{};
        }
    };
    static bool isReady(const void* context) noexcept {return static_cast<const Driver*>(context)->ready;}
    static bool reap(const void* context) noexcept {
        auto& d=*const_cast<Driver*>(static_cast<const Driver*>(context));++d.polls;
        if(!d.terminal)return false;d.borrowed.reset();return true;
    }
    static std::unique_ptr<CaptureBatchProbeReplay::Probe> begin(std::span<const OpenCLProbeInput> inputs,Plan,
        std::shared_ptr<const void> owner,const void* context) {
        auto& d=*const_cast<Driver*>(static_cast<const Driver*>(context));d.count=unsigned(inputs.size());
        CHECK(inputs.size()==d.original.size());
        for(unsigned i=0;i<inputs.size();++i) {
            CHECK(std::equal(d.original[i].begin(),d.original[i].end(),inputs[i].gradient));
            CHECK(inputs[i].capturedPrivateCosts);CHECK(inputs[i].identity.allCells);
            CHECK(retainedCostOwnerBytes(inputs[i].identity)==0);
            const auto steps=inputs[i].costAt(inputs[i].context,0);
            CHECK(steps.cardinal==LAND_STEPS.cardinal);CHECK(steps.diagonal==LAND_STEPS.diagonal);
            CHECK(inputs[i].identity.owner==inputs[0].identity.owner);
        }
        if(d.retainCost)d.retainedCost=inputs[0].identity;
        if(d.failSetup)return {};
        return std::make_unique<Work>(d,std::move(owner));
    }
};
struct Fixture {
    std::shared_ptr<CpuSavingPolicy> policy=std::make_shared<CpuSavingPolicy>();
    std::shared_ptr<Timer> timer=std::make_shared<Timer>();
    std::shared_ptr<Driver> driver=std::make_shared<Driver>();
    std::shared_ptr<int> source=std::make_shared<int>(1);
    GradientCpuCaptureGroup::Input input;
    GradientCpuCaptureGroup::Receipt captured;
    std::shared_ptr<GradientCpuCaptureGroup::Record> record;
    std::unique_ptr<GradientCpuCaptureGroup::Member> late;
    std::vector<std::uint16_t> seeds;
    Fixture(unsigned width=9,unsigned height=1,bool withLate=false):seeds(std::size_t(width)*height,1) {
        seeds[0]=65535;if(seeds.size()>3)seeds[3]=0;if(seeds.size()>7)seeds[7]=65300;
        input.original=seeds;input.key={width,height,BUCKETS,3,1,Family::Clear,255,255};input.key.limit=COST_LIMIT;
        input.scalarClassic=true;input.sourceIdentity=source;input.weakSourceBytes=sizeof(int)+256;
        input.revision=21;input.variant=2;input.tick=1;input.due=17;input.optionalExpiry=101;
        input.requiredGeneration=91;input.maximumMembers=4;input.alternativeReady=true;
        input.reservedCpuNs=8000000;input.captureCpuLimitNs=4000000;
        input.slackNs=100000000;input.conservativeElapsedNs=1000000;
        for(unsigned n:{1u,2u,4u}){auto key=input.key;key.batch=n;policy->qualify(key,input.alternative,true);}
        policy->observeAccepted(input.key,Plan::CPU,1000000000000ull,1);
        for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !record;++i)
            record=GradientCpuCaptureGroup::begin(policy,input,captured,{timer,&Timer::read,sizeof(Timer)+256});
        REQUIRE(bool(record));
        for(unsigned i=0;i<2;++i) {
            auto original=seeds;original.back()=65000+i;input.original=original;input.due=17+i;
            auto member=GradientCpuCaptureGroup::enroll(record,input,captured);REQUIRE(bool(member));
            auto expected=original;GradientWorkspace workspace;
            propagateFieldCPU(expected.data(),0,COST_LIMIT,field::Grid(width,height),workspace,[](std::size_t){return false;});
            REQUIRE(member->complete(expected,{91,1000000+i,true,true},captured));member.reset();
            driver->original.push_back(std::move(original));driver->output.push_back(std::move(expected));
        }
        input.original=seeds;
        if(withLate){late=GradientCpuCaptureGroup::enroll(record,input,captured);REQUIRE(bool(late));}
        REQUIRE(record->freezeReady(91));CHECK(record->count()==2);
    }
    CaptureBatchProbeReplay::Admission admission() const {return {91,1,input.slackNs,false};}
    std::unique_ptr<CaptureBatchProbeReplay> begin(CaptureBatchProbeReplay::Receipt& out) {
        std::size_t bytes=sizeof(Driver)+sizeof(Driver::Work)+512;
        bytes+=(driver->original.capacity()+driver->output.capacity())*sizeof(std::vector<std::uint16_t>);
        for(const auto& field:driver->original)bytes+=field.capacity()*sizeof(std::uint16_t);
        for(const auto& field:driver->output)bytes+=field.capacity()*sizeof(std::uint16_t);
        return CaptureBatchProbeReplay::begin(std::move(record),admission(),out,{timer,&Timer::read,sizeof(Timer)},
            {driver,bytes,&Driver::isReady,&Driver::begin,&Driver::reap});
    }
};
}

TEST_CASE("captured batch replay compares bounded indexed outputs and retains one settlement" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();Fixture fixture(257,33);CaptureBatchProbeReplay::Receipt attempted;
    CHECK_FALSE(CaptureBatchProbeReplay::accountingStorage(fixture.record));
    auto replay=fixture.begin(attempted);REQUIRE(bool(replay));CHECK_FALSE(fixture.record);
    REQUIRE(bool(replay->authorization()));CHECK(replay->authorization()->acceptedReferenceCpuNs==2000001);
    CHECK(replay->key().batch==2);CHECK(attempted.captureCpuLimitNs==4000000);
    CHECK(replay->advance(fixture.admission())==CaptureBatchProbeReplay::Progress::Pending);
    CHECK(replay->nextAdvanceDelayNs()==0);CHECK(fixture.driver->commands==1);
    std::uint64_t compared=0;auto progress=CaptureBatchProbeReplay::Progress::Pending;
    for(unsigned i=0;i<4 && progress==CaptureBatchProbeReplay::Progress::Pending;++i) {
        progress=replay->advance(fixture.admission());const auto current=replay->receipt().comparedCells;
        CHECK(current-compared<=GradientCpuCapture::CopyBlockBytes/sizeof(std::uint16_t));compared=current;
    }
    REQUIRE(progress==CaptureBatchProbeReplay::Progress::ReadyToSettle);CHECK(replay->receipt().exact);
    CHECK(compared==2*fixture.seeds.size());CHECK_FALSE(replay->candidateReceipt().complete());
    auto token=replay->takeSettlement();REQUIRE(bool(token));CHECK_FALSE(replay->takeSettlement());
    token->cleanup();replay.reset();CHECK(openCLProbeBytes()>baseline);
    CHECK_FALSE(token->settle({},{}));token.reset();CHECK(openCLProbeBytes()==baseline);
    CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);CHECK(fixture.policy->metrics().promoted==0);
}

TEST_CASE("batch replay distinguishes indexed mismatch malformed output and driver failure" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();
    for(unsigned scenario=0;scenario<3;++scenario) {
        Fixture fixture;CaptureBatchProbeReplay::Receipt attempted;
        if(scenario==0)fixture.driver->output[1][4]^=1;
        if(scenario==1)fixture.driver->output[1].pop_back();
        if(scenario==2)fixture.driver->failed=true;
        auto replay=fixture.begin(attempted);REQUIRE(bool(replay));
        auto progress=replay->advance(fixture.admission());
        if(progress==CaptureBatchProbeReplay::Progress::Pending)progress=replay->advance(fixture.admission());
        CHECK(progress==CaptureBatchProbeReplay::Progress::Declined);CHECK_FALSE(replay->receipt().exact);
        CHECK(replay->receipt().executionFailed==(scenario!=0));
        CHECK(replay->receipt().firstMismatch==(scenario==0?fixture.seeds.size()+4:SIZE_MAX));
        auto token=replay->takeSettlement();REQUIRE(bool(token));replay.reset();token.reset();CHECK(openCLProbeBytes()==baseline);
    }
}

TEST_CASE("batch setup decline keeps late member aliases until coordinator settlement" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();Fixture fixture(9,1,true);CaptureBatchProbeReplay::Receipt attempted;
    fixture.driver->failSetup=true;auto replay=fixture.begin(attempted);REQUIRE(bool(replay));CHECK(attempted.setupDeclined);
    CHECK(replay->advance(fixture.admission())==CaptureBatchProbeReplay::Progress::Declined);
    CHECK_FALSE(replay->takeSettlement());CHECK(fixture.policy->metrics().probeCpuNs==0);CHECK(openCLProbeBytes()>baseline);
    fixture.late.reset();CHECK(replay->advance(fixture.admission())==CaptureBatchProbeReplay::Progress::Declined);
    auto token=replay->takeSettlement();REQUIRE(bool(token));replay.reset();token.reset();
    CHECK(openCLProbeBytes()==baseline);CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
}

TEST_CASE("batch quarantine and escaped plane alias pin complete credits until proof and alias release" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();Fixture fixture;CaptureBatchProbeReplay::Receipt attempted;
    fixture.driver->quarantine=true;fixture.driver->retainCost=true;auto replay=fixture.begin(attempted);REQUIRE(bool(replay));
    CHECK(replay->advance(fixture.admission())==CaptureBatchProbeReplay::Progress::Quarantined);
    const auto commands=fixture.driver->commands;
    for(unsigned i=0;i<3;++i){CHECK(replay->advance(fixture.admission())==CaptureBatchProbeReplay::Progress::Quarantined);CHECK_FALSE(replay->takeSettlement());}
    CHECK(fixture.driver->commands==commands);CHECK(fixture.policy->metrics().probeCpuNs==0);
    fixture.driver->terminal=true;
    CHECK(replay->advance(fixture.admission())==CaptureBatchProbeReplay::Progress::Quarantined);
    CHECK_FALSE(replay->takeSettlement());CHECK(openCLProbeBytes()>baseline);
    fixture.driver->retainedCost={};
    CHECK(replay->advance(fixture.admission())==CaptureBatchProbeReplay::Progress::Declined);
    auto token=replay->takeSettlement();REQUIRE(bool(token));replay.reset();token.reset();CHECK(openCLProbeBytes()==baseline);
}

TEST_CASE("batch replay invalid capture generation and migration prevent further native work" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();
    for(unsigned scenario=0;scenario<3;++scenario) {
        Fixture fixture(9,1,scenario==2);CaptureBatchProbeReplay::Receipt attempted;fixture.driver->pending=true;
        auto replay=fixture.begin(attempted);REQUIRE(bool(replay));
        CHECK(replay->advance(fixture.admission())==CaptureBatchProbeReplay::Progress::Pending);
        const auto commands=fixture.driver->commands;
        auto admission=fixture.admission();
        if(scenario==0)admission.requiredGeneration=92;
        if(scenario==1)admission.requiredBacklog=true;
        if(scenario==2) {
            CHECK_FALSE(fixture.late->complete({}, {91,0,false,false},fixture.captured));
            fixture.late.reset(); // Invalid required completion invalidates the group.
        }
        CHECK(replay->advance(admission)==CaptureBatchProbeReplay::Progress::Declined);
        CHECK(fixture.driver->commands==commands);CHECK(replay->receipt().requiredCanceled);
        auto token=replay->takeSettlement();REQUIRE(bool(token));replay.reset();token.reset();CHECK(openCLProbeBytes()==baseline);
    }
    if constexpr(GAGCore::ThreadSupport::available) {
        Fixture fixture;CaptureBatchProbeReplay::Receipt attempted;fixture.driver->quarantine=true;
        auto replay=fixture.begin(attempted);REQUIRE(bool(replay));
        CHECK(replay->advance(fixture.admission())==CaptureBatchProbeReplay::Progress::Quarantined);
        const auto calls=fixture.driver->commands+fixture.driver->polls+fixture.driver->cancels;
        std::thread migrated([&]{CHECK(replay->advance(fixture.admission())==CaptureBatchProbeReplay::Progress::Quarantined);CHECK_FALSE(replay->takeSettlement());});migrated.join();
        CHECK(fixture.driver->commands+fixture.driver->polls+fixture.driver->cancels==calls);CHECK_FALSE(replay->receipt().clockValid);
        fixture.driver->terminal=true;CHECK(replay->advance(fixture.admission())==CaptureBatchProbeReplay::Progress::Declined);
        auto token=replay->takeSettlement();REQUIRE(bool(token));replay.reset();token.reset();CHECK(openCLProbeBytes()==baseline);
    }
}

TEST_CASE("late invalid capture cannot regain native coverage after terminal extraction" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();Fixture fixture(9,1,true);CaptureBatchProbeReplay::Receipt attempted;
    auto replay=fixture.begin(attempted);REQUIRE(bool(replay));
    CHECK(replay->advance(fixture.admission())==CaptureBatchProbeReplay::Progress::Pending);
    REQUIRE(replay->advance(fixture.admission())==CaptureBatchProbeReplay::Progress::ReadyToSettle);
    CHECK_FALSE(replay->takeSettlement());
    CHECK_FALSE(fixture.late->complete({}, {91,0,false,false},fixture.captured));fixture.late.reset();
    // Extract directly, without another advance that might mask the boundary.
    auto token=replay->takeSettlement();REQUIRE(bool(token));
    CHECK_FALSE(replay->receipt().clockValid);CHECK(replay->receipt().requiredCanceled);
    CHECK_FALSE(replay->candidateReceipt().complete());replay.reset();token.reset();
    CHECK(openCLProbeBytes()==baseline);CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
}

TEST_CASE("native batch replay rejects missing future storage before consuming ownership" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();Fixture fixture;CaptureBatchProbeReplay::Receipt attempted;
    const auto bytes=openCLProbeBytes();
    auto replay=CaptureBatchProbeReplay::begin(std::move(fixture.record),fixture.admission(),attempted);
    CHECK_FALSE(replay);CHECK(attempted.setupDeclined);CHECK(bool(fixture.record));CHECK(openCLProbeBytes()==bytes);
    CHECK(attempted.setup.reason==CaptureProbeReplay::SetupDecline::MissingFutureStorage);
    CHECK(attempted.setup.count==2);CHECK(attempted.setup.referenceBound);CHECK(attempted.setup.captureValid);
    CHECK(attempted.setup.closed);CHECK(attempted.setup.due==fixture.record->due());CHECK(attempted.invalidClocks==0);
    auto token=GradientCpuCaptureGroup::takeSettlement(fixture.record);REQUIRE(bool(token));token.reset();CHECK(openCLProbeBytes()==baseline);
    {
        Fixture invalidClock;invalidClock.timer->failAt=invalidClock.timer->calls+1;
        auto declined=invalidClock.begin(attempted);CHECK_FALSE(declined);REQUIRE(bool(invalidClock.record));
        CHECK(attempted.setup.reason==CaptureProbeReplay::SetupDecline::ClockRead);
        CHECK_FALSE(attempted.clockValid);CHECK(attempted.invalidClocks==1);CHECK(invalidClock.driver->commands==0);
    }
    for(unsigned cause=0;cause<4;++cause) {
        Fixture rejected;auto admission=rejected.admission();
        if(cause==0)admission.requiredGeneration++;
        else if(cause==1)admission.requiredBacklog=true;
        else if(cause==2)admission.slackNs=0;
        else admission.tick=rejected.record->due();
        const auto reads=rejected.timer->calls;
        auto declined=CaptureBatchProbeReplay::begin(std::move(rejected.record),admission,attempted);
        CHECK_FALSE(declined);REQUIRE(bool(rejected.record));CHECK(rejected.driver->commands==0);
        const auto expected=cause==0?CaptureProbeReplay::SetupDecline::Generation:
            cause==1?CaptureProbeReplay::SetupDecline::RequiredBacklog:
            cause==2?CaptureProbeReplay::SetupDecline::NoSlack:CaptureProbeReplay::SetupDecline::Due;
        CHECK(attempted.setup.reason==expected);CHECK(attempted.setup.tick==admission.tick);
        CHECK(attempted.setup.slackNs==admission.slackNs);CHECK(attempted.setup.due==rejected.record->due());
        CHECK(attempted.setup.admissionGeneration==admission.requiredGeneration);
        CHECK(attempted.setup.capturedGeneration==rejected.record->requiredGeneration());
        CHECK(attempted.invalidClocks==0);CHECK(rejected.timer->calls==reads);
    }
}

TEST_CASE("isolated captured batch replay uses prepaid native arrays and exact eager references" * doctest::test_suite("OpenCLGradient"))
{
    const auto* enabled=std::getenv("GLOB2_TEST_OPENCL_BATCH_REPLAY");
    if(!enabled || std::strcmp(enabled,"1"))return;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    std::thread coordinator([] {
        REQUIRE(initializeOpenCL());const auto configured=openCLStatus();
        REQUIRE_FALSE(configured.parityBound);REQUIRE_FALSE(configured.directSeedUpload);REQUIRE(configured.apiCpuMode==0);
        REQUIRE(glob2::threadCpuNs()!=0);
        const auto baseline=openCLProbeBytes(),initializationCpu=configured.initializationThreadCPUNs;
        for(unsigned plan=1;plan<=6;++plan) {
            REQUIRE((readyPlans.load()&(1u<<plan))!=0);
            for(unsigned count:{1u,2u,4u,8u}) {
                auto timer=std::make_shared<Timer>();auto policy=std::make_shared<CpuSavingPolicy>();
                auto source=std::make_shared<int>(1);
                std::vector<std::uint16_t> seeds(63,1);seeds[0]=65535;seeds[3]=0;seeds[19]=65300;
                GradientCpuCaptureGroup::Input input;input.original=seeds;
                input.key={9,7,BUCKETS,3,1,Family::Clear,255,255};input.key.limit=COST_LIMIT;
                input.scalarClassic=true;input.sourceIdentity=source;input.weakSourceBytes=sizeof(int)+256;
                input.revision=21;input.variant=2;input.tick=1;input.due=17;input.optionalExpiry=101;
                input.requiredGeneration=91;input.maximumMembers=count==1?2:count;input.alternativeReady=true;input.alternative=Plan(plan);
                input.reservedCpuNs=64000000;input.captureCpuLimitNs=32000000;
                input.slackNs=10000000000ull;input.conservativeElapsedNs=1000000;
                auto key=input.key;key.batch=count;REQUIRE(policy->qualify(key,input.alternative,true));
                if(count==1) {
                    // Admit room for two, but freeze the one already completed
                    // field immediately rather than waiting for a second offer.
                    auto capacityKey=key;capacityKey.batch=2;REQUIRE(policy->qualify(capacityKey,input.alternative,true));
                }
                // Synthetic funding/packing clocks isolate admission and exactness;
                // eager reference and actual backend producer clocks stay real.
                policy->observeAccepted(input.key,Plan::CPU,1000000000000ull,1);
                GradientCpuCaptureGroup::Receipt captured;
                std::shared_ptr<GradientCpuCaptureGroup::Record> record;
                for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !record;++i)
                    record=GradientCpuCaptureGroup::beginNative(policy,input,captured,{timer,&Timer::read,sizeof(Timer)+256});
                REQUIRE(bool(record));REQUIRE(bool(CaptureBatchProbeReplay::accountingStorage(record)));
                std::uint64_t sum=0;
                for(unsigned i=0;i<count;++i) {
                    auto original=seeds;original.back()=65000+i;input.original=original;input.due=17+i;
                    auto member=GradientCpuCaptureGroup::enroll(record,input,captured);REQUIRE(bool(member));
                    auto expected=original;GradientWorkspace workspace;const auto start=glob2::threadCpuNs();
                    propagateFieldCPU(expected.data(),0,COST_LIMIT,field::Grid(9,7),workspace,[](std::size_t){return false;});
                    const auto end=glob2::threadCpuNs();REQUIRE(start!=0);REQUIRE(end>start);sum+=end-start;
                    REQUIRE(member->complete(expected,{91,end-start,true,true},captured));member.reset();
                }
                REQUIRE(record->freezeReady(91));CHECK(record->count()==count);
                CaptureBatchProbeReplay::Receipt attempted;const CaptureBatchProbeReplay::Admission admission{91,1,input.slackNs,false};
                auto replay=CaptureBatchProbeReplay::begin(std::move(record),admission,attempted,{timer,&Timer::read,sizeof(Timer)},{});
                REQUIRE(bool(replay));CHECK(attempted.nativeBackend);CHECK_FALSE(attempted.nativeClock);
                auto progress=CaptureBatchProbeReplay::Progress::Pending;
                for(unsigned i=0;i<1000000 && progress==CaptureBatchProbeReplay::Progress::Pending;++i) {
                    progress=replay->advance(admission);if(progress==CaptureBatchProbeReplay::Progress::Pending)std::this_thread::yield();
                }
                REQUIRE(progress==CaptureBatchProbeReplay::Progress::ReadyToSettle);
                const auto receipt=replay->receipt();CHECK(receipt.exact);CHECK(receipt.acceptedReferenceCpuNs==sum);
                CHECK(receipt.nativeMetricsAvailable);CHECK(receipt.nativeMetrics.batchFields==count);
                CHECK(receipt.nativeMetrics.retiredFields==count);CHECK(receipt.nativeMetrics.dispatches>0);
                CHECK(receipt.comparedCells==count*seeds.size());CHECK(receipt.firstMismatch==SIZE_MAX);
                CHECK_FALSE(replay->candidateReceipt().complete()); // Injected adapter clock is not inference evidence.
                auto token=replay->takeSettlement();REQUIRE(bool(token));token->cleanup();replay.reset();
                CHECK_FALSE(token->settle({},{}));token.reset();CHECK(openCLProbeBytes()==baseline);
                CHECK(policy->metrics().probeCpuNs>=input.reservedCpuNs);CHECK(policy->metrics().promoted==0);
            }
        }
        CHECK(openCLStatus().initializationThreadCPUNs==initializationCpu);
    });coordinator.join();
}
