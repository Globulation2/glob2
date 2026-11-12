// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ClaimRoute.h"
#include "field/astar/AStar.h"
#include "ResourceClaimRouteView.h"
#include "ClaimPolicy.h"
#include "ComputeExecutor.h"
#include "sim/snapshot/WorldSnapshot.h"
#include <bit>
#include <deque>
#include <cstdlib>
#include <memory>
#include <optional>

namespace ResourceClaims
{
// Records a claim route query for tools/astar_benchmark.py when
// GLOB2_ASTAR_CAPTURE is set (field/astar/AStarCapture.h); otherwise nothing.
void captureRouteQuery(const RouteDomain& domain, const astar::Query& query, int windowSide);

struct RouteIdentity
{
    Owner unit = 0;
    std::uint64_t task = 0, request = 0;
    bool operator==(const RouteIdentity&) const = default;
};
struct RouteInput
{
    SimulationSnapshot::Handle world;
    RouteProfile profile;
    // The route ends on any cell within one tile (toroidal Chebyshev) of
    // target, the claimed access tile, where the worker can pick up.
    std::uint32_t start = 0, target = 0, maximumCost = 65533;
    std::uint32_t maximumExpansions = Policy::RouteMaximumExpansions;
};
struct RoutePublication
{
    RouteIdentity identity;
    std::uint64_t due = 0;
    std::uint32_t start = 0, target = 0;
    Route route;
    std::uint32_t width = 0, height = 0;
};

// Fixed-boundary publication. Completion and exportPending join computation but
// do not publish early. Submit all decisions for one tick, then flush once to
// start one background batch. Export/save belongs at an observation boundary:
// it seals that source tick, just as an early publish/flush does. The consumer
// checks task identity and each step.
class RoutePipeline
{
    struct Work;
    struct Group
    {
        std::vector<Work*> jobs;
        ComputeExecutor::Batch batch;
    };
    struct Work
    {
        RouteInput input;
        RoutePublication output;
        std::shared_ptr<Group> group;
        std::exception_ptr error;
        bool canceled = false; // Owner-only; the worker does not read this flag.
        bool restored = false;
    };
    ComputeExecutor& executor;
    std::deque<std::unique_ptr<Work>> pending;
    std::uint64_t nextRequest = 0;
    std::size_t maximumPending;
    std::optional<std::uint64_t> lastSubmission;
    std::optional<std::uint64_t> sealedTick;

