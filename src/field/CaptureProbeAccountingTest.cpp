// SPDX-License-Identifier: GPL-3.0-or-later
#include "CaptureProbeAccounting.h"
#include "Glob2Test.h"
#include <vector>

namespace
{
using namespace gradient_kernel;
struct Clocks {
    std::uint64_t processCpu=100,captureCpu=100,tid=1;
    std::array<std::uint64_t,glob2::CpuClockRegistry::MaxThreads+1> cpu{};
    unsigned threadReads=0;bool processValid=true;
    glob2::CpuClockReader reader() {
        return {this,[](void* p) noexcept {auto& f=*static_cast<Clocks*>(p);return glob2::CpuClockSample{f.processCpu,f.processValid};},
            [](void* p,std::int64_t id) noexcept {auto& f=*static_cast<Clocks*>(p);++f.threadReads;return glob2::CpuClockSample{f.cpu[id],true};},
            [](void* p) noexcept {auto& f=*static_cast<Clocks*>(p);return glob2::CpuThreadClock{f.tid,1,std::int64_t(f.tid),true};},
            [](void*) noexcept {return glob2::CpuClockSample{1,true};},1};
    }
    static std::uint64_t capture(const void* p) {return const_cast<Clocks*>(static_cast<const Clocks*>(p))->captureCpu+=100;}
};
struct RegistryOwner {
    Clocks clocks;
    glob2::CpuClockRegistry registry{clocks.reader()};
};
struct Fixture {
    std::shared_ptr<CpuSavingPolicy> policy=std::make_shared<CpuSavingPolicy>();
    std::shared_ptr<Clocks> clock=std::make_shared<Clocks>();
    std::shared_ptr<RegistryOwner> owner=std::make_shared<RegistryOwner>();
    std::shared_ptr<glob2::CpuClockRegistry> registry{owner,&owner->registry};
    GradientCpuCapture::Input input;
    std::optional<GradientCpuCapture::Settlement> token;
    Fixture() {
        const std::array<std::uint16_t,2> seeds{65535,1};input.original=seeds;
        input.key={2,1,BUCKETS,3,1,Family::Materials,255,255};input.key.limit=COST_LIMIT;
        input.tick=1;input.due=17;input.requiredGeneration=91;input.alternativeReady=true;
        input.reservedCpuNs=5000000;input.captureCpuLimitNs=2000000;input.slackNs=1000000000;
        input.conservativeElapsedNs=1000000;
        policy->qualify(input.key,input.alternative,true);policy->observeAccepted(input.key,Plan::CPU,100000000000ull,1);
        GradientCpuCapture::Receipt receipt;std::unique_ptr<GradientCpuCapture> capture;
        for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !capture;++i)
            capture=GradientCpuCapture::begin(policy,input,receipt,{clock,&Clocks::capture,sizeof(Clocks)});
        REQUIRE(bool(capture));auto record=capture->complete(seeds,{91,1000000,true,true},receipt);REQUIRE(bool(record));
        REQUIRE(record->adoptProducer());REQUIRE(bool(record->authorize(91,1,input.slackNs,false)));
        auto extracted=GradientCpuCapture::takeSettlement(record);REQUIRE(bool(extracted));
        token.emplace(std::move(*extracted));
        input.original={}; // No borrowed stack view survives construction.
    }
    std::unique_ptr<CaptureProbeAccounting> begin(bool qualified=false) {
        CaptureProbeAccounting::Receipt receipt;
        return CaptureProbeAccounting::begin(registry,token->receipt().optionalCpuNs,91,token->probeTicket(),qualified,receipt,sizeof(RegistryOwner)+256);
    }
};
void running(CaptureProbeAccounting& accounting) {
    for(unsigned i=0;i<4;++i)accounting.advanceEnvelope();REQUIRE(accounting.envelopeReady());
}
void tails(CaptureProbeAccounting& accounting,GradientCpuCapture::Settlement& token,std::uint64_t start=100) {
    REQUIRE(accounting.markDeviceTerminal(start));token.cleanup();
    if(!accounting.markCleanup(token,true,start)){CHECK(accounting.failed());return;}
    CHECK(accounting.nextTailWakeNs()==start+CaptureProbeAccounting::TailIntervalNs);
    CHECK_FALSE(accounting.advanceTail(start+CaptureProbeAccounting::TailIntervalNs-1));
    CHECK_FALSE(accounting.advanceTail(start+CaptureProbeAccounting::TailIntervalNs));
    CHECK(accounting.nextTailWakeNs()==start+2*CaptureProbeAccounting::TailIntervalNs);
    CHECK_FALSE(accounting.advanceTail(start+2*CaptureProbeAccounting::TailIntervalNs-1));
    accounting.advanceTail(start+2*CaptureProbeAccounting::TailIntervalNs);
    CHECK(accounting.nextTailWakeNs()==0);
    for(unsigned i=0;i<3;++i)accounting.advanceTail(start+2*CaptureProbeAccounting::TailIntervalNs);
}
}

