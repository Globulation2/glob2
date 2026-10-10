// SPDX-License-Identifier: GPL-3.0-or-later
#include "GradientCpuCapture.h"
#include "GradientPropagation.h"
#include "Glob2Test.h"
#include <stdexcept>
#include <vector>

namespace
{
using namespace gradient_kernel;
struct CaptureClock {
    std::uint64_t value=100,calls=0;
    unsigned failAt=0;
    bool throwing=false,reversing=false;
    static std::uint64_t read(const void* context) {
        auto& clock=*const_cast<CaptureClock*>(static_cast<const CaptureClock*>(context));
        ++clock.calls;
        if(clock.calls==clock.failAt) {
            if(clock.throwing)throw std::runtime_error("capture clock failure");
            return clock.reversing?clock.value-50:0;
        }
        return clock.value+=100;
    }
};
struct CaptureFixture {
    std::shared_ptr<CpuSavingPolicy> policy=std::make_shared<CpuSavingPolicy>();
    std::shared_ptr<CaptureClock> clock=std::make_shared<CaptureClock>();
    std::vector<std::uint16_t> seeds;
    std::vector<std::uint8_t> water;
    GradientCpuCapture::Input input;
    explicit CaptureFixture(unsigned width=9,unsigned height=1,int swim=0)
        :seeds(std::size_t(width)*height,GRADIENT_UNREACHABLE),water(seeds.size()) {
        seeds[0]=GRADIENT_AT_GOAL;
        if(seeds.size()>2){seeds[2]=GRADIENT_FORBIDDEN;seeds.back()=GRADIENT_AT_GOAL-127;}
        for(std::size_t i=0;i<water.size();++i)water[i]=i%3==0?2:0;
        input.original=seeds;input.water=water;input.swim=swim;
        input.key={width,height,BUCKETS,3,1,Family::Materials,255,255};
        input.key.limit=COST_LIMIT;input.maxCost=COST_LIMIT;
        input.tick=1;input.due=17;input.requiredGeneration=91;
        input.alternativeReady=true;input.reservedCpuNs=5000000;
        input.captureCpuLimitNs=2000000;input.slackNs=100000000;
        input.conservativeElapsedNs=1000000;input.commonPreparationCpuNs=9000000;
        policy->qualify(input.key,input.alternative,true);
        policy->observeAccepted(input.key,Plan::CPU,100000000000ull,1);
    }
    GradientCpuCapture::Clock timer() const {return {clock,&CaptureClock::read,sizeof(CaptureClock)};}
    std::unique_ptr<GradientCpuCapture> begin(GradientCpuCapture::Receipt& receipt) {
        for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod;++i)
            if(auto capture=GradientCpuCapture::begin(policy,input,receipt,timer()))return capture;
        return {};
    }
};
}

TEST_CASE("accepted CPU capture owns original gold and classic cost inputs" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();
    for(const auto shape:{std::pair{1u,1u},std::pair{1u,17u},std::pair{17u,1u},std::pair{9u,7u}})
        for(const int swim:{0,1,3,6}) {
            CaptureFixture fixture(shape.first,shape.second,swim);
            const auto originals=fixture.seeds;
            const auto water=fixture.water;
            fixture.input.maxCost=130;fixture.input.key.limit=130;
            REQUIRE(fixture.policy->qualify(fixture.input.key,fixture.input.alternative,true));
            GradientCpuCapture::Receipt receipt;
            auto capture=fixture.begin(receipt);REQUIRE(bool(capture));
            CHECK(openCLProbeBytes()==baseline+receipt.reservedBytes);
            GradientWorkspace requiredScratch;
            propagateFieldCPU(fixture.seeds.data(),swim,130,field::Grid(shape.first,shape.second),requiredScratch,
                [&](std::size_t i){return fixture.water[i]!=0;});
            const auto expected=fixture.seeds;
            auto record=capture->complete(fixture.seeds,{91,1000000,true,true},receipt);
            REQUIRE(bool(record));CHECK(receipt.referenceBound);
            fixture.seeds.assign(fixture.seeds.size(),77);fixture.water.assign(fixture.water.size(),99);
            CHECK(std::equal(record->original().begin(),record->original().end(),originals.begin()));
            CHECK(std::equal(record->expected().begin(),record->expected().end(),expected.begin()));
            CHECK(record->due()==17);CHECK(record->requiredGeneration()==91);
            CHECK(record->maxCost()==130);CHECK(record->swim()==swim);
            if(weightedClass(swim))CHECK(std::equal(record->water().begin(),record->water().end(),water.begin()));
            else CHECK(record->water().empty());
            auto replay=originals;
            GradientWorkspace replayScratch;
            propagateFieldCPU(replay.data(),swim,130,field::Grid(shape.first,shape.second),replayScratch,
                [&](std::size_t i){return record->water()[i]!=0;});
            CHECK(replay==expected);
            auto authorization=record->authorize(91,1,100000000,false);REQUIRE(bool(authorization));
            CHECK(authorization->acceptedReferenceCpuNs==1000000);
            CHECK(authorization->commonPreparationCpuNs==9000000);
            CHECK_FALSE(record->authorize(91,1,100000000,false));
            auto alias=record;record.reset();capture.reset();
            CHECK(openCLProbeBytes()>baseline);
            CHECK(fixture.policy->metrics().probeCpuNs==0);
            alias.reset();CHECK(openCLProbeBytes()==baseline);
            CHECK(fixture.policy->metrics().probeCpuNs>0);
        }
}

