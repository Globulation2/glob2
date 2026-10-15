// SPDX-License-Identifier: GPL-3.0-or-later
#include "MaterialDemandFixture.h"
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
