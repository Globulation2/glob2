// SPDX-License-Identifier: GPL-3.0-or-later
#include "GradientCpuCaptureGroup.h"
#include "GradientPropagation.h"
#include "Glob2Test.h"
#include <atomic>
#include <climits>
#include <SDL3/SDL_platform_defines.h>
#include <vector>

namespace
{
using namespace gradient_kernel;
struct GroupClock {
    std::uint64_t value=100,calls=0,step=100;
    unsigned reverseAt=0;
    static std::uint64_t read(const void* context) {
        auto& clock=*const_cast<GroupClock*>(static_cast<const GroupClock*>(context));
        if(++clock.calls==clock.reverseAt)return clock.value-50;
        return clock.value+=clock.step;
    }
};
struct GroupFixture {
    std::shared_ptr<CpuSavingPolicy> policy=std::make_shared<CpuSavingPolicy>();
    std::shared_ptr<GroupClock> clock=std::make_shared<GroupClock>();
    std::shared_ptr<int> source=std::make_shared<int>(7);
    std::vector<std::uint16_t> seeds;
    GradientCpuCaptureGroup::Input input;
    GradientCpuCaptureGroup::Receipt receipt;
    explicit GroupFixture(unsigned width=9,unsigned height=7,unsigned capacity=4)
        :seeds(std::size_t(width)*height,GRADIENT_UNREACHABLE) {
        seeds[0]=GRADIENT_AT_GOAL;
        if(seeds.size()>2){seeds[2]=GRADIENT_FORBIDDEN;seeds.back()=GRADIENT_AT_GOAL-127;}
        input.original=seeds;input.key={width,height,BUCKETS,3,1,Family::Clear,255,255};
        input.key.limit=COST_LIMIT;input.scalarClassic=true;input.sourceIdentity=source;
        input.weakSourceBytes=sizeof(int)+256;input.revision=21;input.variant=2;
        input.tick=1;input.due=17;input.optionalExpiry=101;input.requiredGeneration=91;
        input.maximumMembers=capacity;input.alternativeReady=true;
        input.reservedCpuNs=5000000;input.captureCpuLimitNs=2000000;
        input.slackNs=100000000;input.conservativeElapsedNs=1000000;
        for(unsigned n:{1u,2u,4u,8u}) {
            auto key=input.key;key.batch=n;policy->qualify(key,input.alternative,true);
        }
        policy->observeAccepted(input.key,Plan::CPU,1000000000000ull,1);
    }
    std::shared_ptr<GradientCpuCaptureGroup::Record> begin() {
        for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod;++i)
            if(auto record=GradientCpuCaptureGroup::begin(policy,input,receipt,
                {clock,&GroupClock::read,sizeof(GroupClock)+256}))return record;
        return {};
    }
    std::uint64_t propagate(std::vector<std::uint16_t>& field) {
        GradientWorkspace scratch;const auto start=glob2::threadCpuNs();
        propagateFieldCPU(field.data(),0,COST_LIMIT,field::Grid(input.key.width,input.key.height),scratch,
            [](std::size_t){return false;});
        const auto end=glob2::threadCpuNs();REQUIRE(start!=0);REQUIRE(end>start);return end-start;
    }
};
}

