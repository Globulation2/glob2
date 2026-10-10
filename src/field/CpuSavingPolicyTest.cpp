// SPDX-License-Identifier: GPL-3.0-or-later
#include "AdaptiveGradientPolicy.h"
#include "Glob2Test.h"

namespace
{
using namespace gradient_kernel;
WorkloadKey workload(unsigned width=256) {
    return {width,width,1,3,1,Family::Materials,1,0};
}
std::optional<CpuSavingPolicy::ProbeTicket> nextProbe(CpuSavingPolicy& policy, const WorkloadKey& key,
                                                     std::uint64_t tick=1, std::uint64_t reserve=1200000) {
    for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod;++i)
        if(auto ticket=policy.beginProbe(key,Plan::Frozen8,tick,reserve,100000000,1000000,false)) return ticket;
    return {};
}
std::optional<CpuSavingPolicy::ProbeTicket> nextAcceptedProbe(CpuSavingPolicy& policy,const WorkloadKey& key,
    std::uint64_t commonPreparation=0,std::uint64_t reference=1000000,std::uint64_t reserve=300000) {
    for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod;++i) {
        auto ticket=policy.beginAcceptedCapture(key,Plan::Frozen8,1,reserve,100000000,1000000,false,
            commonPreparation,41,true);
        if(!ticket)continue;
        if(policy.bindAcceptedReference(*ticket,41,reference,true,true) &&
           policy.authorizeOptionalExecution(*ticket,41,1,100000000,false))return ticket;
        policy.cancelProbe(*ticket,0);return {};
    }
    return {};
}
std::optional<CpuSavingPolicy::ProbeTicket> admitCapture(CpuSavingPolicy& policy,const WorkloadKey& key,
    std::uint64_t requiredGeneration=41,bool ready=true) {
    for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod;++i)
        if(auto ticket=policy.beginAcceptedCapture(key,Plan::Frozen8,1,300000,100000000,1000000,false,
            20000,requiredGeneration,ready))return ticket;
    return {};
}
}

TEST_CASE("accepted capture binds and authorizes exactly once without mutable reference tickets" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy;const auto key=workload();
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    policy.observeAccepted(key,Plan::CPU,1000000000,1);
    CHECK_FALSE(admitCapture(policy,key,41,false));
    CHECK_FALSE(admitCapture(policy,key,0,true));
    auto ticket=admitCapture(policy,key);REQUIRE(ticket);
    CHECK_FALSE(policy.authorizeOptionalExecution(*ticket,41,1,100000000,false));
    REQUIRE(policy.bindAcceptedReference(*ticket,41,1000000,true,true));
    CHECK_FALSE(policy.bindAcceptedReference(*ticket,41,2000000,true,true));
    CHECK_FALSE(policy.authorizeOptionalExecution(*ticket,42,1,100000000,false));
    CHECK_FALSE(policy.authorizeOptionalExecution(*ticket,41,0,100000000,false));
    CHECK_FALSE(policy.authorizeOptionalExecution(*ticket,41,1,100000000,true));
    CHECK_FALSE(policy.authorizeOptionalExecution(*ticket,41,1,0,false));
    CHECK_FALSE(policy.authorizeOptionalExecution(*ticket,41,1,1000,false));
    auto authorization=policy.authorizeOptionalExecution(*ticket,41,1,100000000,false);
    REQUIRE(bool(authorization));
    CHECK(authorization->plan==Plan::Frozen8);
    CHECK(authorization->acceptedReferenceCpuNs==1000000);
    CHECK(authorization->commonPreparationCpuNs==20000);
    CHECK(authorization->reservedCpuNs==300000);
    CHECK_FALSE(policy.authorizeOptionalExecution(*ticket,41,1,100000000,false));
    CHECK_FALSE(policy.finishAcceptedCapture(*ticket,{100000,1000000,200000,100000000,true,true,true}));
    CHECK(policy.lookup(key).plan==Plan::CPU); // One observation cannot promote.
    CHECK(policy.metrics().probeCpuNs>=200000);
}

