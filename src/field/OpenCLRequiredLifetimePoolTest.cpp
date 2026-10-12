// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenCLRequiredLifetimePool.h"
#include "OpenCLGradient.h"
#include "Glob2Test.h"
#include "ThreadSupport.h"
#include <thread>
#include <chrono>

namespace
{
using namespace gradient_kernel;
struct PoolCounters {unsigned destroyed=0,queries=0;};
struct PoolState {
    struct Admission {
        std::shared_ptr<OpenCLRequiredLifetimePool> parent;
        OpenCLRequiredLifetimePool::Slot* slot;
        ~Admission(){slot->release();}
    } admission;
    struct Credit {std::size_t bytes=0;~Credit(){if(bytes)releaseOpenCLHostBytes(bytes);}} charged;
    std::shared_ptr<PoolCounters> counters;
    bool terminal=false;
    unsigned privateOutput=77;
    PoolState(std::shared_ptr<OpenCLRequiredLifetimePool> p,OpenCLRequiredLifetimePool::Slot* s,
        std::shared_ptr<PoolCounters> c):admission{std::move(p),s},counters(std::move(c)) {}
    ~PoolState(){++counters->destroyed;}
    static bool proof(void* value) noexcept {
        auto& state=*static_cast<PoolState*>(value);++state.counters->queries;
        ++state.privateOutput; // Simulated late driver write into retained storage.
        return state.terminal;
    }
};
std::shared_ptr<PoolState> makeState(const std::shared_ptr<OpenCLRequiredLifetimePool>& parent,
    OpenCLRequiredLifetimePool::Slot* slot,const std::shared_ptr<PoolCounters>& counters) {
    constexpr auto bytes=sizeof(PoolState)+256;
    REQUIRE(reserveOpenCLHostBytes(bytes));
    struct Guard {std::size_t bytes;~Guard(){if(bytes)releaseOpenCLHostBytes(bytes);}} guard{bytes};
    auto state=std::make_shared<PoolState>(parent,slot,counters);state->charged.bytes=bytes;guard.bytes=0;return state;
}
}

TEST_CASE("required lifetime pool admits independent healthy lanes and declines only at its fixed bound" * doctest::test_suite("OpenCLGradient"))
{
    OpenCLRequiredLifetimePool pool;
    struct Reservations {
        std::array<OpenCLRequiredLifetimePool::Slot*,OpenCLRequiredLifetimePool::MaxSlots> slots{};
        ~Reservations(){for(auto* slot:slots)if(slot)slot->release();}
    } owned;
    owned.slots[0]=pool.tryReserve();REQUIRE(owned.slots[0]);
    for(std::size_t i=1;i<owned.slots.size();++i){
        owned.slots[i]=pool.tryReserve();REQUIRE(owned.slots[i]);
        CHECK(owned.slots[i]!=owned.slots[0]); // A blocked healthy lane owns slot0.
    }
    CHECK_FALSE(pool.tryReserve());CHECK_FALSE(pool.quarantined());
    CHECK_FALSE(pool.tryReap(&PoolState::proof));
    auto* released=owned.slots[17];released->release();owned.slots[17]=nullptr;
    owned.slots[17]=pool.tryReserve();CHECK(owned.slots[17]==released);
    CHECK_FALSE(pool.tryReserve());
}

TEST_CASE("required slot quarantine keeps parent payload and credits until proof and last owner release" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLStatus().hostBytes;
    auto parent=std::make_shared<OpenCLRequiredLifetimePool>();auto counters=std::make_shared<PoolCounters>();
    auto* first=parent->tryReserve();auto* second=parent->tryReserve();REQUIRE(first);REQUIRE(second);
    auto one=makeState(parent,first,counters),two=makeState(parent,second,counters);
    struct Cleanup {
        std::shared_ptr<OpenCLRequiredLifetimePool> parent;std::shared_ptr<PoolState> one,two;
        ~Cleanup(){if(one)one->terminal=true;if(two)two->terminal=true;parent->tryReap(&PoolState::proof);}
    } cleanup{parent,one,two};
    CHECK(first->quarantine(one));CHECK(second->quarantine(two));
    CHECK_FALSE(first->quarantine(two));CHECK(parent->quarantined());
    for(unsigned attempt=0;attempt<3;++attempt)CHECK_FALSE(parent->tryReap(&PoolState::proof));
    CHECK(counters->queries==6);CHECK(counters->destroyed==0);
    CHECK(one->privateOutput==80);CHECK(two->privateOutput==80);
    auto* healthy=parent->tryReserve();REQUIRE(healthy);CHECK(healthy!=first);CHECK(healthy!=second);healthy->release();
    one->terminal=true;CHECK(parent->tryReap(&PoolState::proof));CHECK_FALSE(first->quarantined());
    CHECK(first->busy());CHECK(second->quarantined());CHECK(counters->destroyed==0);
    CHECK(openCLStatus().hostBytes>=baseline+2*(sizeof(PoolState)+256));
    // External owners pin a proven slot; the independent failed slot stays held.
    cleanup.one.reset();one.reset();CHECK_FALSE(first->busy());CHECK(counters->destroyed==1);
    two->terminal=true;CHECK(parent->tryReap(&PoolState::proof));CHECK_FALSE(parent->quarantined());
    cleanup.two.reset();two.reset();CHECK_FALSE(second->busy());CHECK(counters->destroyed==2);
    CHECK(openCLStatus().hostBytes==baseline);
}

