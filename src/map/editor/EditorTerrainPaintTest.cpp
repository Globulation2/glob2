// SPDX-License-Identifier: GPL-3.0-or-later

// Map editor terrain and resource brushes: every brush is centred on the cell
// under the pointer, the hover preview names exactly the cells a stroke
// changes, legacy corner terrain no longer erases neighbouring authored cells
// or the units, buildings and resources it allows, Del reverts only the
// selected terrain, and a resource stroke that places nothing explains why.

#include "BrushCoverage.h"
#include "BuildingType.h"
#include "EngineFixtures.h"
#include "ExperimentalFeatures.h"
#include "MapEdit.h"
#include "Race.h"
#include "Unit.h"
#include <SDL3/SDL.h>
#include <set>

namespace
{
using Cell = std::pair<int, int>;
constexpr int MapSide = 32;

void blank(MapEdit &editor, TerrainType background = GRASS)
{
	editor.game.map.setSize(5, 5, background);
	editor.game.map.setGame(&editor.game);
	editor.game.addTeam();
	editor.game.teams[0]->race.loadDefault();
	for (int y = 0; y < MapSide; ++y)
		for (int x = 0; x < MapSide; ++x)
			editor.game.map.clearImmobileUnit(x, y);
	editor.viewportX = 0;
	editor.viewportY = 0;
	editor.updateCamera();
	editor.minimap.setGame(editor.game);
}

void cursor(MapEdit &editor, int x, int y)
{
	editor.mouseX = x * 32 + 16;
	editor.mouseY = y * 32 + 16;
}

void stroke(MapEdit &editor, int x, int y)
{
	cursor(editor, x, y);
	editor.performAction("terrain drag start");
	editor.performAction("terrain drag end");
}

Cell wrapped(Cell cell)
{
	return {cell.first & (MapSide - 1), cell.second & (MapSide - 1)};
}

std::set<Cell> wrappedSet(const std::vector<Cell> &cells)
{
	std::set<Cell> out;
	for (auto cell : cells)
		out.insert(wrapped(cell));
	return out;
}

std::set<Cell> cellsOf(MapEdit &editor, TerrainType type)
{
	std::set<Cell> out;
	for (int y = 0; y < MapSide; ++y)
		for (int x = 0; x < MapSide; ++x)
			if (editor.game.map.terrainTypeAt(x, y) == type)
				out.insert({x, y});
	return out;
}

std::vector<Cell> hoverPreview(MapEdit &editor, int x, int y)
{
	cursor(editor, x, y);
	const auto [cx, cy] = editor.brushCellAt(editor.mapMouseX(editor.mouseX), editor.mapMouseY(editor.mouseY));
	return editor.terrainStrokeCells(cx, cy);
}

TerrainType importCustom(MapEdit &editor)
{
	editor.game.map.importTerrainDefinitions(
		R"({"schemaVersion":1,"terrains":[{"key":"fixture:moss","name":"Moss","base":"grass","appearance":"grass","properties":{"groundSpeedQ8":192}}]})");
	return *editor.game.map.terrainRegistry().find("fixture:moss");
}

void select(MapEdit &editor, TerrainType type)
{
	editor.beginTerrainPlacement(TerrainSelector::selectorFor(type), MapEdit::TerrainPlacementMode::BaseTerrain);
	REQUIRE(editor.terrainType == TerrainSelector::selectorFor(type));
}

glob2test::GlobalsOptions displayOptions()
{
	return {.display = true, .width = 1024, .height = 768, .screenFlags = GAGCore::GraphicContext::PORTABLEGPU};
}
} // namespace

