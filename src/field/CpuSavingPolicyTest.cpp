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
    // A copied ticket cannot change the internal reservation or profile index.
    ticket->profile=9999; ticket->alternative=9999; ticket->reservedCpuNs=UINT64_MAX;
    ticket->tick=9999;
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
        std::optional<CpuSavingPolicy::ProbeTicket> ticket;
        for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !ticket;++i)
            ticket=policy.beginProbe(key,Plan::Frozen8,1,300000,100000000,1000000,false,10000000,1000000);
        REQUIRE(ticket);
        // Paid alternative is much cheaper than propagation, but preparation
        // means total field savings are below the required ten percent.
        CHECK_FALSE(policy.finishProbe(*ticket,1000000,100000,1000000,200000,100000000,true,true));
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
    // A fresh admission freezes the measured accepted reference internally.
    ticket.reset();
    for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod && !ticket;++i)
        ticket=policy.beginProbe(key,Plan::Frozen8,1,300000,100000000,1000000,false,0,1000000);
    REQUIRE(ticket);
    ticket->acceptedReferenceCpuNs=2000000;
    CHECK_FALSE(policy.finishProbe(*ticket,2000000,100000,1000000,200000,100000000,true,true));
    CHECK(policy.lookup(key).plan==Plan::CPU);
}
