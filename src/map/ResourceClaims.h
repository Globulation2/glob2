// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Material.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace ResourceClaims
{
using Owner = std::uint64_t;
// Script identities are generations local to a reusable GID, not global IDs.
// Include both so different live slots and successive occupants never alias.
constexpr Owner unitOwner(std::uint16_t gid, std::uint32_t generation)
{ return (Owner(generation) << 16) | gid; }
struct Claim
{
    std::uint32_t source = 0, access = 0;
    std::uint8_t material = 0;
    std::uint16_t resource = 0;
    bool exclusive = false;
    bool operator==(const Claim&) const = default;
};

// One byte per cell on the common path. Values of 255 use the sparse exact
// totals below; saturation alone would lose information on the next release.
struct Counts
{
    std::vector<std::uint8_t> cells;
    std::map<std::uint32_t, std::uint32_t> overflow;
    std::uint32_t at(std::size_t index) const
    {
        const auto count = cells.at(index);
        if (count != 255) return count;
        const auto found = overflow.find(std::uint32_t(index));
        return found == overflow.end() ? 255 : found->second;
    }
};

// Owner-thread bookkeeping. No availability decisions depend on hash iteration
// order. Capacity/eligibility come from the current world, never stale gradients.
class Ledger
{
    struct Tile
    {
        std::array<std::uint32_t, MaterialCount> materials{};
        std::uint32_t total = 0, exclusive = 0;
    };
    Counts counts_;
    std::unordered_map<Owner, Claim> owners_;
    std::unordered_map<std::uint32_t, Tile> tiles_;

    void validate(const Claim& claim) const
    {
        if (claim.source >= counts_.cells.size() || claim.access >= counts_.cells.size())
            throw std::out_of_range("resource claim tile");
        if (claim.material >= MaterialCount) throw std::out_of_range("resource claim material");
    }
    void updateCount(std::uint32_t source, std::uint32_t total)
    {
        counts_.cells[source] = std::uint8_t(std::min(total, std::uint32_t(255)));
        if (total > 255) counts_.overflow[source] = total;
        else counts_.overflow.erase(source);
    }
    void remove(const Claim& claim)
    {
        auto found = tiles_.find(claim.source);
        auto& tile = found->second;
        --tile.total; --tile.materials[claim.material];
        tile.exclusive -= claim.exclusive;
        updateCount(claim.source, tile.total);
        if (!tile.total) tiles_.erase(found);
    }
    void add(const Claim& claim)
    {
        auto& tile = tiles_[claim.source];
        ++tile.total; ++tile.materials[claim.material];
        tile.exclusive += claim.exclusive;
        updateCount(claim.source, tile.total);
    }
public:
    void reset(std::size_t cells)
    {
        if (cells > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("resource claim map too large");
        counts_.cells.assign(cells, 0); counts_.overflow.clear(); owners_.clear(); tiles_.clear();
    }
    const Counts& counts() const { return counts_; }
    std::size_t size() const { return owners_.size(); }
    const Claim* find(Owner owner) const
    {
        const auto it = owners_.find(owner);
        return it == owners_.end() ? nullptr : &it->second;
    }
    std::uint32_t materialCount(std::uint32_t source, unsigned material) const
    {
        if (material >= MaterialCount) throw std::out_of_range("resource claim material");
        const auto it = tiles_.find(source);
        return it == tiles_.end() ? 0 : it->second.materials[material];
    }
    bool available(Owner owner, const Claim& claim, std::uint32_t capacity, bool infinite = false) const
    {
        validate(claim);
        if (!capacity && !infinite) return false;
        const auto tile = tiles_.find(claim.source);
        if (tile == tiles_.end()) return true;
        const auto* previous = find(owner);
        const bool ownTile = previous && previous->source == claim.source;
        const auto others = tile->second.total - unsigned(ownTile);
        const auto exclusive = tile->second.exclusive - unsigned(ownTile && previous->exclusive);
        if (exclusive || (claim.exclusive && others)) return false;
        const auto ownMaterial = unsigned(ownTile && previous->material == claim.material);
        return infinite || tile->second.materials[claim.material] - ownMaterial < capacity;
    }
    // Shadow observation deliberately allows overcommitment. The same mutation
    // maintains counts once enforcement starts using acquire instead.
    template<class Changed> bool observe(Owner owner, const Claim& claim, Changed&& changed)
    {
        validate(claim);
        auto found = owners_.find(owner);
        if (found != owners_.end() && found->second == claim) return false;
        const auto old = found == owners_.end() ? std::optional<Claim>{} : found->second;
        if (old) remove(*old);
        owners_.insert_or_assign(owner, claim);
        add(claim);
        if (old) changed(old->source);
        if (!old || old->source != claim.source) changed(claim.source);
        return true;
    }
    template<class Changed> bool acquire(Owner owner, const Claim& claim, std::uint32_t capacity,
        bool infinite, Changed&& changed)
    {
        if (!available(owner, claim, capacity, infinite)) return false;
        observe(owner, claim, changed);
        return true;
    }
    template<class Changed> bool release(Owner owner, Changed&& changed)
    {
        const auto found = owners_.find(owner);
        if (found == owners_.end()) return false;
        const auto claim = found->second;
        remove(claim); owners_.erase(found); changed(claim.source);
        return true;
    }
    // Sorting also supplies a deterministic save/rebuild and invariant-audit view.
    std::vector<std::pair<Owner, Claim>> entries() const
    {
        std::vector<std::pair<Owner, Claim>> result(owners_.begin(), owners_.end());
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        return result;
    }
};
}
