// SPDX-License-Identifier: GPL-3.0-or-later

// The editor's live presentation (MapEditPresentation.h): phones and small
// windows get the tray, tablets and desktops the dock; resizing or rotating
// switches between them without committing unfinished strokes or losing the
// brush; a mouse wheel over the tray scrolls it and over the map zooms.

#include "EngineFixtures.h"
#include "MapEdit.h"
#include "MapEditPresentation.h"
#include "PhoneEditor.h"
#include "EditorDialogs.h"
#include <ui/Host.h>
#include "Race.h"
#include <InterfacePresentation.h>
#include <SDL3/SDL.h>
#include <optional>
#include <string>

namespace
{
using GAGCore::PresentationPreference;

GAGCore::ResolvedPresentation resolved(double width, double height, bool touch,
									   PresentationPreference preference = PresentationPreference::Automatic)
{
	GAGCore::InputCapabilities input;
	input.touch = touch;
	input.pointer = input.hover = !touch;
	return GAGCore::resolvePresentation(preference, {width, height, 1, {}, 0}, input);
}

// Process-wide presentation state and GLOB2_MOBILE_UI, restored on exit so
// other cases keep the legacy (unresolved) behaviour.
struct PresentationScope
{
	GAGCore::ViewportMetrics viewport = GAGCore::presentationViewport;
	GAGCore::InputCapabilities input = GAGCore::presentationInput;
	GAGCore::ResolvedPresentation state = GAGCore::presentationState;
	PresentationPreference preference = GAGCore::presentationPreference;
	std::optional<std::string> mobile;
	PresentationScope()
	{
		if (const char *value = SDL_getenv_unsafe("GLOB2_MOBILE_UI"))
			mobile = value;
		glob2test::unsetEnv("GLOB2_MOBILE_UI");
		GAGCore::presentationPreference = PresentationPreference::Automatic;
	}
	~PresentationScope()
	{
		if (mobile)
			glob2test::setEnv("GLOB2_MOBILE_UI", mobile->c_str());
		else
			glob2test::unsetEnv("GLOB2_MOBILE_UI");
		GAGCore::presentationViewport = viewport;
		GAGCore::presentationInput = input;
		GAGCore::presentationState = state;
		GAGCore::presentationPreference = preference;
	}
	static void host(double width, double height, bool touch)
	{
		GAGCore::InputCapabilities input;
		input.touch = touch;
		input.pointer = input.hover = !touch;
		GAGCore::updatePresentation({width, height, 1, {}, 0}, input);
	}
};

const GAGGUI::ui::Node *findNode(const GAGGUI::ui::Node &node, const std::string &key)
{
	if (node.key == key)
		return &node;
	for (const auto &child : node.children)
		if (child)
			if (const auto *found = findNode(*child, key))
				return found;
	return nullptr;
}

void blank(MapEdit &editor)
{
	editor.game.map.setSize(5, 5, GRASS);
	editor.game.map.setGame(&editor.game);
	editor.game.addTeam();
	editor.game.teams[0]->race.loadDefault();
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 32; ++x)
			editor.game.map.clearImmobileUnit(x, y);
	editor.viewportX = 0;
	editor.viewportY = 0;
	editor.updateCamera();
	editor.minimap.setMapSize(editor.game.map.getW(), editor.game.map.getH());
    editor.preparePresentation();
}
} // namespace