TEST_CASE("capture accounting charges unknown process CPU and waits two separate cleanup tails" * doctest::test_suite("OpenCLGradient"))
{
    if(!glob2::threadCpuNs())return;
    const auto baseline=openCLProbeBytes();Fixture f;
    auto known=f.registry->registerCurrent(glob2::CpuThreadRole::Worker);REQUIRE(bool(known));
    f.owner->clocks.cpu[1]=50;auto accounting=f.begin();REQUIRE(bool(accounting));running(*accounting);
    accounting->accountStep(100,300);f.owner->clocks.cpu[1]=450;f.owner->clocks.processCpu=1100;
    tails(*accounting,*f.token);REQUIRE(accounting->tailFinished());CHECK_FALSE(accounting->failed());
    auto result=accounting->finishResult(*f.token,91,true,true,true,500000001,f.input.slackNs);
    CHECK(result.receipt.processBeginNs==100);CHECK(result.receipt.processEndNs==1100);
    CHECK(result.receipt.cleanupProcessCpuNs==1100);CHECK(result.receipt.firstTailProcessCpuNs==1100);CHECK(result.receipt.secondTailProcessCpuNs==1100);
    CHECK(result.receipt.processCpuNs==1000);CHECK(result.receipt.knownInnerCpuNs==400);CHECK(result.receipt.unknownUpperCpuNs==600);
    CHECK(result.receipt.totalUpperCpuNs>=f.token->receipt().optionalCpuNs+600);
    CHECK(result.receipt.secondTailNs-result.receipt.firstTailNs==CaptureProbeAccounting::TailIntervalNs);
    CHECK_FALSE(result.authorization.permitsInference());CHECK_FALSE(result.outcome.clockValid);
    CHECK(f.policy->metrics().probeCpuNs==0);CHECK(openCLProbeBytes()>baseline);
    result=CaptureProbeAccounting::close(accounting,result);CHECK_FALSE(accounting);
    CHECK_FALSE(f.token->settle(result.outcome,result.authorization));f.token.reset();
    CHECK(openCLProbeBytes()==baseline);CHECK(f.policy->metrics().probeCpuNs>=f.input.reservedCpuNs);CHECK(f.policy->metrics().promoted==0);
}

TEST_CASE("capture accounting bounds clock scans and rejects churn or failed process clocks" * doctest::test_suite("OpenCLGradient"))
{
    for(unsigned failure=0;failure<3;++failure) {
        Fixture f;std::array<glob2::CpuClockRegistry::Lease,130> roles;
        for(unsigned i=0;i<roles.size();++i){f.owner->clocks.tid=i+1;roles[i]=f.registry->registerCurrent(glob2::CpuThreadRole::Worker);REQUIRE(bool(roles[i]));}
        auto accounting=f.begin(true);REQUIRE(bool(accounting));
        for(unsigned i=0;i<4;++i){const auto before=f.owner->clocks.threadReads;accounting->advanceEnvelope();CHECK(f.owner->clocks.threadReads-before<=64);}
        REQUIRE(accounting->envelopeReady());
        if(failure==0)roles[0].reset();
        else if(failure==1)f.owner->clocks.processValid=false;
        else f.owner->clocks.processCpu=99;
        if(failure==0)f.owner->clocks.processCpu=1100;
        tails(*accounting,*f.token);
        CHECK((accounting->tailFinished()||accounting->failed()));
        auto result=accounting->finishResult(*f.token,91,true,true,true,500000001,f.input.slackNs);
        CHECK_FALSE(result.authorization.permitsInference());CHECK_FALSE(result.outcome.clockValid);
        if(failure==0){CHECK(result.receipt.churn);CHECK(result.receipt.omittedThreads==1);}
        else CHECK_FALSE(result.receipt.validEnvelope);
        accounting.reset();f.token.reset();CHECK(f.policy->metrics().probeCpuNs>=f.input.reservedCpuNs);
    }
}

TEST_CASE("capture accounting rejects reversed optional clocks and mismatched cleanup identities" * doctest::test_suite("OpenCLGradient"))
{
    for(unsigned failure=0;failure<4;++failure) {
        Fixture f;auto accounting=f.begin(true);REQUIRE(bool(accounting));running(*accounting);
        accounting->accountStep(100,500);
        if(failure==0)accounting->accountStep(200,600);
        if(failure==1)accounting->accountStep(0,600);
        if(failure==2)accounting->accountStep(600,500);
        REQUIRE(accounting->markDeviceTerminal(100));
        if(failure==3)CHECK_FALSE(accounting->markCleanup(*f.token,true,100)); // Arrays are still owned.
        CHECK(accounting->failed());
        auto result=accounting->finishResult(*f.token,92,true,true,true,500000001,f.input.slackNs);
        CHECK_FALSE(result.authorization.permitsInference());CHECK_FALSE(result.outcome.clockValid);
        accounting.reset();f.token.reset();CHECK(f.policy->metrics().probeCpuNs>=f.input.reservedCpuNs);
    }
}

