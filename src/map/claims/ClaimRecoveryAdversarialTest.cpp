// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ClaimRecovery.h"

TEST_SUITE("ClaimRecoveryAdversarialPure")
{
    TEST_CASE("eviction movement and job context churn cannot reopen the admission window")
    {
        ResourceClaims::ClaimRecovery state;
        state.setContext(1,0);
        for(unsigned attempt=0;attempt<4;++attempt) {
            REQUIRE(state.admit(100+attempt));state.selected(attempt,20);
            state.recordFailure(101+attempt,200+attempt);
        }
        for(unsigned tick=105;tick<164;++tick) {
            // Changing material or mobility models immediate rehire or medical
            // transitions; successful incidental pickup also keeps CPU limits.
            state.setContext(std::uint8_t(tick%3),std::uint8_t(tick%2));
            state.harvested();state.exhausted();
            CHECK_FALSE(state.allowsAttempt(tick));CHECK_FALSE(state.admit(tick));
        }
        CHECK(state.admit(164));CHECK(state.attempts==1);CHECK(state.windowStart==164);
    }

    TEST_CASE("more failed accesses than cache entries evict deterministically without losing retry accounting")
    {
        ResourceClaims::ClaimRecovery state;state.setContext(1,0);
        for(unsigned i=0;i<9;++i) {state.selected(i,20);state.recordFailure(i,100+i);}
        CHECK(state.failureStreak==9);CHECK(state.replacement==1);
        for(unsigned i=0;i<5;++i)CHECK_FALSE(state.rejects(100+i,10));
        for(unsigned i=5;i<9;++i)CHECK(state.rejects(100+i,10));
        state.exhausted();
        CHECK(state.rejects(105,132));CHECK_FALSE(state.rejects(105,133));
        CHECK_FALSE(state.rejects(108,136));
    }

    TEST_CASE("unproductive outcome can retain accounting without rejecting an access")
    {
        ResourceClaims::ClaimRecovery state;state.setContext(1,0);
        REQUIRE(state.admit(10));state.selected(2,10);state.recordFailure(11,42,0);
        CHECK(state.failureStreak==1);CHECK_FALSE(state.rejects(42,11));CHECK_FALSE(state.recovering);
        CHECK(state.nextProbe==3);CHECK(state.allowsAttempt(12));
    }

    TEST_CASE("context replaces material rejection but retains owner budget")
    {
        ResourceClaims::ClaimRecovery state;state.setContext(1,0);
        REQUIRE(state.admit(20));state.recordFailure(21,42);
        REQUIRE(state.rejects(42,21));state.setContext(2,0);
        CHECK_FALSE(state.rejects(42,21));CHECK(state.attempts==1);CHECK(state.windowStart==20);
        CHECK(state.failureStreak==1);CHECK(state.nextProbe==0);
    }

    TEST_CASE("late terminal failure keeps backoff after the admission window expires")
    {
        ResourceClaims::ClaimRecovery state;state.setContext(1,0);
        for(unsigned tick:{100u,120u,140u,163u})REQUIRE(state.admit(tick));
        state.recordFailure(164,42,16);state.exhausted();
        for(unsigned tick=164;tick<180;++tick)CHECK_FALSE(state.admit(tick));
        CHECK(state.rejects(42,179));CHECK_FALSE(state.rejects(42,180));
        CHECK(state.admit(180));CHECK(state.windowStart==180);CHECK(state.attempts==1);
    }

    TEST_CASE("expiry and backoff remain deterministic across unsigned tick wrap")
    {
        ResourceClaims::ClaimRecovery state;state.setContext(1,0);
        constexpr std::uint32_t first=UINT32_MAX-31;
        for(unsigned i=0;i<4;++i) {REQUIRE(state.admit(first+i));state.selected(i,8);state.recordFailure(first+i,50+i);}
        state.exhausted();
        CHECK_FALSE(state.allowsAttempt(31));CHECK(state.allowsAttempt(32));
        CHECK(state.rejects(50,95));CHECK_FALSE(state.rejects(50,96));
        CHECK(state.admit(32));CHECK(state.attempts==1);
    }

    TEST_CASE("copied authoritative recovery state preserves every next decision")
    {
        ResourceClaims::ClaimRecovery original;original.setContext(1,0);
        for(unsigned i=0;i<4;++i) {REQUIRE(original.admit(40+i));original.selected(i,12);original.recordFailure(41+i,70+i);}
        auto resumed=original;CHECK(resumed==original);
        for(std::uint32_t tick=44;tick<240;++tick) {
            CHECK(original.allowsAttempt(tick)==resumed.allowsAttempt(tick));
            for(std::uint32_t tile=70;tile<74;++tile)CHECK(original.rejects(tile,tick)==resumed.rejects(tile,tick));
            if(tick%17==0) {CHECK(original.admit(tick)==resumed.admit(tick));}
            if(tick%23==0) {original.exhausted();resumed.exhausted();}
            CHECK(original==resumed);
        }
    }
}
