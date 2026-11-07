// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Material.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
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
// One worker's reservation of one unit of a material. source is the cell whose
// stock the claim draws on; access is the cell the worker picks up at. They
// differ for a pooled farm field, where a worker harvests at the edge it reaches
// while the stock comes from the field's ripest connected tile. exclusive marks
// a harvest that consumes or destroys the whole deposit (no other claimant);
// reserve is the seed floor a pooled finite source keeps for regrowth.
struct Claim
{
    std::uint32_t source = 0, access = 0;
    std::uint8_t material = 0;
    std::uint16_t resource = 0;
    bool exclusive = false;
    std::uint16_t reserve = 0;
    bool operator==(const Claim&) const = default;
};

// The terms a claim on one material of one resource type carries, given where
// it is picked up (Map::claimTerms). Selection, load and revalidation must all
// derive them the same way: load and revalidation compare stored claims
// against recomputed terms.
struct ClaimTerms
{
    bool exclusive = false; // The harvest consumes or destroys the whole deposit.
    bool pooled = false;    // Picked up in a farm field that pools its stock.
    bool infinite = false;  // The yield never depletes.
    std::uint16_t reserve = 0; // Seed floor kept back from a pooled finite yield.
};

// Bookkeeping, mutated on the owner thread only. The hire-time target pass
// reads it concurrently from compute workers (available, find), while the
// owner waits for that pass and so mutates nothing. No availability decision
// depends on hash iteration order. Capacity/eligibility come from the current
// world, never stale gradients.
class Ledger
{
    struct Tile
    {
        std::array<std::uint32_t, MaterialCount> materials{};
        std::array<std::uint16_t,MaterialCount> reserves{};
        std::array<std::uint32_t,MaterialCount> reserveOwners{};
        std::uint32_t total = 0, exclusive = 0;
        std::vector<Owner> owners;
    };
    struct Record {
        Claim claim;
        std::size_t sourcePosition=0,accessPosition=0;
    };
    std::size_t cells_ = 0;
    std::unordered_map<Owner, Record> owners_;
    std::unordered_map<std::uint32_t, Tile> tiles_;
    std::unordered_map<std::uint32_t,std::vector<Owner>> accesses_;

