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
                                                     std::uint64_t tick=1, std::uint64_t reserve=1000000) {
    for(unsigned i=0;i<CpuSavingPolicy::ProbePeriod;++i)
        if(auto ticket=policy.beginProbe(key,Plan::Frozen8,tick,reserve,100000000,1000000,false)) return ticket;
    return {};
}
}

TEST_CASE("CPU saving policy requires paired qualified background evidence" * doctest::test_suite("OpenCLGradient"))
{
    CpuSavingPolicy policy; const auto key=workload();
    CHECK(policy.lookup(key).plan==Plan::CPU);
    policy.observeAccepted(key,Plan::CPU,1000000000,1);
    CHECK_FALSE(nextProbe(policy,key)); // no offline exactness qualification
    CHECK_FALSE(policy.qualify(key,Plan::Frozen8,false));
    REQUIRE(policy.qualify(key,Plan::Frozen8,true));
    for(unsigned pair=0;pair<8;++pair) {
        auto ticket=nextProbe(policy,key); REQUIRE(ticket);
        CHECK_FALSE(nextProbe(policy,key)); // one globally, even for same class
        const bool promoted=policy.finishProbe(*ticket,1000000,600000,1000000,1000000,100000000,true,true);
        CHECK(promoted==(pair==7));
        CHECK(policy.lookup(key).plan==(pair==7 ? Plan::Frozen8 : Plan::CPU));
    }
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
    policy.observeAccepted(key,Plan::CPU,100000000,1); // 1ms budget
    auto ticket=nextProbe(policy,key); REQUIRE(ticket);
    policy.cancelProbe(*ticket,1000000);
    CHECK_FALSE(nextProbe(policy,key));
    policy.observeAccepted(key,Plan::CPU,100000000,300);
    ticket=nextProbe(policy,key,300); REQUIRE(ticket);
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
