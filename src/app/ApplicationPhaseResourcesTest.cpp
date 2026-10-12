// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ApplicationPhaseResources.h"
#include <stdexcept>
#include <limits>
using namespace resource_policy;
TEST_SUITE("ApplicationPhaseResources") {
TEST_CASE("independent owner phases retain allocation and ignore simulation CPU mandates") {
    PhaseResourceInputs input; input.capabilities.allocatedCpuParticipants=6; input.capabilities.threadsSupported=true;
    input.intent.computeParticipants=12; input.intent.explicitProfile=ExecutionProfile::Luxurious;
    input.intent.constraints.batterySaver=true; input.currentPresentation.frameRate=60;
    const auto before=input.intent; const auto owner=resolveOwnerPhase(input);
    CHECK(owner.resources.computeParticipants==1); CHECK(owner.capabilities.allocatedCpuParticipants==6);
    CHECK(owner.requestedProfile==ExecutionProfile::Luxurious); CHECK(owner.profileTuningPending);
    CHECK(owner.request.constraints.batterySaver==true); CHECK(owner.resources.render.frameRate==60);
    CHECK(input.intent==before);
}
TEST_CASE("background workers include owner and retain caller defaults instead of machine heuristics") {
    PhaseResourceInputs input; input.capabilities.allocatedCpuParticipants=6; input.capabilities.threadsSupported=true;
    CHECK(backgroundWorkers(resolveBackgroundPhase(input,2))==2); // Live picker default.
    CHECK(backgroundWorkers(resolveBackgroundPhase(input,4))==4); // Legacy auto fallback cap.
    input.capabilities.allocatedCpuParticipants=1;
    CHECK(backgroundWorkers(resolveBackgroundPhase(input,4))==0);
    input.capabilities.allocatedCpuParticipants.reset();
    CHECK(backgroundWorkers(resolveBackgroundPhase(input,4))==0);
    input.capabilities.allocatedCpuParticipants=6; input.capabilities.threadsSupported=false;
    CHECK(backgroundWorkers(resolveBackgroundPhase(input,2))==0);
    CHECK_THROWS_AS(resolveBackgroundPhase(input,2,2),std::invalid_argument);
}
TEST_CASE("typed FPS overrides respect pacer integer range without forbidding uncapped zero") {
    PhaseResourceInputs input; input.capabilities.allocatedCpuParticipants=6; input.capabilities.threadsSupported=true;
    input.intent.renderFrameRate=std::numeric_limits<unsigned>::max();
    CHECK_THROWS_AS(resolveOwnerPhase(input),std::invalid_argument);
    CHECK_THROWS_AS(resolveBackgroundPhase(input,2),std::invalid_argument);
    input.intent.renderFrameRate=0;
    CHECK(resolveOwnerPhase(input).resources.render.frameRate==0);
    input.intent.renderFrameRate=unsigned(std::numeric_limits<int>::max());
    CHECK(resolveOwnerPhase(input).resources.render.frameRate==unsigned(std::numeric_limits<int>::max()));
}
TEST_CASE("asset admission and IO budgets remain separate from soft planning and recoverable memory") {
    PhaseResourceInputs input; input.capabilities.allocatedCpuParticipants=6; input.capabilities.threadsSupported=true;
    input.intent.planningMemoryBytes=4096;
    const auto asset=resolveAssetPhase(input,{5,2,65536});
    CHECK(asset.plan.resources.computeParticipants==6); CHECK(asset.options.cpuThreads==3);
    CHECK(asset.options.ioThreads==2); CHECK(asset.options.memoryBytes==65536);
    CHECK(asset.plan.resources.planningMemoryBytes==4096); CHECK(asset.plan.resources.recoverableMemoryBytes==0);
    const auto cooperative=resolveAssetPhase(input,{5,2,65536},0);
    CHECK(cooperative.options.cpuThreads==0); CHECK(cooperative.options.ioThreads==0);
    CHECK_THROWS_AS(resolveAssetPhase(input,{1,1,0}),std::invalid_argument);
    for(const unsigned allocation : {1u,2u,6u}) {
        input.capabilities.allocatedCpuParticipants=allocation;
        const auto bounded=resolveAssetPhase(input,{5,2,65536});
        CHECK(1+bounded.options.cpuThreads+bounded.options.ioThreads<=allocation);
    }
    input.capabilities.allocatedCpuParticipants.reset();
    const auto unknown=resolveAssetPhase(input,{5,2,65536});
    CHECK(unknown.options.cpuThreads==0); CHECK(unknown.options.ioThreads==0);
    input.capabilities.allocatedCpuParticipants=6;
    const auto manual=resolveAssetPhase(input,{5,2,65536},8,2);
    CHECK(manual.plan.resources.computeParticipants==11);
    CHECK(manual.options.cpuThreads==8); CHECK(manual.options.ioThreads==2);
    input.capabilities.allocatedCpuParticipants=1;
    const auto disableIo=resolveAssetPhase(input,{1,2,65536},std::nullopt,0);
    CHECK(disableIo.options.cpuThreads==0); CHECK(disableIo.options.ioThreads==0);
    CHECK(disableIo.plan.resources.computeParticipants==1);
    CHECK_THROWS_AS(resolveAssetPhase(input,{1,2,65536},std::nullopt,2),std::invalid_argument);
    input.capabilities.allocatedCpuParticipants=6;
    const auto cpuOnly=resolveAssetPhase(input,{5,2,65536},8);
    CHECK(cpuOnly.options.cpuThreads==8); CHECK(cpuOnly.options.ioThreads==0);
}
TEST_CASE("explicit asset roles are validated before cooperative normalization") {
    PhaseResourceInputs input; input.capabilities.allocatedCpuParticipants=6; input.capabilities.threadsSupported=true;
    CHECK_THROWS_AS(resolveAssetPhase(input,{1,2,65536},0,2),std::invalid_argument);
    const auto cpuZero=resolveAssetPhase(input,{1,2,65536},0);
    CHECK(cpuZero.options.cpuThreads==0); CHECK(cpuZero.options.ioThreads==0);
    const auto ioZero=resolveAssetPhase(input,{1,2,65536},std::nullopt,0);
    CHECK(ioZero.options.cpuThreads==1); CHECK(ioZero.options.ioThreads==0);
    // A legacy non-explicit cooperative request still suppresses default IO.
    const auto implicit=resolveAssetPhase(input,{0,2,65536});
    CHECK(implicit.options.cpuThreads==0); CHECK(implicit.options.ioThreads==0);
    input.capabilities.threadsSupported=false;
    // Defaults model environment() after platform clamping; raw positives are
    // now passed separately by startup rather than relabelled explicit zero.
    CHECK_THROWS_AS(resolveAssetPhase(input,{0,0,65536},8),std::invalid_argument);
    CHECK_THROWS_AS(resolveAssetPhase(input,{0,0,65536},std::nullopt,2),std::invalid_argument);
    const auto serialZero=resolveAssetPhase(input,{0,0,65536},0,0);
    CHECK(serialZero.options.cpuThreads==0); CHECK(serialZero.options.ioThreads==0);
    CHECK(serialZero.plan.resources.computeParticipants==1);
}

TEST_CASE("asset IO hard limit applies independently of threaded CPU support") {
    PhaseResourceInputs input; input.capabilities.allocatedCpuParticipants=6;
    input.capabilities.threadsSupported=true; input.assetIoWorkerLimit=0;
    CHECK_THROWS_AS(resolveAssetPhase(input,{4,2,65536},4,2),std::invalid_argument);
    const auto automatic=resolveAssetPhase(input,{4,2,65536});
    CHECK(automatic.options.cpuThreads==4); CHECK(automatic.options.ioThreads==0);
    CHECK(automatic.plan.resources.computeParticipants==5);
    const auto zeroIo=resolveAssetPhase(input,{4,2,65536},4,0);
    CHECK(zeroIo.options.cpuThreads==4); CHECK(zeroIo.options.ioThreads==0);
    CHECK(zeroIo.plan.resources.computeParticipants==5);
    input.assetIoWorkerLimit=1;
    CHECK_THROWS_AS(resolveAssetPhase(input,{4,2,65536},4,2),std::invalid_argument);
    CHECK(resolveAssetPhase(input,{4,2,65536}).options.ioThreads==1);
    input.assetIoWorkerLimit.reset();
    const auto native=resolveAssetPhase(input,{4,2,65536},4,2);
    CHECK(native.options.cpuThreads==4); CHECK(native.options.ioThreads==2);
    CHECK(native.plan.resources.computeParticipants==7); // Explicit oversub retained.
}

}
