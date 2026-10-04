// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Order.h"
#include "BuildingGuiState.h"
#include <memory>

TEST_SUITE("GUIOrderCoverage")
{
    TEST_CASE("worker requests clamp deduplicate and retain newer pending changes")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.teams=2,.header=true});
        auto& gui=world.gui;
        gui.localPlayer=0; gui.localTeamNo=0;
        auto* building=world.addBuilding("swarm",4,4);
        auto* enemy=world.addBuilding("swarm",20,20,0,1);
        CHECK_FALSE(gui.requestWorkerAllocation(*enemy,5));
        const int original=building->maxUnitWorking;
        REQUIRE(gui.requestWorkerAllocation(*building,MAX_UNIT_WORKING+20));
        CHECK(gui.displayedMaxUnitWorking(*building)==MAX_UNIT_WORKING);
        CHECK(building->maxUnitWorking==original);
        CHECK_FALSE(gui.requestWorkerAllocation(*building,MAX_UNIT_WORKING+1));
        REQUIRE(gui.requestWorkerAllocation(*building,-1));
        CHECK(gui.orderQueue.size()==2);
        auto older=gui.orderQueue.front(); gui.orderQueue.pop_front(); older->sender=0;
        gui.executeOrder(older);
        CHECK(building->maxUnitWorking==MAX_UNIT_WORKING);
        CHECK(gui.displayedMaxUnitWorking(*building)==0);
        auto latest=gui.orderQueue.front(); gui.orderQueue.pop_front(); latest->sender=0;
        gui.executeOrder(latest);
        CHECK(building->maxUnitWorking==0);
        CHECK_FALSE(gui.pendingFor(building->gid).pendingMaxUnitWorking.has_value());
        CHECK_FALSE(gui.requestWorkerAllocation(*building,0));
    }

    TEST_CASE("priority and radius requests clamp and reconcile against real orders")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.header=true});
        auto& gui=world.gui;
        gui.localPlayer=0; gui.localTeamNo=0;
        auto* flag=world.addBuilding("warflag",4,4);
        REQUIRE(gui.requestBuildingPriority(*flag,99));
        CHECK(gui.displayedPriority(*flag)==1);
        CHECK_FALSE(gui.requestBuildingPriority(*flag,2));
        REQUIRE(gui.requestFlagRange(*flag,999));
        CHECK(gui.displayedUnitStayRange(*flag)==flag->type->maxUnitStayRange);
        CHECK_FALSE(gui.requestFlagRange(*flag,999));
        while (!gui.orderQueue.empty())
        {
            auto order=gui.orderQueue.front(); gui.orderQueue.pop_front(); order->sender=0;
            gui.executeOrder(order);
        }
        CHECK(flag->priority==1);
        CHECK(flag->unitStayRange==flag->type->maxUnitStayRange);
        CHECK_FALSE(gui.pendingFor(flag->gid).pendingPriority.has_value());
        CHECK_FALSE(gui.pendingFor(flag->gid).pendingUnitStayRange.has_value());
        globals->replaying=true;
        CHECK_FALSE(gui.requestBuildingPriority(*flag,-1));
        CHECK_FALSE(gui.requestFlagRange(*flag,0));
        CHECK_FALSE(gui.requestWorkerAllocation(*flag,0));
        CHECK(gui.orderQueue.empty());
        globals->replaying=false;
    }

    TEST_CASE("reconciliation is field-specific and replay clears superseded shadows")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world;
        auto& gui=world.gui;
        gui.localPlayer=0;
        constexpr Uint16 gid=19;
        auto& pending=gui.pendingFor(gid);
        pending.pendingPosX=3; pending.pendingPosY=7;
        pending.pendingMinLevelToFlag=2;
        pending.pendingRatio=std::array<Sint32,NB_UNIT_TYPE>{2,1,3};
        pending.pendingClearingResources=std::array<bool,BASIC_COUNT>{true,false,true};
        auto move=std::make_shared<OrderMoveFlag>(gid,3,6,true);
        move->sender=0; gui.reconcileBuildingGuiState(move);
        CHECK(pending.pendingPosX.has_value()); // Both coordinates must match.
        globals->replaying=true;
        gui.reconcileBuildingGuiState(move);
        globals->replaying=false;
        CHECK_FALSE(pending.pendingPosX.has_value());
        CHECK_FALSE(pending.pendingPosY.has_value());
        CHECK(pending.pendingRatio.has_value());
        auto level=std::make_shared<OrderModifyMinLevelToFlag>(gid,1);
        level->sender=0; gui.reconcileBuildingGuiState(level);
        CHECK(pending.pendingMinLevelToFlag.has_value());
        level->minLevelToFlag=2; gui.reconcileBuildingGuiState(level);
        CHECK_FALSE(pending.pendingMinLevelToFlag.has_value());
        Sint32 ratios[NB_UNIT_TYPE]={2,1,2};
        auto ratio=std::make_shared<OrderModifySwarm>(gid,ratios);
        ratio->sender=0; gui.reconcileBuildingGuiState(ratio);
        CHECK(pending.pendingRatio.has_value());
        ratio->ratio[2]=3; gui.reconcileBuildingGuiState(ratio);
        CHECK_FALSE(pending.pendingRatio.has_value());
        bool mask[BASIC_COUNT]={true,false,false};
        auto clearing=std::make_shared<OrderModifyClearingFlag>(gid,mask);
        clearing->sender=0; gui.reconcileBuildingGuiState(clearing);
        CHECK(pending.pendingClearingResources.has_value());
        clearing->clearingResources[2]=true; gui.reconcileBuildingGuiState(clearing);
        CHECK_FALSE(pending.pendingClearingResources.has_value());
        // Unknown/missing entries must not create phantom GUI state.
        auto absent=std::make_shared<OrderChangePriority>(999,1);
        absent->sender=0; gui.reconcileBuildingGuiState(absent);
        gui.reconcileBuildingGuiState(std::make_shared<NullOrder>());
        CHECK(gui.buildingGuiState.size()==1);
    }
}