TEST_CASE("unbound captured work pays expense and cannot supply paired inference" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy first,second;const auto key=workload();
    for(auto* policy:{&first,&second}) {
        REQUIRE(policy->qualify(key,Plan::Frozen8,true));
        policy->observeAccepted(key,Plan::CPU,1000000000,1);
    }
    auto ticket=admitCapture(first,key);REQUIRE(ticket);
    CHECK_FALSE(admitCapture(second,key));
    CHECK_FALSE(first.finishAcceptedCapture(*ticket,{1,1,100000,100000000,true,true,true}));
    CHECK(first.metrics().probeCpuNs>=100000);
    CHECK(first.metrics().canceled==1);CHECK(first.lookup(key).plan==Plan::CPU);
    ticket=admitCapture(second,key);REQUIRE(ticket);second.cancelProbe(*ticket,1000);
}

TEST_CASE("invalid captured clocks and stale generations retain full reserved credit until reaping" * doctest::test_suite("OpenCLGradient"))
{
    const auto key=workload();
    for(unsigned scenario=0;scenario<4;++scenario) {
        CAPTURE(scenario);
        CpuSavingPolicy first,second;
        for(auto* policy:{&first,&second}) {
            REQUIRE(policy->qualify(key,Plan::Frozen8,true));
            policy->observeAccepted(key,Plan::CPU,1000000000,1);
        }
        auto ticket=admitCapture(first,key);REQUIRE(ticket);
        if(scenario==0)CHECK_FALSE(first.bindAcceptedReference(*ticket,41,1000000,true,false));
        if(scenario==1)CHECK_FALSE(first.bindAcceptedReference(*ticket,41,0,true,true));
        if(scenario>=2) {
            REQUIRE(first.bindAcceptedReference(*ticket,41,1000000,true,true));
            REQUIRE(bool(first.authorizeOptionalExecution(*ticket,41,1,100000000,false)));
        }
        if(scenario==3)first.invalidate();
        CHECK_FALSE(admitCapture(second,key));
        CHECK_FALSE(first.finishAcceptedCapture(*ticket,{1,1,1,100000000,true,true,scenario!=2}));
        CHECK(first.metrics().probeCpuNs>=300000);
        CHECK(first.lookup(key).plan==Plan::CPU);
        ticket=admitCapture(second,key);REQUIRE(ticket);second.cancelProbe(*ticket,0);
    }
}

TEST_CASE("captured reference belongs to the required session generation" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy;const auto key=workload();
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    policy.observeAccepted(key,Plan::CPU,1000000000,1);
    auto ticket=admitCapture(policy,key,4242);REQUIRE(ticket);
    CHECK_FALSE(policy.bindAcceptedReference(*ticket,4243,1000000,true,true));
    CHECK_FALSE(policy.bindAcceptedReference(*ticket,4242,1000000,true,true));
    CHECK_FALSE(policy.authorizeOptionalExecution(*ticket,4242,1,100000000,false));
    policy.cancelProbe(*ticket,100000);
    CHECK(policy.metrics().probeCpuNs>=100000);CHECK(policy.lookup(key).plan==Plan::CPU);
}

TEST_CASE("stale tickets cannot control a later capture owned by another policy" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy first,second;const auto key=workload();
    for(auto* policy:{&first,&second}) {
        REQUIRE(policy->qualify(key,Plan::Frozen8,true));
        policy->observeAccepted(key,Plan::CPU,1000000000,1);
    }
    auto old=admitCapture(first,key);REQUIRE(old);first.cancelProbe(*old,0);
    auto current=admitCapture(second,key);REQUIRE(current);
    CHECK(old->id!=current->id);
    CHECK_FALSE(second.bindAcceptedReference(*old,41,1000000,true,true));
    CHECK_FALSE(second.authorizeOptionalExecution(*old,41,1,100000000,false));
    CHECK_FALSE(second.finishAcceptedCapture(*old,{1,1,1,100000000,true,true,true}));
    second.cancelProbe(*old,0);
    CHECK(second.metrics().probeCpuNs==0);CHECK_FALSE(admitCapture(first,key));
    REQUIRE(second.bindAcceptedReference(*current,41,1000000,true,true));
    REQUIRE(bool(second.authorizeOptionalExecution(*current,41,1,100000000,false)));
    second.cancelProbe(*current,100000);
    CHECK(second.metrics().probeCpuNs>=100000);
}

TEST_CASE("unknown completion bounds cannot admit optional work" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy;const auto key=workload();
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    policy.observeAccepted(key,Plan::CPU,1000000000,1);
    for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod;++i) {
        CHECK_FALSE(policy.beginAcceptedCapture(key,Plan::Frozen8,1,300000,100000000,0,false,0,41,true));
        CHECK_FALSE(policy.beginProbe(key,Plan::Frozen8,1,300000,0,1000000,false));
    }
    CHECK(policy.metrics().admitted==0);
    auto ticket=admitCapture(policy,key);REQUIRE(ticket);policy.cancelProbe(*ticket,0);
}

