// SPDX-License-Identifier: GPL-3.0-or-later
// The desktop/tablet editor dock (EditorDock.h): layout at common window sizes,
// event routing at its edge, live catalogue updates, locked experiments, palette
// navigation and the object inspector.
#include "EditorDock.h"
#include "EngineFixtures.h"
#include "ExperimentalFeatures.h"
#include "MapEdit.h"
#include "MapEditInspector.h"
#include "Race.h"
#include <nlohmann/json.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <map>
#include <set>

namespace
{
using Json = nlohmann::json;
using GAGGUI::ui::Node;
using GAGGUI::ui::Rect;

glob2test::GlobalsOptions window(int w, int h)
{
	return {.display = true, .width = w, .height = h, .screenFlags = GAGCore::GraphicContext::PORTABLEGPU};
}

void blank(MapEdit &editor)
{
	editor.game.map.setSize(6, 6, GRASS);
	editor.game.map.setGame(&editor.game);
	editor.game.addTeam();
	editor.game.teams[0]->race.loadDefault();
	for (int y = 0; y < 64; ++y)
		for (int x = 0; x < 64; ++x)
			editor.game.map.clearImmobileUnit(x, y);
	editor.viewportX = 0;
	editor.viewportY = 0;
	editor.updateCamera();
	editor.minimap.setMapSize(editor.game.map.getW(), editor.game.map.getH());
    editor.preparePresentation();
}

void collect(Node *node, bool inScroll, std::vector<std::pair<Node *, bool>> &out)
{
	if (!node)
		return;
	if (node->interactive() && !node->scrollable())
		out.push_back({node, inScroll});
	const bool scroll = inScroll || node->key == "dock/scroll";
	for (auto &child : node->children)
		collect(child.get(), scroll, out);
}

void frame(MapEdit &editor)
{
	editor.draw(SDL_GetTicks());
	globalContainer->gfx->nextFrame();
	editor.dock->host().layoutIfNeeded();
}

void capture(MapEdit &editor, const std::string &name)
{
	editor.draw(SDL_GetTicks());
	globalContainer->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory() + "/" + name + ".bmp");
	globalContainer->gfx->nextFrame();
}

// Every interactive control lies inside the dock, none overlap, keys are unique,
// and the last control of the scrolling list can be scrolled fully into view.
void checkLayout(MapEdit &editor, const std::string &what)
{
	INFO(what);
	auto &dock = *editor.dock;
	auto &host = dock.host();
	host.layoutIfNeeded();
	const Rect area = dock.rect();
	const Rect scroll = host.bounds("dock/scroll");
	REQUIRE(scroll.h > 0);
	CHECK(area.contains(scroll));
	std::vector<std::pair<Node *, bool>> nodes;
	collect(host.root(), false, nodes);
	REQUIRE_FALSE(nodes.empty());
	std::set<std::string> keys;
	std::vector<std::pair<Node *, Rect>> visible;
	Node *last = nullptr;
	for (auto [node, inScroll] : nodes)
	{
		INFO(node->key);
		CHECK_FALSE(node->key.empty());
		CHECK(keys.insert(node->key).second);
		const Rect b = node->bounds;
		CHECK(b.w > 0);
		CHECK(b.h > 0);
		CHECK(b.x >= area.x);
		CHECK(b.right() <= area.right());
		if (!inScroll)
		{
			CHECK(area.contains(b));
			CHECK_FALSE(b.intersects(scroll));
			visible.push_back({node, b});
		}
		else
		{
			if (!last || b.bottom() >= last->bounds.bottom())
				last = node;
			if (scroll.contains(b))
				visible.push_back({node, b});
		}
	}
	for (std::size_t i = 0; i < visible.size(); ++i)
		for (std::size_t j = i + 1; j < visible.size(); ++j)
		{
			INFO(visible[i].first->key << " / " << visible[j].first->key);
			CHECK_FALSE(visible[i].second.intersects(visible[j].second));
		}
	REQUIRE(last);
	const std::string lastKey = last->key;
	host.scrollIntoView(lastKey);
	host.layoutIfNeeded();
	INFO(lastKey);
	CHECK(host.bounds("dock/scroll").contains(host.bounds(lastKey)));
}

SDL_Event mouse(Uint32 type, int x, int y)
{
	SDL_Event event{};
	event.type = type;
	if (type == SDL_EVENT_MOUSE_MOTION)
	{
		event.motion.x = float(x);
		event.motion.y = float(y);
	}
	else
	{
		event.button.x = float(x);
		event.button.y = float(y);
		event.button.button = SDL_BUTTON_LEFT;
		event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
	}
	return event;
}

void click(MapEdit &editor, int x, int y)
{
	for (auto type : {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP})
	{
		auto event = mouse(type, x, y);
		editor.processEvent(event);
	}
}

void tap(MapEdit &editor, const std::string &key)
{
	auto &host = editor.dock->host();
	host.layoutIfNeeded();
	REQUIRE(host.find(key));
	host.scrollIntoView(key);
	host.layoutIfNeeded();
	const auto b = host.bounds(key);
	click(editor, b.x + b.w / 2, b.y + b.h / 2);
}
} // namespace

