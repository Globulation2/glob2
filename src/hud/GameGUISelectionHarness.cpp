// SPDX-License-Identifier: GPL-3.0-or-later
// Link the real GameGUI and entities. No window is needed to exercise how a
// selection reacts to entity destruction, gid reuse and conversion between
// simulation ticks and the next GUI draw.
#include "EngineFixtures.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "GameGUIKeyActions.h"
#include "IntBuildingType.h"
#include "Order.h"
#include "Unit.h"

#include <memory>


class GameGUISelectionHarness
{
public:
    static void frozenInputAdmission(bool threaded)
    {
        glob2test::HeadlessGame world;
        auto& gui = world.gui;
        auto* building = world.addBuilding("swarm",4,4);
        gui.setSelection(GameGUI::BUILDING_SELECTION, building);
        PresentationFrame scene;
        gui.prepareLocalPresentation(scene);
        gui.setPublishedScene(&scene);
        gui.simulationThreaded = threaded;
        const auto original = Game::refOf(building);
        gui.checkSelection();
        REQUIRE(gui.inputBuildingPanel());
        CHECK(gui.view.selectedBuilding == nullptr);
        gui.requestBuildingDestruction(*gui.inputBuildingPanel());
        REQUIRE(gui.orderQueue.size() == 1);
        CHECK(gui.orderQueue.front()->clientTarget == original);
        REQUIRE(world.game.removeUnitAndBuildingAndFlags(4,4,Game::DEL_BUILDING));
        auto* replacement = world.addBuilding("swarm",4,4);
        REQUIRE(replacement->gid == original.gid);
        REQUIRE(replacement->scriptIdentity != original.generation);
        // Both selection and action still use what was actually displayed.
        gui.checkSelection();
        REQUIRE(gui.inputBuildingPanel());
        CHECK(gui.inputBuildingPanel()->state().scriptIdentity == original.generation);
        // An action issued after slot reuse must still stamp the displayed generation.
        gui.orderQueue.clear();
        gui.enqueueOrder(std::make_shared<OrderDelete>(original.gid));
        REQUIRE(gui.orderQueue.front()->clientTarget == original);
        // The simulation admits no order for the replacement incarnation.
        CHECK(gui.getOrder()->getOrderType() == ORDER_NULL);
        gui.enqueueOrder(std::make_shared<NullOrder>());
        gui.orderQueue.front()->clientWorld = scene.map.identity()+1;
        CHECK(gui.getOrder()->getOrderType() == ORDER_NULL);
        gui.prepareLocalPresentation(scene);
        gui.checkSelection();
        CHECK(gui.selectionMode != GameGUI::BUILDING_SELECTION);
        gui.simulationThreaded = false;
        gui.setPublishedScene(nullptr);
    }
    static void destructionClearsSelection()
    {
        glob2test::HeadlessGame world;
        auto& gui=world.gui;
        const auto viewport=std::pair{gui.viewportX,gui.viewportY};
        auto* building=world.addBuilding("swarm",4,4);
        gui.setSelection(GameGUI::BUILDING_SELECTION,building);
        gui.prepareLocalPresentation(gui.frameScene);
        gui.checkSelection();
        REQUIRE(gui.selectionMode==GameGUI::BUILDING_SELECTION);
        REQUIRE(gui.view.selectedBuilding==nullptr);
        REQUIRE(world.game.removeUnitAndBuildingAndFlags(4,4,Game::DEL_BUILDING));
        // The retained frame and its selection remain coherent until replacement.
        gui.checkSelection();
        REQUIRE(gui.selectionMode==GameGUI::BUILDING_SELECTION);
        gui.prepareLocalPresentation(gui.frameScene);
        gui.iterateSelection();
        assertClearedKeepViewport(gui);

        auto* unit=world.addUnit(WORKER,10,10);
        gui.setSelection(GameGUI::UNIT_SELECTION,unit);
        gui.prepareLocalPresentation(gui.frameScene);
        gui.iterateSelection();
        REQUIRE(gui.selectionMode==GameGUI::UNIT_SELECTION);
        REQUIRE(gui.view.selectedUnit==nullptr);
        REQUIRE(world.game.removeUnitAndBuildingAndFlags(10,10,Game::DEL_UNIT));
        gui.checkSelection();
        REQUIRE(gui.selectionMode==GameGUI::UNIT_SELECTION);
        gui.prepareLocalPresentation(gui.frameScene);
        gui.iterateSelection();
        assertClearedKeepViewport(gui);
        gui.iterateSelection();
        assertClearedKeepViewport(gui);
        gui.setSelection(GameGUI::UNIT_SELECTION,static_cast<void*>(nullptr));
        gui.iterateSelection();
        assertClearedKeepViewport(gui);
        CHECK(std::pair{gui.viewportX,gui.viewportY}==viewport);
    }

