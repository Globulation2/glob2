// SPDX-License-Identifier: GPL-3.0-or-later
// Temporary bounded whole-game device experiment. Never substitutes a fake device.
#include "EngineFixtures.h"
#include "GradientRuntime.h"
#include "field/GradientAccelerators.h"
#include "map/claims/ClaimPolicyConfig.h"
#include "map/claims/ResourceClaimProbe.h"
#include <nlohmann/json.hpp>
#include <zlib.h>
#include <sstream>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

TEST_CASE("actual device material publications preserve whole game execution" * doctest::test_suite("ClaimGPUContinuation"))
{
    using namespace gradient_kernel;
    const char* inputPath=std::getenv("GLOB2_CLAIM_GPU_INITIAL");
    const char* selected=std::getenv("GLOB2_CLAIM_GPU_BACKEND");
    if(!inputPath || !selected) { MESSAGE("Diagnostic requires frozen GUI save and explicit GPU backend environment"); return; }
    const std::string name=selected;
    REQUIRE((name=="opencl" || name=="vulkan"));
    const auto target=name=="opencl"?Backend::OpenCL:Backend::Vulkan;
    struct Restore {
        Configuration applied=configuration(), requested=requestedBackendConfiguration();
        ~Restore(){applyBackendConfiguration(applied);requestBackendConfiguration(requested);}
    } restore;
    glob2test::HeadlessGlobals globals;
    REQUIRE(initializeGradientAccelerator(target)); // Explicit diagnostic must not silently skip unavailable hardware.
    const auto status=acceleratorStatus(target);
    std::ofstream metadata(glob2test::artifactDir()/(name+"-device.txt"));
    metadata<<status.device<<'\n'<<status.driver<<'\n'<<status.shaderIdentity<<'\n';
    REQUIRE(metadata.good());
    struct Tick {
        Uint32 sum;
        std::vector<Uint32> world,buildings,units;
        std::vector<std::string> orders;
        bool operator==(const Tick&) const = default;
    };
    const auto run=[&](Backend mode) {
        applyBackendConfiguration({mode,mode==Backend::CPU?Algorithm::Buckets:Algorithm::Frozen8});
        requestBackendConfiguration(configuration());
        glob2test::HeadlessGame fixture;
        auto& game=fixture.game; auto& map=game.map;
        map.clear();map.resourceCells.clear();map.configureCompute(3);
        FILE* file=std::fopen(inputPath,"rb"); REQUIRE(file);
        GAGCore::BinaryInputStream stream(new GAGCore::FileStreamBackend(file));
        REQUIRE(fixture.gui.load(&stream,true));
        REQUIRE(game.mapHeader.getNumberOfTeams()==2);
        REQUIRE(map.cellCount()==4096);
        REQUIRE(map.materialDestinationsEnabled());
        REQUIRE(map.materialClaimPolicyConfig().usePrimaryGradient);
        REQUIRE_FALSE(game.gameHeader.isResourceGrowthDisabled());
        std::shared_ptr<ResourceClaims::Probe> probe;
        std::string probePath;
        if(std::getenv("GLOB2_CLAIM_GPU_REQUIRE_RAW")) {
            REQUIRE(map.materialClaimPolicyConfig().sourceVersion==4);
            REQUIRE(map.materialClaimPolicyConfig().directResourceProbes);
            ResourceClaims::Probe::Config options;options.sampleEvery=UINT32_MAX;options.maxSamples=1;
            probePath=(glob2test::artifactDir()/((mode==Backend::CPU?std::string("cpu"):name)+"-raw.jsonl.gz")).string();
            probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),probePath,options);
            map.setMaterialClaimProbe(probe);
        }
        auto& rt=*map.gradientRuntime;
        struct Counts {
            std::mutex mutex;
            // Retain the exact published label allocation, so allocator reuse cannot impersonate a device commit.
            std::map<std::shared_ptr<OwnedDestinations>,unsigned,std::owner_less<std::shared_ptr<OwnedDestinations>>> successes;
            unsigned attempted=0,executed=0,published=0,labeledPublished=0;
        };
        auto counts=std::make_shared<Counts>();
        if(mode!=Backend::CPU) {
            map.finishGradientPipeline();
            rt.deviceService=std::make_shared<GradientDeviceService>(GradientDeviceService::Hooks{
                [mode]{return initializeGradientAccelerator(mode);},
                [counts](std::span<const BackendRequest> requests,Plan plan) {
                    std::vector<bool> candidates;
                    for(const auto& r:requests) candidates.push_back(bool(r.destinations) && !alreadyFixedGradient({r.gradient,r.grid.cells()}));
                    const bool result=executeGradientAccelerator(requests,plan);
                    std::lock_guard lock(counts->mutex);
                    for(std::size_t i=0;i<requests.size();++i) if(candidates[i]) {
                        ++counts->attempted;
                        const auto& r=requests[i];
                        if(r.executedOnDevice && *r.executedOnDevice) {
                            ++counts->executed;
                            const auto* owned=static_cast<const OwnedGradientField*>(r.context);
                            if(counts->successes.size()>=4096) throw std::runtime_error("GPU evidence allocation budget exceeded");
                            counts->successes.emplace(owned->destinations,1);
                        }
                    }
                    return result;
                }});
            rt.deviceService->configure(3,mode);
            rt.pipeline.setDeviceService(rt.deviceService);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(!rt.deviceService->metrics().ready && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
            REQUIRE(rt.deviceService->metrics().ready);
        }
        const auto original=rt.pipeline.onPublished;
        rt.pipeline.onPublished=[counts,original](GradientPipeline::Job& job) {
            if(original) original(job);
            if(job.destinations) {
                std::lock_guard lock(counts->mutex); ++counts->labeledPublished;
                if(counts->successes.erase(job.destinations)) ++counts->published;
            }
        };
        const auto stockHash=[&] {
            std::uint64_t hash=1469598103934665603ull;
            for(std::size_t i=0;i<map.cellCount();++i) for(unsigned m=0;m<MaterialCount;++m)
                hash=(hash^map.materialAmountAtSlot(i,m))*1099511628211ull;
            return hash;
        };
        const auto initialStock=stockHash();
        std::uint64_t claimedTicks=0;
        std::vector<Tick> trace;trace.reserve(1024);
        std::ofstream output(glob2test::artifactDir()/((mode==Backend::CPU?std::string("cpu"):name)+"-trace.tsv"));
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
        for(unsigned tick=0;tick<1024;++tick) {
            REQUIRE(std::chrono::steady_clock::now()<deadline);
            Tick row{};row.orders=glob2test::stepAI(game);
            claimedTicks+=map.materialClaimLedger().size()!=0;
            // Inspect every live map cell without joining tomorrow's work early:
            // heavy Game checksums also force pending GPU cancellation/CPU fallback.
            row.sum=game.checkSum(&row.world,&row.buildings,&row.units,false);
            row.world.push_back(map.checkSum(true,false));
            output<<game.stepCounter<<'\t'<<row.sum;
            for(const auto* values:{&row.world,&row.buildings,&row.units}) {output<<'\t';for(auto value:*values) output<<value<<',';}
            output<<'\n'; trace.push_back(std::move(row));
        }
        map.finishGradientPipeline();
        REQUIRE(output.good());
        CHECK(claimedTicks>0);
        CHECK(stockHash()!=initialStock);
        if(probe) {
            map.finishMaterialClaimProbe();
            gzFile file=gzopen(probePath.c_str(),"rb");REQUIRE(file);
            std::string text;char buffer[4096];int count;
            while((count=gzread(file,buffer,sizeof(buffer)))>0) {
                text.append(buffer,count);REQUIRE(text.size()<=4*1024*1024);
            }
            REQUIRE(count==0);REQUIRE(gzclose(file)==Z_OK);
            std::istringstream lines(text);std::string line;nlohmann::json footer;
            while(std::getline(lines,line))footer=nlohmann::json::parse(line);
            Uint64 checks=0,improvements=0,finalSelections=0;
            for(const auto& row:footer.at("raw_resource_probes").at("material_counters")) {checks+=row.at(3).get<Uint64>();improvements+=row.at(4).get<Uint64>();finalSelections+=row.at(5).get<Uint64>();}
            REQUIRE(checks>0);metadata<<"raw_unique_candidate_checks="<<checks<<"\nraw_provisional_improvements="<<improvements<<"\nraw_final_selections="<<finalSelections<<'\n';
        }
        metadata<<"backend="<<(mode==Backend::CPU?"cpu":name)<<" claimed_ticks="<<claimedTicks
                <<" initial_stock_hash="<<initialStock<<" final_stock_hash="<<stockHash()<<'\n';
        if(mode!=Backend::CPU) {
            std::lock_guard lock(counts->mutex);
            metadata<<"nontrivial_labeled_attempts="<<counts->attempted<<"\nnontrivial_labeled_device_successes="<<counts->executed
                    <<"\nactual_matching_device_publications="<<counts->published<<"\nall_labeled_publications="<<counts->labeledPublished<<'\n';
            CHECK(counts->published>0);
        }
        return trace;
    };
    const auto cpu=run(Backend::CPU), gpu=run(target);
    REQUIRE(cpu.size()==gpu.size());
    for(std::size_t i=0;i<cpu.size();++i) { CAPTURE(i); REQUIRE(cpu[i]==gpu[i]); }
}