TEST_CASE("zero measured completion or final slack cannot train captured work" * doctest::test_suite("OpenCLGradient"))
{
    const auto key=workload();
    for(unsigned scenario=0;scenario<2;++scenario) {
        CAPTURE(scenario);
        CpuSavingPolicy policy;REQUIRE(policy.qualify(key,Plan::Frozen8,true));
        policy.observeAccepted(key,Plan::CPU,1000000000,1);
        auto ticket=nextAcceptedProbe(policy,key);REQUIRE(ticket);
        CHECK_FALSE(policy.finishAcceptedCapture(*ticket,
            {100000,scenario==0 ? 0u : 1000000u,200000,scenario==1 ? 0u : 100000000u,true,true,true}));
        CHECK(policy.lookup(key).plan==Plan::CPU);
        CHECK(policy.metrics().probeCpuNs>=200000);
        CHECK(policy.metrics().promoted==0);
    }
}

TEST_CASE("CPU saving policy requires paired qualified background evidence" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy; const auto key=workload();
    CHECK(policy.lookup(key).plan==Plan::CPU);
    policy.observeAccepted(key,Plan::CPU,100000000000ull,1);
    CHECK_FALSE(nextProbe(policy,key)); // no offline exactness qualification
    CHECK_FALSE(policy.qualify(key,Plan::Frozen8,false));
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    bool sawPromotion=false;
    for(unsigned pair=0;pair<256;++pair) {
        auto ticket=nextProbe(policy,key); REQUIRE(ticket);
        CHECK_FALSE(nextProbe(policy,key)); // one globally, even for same class
        const bool promoted=policy.finishProbe(*ticket,1000000,100000,1000000,1100000,100000000,true,true);
        if(promoted) { CHECK(pair>=7); CHECK(((pair+1)&pair)==0); sawPromotion=true; break; }
        CHECK(policy.lookup(key).plan==Plan::CPU);
    }
    CHECK(sawPromotion);
    CHECK(policy.lookup(key).plan==Plan::Frozen8);
    CHECK(policy.lookup(workload(512)).plan==Plan::CPU);
    const auto version=policy.lookup(key).version;
    for(unsigned i=0;i<4;++i) policy.observeAccepted(key,Plan::Frozen8,950000,1);
    CHECK(policy.lookup(key).plan==Plan::CPU);
    CHECK(policy.lookup(key).version>version);
    CHECK_FALSE(nextProbe(policy,key));
    for(unsigned i=0;i<64;++i) policy.observeAccepted(key,Plan::CPU,1000000,1);
    // Existing probes consumed most initial credits; accepted CPU refills them.
    REQUIRE(nextProbe(policy,key));
}

TEST_CASE("probe credits expire and cancellation is not a required dependency" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy; const auto key=workload();
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    policy.observeAccepted(key,Plan::CPU,200000000,1); // 2ms budget
    auto ticket=nextProbe(policy,key); REQUIRE(ticket);
    policy.cancelProbe(*ticket,1000000);
    CHECK_FALSE(nextProbe(policy,key));
    policy.observeAccepted(key,Plan::CPU,100000000,300);
    ticket=nextProbe(policy,key,300,1000000); REQUIRE(ticket);
    policy.invalidate();
    CHECK_FALSE(policy.finishProbe(*ticket,1000000,1,1,1,100000000,true,true));
    CHECK(policy.lookup(key).plan==Plan::CPU);
    CHECK_FALSE(nextProbe(policy,key,300)); // invalidation retains reserved credit
}

TEST_CASE("CPU saving workload tables and qualified alternatives are bounded" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy;
    const auto key=workload();
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    REQUIRE(policy.qualify(key,Plan::Jacobi4,true));
    CHECK_FALSE(policy.qualify(key,Plan::Colored2,true));
    for(unsigned i=1;i<64;++i) REQUIRE(policy.qualify(workload(256+i),Plan::Frozen8,true));
    CHECK_FALSE(policy.qualify(workload(1024),Plan::Frozen8,true));
    CHECK(policy.metrics().profiles==64);
    CHECK(policy.lookup(workload(1024)).plan==Plan::CPU);
}

