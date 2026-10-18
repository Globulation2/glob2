// SPDX-License-Identifier: GPL-3.0-or-later
// Dense reference formulas stay independent of the optimized production calculation.
#include "EngineFixtures.h"
#include "BuildingType.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>

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
    Building* building;
    BuildingType* originalType;
    const BuildingRuntimeTraits* originalRuntime;
    BuildingType type;
    BuildingRuntimeTraits runtime;

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


void checkDemand(Building& b)
{
    const auto rng=b.entityRandom.exportState();
    const auto expected=referenceDemand(b);
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

#include <nlohmann/json.hpp>

TEST_SUITE("BuildingMaterialDemand")
{
TEST_CASE("dense oracle covers generated private shared construction and repair states")
{
    glob2test::HeadlessGlobals globals;
    DemandFixture fixture;
    auto& b=*fixture.building;
    auto& type=fixture.type;
    DemandRandom random;
    const std::array<int,6> multipliers{1,2,3,7,10,1000000};
    for (int state=0;state<1600;++state)
    {
        CAPTURE(state);
        type.useTeamMaterials=(state&1)!=0;
        b.updateMaterialsPointer();
        type.isBuildingSite=(state&2)!=0;
        b.constructionResultState=(state&4) ? Building::REPAIR : Building::NEW_BUILDING;
        fixture.runtime.replenishMaterialMask=MaterialMask(random.next()&AllMaterials);
        b.maxUnitWorking=random.next()%1001;
        for (int r=0;r<MaterialSlotCount;++r)
        {
            type.maxMaterial[r]=r<int(MaterialCount) ? random.next()%1000001 : 0;
            type.materialMultiplier[r]=multipliers[random.next()%multipliers.size()];
            type.semantics.constructionCost[r]=r<int(MaterialCount) ? random.next()%101 : 0;
            b.constructionBudget[r]=r<int(MaterialCount) ? random.next()%1000001 : 0;
            b.constructionReserved[r]=random.next()%(b.constructionBudget[r]+1);
            const int stock=random.next()%(type.useTeamMaterials ? 2000001 : 256);
            b.setMaterial(r,stock);
            b.reservedMaterials[r]=random.next()%(stock+1);
            fixture.world.team->reservedTeamMaterials[r]=b.reservedMaterials[r];
        }
        checkDemand(b);
    }
}

TEST_CASE("per material rounding differs from aggregate staffing and wishes can be negative")
{
    glob2test::HeadlessGlobals globals;
    DemandFixture fixture;
    auto& b=*fixture.building;
    std::fill_n(fixture.type.maxMaterial,MaterialSlotCount,0);
    for (int r=0;r<3;++r) fixture.type.maxMaterial[r]=1;
    CHECK(b.desiredNumberOfWorkers()==4);
    int wishes[MaterialSlotCount];
    b.computeWishedMaterials(wishes);
    CHECK(wishes[0]+wishes[1]+wishes[2]==3);
    for (int purpose:{0,0,0,1,2,14})
    {
        auto* unit=fixture.world.addUnit(WORKER);
        unit->destinationPurpose=purpose;
        b.unitsWorking.push_back(unit);
    }
    checkDemand(b);
    b.computeWishedMaterials(wishes);
    CHECK(wishes[0]==-2);
    CHECK(wishes[14]==-1);
    b.unitsWorking.pop_back(); // Departure must be visible to the next operation.
    checkDemand(b);
    fixture.runtime.attractionRoles=1;
    CHECK(b.desiredNumberOfWorkers()==1000);
}

TEST_CASE("division boundaries capacity clamps and authored multiplier bounds remain exact")
{
    glob2test::HeadlessGlobals globals;
    DemandFixture fixture;
    auto& b=*fixture.building;
    for (bool shared:{false,true})
    {
        fixture.type.useTeamMaterials=shared;
        b.updateMaterialsPointer();
        for (int multiplier:{1,2,3,7,10,1000000})
            for (int target:{0,1,2,3,6,7,8,999999,1000000})
                for (int stock:{0,1,6,7,8,254,255,1000000,INT_MAX})
                {
                    fixture.type.maxMaterial[WOOD]=target;
                    fixture.type.materialMultiplier[WOOD]=multiplier;
                    b.setMaterial(WOOD,stock);
                    checkDemand(b);
                }
    }
}

TEST_CASE("fresh queries see shared reservations releases consumption and other building deliveries")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true});
    auto json=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    const int id=world.game.buildingsTypes.getTypeNum("inn",0,false);
    auto& inn=json["variants"][id];
    inn["semantics"]["market"]["sharedStock"]=true;
    inn["semantics"]["replenishMaterials"]=MaterialKeys;
    for (int r=0;r<int(MaterialCount);++r)
    {
        inn["properties"]["maxMaterial"][r]=1000+r;
        inn["properties"]["materialMultiplier"][r]=1+r;
    }
    inn["properties"]["maxMaterial"][WHEAT]=1000;
    inn["properties"]["materialMultiplier"][WHEAT]=7;
    const int siteId=world.game.buildingsTypes.getTypeNum("inn",0,true);
    json["variants"][siteId]["semantics"]["market"]["sharedStock"]=true;
    json["variants"][siteId]["semantics"]["constructionCost"]={{"food",100}};
    world.game.buildingsTypes.loadSnapshotJson(json.dump());
    world.game.configureBuildingCatalog();
    auto* a=world.addBuilding("inn",4,4);
    auto* other=world.addBuilding("inn",12,12);
    REQUIRE(a->usesTeamMaterials());
    for (int stock:{0,6,7,999,1000,1200,INT_MAX})
    {
        other->setMaterial(WHEAT,stock);
        checkDemand(*a);
    }
    other->setMaterial(WHEAT,70);
    BuildingMaterialCost reservation{};
    reservation[WHEAT]=28;
    REQUIRE(other->reserveMaterials(reservation));
    checkDemand(*a);
    other->releaseMaterials(reservation);
    checkDemand(*a);
    REQUIRE(other->reserveMaterials(reservation));
    other->consumeReservedMaterials(reservation,-1);
    checkDemand(*a);
    other->deliverMaterialPacket(WHEAT,{});
    checkDemand(*a);
    other->setMaterial(WHEAT,0);
    auto* site=world.game.addBuilding(20,4,siteId,0);
    REQUIRE(site);
    other->setMaterial(WHEAT,70);
    checkDemand(*site);
    CHECK(site->materialDeliveryNeed(WHEAT)==30);
    REQUIRE(other->reserveMaterials(reservation));
    checkDemand(*site);
    CHECK(site->materialDeliveryNeed(WHEAT)==58);
    other->releaseMaterials(reservation);
    checkDemand(*site);
    CHECK(site->materialDeliveryNeed(WHEAT)==30);
    REQUIRE(other->reserveMaterials(reservation));
    other->consumeReservedMaterials(reservation,-1);
    checkDemand(*site);
    CHECK(site->materialDeliveryNeed(WHEAT)==58);
}