TEST_SUITE("EditorPresentation")
{
	TEST_CASE("the editor presentation table follows room and overrides")
	{
		struct Case
		{
			double width, height;
			bool touch;
			std::optional<PresentationPreference> forced;
			PresentationPreference preference;
			EditorPresentation expected;
		};
		const auto Phone = EditorPresentation::Phone, Dock = EditorPresentation::Dock;
		const auto Auto = PresentationPreference::Automatic, Compact = PresentationPreference::Compact,
				   Spacious = PresentationPreference::Spacious;
		const std::optional<PresentationPreference> none;
		const Case cases[] = {
			// Tablets and touch laptops dock; phones in either orientation use the tray.
			{1024, 768, true, none, Auto, Dock},
			{768, 1024, true, none, Auto, Phone},
			{1366, 768, true, none, Auto, Dock},
			{390, 844, true, none, Auto, Phone},
			{844, 390, true, none, Auto, Phone},
			{932, 430, true, none, Auto, Phone},
			// Pointer windows: the same room rule.
			{1280, 720, false, none, Auto, Dock},
			{1024, 600, false, none, Auto, Dock},
			{800, 600, false, none, Auto, Dock},
			{779, 600, false, none, Auto, Phone},
			{1024, 479, false, none, Auto, Phone},
			{640, 480, false, none, Auto, Phone},
			// The player's preference.
			{1024, 768, true, none, Compact, Phone},
			{1920, 1080, false, none, Compact, Phone},
			{700, 420, false, none, Spacious, Dock},
			{768, 1024, true, none, Spacious, Dock},
			{844, 390, true, none, Spacious, Phone},
			{600, 800, false, none, Spacious, Phone},
			// GLOB2_MOBILE_UI: 1 forces the tray, 0/touch-spacious the dock,
			// touch-auto the automatic rule regardless of the preference.
			{1920, 1080, false, Compact, Auto, Phone},
			{390, 844, true, Spacious, Auto, Dock},
			{390, 844, true, Spacious, Compact, Dock},
			{1024, 768, true, Auto, Compact, Dock},
			{844, 390, true, Auto, Spacious, Phone},
		};
		for (const auto &c : cases)
		{
			CAPTURE(c.width);
			CAPTURE(c.height);
			CAPTURE(c.touch);
			const auto choice =
				chooseEditorPresentation(resolved(c.width, c.height, c.touch, c.forced.value_or(c.preference)), c.forced,
										 c.preference);
			CHECK(choice.presentation == c.expected);
			CHECK(choice.touchTargets == c.touch);
		}
		// A touch-* override sizes dock targets for fingers on any host.
		CHECK(chooseEditorPresentation(resolved(1024, 768, false), PresentationPreference::Spacious,
									   PresentationPreference::Automatic, true)
				  .touchTargets);
		// Safe-area insets and the interface scale shrink the room.
		GAGCore::InputCapabilities touch{true, false, false, false};
		const auto inset = GAGCore::resolvePresentation(PresentationPreference::Automatic,
														{1024, 768, 1, {0, 0, 0, 300}, 0}, touch);
		CHECK(chooseEditorPresentation(inset, std::nullopt).presentation == EditorPresentation::Phone);
		const auto scaled =
			GAGCore::resolvePresentation(PresentationPreference::Automatic, {1280, 800, 2, {}, 0}, touch);
		CHECK(chooseEditorPresentation(scaled, std::nullopt).presentation == EditorPresentation::Phone);
	}

	TEST_CASE("live presentation follows the resolved host and GLOB2_MOBILE_UI")
	{
		PresentationScope scope;
		// Unresolved hosts keep the legacy rule: the tray only where forms adapt.
		GAGCore::presentationState = {};
		CHECK(currentEditorPresentation().presentation == EditorPresentation::Dock);
		PresentationScope::host(1024, 768, true);
		CHECK(currentEditorPresentation() == EditorPresentationChoice{EditorPresentation::Dock, true});
		PresentationScope::host(390, 844, true);
		CHECK(currentEditorPresentation().presentation == EditorPresentation::Phone);
		glob2test::setEnv("GLOB2_MOBILE_UI", "0");
		CHECK(currentEditorPresentation().presentation == EditorPresentation::Dock);
		glob2test::setEnv("GLOB2_MOBILE_UI", "1");
		PresentationScope::host(1920, 1080, false);
		CHECK(currentEditorPresentation().presentation == EditorPresentation::Phone);
		glob2test::setEnv("GLOB2_MOBILE_UI", "touch-spacious");
		CHECK(currentEditorPresentation() == EditorPresentationChoice{EditorPresentation::Dock, true});
		glob2test::unsetEnv("GLOB2_MOBILE_UI");
		GAGCore::presentationPreference = PresentationPreference::Compact;
		PresentationScope::host(1920, 1080, false);
		CHECK(currentEditorPresentation().presentation == EditorPresentation::Phone);
	}

	TEST_CASE("resizing mid-stroke switches presentation and keeps the brush [display]")
	{
		PresentationScope scope;
		glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
			.display = true, .width = 1024, .height = 768, .screenFlags = GAGCore::GraphicContext::PORTABLEGPU});
		auto *gfx = globalContainer->gfx;
		REQUIRE(gfx->hasPortableRenderer());
		PresentationScope::host(1024, 768, true);
		MapEdit editor;
		blank(editor);
		editor.beginEditing();
		CHECK(editor.presentation() == EditorPresentation::Dock);
		CHECK_FALSE(editor.syncPresentation());
		CHECK(editor.wantsResponsiveViewport()); // Touch hosts keep point-sized layouts.
		editor.performAction("select resource wheat");
		REQUIRE(editor.currentBrushId() == "resource/wheat");
		const auto panel = editor.panelMode;
		editor.brush.setFigure(3);
		// A desktop stroke in progress.
		editor.mouseX = 200;
		editor.mouseY = 200;
		editor.performAction("terrain drag start");
		REQUIRE(editor.isDraggingTerrain);
		auto resize = [&](double width, double height, bool touch)
		{
			// What MapEditorScreen does when ScreenStack reports a new viewport.
			PresentationScope::host(width, height, touch);
			editor.suspendInput();
			editor.viewportResized(gfx->getW(), gfx->getH(), gfx->getW(), gfx->getH());
			editor.drawEditing();
		};
		resize(390, 844, true);
		CHECK(editor.presentation() == EditorPresentation::Phone);
		CHECK(editor.usesPhone());
		CHECK_FALSE(editor.isDraggingTerrain);
		CHECK(editor.lastPlacementX == -1);
		CHECK(editor.currentBrushId() == "resource/wheat");
		CHECK(editor.panelMode == panel);
		CHECK(editor.brush.getFigure() == 3);
		// A tray stroke in progress, then rotation.
		auto &phone = *editor.phone;
		phone.prepare();
		const auto checksum = editor.game.checkSum(nullptr, nullptr, nullptr, true);
		auto finger = [&](Uint32 type, GAGCore::ViewPoint p)
		{
			SDL_Event event{};
			event.type = type;
			event.tfinger.touchID = 31;
			event.tfinger.fingerID = 1;
			event.tfinger.timestamp = SDL_GetTicksNS();
			event.tfinger.x = float(p.x / gfx->getW());
			event.tfinger.y = float(p.y / gfx->getH());
			editor.advanceEditing({event}, 0);
		};
		const GAGCore::ViewPoint a{phone.content.x + phone.content.w / 3, phone.content.y + phone.content.h / 2};
		finger(SDL_EVENT_FINGER_DOWN, a);
		finger(SDL_EVENT_FINGER_MOTION, {a.x + 60, a.y});
		CHECK_FALSE(editor.phone->stroke.empty());
		resize(844, 390, true);
		REQUIRE(editor.usesPhone());
		CHECK(editor.phone->stroke.empty());
		finger(SDL_EVENT_FINGER_UP, {a.x + 60, a.y});
		CHECK(editor.game.checkSum(nullptr, nullptr, nullptr, true) == checksum);
		CHECK(editor.currentBrushId() == "resource/wheat");
		// Back to a pointer window: the dock, with the same brush.
		editor.phone->stroke.push_back(a); // Even a stray buffered point is dropped.
		resize(1280, 720, false);
		CHECK(editor.presentation() == EditorPresentation::Dock);
		CHECK_FALSE(editor.usesPhone());
		CHECK(editor.currentBrushId() == "resource/wheat");
		CHECK(editor.panelMode == panel);
		CHECK(editor.game.checkSum(nullptr, nullptr, nullptr, true) == checksum);
		CHECK_FALSE(editor.wantsResponsiveViewport());
		// A building selected on the dock stays selected on the tray.
		editor.performAction("switch to building view");
		const BrushEntry *first = nullptr;
		for (const auto &group : editor.brushCatalog())
			if (group.section == BrushSection::Buildings && !group.entries.empty() && !first)
				first = &group.entries.front();
		REQUIRE(first);
		const std::string building = first->id;
		editor.performAction(first->action);
		REQUIRE(editor.currentBrushId() == building);
		resize(390, 844, true);
		CHECK(editor.usesPhone());
		CHECK(editor.currentBrushId() == building);
		editor.phone->prepare();
		CHECK(editor.phone->paletteMode == 2);
		CHECK(editor.phone->rowOf(building) >= 0);
	}

	TEST_CASE("a small pointer window uses the tray and the wheel zooms over the map [display]")
	{
		PresentationScope scope;
		glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
			.display = true, .width = 700, .height = 520, .screenFlags = GAGCore::GraphicContext::PORTABLEGPU});
		auto *gfx = globalContainer->gfx;
		REQUIRE(gfx->hasPortableRenderer());
		PresentationScope::host(gfx->getW(), gfx->getH(), false);
		MapEdit editor;
		blank(editor);
		editor.beginEditing();
		REQUIRE(editor.usesPhone());
		auto &phone = *editor.phone;
		phone.chooseMode(0);
		phone.prepare();
		REQUIRE(phone.maximum > 0);
		auto wheel = [&](GAGCore::ViewPoint p, float steps)
		{
			SDL_Event event{};
			event.type = SDL_EVENT_MOUSE_WHEEL;
			event.wheel.y = steps;
			event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
			event.wheel.mouse_x = float(p.x);
			event.wheel.mouse_y = float(p.y);
			editor.advanceEditing({event}, 0);
		};
		editor.updateCamera();
		const double zoom = editor.camera.zoom;
		wheel({phone.cardBar.x + phone.cardBar.w / 2, phone.cardBar.y + phone.cardBar.h / 2}, -2);
		CHECK(phone.offset > 0);
		CHECK(editor.camera.zoom == doctest::Approx(zoom));
		const double offset = phone.offset;
		wheel({phone.content.x + phone.content.w / 2, phone.content.y + phone.content.h / 2}, 1);
		CHECK(phone.offset == doctest::Approx(offset));
		CHECK(editor.camera.zoom > zoom);
		// A mouse click on a card selects it, in logical coordinates.
		phone.offset = 0;
		phone.prepare();
		const int grass = phone.rowOf("terrain/grass");
		REQUIRE(grass >= 0);
		const auto card = phone.rows[grass].rect;
		for (Uint32 type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP})
		{
			SDL_Event event{};
			event.type = type;
			event.button.button = SDL_BUTTON_LEFT;
			event.button.which = 1;
			event.button.x = float(card.x + card.w / 2);
			event.button.y = float(card.y + card.h / 2);
			editor.advanceEditing({event}, 0);
		}
		CHECK(editor.currentBrushId() == "terrain/grass");
	}

	TEST_CASE("decision cards fit the phone tray presentation in both orientations [display][artifacts]")
	{
		for (const auto &[width, height] : {std::pair{390, 844}, std::pair{844, 390}})
		{
			CAPTURE(width);
			PresentationScope scope;
			glob2test::setEnv("GLOB2_MOBILE_UI", "1");
			glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display = true,
																		 .loadStrings = true,
																		 .width = width,
																		 .height = height,
																		 .screenFlags = GAGCore::GraphicContext::PORTABLEGPU});
			auto *gfx = globalContainer->gfx;
			// MapEditorScreen's viewport policy, as the gallery applies it.
			gfx->setResponsiveViewport(true, 800, 600);
			PresentationScope::host(width, height, true);
			MapEdit editor;
			blank(editor);
			editor.beginEditing();
			REQUIRE(editor.usesPhone());
			editor.mapHasBeenModified();
			for (const char *action : {"open load screen", "quit editor"})
			{
				CAPTURE(action);
				editor.performAction(action);
				editor.advanceEditing({}, 1000);
				REQUIRE(editor.confirmation());
				editor.drawEditing();
				auto *card = editor.confirmation();
				REQUIRE(card->host().root());
				const GAGGUI::ui::Rect surface{0, 0, gfx->getW(), gfx->getH()};
				const double unit = gfx->logicalUnitsPerPoint();
				for (std::size_t i = 0; i < card->choiceCount(); ++i)
				{
					const auto *choice = findNode(*card->host().root(), "choice/" + std::to_string(i));
					REQUIRE(choice);
					const auto b = choice->bounds;
					CHECK(b.x >= surface.x);
					CHECK(b.y >= surface.y);
					CHECK(b.x + b.w <= surface.w);
					CHECK(b.y + b.h <= surface.h);
					CHECK(b.h >= int(40 * unit));
				}
				const std::string name = std::string("editor-card-") + (action[0] == 'o' ? "load-" : "quit-") +
										 std::to_string(width) + "x" + std::to_string(height) + ".bmp";
				gfx->printScreen(glob2test::artifactDirFromWorkingDirectory() + "/" + name);
				editor.drawEditing(); // The frame's end writes the capture.
				card->choose(card->cancelChoice());
				SDL_Event poll{};
				poll.type = SDL_EVENT_USER;
				editor.delegateMenu(poll);
				CHECK_FALSE(editor.confirmation());
			}
		}
	}
}