TEST_CASE("paused probes survive credit window rotation without underflow" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy; const auto key=workload();
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    policy.observeAccepted(key,Plan::CPU,200000000,1);
    auto ticket=nextProbe(policy,key); REQUIRE(ticket);
    policy.observeAccepted(key,Plan::CPU,300000000,257); // same ring slot
    // Tickets expose identity only, never mutable reservation/profile fields.
    static_assert(sizeof(CpuSavingPolicy::ProbeTicket)==2*sizeof(std::uint64_t));
    auto forged=*ticket;++forged.id;++forged.generation;
    CHECK_FALSE(policy.finishProbe(forged,600000,300000,1000000,1000000,100000000,true,true));
    CHECK(policy.metrics().probeCpuNs==0);
    CHECK_FALSE(policy.finishProbe(*ticket,600000,300000,1000000,1000000,100000000,true,true));
    CHECK(policy.metrics().probeCpuNs>=1000000);
    ticket=nextProbe(policy,key,257); REQUIRE(ticket); // expired reservation charged in new slot
    policy.cancelProbe(*ticket,1000000);
    CHECK_FALSE(nextProbe(policy,key,257));
}

TEST_CASE("CPU saving promotion requires both per field savings and deadline slack" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy; auto key=workload(); key.batch=8;
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    policy.observeAccepted(key,Plan::CPU,10000000000ull,1);
    for(unsigned pair=0;pair<8;++pair) {
        auto ticket=nextProbe(policy,key); REQUIRE(ticket);
        // 20% of this small batch saves only 5us per field: insufficient.
        CHECK_FALSE(policy.finishProbe(*ticket,200000,160000,1000000,1000000,100000000,true,true));
    }
    CHECK(policy.lookup(key).plan==Plan::CPU);
    CHECK_FALSE(policy.beginProbe(key,Plan::Frozen8,1,1000000,1000,1000,false));
}

TEST_CASE("optional probe ownership is global across map policies" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy first, second; const auto key=workload();
    for(auto* policy:{&first,&second}) {
        REQUIRE(policy->qualify(key,Plan::Frozen8,true));
        policy->observeAccepted(key,Plan::CPU,200000000,1);
    }
    auto ticket=nextProbe(first,key); REQUIRE(ticket);
    CHECK_FALSE(nextProbe(second,key));
    first.cancelProbe(*ticket,0);
    REQUIRE(nextProbe(second,key));
}

TEST_CASE("accepted reference is frozen and shared preparation reduces apparent savings" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy; const auto key=workload();
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    policy.observeAccepted(key,Plan::CPU,100000000000ull,1);
    for(unsigned pair=0;pair<256;++pair) {
        auto ticket=nextAcceptedProbe(policy,key,10000000);
        REQUIRE(ticket);
        // Paid alternative is much cheaper than propagation, but preparation
        // means total field savings are below the required ten percent.
        CHECK_FALSE(policy.finishAcceptedCapture(*ticket,{100000,1000000,200000,100000000,true,true,true}));
    }
    CHECK(policy.lookup(key).plan==Plan::CPU);
}

TEST_CASE("probe costs cannot exceed predictable caps or falsify accepted reference" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy; const auto key=workload();
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    policy.observeAccepted(key,Plan::CPU,100000000000ull,1);
    auto ticket=nextProbe(policy,key); REQUIRE(ticket);
    CHECK_FALSE(policy.finishProbe(*ticket,1000000,600000,1000000,1000000,100000000,true,true));
    CHECK_FALSE(nextProbe(policy,key));
    for(unsigned i=0;i<64;++i) policy.observeAccepted(key,Plan::CPU,1000000,1);
    // The paid replay API cannot inject a different reference into a capture.
    ticket=nextAcceptedProbe(policy,key);
    REQUIRE(ticket);
    CHECK_FALSE(policy.finishProbe(*ticket,2000000,100000,1000000,200000,100000000,true,true));
    CHECK(policy.lookup(key).plan==Plan::CPU);
}

TEST_CASE("generation invalidation retains global probe slot until background reaping" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy first, second; const auto key=workload();
    for(auto* policy:{&first,&second}) {
        REQUIRE(policy->qualify(key,Plan::Frozen8,true));
        policy->observeAccepted(key,Plan::CPU,1000000000,1);
    }
    auto ticket=nextProbe(first,key); REQUIRE(ticket);
    first.invalidate();
    CHECK_FALSE(nextProbe(second,key));
    first.cancelProbe(*ticket,0); // terminal background reaping, no owner join
    CHECK(first.metrics().probeCpuNs>=1200000);
    REQUIRE(nextProbe(second,key));
}