TEST_CASE("capture accounting requires native qualified identity and pre-reserved registry ownership" * doctest::test_suite("OpenCLGradient"))
{
    if(!glob2::threadCpuNs())return;
    Fixture f;CaptureProbeAccounting::Receipt attempted;
    const auto bytes=openCLProbeBytes();
    CHECK_FALSE(CaptureProbeAccounting::begin(f.registry,0,91,f.token->probeTicket(),true,attempted,1));CHECK(openCLProbeBytes()==bytes);
    auto accounting=f.begin(true);REQUIRE(bool(accounting));running(*accounting);
    f.owner->clocks.processCpu=1100;tails(*accounting,*f.token);REQUIRE(accounting->tailFinished());
    auto result=accounting->finishResult(*f.token,91,false,true,true,500000001,f.input.slackNs);
    CHECK_FALSE(result.authorization.permitsInference());
    result=accounting->finishResult(*f.token,92,true,true,true,500000001,f.input.slackNs);CHECK_FALSE(result.authorization.permitsInference());
    result=accounting->finishResult(*f.token,91,true,true,true,0,f.input.slackNs);CHECK_FALSE(result.authorization.permitsInference());
    result=accounting->finishResult(*f.token,91,true,true,true,500000001,f.input.slackNs);
    // Synthetic envelope can test the friend gate; this is not release evidence.
    CHECK(result.authorization.permitsInference());CHECK(result.outcome.clockValid);
    CHECK(result.outcome.alternativeCpuNs==result.outcome.actualProbeCpuNs);CHECK_FALSE(result.receipt.splitCandidate);
    const auto floor=result.outcome.actualProbeCpuNs;
    result.outcome.actualProbeCpuNs=0;result.outcome.alternativeCpuNs=0; // Cannot lower the private frozen floors.
    result.receipt.splitCandidate=true; // Nor forge a debt-only cleanup mode.
    result=CaptureProbeAccounting::close(accounting,result);CHECK_FALSE(accounting);
    CHECK(result.outcome.actualProbeCpuNs>=floor);CHECK(result.outcome.alternativeCpuNs>=floor);
    CHECK(result.outcome.actualProbeCpuNs==result.receipt.totalUpperCpuNs);
    CHECK(result.outcome.alternativeCpuNs==result.outcome.actualProbeCpuNs);
    CHECK_FALSE(f.token->settle(result.outcome,result.authorization)); // A single pair cannot promote.
    CHECK(f.policy->metrics().promoted==0);
}

TEST_CASE("capture accounting rejects absent native coverage without forgiving optional debt" * doctest::test_suite("OpenCLGradient"))
{
    if(!glob2::threadCpuNs())return;
    const auto baseline=openCLProbeBytes();Fixture f;
    auto accounting=f.begin(true);REQUIRE(bool(accounting));running(*accounting);
    f.owner->clocks.processCpu=1100;tails(*accounting,*f.token);REQUIRE(accounting->tailFinished());
    RequiredCandidateReceipt absent;
    auto result=accounting->finishResult(*f.token,91,true,true,true,500000001,f.input.slackNs,&absent);
    CHECK(result.receipt.splitCandidate);CHECK_FALSE(result.receipt.nativeCoverageComplete);
    CHECK_FALSE(result.authorization.permitsInference());CHECK_FALSE(result.outcome.clockValid);
    CHECK(result.outcome.actualProbeCpuNs>0);CHECK(result.outcome.actualProbeCpuNs==result.receipt.totalUpperCpuNs);
    result=CaptureProbeAccounting::close(accounting,result);
    CHECK_FALSE(f.token->settle(result.outcome,result.authorization));f.token.reset();
    CHECK(f.policy->metrics().probeCpuNs>=f.input.reservedCpuNs);CHECK(f.policy->metrics().promoted==0);
    // Fixture still owns the charged registry; only capture/adapter credits end.
    CHECK(openCLProbeBytes()==baseline);
}


TEST_CASE("capture accounting rejects zero process clocks even when provider marks them valid" * doctest::test_suite("OpenCLGradient"))
{
    Fixture f;f.owner->clocks.processCpu=0;
    auto accounting=f.begin(true);REQUIRE(bool(accounting));CHECK(accounting->failed());
    const auto reads=f.owner->clocks.threadReads;
    CHECK(accounting->advanceEnvelope()==glob2::ProcessCpuEnvelope::Progress::Invalid);
    CHECK(f.owner->clocks.threadReads==reads);CHECK_FALSE(accounting->envelopeReady());
    auto result=accounting->finishResult(*f.token,91,true,true,true,500000001,f.input.slackNs);
    CHECK_FALSE(result.authorization.permitsInference());CHECK_FALSE(result.outcome.clockValid);
    accounting.reset();f.token.reset();CHECK(f.policy->metrics().probeCpuNs>=f.input.reservedCpuNs);
}
