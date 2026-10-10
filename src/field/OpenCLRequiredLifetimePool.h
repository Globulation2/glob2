// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OpenCLBorrowedLifetime.h"
#include <array>

namespace gradient_kernel
{
// Independent required lanes pre-admit their own retention slot before commands.
// A transaction pins the parent Device until its chosen slot is released. The
// fixed metadata is charged with Device; payload storage is charged separately.
class OpenCLRequiredLifetimePool
{
public:
    static constexpr std::size_t MaxSlots=64;
    using Slot=OpenCLBorrowedLifetime;
private:
    std::array<Slot,MaxSlots> slots;
    std::atomic_flag reaping=ATOMIC_FLAG_INIT;
public:
    Slot* tryReserve() noexcept {
        for(auto& slot:slots)if(slot.tryReserve())return &slot;
        return nullptr;
    }
    bool quarantined() const noexcept {
        for(const auto& slot:slots)if(slot.quarantined())return true;
        return false;
    }
    // Concurrent background callers never invoke a slot's mutable event proof
    // simultaneously. Losing callers return immediately, without a driver call.
    // Positive proof removes retention; external owners still hold admission.
    bool tryReap(bool (*proof)(void*) noexcept) noexcept {
        if(reaping.test_and_set(std::memory_order_acquire))return false;
        struct Release {std::atomic_flag& flag;~Release(){flag.clear(std::memory_order_release);}} release{reaping};
        bool reaped=false;
        for(auto& slot:slots)if(slot.tryReap(proof))reaped=true;
        return reaped;
    }
};
}