TEST_CASE("optional bootstrap does not use slow CPU time as a GPU completion bound" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy;const auto key=workload(512);
    CHECK(policy.probeOffer(key).reservedCpuNs==0);
    policy.observeAccepted(key,Plan::CPU,100000000,1);
    CHECK(policy.probeOffer(key).reservedCpuNs==0);
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    const auto offer=policy.probeOffer(key);
    CHECK(offer.plan==Plan::Frozen8);
    CHECK(offer.elapsedNs==1000000); // Exploratory dispatch admission only.
    CHECK(offer.reservedCpuNs==58000000);
    CHECK(policy.lookup(key).plan==Plan::CPU);
    for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod;++i)
        CHECK_FALSE(policy.beginAcceptedCapture(key,offer.plan,1,offer.reservedCpuNs,1000000,
            offer.elapsedNs,false,0,41,true));
    CHECK(policy.metrics().admitted==0);
    CHECK(policy.lookup(key).plan==Plan::CPU);
}

TEST_CASE("complete probe reservations require real rolling credits without changing required choices" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy;const auto key=workload(512);
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    auto pair=key;pair.batch=2;REQUIRE(policy.qualify(pair,Plan::Frozen8,true));
    policy.observeAccepted(key,Plan::CPU,1000000,1);
    const auto offer=policy.probeOffer(key);
    CHECK(offer.reservedCpuNs==8500000);
    CHECK(offer.elapsedNs==1000000);
    CHECK(policy.groupOffer(key,2,1).reservedCpuNs==0);
    for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod;++i)
        CHECK_FALSE(policy.beginAcceptedCapture(key,offer.plan,1,offer.reservedCpuNs,
            100000000,offer.elapsedNs,false,0,41,true));
    CHECK(policy.metrics().admitted==0);CHECK(policy.lookup(key).plan==Plan::CPU);
    // Other actual accepted jobs can fund learning, but never become this
    // immutable job's paired reference or this batch's measured CPU baseline.
    auto other=workload(256);policy.observeAccepted(other,Plan::CPU,2000000000,1);
    CHECK(policy.groupOffer(key,2,1).reservedCpuNs==9000000);
    std::optional<CpuSavingPolicy::ProbeTicket> ticket;
    for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !ticket;++i)
        ticket=policy.beginAcceptedCapture(key,offer.plan,1,offer.reservedCpuNs,
            100000000,offer.elapsedNs,false,0,41,true);
    REQUIRE(bool(ticket));REQUIRE(policy.bindAcceptedReference(*ticket,41,1000000,true,true));
    const auto authorization=policy.authorizeOptionalExecution(*ticket,41,1,100000000,false);
    REQUIRE(bool(authorization));CHECK(authorization->acceptedReferenceCpuNs==1000000);
    policy.cancelProbe(*ticket,7000000);
    CHECK(policy.metrics().probeCpuNs>=7000000);CHECK(policy.lookup(key).plan==Plan::CPU);
    // Cumulative headroom cannot substitute for an expired rolling window.
    policy.observeAccepted(other,Plan::CPU,2000000,257);
    CHECK(policy.groupOffer(key,2,257).reservedCpuNs==0);
    for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod;++i)
        CHECK_FALSE(policy.beginAcceptedCapture(key,offer.plan,257,offer.reservedCpuNs,
            100000000,offer.elapsedNs,false,0,41,true));
    CHECK(policy.metrics().admitted==1);CHECK(policy.lookup(key).plan==Plan::CPU);
}

TEST_CASE("tuning debt cannot wrap around and restore probe admission" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy;const auto key=workload();
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    policy.observeAccepted(key,Plan::CPU,UINT64_MAX,1);
    policy.observeAccepted(key,Plan::CPU,UINT64_MAX,1);
    CHECK(policy.metrics().acceptedCpuNs==UINT64_MAX);
    policy.recordTuningOverhead(1,UINT64_MAX);
    policy.recordTuningOverhead(1,UINT64_MAX);
    CHECK(policy.metrics().probeCpuNs==UINT64_MAX);
    CHECK_FALSE(nextProbe(policy,key));
    CHECK(policy.lookup(key).plan==Plan::CPU);
}

