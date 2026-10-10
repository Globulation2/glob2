// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenCLBorrowedLifetime.h"
#include "OpenCLGradient.h"
#include "Glob2Test.h"
#include <array>

namespace
{
struct BorrowedState {
    gradient_kernel::OpenCLBorrowedLifetime& slot;
    unsigned& destroyed;
    std::array<unsigned,4> metadata{11,22,33,44};
    std::array<std::uint16_t,4> originals{65535,1,0,65300},output{77,77,77,77};
    bool terminal=false;
    unsigned queries=0;
    const std::size_t charged;
    BorrowedState(gradient_kernel::OpenCLBorrowedLifetime& slot,unsigned& count,std::size_t bytes)
        :slot(slot),destroyed(count),charged(bytes) {}
    ~BorrowedState() {
        ++destroyed;
        gradient_kernel::releaseOpenCLProbeBytes(charged);
        slot.release();
    }
    static bool proof(void* value) noexcept {
        auto& state=*static_cast<BorrowedState*>(value);++state.queries;
        // A simulated driver reads/writes the borrowed destinations on every
        // attempt, including repeated failed completion queries.
        if(state.metadata!=std::array<unsigned,4>{11,22,33,44} ||
           state.originals!=std::array<std::uint16_t,4>{65535,1,0,65300} ||
           state.output!=std::array<std::uint16_t,4>{77,77,77,77})return false;
        return state.terminal;
    }
};
}

TEST_CASE("borrowed OpenCL storage survives repeated failed proof and later reap" * doctest::test_suite("OpenCLGradient"))
{
    using namespace gradient_kernel;
    const auto baseline=openCLProbeBytes();
    OpenCLBorrowedLifetime slot;unsigned destroyed=0;
    REQUIRE(slot.tryReserve());CHECK_FALSE(slot.tryReserve());
    REQUIRE(reserveOpenCLProbeBytes(sizeof(BorrowedState)+256));
    auto state=std::make_shared<BorrowedState>(slot,destroyed,sizeof(BorrowedState)+256);
    auto* borrowed=state.get();std::weak_ptr<BorrowedState> weak=state;
    CHECK(slot.quarantine(state));CHECK(slot.quarantine(state));state.reset();
    CHECK(slot.busy());CHECK(slot.quarantined());CHECK(destroyed==0);
    for(unsigned retry=0;retry<4;++retry)CHECK_FALSE(slot.tryReap(&BorrowedState::proof));
    CHECK(borrowed->queries==4);CHECK_FALSE(weak.expired());
    CHECK(openCLProbeBytes()==baseline+sizeof(BorrowedState)+256);
    CHECK_FALSE(slot.tryReserve());CHECK(destroyed==0);
    borrowed->terminal=true;CHECK(slot.tryReap(&BorrowedState::proof));
    CHECK(weak.expired());CHECK(destroyed==1);CHECK_FALSE(slot.busy());
    CHECK_FALSE(slot.quarantined());CHECK(openCLProbeBytes()==baseline);
    REQUIRE(slot.tryReserve());slot.release();
}

TEST_CASE("positive OpenCL proof does not free a still borrowed external owner" * doctest::test_suite("OpenCLGradient"))
{
    using namespace gradient_kernel;
    const auto baseline=openCLProbeBytes();
    OpenCLBorrowedLifetime slot;unsigned destroyed=0;
    REQUIRE(slot.tryReserve());REQUIRE(reserveOpenCLProbeBytes(sizeof(BorrowedState)+256));
    auto state=std::make_shared<BorrowedState>(slot,destroyed,sizeof(BorrowedState)+256);
    state->terminal=true;CHECK(slot.quarantine(state));
    CHECK(slot.tryReap(&BorrowedState::proof));CHECK_FALSE(slot.quarantined());
    CHECK(slot.busy());CHECK_FALSE(slot.tryReserve());CHECK(destroyed==0);
    CHECK(openCLProbeBytes()>baseline);
    state.reset();CHECK(destroyed==1);CHECK_FALSE(slot.busy());CHECK(openCLProbeBytes()==baseline);
    CHECK_FALSE(slot.tryReap(&BorrowedState::proof));
}

TEST_CASE("failed OpenCL proof never runs payload destructors or releases admission" * doctest::test_suite("OpenCLGradient"))
{
    using namespace gradient_kernel;
    OpenCLBorrowedLifetime slot;unsigned destroyed=0;
    const auto baseline=openCLProbeBytes();
    REQUIRE(slot.tryReserve());REQUIRE(reserveOpenCLProbeBytes(sizeof(BorrowedState)+256));
    auto state=std::make_shared<BorrowedState>(slot,destroyed,sizeof(BorrowedState)+256);
    CHECK(slot.quarantine(state));
    CHECK_FALSE(slot.quarantine({}));
    auto foreign=std::make_shared<unsigned>(123);
    CHECK_FALSE(slot.quarantine(foreign));
    CHECK_FALSE(slot.tryReap(nullptr));CHECK_FALSE(slot.tryReap(&BorrowedState::proof));
    CHECK(destroyed==0);CHECK(slot.busy());CHECK(slot.quarantined());
    // Test teardown supplies an explicit successful proof. Production shutdown
    // must retain an unproved graph; it cannot substitute ordinary destruction.
    state->terminal=true;CHECK(slot.tryReap(&BorrowedState::proof));state.reset();
    CHECK(destroyed==1);CHECK(openCLProbeBytes()==baseline);
}