TEST_SUITE("EditorTerrainPaint")
{
	TEST_CASE("every terrain brush paints exactly the cells its hover preview shows [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		globals->settings.experiments.set(ExperimentId::IceTerrain, true);
		for (unsigned figure = 0; figure < BrushTool::BRUSH_COUNT; ++figure)
			for (const char *kind : {"grass", "sand", "water", "catalogue", "custom"})
				for (const Cell centre : {Cell{16, 16}, Cell{0, 0}})
				{
					INFO("figure " << figure << " " << kind << " at " << centre.first << "," << centre.second);
					MapEdit editor;
					// Grass is painted over water so its own cells stand out.
					blank(editor, std::string(kind) == "grass" ? WATER : GRASS);
					TerrainType type = GRASS;
					if (std::string(kind) == "sand") type = SAND;
					else if (std::string(kind) == "water") type = WATER;
					else if (std::string(kind) == "catalogue") type = ICE;
					else if (std::string(kind) == "custom") type = importCustom(editor);
					select(editor, type);
					CHECK(editor.brush.getType() == BrushTool::MODE_ADD);
					CHECK(editor.brush.addRemoveEnabled);
					editor.brush.setFigure(figure);
					const auto preview = hoverPreview(editor, centre.first, centre.second);
					const auto figureCells = BrushCoverage::stamp(figure, centre, centre);
					// Only a corner terrain fills the gaps of the checkerboard figures.
					const bool legacy = type == GRASS || type == SAND || type == WATER;
					CHECK(std::set<Cell>(preview.begin(), preview.end()) ==
						  (legacy ? BrushCoverage::cornerClosure(figureCells) : figureCells));
					editor.performAction("terrain drag start");
					editor.performAction("terrain drag end");
					CHECK(cellsOf(editor, type) == wrappedSet(preview));
					CHECK(editor.hasMapBeenModified);
				}
	}

	TEST_CASE("a corner terrain stroke leaves neighbouring authored cells and their contents [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		MapEdit editor;
		blank(editor);
		auto &map = editor.game.map;
		// Authored whole-cell materials beside, below and diagonal to the stroke.
		for (const Cell cell : {Cell{17, 16}, Cell{16, 17}, Cell{17, 17}, Cell{15, 15}})
			map.setCellTerrain(cell.first, cell.second, HEDGE);
		map.setCellTerrain(14, 16, ICE);
		for (const auto type : {WATER, SAND, GRASS})
		{
			INFO(int(type));
			select(editor, type);
			editor.brush.setFigure(0);
			stroke(editor, 16, 16);
			CHECK(map.terrainTypeAt(16, 16) == type);
			for (const Cell cell : {Cell{17, 16}, Cell{16, 17}, Cell{17, 17}, Cell{15, 15}})
				CHECK(map.terrainTypeAt(cell.first, cell.second) == HEDGE);
			CHECK(map.terrainTypeAt(14, 16) == ICE);
		}
		// Painting over an authored cell replaces it, and only it.
		select(editor, WATER);
		stroke(editor, 17, 16);
		CHECK(map.terrainTypeAt(17, 16) == WATER);
		CHECK(map.terrainTypeAt(17, 17) == HEDGE);
		CHECK(map.terrainTypeAt(16, 17) == HEDGE);
	}

	TEST_CASE("terrain strokes remove only what the new terrain disallows [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		MapEdit editor;
		blank(editor);
		auto &game = editor.game;
		auto &map = game.map;
		const int swarm = globals->buildingsTypes.getTypeNum("swarm", 0, false);
		Building *building = game.addBuilding(14, 14, swarm, 0);
		Unit *worker = game.addUnit(18, 18, 0, WORKER, 0, 0, 0, 0);
		Unit *explorer = game.addUnit(13, 18, 0, EXPLORER, 0, 0, 0, 0);
		REQUIRE((building && worker && explorer));
		const auto buildingGid = building->gid, workerGid = worker->gid, explorerGid = explorer->gid;
		for (int y = 12; y <= 20; ++y)
			for (int x = 12; x <= 20; ++x)
				if (map.isResourceAllowed(x, y, WHEAT))
					map.setResourceByIndex(x, y, WHEAT, 1);
		const auto wheatBefore = [&]
		{
			int count = 0;
			for (int y = 0; y < MapSide; ++y)
				for (int x = 0; x < MapSide; ++x)
					count += map.getResource(x, y).type == WHEAT;
			return count;
		}();
		REQUIRE(wheatBefore > 0);
		// The grass brush used to delete units, buildings and every resource.
		select(editor, GRASS);
		editor.brush.setFigure(7);
		for (const Cell at : {Cell{16, 16}, Cell{14, 14}, Cell{18, 18}})
			stroke(editor, at.first, at.second);
		CHECK(game.teams[0]->myBuildings[Building::GIDtoID(buildingGid)] != nullptr);
		CHECK(game.teams[0]->myUnits[Unit::GIDtoID(workerGid)] != nullptr);
		CHECK(game.teams[0]->myUnits[Unit::GIDtoID(explorerGid)] != nullptr);
		int wheatAfter = 0;
		for (int y = 0; y < MapSide; ++y)
			for (int x = 0; x < MapSide; ++x)
				wheatAfter += map.getResource(x, y).type == WHEAT;
		CHECK(wheatAfter == wheatBefore);

		// Water removes wheat and the walker it floods, but not algae or the flyer.
		for (int y = 24; y <= 28; ++y)
			for (int x = 24; x <= 28; ++x)
				map.paintLegacyCells({{x, y}}, WATER);
		REQUIRE(map.isResourceAllowed(26, 26, ALGA));
		map.setResourceByIndex(26, 26, ALGA, 1);
		select(editor, WATER);
		editor.brush.setFigure(7);
		stroke(editor, 18, 18);
		stroke(editor, 13, 18);
		stroke(editor, 26, 26);
		CHECK(map.getResource(18, 18).type == NO_RES_TYPE);
		CHECK(map.getResource(26, 26).type == ALGA);
		CHECK(game.teams[0]->myUnits[Unit::GIDtoID(workerGid)] == nullptr);
		CHECK(game.teams[0]->myUnits[Unit::GIDtoID(explorerGid)] != nullptr);
		// The shore ring around the stroke loses the wheat it no longer allows.
		for (int y = 0; y < MapSide; ++y)
			for (int x = 0; x < MapSide; ++x)
				if (map.getResource(x, y).type != NO_RES_TYPE)
				{
					INFO(x << "," << y);
					CHECK(map.terrainSupportsResourceAtByIndex(x, y, map.getResource(x, y).type));
				}
	}

	TEST_CASE("Del reverts only cells of the selected terrain to grass [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		globals->settings.experiments.set(ExperimentId::IceTerrain, true);
		MapEdit editor;
		blank(editor);
		auto &map = editor.game.map;
		// A pond, an ice patch and a hedge, all under one 5x5 Del brush.
		map.paintLegacyCells({{14, 14}, {15, 14}, {14, 15}, {15, 15}}, WATER);
		map.setCellTerrain(17, 17, ICE);
		map.setCellTerrain(18, 17, ICE);
		map.setCellTerrain(17, 18, HEDGE);
		const auto before = cellsOf(editor, GRASS);

		select(editor, ICE);
		editor.brush.setFigure(7);
		editor.brush.mode = BrushTool::MODE_DEL;
		stroke(editor, 17, 17);
		CHECK(map.terrainTypeAt(17, 17) == GRASS);
		CHECK(map.terrainTypeAt(18, 17) == GRASS);
		CHECK(map.terrainTypeAt(17, 18) == HEDGE);
		CHECK(map.terrainTypeAt(14, 14) == WATER);

		// Water Del clears the pond and its sand shore; the hedge stays.
		// A new selection starts in Add.
		select(editor, WATER);
		CHECK(editor.brush.getType() == BrushTool::MODE_ADD);
		editor.brush.mode = BrushTool::MODE_DEL;
		stroke(editor, 15, 15);
		for (int y = 12; y <= 18; ++y)
			for (int x = 12; x <= 18; ++x)
			{
				INFO(x << "," << y);
				if (x == 17 && y == 18)
					CHECK(map.terrainTypeAt(x, y) == HEDGE);
				else
					CHECK(map.terrainTypeAt(x, y) != WATER);
			}
		CHECK(map.terrainTypeAt(15, 15) == GRASS);
		CHECK(map.terrainTypeAt(14, 14) == GRASS);
		CHECK(before.size() <= cellsOf(editor, GRASS).size());

		// Del of grass changes nothing.
		const auto grass = cellsOf(editor, GRASS);
		select(editor, GRASS);
		editor.brush.mode = BrushTool::MODE_DEL;
		stroke(editor, 3, 3);
		CHECK(cellsOf(editor, GRASS) == grass);
	}

	TEST_CASE("a resource stroke that places nothing explains where the resource grows [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		MapEdit editor;
		blank(editor);
		auto &map = editor.game.map;
		editor.performAction("select algae");
		REQUIRE(TerrainSelector::isResource(editor.terrainType));
		editor.brush.setFigure(6);
		cursor(editor, 10, 10);
		const auto footprint = editor.terrainBrushCells(10, 10);
		CHECK(editor.invalidResourceCells(footprint).size() == footprint.size());
		CHECK(editor.lastStatus().empty());
		stroke(editor, 10, 10);
		for (const auto &[x, y] : footprint)
			CHECK(map.getResource(x, y).type == NO_RES_TYPE);
		const auto hint = editor.lastStatus();
		CHECK(hint == editor.resourcePlacementHint());
		CHECK(hint.find(" can only be placed on: ") != std::string::npos);
		CHECK(hint.find("none") == std::string::npos);
		// The preview draws the invalid cells; the status strip draws too.
		editor.draw(SDL_GetTicks());
		globals->gfx->nextFrame();

		// Wheat beside a pond places where it may, and a partly valid stroke stays quiet.
		editor.showStatus("");
		map.paintLegacyCells({{22, 22}}, WATER);
		editor.performAction("select wheat");
		editor.brush.setFigure(7);
		const auto mixed = editor.terrainBrushCells(18, 18);
		const auto invalid = editor.invalidResourceCells(mixed);
		CHECK(!invalid.empty());
		CHECK(invalid.size() < mixed.size());
		stroke(editor, 18, 18);
		CHECK(map.getResource(16, 16).type == WHEAT);
		for (const auto &[x, y] : invalid)
			CHECK(map.getResource(x, y).type == NO_RES_TYPE);
		CHECK(editor.lastStatus().empty());
	}
}