TEST_CASE("historical tuning debt at the cumulative cap cannot borrow fresh rolling credit" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy;const auto key=workload();
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    policy.observeAccepted(key,Plan::CPU,1000000000,1);
    auto ticket=nextProbe(policy,key);REQUIRE(ticket);
    policy.cancelProbe(*ticket,20000000);
    policy.observeAccepted(key,Plan::CPU,1000000000,257);
    CHECK(policy.metrics().acceptedCpuNs==2000000000);
    // Settlement also pays its real mutex/statistics CPU; historical debt may
    // exceed the requested work charge, never fall below it.
    CHECK(policy.metrics().probeCpuNs>=20000000);
    CHECK_FALSE(nextProbe(policy,key,257));
    CHECK(policy.lookup(key).plan==Plan::CPU);
}

TEST_CASE("suppressed promotion still processes and pays qualified counterfactuals" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy(false);const auto key=workload();
    policy.observeAccepted(key,Plan::CPU,100000000000ull,1);
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    for(unsigned pair=0;pair<256;++pair) {
        auto ticket=nextProbe(policy,key);REQUIRE(ticket);
        CHECK_FALSE(policy.finishProbe(*ticket,1000000,100000,1000000,1100000,100000000,true,true));
        CHECK(policy.lookup(key).plan==Plan::CPU);
    }
    CHECK_FALSE(policy.metrics().promotionEnabled);
    CHECK(policy.metrics().confidenceQualified>0);
    CHECK(policy.metrics().promoted==0);
    CHECK(policy.metrics().probeCpuNs>=256ull*1100000);
}

TEST_CASE("ready batch binding freezes selected accepted CPU sums and cannot infer an unqualified cardinality" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy;auto maximum=workload();maximum.batch=4;
    auto ready=maximum;ready.batch=2;
    REQUIRE(policy.qualify(maximum,Plan::Frozen8,true));
    policy.observeAccepted(workload(),Plan::CPU,1000000000000ull,1);
    auto admit=[&] {
        for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod;++i)
            if(auto value=policy.beginAcceptedBatchCapture(maximum,Plan::Frozen8,1,1000000,
                100000000,1000000,false,41,true))return value;
        return std::optional<CpuSavingPolicy::ProbeTicket>{};
    };
    auto ticket=admit();REQUIRE(ticket);
    CHECK_FALSE(policy.bindAcceptedReference(*ticket,41,4000000,true,true));
    CHECK_FALSE(policy.bindReadyBatch(*ticket,ready,41,4000000,300000,true,true));
    REQUIRE(policy.qualify(ready,Plan::Frozen8,true));
    auto wrong=ready;wrong.width+=1;
    CHECK_FALSE(policy.bindReadyBatch(*ticket,wrong,41,4000000,300000,true,true));
    REQUIRE(policy.bindReadyBatch(*ticket,ready,41,1000000+3000000,100000+200000,true,true));
    CHECK_FALSE(policy.bindReadyBatch(*ticket,ready,41,99999999,0,true,true));
    const auto authorization=policy.authorizeOptionalExecution(*ticket,41,1,100000000,false);
    REQUIRE(authorization);
    CHECK(authorization->acceptedReferenceCpuNs==4000000);
    CHECK(authorization->commonPreparationCpuNs==300000);
    CHECK(authorization->reservedCpuNs==1000000);
    policy.cancelProbe(*ticket,12345);
    CHECK(policy.metrics().acceptedCpuNs==1000000000000ull);
    CHECK(policy.lookup(ready).plan==Plan::CPU);
}

TEST_CASE("batch promotion publishes a directly measured bound and failure demotes its singleton too" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy;auto singleton=workload();auto batch=singleton;batch.batch=2;
    for(const auto& key:{singleton,batch}) {
        REQUIRE(policy.qualify(key,Plan::Frozen8,true));
        policy.observeAccepted(key,Plan::CPU,1000000000000ull,1);
        for(unsigned pair=0;pair<256 && policy.lookup(key).plan==Plan::CPU;++pair) {
            auto ticket=nextAcceptedProbe(policy,key,0,5000000,1000000);REQUIRE(ticket);
            policy.finishAcceptedCapture(*ticket,{100000,777777,600000,100000000,true,true,true});
        }
        REQUIRE(policy.lookup(key).plan==Plan::Frozen8);
        CHECK(policy.lookup(key).maximumElapsedNs==777777);
    }
    policy.observeBatchFailure(singleton,2,1);
    CHECK(policy.lookup(singleton).plan==Plan::CPU);
    CHECK(policy.lookup(batch).plan==Plan::CPU);
    CHECK(policy.lookup(batch).maximumElapsedNs==0);
}