    static void run(void* context, std::size_t index) noexcept
    {
        auto& work = *static_cast<Group*>(context)->jobs[index];
        try {
            const auto& input = work.input;
            const auto view = input.world.view();
            // Consistent A* heuristic: octile distance priced at the cheapest
            // cardinal and diagonal entry costs among the terrain rules present
            // on this map for the unit's movement class (the engine never
            // reopens a cell), computed outside the hot loop and cached per
            // thread until the terrain changes.
            unsigned cardinal=0, diagonal=0;
            {
                // A publication lease is immutable, but BufferPool reuses its
                // object/control block after consumers release it. Copy version
                // values: reading them through an old weak lease sees the refill.
                struct Minima {
                    std::weak_ptr<const SimulationSnapshot::Terrain> terrain;
                    std::weak_ptr<const CellRuleTable> rules;
                    std::array<Uint64,4> version{};
                    int width=0,height=0;
                    std::array<unsigned,MOVEMENT_CLASS_COUNT> cardinal{},diagonal{};
                };
                thread_local Minima cache;
                const std::array<Uint64,4> version{input.world.worldIdentity,input.world.configurationRevision,
                    input.world.mapGenerations[0],input.world.terrain->revision};
                if (cache.terrain.lock()!=input.world.terrain || cache.version!=version
                    || cache.rules.lock()!=input.world.terrain->rules || cache.width!=view.width || cache.height!=view.height) {
                    cache.terrain=input.world.terrain;cache.rules=input.world.terrain->rules;
                    cache.version=version;cache.width=view.width;cache.height=view.height;
                    cache.cardinal.fill(std::numeric_limits<unsigned>::max());
                    cache.diagonal.fill(std::numeric_limits<unsigned>::max());
                    std::vector<bool> present(view.rules->size());
                    for (const auto rule:input.world.terrain->cellRules) present[rule]=true;
                    for (unsigned rule=0;rule<present.size();++rule) if(present[rule]) {
                        const auto& entry=(*view.rules)[rule];
                        for (int movement=0;movement<MOVEMENT_CLASS_COUNT;++movement)
                            if (gradient_kernel::terrainAllowsMovement(entry.properties,movement)) {
                                cache.cardinal[movement]=std::min(cache.cardinal[movement],unsigned(entry.ground[movement].cardinal));
                                cache.diagonal[movement]=std::min(cache.diagonal[movement],unsigned(entry.ground[movement].diagonal));
                            }
                    }
                }
                cardinal=cache.cardinal[input.profile.movement]; diagonal=cache.diagonal[input.profile.movement];
                if(cardinal==std::numeric_limits<unsigned>::max()) cardinal=diagonal=0;
            }
            thread_local astar::Workspace scratch;
            astar::Query query;
            query.width = view.width; query.height = view.height;
            query.start = input.start; query.target = input.target;
            query.goalRadius = 1; // routes end within one tile of the target, where pickup happens
            query.maximumCost = input.maximumCost;
            query.maximumExpansions = input.maximumExpansions;
            query.floor = astar::consistentFloor(cardinal, diagonal);
            query.wantPath = true;
            const RouteDomain domain{view, input.profile};
            auto found = scratch.search(domain, query);
            captureRouteQuery(domain, query, scratch.windowSide());
            auto& route = work.output.route;
            static_assert(int(RouteStatus::ExpansionLimit) == int(astar::Status::ExpansionLimit));
            route.status = RouteStatus(found.status);
            route.cost = found.cost; route.expansions = found.expansions;
            route.cells = std::move(found.cells);
        } catch (...) { work.error = std::current_exception(); }
        work.input.world = {}; // Drop the immutable lease after computation.
    }
public:
    std::uint64_t requestSequence() const { return nextRequest; }
    std::optional<std::uint64_t> sealedSubmissionTick() const { return sealedTick; }
    void restoreControl(std::uint64_t value, std::optional<std::uint64_t> tick)
    {
        if (value < nextRequest || bool(value) != tick.has_value() ||
            (tick && (*tick >= std::numeric_limits<std::uint64_t>::max() / 2 || (sealedTick && *tick != *sealedTick))))
            throw std::invalid_argument("invalid saved route control state");
        nextRequest = value; sealedTick = tick; lastSubmission = tick;
    }
    explicit RoutePipeline(ComputeExecutor& value, std::size_t capacity = 4096) : executor(value), maximumPending(capacity)
    { if (!capacity) throw std::invalid_argument("claim route capacity must be positive"); }
    RoutePipeline(const RoutePipeline&) = delete;
    ~RoutePipeline()
    {
        for (auto& work : pending) {
            try { if (work->group) executor.join(work->group->batch, {JoinReason::Teardown, ComputeTelemetry::NoTick}); }
            catch (...) {} // Work remains alive until every submitted callback ends.
        }
    }
    RouteIdentity submit(Owner unit, std::uint64_t task, std::uint64_t tick, RouteInput input)
    {
        if (!admissionAvailable()) throw std::length_error("claim route pending limit");
        if (lastSubmission && tick < *lastSubmission) throw std::invalid_argument("claim routes require monotonic submission ticks");
        if (sealedTick && tick <= *sealedTick) throw std::logic_error("claim route tick is already flushed");
        if (!pending.empty() && pending.front()->output.due <= tick)
            throw std::logic_error("publish due claim routes before admitting the next tick");
        if (tick >= std::numeric_limits<std::uint64_t>::max() / 2) throw std::overflow_error("claim route deadline");
        const auto cells = std::size_t(input.world.width) * input.world.height;
        // The search engine wraps with masks: map sides are powers of two.
        if (input.world.width <= 0 || input.world.height <= 0 || !std::has_single_bit(unsigned(input.world.width)) ||
            !std::has_single_bit(unsigned(input.world.height)) || input.start >= cells || input.target >= cells ||
            !input.world.terrain || !input.world.terrain->rules || !input.world.occupancy || !input.world.areas || !input.world.resources ||
            !input.world.catalogs || !input.world.catalogs->resources ||
            input.profile.movement < 0 || input.profile.movement >= MOVEMENT_CLASS_COUNT || !input.maximumExpansions)
            throw std::invalid_argument("claim route needs complete immutable map inputs");
        if (input.world.terrain->cellRules.size() != cells || input.world.resources->cells.size() != cells ||
            input.world.occupancy->cells.size() != cells || input.world.areas->cells.size() != cells)
            throw std::invalid_argument("claim route map shape mismatch");
        if (nextRequest == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("claim route request identity");
        auto work = std::make_unique<Work>();
        work->input = std::move(input);
        work->output = {{unit, task, ++nextRequest}, tick + 1, work->input.start, work->input.target, {}};
        work->output.width = unsigned(work->input.world.width); work->output.height = unsigned(work->input.world.height);
        auto* raw = work.get();
        // Staging does not spend an executor slot per unit.
        pending.push_back(std::move(work));
        lastSubmission = tick;
        return raw->output.identity;
    }
    void flush()
    {
        // Publication before the next tick bounds this to one new source tick.
        auto group = std::make_shared<Group>();
        for (auto& work : pending) if (!work->group && !work->restored) group->jobs.push_back(work.get());
        if (group->jobs.empty()) return;
        const auto due = group->jobs.front()->output.due;
        for (const auto* work : group->jobs)
            if (work->output.due != due) throw std::logic_error("unpublished claim route horizon");
        // Freeze the callback's entire vector and retain it before submission.
        for (auto* work : group->jobs) work->group = group;
        const ComputeExecutor::Group batch{group->jobs.size(), {run, group.get()}, ComputeExecutor::NoLane};
        try {
            group->batch = executor.submit(std::span(&batch, 1), ComputeExecutor::boundaryDue(due),
                {ComputeProducer::Claims, due - 1, due});
        } catch (...) { for (auto* work : group->jobs) work->group.reset(); throw; }
        sealedTick = due - 1;
    }
    bool admissionAvailable() const { return pending.size() < maximumPending; }
    void cancel(Owner unit)
    { for (auto& work : pending) if (work->output.identity.unit == unit) work->canceled = true; }
    std::vector<RoutePublication> publish(std::uint64_t tick)
    {
        flush();
        std::vector<RoutePublication> result;
        while (!pending.empty() && pending.front()->output.due <= tick) {
            auto& work = *pending.front();
            if (work.group) executor.join(work.group->batch, {JoinReason::TickDeadline, Uint32(tick)});
            if (work.error) std::rethrow_exception(work.error);
            if (!work.canceled) result.push_back(std::move(work.output));
            pending.pop_front();
        }
        return result;
    }
    std::vector<RoutePublication> exportPending()
    {
        flush();
        std::vector<RoutePublication> result;
        for (auto& work : pending) {
            if (work->group) executor.join(work->group->batch, {JoinReason::Save, ComputeTelemetry::NoTick});
            if (work->error) std::rethrow_exception(work->error);
            if (!work->canceled) result.push_back(work->output);
        }
        return result;
    }
    void restore(std::vector<RoutePublication> saved)
    {
        if (!pending.empty()) throw std::logic_error("restore claim routes into an empty pipeline");
        std::optional<std::uint64_t> previousDue;
        for (auto& output : saved) {
            const auto cells = std::uint64_t(output.width) * output.height;
            if (!output.due || output.due > std::numeric_limits<std::uint64_t>::max() / 2 ||
                !output.identity.request || !output.width || !output.height ||
                output.width > unsigned(std::numeric_limits<int>::max()) || output.height > unsigned(std::numeric_limits<int>::max()) ||
                cells > std::numeric_limits<std::uint32_t>::max() || output.start >= cells || output.target >= cells ||
                (previousDue && output.due != *previousDue) ||
                (output.route.status != RouteStatus::Found && output.route.status != RouteStatus::Unreachable && output.route.status != RouteStatus::CostLimit && output.route.status != RouteStatus::ExpansionLimit) ||
                (output.route.status != RouteStatus::Found && !output.route.cells.empty()))
                throw std::invalid_argument("invalid saved claim route deadlines");
            const field::Grid grid(int(output.width), int(output.height));
            auto previous = output.start;
            for (const auto cell : output.route.cells) {
                if (cell >= cells) throw std::invalid_argument("saved claim route cell out of bounds");
                bool adjacent = false;
                grid.neighborIndices(int(previous), field::Surrounding, [&](int next) { adjacent |= unsigned(next) == cell; });
                if (!adjacent) throw std::invalid_argument("saved claim route discontinuity");
                previous = cell;
            }
            previousDue = output.due;
        }
        if (saved.size() > maximumPending) throw std::length_error("saved claim route pending limit");
        for (auto& output : saved) {
            nextRequest = std::max(nextRequest, output.identity.request);
            lastSubmission = output.due - 1;
            sealedTick = output.due - 1;
            auto work = std::make_unique<Work>(); work->output = std::move(output);
            work->restored = true;
            pending.push_back(std::move(work)); // Saved outputs need no computation.
        }
    }
};
}
