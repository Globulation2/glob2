// SPDX-License-Identifier: GPL-3.0-or-later
#include "CaptureProbeReplay.h"
#include "GradientPropagation.h"
#include "Glob2Test.h"
#include <vector>

namespace
{
using namespace gradient_kernel;
struct Timer {
    std::uint64_t value=100,calls=0,failAt=0;
    static std::uint64_t read(const void* context) {
        auto& timer=*const_cast<Timer*>(static_cast<const Timer*>(context));
        ++timer.calls;if(timer.calls==timer.failAt)return 0;
        return timer.value+=100;
    }
};
struct Driver {
    std::vector<std::uint16_t> original,output;
    std::shared_ptr<const void> borrowed;
    unsigned commands=0,polls=0,cancels=0,costCalls=0;
    bool ready=true,quarantine=false,terminal=false;
    struct Work final : CaptureProbeReplay::Probe {
        Driver& driver;std::shared_ptr<const void> owner;bool canceled=false,quarantined=false;
        Work(Driver& value,std::shared_ptr<const void> keep):driver(value),owner(std::move(keep)) {}
        ~Work(){if(quarantined)driver.borrowed=std::move(owner);}
        OpenCLProbeProgress advance() noexcept override {
            if(canceled)return OpenCLProbeProgress::Declined;
            ++driver.commands;
            if(driver.quarantine){quarantined=true;return OpenCLProbeProgress::Quarantined;}
            return OpenCLProbeProgress::Complete;
        }
        void cancel() noexcept override {canceled=true;++driver.cancels;}
        std::span<const std::uint16_t> result() const noexcept override {return driver.output;}
    };
    static bool isReady(const void* value) noexcept {return static_cast<const Driver*>(value)->ready;}
    static std::unique_ptr<CaptureProbeReplay::Probe> begin(const OpenCLProbeInput& input,Plan,
        std::shared_ptr<const void> owner,const void* value) {
        auto& driver=*const_cast<Driver*>(static_cast<const Driver*>(value));
        CHECK(std::equal(driver.original.begin(),driver.original.end(),input.gradient));
        CHECK(input.identity.allCells);CHECK(input.identity.retainedBytes>0);
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
    std::unique_ptr<CaptureProbeReplay> begin(CaptureProbeReplay::Receipt& receipt) {
        const auto bytes=sizeof(Driver)+sizeof(Driver::Work)+256+
            (driver->original.capacity()+driver->output.capacity())*sizeof(std::uint16_t);
        return CaptureProbeReplay::begin(std::move(record),admission(),receipt,
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
