// SPDX-License-Identifier: GPL-3.0-or-later

// Map editor terrain and resource brushes: a terrain brush is centred on the
// vertex nearest the pointer and paints exactly the vertices its hover preview
// shows (down to a single vertex), grass and water get a sand beach where they
// would meet, a stroke keeps the units, buildings and resources the new terrain
// allows, Del reverts only the selected terrain, and a resource stroke (still
// on cells) that places nothing explains why.

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
	editor.minimap.setMapSize(editor.game.map.getW(), editor.game.map.getH());
    editor.preparePresentation();
}

void cursor(MapEdit &editor, int x, int y)
{
	editor.mouseX = x * 32 + 16;
	editor.mouseY = y * 32 + 16;
}

//! The pointer exactly on vertex (x,y), the top-left corner of cell (x,y).
void cursorVertex(MapEdit &editor, int x, int y)
{
	editor.mouseX = x * 32;
	editor.mouseY = y * 32;
}

void stroke(MapEdit &editor, int x, int y)
{
	cursor(editor, x, y);
	editor.performAction("terrain drag start");
	editor.performAction("terrain drag end");
}

void vertexStroke(MapEdit &editor, int x, int y)
{
	cursorVertex(editor, x, y);
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

std::set<Cell> verticesOf(MapEdit &editor, TerrainType type)
{
	std::set<Cell> out;
	for (int y = 0; y < MapSide; ++y)
		for (int x = 0; x < MapSide; ++x)
			if (editor.game.map.vertexTerrainAt(x, y) == type)
				out.insert({x, y});
	return out;
}

std::vector<Cell> hoverPreview(MapEdit &editor, int x, int y)
{
	cursorVertex(editor, x, y);
	const auto [cx, cy] = editor.brushCellAt(editor.mapMouseX(editor.mouseX), editor.mapMouseY(editor.mouseY));
	return editor.terrainBrushCells(cx, cy);
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
	TEST_CASE("every terrain brush paints exactly the vertices its hover preview shows [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		globals->settings.experiments.set(ExperimentId::IceTerrain, true);
		for (unsigned figure = 0; figure < BrushTool::BRUSH_COUNT; ++figure)
			for (const char *kind : {"grass", "sand", "water", "catalogue", "custom"})
				for (const Cell centre : {Cell{16, 16}, Cell{0, 0}})
				{
					INFO("figure " << figure << " " << kind << " at " << centre.first << "," << centre.second);
					MapEdit editor;
					// Grass is painted over water so its own vertices stand out.
					blank(editor, std::string(kind) == "grass" ? WATER : GRASS);
					TerrainType type = GRASS;
					if (std::string(kind) == "sand") type = SAND;
					else if (std::string(kind) == "water") type = WATER;
					else if (std::string(kind) == "catalogue") type = ICE;
					else if (std::string(kind) == "custom") type = importCustom(editor);
					select(editor, type);
					CHECK(editor.brushOnVertices());
					CHECK(editor.brush.getType() == BrushTool::MODE_ADD);
					CHECK(editor.brush.addRemoveEnabled);
					editor.brush.setFigure(figure);
					const auto preview = hoverPreview(editor, centre.first, centre.second);
					CHECK(std::set<Cell>(preview.begin(), preview.end()) == BrushCoverage::stamp(figure, centre, centre));
					editor.performAction("terrain drag start");
					editor.performAction("terrain drag end");
					CHECK(verticesOf(editor, type) == wrappedSet(preview));
					CHECK(editor.hasMapBeenModified);
				}
	}

	TEST_CASE("the smallest brush paints one vertex and lays a beach around water [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		MapEdit editor;
		blank(editor);
		auto &map = editor.game.map;
		// Authored terrain two vertices away is out of the beach's reach.
		map.setVertexTerrain(18, 16, HEDGE);
		map.setVertexTerrain(14, 16, ICE);
		select(editor, WATER);
		editor.brush.setFigure(0);
		vertexStroke(editor, 16, 16);
		CHECK(map.vertexTerrainAt(16, 16) == WATER);
		for (int y = 15; y <= 17; ++y)
			for (int x = 15; x <= 17; ++x)
				if (x != 16 || y != 16)
					CHECK(map.vertexTerrainAt(x, y) == SAND);
		CHECK(map.vertexTerrainAt(18, 16) == HEDGE);
		CHECK(map.vertexTerrainAt(14, 16) == ICE);
		CHECK(map.vertexTerrainAt(16, 18) == GRASS);
		// One vertex reaches the four cells around it.
		CHECK(map.cellCorners(15, 15) == std::array{SAND, SAND, SAND, WATER});
		CHECK(map.terrainTypeAt(16, 16) == MIXED_TERRAIN);
		// Sand needs no beach, and painting over authored terrain replaces just it.
		select(editor, SAND);
		vertexStroke(editor, 18, 16);
		CHECK(map.vertexTerrainAt(18, 16) == SAND);
		CHECK(map.vertexTerrainAt(19, 16) == GRASS);
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
			vertexStroke(editor, at.first, at.second);
		CHECK(game.teams[0]->myBuildings[Building::GIDtoID(buildingGid)] != nullptr);
		CHECK(game.teams[0]->myUnits[Unit::GIDtoID(workerGid)] != nullptr);
		CHECK(game.teams[0]->myUnits[Unit::GIDtoID(explorerGid)] != nullptr);
		int wheatAfter = 0;
		for (int y = 0; y < MapSide; ++y)
			for (int x = 0; x < MapSide; ++x)
				wheatAfter += map.getResource(x, y).type == WHEAT;
		CHECK(wheatAfter == wheatBefore);

		// Water removes wheat and the walker it floods, but not algae or the flyer.
		std::vector<Cell> pond;
		for (int y = 24; y <= 29; ++y)
			for (int x = 24; x <= 29; ++x)
				pond.push_back({x, y});
		map.paintVertices(pond, WATER);
		REQUIRE(map.isResourceAllowed(26, 26, ALGA));
		map.setResourceByIndex(26, 26, ALGA, 1);
		select(editor, WATER);
		editor.brush.setFigure(7);
		vertexStroke(editor, 18, 18);
		vertexStroke(editor, 13, 18);
		vertexStroke(editor, 26, 26);
		CHECK(map.getResource(18, 18).type == NO_RES_TYPE);
		CHECK(map.getResource(26, 26).type == ALGA);
		CHECK(game.teams[0]->myUnits[Unit::GIDtoID(workerGid)] == nullptr);
		CHECK(game.teams[0]->myUnits[Unit::GIDtoID(explorerGid)] != nullptr);
		// The beach around the stroke loses the wheat it no longer allows.
		for (int y = 0; y < MapSide; ++y)
			for (int x = 0; x < MapSide; ++x)
				if (map.getResource(x, y).type != NO_RES_TYPE)
				{
					INFO(x << "," << y);
					CHECK(map.terrainSupportsResourceAtByIndex(x, y, map.getResource(x, y).type));
				}
	}

	TEST_CASE("Del reverts only vertices of the selected terrain to grass [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		globals->settings.experiments.set(ExperimentId::IceTerrain, true);
		MapEdit editor;
		blank(editor);
		auto &map = editor.game.map;
		// A pond, an ice patch and a hedge, all under one Del brush.
		map.paintVertices({{14, 14}, {15, 14}, {16, 14}, {14, 15}, {15, 15}, {16, 15}, {14, 16}, {15, 16}, {16, 16}}, WATER);
		map.setVertexTerrain(17, 17, ICE);
		map.setVertexTerrain(18, 17, ICE);
		map.setVertexTerrain(17, 18, HEDGE);

		select(editor, ICE);
		editor.brush.setFigure(7);
		editor.brush.mode = BrushTool::MODE_DEL;
		vertexStroke(editor, 17, 17);
		CHECK(map.vertexTerrainAt(17, 17) == GRASS);
		CHECK(map.vertexTerrainAt(18, 17) == GRASS);
		CHECK(map.vertexTerrainAt(17, 18) == HEDGE);
		CHECK(map.vertexTerrainAt(15, 15) == WATER);

		// Water Del clears the pond; the hedge stays. A new selection starts in Add.
		select(editor, WATER);
		CHECK(editor.brush.getType() == BrushTool::MODE_ADD);
		editor.brush.mode = BrushTool::MODE_DEL;
		vertexStroke(editor, 15, 15);
		CHECK(verticesOf(editor, WATER).empty());
		CHECK(map.vertexTerrainAt(17, 18) == HEDGE);
		CHECK(map.vertexTerrainAt(15, 15) == GRASS);

		// Del of grass changes nothing.
		const auto grass = verticesOf(editor, GRASS);
		select(editor, GRASS);
		editor.brush.mode = BrushTool::MODE_DEL;
		vertexStroke(editor, 3, 3);
		CHECK(verticesOf(editor, GRASS) == grass);
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
		map.paintVertices({{22, 22}, {23, 22}, {22, 23}, {23, 23}}, WATER);
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