TEST_CASE("stale ready batch binding holds full credit until explicit settlement" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy first,other;auto key=workload();key.batch=2;
    for(auto* policy:{&first,&other}) {
        REQUIRE(policy->qualify(key,Plan::Frozen8,true));
        policy->observeAccepted(key,Plan::CPU,1000000000000ull,1);
    }
    auto admit=[&](CpuSavingPolicy& policy) {
        for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod;++i)
            if(auto ticket=policy.beginAcceptedBatchCapture(key,Plan::Frozen8,1,1000000,
                100000000,1000000,false,41,true))return ticket;
        return std::optional<CpuSavingPolicy::ProbeTicket>{};
    };
    auto ticket=admit(first);REQUIRE(ticket);first.invalidate();
    CHECK_FALSE(first.bindReadyBatch(*ticket,key,41,4000000,0,true,true));
    CHECK_FALSE(admit(other));
    first.cancelProbe(*ticket,12);
    CHECK(first.metrics().probeCpuNs>=1000000);
    ticket=admit(other);REQUIRE(ticket);other.cancelProbe(*ticket,0);
}

TEST_CASE("unproductive batch-only offers pay actual fallback CPU and demote without batch training" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy;auto key=workload();key.batch=2;
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));policy.observeAccepted(key,Plan::CPU,1000000000000ull,1);
    for(unsigned pair=0;pair<256 && policy.lookup(key).plan==Plan::CPU;++pair) {
        auto ticket=nextAcceptedProbe(policy,key,0,5000000,1000000);REQUIRE(bool(ticket));
        policy.finishAcceptedCapture(*ticket,{100000,777777,600000,100000000,true,true,true});
    }
    const auto accepted=policy.lookup(key);REQUIRE(accepted.plan==Plan::Frozen8);
    const auto paid=policy.metrics().acceptedCpuNs;
    for(unsigned job=0;job<7;++job) {
        policy.observeBatchOfferFallback(key,accepted.plan,accepted.version,3000000,1,true);
        CHECK(policy.lookup(key).plan==Plan::Frozen8);
        CHECK(policy.lookup(key).maximumElapsedNs==777777);
    }
    policy.observeBatchOfferFallback(key,accepted.plan,accepted.version,3000000,1,true);
    CHECK(policy.lookup(key).plan==Plan::CPU);CHECK(policy.lookup(key).maximumElapsedNs==0);
    CHECK(policy.metrics().acceptedCpuNs==paid+8ull*3000000);
}

TEST_CASE("matching real singleton jobs age a demoted batch cooldown without batch training" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy;auto singleton=workload();auto batch=singleton;batch.batch=2;
    REQUIRE(policy.qualify(batch,Plan::Frozen8,true));
    policy.observeAccepted(singleton,Plan::CPU,1000000000000ull,1);
    policy.observeAccepted(batch,Plan::Frozen8,0,1,true);
    auto unrelated=singleton;unrelated.family=Family::Guard;
    if(unrelated.family==singleton.family)unrelated.family=Family::Clear;
    const auto paid=policy.metrics().acceptedCpuNs;
    for(unsigned job=0;job<64;++job)policy.observeAccepted(unrelated,Plan::CPU,1000000,1);
    CHECK(policy.groupOffer(singleton,2,1).reservedCpuNs==0);
    for(unsigned job=0;job<63;++job)policy.observeAccepted(singleton,Plan::CPU,1000000,1);
    CHECK(policy.groupOffer(singleton,2,1).reservedCpuNs==0);
    // Failed/invalid observations cannot masquerade as eligible requests.
    policy.observeAccepted(singleton,Plan::CPU,0,1,true);
    CHECK(policy.groupOffer(singleton,2,1).reservedCpuNs==0);
    policy.observeAccepted(singleton,Plan::CPU,1000000,1);
    CHECK(policy.groupOffer(singleton,2,1).reservedCpuNs>0);
    CHECK(policy.lookup(batch).plan==Plan::CPU);
    CHECK(policy.lookup(batch).maximumElapsedNs==0);
    CHECK(policy.metrics().acceptedCpuNs==paid+128ull*1000000);
}
