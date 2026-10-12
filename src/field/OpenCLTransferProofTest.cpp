// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenCLTransferProof.h"
#include "OpenCLBorrowedLifetime.h"
#include "OpenCLGradient.h"
#include "Glob2Test.h"
#include <array>
#include <memory>

TEST_CASE("required completion proof rejects old events and ambiguous commands" * doctest::test_suite("OpenCLGradient"))
{
    gradient_kernel::OpenCLTransferProof proof;
    CHECK_FALSE(proof.needsProof());
    proof.enqueue(0,true);CHECK(proof.canPollEvent());
    proof.enqueueUnobserved(0);CHECK_FALSE(proof.canPollEvent());
    proof.eventCompleted();CHECK(proof.needsProof()); // Earlier upload event.
    proof.enqueue(0,true);CHECK(proof.canPollEvent()); // Later in-order read.
    proof.eventCompleted();CHECK_FALSE(proof.needsProof());
    proof.enqueue(-5,true);CHECK_FALSE(proof.canPollEvent());
    proof.eventCompleted();CHECK(proof.needsProof()); // Error-returned event.
    proof.drained();CHECK_FALSE(proof.needsProof()); // Explicit positive whole-queue proof.
    proof.enqueue(0,false);CHECK(proof.needsProof());CHECK_FALSE(proof.canPollEvent());
    proof.drained();proof.enqueueUnobserved(0);
    proof.enqueue(0,false,true);CHECK_FALSE(proof.needsProof()); // Blocking proof.
    proof.enqueue(-5,false,true);CHECK(proof.needsProof()); // Blocking error.
    CHECK_FALSE(proof.canPollEvent());
}

TEST_CASE("failed required drain retains private destinations while CPU recovers originals" * doctest::test_suite("OpenCLGradient"))
{
    using namespace gradient_kernel;
    struct DriverStorage {
        OpenCLBorrowedLifetime& slot;
        unsigned& destroyed;
        const std::size_t charged;
        OpenCLTransferProof proof;
        std::array<unsigned,4> descriptors{11,22,33,44},flags{};
        std::array<unsigned short,4> upload{65535,1,0,65300},readback{};
        bool eventComplete=false;
        DriverStorage(OpenCLBorrowedLifetime& slot,unsigned& count,std::size_t bytes):slot(slot),destroyed(count),charged(bytes){}
        ~DriverStorage(){++destroyed;releaseOpenCLHostBytes(charged);slot.release();}
        static bool poll(void* value) noexcept {
            auto& state=*static_cast<DriverStorage*>(value);
            if(!state.eventComplete || !state.proof.canPollEvent())return false;
            state.proof.eventCompleted();return !state.proof.needsProof();
        }
    };
    OpenCLBorrowedLifetime slot;unsigned destroyed=0,continuations=0;
    const auto baseline=openCLStatus().hostBytes;
    REQUIRE(slot.tryReserve());
    constexpr auto charged=sizeof(DriverStorage)+256;
    REQUIRE(reserveOpenCLHostBytes(charged));
    std::shared_ptr<DriverStorage> storage;
    try {storage=std::make_shared<DriverStorage>(slot,destroyed,charged);}
    catch(...) {releaseOpenCLHostBytes(charged);slot.release();throw;}
    std::weak_ptr<DriverStorage> weak=storage;
    auto* borrowed=storage.get();
    const auto seeds=storage->upload;auto originals=seeds;
    storage->proof.enqueue(0,true); // In-order async final read accepted.
    CHECK(slot.quarantine(storage));storage.reset();
    // Failed status/drain does not constitute proof or release host addresses.
    for(unsigned attempt=0;attempt<3;++attempt)CHECK_FALSE(slot.tryReap(&DriverStorage::poll));
    REQUIRE_FALSE(weak.expired());CHECK(destroyed==0);CHECK_FALSE(slot.tryReserve());
    CHECK(openCLStatus().hostBytes==baseline+charged);
    ++continuations;originals[1]=88; // Exactly one CPU recovery owns originals.
    // Driver finishes later and accesses only retained transaction endpoints.
    CHECK(borrowed->upload==seeds);CHECK(borrowed->descriptors[2]==33);
    borrowed->readback[1]=99;borrowed->flags[0]=0;borrowed->eventComplete=true;
    CHECK(originals[1]==88);CHECK(continuations==1);
    CHECK(slot.tryReap(&DriverStorage::poll));CHECK(weak.expired());CHECK(destroyed==1);
    CHECK_FALSE(slot.busy());CHECK(openCLStatus().hostBytes==baseline);
}