TEST_CASE("quarantined required graph alone pins its pool until a background reaper takes ownership" * doctest::test_suite("OpenCLGradient"))
{
    const auto baseline=openCLStatus().hostBytes;
    auto parent=std::make_shared<OpenCLRequiredLifetimePool>();auto counters=std::make_shared<PoolCounters>();
    auto* slot=parent->tryReserve();REQUIRE(slot);
    auto state=makeState(parent,slot,counters);std::weak_ptr<PoolState> borrowed=state;
    std::weak_ptr<OpenCLRequiredLifetimePool> weakParent=parent;
    struct Cleanup {
        std::weak_ptr<PoolState> state;
        ~Cleanup(){if(auto retained=state.lock()){
            retained->terminal=true;auto parent=retained->admission.parent;parent->tryReap(&PoolState::proof);}}
    } cleanup{state};
    REQUIRE(slot->quarantine(state));state.reset();parent.reset();
    CHECK_FALSE(weakParent.expired());CHECK_FALSE(borrowed.expired());CHECK(counters->destroyed==0);
    CHECK(openCLStatus().hostBytes>=baseline+sizeof(PoolState)+256);
    auto coordinator=weakParent.lock();REQUIRE(bool(coordinator));
    CHECK_FALSE(coordinator->tryReap(&PoolState::proof));
    {auto terminal=borrowed.lock();REQUIRE(bool(terminal));terminal->terminal=true;}
    CHECK(coordinator->tryReap(&PoolState::proof));CHECK(borrowed.expired());CHECK(counters->destroyed==1);
    coordinator.reset();CHECK(weakParent.expired());CHECK(openCLStatus().hostBytes==baseline);
}

TEST_CASE("required pool reaping never invokes one event proof concurrently" * doctest::test_suite("OpenCLGradient"))
{
    if constexpr(!GAGCore::ThreadSupport::available)return;
    struct Held {
        std::shared_ptr<OpenCLRequiredLifetimePool> parent;
        OpenCLRequiredLifetimePool::Slot* slot=nullptr;
        std::atomic<bool> entered{false},released{false};std::atomic<unsigned> calls{0};
        ~Held(){if(slot)slot->release();}
        static bool proof(void* value) noexcept {
            auto& state=*static_cast<Held*>(value);++state.calls;state.entered=true;
            while(!state.released.load())std::this_thread::yield();return true;
        }
    };
    auto parent=std::make_shared<OpenCLRequiredLifetimePool>();auto* slot=parent->tryReserve();REQUIRE(slot);
    auto state=std::make_shared<Held>();state->parent=parent;state->slot=slot;REQUIRE(slot->quarantine(state));
    bool reaped=false;std::thread worker([&]{reaped=parent->tryReap(&Held::proof);});
    struct Join {std::shared_ptr<Held> state;std::thread& worker;~Join(){if(state)state->released=true;if(worker.joinable())worker.join();}} join{state,worker};
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!state->entered && std::chrono::steady_clock::now()<until)std::this_thread::yield();
    REQUIRE(state->entered.load());CHECK_FALSE(parent->tryReap(&Held::proof));CHECK(state->calls==1);
    auto* independent=parent->tryReserve();REQUIRE(independent);CHECK(independent!=slot);independent->release();
    state->released=true;worker.join();CHECK(reaped);CHECK_FALSE(slot->quarantined());CHECK(slot->busy());
    join.state.reset();state.reset();CHECK_FALSE(slot->busy());
}