TEST_CASE("maximum authored demand keeps ceiling and aggregate arithmetic in range")
{
    glob2test::HeadlessGlobals globals;
    DemandFixture fixture;
    auto& b=*fixture.building;
    fixture.type.useTeamMaterials=true;
    b.updateMaterialsPointer();
    for (int r=0;r<int(MaterialCount);++r)
    {
        fixture.type.maxMaterial[r]=1000000;
        fixture.type.materialMultiplier[r]=1;
        b.setMaterial(r,0);
    }
    checkDemand(b);
    int wishes[MaterialSlotCount];
    b.computeWishedMaterials(wishes);
    for (int r=0;r<int(MaterialCount);++r) CHECK(wishes[r]==1333333);
    CHECK(referenceDemand(b).total==12000000);
}

TEST_CASE("reserved output slots are initialized and remain distinct from real material staffing")
{
    glob2test::HeadlessGlobals globals;
    DemandFixture fixture(true);
    auto& b=*fixture.building;
    std::fill_n(fixture.type.maxMaterial,MaterialSlotCount,0);
    fixture.type.semantics.constructionCost.fill(0);
    b.constructionBudget.fill(0);
    b.constructionBudget[14]=3;
    checkDemand(b);
    int wishes[MaterialSlotCount];
    b.computeWishedMaterials(wishes);
    CHECK(wishes[14]==4);
    CHECK(b.desiredNumberOfWorkers()==0);
    b.constructionBudget[14]=0;
    checkDemand(b);
    b.computeWishedMaterials(wishes);
    CHECK(wishes[14]==0);
}

TEST_CASE("sparse delivery accounting clears unused slots and still counts committed workers")
{
    glob2test::HeadlessGlobals globals;
    DemandFixture fixture;
    auto& b=*fixture.building;
    fixture.runtime.replenishMaterialMask=1u<<WHEAT;
    auto* unit=fixture.world.addUnit(WORKER,20,20);
    REQUIRE(unit);
    unit->destinationPurpose=WOOD;
    unit->attachedBuilding=&b;
    b.unitsWorking.push_back(unit);
    checkDemand(b);
    int targets[MaterialSlotCount], served[MaterialSlotCount];
    std::fill_n(targets,MaterialSlotCount,-999);
    std::fill_n(served,MaterialSlotCount,-999);
    b.fetchApportionment(targets,served);
    CHECK(targets[WOOD]==0);
    CHECK(served[WOOD]==1);
    for (int r=MaterialCount;r<MaterialSlotCount;++r) {
        CHECK(targets[r]==0);
        CHECK(served[r]==0);
    }
}