TEST_SUITE("EditorDockLayout")
{
	TEST_CASE("dock controls fit inside the dock without overlapping at common window sizes [display][artifacts]")
	{
		const std::pair<int, int> sizes[] = {{1024, 480}, {1024, 600}, {1280, 720}, {1920, 1080}};
		for (const auto &[w, h] : sizes)
		{
			glob2test::HeadlessGlobals globals(window(w, h));
			globals->settings.experiments.set(ExperimentId::ObstacleTerrain, true);
			MapEdit editor;
			blank(editor);
			REQUIRE(editor.dock);
			const auto area = editor.dock->rect();
			const int width = globals->gfx->getW();
			INFO(w << "x" << h << " logical " << width << "x" << globals->gfx->getH());
			CHECK(area.right() == width);
			CHECK(area.y == 0);
			CHECK(area.h == globals->gfx->getH());
			CHECK(area.w >= std::min(240, int(width * 0.4)));
			CHECK(area.w <= std::max(240, int(width * 0.4)));
			CHECK(editor.dockWidth() == area.w);
			for (auto tab : {EditorDock::Tab::Terrain, EditorDock::Tab::Resources, EditorDock::Tab::Buildings,
							 EditorDock::Tab::FlagsAndUnits, EditorDock::Tab::Teams})
			{
				editor.dock->showTab(tab);
				if (tab == EditorDock::Tab::Terrain)
					editor.performAction("select terrain grass");
				frame(editor);
				const std::string name = std::to_string(w) + "x" + std::to_string(h) + "-tab" + std::to_string(int(tab));
				checkLayout(editor, name);
				capture(editor, "dock-" + name);
			}
		}
	}

	TEST_CASE("a click one pixel left of the dock paints the map and a click inside does not [display]")
	{
		glob2test::HeadlessGlobals globals(window(1280, 720));
		MapEdit editor;
		blank(editor);
		REQUIRE(editor.dock);
		editor.dock->showTab(EditorDock::Tab::Terrain);
		editor.performAction("select terrain sand");
		editor.brush.setFigure(0);
		REQUIRE(editor.currentBrushId() == "terrain/sand");
		const auto area = editor.dock->rect();
		const int y = 300;
		auto cellAt = [&](int x)
		{
			int cx = 0, cy = 0;
			// Terrain brushes paint the vertex nearest the pointer.
			editor.game.map.displayToMapCaseUnaligned(editor.mapMouseX(x), editor.mapMouseY(y), &cx, &cy,
													  editor.viewportX, editor.viewportY);
			return std::pair{cx, cy};
		};
		const auto [mx, my] = cellAt(area.x - 1);
		REQUIRE(editor.game.map.vertexTerrainAt(mx, my) == GRASS);
		click(editor, area.x - 1, y);
		CHECK(editor.game.map.vertexTerrainAt(mx, my) == SAND);
		CHECK_FALSE(editor.isDraggingTerrain);
		// Inside the dock the same click belongs to the dock: the map is untouched
		// and the brush preview is hidden.
		const auto [ix, iy] = cellAt(area.x + 1);
		const auto before = editor.game.map.vertexTerrainAt(ix, iy);
		click(editor, area.x + 1, y);
		CHECK(editor.game.map.vertexTerrainAt(ix, iy) == before);
		CHECK(editor.pointerOverInterface());
		// A stroke that starts on the map keeps painting when it crosses into the dock.
		auto down = mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, area.x - 40, y);
		editor.processEvent(down);
		CHECK(editor.isDraggingTerrain);
		auto move = mouse(SDL_EVENT_MOUSE_MOTION, area.x + 20, y);
		editor.processEvent(move);
		auto up = mouse(SDL_EVENT_MOUSE_BUTTON_UP, area.x + 20, y);
		editor.processEvent(up);
		CHECK_FALSE(editor.isDraggingTerrain);
	}

	TEST_CASE("importing terrain and resource definitions adds dock cards without restarting [display]")
	{
		glob2test::HeadlessGlobals globals(window(1280, 720));
		MapEdit editor;
		blank(editor);
		REQUIRE(editor.dock);
		editor.dock->showTab(EditorDock::Tab::Terrain);
		frame(editor);
		auto &host = editor.dock->host();
		CHECK_FALSE(host.find("brush/terrain/example:moss"));
		editor.game.map.importTerrainDefinitions(
			Json{{"schemaVersion", 1},
				 {"terrains", Json::array({{{"key", "example:moss"}, {"name", "Moss"}, {"base", "grass"},
											{"properties", {{"groundSpeedQ8", 192}}}, {"appearance", "sand"}}})}}
				.dump());
		frame(editor);
		REQUIRE(host.find("brush/terrain/example:moss"));
		tap(editor, "brush/terrain/example:moss");
		CHECK(editor.currentBrushId() == "terrain/example:moss");

		editor.dock->showTab(EditorDock::Tab::Resources);
		frame(editor);
		CHECK_FALSE(host.find("brush/resource/example:berry"));
		auto source = Json::parse(editor.game.map.resourceRegistry().serialize())["resources"][0];
		source["key"] = "example:berry";
		editor.game.map.installResourceDefinitions(Json{{"schemaVersion", 1}, {"resources", Json::array({source})}}.dump());
		frame(editor);
		REQUIRE(host.find("brush/resource/example:berry"));
		tap(editor, "brush/resource/example:berry");
		CHECK(editor.currentBrushId() == "resource/example:berry");
	}

	TEST_CASE("locked brushes are shown and one tap enables their experiment for the map [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(window(1280, 720));
		MapEdit editor;
		blank(editor);
		REQUIRE(editor.dock);
		REQUIRE_FALSE(editor.experimentEnabled("foundation-resources"));
		editor.performAction("open resource palette foundation-resources");
		CHECK(editor.dock->tab() == EditorDock::Tab::Resources);
		frame(editor);
		auto &host = editor.dock->host();
		REQUIRE(host.find("brush/resource/silica"));
		CHECK_FALSE(host.find("brush/resource/silica")->enabled());
		tap(editor, "brush/resource/silica");
		CHECK(editor.currentBrushId().empty());
		capture(editor, "dock-locked");
		editor.hasMapBeenModified = false;
		tap(editor, "dock/enable/resources/foundation-resources");
		CHECK(editor.experimentEnabled("foundation-resources"));
		CHECK(editor.hasMapBeenModified);
		frame(editor);
		REQUIRE(host.find("brush/resource/silica"));
		CHECK(host.find("brush/resource/silica")->enabled());
		CHECK_FALSE(host.find("dock/enable/resources/foundation-resources"));
		tap(editor, "brush/resource/silica");
		CHECK(editor.currentBrushId() == "resource/silica");
	}

	TEST_CASE("palette actions switch tabs expand the group and scroll it to the top [display]")
	{
		glob2test::HeadlessGlobals globals(window(1024, 600));
		globals->settings.experiments.set(ExperimentId::ObstacleTerrain, true);
		MapEdit editor;
		blank(editor);
		REQUIRE(editor.dock);
		editor.dock->showTab(EditorDock::Tab::Buildings);
		editor.dockCollapsed.insert("terrain/obstacles");
		frame(editor);
		editor.performAction("open terrain palette obstacles");
		CHECK(editor.dock->tab() == EditorDock::Tab::Terrain);
		CHECK_FALSE(editor.dockCollapsed.count("terrain/obstacles"));
		frame(editor);
		auto &host = editor.dock->host();
		const auto scroll = host.bounds("dock/scroll");
		const auto heading = host.bounds("dock/section/terrain/obstacles");
		CHECK(heading.y >= scroll.y);
		CHECK(heading.y <= scroll.y + 4);
		CHECK(scroll.contains(host.bounds("brush/terrain/hedge")));
		// Section headers collapse and expand their group.
		tap(editor, "dock/section/terrain/obstacles");
		frame(editor);
		CHECK(editor.dockCollapsed.count("terrain/obstacles"));
		CHECK_FALSE(host.find("brush/terrain/hedge"));
		// Searching finds brushes in every section, including collapsed ones, and
		// selecting a result lands on its tab.
		editor.dock->showTab(EditorDock::Tab::Teams);
		editor.dock->showTab(EditorDock::Tab::FlagsAndUnits);
		editor.dock->setSearch("hedge");
		frame(editor);
		REQUIRE(host.find("brush/terrain/hedge"));
		CHECK_FALSE(host.find("brush/terrain/grass"));
		CHECK_FALSE(host.find("brush/zone/forbidden"));
		tap(editor, "brush/terrain/hedge");
		CHECK(editor.currentBrushId() == "terrain/hedge");
		CHECK(editor.dock->tab() == EditorDock::Tab::Terrain);
	}

	TEST_CASE("selecting a building shows an inspector whose steppers edit it [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(window(1280, 720));
		MapEdit editor;
		blank(editor);
		REQUIRE(editor.dock);
		editor.performAction("set place building selection inn");
		editor.mouseX = 10 * 32 + 16;
		editor.mouseY = 10 * 32 + 16;
		editor.performAction("place building");
		editor.performAction("unselect");
		editor.performAction("select map building");
		REQUIRE(editor.panelMode == MapEdit::BuildingEditor);
		frame(editor);
		auto model = buildInspectorModel(editor);
		REQUIRE(model.kind == InspectorModel::Kind::Building);
		REQUIRE_FALSE(model.rows.empty());
		auto &host = editor.dock->host();
		REQUIRE(host.find("dock/inspect/hp"));
		checkLayout(editor, "inspector");
		capture(editor, "dock-inspector");
		auto *hp = model.rows.front().value;
		const int before = hp->currentValue();
		hp->setValue(before - 1);
		frame(editor);
		CHECK(buildInspectorModel(editor).rows.front().value->currentValue() == before - 1);
		// Closing returns to the tab the author came from.
		tap(editor, "dock/inspect/close");
		CHECK(editor.panelMode != MapEdit::BuildingEditor);
		frame(editor);
		CHECK_FALSE(host.find("dock/inspect/hp"));
	}
}
