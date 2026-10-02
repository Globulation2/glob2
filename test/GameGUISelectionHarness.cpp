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
	static void destructionClearsSelection()
	{
		GameGUI gui;
		gui.init();
		gui.localTeamNo = 0;
		Team team(&gui.game);
		team.race.load();
		gui.game.teams[0] = &team;
		const int buildingType = globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);

		// Match Team::syncStep: empty the table slot, then delete the entity.
		// The selection holds a gid + generation, so nothing dangles and the
		// GUI needs no destruction callback.
		auto* building = new Building(0, 0, 2, buildingType, &team,
		                              &globalContainer->buildingsTypes, 0, 0);
		team.myBuildings[2] = building;
		gui.setSelection(GameGUI::BUILDING_SELECTION, building);
		REQUIRE((gui.selectionBuilding() == building && gui.view.selectedBuilding == building));
		team.myBuildings[2] = nullptr;
		delete building;
		REQUIRE(gui.selectionMode == GameGUI::BUILDING_SELECTION);
		REQUIRE(!gui.selectionBuilding());
		gui.iterateSelection();
		assertCleared(gui);

		auto* unit = new Unit(0, 0, 2, WORKER, &team, 0);
		team.myUnits[2] = unit;
		gui.setSelection(GameGUI::UNIT_SELECTION, unit);
		team.myUnits[2] = nullptr;
		delete unit;
		REQUIRE(!gui.selectionUnit());
		gui.iterateSelection();
		assertCleared(gui);

		gui.iterateSelection();
		assertCleared(gui);
		gui.setSelection(GameGUI::BUILDING_SELECTION, static_cast<void*>(nullptr));
		gui.iterateSelection();
		assertCleared(gui);
		gui.setSelection(GameGUI::UNIT_SELECTION, static_cast<void*>(nullptr));
		gui.iterateSelection();
		assertCleared(gui);

		// A live unit with no peer remains selected. No viewport centering
		// occurs in this case, so this also runs without a graphics context.
		unit = new Unit(0, 0, 2, WORKER, &team, 0);
		team.myUnits[2] = unit;
		gui.setSelection(GameGUI::UNIT_SELECTION, unit);
		gui.iterateSelection();
		REQUIRE(gui.selectionMode == GameGUI::UNIT_SELECTION);
		REQUIRE((gui.selectionUnit() == unit && gui.view.selectedUnit == unit));
		gui.clearSelection();
		team.myUnits[2] = nullptr;
		delete unit;
		gui.game.teams[0] = nullptr;
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
		REQUIRE(gui.view.selectedUnit == unit);
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
