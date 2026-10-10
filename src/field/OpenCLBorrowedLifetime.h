// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <memory>

namespace gradient_kernel
{
// One pre-admitted optional state. The state owns this slot's parent/runtime
// lifetime and releases admission in its destructor, after borrowed storage.
// A quarantined state is retained without allocating a failure-path container.
// Only a positively successful completion proof may remove that retention.
// Negative/error/unknown completion cannot be used as a drain proof.
class OpenCLBorrowedLifetime
{
    std::atomic<bool> occupied{false};
    std::atomic<std::shared_ptr<void>> retained;
public:
    OpenCLBorrowedLifetime()=default;
    OpenCLBorrowedLifetime(const OpenCLBorrowedLifetime&)=delete;
    bool tryReserve() noexcept {
        bool expected=false;
        return occupied.compare_exchange_strong(expected,true,std::memory_order_acq_rel);
    }
    // Exactly one constructor-failure guard or admitted-state destructor calls
    // release. Quarantine itself never releases admission or its payload lease.
    void release() noexcept {occupied.store(false,std::memory_order_release);}
    bool busy() const noexcept {return occupied.load(std::memory_order_acquire);}
    bool quarantined() const noexcept {return bool(retained.load(std::memory_order_acquire));}
    // Requires the reserved state. Repeated retention of the same owner is
    // idempotent; a second state cannot be admitted while occupied is true.
    bool quarantine(const std::shared_ptr<void>& owner) noexcept {
        if(!owner || !busy())return false;
        std::shared_ptr<void> expected;
        if(retained.compare_exchange_strong(expected,owner,std::memory_order_acq_rel))return true;
        return expected.get()==owner.get() && !expected.owner_before(owner) && !owner.owner_before(expected);
    }
    // Coordinator/background only. The callback must preserve the payload on
    // failure and must not block waiting for an unresponsive driver. Actual
    // integration polls an existing trailing event; no Finish is implied.
    bool tryReap(bool (*proof)(void*) noexcept) noexcept {
        auto owner=retained.load(std::memory_order_acquire);
        if(!owner || !proof || !proof(owner.get()))return false;
        std::shared_ptr<void> expected=owner;
        return retained.compare_exchange_strong(expected,{},std::memory_order_acq_rel);
        // owner keeps the graph alive until after the callback/CAS; its final
        // destruction releases admission only if no external handle remains.
    }
};
}