TEST_CASE("ready group owns exact eager fields and binds only selected actual CPU sums" * doctest::test_suite("OpenCLGradient"))
{
    if(!glob2::threadCpuNs())return;
    const auto baseline=openCLProbeBytes();
    for(const auto shape:{std::pair{1u,17u},std::pair{17u,1u},std::pair{9u,7u}}) {
        GroupFixture fixture(shape.first,shape.second);auto record=fixture.begin();REQUIRE(bool(record));
        std::array<std::vector<std::uint16_t>,3> original,gold;
        std::array<std::uint64_t,3> cpu{};
        for(unsigned i=0;i<3;++i) {
            auto input=fixture.input;input.commonPreparationCpuNs=100+i;input.due=17+i;
            fixture.seeds[0]=GRADIENT_AT_GOAL-i;original[i]=fixture.seeds;
            auto member=GradientCpuCaptureGroup::enroll(record,input,fixture.receipt);REQUIRE(bool(member));
            cpu[i]=fixture.propagate(fixture.seeds);gold[i]=fixture.seeds;
            REQUIRE(member->complete(fixture.seeds,{91,cpu[i],true,true},fixture.receipt));member.reset();
            fixture.seeds=original[i];
        }
        const auto paidBefore=record->receipt().optionalCpuNs;
        REQUIRE(record->freezeReady(91));CHECK(record->receipt().referenceBound);
        REQUIRE(record->adoptProducer());CHECK(record->count()==2);CHECK(record->key().batch==2);
        auto authorization=record->authorize(91,1,100000000,false);REQUIRE(bool(authorization));
        CHECK(authorization->acceptedReferenceCpuNs==cpu[0]+cpu[1]);
        CHECK(authorization->commonPreparationCpuNs==201);
        CHECK(record->receipt().optionalCpuNs>=paidBefore); // Unused third clone stays paid.
        for(unsigned i=0;i<2;++i) {
            CHECK(std::equal(record->original(i).begin(),record->original(i).end(),original[i].begin()));
            CHECK(std::equal(record->expected(i).begin(),record->expected(i).end(),gold[i].begin()));
            CHECK(record->publicationDue(i)==17+i);
        }
        fixture.seeds.assign(fixture.seeds.size(),77);
        auto alias=record;CHECK_FALSE(GradientCpuCaptureGroup::takeSettlement(record));alias.reset();
        std::weak_ptr<GradientCpuCaptureGroup::Record> weak=record;
        auto token=GradientCpuCaptureGroup::takeSettlement(record);REQUIRE(bool(token));
        token->cleanup();CHECK(weak.expired());CHECK(openCLProbeBytes()>baseline);
        CHECK(fixture.policy->metrics().probeCpuNs==0); // Cleanup is not premature settlement.
        CHECK_FALSE(token->settle({}));CHECK(fixture.policy->metrics().probeCpuNs>=5000000);
        CHECK(openCLProbeBytes()>baseline); // Escaped weak control remains charged.
        weak.reset();token.reset();CHECK(openCLProbeBytes()==baseline);
    }
}

TEST_CASE("closing ready membership never waits for or changes a late required field" * doctest::test_suite("OpenCLGradient"))
{
    if(!glob2::threadCpuNs())return;
    const auto baseline=openCLProbeBytes();GroupFixture fixture;auto record=fixture.begin();REQUIRE(bool(record));
    const auto original=fixture.seeds;std::array<std::uint64_t,2> cpu{};
    for(unsigned i=0;i<2;++i) {
        auto member=GradientCpuCaptureGroup::enroll(record,fixture.input,fixture.receipt);REQUIRE(bool(member));
        auto field=original;cpu[i]=fixture.propagate(field);
        REQUIRE(member->complete(field,{91,cpu[i],true,true},fixture.receipt));
    }
    auto late=GradientCpuCaptureGroup::enroll(record,fixture.input,fixture.receipt);REQUIRE(bool(late));
    REQUIRE(record->freezeReady(91));REQUIRE(record->adoptProducer());
    const auto selected=std::vector<std::uint16_t>(record->expected(0).begin(),record->expected(0).end());
    const auto paidBefore=record->receipt().optionalCpuNs;
    auto lateField=original;unsigned requiredRuns=0;++requiredRuns;const auto lateCpu=fixture.propagate(lateField);
    CHECK_FALSE(late->complete(lateField,{91,lateCpu,true,true},fixture.receipt));
    CHECK(requiredRuns==1);CHECK(std::equal(selected.begin(),selected.end(),record->expected(0).begin()));
    CHECK(record->publicationDue(0)==17);CHECK(record->publicationDue(1)==17);
    auto authorization=record->authorize(91,1,100000000,false);REQUIRE(bool(authorization));
    CHECK(authorization->acceptedReferenceCpuNs==cpu[0]+cpu[1]);
    CHECK(record->receipt().optionalCpuNs>paidBefore);
    CHECK_FALSE(GradientCpuCaptureGroup::takeSettlement(record));CHECK(fixture.policy->metrics().probeCpuNs==0);
    late.reset();auto token=GradientCpuCaptureGroup::takeSettlement(record);REQUIRE(bool(token));
    token.reset();CHECK(fixture.policy->metrics().probeCpuNs>=5000000);CHECK(openCLProbeBytes()==baseline);
}