    void validate(const Claim& claim) const
    {
        if (claim.source >= cells_ || claim.access >= cells_)
            throw std::out_of_range("resource claim tile");
        if (claim.material >= MaterialCount) throw std::out_of_range("resource claim material");
    }
    void remove(const Record& record)
    {
        const auto& claim=record.claim;
        auto found = tiles_.find(claim.source);
        auto& tile = found->second;
        const Owner movedSource=tile.owners.back();
        tile.owners[record.sourcePosition]=movedSource;
        owners_.at(movedSource).sourcePosition=record.sourcePosition;
        tile.owners.pop_back();
        auto access=accesses_.find(claim.access);
        const Owner movedAccess=access->second.back();
        access->second[record.accessPosition]=movedAccess;
        owners_.at(movedAccess).accessPosition=record.accessPosition;
        access->second.pop_back();
        if (access->second.empty()) accesses_.erase(access);
        --tile.total; --tile.materials[claim.material];
        const auto material=claim.material;
        if (claim.reserve && claim.reserve==tile.reserves[material] && !--tile.reserveOwners[material]) {
            tile.reserves[material]=0;
            // Only the last holder of a positive maximum requires a scan.
            // Packed source owners already exclude the removed reservation.
            for (const auto owner:tile.owners) {
                const auto& remaining=owners_.at(owner).claim;
                if (remaining.material!=material || !remaining.reserve) continue;
                if (remaining.reserve>tile.reserves[material]) {
                    tile.reserves[material]=remaining.reserve;tile.reserveOwners[material]=1;
                } else if (remaining.reserve==tile.reserves[material]) ++tile.reserveOwners[material];
            }
        }
        tile.exclusive -= claim.exclusive;
        if (!tile.total) tiles_.erase(found);
    }
    void add(Owner owner,Record& record)
    {
        const auto& claim=record.claim;
        auto& tile = tiles_[claim.source];
        auto& access=accesses_[claim.access];
        record.sourcePosition=tile.owners.size(); tile.owners.push_back(owner);
        record.accessPosition=access.size(); access.push_back(owner);
        ++tile.total; ++tile.materials[claim.material];
        if (claim.reserve>tile.reserves[claim.material]) {
            tile.reserves[claim.material]=claim.reserve;tile.reserveOwners[claim.material]=1;
        } else if (claim.reserve && claim.reserve==tile.reserves[claim.material]) ++tile.reserveOwners[claim.material];
        tile.exclusive += claim.exclusive;
    }
public:
    void reset(std::size_t cells)
    {
        if (cells > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("resource claim map too large");
        cells_ = cells; owners_.clear(); tiles_.clear(); accesses_.clear();
    }
    std::size_t size() const { return owners_.size(); }
    const Claim* find(Owner owner) const
    {
        const auto it = owners_.find(owner);
        return it == owners_.end() ? nullptr : &it->second.claim;
    }
    // Detached sorted IDs allow mutation callbacks to release/retarget claims
    // without invalidating iteration. Only owners touching this tile are read;
    // sorting makes callback order independent of packed swap-removal order.
    std::vector<Owner> ownersAtSource(std::uint32_t source) const
    {
        const auto found=tiles_.find(source);
        if (found==tiles_.end()) return {};
        auto result=found->second.owners;
        std::sort(result.begin(),result.end());
        return result;
    }
    std::vector<Owner> ownersAtAccess(std::uint32_t access) const
    {
        const auto found=accesses_.find(access);
        if (found==accesses_.end()) return {};
        auto result=found->second;
        std::sort(result.begin(),result.end());
        return result;
    }
    std::uint32_t materialCount(std::uint32_t source, unsigned material) const
    {
        if (material >= MaterialCount) throw std::out_of_range("resource claim material");
        const auto it = tiles_.find(source);
        return it == tiles_.end() ? 0 : it->second.materials[material];
    }
    // Capacity is this candidate's spendable stock (physical minus its reserve).
    // Reservations for one material share the largest seed floor, including
    // holders approaching through a different farm or an ordinary pickup.
    // Infinite yields carry reserve zero and bypass finite stock/floor limits.
    // Same-material claimants (excluding this owner's replaced claim) must stay
    // below spendable, the stock above the shared floor: one claimant per unit.
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
        if (infinite) return true;
        const auto ownMaterial = unsigned(ownTile && previous->material == claim.material);
        auto otherReserve=tile->second.reserves[claim.material];
        // The old reservation is replaced, not added. Same-floor retries stay
        // constant time; lowering the sole maximum is a rare transfer case.
        if (ownMaterial && previous->reserve>claim.reserve && previous->reserve==otherReserve
            && tile->second.reserveOwners[claim.material]==1) {
            otherReserve=0;
            for (const auto other:tile->second.owners) {
                const auto& held=owners_.at(other).claim;
                if (other!=owner && held.material==claim.material) otherReserve=std::max(otherReserve,held.reserve);
            }
        }
        const std::uint64_t physical=std::uint64_t(capacity)+claim.reserve;
        const auto floor=std::max(otherReserve,claim.reserve);
        if (physical<floor) return false;
        const std::uint64_t spendable=physical-floor;
        return std::uint64_t(tile->second.materials[claim.material]-ownMaterial)+1<=spendable;
    }
    // Record without the capacity check. acquire uses it after checking; the
    // load path uses it directly to keep claims saved under different rules
    // until the scheduled rule-change invalidation releases them.
    bool record(Owner owner, const Claim& claim)
    {
        validate(claim);
        auto found = owners_.find(owner);
        if (found != owners_.end() && found->second.claim == claim) return false;
        if (found != owners_.end()) remove(found->second);
        auto& record=owners_.insert_or_assign(owner,Record{claim}).first->second;
        add(owner,record);
        return true;
    }
    bool acquire(Owner owner, const Claim& claim, std::uint32_t capacity, bool infinite)
    {
        if (!available(owner, claim, capacity, infinite)) return false;
        record(owner, claim);
        return true;
    }
    bool release(Owner owner)
    {
        const auto found = owners_.find(owner);
        if (found == owners_.end()) return false;
        remove(found->second); owners_.erase(found);
        return true;
    }
    // Sorting also supplies a deterministic save/rebuild and invariant-audit view.
    std::vector<std::pair<Owner, Claim>> entries() const
    {
        std::vector<std::pair<Owner, Claim>> result; result.reserve(owners_.size());
        for (const auto& [owner,record]:owners_) result.emplace_back(owner,record.claim);
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        return result;
    }
};
}
