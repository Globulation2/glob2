// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Order.h"
#include "BuildingGuiState.h"
#include <memory>

namespace {
const SceneBuildingPanel& presentedBuilding(GameGUI& gui, const Building& building)
{
    gui.game.finishOrderPhase(false); // A paused owner edit precedes its next observation.
    gui.prepareLocalPresentation();
    gui.setSelection(GameGUI::BUILDING_SELECTION, unsigned(building.gid));
    gui.prepareLocalPresentation();
    const auto* panel = gui.inputBuildingPanel();
    REQUIRE(panel);
    return *panel;
}
}

TEST_SUITE("GUIOrderCoverage")
{
	TEST_CASE("headless receipts without pending GUI fields do not accumulate")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world;
		auto& gui=world.gui;
		for (Uint64 revision=1;revision<=1000;++revision)
		{
			auto order=std::make_shared<OrderChangePriority>(19,1);
			gui.handleClientEvent(ClientEventVariant{ClientEvent::OrderExecuted{order,revision}});
			gui.handleClientEvent(ClientEventVariant{ClientEvent::OrderExecuted{std::make_shared<NullOrder>(),revision+1000}});
		}
		CHECK(gui.pendingBuildingReconciliations.empty());
		CHECK(gui.buildingGuiState.empty());
	}

	TEST_CASE("compound production edits retain acknowledged values until their Scene is published")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world(glob2test::GameOptions{.header = true});
		auto &gui = world.gui;
		gui.localPlayer = 0;
		gui.localTeamNo = 0;
		auto *building = world.addBuilding("swarm", 4, 4);
		REQUIRE(building);
		const auto &panel = presentedBuilding(gui, *building);
		const auto original = gui.displayedRatio(panel);
		auto first = original;
		first[WORKER] = original[WORKER] + 3;
		gui.pendingFor(building->gid).pendingRatio = first;
		auto firstOrder = std::make_shared<OrderModifySwarm>(building->gid, first.data());
		firstOrder->sender = 0;
		const auto revision = gui.drawnScene().executedOrderRevision + 1;
		gui.handleClientEvent(ClientEventVariant{ClientEvent::OrderExecuted{firstOrder, revision}});
		REQUIRE(gui.pendingFor(building->gid).pendingRatio.has_value());
		// The second component edit still reads the old published frame. It
		// must compose with the acknowledged first edit, not its old baseline.
		auto second = gui.displayedRatio(panel);
		CHECK(second[WORKER] == first[WORKER]);
		second[EXPLORER] = original[EXPLORER] + 2;
		gui.pendingFor(building->gid).pendingRatio = second;
		auto secondOrder = std::make_shared<OrderModifySwarm>(building->gid, second.data());
		secondOrder->sender = 0;
		gui.handleClientEvent(
			ClientEventVariant{ClientEvent::OrderExecuted{secondOrder, revision + 1}});
		// Publishing the older receipt cannot erase the newer compound edit.
		auto scene = gui.drawnScene();
		scene.executedOrderRevision = revision;
		gui.setPublishedScene(&scene);
		REQUIRE(gui.pendingFor(building->gid).pendingRatio.has_value());
		CHECK(gui.displayedRatio(panel) == second);
		CHECK(gui.displayedRatio(panel)[WORKER] == first[WORKER]);
		// Once that latest receipt is represented by a frame, ordinary
		// authoritative presentation takes over again.
		scene.executedOrderRevision = revision + 1;
		gui.setPublishedScene(&scene);
		CHECK_FALSE(gui.pendingFor(building->gid).pendingRatio.has_value());
		CHECK(gui.pendingBuildingReconciliations.empty());
		gui.setPublishedScene(nullptr);
		// A removal invalidates old receipts before a gid can be recycled.
		gui.pendingFor(building->gid).pendingRatio=first;
		gui.handleClientEvent(ClientEventVariant{ClientEvent::OrderExecuted{firstOrder,revision+2}});
		REQUIRE_FALSE(gui.pendingBuildingReconciliations.empty());
		gui.handleClientEvent(ClientEventVariant{ClientEvent::BuildingRemoved{building->gid}});
		CHECK(gui.pendingBuildingReconciliations.empty());
		gui.pendingFor(building->gid).pendingRatio=second;
		scene.executedOrderRevision=revision+2;
		gui.setPublishedScene(&scene);
		CHECK(gui.pendingFor(building->gid).pendingRatio==second);
		// Reload/init retires the entire session's acknowledgement journal.
		gui.handleClientEvent(ClientEventVariant{ClientEvent::OrderExecuted{firstOrder,revision+3}});
		REQUIRE_FALSE(gui.pendingBuildingReconciliations.empty());
		gui.init();
		CHECK(gui.pendingBuildingReconciliations.empty());
		CHECK(gui.buildingGuiState.empty());
		gui.setPublishedScene(nullptr);
	}

	TEST_CASE("worker requests clamp deduplicate and retain newer pending changes")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world(glob2test::GameOptions{.teams = 2, .header = true});
		auto &gui = world.gui;
		gui.localPlayer=0; gui.localTeamNo=0;
        auto* building=world.addBuilding("swarm",4,4);
        auto* enemy=world.addBuilding("swarm",20,20,0,1);
        CHECK_FALSE(gui.requestWorkerAllocation(presentedBuilding(gui, *enemy),5));
        const int original=building->maxUnitWorking;
        REQUIRE(gui.requestWorkerAllocation(presentedBuilding(gui, *building),MAX_UNIT_WORKING+20));
        CHECK(gui.displayedMaxUnitWorking(*building)==MAX_UNIT_WORKING);
        CHECK(building->maxUnitWorking==original);
        CHECK_FALSE(gui.requestWorkerAllocation(presentedBuilding(gui, *building),MAX_UNIT_WORKING+1));
        REQUIRE(gui.requestWorkerAllocation(presentedBuilding(gui, *building),-1));
        CHECK(gui.orderQueue.size()==2);
        auto older=gui.orderQueue.front(); gui.orderQueue.pop_front(); older->sender=0;
        gui.executeOrder(older);
        CHECK(building->maxUnitWorking==MAX_UNIT_WORKING);
        CHECK(gui.displayedMaxUnitWorking(*building)==0);
        auto latest=gui.orderQueue.front(); gui.orderQueue.pop_front(); latest->sender=0;
        gui.executeOrder(latest);
        CHECK(building->maxUnitWorking==0);
        presentedBuilding(gui, *building);
        CHECK_FALSE(gui.pendingFor(building->gid).pendingMaxUnitWorking.has_value());
        CHECK_FALSE(gui.requestWorkerAllocation(presentedBuilding(gui, *building),0));
	}

	TEST_CASE("priority and radius requests clamp and reconcile against real orders")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{.header=true});
        auto& gui=world.gui;
        gui.localPlayer=0; gui.localTeamNo=0;
        auto* flag=world.addBuilding("warflag",4,4);
        REQUIRE(gui.requestBuildingPriority(presentedBuilding(gui, *flag),99));
        CHECK(gui.displayedPriority(*flag)==1);
        CHECK_FALSE(gui.requestBuildingPriority(presentedBuilding(gui, *flag),2));
        REQUIRE(gui.requestFlagRange(presentedBuilding(gui, *flag),999));
        CHECK(gui.displayedUnitStayRange(*flag)==flag->type->maxUnitStayRange);
        CHECK_FALSE(gui.requestFlagRange(presentedBuilding(gui, *flag),999));
        while (!gui.orderQueue.empty())
        {
            auto order=gui.orderQueue.front(); gui.orderQueue.pop_front(); order->sender=0;
            gui.executeOrder(order);
        }
        CHECK(flag->priority==1);
        CHECK(flag->unitStayRange==flag->type->maxUnitStayRange);
        presentedBuilding(gui, *flag);
        CHECK_FALSE(gui.pendingFor(flag->gid).pendingPriority.has_value());
        CHECK_FALSE(gui.pendingFor(flag->gid).pendingUnitStayRange.has_value());
        globals->replaying=true;
        CHECK_FALSE(gui.requestBuildingPriority(presentedBuilding(gui, *flag),-1));
        CHECK_FALSE(gui.requestFlagRange(presentedBuilding(gui, *flag),0));
        CHECK_FALSE(gui.requestWorkerAllocation(presentedBuilding(gui, *flag),0));
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
        pending.pendingMinWorkerLevelToFlag=1;
        pending.pendingExplorersRequireBombing=true;
        pending.pendingRatio=std::array<Sint32,NB_UNIT_TYPE>{2,1,3};
        pending.pendingClearingResources=std::array<bool,MaterialCount>{true,false,true};
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
        CHECK(pending.pendingMinWorkerLevelToFlag.has_value());
        CHECK(pending.pendingExplorersRequireBombing.has_value());
        auto workerLevel=std::make_shared<OrderModifyMinLevelToFlag>(gid,1,2);
        workerLevel->sender=0; gui.reconcileBuildingGuiState(workerLevel);
        CHECK_FALSE(pending.pendingMinWorkerLevelToFlag.has_value());
        CHECK(pending.pendingExplorersRequireBombing.has_value());
        Sint32 ratios[NB_UNIT_TYPE]={2,1,2};
        auto ratio=std::make_shared<OrderModifySwarm>(gid,ratios);
        ratio->sender=0; gui.reconcileBuildingGuiState(ratio);
        CHECK(pending.pendingRatio.has_value());
        ratio->ratio[2]=3; gui.reconcileBuildingGuiState(ratio);
        CHECK_FALSE(pending.pendingRatio.has_value());
        bool mask[MaterialCount]={true,false,false};
        auto clearing=std::make_shared<OrderModifyClearingFlag>(gid,mask);
        clearing->sender=0; gui.reconcileBuildingGuiState(clearing);
        CHECK(pending.pendingClearingResources.has_value());
        clearing->clearingMaterials[2]=true; gui.reconcileBuildingGuiState(clearing);
        CHECK_FALSE(pending.pendingClearingResources.has_value());
        // Unknown/missing entries must not create phantom GUI state.
        auto absent=std::make_shared<OrderChangePriority>(999,1);
        absent->sender=0; gui.reconcileBuildingGuiState(absent);
        gui.reconcileBuildingGuiState(std::make_shared<NullOrder>());
        CHECK(gui.buildingGuiState.size()==1);
    }
}