TEST_CASE("group enrollment distinguishes weak controls and cancellation retains aliases" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();GroupFixture fixture;auto record=fixture.begin();REQUIRE(bool(record));
    // Same raw address, dimensions and revision still cannot impersonate the source owner.
    auto foreign=std::shared_ptr<int>(fixture.source.get(),[](int*){});
    auto input=fixture.input;input.sourceIdentity=foreign;
    CHECK_FALSE(GradientCpuCaptureGroup::enroll(record,input,fixture.receipt));
    input=fixture.input;++input.revision;CHECK_FALSE(GradientCpuCaptureGroup::enroll(record,input,fixture.receipt));
    auto member=GradientCpuCaptureGroup::enroll(record,fixture.input,fixture.receipt);REQUIRE(bool(member));
    fixture.policy->invalidate();record->requestCancel();CHECK_FALSE(record->freezeReady(92));
    CHECK_FALSE(GradientCpuCaptureGroup::takeSettlement(record));
    CHECK(openCLProbeBytes()>baseline);CHECK(fixture.policy->metrics().probeCpuNs==0);
    member.reset();record.reset();CHECK(openCLProbeBytes()==baseline);
    CHECK(fixture.policy->metrics().probeCpuNs>=5000000);
}

TEST_CASE("group storage pressure and intermediate setup clock reversal leave seeds untouched" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLProbeBytes();
    {
        GroupFixture fixture;const auto original=fixture.seeds;
        const auto pressure=OpenCLProbeBudget-baseline;REQUIRE(reserveOpenCLProbeBytes(pressure));
        struct Release {std::size_t n;~Release(){releaseOpenCLProbeBytes(n);}} release{pressure};
        CHECK_FALSE(fixture.begin());CHECK(fixture.seeds==original);
        CHECK(fixture.policy->metrics().probeCpuNs>=5000000);CHECK(openCLProbeBytes()==OpenCLProbeBudget);
    }
    CHECK(openCLProbeBytes()==baseline);
    {
        GroupFixture fixture;const auto original=fixture.seeds;
        // Align sampling first; the following admitted setup sees the reversal
        // between valid allocation pairs, which a pairwise-only guard would miss.
        auto maximum=fixture.input.key;maximum.batch=4;
        std::optional<CpuSavingPolicy::ProbeTicket> ticket;
        for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !ticket;++i)
            ticket=fixture.policy->beginAcceptedBatchCapture(maximum,fixture.input.alternative,1,5000000,
                100000000,1000000,false,91,true);
        REQUIRE(bool(ticket));fixture.policy->cancelProbe(*ticket,0);
        for(unsigned i=1;i<CpuSavingPolicy::ProbePeriod;++i)
            CHECK_FALSE(GradientCpuCaptureGroup::begin(fixture.policy,fixture.input,fixture.receipt,
                {fixture.clock,&GroupClock::read,sizeof(GroupClock)+256}));
        // The next attempt is admitted, so the fourth read is setup rather
        // than a rejected sampling attempt.
        fixture.clock->calls=0;fixture.clock->reverseAt=4;
        CHECK_FALSE(GradientCpuCaptureGroup::begin(fixture.policy,fixture.input,fixture.receipt,
            {fixture.clock,&GroupClock::read,sizeof(GroupClock)+256}));
        CHECK(fixture.seeds==original);CHECK(openCLProbeBytes()==baseline);
        CHECK_FALSE(fixture.receipt.clockValid);CHECK(fixture.receipt.requiresFullReservedCharge);
        CHECK(fixture.policy->metrics().probeCpuNs>=5000000);
    }
}