TEST_CASE("update boundaries keep wishes staffing and recruitment bucket order observable")
{
    glob2test::HeadlessGlobals globals;
    DemandFixture fixture;
    auto& b=*fixture.building;
    b.maxUnitWorking=12;
    b.priority=3;
    for (int stock:{0,3,255,0})
    {
        b.setMaterial(WHEAT,stock);
        const auto expected=referenceDemand(b);
        b.step();
        for (int r=0;r<MaterialSlotCount;++r) CHECK(b.wishedMaterials[r]==expected.wishes[r]);
        CHECK(b.desiredMaxUnitWorking==expected.workers);
        b.update();
        for (int r=0;r<MaterialSlotCount;++r) CHECK(b.wishedMaterials[r]==expected.wishes[r]);
        CHECK(b.desiredMaxUnitWorking==expected.workers);
        const auto once=fixture.world.team->buildingsNeedingUnits;
        b.updateCallLists();
        CHECK(fixture.world.team->buildingsNeedingUnits==once);
        checkDemand(b);
    }
    auto* peer=fixture.world.addBuilding("inn",12,12);
    peer->priority=4;
    peer->maxUnitWorking=12;
    b.priority=4; b.updateCallLists();
    CHECK(std::count(fixture.world.team->buildingsNeedingUnits[3].begin(),fixture.world.team->buildingsNeedingUnits[3].end(),&b)==0);
    CHECK(std::count(fixture.world.team->buildingsNeedingUnits[4].begin(),fixture.world.team->buildingsNeedingUnits[4].end(),&b)==1);
    peer->updateCallLists();
    std::fill_n(b.wishedMaterials,MaterialSlotCount,0);
    // Preserve same-priority removal/reinsertion using the authoritative comparator.
    auto expected=fixture.world.team->buildingsNeedingUnits[4];
    expected.erase(std::find(expected.begin(),expected.end(),&b));
    auto position=std::find_if(expected.begin(),expected.end(),[&](Building* p){return Team::buildingHasHigherPriority(&b,p);});
    expected.insert(position,&b);
    b.updateCallLists();
    CHECK(fixture.world.team->buildingsNeedingUnits[4]==expected);
}

TEST_CASE("same priority reposition matches removal and insertion in unsorted buckets")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    std::array<Building*,4> buildings{};
    for (int i=0;i<4;++i)
    {
        buildings[i]=world.addBuilding("inn",4+6*i,8);
        REQUIRE(buildings[i]);
        buildings[i]->maxUnitWorking=12;
        buildings[i]->setMaterial(WHEAT,0);
        buildings[i]->updateCallLists();
    }
    auto& bucket=world.team->buildingsNeedingUnits[buildings[0]->priority];
    std::array<int,4> permutation{0,1,2,3};
    do {
        for (int target=0;target<4;++target)
            for (int wish=0;wish<5;++wish)
            {
                // Peers' wishes can change before they update their own lists.
                // Exercise every position, both movement directions, unchanged
                // positions, and equal-wish ties resolved by building identity.
                for (int i=0;i<4;++i) {
                    std::fill_n(buildings[i]->wishedMaterials,MaterialSlotCount,0);
                    buildings[i]->wishedMaterials[WHEAT]=i;
                }
                buildings[target]->wishedMaterials[WHEAT]=wish;
                bucket.clear();
                for (int i:permutation) bucket.push_back(buildings[i]);
                auto expected=bucket;
                auto* b=buildings[target];
                expected.erase(std::find(expected.begin(),expected.end(),b));
                const auto position=std::find_if(expected.begin(),expected.end(),
                    [&](Building* peer){return Team::buildingHasHigherPriority(b,peer);});
                expected.insert(position,b);
                b->updateCallLists(12,false);
                CHECK(bucket==expected);
                CHECK(bucket.size()==4);
            }
    } while (std::next_permutation(permutation.begin(),permutation.end()));
    bucket={buildings[0]};
    buildings[0]->updateCallLists(12,false);
    CHECK(bucket==std::vector<Building*>{buildings[0]});
    bucket.assign(buildings.begin(),buildings.end());
}

TEST_CASE("repair upgrade delivery completion and destruction refresh demand at their existing boundaries")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto* b=world.addBuilding("inn",8,8);
    b->hp=b->getEffectiveMaxHp()/2;
    REQUIRE(b->launchConstruction(2,2));
    REQUIRE(b->tryToBuildingSiteRoom());
    REQUIRE(b->constructionResultState==Building::REPAIR);
    checkDemand(*b);
    for (int pass=0;b->type->isBuildingSite && pass<100;++pass)
        for (int r=0;r<int(MaterialCount);++r)
            if (referenceNeed(*b,r)>0)
            {
                b->deliverMaterialPacket(r,{});
                checkDemand(*b);
            }
    REQUIRE_FALSE(b->type->isBuildingSite);
    checkDemand(*b);
    REQUIRE(b->launchConstruction(2,2));
    REQUIRE(b->tryToBuildingSiteRoom());
    checkDemand(*b);
    b->cancelConstruction(2);
    checkDemand(*b);
    b->kill();
    int wishes[MaterialSlotCount];
    b->computeWishedMaterials(wishes);
    b->update(); // Dead update still refreshes wishedMaterials before returning.
    for (int r=0;r<MaterialSlotCount;++r) CHECK(b->wishedMaterials[r]==wishes[r]);
}
}
