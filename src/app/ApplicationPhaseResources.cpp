// SPDX-License-Identifier: GPL-3.0-or-later
#include "ApplicationPhaseResources.h"
#include <limits>
#include <algorithm>
#include <stdexcept>
namespace resource_policy {
namespace {
ExecutionRequest phaseRequest(const PhaseResourceInputs &input) {
    auto request=input.intent;
    // Simulation CPU/GPU overrides do not mandate work in independent UI phases.
    request.computeParticipants.reset(); request.computeGpu.reset();
    return request;
}
WorkloadRequirements phaseWorkload(const PhaseResourceInputs &input) {
    WorkloadRequirements workload;
    workload.defaults.render=input.currentPresentation;
    return workload;
}
ResourcePlan resolvePhase(const ExecutionRequest &request,const DeviceCapabilities &device,const WorkloadRequirements &workload) {
    auto plan=resolveResourcePlan(request,device,workload);
    // The application pacer takes int. Zero is valid uncapped, not a policy max.
    if(plan.resources.render.frameRate>unsigned(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Application render FPS exceeds supported integer range");
    return plan;
}
unsigned participants(unsigned workers) {
    if(workers==std::numeric_limits<unsigned>::max()) throw std::invalid_argument("Phase worker count overflow");
    return workers+1; // Owner finalizes/polls in addition to background CPU workers.
}
}
ResourcePlan resolveOwnerPhase(const PhaseResourceInputs &input) {
    auto workload=phaseWorkload(input); workload.maximumComputeParticipants=1;
    return resolvePhase(phaseRequest(input),input.capabilities,workload);
}
ResourcePlan resolveBackgroundPhase(const PhaseResourceInputs &input,unsigned validatedWorkers,
                                    std::optional<unsigned> explicitWorkers) {
    auto request=phaseRequest(input); auto workload=phaseWorkload(input);
    workload.defaults.computeParticipants=participants(validatedWorkers);
    if(explicitWorkers) request.computeParticipants=participants(*explicitWorkers);
    return resolvePhase(request,input.capabilities,workload);
}
unsigned backgroundWorkers(const ResourcePlan &plan) {
    if(!plan.resources.computeParticipants) throw std::invalid_argument("Phase owner is missing");
    return plan.resources.computeParticipants-1;
}
AssetPhaseResources resolveAssetPhase(const PhaseResourceInputs &input,GAGCore::AssetLoader::Options options,
                                     std::optional<unsigned> explicitCpu,std::optional<unsigned> explicitIo) {
    if(!options.memoryBytes) throw std::invalid_argument("Asset admission bytes must be positive");
    if(explicitCpu) options.cpuThreads=*explicitCpu;
    if(explicitIo) options.ioThreads=*explicitIo;
    if(input.assetIoWorkerLimit) {
        if(explicitIo && *explicitIo>*input.assetIoWorkerLimit)
            throw std::invalid_argument("Explicit asset IO exceeds supported platform limit");
        options.ioThreads=std::min(options.ioThreads,*input.assetIoWorkerLimit);
    }
    // Preserve explicit intent before the legacy cooperative normalization.
    if(explicitIo && *explicitIo && !options.cpuThreads)
        throw std::invalid_argument("Explicit asset IO needs a decoder");
    if(!options.cpuThreads) options.ioThreads=0; // Existing non-explicit cooperative contract.
    const auto desired=std::uint64_t(options.cpuThreads)+options.ioThreads;
    if(desired>=std::numeric_limits<unsigned>::max()) throw std::invalid_argument("Asset participant count overflow");
    const auto automatic=resolveBackgroundPhase(input,unsigned(desired));
    const auto budget=backgroundWorkers(automatic);
    if(explicitCpu || explicitIo) {
        const auto reserved=std::uint64_t(explicitCpu.value_or(0))+explicitIo.value_or(0);
        const auto remaining=reserved<budget ? unsigned(budget-reserved) : 0;
        if(!explicitCpu) options.cpuThreads=std::min(options.cpuThreads,remaining);
        if(!explicitIo) options.ioThreads=std::min(options.ioThreads,remaining);
        if(!options.cpuThreads && options.ioThreads)
            throw std::invalid_argument("Explicit asset IO needs a decoder within the allocation or an explicit CPU override");
    } else {
        options.ioThreads=budget ? std::min(options.ioThreads,budget-1) : 0;
        options.cpuThreads=budget-options.ioThreads;
    }
    const auto used=std::uint64_t(options.cpuThreads)+options.ioThreads;
    if(used>=std::numeric_limits<unsigned>::max()) throw std::invalid_argument("Asset participant count overflow");
    const auto plan=resolveBackgroundPhase(input,unsigned(desired),
        explicitCpu || explicitIo ? std::optional<unsigned>(unsigned(used)) : std::nullopt);
    return {plan,options}; // Planning memory does not alter admission or retained payloads.
}
} // namespace resource_policy
