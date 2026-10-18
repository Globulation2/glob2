// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private Map-owned execution state. Keeping this behind a pointer in Map.h
// prevents queue, thread and scratch-storage details from entering Map's API.
#include "GradientPipeline.h"
#include "BuildingGradientJob.h"
#include "ResourceGrowth.h"
#include "BuildingGradientDepthPolicy.h"
#include "SnapshotGradient.h"
#include "ResourceSeedCache.h"
#include "TerrainSafetyCache.h"
#include "ResourceGradientProbe.h"
#include <cstdlib>
#include "field/GradientWorkspace.h"

#include <vector>
#include <array>
#include <map>
#include <list>
#include "Team.h"
#include "Ressource.h"
#include <unordered_map>
#include <deque>
#include <utility>
#include "Map.h"
#include "ResourceExperiment.h"
#include "BudgetExperiment.h"

struct GradientRuntime
{
    // LOCAL BENCHMARK ONLY: no save/load, replay or network contract.
    bool demandEnabled = std::getenv("GLOB2_EXPERIMENT_RESOURCE_DEMAND8") != nullptr || std::getenv("GLOB2_RESOURCE_REFRESH_MODEL");
    unsigned budgetPeriod=ResourceExperiment::setting("GLOB2_RESOURCE_BUDGET_PERIOD",0);
    int budgetPolicy=int(ResourceExperiment::setting("GLOB2_RESOURCE_BUDGET_POLICY",0));
    int budgetCursor=0;
    std::uint64_t budgetSlots=0,budgetUsed=0,budgetCandidates=0,budgetAgreement=0;
    std::ofstream budgetOutput,workOutput;
    unsigned demandInterval=ResourceExperiment::setting("GLOB2_RESOURCE_INTERVAL",8);
    ResourceExperiment::Tree refreshModel{"GLOB2_RESOURCE_REFRESH_MODEL"},depthModel{"GLOB2_RESOURCE_DEPTH_MODEL"};
    ResourceExperiment::Tree severityModel{"GLOB2_RESOURCE_SEVERITY_MODEL"},refreshCostModel{"GLOB2_RESOURCE_REFRESH_COST_MODEL"};
    bool adaptiveEnabled=std::getenv("GLOB2_RESOURCE_PORTABLE_MODEL")!=nullptr;
    int adaptiveThreshold=int(ResourceExperiment::setting("GLOB2_RESOURCE_PORTABLE_THRESHOLD",0));
    bool adaptiveSourcesInitialized=false;
    std::array<AdaptiveResource::Sources,MaterialCount> adaptiveSources{};
    void initializeAdaptiveSources(const Map&);
    void adaptiveStockChanged(unsigned before,unsigned after) {
        if(!adaptiveSourcesInitialized)return;
        for(unsigned bits=before^after;bits;bits&=bits-1){auto& c=adaptiveSources[std::countr_zero(bits)];if(after&(bits&-bits)){++c.count;++c.added;}else{assert(c.count);--c.count;++c.removed;}}
    }
    std::array<AdaptiveResource::I,7> adaptiveInputs(unsigned tick,int material,int swim,int team)const {
        const auto& d=demand[team][material][swim];const auto& c=adaptiveSources[material];const auto& b=d.servingSources;
        AdaptiveResource::I n32=0,n128=0;
        for(const auto& q:d.queries)if(tick-q.tick<128){++n128;n32+=tick-q.tick<32;}
        const auto added=std::min<std::uint64_t>(1000000,c.added-b.added),removed=std::min<std::uint64_t>(1000000,c.removed-b.removed),den=std::max<std::uint64_t>(1,b.count);
        return {AdaptiveResource::I(tick-d.serving),n32,n128,AdaptiveResource::I(added),AdaptiveResource::I(removed),AdaptiveResource::I(std::min<std::uint64_t>(1000000,added*4096/den)),AdaptiveResource::I(std::min<std::uint64_t>(1000000,removed*4096/den))};
    }
    unsigned rankingVariant=ResourceExperiment::setting("GLOB2_RESOURCE_RANKING_VARIANT",0);
    int refreshThreshold=int(ResourceExperiment::setting("GLOB2_RESOURCE_REFRESH_THRESHOLD",250));
    int depthMode=std::getenv("GLOB2_RESOURCE_LAZY_DEPTH") ? std::stoi(std::getenv("GLOB2_RESOURCE_LAZY_DEPTH")) : -1;
    bool depthCensus=std::getenv("GLOB2_RESOURCE_DEPTH_CSV") && depthMode<0 && !depthModel.active();
    bool lazyActive=depthMode>=0 || depthCensus || depthModel.active();
    unsigned lastGameTick=0;
    std::ofstream depthOutput;
    std::map<Uint16**,ResourceExperiment::Lazy> resourceSearches;
    std::map<int,std::pair<int,int>> previousDepth;
    struct Capture { unsigned tick; BuildingGradientSearch::Inputs inputs; std::array<std::uint64_t,5> changes; AdaptiveResource::Sources sources{}; };
    std::map<const Uint16*,Capture> resourceCaptures;
    std::uint64_t lazyReads=0,lazyResumes=0,lazyResumeNs=0,lazyExports=0;
    std::vector<int> depthFeatures(Uint16** slot,int key,int units) {
        const auto it=resourceSearches.find(slot);const auto previous=previousDepth[key];
        const auto* serving=it!=resourceSearches.end()&&it->second.data==*slot ? &it->second : nullptr;
        return {serving?serving->required:0,previous.first,serving?int(serving->movement+serving->scalar):0,
            previous.second,int((key/SWIM_CLASS_COUNT)%MaterialCount),key%SWIM_CLASS_COUNT,units};
    }
    void retireResource(Uint16** slot,unsigned tick) {
        const auto it=resourceSearches.find(slot);if(it==resourceSearches.end())return;
        auto& v=it->second;
        const auto demandedEntries=v.search->expandedEntries();
        if(!v.shadow.empty())v.search->finish();
        const long long fullEntries=v.search->complete()?static_cast<long long>(v.search->expandedEntries()):-1;
        if(depthOutput) {
            depthOutput<<v.key<<','<<v.capture<<','<<v.published<<','<<tick<<','<<v.required<<','<<v.movement<<','<<v.scalar<<','<<v.bulk<<','<<v.exports<<','<<demandedEntries<<','<<fullEntries<<','<<v.search->retainedBytes()<<','<<v.resumeNs;
            for(int f:v.features)depthOutput<<','<<f;depthOutput<<'\n';
        }
        previousDepth[v.key]={v.required,int(v.movement+v.scalar)};ResourceSnapshotExperiment::unregisterSlot(slot);recycleBuildingGradientSearch(std::move(v.search));resourceSearches.erase(it);
    }
    void resolveResource(Uint16** slot,size_t cell,ResourceExperiment::Reader kind) {
        auto it=resourceSearches.find(slot);if(it==resourceSearches.end()||it->second.data!=*slot)return;
        auto& v=it->second;const bool full=kind==ResourceExperiment::Reader::Bulk||kind==ResourceExperiment::Reader::Export;
        ++lazyReads;v.movement+=kind==ResourceExperiment::Reader::Movement;v.scalar+=kind==ResourceExperiment::Reader::Scalar;
        v.bulk+=kind==ResourceExperiment::Reader::Bulk;v.exports+=kind==ResourceExperiment::Reader::Export;lazyExports+=kind==ResourceExperiment::Reader::Export;
        const auto before=v.search->expandedEntries();const auto start=std::chrono::steady_clock::now();
        if(full){v.search->finish();v.required=std::max(v.required,v.search->settledCost());}else{v.search->resolve(cell);v.required=std::max(v.required,v.search->requiredCost());}
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        v.resumeNs+=ns;lazyResumeNs+=ns;lazyResumes+=v.search->expandedEntries()!=before;
        if(!v.shadow.empty() && !full && v.shadow[cell]!=(*slot)[cell])throw std::runtime_error("Resource lazy census mismatch");
        if(!v.shadow.empty() && full && !std::equal(v.shadow.begin(),v.shadow.end(),*slot))throw std::runtime_error("Resource lazy full mismatch");
    }
    using Demand=ResourceExperiment::Demand;
    std::array<std::array<std::array<Demand,SWIM_CLASS_COUNT>,MaterialCount>,Team::MAX_COUNT> demand{};
    std::unique_ptr<ResourceGradientProbe> qualityProbe;
	struct Workspace
	{
		GradientWorkspace propagation;
		gradient_preparation::CrowdingScratch crowding;
	};
    TerrainSafetyCache safety;
	struct MaterialField
	{
		std::unique_ptr<Uint16[]> cells;
		Uint64 sourceRevision=0, recency=0;
		Uint32 topology=0, builtStep=0, identity=0;
		Sint32 consumer=-1, type=-1, x=0, y=0;
		int team=0, resource=0, swim=0;
		unsigned modes=0;
		std::list<Uint64>::iterator lru;
	};
	std::map<Uint64,MaterialField> materialFields;
	std::list<Uint64> materialLru;
	std::array<std::array<Uint64,MaterialCount>,Team::MAX_COUNT> stockRevision{};
	Uint64 materialCacheClock=0, materialCacheBudget=64ull*1024*1024;
	std::vector<Workspace> workspaces{1};
	GradientPipeline pipeline;
	// Scheduled building fields share the executor with the periodic pipeline.
	// Drain it before teams and the gradient buffer pool are destroyed.
	BuildingGradientJobPipeline buildings;
	// A refresh asked for during team stepping, admitted FIFO after the tick.
	// stale requests were superseded while queued; they stay in place so queue
	// length (and with it the overflow fallback) survives save and load.
	struct BuildingRequest
	{
		int team = 0;
		Uint16 building = 0;
		Uint32 identity = 0, epoch = 0;
		int slot = 0;
		bool stale = false;
	};
	static constexpr std::size_t BuildingRequestLimit = 64;
	std::deque<BuildingRequest> buildingRequests;
	// Reserved this tick, prepared together at the observation boundary.
	std::vector<BuildingGradientJobPipeline::Job*> stagedBuildings;
	// Owner-side building walking field builds (cold fields, own-building
	// changes and queue overflow; every build on a map without a game), split
	// by reason.
	Uint64 buildingSynchronous = 0;
	std::array<Uint64, std::size_t(Map::BuildingSyncReason::Count)> synchronousByReason{};
	Map::BuildingSyncReason syncReason = Map::BuildingSyncReason::Other;
	void countSynchronous()
	{
		++buildingSynchronous;
		++synchronousByReason[std::size_t(std::exchange(syncReason, Map::BuildingSyncReason::Other))];
	}
    ResourceGrowth::Pipeline growth;
	// One simulation-owned reservation, consumed once during the observation phase.
	// Scalar identities survive the scheduling barrier without borrowing stack lambdas.
	struct Preparation {
		using Kind = gradient_preparation::Kind;
		GradientPipeline::Job *job = nullptr;
		Kind kind = Kind::Materials;
		int team = 0, material = 0, swim = 0;
	} preparation;
    std::vector<Preparation> demandPreparations;
	bool supplierLocationsDirty = true;
	std::unordered_map<std::size_t, std::vector<std::uint16_t>> overlaySupplierLocations;
	ResourceSeedCache resourceSeeds;
	// Shared inert field for materials with no natural or supplier source.
	std::vector<Uint16> absentMaterialField;
    std::shared_ptr<gradient_kernel::GradientDeviceService> deviceService;
    std::shared_ptr<gradient_kernel::BackendSession> backendSession;
    void shareBackendSession()
    {
        for (auto& workspace : workspaces) workspace.propagation.backendSession = backendSession;
        pipeline.setBackendSession(backendSession);

    }
    void resetBackendSession()
    {
        if (backendSession) backendSession = std::make_shared<gradient_kernel::BackendSession>();
        shareBackendSession();
    }
    ~GradientRuntime() {
        if(qualityProbe)qualityProbe->finishStudy(lastGameTick);
        while(!resourceSearches.empty())retireResource(resourceSearches.begin()->first,lastGameTick);
        if (const char* path=std::getenv("GLOB2_EXPERIMENT_GRADIENT_METRICS")) {
            std::ofstream out(path);
            out << "{\"budget_slots\":" << budgetSlots << ",\"budget_used\":" << budgetUsed
                << ",\"budget_candidates\":" << budgetCandidates << ",\"budget_agreement\":" << budgetAgreement
                << ",\"natural_jobs\":" << pipeline.metrics.naturalJobs
                << ",\"total_jobs\":" << pipeline.metrics.jobs
                << ",\"lazy_reads\":" << lazyReads << ",\"lazy_resumes\":" << lazyResumes << ",\"lazy_resume_ns\":" << lazyResumeNs << ",\"lazy_exports\":" << lazyExports
                << ",\"snapshot_clones\":" << ResourceSnapshotExperiment::metrics()[0]
                << ",\"snapshot_reads\":" << ResourceSnapshotExperiment::metrics()[1]
                << ",\"snapshot_queue_entries\":" << ResourceSnapshotExperiment::metrics()[2]
                << ",\"snapshot_full_reads\":" << ResourceSnapshotExperiment::metrics()[3] << "}\n";
        }
    }
    GradientRuntime() {
        if(std::getenv("GLOB2_RESOURCE_STUDY_CSV") && (depthMode>=0||depthModel.active()))
            throw std::invalid_argument("Refresh episode collection requires full serving fields");
        if(rankingVariant>2 || (rankingVariant>=1&&!severityModel.active()) || (rankingVariant==2&&!refreshCostModel.active()) || severityModel.active()!=refreshCostModel.active())
            throw std::invalid_argument("Invalid value-ranking configuration");
        if(budgetPeriod && (!demandEnabled || budgetPolicy>2 || (budgetPolicy==2&&!refreshModel.active())))
            throw std::invalid_argument("Invalid budget experiment configuration");
        if(const char* p=std::getenv("GLOB2_RESOURCE_BUDGET_CSV")) {budgetOutput.open(p);if(!budgetOutput)throw std::runtime_error("Budget CSV");budgetOutput<<"tick,period,policy,candidates,selected,rr,oldest,model,key,age,score,flag_score,benefit_milli,cost_us,value_score\n";}
        if(const char* p=std::getenv("GLOB2_RESOURCE_WORK_CSV")) {workOutput.open(p);if(!workOutput)throw std::runtime_error("Work CSV");workOutput<<"capture,published,team,material,swim,seed_cpu_ns,propagate_cpu_ns\n";}
        shareBackendSession();
        if(const char* path=std::getenv("GLOB2_RESOURCE_DEPTH_CSV")) {
            depthOutput.open(path);if(!depthOutput)throw std::runtime_error("Cannot open depth census");
            depthOutput<<"key,capture,published,retired,required,movement,scalar,bulk,exports,expanded_entries,full_entries,retained_bytes,resume_ns,serving_required,previous_required,serving_queries,previous_queries,material,swim,class_units\n";
        }
        if (const char* path=std::getenv("GLOB2_RESOURCE_GRADIENT_PROBE"))
            if (*path) qualityProbe=std::make_unique<ResourceGradientProbe>(path);
    }

};