TEST_CASE("late invalid required receipt cancels an authorized group without early global release" * doctest::test_suite("OpenCLGradient"))
{
    if(!glob2::threadCpuNs())return;
    const auto baseline=openCLProbeBytes();GroupFixture fixture;auto record=fixture.begin();REQUIRE(bool(record));
    for(unsigned i=0;i<2;++i) {
        auto member=GradientCpuCaptureGroup::enroll(record,fixture.input,fixture.receipt);REQUIRE(bool(member));
        auto field=fixture.seeds;const auto cpu=fixture.propagate(field);
        REQUIRE(member->complete(field,{91,cpu,true,true},fixture.receipt));
    }
    auto late=GradientCpuCaptureGroup::enroll(record,fixture.input,fixture.receipt);REQUIRE(bool(late));
    REQUIRE(record->freezeReady(91));REQUIRE(record->adoptProducer());
    REQUIRE(bool(record->authorize(91,1,100000000,false)));CHECK(record->captureValid());
    auto field=fixture.seeds;const auto cpu=fixture.propagate(field);
    CHECK_FALSE(late->complete(field,{91,cpu,true,false},fixture.receipt));
    CHECK_FALSE(record->captureValid());CHECK(record->receipt().requiresFullReservedCharge);
    CHECK_FALSE(GradientCpuCaptureGroup::takeSettlement(record));
    GroupFixture other;CHECK_FALSE(other.begin()); // An unused late member still owns the global admission.
    late.reset();auto token=GradientCpuCaptureGroup::takeSettlement(record);REQUIRE(bool(token));
    CHECK_FALSE(token->settle({1,1,5000000,100000000,true,true,true}));
    CHECK(fixture.policy->metrics().probeCpuNs>=5000000);CHECK(fixture.policy->metrics().promoted==0);
    CHECK(openCLProbeBytes()==baseline);
    auto next=other.begin();REQUIRE(bool(next));next.reset();CHECK(openCLProbeBytes()==baseline);
}

TEST_CASE("authorized group cleanup rejects an earlier clock namespace and measured overshoot" * doctest::test_suite("OpenCLGradient"))
{
    if(!glob2::threadCpuNs())return;
    const auto baseline=openCLProbeBytes();
    for(const bool overshoot:{false,true}) {
        GroupFixture fixture(9,7,2);auto record=fixture.begin();REQUIRE(bool(record));
        for(unsigned i=0;i<2;++i) {
            auto member=GradientCpuCaptureGroup::enroll(record,fixture.input,fixture.receipt);REQUIRE(bool(member));
            auto field=fixture.seeds;const auto cpu=fixture.propagate(field);
            REQUIRE(member->complete(field,{91,cpu,true,true},fixture.receipt));
        }
        REQUIRE(record->freezeReady(91));REQUIRE(record->adoptProducer());
        REQUIRE(bool(record->authorize(91,1,100000000,false)));
        auto token=GradientCpuCaptureGroup::takeSettlement(record);REQUIRE(bool(token));
        if(overshoot)fixture.clock->step=GradientCpuCapture::MaxBlockCpuNs+1;
        else fixture.clock->value=1; // Positive increasing endpoints, both below the retained floor.
        const auto cleaned=token->cleanup();
        CHECK(cleaned.requiresFullReservedCharge);
        if(overshoot)CHECK(cleaned.overshoots>0);else CHECK_FALSE(cleaned.clockValid);
        CHECK_FALSE(token->settle({1,1,5000000,100000000,true,true,true}));
        CHECK(fixture.policy->metrics().probeCpuNs>=5000000);CHECK(fixture.policy->metrics().promoted==0);
        token.reset();CHECK(openCLProbeBytes()==baseline);
    }
}