TEST_CASE("capture declines unsupported and unprepared jobs before retaining payload" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();
    CaptureFixture fixture;GradientCpuCapture::Receipt receipt;
    fixture.input.alternativeReady=false;CHECK_FALSE(fixture.begin(receipt));
    CHECK(fixture.policy->metrics().admitted==0);CHECK(openCLProbeBytes()==baseline);
    fixture.input.alternativeReady=true;fixture.input.modifiedCosts=true;
    const auto calls=fixture.clock->calls;CHECK_FALSE(fixture.begin(receipt));
    CHECK(fixture.clock->calls==calls);CHECK(openCLProbeBytes()==baseline);
    fixture.input.modifiedCosts=false;fixture.input.key.movementModifiers=true;
    CHECK_FALSE(fixture.begin(receipt));CHECK(fixture.clock->calls==calls);
}

TEST_CASE("capture reservations cover gold before originals and unwind pressure declines" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();
    REQUIRE(reserveOpenCLProbeBytes(OpenCLProbeBudget-baseline));
    {
        CaptureFixture fixture(512,512);GradientCpuCapture::Receipt receipt;
        const auto original=fixture.seeds;
        CHECK_FALSE(fixture.begin(receipt));CHECK(fixture.seeds==original);
        CHECK(fixture.policy->metrics().admitted>0);
        CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
        CHECK(openCLProbeBytes()==OpenCLProbeBudget);
    }
    releaseOpenCLProbeBytes(OpenCLProbeBudget-baseline);
    CHECK(openCLProbeBytes()==baseline);
    CaptureFixture fixture(512,512);GradientCpuCapture::Receipt receipt;
    auto capture=fixture.begin(receipt);REQUIRE(bool(capture));
    CHECK(receipt.reservedBytes>=fixture.seeds.size()*2*sizeof(std::uint16_t)+sizeof(CpuSavingPolicy));
    CHECK(receipt.copiedBytes==fixture.seeds.size()*sizeof(std::uint16_t));
    CHECK(receipt.blocks==fixture.seeds.size()*sizeof(std::uint16_t)/GradientCpuCapture::CopyBlockBytes);
    capture.reset();CHECK(openCLProbeBytes()==baseline);
}

TEST_CASE("capture never binds invalid required references or borrowed golden output" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();
    for(const auto required:{GradientCpuCapture::RequiredReceipt{92,100,true,true},
                            GradientCpuCapture::RequiredReceipt{91,0,true,true},
                            GradientCpuCapture::RequiredReceipt{91,100,true,false},
                            GradientCpuCapture::RequiredReceipt{91,100,false,true}}) {
        CaptureFixture fixture;GradientCpuCapture::Receipt receipt;
        const auto original=fixture.seeds;
        auto capture=fixture.begin(receipt);REQUIRE(bool(capture));
        CHECK_FALSE(capture->complete(fixture.seeds,required,receipt));
        CHECK(fixture.seeds==original);CHECK(openCLProbeBytes()==baseline);
        if(!required.clockValid || !required.propagationCpuNs)
            CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
    }
}

TEST_CASE("capture CPU clock failures stop optional copies and pay reserved credit" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();
    for(const unsigned failure:{1u,2u,4u,7u})for(const unsigned mode:{0u,1u,2u}) {
        CaptureFixture fixture(32768,1);GradientCpuCapture::Receipt receipt;
        const auto original=fixture.seeds;
        // Sampling rejections use two reads. Position failures relative to an
        // actual admitted call, whose first copied block follows allocation.
        std::optional<CpuSavingPolicy::ProbeTicket> reservation;
        for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod;++i) {
            reservation=fixture.policy->beginAcceptedCapture(fixture.input.key,fixture.input.alternative,1,
                fixture.input.reservedCpuNs,100000000,1000000,false,0,91,true);
            if(reservation)break;
        }
        REQUIRE(bool(reservation));fixture.policy->cancelProbe(*reservation,0);
        // The next admitted call is 128 opportunities away. Failure at setup
        // cannot create work; later injected failures target that admitted call.
        if(failure<=2) {
            fixture.clock->failAt=failure;fixture.clock->throwing=mode==1;fixture.clock->reversing=mode==2;
            CHECK_FALSE(GradientCpuCapture::begin(fixture.policy,fixture.input,receipt,fixture.timer()));
            CHECK_FALSE(receipt.clockValid);CHECK(fixture.seeds==original);
        } else {
            for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod-1;++i)
                CHECK_FALSE(GradientCpuCapture::begin(fixture.policy,fixture.input,receipt,fixture.timer()));
            fixture.clock->failAt=fixture.clock->calls+failure;
            fixture.clock->throwing=mode==1;fixture.clock->reversing=mode==2;
            CHECK_FALSE(GradientCpuCapture::begin(fixture.policy,fixture.input,receipt,fixture.timer()));
            CHECK_FALSE(receipt.clockValid);CHECK(receipt.requiresFullReservedCharge);
            CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
            CHECK(fixture.seeds==original);
        }
        CHECK(openCLProbeBytes()==baseline);
    }
}

TEST_CASE("capture producer handoff resets thread clock floor exactly once" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();
    CaptureFixture fixture;GradientCpuCapture::Receipt receipt;
    auto capture=fixture.begin(receipt);REQUIRE(bool(capture));
    auto record=capture->complete(fixture.seeds,{91,1000000,true,true},receipt);REQUIRE(bool(record));
    std::thread background([record] {
        CHECK(record->adoptProducer());CHECK_FALSE(record->adoptProducer());
        const auto authorization=record->authorize(91,1,100000000,false);
        CHECK(bool(authorization));
    });
    background.join();
    // Final cleanup on a different producer is conservatively invalid, never
    // an implicit incomparable-clock migration or a free optional allocation.
    record.reset();capture.reset();CHECK(openCLProbeBytes()==baseline);
    CHECK(fixture.policy->metrics().probeCpuNs>=fixture.input.reservedCpuNs);
}
