// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MaterialPacket.h"
#include <map>
#include <vector>
#include <numeric>
#include <limits>

struct WideMaterialPacket
{
    Uint64 numerator=1, denominator=1;
    bool operator==(const WideMaterialPacket&) const = default;
};
// Whether a packet contains at least one representable unit of this building's
// stock. Shared by live admission and immutable hiring qualification.
inline bool packetCanDeliver(WideMaterialPacket packet, Uint64 multiplier)
{
    if(!packet.denominator || !multiplier)return false;
    const Uint64 divisor=std::gcd(packet.numerator,packet.denominator);
    packet.numerator/=divisor; packet.denominator/=divisor;
    const Uint64 common=std::gcd(packet.denominator,multiplier);
    const Uint64 scale=multiplier/common;
    const Uint64 maximum=std::numeric_limits<Uint64>::max();
    return packet.numerator<=maximum/scale && packet.denominator<=maximum/scale
        && packet.numerator*scale>=packet.denominator/common;
}
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

// Additional packets live here, along with a primary packet whose exact fraction
// exceeds the inline representation (UnitState::widePrimaryCargo). Ordinary
// one-packet counting and delivery use UnitState directly without a store lookup.
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
