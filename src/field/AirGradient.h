// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "GradientCosts.h"
#include "GradientWorkspace.h"
#include "Grid.h"
#include "map/gradient/GradientExecutionAudit.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>

namespace field::air
{
inline constexpr std::uint32_t NoGoal=std::numeric_limits<std::uint32_t>::max();
struct Traversal
{
    Grid grid;
    std::span<const std::uint8_t> passable;
    std::span<const gradient_kernel::EntrySteps> costs;
};
struct Goal { std::uint32_t cell,rank,cost=0; };
enum class GoalPolicy { Exact, ApproachBlocked };
enum class SourcePolicy { RequirePassable, LeaveBlocked };
enum class Direction { ToDestination, FromSource };
struct Sample { bool reachable=false; std::uint32_t cost=0,rank=NoGoal; };

inline gradient_kernel::EntrySteps compileSteps(unsigned cardinal)
{
    const std::uint64_t diagonal=std::uint64_t(cardinal)*GRADIENT_DIAGONAL_STEP/GRADIENT_STEP;
    if (!cardinal || cardinal>=256 || !diagonal || diagonal>=256)
        throw std::invalid_argument("Air edge does not fit a supported gradient bucket ring");
    return {cardinal,unsigned(diagonal)};
}
inline std::optional<std::uint32_t> decode(std::uint16_t value)
{
    if (value<=GRADIENT_UNREACHABLE) return {};
    const auto cost=std::uint32_t(GRADIENT_AT_GOAL-value);
    return cost<=unsigned(gradient_kernel::COST_LIMIT) ? std::optional(cost) : std::nullopt;
}
inline std::uint32_t ceilTiles(std::uint32_t cost)
{
    return std::uint32_t((std::uint64_t(cost)+GRADIENT_STEP-1)/GRADIENT_STEP);
}
inline unsigned wrappedDelta(int a,int b,int size)
{
    const auto difference=unsigned(a>b ? a-b : b-a);
    return std::min(difference,unsigned(size)-difference);
}
inline unsigned wrappedChebyshevTiles(const Grid& grid,int x,int y,int tx,int ty)
{
    return std::max(wrappedDelta(grid.wrapX(x),grid.wrapX(tx),grid.width()),
        wrappedDelta(grid.wrapY(y),grid.wrapY(ty),grid.height()));
}
inline std::uint64_t wrappedSquaredDistance(const Grid& grid,int x,int y,int tx,int ty)
{
    const std::uint64_t dx=wrappedDelta(grid.wrapX(x),grid.wrapX(tx),grid.width());
    const std::uint64_t dy=wrappedDelta(grid.wrapY(y),grid.wrapY(ty),grid.height());
    return dx*dx+dy*dy;
}

// Returns the existing supported ring selected once, outside propagation.
inline unsigned validate(const Traversal& traversal)
{
    if (traversal.grid.cells()>std::size_t(std::numeric_limits<int>::max())
        || traversal.passable.size()!=traversal.grid.cells() || traversal.costs.size()!=traversal.grid.cells())
        throw std::invalid_argument("Air traversal plane size");
    unsigned largest=0;
    for (std::size_t i=0;i<traversal.grid.cells();++i) if (traversal.passable[i]) {
        const auto cost=traversal.costs[i];
        if (!cost.cardinal || !cost.diagonal || cost.cardinal>=256 || cost.diagonal>=256)
            throw std::invalid_argument("Air traversal edge out of range");
        largest=std::max({largest,cost.cardinal,cost.diagonal});
    }
    return largest<64 ? 64 : largest<128 ? 128 : 256;
}

// Complete destination-rooted CPU field. Positive edges let ordinary Dial
// buckets carry equal-cost label improvements too: all incoming improvements
// arrive before their destination's layer is expanded. Labels follow caller
// rank, never flat cell index or worker/queue order. Existing ground kernels
// retain their specialized scalar/SIMD implementations unchanged.
inline void build(const Traversal& traversal,std::span<const Goal> goals,
    std::span<std::uint16_t> output,GradientWorkspace& workspace,
    std::span<std::uint32_t> labels={},GoalPolicy policy=GoalPolicy::Exact,
    Direction direction=Direction::ToDestination)
{
    GradientExecutionAudit::record(GradientExecutionAudit::Kind::AirFieldInitialization);
    const unsigned ring=validate(traversal);
    const auto cells=traversal.grid.cells();
    if (output.size()!=cells || (!labels.empty() && labels.size()!=cells))
        throw std::invalid_argument("Air output plane size");
    if (!workspace.terrain) workspace.terrain=std::make_unique<TerrainGradientWorkspace>();
    workspace.terrain->prepare(ring);
    auto& buckets=workspace.terrain->buckets;
    for (auto& bucket:buckets) bucket.clear();
    workspace.deferredSeeds.clear();
    for (std::size_t i=0;i<cells;++i) {
        output[i]=traversal.passable[i] ? GRADIENT_UNREACHABLE : GRADIENT_FORBIDDEN;
        if (!labels.empty()) labels[i]=NoGoal;
    }
    const auto seed=[&](std::size_t cell,const Goal& goal) {
        if ((!traversal.passable[cell] && direction!=Direction::FromSource)
            || goal.cost>unsigned(gradient_kernel::COST_LIMIT)) return;
        const auto value=std::uint16_t(GRADIENT_AT_GOAL-goal.cost);
        if (output[cell]<value || (output[cell]==value && !labels.empty() && goal.rank<labels[cell])) {
            output[cell]=value;
            if (!labels.empty()) labels[cell]=goal.rank;
        }
    };
    for (const auto goal:goals) {
        if (goal.cell>=cells || goal.rank==NoGoal) throw std::invalid_argument("Invalid air goal");
        if (direction==Direction::FromSource || traversal.passable[goal.cell]) seed(goal.cell,goal);
        else if (policy==GoalPolicy::ApproachBlocked)
            traversal.grid.neighborIndices(goal.cell,Surrounding,[&](int cell) { seed(cell,goal); });
    }
    // Nonzero approach seeds can start many ring rotations apart. Enroll them
    // at their own layer rather than aliasing an earlier bucket rotation.
    for (std::size_t i=0;i<cells;++i) if (const auto cost=decode(output[i]))
        workspace.deferredSeeds.emplace_back(int(*cost),int(i));
    std::sort(workspace.deferredSeeds.begin(),workspace.deferredSeeds.end());
    GradientExecutionAudit::record(GradientExecutionAudit::Kind::AirFieldExpansion);
    std::size_t pending=0,nextSeed=0;
    unsigned current=0;
    while (pending || nextSeed<workspace.deferredSeeds.size()) {
        if (!pending) current=unsigned(workspace.deferredSeeds[nextSeed].first);
        while (nextSeed<workspace.deferredSeeds.size() && unsigned(workspace.deferredSeeds[nextSeed].first)==current) {
            const auto cell=workspace.deferredSeeds[nextSeed++].second;
            if (output[cell]==GRADIENT_AT_GOAL-current) { buckets[current%ring].push(cell); ++pending; }
        }
        auto& bucket=buckets[current%ring];
        for (std::size_t entry=0;entry<bucket.size;++entry) {
            const auto cell=bucket.cells[entry];
            --pending;
            if (output[cell]!=GRADIENT_AT_GOAL-current) continue;
            // Reverse u<-v charges entry(v), including its diagonal price.
            const auto steps=traversal.costs[cell];
            const int x=cell%traversal.grid.width(),y=cell/traversal.grid.width();
            for (const auto offset:Surrounding) {
                const auto neighbor=traversal.grid.index(x+offset.x,y+offset.y);
                if (!traversal.passable[neighbor]) continue;
                const auto entrySteps=direction==Direction::ToDestination ? steps : traversal.costs[neighbor];
                const unsigned candidate=current+(offset.x && offset.y ? entrySteps.diagonal : entrySteps.cardinal);
                if (candidate>unsigned(gradient_kernel::COST_LIMIT)) continue;
                const auto value=std::uint16_t(GRADIENT_AT_GOAL-candidate);
                const bool tie=output[neighbor]==value && !labels.empty() && labels[cell]<labels[neighbor];
                if (output[neighbor]<value || tie) {
                    output[neighbor]=value;
                    if (!labels.empty()) labels[neighbor]=labels[cell];
                    buckets[candidate%ring].push(neighbor); ++pending;
                }
            }
        }
        bucket.clear();
        ++current;
    }
}

// The blocked-source exception inspects only eight neighbors. It cannot extend
// a frontier or turn a pending/missing plane into an unavailable result.
inline Sample sample(const Traversal& traversal,std::span<const std::uint16_t> values,
    std::size_t source,SourcePolicy policy=SourcePolicy::RequirePassable,
    std::span<const std::uint32_t> labels={})
{
    if (values.size()!=traversal.grid.cells() || traversal.passable.size()!=values.size()
        || traversal.costs.size()!=values.size() || source>=values.size()
        || (!labels.empty() && labels.size()!=values.size()))
        throw std::invalid_argument("Invalid air field sample");
    Sample best;
    const auto consider=[&](std::size_t cell,unsigned extra) {
        const auto cost=decode(values[cell]);
        if (!cost || extra>unsigned(gradient_kernel::COST_LIMIT)-*cost) return;
        const unsigned candidate=*cost+extra;
        const auto rank=labels.empty() ? NoGoal : labels[cell];
        if (!best.reachable || candidate<best.cost || (candidate==best.cost && rank<best.rank))
            best={true,candidate,rank};
    };
    if (traversal.passable[source]) consider(source,0);
    else if (policy==SourcePolicy::LeaveBlocked) {
        const int x=source%traversal.grid.width(),y=source/traversal.grid.width();
        for (const auto offset:Surrounding) {
            const auto cell=traversal.grid.index(x+offset.x,y+offset.y);
            if (traversal.passable[cell])
                consider(cell,offset.x && offset.y ? traversal.costs[cell].diagonal : traversal.costs[cell].cardinal);
        }
    }
    return best;
}
}
