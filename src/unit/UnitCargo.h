// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MaterialPacket.h"
#include <map>
#include <vector>

struct WideMaterialPacket
{
    Uint64 numerator=1, denominator=1;
    bool operator==(const WideMaterialPacket&) const = default;
};
struct WideMaterialDeliveryResult
{
    Sint32 acceptedStock=0;
    WideMaterialPacket residual{0,1};
};
struct UnitCargoEntry
{
    Sint32 material = -1;
    WideMaterialPacket packet{};
    bool operator==(const UnitCargoEntry&) const = default;
};

// Only additional packets live here. The ordinary one-packet inventory stays
// inline in UnitState and never allocates or probes this store during a tick.
// GIDs include the team; conversion explicitly moves the inventory to its new
// identity. A value copy is sufficient for snapshot ownership.
class UnitCargoStore
{
public:
    using Inventory = std::vector<UnitCargoEntry>;
    const Inventory* find(Uint16 gid) const {
        const auto it=entries_.find(gid);
        return it==entries_.end()?nullptr:&it->second;
    }
    Inventory* find(Uint16 gid) {
        const auto it=entries_.find(gid);
        return it==entries_.end()?nullptr:&it->second;
    }
    Inventory& overflow(Uint16 gid) { return entries_[gid]; }
    void erase(Uint16 gid) { entries_.erase(gid); }
    void clear() { entries_.clear(); }
    bool empty() const { return entries_.empty(); }
    std::size_t capacityBytes() const {
        std::size_t result=entries_.size()*(sizeof(std::pair<const Uint16,Inventory>)+3*sizeof(void*));
        for (const auto& [gid,inventory]:entries_) result+=inventory.capacity()*sizeof(UnitCargoEntry);
        return result;
    }
    void transfer(Uint16 from, Uint16 to) {
        if (from==to) return;
        auto node=entries_.extract(from);
        entries_.erase(to);
        if (!node.empty()) { node.key()=to; entries_.insert(std::move(node)); }
    }
    const auto& entries() const { return entries_; }
private:
    std::map<Uint16,Inventory> entries_;
};
