// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourcePolicy.h"
#include <AssetLoader.h>
#include <string>
namespace resource_policy {
// Local snapshots never replace the active simulation session plan.
struct PhaseResourceInputs {
    ExecutionRequest intent;
    DeviceCapabilities capabilities;
    RenderResources currentPresentation;
    // Asset role support is distinct from simulation/GPU capabilities. Zero is
    // a valid hard limit (browser MEMFS reads remain on the application owner).
    std::optional<unsigned> assetIoWorkerLimit;
};
struct AssetPhaseResources {
    ResourcePlan plan;
    GAGCore::AssetLoader::Options options;
    std::optional<std::string> originalCpuEnvironment, originalIoEnvironment;
    // Plan counts owner+CPU workers+IO workers: IO also executes CPU work.
    // IO role and admission bytes remain separately observable.
    // Admission credit is neither recoverable cache nor a total RSS ceiling.
};
ResourcePlan resolveOwnerPhase(const PhaseResourceInputs &);
ResourcePlan resolveBackgroundPhase(const PhaseResourceInputs &, unsigned validatedWorkers,
                                    std::optional<unsigned> explicitWorkers = std::nullopt);
AssetPhaseResources resolveAssetPhase(const PhaseResourceInputs &, GAGCore::AssetLoader::Options,
                                     std::optional<unsigned> explicitCpuWorkers = std::nullopt,
                                     std::optional<unsigned> explicitIoWorkers = std::nullopt);
unsigned backgroundWorkers(const ResourcePlan &);
} // namespace resource_policy
