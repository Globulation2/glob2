// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Test/benchmark-only fixtures and a deliberately dense, independent oracle.
#include "EngineFixtures.h"
#include "BuildingType.h"
#include "MaterialDemand.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace
{
struct DemandReference
{
    std::array<int, MaterialSlotCount> needs{}, deliveries{}, wishes{}, targets{}, served{};
    int total = 0, workers = 0;
    MaterialMask missing = 0;
};

// Frozen pre-optimization formulas. Do not call the production demand helpers.
int referenceNeed(const Building& b, int r)
{
    const Sint64 stock = b.material(r);
    const Sint64 room = std::max<Sint64>(0, Sint64(b.materialStockLimit())-stock);
    if (b.type->isBuildingSite && (b.constructionBudget[r] || b.type->semantics.constructionCost[r]))
    {
        const int reserved = b.type->useTeamMaterials ? b.owner->reservedTeamMaterials[r] : b.reservedMaterials[r];
        const Sint64 available = b.constructionResultState == Building::REPAIR ? 0 : std::max<Sint64>(0,stock-reserved);
        const Sint64 unfunded = std::max<Sint64>(0,Sint64(b.constructionBudget[r])-b.constructionReserved[r]);
        return int(std::min(room,std::max<Sint64>(0,unfunded-available)));
    }
    return b.runtime->replenishMaterialMask & (1u<<r)
        ? int(std::min(room,std::max<Sint64>(0,Sint64(b.type->maxMaterial[r])-stock))) : 0;
}

DemandReference referenceDemand(const Building& b)
{
    DemandReference out;
    for (int r=0; r<MaterialSlotCount; ++r)
    {
        out.needs[r] = referenceNeed(b,r);
        const int multiplier = b.type->materialMultiplier[r];
        out.deliveries[r] = int((Sint64(out.needs[r])+multiplier-1)/multiplier);
        out.wishes[r] = 4*out.deliveries[r]/3;
        if (r<int(MaterialCount)) out.total += out.deliveries[r];
        if (out.needs[r]>0) out.missing |= MaterialMask(1u<<r);
        const int target = b.type->isBuildingSite && (b.constructionBudget[r] || b.type->semantics.constructionCost[r])
            ? b.constructionBudget[r] : b.runtime->replenishMaterialMask & (1u<<r) ? b.type->maxMaterial[r] : 0;
        out.targets[r] = int((Sint64(target)+multiplier-1)/multiplier);
        out.served[r] = out.targets[r]-out.deliveries[r];
    }
    for (const Unit* u:b.unitsWorking)
        if (u->destinationPurpose>=0)
        {
            --out.wishes[u->destinationPurpose];
            ++out.served[u->destinationPurpose];
        }
    const int user = std::min(b.maxUnitWorking,b.type->semantics.assignmentLimit);
    out.workers = b.runtime->attractionRoles ? user : std::min(user,4*out.total/3);
    return out;
}

struct DemandFixture
{
    glob2test::HeadlessGame world{{.loadDefaultRace=true}};
    Building* building = world.addBuilding("inn",4,4);
    BuildingType* originalType = building->type;
    const BuildingRuntimeTraits* originalRuntime = building->runtime;
    BuildingType type = *originalType;
    BuildingRuntimeTraits runtime = *originalRuntime;

    DemandFixture(bool site=false)
        : building(site ? world.game.addBuilding(4,4,world.game.buildingsTypes.getTypeNum("inn",0,true),0)
                        : world.addBuilding("inn",4,4)),
          originalType(building->type), originalRuntime(building->runtime), type(*originalType), runtime(*originalRuntime)
    {
        building->type=&type;
        building->runtime=&runtime;
        runtime.attractionRoles=0;
        runtime.replenishMaterialMask=AllMaterials;
        type.semantics.assignmentLimit=1000;
        building->maxUnitWorking=1000;
        for (int r=0;r<MaterialSlotCount;++r)
        {
            type.maxMaterial[r]=r<int(MaterialCount) ? 7+r*3 : 0;
            type.materialMultiplier[r]=1;
            building->setMaterial(r,0);
        }
    }
    ~DemandFixture()
    {
        // Do not leave borrowed test definitions dangling during Game teardown.
        for (auto* unit:building->unitsWorking) unit->attachedBuilding=nullptr;
        building->unitsWorking.clear();
        building->type=originalType;
        building->runtime=originalRuntime;
        building->updateMaterialsPointer();
        building->reservedMaterials.fill(0);
        building->constructionReserved.fill(0);
        std::fill_n(world.team->reservedTeamMaterials,MaterialSlotCount,0);
    }
};

template<class B>
void checkSummary(const B& b, const DemandReference& expected)
{
    // The same fixture source can qualify the pre-summary baseline binary.
    if constexpr (requires { b.materialDemand(); })
    {
        const auto summary=b.materialDemand();
        CHECK(summary.totalDeliveries==expected.total);
        CHECK(summary.missingMaterials==expected.missing);
        for (int r=0;r<MaterialSlotCount;++r) CHECK(summary.deliveries[r]==expected.deliveries[r]);
    }
}

void checkDemand(Building& b)
{
    const auto rng=b.entityRandom.exportState();
    const auto expected=referenceDemand(b);
    checkSummary(b,expected);
    int needs[MaterialSlotCount], wishes[MaterialSlotCount], targets[MaterialSlotCount], served[MaterialSlotCount];
    b.neededMaterials(needs);
    b.computeWishedMaterials(wishes);
    b.fetchApportionment(targets,served);
    for (int r=0;r<MaterialSlotCount;++r)
    {
        CAPTURE(r);
        CHECK(b.materialDeliveryNeed(r)==expected.needs[r]);
        CHECK(needs[r]==expected.needs[r]);
        CHECK(wishes[r]==expected.wishes[r]);
        CHECK(targets[r]==expected.targets[r]);
        CHECK(served[r]==expected.served[r]);
    }
    CHECK(b.desiredNumberOfWorkers()==expected.workers);
    CHECK(b.isMaterialFull()==(expected.missing==0));
    CHECK(b.entityRandom.exportState()==rng);
}

struct DemandRandom
{
    std::uint32_t value=713;
    unsigned next() { value^=value<<13; value^=value>>17; value^=value<<5; return value; }
};
}