	// A newcomer that takes the dead entity's slot (same gid) must inherit
	// neither the selection nor the failing-unit recording.
	static void gidReuseDoesNotInheritSelection()
	{
		glob2test::HeadlessGame world;
		GameGUI& gui = world.gui;
		Game& game = world.game;
		gui.localTeamNo = 0;

		Building* first = world.addBuilding("swarm", 4, 4);
		const BuildingRef firstRef = Game::refOf(first);
		gui.setSelection(GameGUI::BUILDING_SELECTION, first);
		REQUIRE(gui.clientRequests.latest().observedBuilding == firstRef);
		REQUIRE(!first->recordFailingUnits);
		game.applyClientRequests();
		REQUIRE(first->recordFailingUnits);

		REQUIRE(game.removeUnitAndBuildingAndFlags(4, 4, Game::DEL_BUILDING));
		Building* second = world.addBuilding("swarm", 4, 4);
		REQUIRE(second->gid == firstRef.gid);
		REQUIRE(second->scriptIdentity != firstRef.generation);
		REQUIRE(gui.selectionMode == GameGUI::BUILDING_SELECTION);
		REQUIRE(!gui.selectionBuilding());
		game.applyClientRequests();
		REQUIRE(!second->recordFailingUnits);
		gui.checkSelection();
		REQUIRE(gui.selectionMode == GameGUI::NO_SELECTION);
		REQUIRE(!gui.view.selectedBuilding);
		REQUIRE(gui.clientRequests.latest().observedBuilding.empty());

		// Switching the observed building moves the recording at the next boundary.
		Building* third = world.addBuilding("swarm", 12, 4);
		gui.setSelection(GameGUI::BUILDING_SELECTION, second);
		game.applyClientRequests();
		REQUIRE(second->recordFailingUnits);
		gui.setSelection(GameGUI::BUILDING_SELECTION, third);
		REQUIRE(second->recordFailingUnits);
		game.applyClientRequests();
		REQUIRE((!second->recordFailingUnits && third->recordFailingUnits));
		gui.clearSelection();
		game.applyClientRequests();
		REQUIRE(!third->recordFailingUnits);

		Unit* unit = world.addUnit(WORKER, 10, 10);
		const Uint16 unitGid = unit->gid;
		gui.setSelection(GameGUI::UNIT_SELECTION, unit);
		REQUIRE(game.removeUnitAndBuildingAndFlags(10, 10, Game::DEL_UNIT));
		Unit* replacement = world.addUnit(WORKER, 12, 12);
		REQUIRE(replacement->gid == unitGid);
		REQUIRE(!gui.selectionUnit());
		gui.checkSelection();
		assertClearedKeepViewport(gui);
	}

