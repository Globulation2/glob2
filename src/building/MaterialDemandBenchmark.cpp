// SPDX-License-Identifier: GPL-3.0-or-later
#include "MaterialDemandFixture.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>

namespace
{
// This fixture is also usable on the baseline revision: its measured production
// entry points predate the optimization. Keep the frozen reference independent.
int denseDeliveries(const Building& b, bool reference)
{
    int total=0;
    for (int r=0;r<MaterialSlotCount;++r)
    {
        const int need=reference ? referenceNeed(b,r) : b.materialDeliveryNeed(r);
        total+=int((Sint64(need)+b.type->materialMultiplier[r]-1)/b.type->materialMultiplier[r]);
    }
    return total;
}

template<class Operation>
void demandSample(const char* scenario, const char* operation, bool checkOnly, int iterations, Operation&& op)
{
    using Clock=std::chrono::steady_clock;
    for (int repeat=0;repeat<(checkOnly ? 1 : 7);++repeat)
    {
        std::uint64_t digest=0;
        Clock::time_point start{};
        if (!checkOnly) start=Clock::now();
        for (int n=0;n<iterations;++n)
        {
            // Compiler barrier, not a hardware fence. Input setup and allocation
            // happen outside this loop; the result is consumed below.
            std::atomic_signal_fence(std::memory_order_seq_cst);
            digest=digest*16777619u+std::uint64_t(op(n));
        }
        if (checkOnly)
            std::printf("{\"fixture\":\"building-material-demand\",\"case\":\"%s\",\"operation\":\"%s\",\"repeat\":%d,\"iterations\":%d,\"digest\":%llu,\"elapsed_ns\":null,\"summary_bytes\":%zu}\n",
                scenario,operation,repeat,iterations,(unsigned long long)digest,sizeof(BuildingMaterialDemand));
        else
            std::printf("{\"fixture\":\"building-material-demand\",\"case\":\"%s\",\"operation\":\"%s\",\"repeat\":%d,\"iterations\":%d,\"digest\":%llu,\"elapsed_ns\":%lld,\"summary_bytes\":%zu}\n",
                scenario,operation,repeat,iterations,(unsigned long long)digest,
                (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count(),sizeof(BuildingMaterialDemand));
    }
}

template<class B>
void summarySample(B& b, const char* name, bool checkOnly, int iterations)
{
    if constexpr (requires { b.materialDemand(); })
        demandSample(name,"summary",checkOnly,iterations,[&](int){
            const auto demand=b.materialDemand();
            unsigned value=unsigned(demand.totalDeliveries)+demand.missingMaterials;
            for (int deliveries:demand.deliveries) value=value*33u+unsigned(deliveries);
            return value;
        });
}
}

TEST_SUITE("BuildingMaterialDemandBenchmark")
{
TEST_CASE("isolated queries complete calculations and production callers [benchmark]")
{
    glob2test::HeadlessGlobals globals;
    const bool checkOnly=std::getenv("GLOB2_DEMAND_CHECK_ONLY")!=nullptr;
    const int iterations=checkOnly ? 30 : 20000;
    for (const char* name: {"full_idle","sparse_depleted","dense_authored","capacity_limited",
                           "construction","repair","shared","shared_dense","shared_construction",
                           "shared_repair","workers_4","workers_12","mixed_population"})
    {
        const std::string_view scenario(name);
        const bool site=scenario.ends_with("construction") || scenario.ends_with("repair");
        const bool shared=scenario=="shared" || scenario.starts_with("shared_");
        DemandFixture f(site);
        auto& b=*f.building;
        if (scenario=="full_idle")
            for (int r=0;r<MaterialSlotCount;++r) b.setMaterial(r,f.type.maxMaterial[r]);
        if (scenario=="sparse_depleted") f.runtime.replenishMaterialMask=(1u<<WOOD)|(1u<<WHEAT);
        if (scenario=="capacity_limited")
            for (int r=0;r<int(MaterialCount);++r) {f.type.maxMaterial[r]=1000000;b.setMaterial(r,254);}
        if (scenario=="dense_authored" || scenario=="shared_dense")
        {
            constexpr std::array<int,6> multipliers{1,2,3,7,10,1000000};
            for (int r=0;r<int(MaterialCount);++r) f.type.materialMultiplier[r]=multipliers[r%multipliers.size()];
        }
        if (shared)
        {
            f.type.useTeamMaterials=true;
            b.updateMaterialsPointer();
            b.setMaterial(WHEAT,site ? 20 : 100000);
        }
        if (scenario=="shared_dense")
            for (int r=0;r<int(MaterialCount);++r) {f.type.maxMaterial[r]=1000000;b.setMaterial(r,500000);}
        if (site)
        {
            // Keep these sites unfunded: production update callers must not
            // complete or change the input type during the measured loop.
            b.constructionResultState=scenario.ends_with("repair") ? Building::REPAIR : Building::NEW_BUILDING;
            for (int r=0;r<int(MaterialCount);++r)
                b.constructionBudget[r]=100;
            b.fundConstructionFromInventory();
        }
        if (scenario=="workers_4" || scenario=="workers_12")
            for (int n=0;n<(scenario=="workers_4" ? 4 : 12);++n)
            {
                auto* u=f.world.addUnit(WORKER);
                u->destinationPurpose=n%MaterialCount;
                u->attachedBuilding=&b;
                b.unitsWorking.push_back(u);
            }
        std::vector<Building*> population{&b};
        if (scenario=="mixed_population")
            for (int n=0;n<8;++n)
            {
                auto* other=f.world.addBuilding(n%2 ? "inn" : "swarm",12+(n%2)*8,4+(n/2)*6);
                other->setMaterial(WHEAT,n*3);
                population.push_back(other);
            }
        checkDemand(b);
        f.world.game.stepCounter=1; // Exclude the unrelated periodic idle-gradient trim.
        for (auto* p:population)
        {
            p->computeWishedMaterials(p->wishedMaterials);
            p->updateCallLists(); // Establish buffers and recruitment membership before samples.
            p->canNotConvertUnitTimer=0;
        }
        demandSample(name,"single_reference",checkOnly,iterations,[&](int n){return referenceNeed(b,n%MaterialSlotCount);});
        demandSample(name,"single_current",checkOnly,iterations,[&](int n){return b.materialDeliveryNeed(n%MaterialSlotCount);});
        demandSample(name,"dense_reference",checkOnly,iterations,[&](int){return denseDeliveries(b,true);});
        demandSample(name,"dense_current_queries",checkOnly,iterations,[&](int){return denseDeliveries(b,false);});
        summarySample(b,name,checkOnly,iterations);
        demandSample(name,"wishes",checkOnly,iterations,[&](int){int w[MaterialSlotCount];b.computeWishedMaterials(w);int sum=0;for(int v:w)sum+=v;return sum;});
        demandSample(name,"staffing",checkOnly,iterations,[&](int){return b.desiredNumberOfWorkers();});
        demandSample(name,"call_lists",checkOnly,iterations,[&](int){b.updateCallLists();return b.desiredMaxUnitWorking;});
        demandSample(name,"step",checkOnly,iterations,[&](int n){auto* p=population[n%population.size()];p->step();return p->totalWishedMaterial();});
        demandSample(name,"update",checkOnly,iterations,[&](int n){auto* p=population[n%population.size()];p->update();return p->desiredMaxUnitWorking;});
        checkDemand(b);
    }
}
}
