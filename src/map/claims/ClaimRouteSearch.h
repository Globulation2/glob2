// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "field/Grid.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace ResourceClaims
{
enum class RouteStatus { Found, Unreachable, CostLimit, ExpansionLimit };
struct Route
{
    RouteStatus status = RouteStatus::Unreachable;
    std::vector<std::uint32_t> cells; // Excludes start, includes terminal stance.
    std::uint32_t cost = 0, expansions = 0;
};

// Private worker scratch, reused without clearing full-map arrays each request.
// All inputs are immutable callbacks; this search never reads a live Map.
class RouteSearch
{
    struct Node { std::uint32_t cost = 0, parent = 0, stamp = 0; };
    using Entry = std::tuple<std::uint64_t, std::uint32_t, std::uint32_t>; // f, g, row-major index
    std::vector<Node> nodes;
    std::vector<Entry> open;
    std::uint32_t generation = 0;
public:
    // An admissible heuristic and positive edge costs yield the exact shortest
    // route, including reopening nodes if a supplied heuristic is inconsistent.
    template<class Passable, class StepCost, class Goal, class Heuristic>
    Route find(field::Grid grid, std::uint32_t start, std::uint32_t maximumCost,
        Passable passable, StepCost stepCost, Goal goal, Heuristic heuristic,
        std::uint32_t maximumExpansions = std::numeric_limits<std::uint32_t>::max())
    {
        if (start >= grid.cells()) throw std::out_of_range("claim route start");
        if (nodes.size() != grid.cells()) { nodes.assign(grid.cells(), {}); generation = 0; }
        if (++generation == 0) { for (auto& node : nodes) node.stamp = 0; generation = 1; }
        open.clear();
        const auto push = [&](std::uint32_t index, std::uint32_t cost) {
            open.emplace_back(std::uint64_t(cost) + heuristic(index), cost, index);
            std::push_heap(open.begin(), open.end(), std::greater<Entry>{});
        };
        nodes[start] = {0, start, generation}; push(start, 0);
        Route result;
        bool limited = false;
        while (!open.empty()) {
            std::pop_heap(open.begin(), open.end(), std::greater<Entry>{});
            const auto [estimate, cost, index] = open.back(); open.pop_back();
            if (nodes[index].stamp != generation || nodes[index].cost != cost) continue;
            if (result.expansions >= maximumExpansions) { result.status = RouteStatus::ExpansionLimit; return result; }
            ++result.expansions;
            if (goal(index)) {
                result.status = RouteStatus::Found; result.cost = cost;
                for (auto at = index; at != start; at = nodes[at].parent) result.cells.push_back(at);
                std::reverse(result.cells.begin(), result.cells.end());
                return result;
            }
            const int x = int(index % grid.width()), y = int(index / grid.width());
            for (const auto direction : field::Surrounding) {
                const auto next = std::uint32_t(grid.index(x + direction.x, y + direction.y));
                if (!passable(next)) continue;
                const auto step = stepCost(next, direction.x != 0 && direction.y != 0);
                if (!step) throw std::invalid_argument("claim route requires positive edge costs");
                if (std::uint64_t(cost) + step > maximumCost) { limited = true; continue; }
                const auto candidate = cost + step;
                auto& node = nodes[next];
                if (node.stamp == generation && node.cost <= candidate) continue;
                node = {candidate, index, generation}; push(next, candidate);
            }
        }
        result.status = limited ? RouteStatus::CostLimit : RouteStatus::Unreachable;
        return result;
    }
};
}
