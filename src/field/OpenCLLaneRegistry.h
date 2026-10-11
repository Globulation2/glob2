// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <memory>

namespace gradient_kernel
{
// Caller holds its registry mutex. Payload allocations must be separate from
// shared control blocks: an expired weak entry retains only bounded control
// metadata, covered by the parent's fixed registry lease.
template<class T,std::size_t Capacity> class OpenCLLaneRegistry
{
    struct Entry {
        std::shared_ptr<const void> controlCredit; // Destroyed after weak block.
        std::weak_ptr<T> lane;
    };
    std::array<Entry,Capacity> slots{};
public:
    OpenCLLaneRegistry()=default;
    OpenCLLaneRegistry(const OpenCLLaneRegistry&)=delete;
    OpenCLLaneRegistry& operator=(const OpenCLLaneRegistry&)=delete;
    void clear() noexcept {for(auto& slot:slots){slot.lane.reset();slot.controlCredit.reset();}}
    void clearExpired() noexcept {for(auto& slot:slots)if(slot.lane.expired()){slot.lane.reset();slot.controlCredit.reset();}}
    bool insert(const std::shared_ptr<T>& lane,std::shared_ptr<const void> credit) noexcept {
        for(auto& slot:slots)if(slot.lane.expired()){
            slot.lane.reset();slot.controlCredit.reset();
            slot.controlCredit=std::move(credit);slot.lane=lane;return true;
        }
        return false;
    }
    std::array<std::shared_ptr<T>,Capacity> snapshot() const noexcept {
        std::array<std::shared_ptr<T>,Capacity> live{};
        for(std::size_t i=0;i<Capacity;++i)live[i]=slots[i].lane.lock();
        return live;
    }
};
}
