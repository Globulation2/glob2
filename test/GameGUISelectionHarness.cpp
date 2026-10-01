// SPDX-License-Identifier: GPL-3.0-or-later
// Link the real GameGUI and entities. No window or simulation loop is needed
// to exercise cycling between entity destruction and the next GUI draw.
#include "EngineFixtures.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "GameGUIKeyActions.h"
#include "IntBuildingType.h"
#include "Unit.h"


class GameGUISelectionHarness
{
public:
	static void run()
	{
		GameGUI gui;
		gui.init();
		gui.localTeamNo = 0;
		Team team(&gui.game);
		team.race.load();
		gui.game.teams[0] = &team;
		const int buildingType = globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);

		// Match Team::syncStep: notify the GUI, empty the table slot, then
		// delete the entity. The cached payload is still non-null at deletion.
		auto* building = new Building(0, 0, 2, buildingType, &team,
		                              &globalContainer->buildingsTypes, 0, 0);
		team.myBuildings[2] = building;
		gui.setSelection(GameGUI::BUILDING_SELECTION, building);
		gui.onBuildingDestroyed(building);
		team.myBuildings[2] = nullptr;
		REQUIRE((gui.selectionBuilding() == building && !gui.view.selectedBuilding));
		delete building;
		gui.iterateSelection();
		assertCleared(gui);

		auto* unit = new Unit(0, 0, 2, WORKER, &team, 0);
		team.myUnits[2] = unit;
		gui.setSelection(GameGUI::UNIT_SELECTION, unit);
		gui.onUnitDestroyed(unit);
		team.myUnits[2] = nullptr;
		REQUIRE((gui.selectionUnit() == unit && !gui.view.selectedUnit));
		delete unit;
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
		gui.game.teams[0] = nullptr;
	}

private:
	static void assertCleared(const GameGUI& gui)
	{
		REQUIRE(gui.selectionMode == GameGUI::NO_SELECTION);
		REQUIRE(std::holds_alternative<std::monostate>(gui.selection));
		REQUIRE((!gui.view.selectedBuilding && !gui.view.selectedUnit));
		REQUIRE((gui.viewportX == 0 && gui.viewportY == 0));
	}
};

TEST_SUITE("GameGUISelection")
{
	TEST_CASE("selection survives entity destruction until the next draw")
	{
		glob2test::HeadlessGlobals globals;
		GameGUISelectionHarness::run();
	}
}
