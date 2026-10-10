// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientCosts.h"
#include "AdaptiveGradientPolicy.h"
#include "Grid.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <utility>

namespace gradient_kernel
{
// The owner must keep the complete cost input immutable. Retaining it prevents
// pooled storage/address reuse; variant identifies movement class/cost semantics.
// Without an identity accelerators must compare actual cost contents.
struct CostIdentity
{
    std::shared_ptr<const void> owner;
    std::uint64_t variant = 0, revision = 0;
    // True only when costAt is valid for every cell, including forbidden ones.
    // Such planes depend on terrain/movement alone, not the field's obstacles.
    bool allCells = false;
};
struct BackendRequest
{
    std::uint16_t *gradient;
    int limit;
    field::Grid grid;
    BackendSession &session;
    void *context;
    EntrySteps (*costAt)(void *, std::size_t);
    void (*cpu)(void *, std::uint16_t *);
    CostIdentity identity;
    Family family = Family::Generic;
    // Optional caller-owned executor for a complete, ready CPU batch.
    void (*cpuBatch)(std::span<const BackendRequest* const>, std::span<std::uint16_t* const>) = nullptr;
    Operation operation = Operation::CompleteField;
    void (*failure)(void*,std::exception_ptr) = nullptr;
    unsigned cpuBuckets = 0; // zero means unknown, never inferred by scanning
};
// An accelerator must leave the seed buffer untouched when returning false.
// Registered by the optional native implementation; absent in standalone users.
inline bool (*accelerator)(const BackendRequest &, Plan) = nullptr;
// Explicit scheduler batches own all contexts until this synchronous call ends.
inline bool (*batchAccelerator)(std::span<const BackendRequest>, Plan) = nullptr;
inline bool canBatch(const BackendSession &session)
{
    const auto choice = backend();
    return batchAccelerator && choice != Backend::CPU && !session.failed.load(std::memory_order_relaxed);
}

// The shared layer selects once. Backends execute exactly that plan or decline
// without touching seeds. No selected plan invokes a comparison or tournament.
// A direct group has at most eight requests with one family, session and
// operation. Invalid groups throw before execution or telemetry; empty is a no-op.
// Dimensions, movement costs and propagation caps may differ within a group.
inline void executeGradientGroup(std::span<const BackendRequest> requests, Backend mode)
{
    if(requests.empty()) return;
    if (requests.size() > BATCH_CATEGORIES)
        throw std::invalid_argument("Gradient group exceeds eight requests");
    const auto& first=requests.front();
    for (const auto& request : requests) {
        validateFamily(request.family);
        if (request.family != first.family || &request.session != &first.session ||
            request.operation != first.operation)
            throw std::invalid_argument("Gradient group requires one family, session and operation");
    }
    const bool eligible=std::all_of(requests.begin(),requests.end(),[](const auto& request) {
        return request.operation==Operation::CompleteField && request.limit>=0;
    });
    const auto chosen=eligible ? first.session.choose(first.family,requests.size(),mode) : PlanDecision{};
    const bool sample=eligible && first.session.sample();
    GradientObservation observation;
    const auto started=sample ? monotonicNs() : 0;
    if(sample) {
        observation.decision=chosen; observation.family=first.family; observation.batch=unsigned(requests.size());
        observation.width=first.grid.width(); observation.height=first.grid.height(); observation.limit=first.limit;
        observation.movement=first.identity.variant; observation.costRevision=first.identity.revision;
        observation.cpuBuckets=first.cpuBuckets; observation.threads=unsigned(ComputeExecutor::executionThreads());
    }
    StageTimingScope stageScope(sample ? &observation.stages : nullptr);
    bool handled=false;
    if(chosen.plan!=Plan::CPU) {
        if(requests.size()==1 && accelerator) handled=accelerator(first,chosen.plan);
        else if(batchAccelerator) handled=batchAccelerator(requests,chosen.plan);
    }
    if(!handled) {
        observation.decision.plan=Plan::CPU;
        if(first.cpuBatch && std::all_of(requests.begin(),requests.end(),[&](const auto& r){return r.cpuBatch==first.cpuBatch;})) {
            std::array<const BackendRequest*,8> pointers{};
            std::array<std::uint16_t*,8> outputs{};
            for(std::size_t i=0;i<requests.size();++i) { pointers[i]=&requests[i]; outputs[i]=requests[i].gradient; }
            first.cpuBatch(std::span(pointers.data(),requests.size()),std::span(outputs.data(),requests.size()));
        }
        else for(const auto& r:requests) {
            try { r.cpu(r.context,r.gradient); }
            catch(...) { if(r.failure) r.failure(r.context,std::current_exception()); else throw; }
        }
    }
    if(sample) {
        const auto completed=monotonicNs();
        const auto* job=JobTiming::current;
        observation.hasQueue=job && job->submitted;
        const auto executionStart=observation.hasQueue ? job->started : started;
        observation.executionNs=completed-executionStart;
        observation.queueNs=observation.hasQueue ? job->started-job->submitted : 0;
        observation.serviceNs=observation.executionNs+observation.queueNs;
        observation.seedPreparationNs=job ? job->preparation : 0;
        first.session.record(observation);
    }
}
inline void executeGradientBatch(std::span<const BackendRequest> input, Backend mode)
{
    for (const auto& request : input) validateFamily(request.family);
    // Batches are explicitly ready; no gathering, retained inputs or optional
    // dependency. Group only adjacent requests sharing semantic eligibility.
    for(std::size_t begin=0;begin<input.size();) {
        std::size_t end=begin+1;
        while(end<input.size() && end-begin<8 && input[end].family==input[begin].family &&
              &input[end].session==&input[begin].session && input[end].operation==input[begin].operation) ++end;
        executeGradientGroup(input.subspan(begin,end-begin),mode); begin=end;
    }
}

template <class Costs, class CPU>
bool tryAcceleratedGradient(std::uint16_t *gradient, int maxCost, field::Grid grid, BackendSession &session,
                            Costs costs, CPU cpu, CostIdentity identity = {}, Family family = Family::Generic)
{
    const auto choice = backend();
    if (maxCost < 0) return false;
    struct Context
    {
        Costs costs;
        CPU cpu;
    } context{costs, cpu};
    const BackendRequest request{gradient,
                                 std::min(maxCost, COST_LIMIT),
                                 grid,
                                 session,
                                 &context,
                                 [](void *p, std::size_t i) { return static_cast<Context *>(p)->costs(i); },
                                 [](void *p, std::uint16_t *out) { static_cast<Context *>(p)->cpu(out); },
                                 std::move(identity), family, nullptr};
    executeGradientGroup(std::span(&request,1),choice);
    return true;
}
} // namespace gradient_kernel