	// A building the simulation destroys at a tick reaches the GUI through
	// ClientEvents: the selection clears and the per-gid pending shadow goes.
	static void tickDestructionClearsSelectionAndShadow()
	{
		glob2test::GameOptions options;
		options.header = true;
		glob2test::HeadlessGame world(options);
		GameGUI& gui = world.gui;
		gui.localTeamNo = 0;
		gui.localPlayer = 0;
		Building* building = world.addBuilding("swarm", 4, 4);
		const Uint16 gid = building->gid;
		gui.setSelection(GameGUI::BUILDING_SELECTION, building);
		gui.pendingFor(gid).pendingPriority = 1;

		auto order = std::make_shared<OrderDelete>(gid);
		order->sender = 0;
		gui.executeOrder(order);
		int ticks = 0;
		while (world.game.teams[0]->myBuildings[Building::GIDtoID(gid)] && ticks < 200)
		{
			world.step();
			++ticks;
		}
		REQUIRE(!world.game.teams[0]->myBuildings[Building::GIDtoID(gid)]);
		REQUIRE(gui.buildingGuiState.find(gid) == gui.buildingGuiState.end());
		REQUIRE(!gui.view.selectedBuilding);
		gui.checkSelection();
		REQUIRE(gui.selectionMode == GameGUI::NO_SELECTION);
	}

	// A converted unit keeps its object but moves to another team's slot with a
	// new gid and generation; the selection follows it, as it did by pointer.
	static void conversionKeepsSelection()
	{
		glob2test::GameOptions options;
		options.teams = 2;
		glob2test::HeadlessGame world(options);
		GameGUI& gui = world.gui;
		Unit* unit = world.addUnit(WORKER, 10, 10, 0);
		const UnitRef before = Game::refOf(unit);
		gui.setSelection(GameGUI::UNIT_SELECTION, unit);

		// Replay what Unit::handleActivity does on conversion.
		Team* target = world.game.teams[1];
		const Uint16 targetGid = Unit::GIDfrom(5, 1);
		const Uint32 identity = world.game.allocateScriptIdentity(false, targetGid);
		world.game.publishClientEvent(ClientEvent::UnitConverted{before, UnitRef{targetGid, identity}});
		world.game.teams[0]->myUnits[Unit::GIDtoID(unit->gid)] = nullptr;
		target->myUnits[5] = unit;
		world.game.map.setGroundUnit(10, 10, targetGid);
		unit->scriptIdentity = identity;
		unit->gid = targetGid;
		unit->owner = target;
		REQUIRE(!gui.selectionUnit());
		gui.consumeClientEvents();
		REQUIRE(gui.selectionUnit() == unit);
		REQUIRE(gui.view.selectedUnit == nullptr);
	}

	static void assertClearedKeepViewport(const GameGUI& gui)
	{
		REQUIRE(gui.selectionMode == GameGUI::NO_SELECTION);
		REQUIRE(std::holds_alternative<std::monostate>(gui.selection));
		REQUIRE((!gui.view.selectedBuilding && !gui.view.selectedUnit));
	}

	static void assertCleared(const GameGUI& gui)
	{
		assertClearedKeepViewport(gui);
		REQUIRE((gui.viewportX == 0 && gui.viewportY == 0));
	}
};

TEST_SUITE("GameGUISelection")
{
    TEST_CASE("displayed actions cannot target a reused entity or replaced world")
    {
        glob2test::HeadlessGlobals globals;
        GameGUISelectionHarness::frozenInputAdmission(false);
        GameGUISelectionHarness::frozenInputAdmission(true);
    }

	TEST_CASE("selection clears once its entity is destroyed")
	{
		glob2test::HeadlessGlobals globals;
		GameGUISelectionHarness::destructionClearsSelection();
	}

	TEST_CASE("selection and failing-unit recording do not follow a reused gid")
	{
		glob2test::HeadlessGlobals globals;
		GameGUISelectionHarness::gidReuseDoesNotInheritSelection();
	}

	TEST_CASE("a building destroyed by a tick clears its selection and pending shadow")
	{
		glob2test::HeadlessGlobals globals;
		GameGUISelectionHarness::tickDestructionClearsSelectionAndShadow();
	}

	TEST_CASE("a converted unit stays selected")
	{
		glob2test::HeadlessGlobals globals;
		GameGUISelectionHarness::conversionKeepsSelection();
	}
}
