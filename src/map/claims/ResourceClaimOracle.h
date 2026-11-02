// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourceClaimRouteView.h"
#include "engine/sim/snapshot/WorldSnapshot.h"
#include <limits>
#include <optional>

namespace ResourceClaims::Oracle
{
inline constexpr Uint64 Unreachable = std::numeric_limits<Uint64>::max();
inline constexpr Uint32 NoTile = std::numeric_limits<Uint32>::max();
struct Query
{
    Owner owner = 0;
    Uint32 origin = 0;
    Uint8 material = 0;
    RouteProfile route;
};
struct Candidate
{
    Uint32 source = NoTile, access = NoTile, stance = NoTile;
    Uint32 component = NoTile; // Connected nondestructive farm, or NoTile.
    Uint64 distance = Unreachable; // Weighted travel to a harvesting stance, no pickup cost.
    Uint64 accessDistance = Unreachable; // Travel to this tile's own pickup access, before pooling.
    Uint32 accessStance = NoTile;
    Uint32 capacity = 0, claimed = 0;
    Uint16 resource = 0;
    bool infinite = false, exclusive = false, available = false, pooled = false;
    bool accessEligible = false; // This source tile itself can be selected as a pickup access.
};
struct Result
{
    // Dense forward distances also score waypoint/path variants without rerunning Dijkstra.
    std::vector<Uint64> distances;
    // One entry per nonempty requested-material source, in ascending tile order.
    std::vector<Candidate> candidates;
    std::optional<std::size_t> best; // Index into candidates, only reachable AND available.
    Uint64 expanded = 0, relaxations = 0;
};

// Temporary shadow evaluator. All inputs must describe the same immutable event
// boundary; it neither changes claims nor consumes any simulation randomness.
// Farm pooling measures the best addressable source in a connected farm, rather
// than pretending the live ripest-first legacy picker already enforces claims.
Result evaluate(const SimulationSnapshot::Handle& snapshot, const Query& query,
                std::span<const std::pair<Owner, Claim>> owners);
}