TEST_CASE("group cleanup preserves separately paid replay CPU exactly once" * doctest::test_suite("OpenCLGradient"))
{
    if(!glob2::threadCpuNs())return;
    const auto baseline=openCLProbeBytes();GroupFixture fixture(9,7,2);auto record=fixture.begin();REQUIRE(bool(record));
    for(unsigned i=0;i<2;++i) {
        auto member=GradientCpuCaptureGroup::enroll(record,fixture.input,fixture.receipt);REQUIRE(bool(member));
        auto field=fixture.seeds;const auto cpu=fixture.propagate(field);
        REQUIRE(member->complete(field,{91,cpu,true,true},fixture.receipt));
    }
    REQUIRE(record->freezeReady(91));REQUIRE(record->adoptProducer());
    REQUIRE(bool(record->authorize(91,1,100000000,false)));
    const auto captured=record->receipt().optionalCpuNs;
    auto token=GradientCpuCaptureGroup::takeSettlement(record);REQUIRE(bool(token));
    constexpr std::uint64_t separatelyPaid=700000;
    token->addObservedCpu(separatelyPaid);
    const auto before=fixture.clock->value;const auto after=token->cleanup();
    const auto elapsed=fixture.clock->value-before;
    // First read opens the outer bracket; the remaining monotonic delta is
    // interior destruction plus its disjoint bookkeeping, each paid once.
    CHECK(after.optionalCpuNs==captured+separatelyPaid+elapsed-fixture.clock->step);
    CHECK(after.clockValid);CHECK_FALSE(after.requiresFullReservedCharge);
    const auto again=token->cleanup();CHECK(again.optionalCpuNs==after.optionalCpuNs);
    token.reset();CHECK(openCLProbeBytes()==baseline);
}

TEST_CASE("production group capacity includes the full future graph before any clone" * doctest::test_suite("OpenCLGradient"))
{
    GroupFixture fixture(512,512,8);
    const auto paid=openCLProbeBytes(),reads=fixture.clock->calls;
#if defined(SDL_PLATFORM_EMSCRIPTEN) || defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    CHECK(GradientCpuCaptureGroup::nativeCapacity(fixture.input,sizeof(GroupClock)+256)==0);
    CHECK(fixture.clock->calls==reads);CHECK(openCLProbeBytes()==paid);return;
#else
    REQUIRE(bool(OpenCLProbeBatchStorage::estimate({512,512},8,Plan::Frozen8)));
#endif
    CHECK(GradientCpuCaptureGroup::nativeCapacity(fixture.input,sizeof(GroupClock)+256)==8);
    // Restricting storage before admission chooses a real declared smaller cap,
    // rather than cloning eight members and discovering that replay cannot fit.
    fixture.seeds.resize(1024*1024);fixture.input.original=fixture.seeds;
    fixture.input.key.width=fixture.input.key.height=1024;
    CHECK(GradientCpuCaptureGroup::nativeCapacity(fixture.input,sizeof(GroupClock)+256)==2);
    fixture.input.key.width=UINT_MAX;
    CHECK(GradientCpuCaptureGroup::nativeCapacity(fixture.input,sizeof(GroupClock)+256)==0);
    CHECK(fixture.clock->calls==reads);CHECK(openCLProbeBytes()==paid);
}

TEST_CASE("group rejects expired original publication before capture or global admission" * doctest::test_suite("OpenCLGradient"))
{
    GroupFixture fixture;fixture.input.due=fixture.input.tick;
    const auto original=fixture.seeds;const auto bytes=openCLProbeBytes();
    CHECK_FALSE(fixture.begin());CHECK(fixture.clock->calls==0);
    CHECK(fixture.policy->metrics().admitted==0);CHECK(fixture.seeds==original);
    CHECK(openCLProbeBytes()==bytes);
}
