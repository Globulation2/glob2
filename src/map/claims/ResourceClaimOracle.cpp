// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceClaimOracle.h"
#include <algorithm>
#include <bit>
#include <queue>
#include <stdexcept>
#include <tuple>

namespace ResourceClaims::Oracle
{
namespace
{
struct Access
{
    Uint64 distance = Unreachable;
    Uint32 stance = NoTile, access = NoTile;
    auto key() const { return std::tie(distance, stance, access); }
};
void improve(Access& best, const Access& candidate)
{
    if (candidate.key() < best.key()) best = candidate;
}
}

Result evaluate(const SimulationSnapshot::Handle& snapshot, const Query& query,
                std::span<const std::pair<Owner, Claim>> owners)
{
    if (!snapshot.catalogs || !snapshot.catalogs->resources || !snapshot.terrain ||
        !snapshot.terrain->rules || !snapshot.resources || !snapshot.occupancy ||
        !snapshot.areas || !snapshot.visibility || !snapshot.rules)
        throw std::invalid_argument("claim oracle requires a complete frozen map view");
    if (snapshot.width <= 0 || snapshot.height <= 0 ||
        !std::has_single_bit(unsigned(snapshot.width)) || !std::has_single_bit(unsigned(snapshot.height)) ||
        query.material >= MaterialCount || query.route.movement < 0 || query.route.movement >= MOVEMENT_CLASS_COUNT ||
        !std::has_single_bit(query.route.teamMask))
        throw std::invalid_argument("invalid claim oracle query");
    const auto view = snapshot.view();
    const auto size = std::size_t(snapshot.width)*snapshot.height;
    if (size > NoTile || query.origin >= size || view.resources.size() != size ||
        view.occupancy.size() != size || view.areas.size() != size || view.cellRules.size() != size ||
        snapshot.visibility->visible.size() != size)
        throw std::invalid_argument("invalid claim oracle map shape");
    Ledger ledger; ledger.reset(size);
    std::optional<Owner> previous;
    for (const auto& [owner, claim] : owners) {
        if (previous && owner <= *previous) throw std::invalid_argument("claim oracle owners must be sorted and unique");
        ledger.observe(owner, claim, [](unsigned) {});
        previous = owner;
    }

    Result result;
    result.distances.assign(size, Unreachable);
    result.distances[query.origin] = 0;
    using QueueItem = std::pair<Uint64, Uint32>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;
    queue.emplace(0, query.origin);
    while (!queue.empty()) {
        const auto [distance, index] = queue.top(); queue.pop();
        if (distance != result.distances[index]) continue;
        ++result.expanded;
        const int x = int(index % snapshot.width), y = int(index / snapshot.width);
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            const auto next = Uint32(view.index(x+dx, y+dy));
            if (!routePassable(view, next, query.route)) continue;
            const auto candidate = distance + routeEntryCost(view, next, query.route, dx && dy);
            if (candidate < result.distances[next]) {
                result.distances[next] = candidate;
                queue.emplace(candidate, next);
                ++result.relaxations;
            }
        }
    }

    // Label stock-connected farm components once, not once per candidate.
    const auto pooled = [&](Uint32 index) {
        if (!snapshot.areas->farmEnabled || !(view.areas[index].farm & query.route.teamMask) ||
            !MapState::isFarmableResource(view, view.resources[index].resource.type) ||
            !MapState::materialAmountAt(view, index, query.material)) return false;
        const auto& yield = view.resourceRegistry->yields(static_cast<ResourceId>(view.resources[index].resource.type))[query.material];
        return yield.consumption != ResourceConsumption::All && !yield.destroysDeposit;
    };
    std::vector<Uint32> components(size, NoTile), frontier;
    std::vector<Access> componentAccess;
    for (Uint32 index = 0; index < size; ++index) {
        if (components[index] != NoTile || !pooled(index)) continue;
        const auto component = Uint32(componentAccess.size());
        componentAccess.emplace_back();
        frontier.clear(); frontier.push_back(index); components[index] = component;
        for (std::size_t head = 0; head < frontier.size(); ++head) {
            const auto at = frontier[head];
            const int x = int(at % snapshot.width), y = int(at / snapshot.width);
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const auto next = Uint32(view.index(x+dx, y+dy));
                if (components[next] != NoTile || !pooled(next)) continue;
                components[next] = component; frontier.push_back(next);
            }
        }
    }
    std::vector<Access> access(size);
    for (Uint32 index = 0; index < size; ++index) {
        if (!MapState::materialAmountAt(view, index, query.material) ||
            (view.areas[index].forbidden & query.route.teamMask) ||
            (MapState::resourceVisibleToHarvest(view, index) && !(snapshot.visibility->visible[index] & query.route.teamMask))) continue;
        access[index].access = index;
        const int x = int(index % snapshot.width), y = int(index / snapshot.width);
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const auto stance = Uint32(view.index(x+dx, y+dy));
            if (result.distances[stance] == Unreachable) continue;
            improve(access[index], {result.distances[stance], stance, index});
        }
        if (components[index] != NoTile) improve(componentAccess[components[index]], access[index]);
    }
    const auto* own = ledger.find(query.owner);
    for (Uint32 index = 0; index < size; ++index) {
        const auto amount = MapState::materialAmountAt(view, index, query.material);
        if (!amount) continue;
        Candidate candidate;
        candidate.source = index;
        candidate.resource = view.resources[index].resource.type;
        candidate.pooled = components[index] != NoTile;
        candidate.component = components[index];
        candidate.accessEligible = access[index].access != NoTile;
        candidate.accessDistance = access[index].distance;
        candidate.accessStance = access[index].stance;
        const auto& yield = view.resourceRegistry->yields(static_cast<ResourceId>(candidate.resource))[query.material];
        candidate.infinite = yield.consumption == ResourceConsumption::Infinite;
        candidate.exclusive = yield.consumption == ResourceConsumption::All || yield.destroysDeposit;
        const unsigned reserve = candidate.pooled && !view.resourceGrowthDisabled ? yield.seedReserve : 0;
        candidate.capacity = candidate.exclusive ? 1u : amount > reserve ? amount-reserve : 0u;
        candidate.claimed = ledger.materialCount(index, query.material) -
            unsigned(own && own->source == index && own->material == query.material);
        const auto& destination = candidate.pooled ? componentAccess[components[index]] : access[index];
        candidate.distance = destination.distance;
        candidate.access = destination.access;
        candidate.stance = destination.stance;
        candidate.available = ledger.available(query.owner,
            {index, index, query.material, candidate.resource, candidate.exclusive}, candidate.capacity, candidate.infinite);
        result.candidates.push_back(candidate);
        if (!candidate.available || candidate.distance == Unreachable) continue;
        if (!result.best || std::tie(candidate.distance, candidate.source, candidate.stance, candidate.access) <
            std::tie(result.candidates[*result.best].distance, result.candidates[*result.best].source,
                     result.candidates[*result.best].stance, result.candidates[*result.best].access))
            result.best = result.candidates.size()-1;
    }
    return result;
}
}
