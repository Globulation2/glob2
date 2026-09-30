// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include <string>
#include <memory>
#include <algorithm>
#include <iostream>
#include <utility>
#include <iterator>
#include <cmath>
#include <cstdlib>
#include "Engine.h"
#include "GameGUITouch.h"
#include <MapCamera.h>
#include "InGameTouchTheme.h"
#include "GameGUIDialog.h"
#include "LoadSaveDialog.h"
#include "GameGUIInternal.h"
#include "GameUtilities.h"
#include <Toolkit.h>
#include <TrueTypeFont.h>
#include <StringTable.h>
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "ReplayWriter.h"
#include "ReplayReader.h"
#include "usl.h"
#include "native.h"
#include "CampaignSelectorScreen.h"
#include "CampaignMenuScreen.h"
#include "ChooseMapScreen.h"
#include "CustomGameScreen.h"
#include "NewMapScreen.h"
#include "SettingsScreen.h"
#include "EndGameScreen.h"

#include "MapEdit.h"
#include "MapEditorScreen.h"
#include "CampaignEditor.h"
#include "ScriptEditorScreen.h"
#include "PhoneEditor.h"
#include "MapEditDialog.h"
#include <ScreenStack.h>
#include <BinaryStream.h>
#include <filesystem>
#include <fstream>
#include <SDL_net.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

static void require(bool condition, const char *message)
{
	GLOB2_REQUIRE(condition, message);
}

// Exercise the portable renderer's first draw and alternating touch transforms.
// Layout metrics stay authored; raster/font caches must survive scale changes.
static void verifyTouchFontRaster()
{
	struct Probe : GAGCore::TrueTypeFont
	{
		Probe() : TrueTypeFont("data/fonts/sans.ttf", 13) {}
		float scale() const { return renderScale; }
		unsigned misses() const { return cacheMiss; }
		size_t fonts() const { return rasterFonts.size(); }
	} font;
	auto *gfx = globalContainer->gfx;
	const std::string text = "Workers 123";
	const int width = font.getStringWidth(text), height = font.getStringHeight(text);
	auto draw = [&](float scale)
	{
		gfx->setUITransform(scale, 10, 10);
		gfx->drawString(0, 0, &font, text);
		require(font.getStringWidth(text) == width && font.getStringHeight(text) == height,
				"Sharper font raster must not change layout metrics");
		gfx->setUITransform();
	};
	draw(2);
	require(font.scale() >= 2, "First portable draw must rasterize at the transformed resolution");
	draw(1);
	draw(2);
	const auto misses = font.misses();
	const auto fonts = font.fonts();
	draw(1);
	draw(2);
	require(font.misses() == misses && font.fonts() == fonts,
			"Alternating UI scales must reuse glyph and font caches");
	GAGCore::DrawableSurface offscreen(width, height);
	offscreen.drawString(0, 0, &font, text);
	require(font.misses() == misses, "Logical offscreen text must reuse the authored raster");
}
class GameGUITouchHarness
{
  public:
	static void editorInteractions()
	{
		MapEdit editor;
		require(editor.load("maps/balanced.map"), "Editor touch fixture loads");
		editor.phone = std::make_unique<PhoneEditor>(editor);
		auto &touch = *editor.phone;
		auto *gfx = globalContainer->gfx;
		touch.chooseMode(0);
		touch.prepare();
		editor.updateCamera();
		// Separate interactions are further apart than the double-tap window.
		const Uint32 separateTouchStep = 400;
		Uint32 editorTicks = 1000, editorTickStep = separateTouchStep;
		auto finger = [&](Uint32 kind, int id, GAGCore::ViewPoint p)
		{
			SDL_Event event{};
			event.type = kind;
			event.tfinger.timestamp = editorTicks += editorTickStep;
			event.tfinger.touchId = 19;
			event.tfinger.fingerId = id;
			event.tfinger.x = p.x / gfx->getW();
			event.tfinger.y = p.y / gfx->getH();
			touch.event(event);
		};
		const GAGCore::ViewPoint start{touch.content.x + 96, touch.content.y + 48};
		const GAGCore::ViewPoint finish{start.x + 64, start.y + 32};
		editor.performAction("select sand");
		auto checksum = [&] { return editor.game.checkSum(nullptr, nullptr, nullptr, true); };
		auto before = checksum();
		finger(SDL_FINGERDOWN, 1, start);
		finger(SDL_FINGERMOTION, 1, finish);
		touch.draw();
		require(checksum() == before,
				"Editor paint preview mutated terrain before completing the stroke");
		finger(SDL_FINGERUP, 1, finish);
		require(checksum() != before, "Completed editor paint stroke failed to apply");
		editor.performAction("select water");
		before = checksum();
		finger(SDL_FINGERDOWN, 1, start);
		finger(SDL_FINGERMOTION, 1, finish);
		finger(SDL_FINGERDOWN, 2, {finish.x + 48, finish.y});
		finger(SDL_FINGERUP, 2, {finish.x + 48, finish.y});
		finger(SDL_FINGERUP, 1, finish);
		require(checksum() == before, "Second finger committed an unfinished editor paint stroke");
		finger(SDL_FINGERDOWN, 1, start);
		finger(SDL_FINGERMOTION, 1, finish);
		SDL_Event focus{};
		focus.type = SDL_WINDOWEVENT;
		focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
		touch.event(focus);
		finger(SDL_FINGERUP, 1, finish);
		require(checksum() == before, "Focus loss committed an unfinished editor paint stroke");
		{
			// A painted tap waits one double-tap window, as it may begin a zoom.
			finger(SDL_FINGERDOWN, 1, start);
			finger(SDL_FINGERUP, 1, start);
			require(checksum() == before && touch.deferred,
					"Editor paint tap must wait for the double-tap window");
			SDL_Delay(InGameTouchTheme::doubleTapWindowMs + 20);
			touch.draw();
			require(!touch.deferred && checksum() != before,
					"Editor paint tap must land after the double-tap window");
			// Two quick taps reset the zoom and paint nothing.
			before = checksum();
			const double u = gfx->logicalUnitsPerPoint();
			editor.camera.setZoom(1.5, finish.x, finish.y);
			editorTicks += separateTouchStep;
			editorTickStep = 50;
			finger(SDL_FINGERDOWN, 1, finish);
			finger(SDL_FINGERUP, 1, finish);
			finger(SDL_FINGERDOWN, 1, finish);
			finger(SDL_FINGERUP, 1, finish);
			editorTickStep = separateTouchStep;
			SDL_Delay(InGameTouchTheme::doubleTapWindowMs + 20);
			touch.draw();
			require(std::abs(editor.camera.zoom - 1) < 0.001 && checksum() == before,
					"Editor double tap must reset zoom instead of painting");
			// Tap, press again and drag up: one-finger zoom, still without painting.
			globalContainer->settings.oneFingerZoomDirection = Settings::ONE_FINGER_ZOOM_UP_IN;
			editorTicks += separateTouchStep;
			editorTickStep = 50;
			finger(SDL_FINGERDOWN, 1, finish);
			finger(SDL_FINGERUP, 1, finish);
			finger(SDL_FINGERDOWN, 1, finish);
			finger(SDL_FINGERMOTION, 1,
				   {finish.x, finish.y - GAGCore::TouchInput::zoomDoublingPoints / 2 * u});
			require(touch.touch.zoomDragging(), "Editor zoom drag must show its readout");
			finger(SDL_FINGERUP, 1,
				   {finish.x, finish.y - GAGCore::TouchInput::zoomDoublingPoints / 2 * u});
			editorTickStep = separateTouchStep;
			SDL_Delay(InGameTouchTheme::doubleTapWindowMs + 20);
			touch.draw();
			require(std::abs(editor.camera.zoom - std::sqrt(2.0)) < 0.01 && checksum() == before,
					"Editor one-finger zoom must follow the finger without painting");
			globalContainer->settings.oneFingerZoomDirection = Settings::ONE_FINGER_ZOOM_PLATFORM;
			editor.zoomMap(std::log(1.0 / editor.camera.zoom) / std::log(1.1), finish.x, finish.y);
		}
		touch.chooseMode(2);
		touch.prepare();
		editor.updateCamera();
		const auto icon = touch.rows[1].rect; // inn, using the real palette hit area
		const GAGCore::ViewPoint source{icon.x + icon.w / 2, icon.y + icon.h / 2};
		const int type =
			globalContainer->buildingsTypes.getTypeNum("inn", editor.buildingLevel, false);
		auto *building = globalContainer->buildingsTypes.get(type);
		// The bundled map may open over sea; put a known legal footprint in
		// view before exercising the gesture, without changing map contents.
		bool room = false;
		for (int y = 0; y < editor.game.map.getH() && !room; ++y)
			for (int x = 0; x < editor.game.map.getW(); ++x)
			{
				int bx, by;
				if (editor.game.checkRoomForBuilding(x, y, building, &bx, &by, editor.team, false))
				{
					int dx, dy;
					editor.game.map.cursorToBuildingPos(
						editor.mapMouseX(touch.content.x + touch.content.w / 2),
						editor.mapMouseY(touch.content.y + touch.content.h / 2 -
										 40 * gfx->logicalUnitsPerPoint()),
						building->width, building->height, &dx, &dy, 0, 0);
					editor.viewportX = (x - dx) & editor.game.map.wMask;
					editor.viewportY = (y - dy) & editor.game.map.hMask;
					room = true;
					break;
				}
			}
		require(room, "Editor map has no legal building footprint");
		editor.updateCamera();
		GAGCore::ViewPoint destination{};
		bool found = false;
		for (int y = touch.content.y + 48 * gfx->logicalUnitsPerPoint();
			 y < touch.content.y + touch.content.h - 8 && !found; y += 8)
			for (int x = touch.content.x + 16; x < touch.content.x + touch.content.w - 16; x += 8)
			{
				int mx, my, bx, by;
				editor.game.map.cursorToBuildingPos(
					editor.mapMouseX(x), editor.mapMouseY(y - 40 * gfx->logicalUnitsPerPoint()),
					building->width, building->height, &mx, &my, editor.viewportX,
					editor.viewportY);
				if (editor.game.checkRoomForBuilding(mx, my, building, &bx, &by, editor.team,
													 false))
				{
					destination = {double(x), double(y)};
					found = true;
					break;
				}
			}
		require(found, "No visible valid editor placement fixture");
		auto count = [&]
		{
			int count = 0;
			for (int t = 0; t < editor.game.mapHeader.getNumberOfTeams(); ++t)
				for (int b = 0; b < Building::MAX_COUNT; ++b)
					count += editor.game.teams[t]->myBuildings[b] != nullptr;
			return count;
		};
		auto drag = [&](GAGCore::ViewPoint end)
		{
			finger(SDL_FINGERDOWN, 1, source);
			finger(SDL_FINGERMOTION, 1, destination);
			finger(SDL_FINGERUP, 1, end);
		};
		const int countBefore = count();
		drag(destination);
		require(count() == countBefore + 1,
				"Editor valid building drag must place exactly one building");
		drag(destination);
		require(count() == countBefore + 1, "Editor invalid occupied drop placed another building");
		drag(source);
		require(count() == countBefore + 1, "Editor UI drop placed a building");
		finger(SDL_FINGERDOWN, 1, source);
		finger(SDL_FINGERMOTION, 1, destination);
		finger(SDL_FINGERDOWN, 2, start);
		finger(SDL_FINGERUP, 1, destination);
		finger(SDL_FINGERUP, 2, start);
		require(count() == countBefore + 1, "Second finger committed an interrupted editor drag");
		auto tap = [&](GAGCore::ViewPoint p)
		{
			finger(SDL_FINGERDOWN, 1, p);
			finger(SDL_FINGERUP, 1, p);
		};
		touch.chooseMode(0);
		editor.performAction("select water");
		touch.prepare();
		const double u = gfx->logicalUnitsPerPoint();
		tap({touch.safe.x + touch.safe.w * 7 / 8, touch.safe.y + 22 * u});
		require(editor.selectionMode == MapEdit::PlaceNothing && !touch.pan,
				"Done did not return editor to selection");
		editor.performAction("select sand");
		tap({touch.safe.x + touch.safe.w * 3 / 8, touch.safe.y + 22 * u});
		require(touch.brushOpen, "Brush control did not open visual brush choices");
		tap({touch.brushPanel.x + touch.brushPanel.w * 7 / 8, touch.brushPanel.y + 84 * u});
		require(editor.brush.getFigure() == 7 && !touch.brushOpen,
				"Visual brush choice did not apply");
		touch.chooseMode(2);
		editor.mouseX = destination.x;
		editor.mouseY = destination.y - 40 * u;
		editor.performAction("select map building");
		touch.prepare();
		require(touch.inspecting() && !touch.properties.empty(),
				"Selected building has no contextual property rows");
		auto property = touch.properties.front();
		const int oldValue = property.value->currentValue();
		editor.hasMapBeenModified = false;
		tap({property.rect.x + 22 * u, property.rect.y + 46 * u});
		require(property.value->currentValue() == std::max(0, oldValue - 1) &&
					editor.hasMapBeenModified,
				"Inspector step did not change named property or mark draft modified");
		tap({touch.inspector.x + touch.inspector.w - 26 * u, touch.inspector.y + 26 * u});
		require(!touch.inspecting() && touch.paletteMode == 2,
				"Closing inspector did not restore palette");
		std::puts(
			"PASS: editor buffered paint, focus/second-finger cancellation, one-building drag "
			"and invalid/UI drops");
	}

	static void editorAreaNameInteractions()
	{
		auto *gfx = globalContainer->gfx;
		auto dialogTap = [&](GAGGUI::ui::UIDialog &dialog, const std::string &key)
		{
			dialog.draw(0);
			const auto r = dialog.host().bounds(key);
			SDL_Event finger{};
			finger.type = SDL_FINGERDOWN;
			finger.tfinger.touchId = 31;
			finger.tfinger.fingerId = 1;
			finger.tfinger.x = float(r.x + r.w / 2) / gfx->getW();
			finger.tfinger.y = float(r.y + r.h / 2) / gfx->getH();
			dialog.event(finger);
			finger.type = SDL_FINGERUP;
			dialog.event(finger);
		};
		AskForTextInput area("[Change Area Name]", "Northern passage");
		area.attach(*gfx);
		area.draw(0);
		require(!area.host().editing().empty(), "Area name dialog must focus its entry on open");
		SDL_Event composition{};
		composition.type = SDL_TEXTEDITING;
		std::strcpy(composition.edit.text, "\xC3\xA9");
		area.event(composition);
		SDL_Event enter{};
		enter.type = SDL_KEYDOWN;
		enter.key.keysym.sym = SDLK_RETURN;
		area.event(enter);
		require(!area.finished() && area.draft() == "Northern passage",
				"Area IME submitted provisional text");
		SDL_Event text{};
		text.type = SDL_TEXTINPUT;
		std::strcpy(text.text.text, "\xC3\xA9");
		area.event(text);
		dialogTap(area, "ok");
		require(area.finished() && area.result() == AskForTextInput::OK &&
					area.getText() == "Northern passage\xC3\xA9",
				"Area touch confirmation lost native UTF-8 edits");
		AskForTextInput cancelled("[Change Area Name]", "Original");
		cancelled.attach(*gfx);
		cancelled.draw(0);
		cancelled.event(text);
		enter.key.keysym.sym = SDLK_ESCAPE;
		cancelled.event(enter);
		require(cancelled.finished() && cancelled.result() == AskForTextInput::CANCEL &&
					cancelled.getText() == "Original",
				"Cancelling area name committed draft");
		std::cout << "PASS: editor area name IME, touch confirmation and cancellation\n";
	}

	static void editorFileInteractions()
	{
		auto *gfx = globalContainer->gfx;
		auto finger = [&](GAGGUI::ui::UIDialog &dialog, Uint32 kind, int id, GAGCore::ViewPoint p)
		{
			SDL_Event event{};
			event.type = kind;
			event.tfinger.touchId = 29;
			event.tfinger.fingerId = id;
			event.tfinger.x = p.x / gfx->getW();
			event.tfinger.y = p.y / gfx->getH();
			dialog.event(event);
		};
		auto tap = [&](GAGGUI::ui::UIDialog &dialog, const std::string &key)
		{
			dialog.draw(0);
			const auto r = dialog.host().bounds(key);
			const GAGCore::ViewPoint p{r.x + r.w / 2., r.y + r.h / 2.};
			finger(dialog, SDL_FINGERDOWN, 1, p);
			finger(dialog, SDL_FINGERUP, 1, p);
		};
		LoadSaveDialog save("maps", "map", false, "Save map", "touch-file-fixture", glob2FilenameToName,
							glob2NameToFilename);
		save.attach(*gfx);
		save.draw(0);
		gfx->printScreen(("localized-file-" +
						  std::to_string(GAGCore::Toolkit::getStringTable()->getLang()) + "-" +
						  std::to_string(gfx->getW()) + ".bmp")
							 .c_str());
		gfx->nextFrame();
		const double u = gfx->logicalUnitsPerPoint();
		for (const auto key : {"name", "cancel", "ok"})
		{
			const auto r = save.host().bounds(key);
			require(r.y >= 0 && r.y + r.h <= gfx->getH() && r.h >= 44 * u,
					"File dialog controls escape the viewport or the touch target");
		}
		tap(save, "name");
		SDL_Event composition{};
		composition.type = SDL_TEXTEDITING;
		std::strcpy(composition.edit.text, "\xC3\xA9");
		save.event(composition);
		SDL_Event enter{};
		enter.type = SDL_KEYDOWN;
		enter.key.keysym.sym = SDLK_RETURN;
		save.event(enter);
		require(!save.finished() && std::string(save.getName()) == "touch-file-fixture",
				"Filename IME confirmation submitted a save or committed provisional text");
		tap(save, "name");
		SDL_Event text{};
		text.type = SDL_TEXTINPUT;
		std::strcpy(text.text.text, "\xC3\xA9");
		save.event(text);
		const std::string draft = "touch-file-fixture\xC3\xA9";
		require(std::string(save.getName()) == draft,
				"Native filename input lost its UTF-8 committed text");
		tap(save, "ok");
		require(save.finished() && save.result() == LoadSaveDialog::OK &&
					std::string(save.getFileName()) == glob2NameToFilename("maps", draft, "map"),
				"Touch save did not use the existing filename conversion and confirmation path");
		save.showSaveFailure();
		save.draw(0);
		require(!save.finished() && save.filePresentation().failed &&
					!save.filePresentation().status.empty() && std::string(save.getName()) == draft,
				"Save failure lost its draft or visible retry state");
		tap(save, "ok");
		require(save.finished() && save.result() == LoadSaveDialog::OK,
				"Retry did not reuse the original save command");
		struct Pending final : GAGCore::ApplicationHost::Persistence
		{
			GAGCore::ApplicationHost::PersistenceState value =
				GAGCore::ApplicationHost::PersistenceState::Pending;
			GAGCore::ApplicationHost::PersistenceState state() const override { return value; }
		};
		auto pending = std::make_unique<Pending>();
		auto *operation = pending.get();
		save.beginPersistence(std::move(pending));
		save.draw(0);
		tap(save, "name");
		save.event(text);
		tap(save, "cancel");
		require(!save.finished() && std::string(save.getName()) == draft && !save.pollPersistence(),
				"Busy file dialog accepted editing/cancellation or completed a pending save");
		operation->value = GAGCore::ApplicationHost::PersistenceState::Failed;
		require(!save.pollPersistence() && save.filePresentation().failed,
				"Persistence failure did not return the file view to its retry state");

		LoadSaveDialog load("maps", "map", true, "Load map", nullptr, glob2FilenameToName, glob2NameToFilename);
		load.attach(*gfx);
		load.draw(0);
		const auto model = load.filePresentation();
		require(model.files.size() > 1 && model.selected == -1, "File interaction fixture requires a file list");
		const auto list = load.host().bounds("files");
		require(list.h >= 44 * u, "File list must keep full touch rows");
		const GAGCore::ViewPoint bottom{list.x + 20 * u, list.y + list.h - 10 * u};
		const GAGCore::ViewPoint top{bottom.x, list.y + 10 * u};
		finger(load, SDL_FINGERDOWN, 1, bottom);
		finger(load, SDL_FINGERMOTION, 1, top);
		finger(load, SDL_FINGERUP, 1, top);
		require(load.filePresentation().selected == -1 && !load.finished(),
				"Swiping the file list selected a map or confirmed a load");
		load.selectPresentedFile(1);
		require(std::string(load.getName()) == model.files[1] && !load.finished(),
				"Selecting a file row did not select its semantic filename without loading");
		tap(load, "ok");
		require(load.finished() && load.result() == LoadSaveDialog::OK,
				"Explicit Load did not confirm the selected file");
		std::puts("PASS: file dialog UTF-8/IME, shared filename commands, busy/error retry "
				  "and scrolling");
	}
	static void run()
	{
		{
			Usl interpreter;
			auto *constant = new NativeValue<int>(&interpreter.heap, 42);
			interpreter.setConstant("retained", constant);
			for (int cycle = 0; cycle < 100; ++cycle)
			{
				interpreter.run(1);
				require(std::find(interpreter.heap.values.begin(), interpreter.heap.values.end(),
								  constant) != interpreter.heap.values.end(),
						"Repeated script collection retains bridge constants");
				require(interpreter.getConstant("retained") == constant,
						"Script constant remains accessible");
			}
		}
		GameGUI gui;
		auto map = Engine::loadMapHeader("maps/balanced.map");
		GameHeader players;
		players.setNumberOfPlayers(1);
		players.getBasePlayer(0) = BasePlayer(0, "Touch", 0, BasePlayer::P_LOCAL);
		require(gui.loadFromHeaders(map, players, true, true), "Fixture load failed");
		gui.localTeamNo = 0;
		gui.localPlayer = 0;
		gui.adjustLocalTeam();
		gui.viewportX = gui.viewportY = 0;
		{
			globalContainer->replayWriter = std::make_unique<ReplayWriter>();
			auto &writer = *globalContainer->replayWriter;
			writer.init("", gui);
			require(writer.write("replays/touch-empty.replay"),
					"An unfinished in-memory replay can be exported");
			{
				ReplayReader empty;
				require(empty.loadReplay("replays/touch-empty.replay"),
						"Replay export retains the final buffered header byte");
			}
			for (int i = 0; i < 100; ++i)
				writer.advanceStep();
			writer.finish();
			require(writer.write("replays/touch-preview.replay"), "Replay fixture writes");
			const auto position = writer.getBuffer()->getPosition();
			const auto blocked =
				std::filesystem::path(SDL_getenv("GLOB2_USER_DATA_DIR")) / "replays/blocked.replay";
			std::filesystem::create_directories(blocked);
			{
				std::ofstream marker(blocked / "keep");
				marker << "preserve";
			}
			require(!writer.write(blocked.string()),
					"Replay replacement failure returns false without asserting");
			require(std::filesystem::exists(blocked / "keep") &&
						writer.getBuffer()->getPosition() == position,
					"Failed replay write preserves destination and live buffer position");
			std::filesystem::remove(blocked / "keep");
			std::filesystem::remove(blocked);
			require(writer.write(blocked.string()) && writer.getBuffer()->getPosition() == position,
					"Replay retries after a failed replacement");
			std::ifstream original(std::filesystem::path(SDL_getenv("GLOB2_USER_DATA_DIR")) /
									   "replays/touch-preview.replay",
								   std::ios::binary);
			std::ifstream retried(blocked, std::ios::binary);
			require(std::string(std::istreambuf_iterator<char>(original), {}) ==
						std::string(std::istreambuf_iterator<char>(retried), {}),
					"Replay retry produces identical bytes");
			original.close();
			retried.close();
			std::filesystem::remove(blocked);
		}

		const auto checksum = gui.game.checkSum();
		Uint32 touchTicks = 1000;
		// Separate interactions are further apart than the double-tap window
		// (measured from a release to the next press); double-tap cases lower it.
		const Uint32 separateTouchStep = 400;
		Uint32 touchTickStep = separateTouchStep;
		// Begin a quick sequence (double tap) well after the previous interaction.
		auto quickTouches = [&]
		{
			touchTicks += separateTouchStep;
			touchTickStep = 50;
		};
		auto finger = [&](Uint32 type, int id, float x, float y)
		{
			SDL_Event event{};
			event.type = type;
			event.tfinger.timestamp = touchTicks += touchTickStep;
			event.tfinger.touchId = 7;
			event.tfinger.fingerId = id;
			event.tfinger.x = x / globalContainer->gfx->getW();
			event.tfinger.y = y / globalContainer->gfx->getH();
			gui.processEvent(&event);
		};
		auto tap = [&](float x, float y)
		{
			finger(SDL_FINGERDOWN, 1, x, y);
			finger(SDL_FINGERUP, 1, x, y);
		};
		auto flag = [&]
		{ gui.setSelection(GameGUI::TOOL_SELECTION, const_cast<char *>("warflag")); };
		auto noOrder = [&](int line = __builtin_LINE())
		{
			require(!gui.toolManager.getOrder(),
					("Navigation or preview emitted a tool order (harness line " + std::to_string(line) + ")")
						.c_str());
		};
		tap(760, 208);
		require(gui.selectionMode == GameGUI::TOOL_SELECTION &&
					gui.toolManager.getBuildingName() == "inn",
				"A tool must be selectable from its real sidebar hit area without mouse hover");
		noOrder();
		gui.clearSelection();
		finger(SDL_FINGERDOWN, 1, 160, 160);
		finger(SDL_FINGERMOTION, 1, 166, 160);
		finger(SDL_FINGERUP, 1, 166, 160);
		require(gui.viewportX == 0, "Sub-threshold movement must not pan");
		finger(SDL_FINGERDOWN, 1, 160, 160);
		finger(SDL_FINGERMOTION, 1, 224, 160);
		finger(SDL_FINGERUP, 1, 224, 160);
		require(gui.viewportX == gui.game.map.getW() - 2,
				"Dragging must pan across the toroidal seam");
		flag();
		tap(200, 200);
		noOrder();
		require(gui.touch->hasPreview(), "Placement tap must retain a preview");
		const int before = gui.viewportX;
		finger(SDL_FINGERDOWN, 1, 200, 200);
		finger(SDL_FINGERDOWN, 2, 300, 200);
		finger(SDL_FINGERMOTION, 1, 264, 200);
		finger(SDL_FINGERMOTION, 2, 364, 200);
		finger(SDL_FINGERUP, 2, 364, 200);
		finger(SDL_FINGERUP, 1, 264, 200);
		require(gui.viewportX == ((before - 2) & gui.game.map.getMaskW()),
				"Two fingers must pan while placing");
		noOrder();
		tap(100, 576);
		auto order = std::dynamic_pointer_cast<OrderCreate>(gui.toolManager.getOrder());
		require(bool(order), "Confirmation must emit the shared create order");
		require(gui.selectionMode == GameGUI::NO_SELECTION,
				"Confirmed placement must exit preview mode");
		noOrder();
		gui.clearSelection();
		const double oldZoom = gui.camera.zoom;
		finger(SDL_FINGERDOWN, 1, 200, 200);
		finger(SDL_FINGERDOWN, 2, 300, 200);
		finger(SDL_FINGERMOTION, 2, 350, 200);
		finger(SDL_FINGERUP, 2, 350, 200);
		finger(SDL_FINGERUP, 1, 200, 200);
		require(std::abs(gui.camera.zoom - oldZoom * 1.5) < 0.001,
				"Pinch uses the shared camera zoom");
		noOrder();
		gui.camera.setZoom(1.7, 200, 200);
		const auto tapAnchor = gui.camera.screenToWorld(200, 200);
		touchTickStep = 50;
		tap(200, 200);
		tap(200, 200);
		touchTickStep = separateTouchStep;
		require(std::abs(gui.camera.zoom - 1) < 0.001,
				"Double tap restores 1:1 map zoom");
		const auto restoredAnchor = gui.camera.screenToWorld(200, 200);
		require(std::abs(MapCamera::wrap(tapAnchor.first, gui.camera.mapWidth) -
					MapCamera::wrap(restoredAnchor.first, gui.camera.mapWidth)) < 0.001 &&
					std::abs(MapCamera::wrap(tapAnchor.second, gui.camera.mapHeight) -
					MapCamera::wrap(restoredAnchor.second, gui.camera.mapHeight)) < 0.001,
				"Double tap keeps the tapped world position anchored");
		gui.clearSelection();
		bool foundEmptyGround = false;
		for (int y = 80; y < 480 && !foundEmptyGround; y += 32)
			for (int x = 80; x < 560 && !foundEmptyGround; x += 32)
			{
				const auto world = gui.camera.screenToWorld(x, y);
				const int tileX = int(MapCamera::wrap(world.first, gui.camera.mapWidth)) / 32;
				const int tileY = int(MapCamera::wrap(world.second, gui.camera.mapHeight)) / 32;
				if (gui.game.map.getBuilding(tileX, tileY) != NOGBID ||
					gui.game.map.getGroundUnit(tileX, tileY) != NOGUID ||
					gui.game.map.getAirUnit(tileX, tileY) != NOGUID)
					continue;
				bool flagHere = false;
				for (auto *flagBuilding : gui.localTeam->virtualBuildings)
					flagHere |= gui.displayedPosX(*flagBuilding) == tileX &&
						gui.displayedPosY(*flagBuilding) == tileY;
				if (flagHere) continue;
				foundEmptyGround = true;
				gui.camera.setZoom(1.7, x, y);
				gui.mouseX = x; gui.mouseY = y;
				gui.updateCamera();
				const auto wheelAnchor = gui.camera.screenToWorld(x, y);
				SDL_Event wheel{};
				wheel.type = SDL_MOUSEWHEEL;
				wheel.wheel.y = -1;
#if SDL_VERSION_ATLEAST(2,0,18)
				wheel.wheel.preciseY = -1;
#endif
				gui.processEvent(&wheel);
				require(gui.camera.zoom < 1.7, "Wheel on empty map zooms without Alt");
				const auto afterWheel = gui.camera.screenToWorld(x, y);
				require(std::abs(MapCamera::wrap(wheelAnchor.first, gui.camera.mapWidth) -
						MapCamera::wrap(afterWheel.first, gui.camera.mapWidth)) < 0.001 &&
					std::abs(MapCamera::wrap(wheelAnchor.second, gui.camera.mapHeight) -
						MapCamera::wrap(afterWheel.second, gui.camera.mapHeight)) < 0.001,
					"Wheel zoom keeps the cursor's world position anchored");
			}
		require(foundEmptyGround, "Fixture needs empty ground for wheel zoom");
		flag();
		tap(240, 240);
		const auto position = gui.camera.screenToWorld(240, 240);
		require(gui.touch->preview && std::abs(gui.touch->preview->x - position.first) < 1.01 &&
					std::abs(gui.touch->preview->y - position.second) < 1.01,
				"Placement preview tracks world coordinates after fractional pan and zoom");
		tap(480, 576);
		noOrder();
		flag();
		tap(200, 200);
		tap(480, 576);
		noOrder();
		require(gui.selectionMode == GameGUI::NO_SELECTION, "Cancel must exit placement");
		flag();
		tap(220, 220);
		// The first point is in Confirm, the second just outside the strip.
		finger(SDL_FINGERDOWN, 1, 100, 554);
		finger(SDL_FINGERUP, 1, 100, 550);
		noOrder();
		require(gui.touch->hasPreview(), "Crossing a control boundary must not place or cancel");
		finger(SDL_FINGERDOWN, 1, 100, 576);
		gui.suspendInput();
		finger(SDL_FINGERUP, 1, 100, 576);
		noOrder();
		require(gui.touch->hasPreview(),
				"Suspension retains preview but cancels held confirmation");
		tap(220, 220);
		gui.localTeam->noMoreBuildingSitesCountdown = 1;
		tap(100, 576);
		noOrder();
		require(gui.touch->hasPreview(), "Failed validation must retain the preview");
		gui.localTeam->noMoreBuildingSitesCountdown = 0;
		gui.suspendInput();
		flag();
		finger(SDL_FINGERDOWN, 1, 200, 200);
		gui.clearSelection();
		finger(SDL_FINGERUP, 1, 200, 200);
		noOrder();
		require(!gui.touch->hasPreview(), "Mode changes must cancel an owned gesture");
		flag();
		tap(200, 200);
		SDL_Event synthetic{};
		synthetic.type = SDL_MOUSEBUTTONUP;
		synthetic.button.which = SDL_TOUCH_MOUSEID;
		synthetic.button.button = SDL_BUTTON_LEFT;
		synthetic.button.x = 200;
		synthetic.button.y = 200;
		gui.step({synthetic}, SDL_GetTicks64());
		require(gui.getOrder()->getOrderType() == ORDER_NULL,
				"Synthesized mouse release must not place");
		noOrder();
		gui.suspendInput();
		gui.setSelection(GameGUI::BRUSH_SELECTION);
		gui.toolManager.activateZoneTool(GameGUIToolManager::Forbidden);
		gui.brush.defaultSelection();
		finger(SDL_FINGERDOWN, 1, 200, 200);
		finger(SDL_FINGERMOTION, 1, 232, 200);
		finger(SDL_FINGERDOWN, 2, 300, 200);
		noOrder(); // A second finger cancels the unfinished stroke.
		finger(SDL_FINGERMOTION, 1, 296, 200);
		finger(SDL_FINGERUP, 2, 300, 200);
		finger(SDL_FINGERUP, 1, 296, 200);
		noOrder();
		finger(SDL_FINGERDOWN, 1, 240, 240);
		gui.suspendInput();
		noOrder(); // Focus suspension must not commit an unfinished stroke.
		finger(SDL_FINGERUP, 1, 240, 240);
		noOrder();
		flag();
		tap(220, 220);
		globalContainer->replaying = true;
		tap(100, 576);
		noOrder();
		globalContainer->replaying = false;
		gui.suspendInput();
		flag();
		tap(200, 200);
		finger(SDL_FINGERDOWN, 1, 100, 576);
		gui.viewportResized(800, 600, 600, 800);
		finger(SDL_FINGERUP, 1, 100, 576);
		noOrder();
		require(gui.touch->hasPreview(), "Rotation retains preview but cancels held confirmation");
		finger(SDL_FINGERDOWN, 1, 200, 200);
		SDL_Event focus{};
		focus.type = SDL_WINDOWEVENT;
		focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
		gui.processEvent(&focus);
		finger(SDL_FINGERUP, 1, 200, 200);
		noOrder();
		require(!gui.touch->hasPreview(), "Focus loss must clear owned pointers");
		focus.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
		gui.processEvent(&focus);
		tap(200, 200);
		gui.drawAll(0);
		globalContainer->gfx->printScreen("touch-placement.bmp");
		globalContainer->gfx->nextFrame();
		auto *capture = SDL_LoadBMP(
			(std::string(SDL_getenv("GLOB2_USER_DATA_DIR")) + "/touch-placement.bmp").c_str());
		require(capture && capture->format->BytesPerPixel == 4,
				"Placement screenshot must be captured");
		Uint8 r, g, b;
		const auto pixel = static_cast<Uint32 *>(static_cast<void *>(
			static_cast<char *>(capture->pixels) + (capture->h - 10) * capture->pitch))[10];
		SDL_GetRGB(pixel, capture->format, &r, &g, &b);
		SDL_FreeSurface(capture);
		require(g > r + 20, "Confirm must be visibly drawn over the world");
		require(gui.game.checkSum() == checksum,
				"Touch navigation and queued orders must not mutate simulation state");
		gui.game.map.setMapDiscovered(); // Expose terrain for this rendering fixture only.
		const auto hudChecksum = gui.game.checkSum();
		gui.clearSelection();
		gui.suspendInput();
		SDL_setenv("GLOB2_MOBILE_UI", "1", 1);
		auto *gfx = globalContainer->gfx;
		gfx->setResponsiveViewport(true, 800, 600);
		for (auto [width, height] : {std::pair{320, 568}, {568, 320}})
		{
			const int oldW = gfx->getW(), oldH = gfx->getH();
			SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()), width, height);
			SDL_Event resize{};
			resize.type = SDL_WINDOWEVENT;
			resize.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
			GAGCore::GraphicContext::translateMouseEvent(&resize);
			gui.viewportResized(oldW, oldH, gfx->getW(), gfx->getH());
			tap(100, 200); // Activate touch after the resize cancellation.
			const float unit = gfx->logicalUnitsPerPoint();
			gui.clearSelection();
			gui.displayMode = GameGUI::FLAG_VIEW;
			gui.touch->panelOpen = true;
			const auto flagContent = gui.touch->panelContent();
			const auto firstFlag = gui.touch->paletteItemRect(0);
			for (size_t i = 0; i < gui.touch->paletteItems().size(); ++i)
			{
				const auto box = gui.touch->paletteItemRect(i);
				require(box.y == firstFlag.y && box.x >= flagContent.x &&
							box.x + box.w <= flagContent.x + flagContent.w &&
							box.y + box.h <= flagContent.y + flagContent.h,
						"All flags and zones must fit in one visible row");
			}
			gui.drawAll(0);
			gfx->printScreen(width < height ? "touch-flags-portrait.bmp"
											: "touch-flags-landscape.bmp");
			gfx->nextFrame();
			gui.touch->panelOpen = false;
			tap(gfx->getW() / 12.0f, gfx->getH() - 24 * unit);
			require(gui.touch->usesHUD(), "Phone HUD must be active");
			require(gui.displayMode == GameGUI::CONSTRUCTION_VIEW,
					"Visible Build control routes to construction");
			gui.drawAll(0);
			gfx->printScreen(width < height ? "touch-hud-portrait.bmp" : "touch-hud-landscape.bmp");
			gfx->nextFrame();
			int targetX, targetY, centerX, centerY;
			gui.minimap.convertToMap(gfx->getW() - 80, 64, targetX, targetY);
			gui.minimapMouseToPos(gfx->getW() - 80, 64, &centerX, &centerY, true);
			const auto world = gui.touch->worldBounds();
			require(centerX == gui.camera.tileX() && centerY == gui.camera.tileY(),
					"Minimap navigation uses the shared normalized camera");
			{
				// Dragging on the HUD minimap steers the camera live, clamped at its edge.
				const double originX = gui.camera.originX, originY = gui.camera.originY;
				const auto mini = gui.touch->minimapRect();
				const GAGCore::ViewPoint from{mini.x + mini.w * 0.25, mini.y + mini.h * 0.25},
					to{mini.x + mini.w * 0.75, mini.y + mini.h * 0.7},
					outside{mini.x - 60 * unit, mini.y + mini.h * 0.5};
				finger(SDL_FINGERDOWN, 1, from.x, from.y);
				finger(SDL_FINGERMOTION, 1, to.x, to.y);
				const int dragX = gui.viewportX, dragY = gui.viewportY;
				gui.touch->navigateMinimap(to);
				require(gui.viewportX == dragX && gui.viewportY == dragY,
						"Minimap drag must follow the finger");
				gui.touch->navigateMinimap(from);
				require(gui.viewportX != dragX || gui.viewportY != dragY,
						"Minimap drag fixture must move the camera");
				finger(SDL_FINGERMOTION, 1, outside.x, outside.y);
				const int edgeX = gui.viewportX, edgeY = gui.viewportY;
				gui.touch->navigateMinimap(mini.clamp(outside));
				require(gui.viewportX == edgeX && gui.viewportY == edgeY,
						"Leaving the minimap must clamp the drag to its edge");
				finger(SDL_FINGERUP, 1, outside.x, outside.y);
				noOrder();
				const int oldX = gui.viewportX, oldY = gui.viewportY;
				gui.camera.originX = originX;
				gui.camera.originY = originY;
				gui.camera.normalize();
				gui.viewportX = gui.camera.tileX();
				gui.viewportY = gui.camera.tileY();
				gui.viewportChanged(oldX, gui.viewportX, oldY, gui.viewportY);
			}
			// A visible map spot with nothing selectable around it.
			auto emptyGround = [&]
			{
				for (double y = world.y + 48 * unit; y < world.y + world.h - 24 * unit; y += 16 * unit)
					for (double x = world.x + 24 * unit; x < world.x + world.w - 24 * unit; x += 16 * unit)
					{
						if (gui.touch->interfaceRegion({x, y}) != 0)
							continue;
						const int cx = gui.mapMouseX(int(x)) / 32 + gui.viewportX,
								  cy = gui.mapMouseY(int(y)) / 32 + gui.viewportY;
						bool clear = true;
						for (int dy = -1; dy <= 1; ++dy)
							for (int dx = -1; dx <= 1; ++dx)
							{
								const int mx = cx + dx, my = cy + dy;
								clear = clear && gui.game.map.getBuilding(mx, my) == NOGBID &&
										!gui.game.map.isResource(mx, my) &&
										gui.game.map.getGroundUnit(mx, my) == NOGUID &&
										gui.game.map.getAirUnit(mx, my) == NOGUID;
							}
						if (clear)
							return GAGCore::ViewPoint{x, y};
					}
				throw std::runtime_error("No empty ground visible in the phone world");
			};
			{
				// One-finger zoom: tap, press again and drag; direction follows settings.
				gui.clearSelection();
				gui.touch->panelOpen = false;
				const auto spot = emptyGround();
				const double travel = GAGCore::TouchInput::zoomDoublingPoints / 2 * unit;
				auto wrapped = [&](std::pair<double, double> a, std::pair<double, double> b)
				{
					return std::abs(MapCamera::wrap(a.first, gui.camera.mapWidth) -
									MapCamera::wrap(b.first, gui.camera.mapWidth)) < 1 &&
						   std::abs(MapCamera::wrap(a.second, gui.camera.mapHeight) -
									MapCamera::wrap(b.second, gui.camera.mapHeight)) < 1;
				};
				auto armedPress = [&]
				{
					gui.camera.setZoom(1, spot.x, spot.y);
					quickTouches();
					tap(spot.x, spot.y);
					finger(SDL_FINGERDOWN, 1, spot.x, spot.y);
				};
				for (int direction : {int(Settings::ONE_FINGER_ZOOM_UP_IN), int(Settings::ONE_FINGER_ZOOM_DOWN_IN)})
				{
					globalContainer->settings.oneFingerZoomDirection = direction;
					armedPress();
					const auto anchor = gui.camera.screenToWorld(spot.x, spot.y);
					const double end =
						spot.y + (direction == Settings::ONE_FINGER_ZOOM_UP_IN ? -travel : travel);
					finger(SDL_FINGERMOTION, 1, spot.x, end);
					require(gui.touch->gesture.zoomDragging(),
							"One-finger zoom must show its readout while dragging");
					if (direction == Settings::ONE_FINGER_ZOOM_UP_IN)
					{
						gui.drawAll(0);
						gfx->printScreen(width < height ? "touch-zoom-drag-portrait.bmp"
														: "touch-zoom-drag-landscape.bmp");
						gfx->nextFrame();
					}
					finger(SDL_FINGERUP, 1, spot.x, end);
					touchTickStep = separateTouchStep;
					require(std::abs(gui.camera.zoom - std::sqrt(2.0)) < 0.01,
							"One-finger zoom must follow the direction setting");
					require(wrapped(anchor, gui.camera.screenToWorld(spot.x, spot.y)),
							"One-finger zoom keeps the pressed world position anchored");
					noOrder();
				}
				globalContainer->settings.oneFingerZoomDirection = Settings::ONE_FINGER_ZOOM_UP_IN;
				armedPress();
				finger(SDL_FINGERMOTION, 1, spot.x, spot.y - travel / 2);
				double partial = gui.camera.zoom;
				require(partial > 1.01, "One-finger zoom fixture must zoom");
				finger(SDL_FINGERDOWN, 2, spot.x + 80 * unit, spot.y);
				finger(SDL_FINGERUP, 2, spot.x + 80 * unit, spot.y);
				finger(SDL_FINGERMOTION, 1, spot.x, spot.y - travel * 1.5);
				finger(SDL_FINGERUP, 1, spot.x, spot.y - travel * 1.5);
				touchTickStep = separateTouchStep;
				require(std::abs(gui.camera.zoom - partial) < 1e-9, "A second finger must end one-finger zoom");
				armedPress();
				finger(SDL_FINGERMOTION, 1, spot.x, spot.y - travel / 2);
				partial = gui.camera.zoom;
				SDL_Event lost{};
				lost.type = SDL_WINDOWEVENT;
				lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
				gui.processEvent(&lost);
				finger(SDL_FINGERMOTION, 1, spot.x, spot.y - travel * 1.5);
				finger(SDL_FINGERUP, 1, spot.x, spot.y - travel * 1.5);
				lost.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
				gui.processEvent(&lost);
				touchTickStep = separateTouchStep;
				require(std::abs(gui.camera.zoom - partial) < 1e-9 && !gui.touch->gesture.zoomDragging(),
						"Focus loss must end one-finger zoom");
				// A tap followed quickly by a sideways drag from the same spot still pans.
				armedPress();
				const int panX = gui.viewportX;
				finger(SDL_FINGERMOTION, 1, spot.x + 64 * unit, spot.y);
				finger(SDL_FINGERUP, 1, spot.x + 64 * unit, spot.y);
				touchTickStep = separateTouchStep;
				require(gui.viewportX != panX && std::abs(gui.camera.zoom - 1) < 1e-9,
						"A sideways drag after a tap must pan, not zoom");
				globalContainer->settings.oneFingerZoomDirection = Settings::ONE_FINGER_ZOOM_PLATFORM;
				gui.camera.setZoom(1, spot.x, spot.y);
				noOrder();
			}
			{
				// A painted tap waits one double-tap window, since it may begin a zoom.
				auto brush = [&]
				{
					gui.setSelection(GameGUI::BRUSH_SELECTION);
					gui.toolManager.activateZoneTool(GameGUIToolManager::Forbidden);
					gui.brush.defaultSelection();
				};
				auto forbidden = [&]
				{ return bool(std::dynamic_pointer_cast<OrderAlterForbidden>(gui.toolManager.getOrder())); };
				auto drain = [&] { while (gui.toolManager.getOrder()) {} };
				brush();
				const auto spot = emptyGround();
				tap(spot.x, spot.y);
				require(bool(gui.touch->deferredStroke), "A paint tap must wait for the double-tap window");
				noOrder();
				SDL_Delay(InGameTouchTheme::doubleTapWindowMs + 20);
				gui.touch->prepareDraw();
				require(forbidden(), "A paint tap must land after the double-tap window");
				drain();
				gui.camera.setZoom(1.5, spot.x, spot.y);
				quickTouches();
				tap(spot.x, spot.y);
				tap(spot.x, spot.y);
				touchTickStep = separateTouchStep;
				require(!gui.touch->deferredStroke, "A double tap while painting must not paint");
				require(std::abs(gui.camera.zoom - 1) < 0.001,
						("A double tap while painting must reset zoom (zoom " + std::to_string(gui.camera.zoom) + ")").c_str());
				SDL_Delay(InGameTouchTheme::doubleTapWindowMs + 20);
				gui.touch->prepareDraw();
				noOrder();
				finger(SDL_FINGERDOWN, 1, spot.x, spot.y);
				finger(SDL_FINGERMOTION, 1, spot.x + 40 * unit, spot.y);
				finger(SDL_FINGERUP, 1, spot.x + 40 * unit, spot.y);
				require(forbidden(), "A painted drag must not wait");
				drain();
				tap(spot.x, spot.y);
				const auto bar = gui.touch->controls();
				tap(bar.x + bar.w * 7 / 8, bar.y + bar.h / 2);
				require(forbidden(), "A held paint tap must land before Done leaves the brush");
				require(gui.selectionMode == GameGUI::NO_SELECTION, "Done must leave the brush");
				drain();
				brush();
				tap(spot.x, spot.y);
				gui.brush.setFigure((gui.brush.getFigure() + 1) % BrushTool::BRUSH_COUNT);
				SDL_Delay(InGameTouchTheme::doubleTapWindowMs + 20);
				gui.touch->prepareDraw();
				noOrder();
				require(!gui.touch->deferredStroke, "A held tap must be dropped when its brush changes");
				gui.clearSelection();
			}
			auto ui = gui.touch->layout();
			const int cameraX = gui.viewportX, cameraY = gui.viewportY;
			finger(SDL_FINGERDOWN, 1, ui.panel.x + 2 * unit, ui.panel.y + 70 * unit);
			finger(SDL_FINGERMOTION, 1, ui.panel.x + 2 * unit, ui.panel.y + 30 * unit);
			finger(SDL_FINGERUP, 1, ui.panel.x + 2 * unit, ui.panel.y + 30 * unit);
			require(gui.viewportX == cameraX && gui.viewportY == cameraY,
					"Panel scrolling must not pan the world");
			noOrder();
			const auto inn =
				std::find(gui.buildingsChoiceName.begin(), gui.buildingsChoiceName.end(), "inn") -
				gui.buildingsChoiceName.begin();
			gui.touch->panelScroll = 0;
			gui.touch->clampScroll();
			const auto palette = gui.touch->paletteItemRect(inn);
			tap(palette.x + palette.w / 2, palette.y + palette.h / 2);
			require(gui.selectionMode == GameGUI::TOOL_SELECTION &&
						gui.toolManager.getBuildingName() == "inn",
					"Labeled touch palette must select the same building after rotation");
			noOrder();
			gui.clearSelection();
			gui.scriptText =
				"Build an inn to feed your workers. Drag the panel to find more buildings. "
				"Select a building, choose a location, and confirm when you are ready.\n"
				"This long instruction remains readable after rotating the phone.";
			gui.swallowSpaceKey = true;
			gui.setIsSpaceSet(false);
			gui.drawAll(0);
			gfx->printScreen(width < height ? "touch-tutorial-portrait.bmp"
											: "touch-tutorial-landscape.bmp");
			gfx->nextFrame();
			finger(SDL_FINGERDOWN, 1, 30 * unit, 80 * unit);
			finger(SDL_FINGERMOTION, 1, 30 * unit, 60 * unit);
			finger(SDL_FINGERUP, 1, 30 * unit, 60 * unit);
			require(!gui.isSpaceSet(), "Scrolling tutorial text must not acknowledge it");
			const auto tutorial = gui.touch->tutorialRect();
			tap(tutorial.x + 20 * unit, tutorial.y + tutorial.h - 24 * unit);
			require(gui.isSpaceSet(), "Tutorial touch acknowledgment uses the shared Space action");
			gui.scriptText.clear();
			gui.swallowSpaceKey = false;
		}
		require(gui.game.checkSum() == hudChecksum,
				"HUD interaction must not mutate the simulation");
		// Both input patterns must reach the identical construction operation.
		// Flags have no resource cost and are convenient placement fixtures.
		gui.clearSelection();
		gui.touch->cancel();
		gui.displayMode = GameGUI::FLAG_VIEW;
		gui.touch->panelOpen = true;
		gui.touch->panelScroll = 0;
		gui.drawAll(0);
		gfx->nextFrame();
		const auto icon = gui.touch->paletteItemRect(1);
		const float iconX = icon.x + icon.w / 2, iconY = icon.y + icon.h / 2;
		const float dropX = 120, dropY = 190;
		finger(SDL_FINGERDOWN, 1, iconX, iconY);
		require(gui.touch->placement.has_value(),
				"Palette press starts an owned placement session");
		noOrder();
		finger(SDL_FINGERMOTION, 99, dropX, dropY);
		finger(SDL_FINGERUP, 99, dropX, dropY);
		require(gui.touch->placement.has_value(),
				"Unknown contact motion and release do not steal placement ownership");
		noOrder();
		finger(SDL_FINGERMOTION, 1, dropX, dropY);
		noOrder();
		require(gui.touch->showsBuildPalette(),
				"Active placement retains palette composition for persistent layouts");
		gui.drawAll(0);
		gfx->printScreen("touch-drag-preview.bmp");
		gfx->nextFrame();
		finger(SDL_FINGERUP, 1, dropX, dropY);
		auto dragged = std::dynamic_pointer_cast<OrderCreate>(gui.toolManager.getOrder());
		require(bool(dragged), "Valid palette drag commits one order on release");
		noOrder();
		require(gui.touch->panelOpen && gui.selectionMode == GameGUI::NO_SELECTION,
				"Drag restores the repeated-placement palette");
		gui.ghostManager.removeBuilding(dragged->posX, dragged->posY);
		tap(iconX, iconY);
		noOrder();
		require(gui.selectionMode == GameGUI::TOOL_SELECTION,
				"Palette tap selects preview-and-confirm");
		tap(dropX, dropY - 48 * gfx->logicalUnitsPerPoint());
		noOrder();
		const auto confirm = gui.touch->controls();
		tap(confirm.x + confirm.w / 4, confirm.y + confirm.h / 2);
		auto tapped = std::dynamic_pointer_cast<OrderCreate>(gui.toolManager.getOrder());
		require(tapped && std::memcmp(tapped->getData(), dragged->getData(),
									  dragged->getDataLength()) == 0,
				"Tap and drag serialize equivalent construction commands");
		noOrder();
		gui.ghostManager.removeBuilding(tapped->posX, tapped->posY);
		gui.touch->panelOpen = true;
		finger(SDL_FINGERDOWN, 1, iconX, iconY);
		finger(SDL_FINGERMOTION, 1, dropX, dropY);
		finger(SDL_FINGERUP, 1, iconX, iconY);
		noOrder();
		require(gui.touch->panelOpen, "Dropping back onto the source palette cancels construction");
		finger(SDL_FINGERDOWN, 1, iconX, iconY);
		finger(SDL_FINGERMOTION, 1, dropX, dropY);
		finger(SDL_FINGERDOWN, 2, dropX + 30, dropY);
		finger(SDL_FINGERUP, 1, dropX, dropY);
		finger(SDL_FINGERUP, 2, dropX + 30, dropY);
		noOrder();
		require(!gui.touch->placement && gui.touch->panelOpen,
				"Second finger cancels palette placement without a release tap");
		gui.localTeam->noMoreBuildingSitesCountdown = 1;
		finger(SDL_FINGERDOWN, 1, iconX, iconY);
		finger(SDL_FINGERMOTION, 1, dropX, dropY);
		finger(SDL_FINGERUP, 1, dropX, dropY);
		noOrder();
		gui.localTeam->noMoreBuildingSitesCountdown = 0;
		gui.clearSelection();
		for (bool drag : {false, true})
		{
			gui.clearSelection();
			gui.touch->panelOpen = true;
			gui.displayMode = GameGUI::FLAG_VIEW;
			const auto source = gui.touch->paletteItemRect(1);
			const auto bounds = gui.touch->world();
			const double x = bounds.x + 2, y = bounds.y + bounds.h / 2;
			if (drag)
			{
				finger(SDL_FINGERDOWN, 1, source.x + source.w / 2, source.y + source.h / 2);
				finger(SDL_FINGERMOTION, 1, x, y);
			}
			else
			{
				tap(source.x + source.w / 2, source.y + source.h / 2);
				finger(SDL_FINGERDOWN, 1, x, y);
			}
			auto &session = drag ? gui.touch->placement : gui.touch->placementHold;
			require(bool(session), "Both placement patterns own a held contact");
			gui.camera.originX = 0;
			gui.viewportX = 0;
			session->lastUpdate = SDL_GetTicks64() - 100;
			gui.touch->advancePlacement();
			require(gui.camera.originX > gui.game.map.getW() * 16,
					"Stationary edge hold pans and wraps across the toroidal boundary");
			noOrder();
			// Hovering over UI suspends panning even if the contact began on the map.
			const auto toolbar = gui.touch->layout().actions;
			finger(SDL_FINGERMOTION, 1, x, toolbar.y + toolbar.h / 2);
			const double overUI = gui.camera.originX;
			session->lastUpdate = SDL_GetTicks64() - 100;
			gui.touch->advancePlacement();
			require(gui.camera.originX == overUI, "Placement never pans while over UI");
			finger(SDL_FINGERMOTION, 1, x, y);
			if (!drag)
			{
				finger(SDL_FINGERUP, 1, x, y);
				const double released = gui.camera.originX;
				gui.touch->advancePlacement();
				require(!gui.touch->placementHold && gui.camera.originX == released,
						"Preview release stops panning and still requires confirmation");
				noOrder();
				finger(SDL_FINGERDOWN, 1, x, y);
			}
			finger(SDL_FINGERDOWN, 2, x + 30, y);
			const double stopped = gui.camera.originX;
			gui.touch->advancePlacement();
			require(!gui.touch->placementHold && !gui.touch->placement &&
						gui.camera.originX == stopped,
					"Second finger stops placement edge panning");
			finger(SDL_FINGERUP, 1, x, y);
			finger(SDL_FINGERUP, 2, x + 30, y);
			noOrder();
			gui.touch->cancel();
		}
		gui.touch->panelOpen = false;
		const int type = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
		auto *building =
			new Building(0, 0, 2, type, gui.localTeam, &globalContainer->buildingsTypes, 1, 1);
		gui.localTeam->myBuildings[2] = building;
		require(building->type->maxUnitWorking > 0, "Allocation fixture must accept workers");
		auto actionPoint = [&](int kind, int value, int side = 0)
		{
			for (int attempt = 0; attempt < 30; ++attempt)
			{
				gui.drawAll(0);
				gfx->nextFrame();
				const auto rows = gui.touch->buildingActions();
				const auto found =
					std::find_if(rows.begin(), rows.end(), [&](const auto &row)
								 { return row.kind == kind && (kind == 7 || row.value == value); });
				require(found != rows.end(), "Building action must be available");
				const auto r = gui.touch->panelContent();
				const double u = gfx->logicalUnitsPerPoint();
				const auto box = gui.touch->buildingActionRect(std::distance(rows.begin(), found));
				const double top = box.y;
				if (top >= r.y && top + box.h <= r.y + r.h)
					return GAGCore::ViewPoint{kind == 7  ? box.x + (value + 1.5) * box.w / 3
											  : side < 0 ? box.x + 24 * u
											  : side > 0 ? box.x + box.w - 24 * u
														 : box.x + box.w / 2,
											  top + box.h - 22 * u};
				const float x = r.x + r.w / 2, y = r.y + r.h / 2;
				const float delta = (top < r.y ? 1 : -1) * std::min(r.h / 3, 56 * u);
				finger(SDL_FINGERDOWN, 1, x, y);
				finger(SDL_FINGERMOTION, 1, x, y + delta);
				finger(SDL_FINGERUP, 1, x, y + delta);
			}
			throw std::runtime_error("Building action must be reachable by scrolling");
		};
		auto pressAction = [&](int kind, int value = 0, int side = 0)
		{
			auto p = actionPoint(kind, value, side);
			tap(p.x, p.y);
		};
		auto *rangeFlag =
			new Building(0, 0, 3, globalContainer->buildingsTypes.getTypeNum("warflag", 0, false),
						 gui.localTeam, &globalContainer->buildingsTypes, 1, 1);
		gui.localTeam->myBuildings[3] = rangeFlag;
		{
			const auto savedCamera = gui.camera;
			const int savedX = gui.viewportX, savedY = gui.viewportY;
			const auto savedFlags = gui.localTeam->virtualBuildings;
			gui.localTeam->virtualBuildings = {rangeFlag};
			rangeFlag->posX = rangeFlag->posY = 2;
			gui.view.mouseUnit = nullptr;
			for (double zoom : {.33, .5, 1.})
			{
				gui.camera.zoom = zoom;
				gui.camera.originX = gui.camera.originY = 0;
				gui.viewportX = gui.viewportY = 0;
				gui.updateCamera();
				const auto center = gui.camera.worldToScreen(80, 80);
				const double unit = gfx->logicalUnitsPerPoint();
				const int x = int(center.first + 20 * unit), y = int(center.second);
				const int mapX = gui.mapMouseX(x) / 32 + gui.viewportX;
				const int mapY = gui.mapMouseY(y) / 32 + gui.viewportY;
				require(gui.game.map.getBuilding(mapX, mapY) == NOGBID,
						"Flag halo fixture needs ground outside buildings");
				gui.clearSelection();
				gui.handleMapClick(x, y, SDL_BUTTON_LEFT);
				require(gui.selectionMode == GameGUI::BUILDING_SELECTION &&
							gui.selectionBuilding() == rangeFlag && !gui.selectionPushed,
						"Near flag clicks select across zoom levels without starting a move");
				SDL_MouseButtonEvent release{};
				release.button = SDL_BUTTON_LEFT;
				release.x = x;
				release.y = y;
				gui.handleMouseButtonUp(release);
				require(gui.orderQueue.empty(), "Forgiving flag selection must not move the flag");
				gui.game.map.setBuilding(mapX, mapY, 1, 1, building->gid);
				gui.clearSelection();
				gui.handleMapClick(x, y, SDL_BUTTON_LEFT);
				require(gui.selectionMode == GameGUI::BUILDING_SELECTION &&
							gui.selectionBuilding() == building,
						"Expanded flag hits must not steal direct building selection");
				gui.game.map.setBuilding(mapX, mapY, 1, 1, NOGBID);
				gui.clearSelection();
				gui.handleMapClick(int(center.first + 30 * unit), y, SDL_BUTTON_LEFT);
				require(gui.selectionMode != GameGUI::BUILDING_SELECTION,
						"Clicks outside the halo must not select a flag");

				// The larger target belongs to touch presentation only. Check
				// both a near miss and an exact desktop hit at every zoom.
				SDL_setenv("GLOB2_MOBILE_UI", "0", 1);
				gui.clearSelection();
				gui.updateCamera();
				// Switching presentation preserves the camera centre; reset the
				// fixture origin so the flag stays inside the desktop viewport.
				gui.camera.originX = gui.camera.originY = 0;
				gui.viewportX = gui.viewportY = 0;
				const auto desktopCenter = gui.camera.worldToScreen(80, 80);
				gui.handleMapClick(int(desktopCenter.first + 20 * unit), int(desktopCenter.second),
								   SDL_BUTTON_LEFT);
				require(gui.selectionMode != GameGUI::BUILDING_SELECTION,
						"Desktop must not use the touch flag selection halo");
				gui.clearSelection();
				gui.handleMapClick(int(desktopCenter.first), int(desktopCenter.second),
								   SDL_BUTTON_LEFT);
				require(gui.selectionMode == GameGUI::BUILDING_SELECTION &&
							gui.selectionBuilding() == rangeFlag && gui.selectionPushed,
						"Desktop exact flag hits retain selection and dragging");
				gui.clearSelection();
				SDL_setenv("GLOB2_MOBILE_UI", "1", 1);
				gui.updateCamera();
			}
			gui.camera.zoom = 1;
			gui.viewportX = gui.game.map.getW() - 2;
			gui.viewportY = 0;
			gui.camera.originX = gui.viewportX * 32.;
			gui.camera.originY = 0;
			rangeFlag->posX = gui.game.map.getW() - 1;
			require(gui.game.map.getBuilding(0, 2) == NOGBID, "Seam fixture needs empty ground");
			gui.clearSelection();
			gui.handleMapClick(int(gui.camera.offsetX + 69), int(gui.camera.offsetY + 80),
							   SDL_BUTTON_LEFT);
			require(gui.selectionMode == GameGUI::BUILDING_SELECTION &&
						gui.selectionBuilding() == rangeFlag,
					"Expanded flag selection crosses the toroidal seam");
			rangeFlag->posX = rangeFlag->posY = 0;
			gui.localTeam->virtualBuildings = savedFlags;
			gui.clearSelection();
			gui.camera = savedCamera;
			gui.viewportX = savedX;
			gui.viewportY = savedY;
		}

		require(rangeFlag->type->defaultUnitStayRange && rangeFlag->type->maxUnitWorking,
				"Flag fixture needs range and workers");
		gui.orderQueue.clear();
		for (auto [width, height] : {std::pair{320, 568}, {568, 320}})
		{
			const int oldW = gfx->getW(), oldH = gfx->getH();
			SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()), width, height);
			SDL_Event resized{};
			resized.type = SDL_WINDOWEVENT;
			resized.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
			GAGCore::GraphicContext::translateMouseEvent(&resized);
			gui.viewportResized(oldW, oldH, gfx->getW(), gfx->getH());
			const float unit = gfx->logicalUnitsPerPoint();
			// A completed empty-map tap dismisses inspection and restores the
			// palette state from before inspection, in both phone orientations.
			for (bool paletteWasOpen : {false, true})
			{
				gui.setSelection(GameGUI::BUILDING_SELECTION, building);
				gui.touch->panelOpen = true;
				gui.touch->restorePalette = true;
				gui.touch->previousPanelOpen = paletteWasOpen;
				gui.touch->previousDisplayMode = gui.displayMode;
				gui.touch->prepareDraw();
				bool dismissed = false;
				for (int y = 80; y < gfx->getH() - 80 && !dismissed; y += 32)
					for (int x = 16; x < gfx->getW() && !dismissed; x += 32)
					{
						if (gui.touch->interfaceRegion({double(x), double(y)}) != 0)
							continue;
						const int mx = gui.mapMouseX(x) / 32 + gui.viewportX;
						const int my = gui.mapMouseY(y) / 32 + gui.viewportY;
						if (gui.game.map.getBuilding(mx, my) != NOGBID ||
							gui.game.map.isResource(mx, my) ||
							gui.game.map.getGroundUnit(mx, my) != NOGUID ||
							gui.game.map.getAirUnit(mx, my) != NOGUID)
							continue;
						tap(x, y);
						gui.touch->prepareDraw();
						require(gui.selectionMode == GameGUI::NO_SELECTION,
								"Empty map tap must dismiss the building inspector");
						require(gui.touch->panelOpen == paletteWasOpen && gui.orderQueue.empty(),
								"Dismissal must restore palette state without issuing an order");
						dismissed = true;
					}
				require(dismissed, "Inspector dismissal fixture needs exposed empty terrain");
			}
			gui.touch->panelOpen = false;
			gui.setSelection(GameGUI::BUILDING_SELECTION, building);
			// Info opens the inspector for the selected entity.
			tap(gfx->getW() * 2.5f / 6, gfx->getH() - 24 * unit);
			auto workerPoint = actionPoint(6, 0, 1);
			const float plusX = workerPoint.x, rowY = workerPoint.y;
			const int before = gui.displayedMaxUnitWorking(*building),
					  authoritative = building->maxUnitWorking;
			const auto simulation = gui.game.checkSum();
			tap(plusX, rowY);
			tap(plusX, rowY);
			require(gui.orderQueue.size() == 2,
					"Two allocation taps must queue exactly two orders");
			auto first = std::dynamic_pointer_cast<OrderModifyBuilding>(gui.orderQueue.front());
			gui.orderQueue.pop_front();
			auto second = std::dynamic_pointer_cast<OrderModifyBuilding>(gui.orderQueue.front());
			gui.orderQueue.pop_front();
			require(first && second && first->gid == building->gid &&
						first->numberRequested == before + 1 &&
						second->numberRequested == before + 2,
					"Rapid allocation taps use pending values and the shared order format");
			require(building->maxUnitWorking == authoritative && gui.game.checkSum() == simulation,
					"Allocation UI must not change authoritative simulation state");
			const auto track = gui.touch->buildingActionRect(0);
			const float startX = track.x + track.w / 2, endX = track.x + track.w - 60 * unit;
			finger(SDL_FINGERDOWN, 1, startX, rowY);
			finger(SDL_FINGERMOTION, 1, endX, rowY);
			require(gui.touch->allocation && gui.orderQueue.empty(),
					"Slider drag previews without intermediate orders");
			finger(SDL_FINGERUP, 1, endX, rowY);
			require(!gui.touch->allocation && gui.orderQueue.size() == 1,
					"Slider release sends exactly one allocation order");
			gui.orderQueue.clear();
			finger(SDL_FINGERDOWN, 1, startX, rowY);
			finger(SDL_FINGERMOTION, 1, endX, rowY);
			finger(SDL_FINGERDOWN, 2, startX, rowY);
			finger(SDL_FINGERUP, 1, endX, rowY);
			finger(SDL_FINGERUP, 2, startX, rowY);
			require(!gui.touch->allocation && gui.orderQueue.empty(),
					"Second finger cancels slider without an order");
			gui.drawAll(0);
			gfx->printScreen(width < height ? "touch-allocation-portrait.bmp"
											: "touch-allocation-landscape.bmp");
			gfx->nextFrame();
			gui.requestWorkerAllocation(*building, MAX_UNIT_WORKING);
			gui.orderQueue.clear();
			tap(plusX, rowY);
			require(gui.orderQueue.empty(), "Allocation at maximum must not queue duplicates");
			finger(SDL_FINGERDOWN, 1, plusX, rowY);
			gui.clearSelection();
			finger(SDL_FINGERUP, 1, plusX, rowY);
			require(gui.orderQueue.empty(), "Selection changes cancel held allocation gestures");
			gui.setSelection(GameGUI::BUILDING_SELECTION, building);
			gui.requestWorkerAllocation(*building, 0);
			gui.orderQueue.clear();
			pressAction(6, 0, -1);
			require(gui.orderQueue.empty(), "Allocation at zero must not queue duplicates");
			for (int priority : {-1, 0, 1})
			{
				pressAction(7, priority);
				require(gui.orderQueue.size() == 1, "Priority tap emits exactly one order");
				auto order = std::dynamic_pointer_cast<OrderChangePriority>(gui.orderQueue.front());
				gui.orderQueue.clear();
				require(order && order->gid == building->gid && order->priority == priority,
						"Priority uses the shared order format");
				pressAction(7, priority);
				require(gui.orderQueue.empty(), "Selected pending priority is a no-op");
			}
			require(gui.game.checkSum() == simulation,
					"Priority changes stay outside authoritative state");
			gui.drawAll(0);
			gfx->printScreen(width < height ? "touch-priority-portrait.bmp"
											: "touch-priority-landscape.bmp");
			gfx->nextFrame();
			gui.setSelection(GameGUI::BUILDING_SELECTION, rangeFlag);
			const int rangeBefore = gui.displayedUnitStayRange(*rangeFlag);
			const auto rangeChecksum = gui.game.checkSum();
			pressAction(8, 0, 1);
			pressAction(8, 0, 1);
			require(gui.orderQueue.size() == 2, "Rapid range taps queue two orders");
			for (int delta : {1, 2})
			{
				auto order = std::dynamic_pointer_cast<OrderModifyFlag>(gui.orderQueue.front());
				gui.orderQueue.pop_front();
				require(order && order->gid == rangeFlag->gid &&
							order->range == rangeBefore + delta,
						"Range uses pending values and shared orders");
			}
			gui.drawAll(0);
			gfx->printScreen(width < height ? "touch-range-portrait.bmp"
											: "touch-range-landscape.bmp");
			gfx->nextFrame();
			gui.requestFlagRange(*rangeFlag, rangeFlag->type->maxUnitStayRange);
			gui.orderQueue.clear();
			pressAction(8, 0, 1);
			require(gui.orderQueue.empty(), "Maximum range is a no-op");
			gui.requestFlagRange(*rangeFlag, 0);
			gui.orderQueue.clear();
			pressAction(8, 0, -1);
			require(gui.orderQueue.empty(), "Zero range is a no-op");
			finger(SDL_FINGERDOWN, 1, plusX, rowY);
			gui.setSelection(GameGUI::BUILDING_SELECTION, building);
			finger(SDL_FINGERUP, 1, plusX, rowY);
			require(gui.orderQueue.empty(),
					"Changing selected buildings cancels held range controls");
			require(gui.game.checkSum() == rangeChecksum,
					"Range controls preserve authoritative state");
			tap(gfx->getW() * 5.5f / 6, gfx->getH() - 24 * unit);
			require(gui.inGameMenu == GameGUI::IGM_MAIN, "Toolbar opens the in-game pause menu");
			gui.drawAll(0);
			gfx->printScreen(width < height ? "touch-pause-portrait.bmp"
											: "touch-pause-landscape.bmp");
			gfx->nextFrame();
			require(bool(gui.gameMenuScreen), "Pause menu dialog must exist");
			const auto back = gui.gameMenuScreen->host().bounds("return");
			const float returnX = back.x + back.w / 2, returnY = back.y + back.h / 2;
			finger(SDL_FINGERDOWN, 1, returnX, returnY);
			finger(SDL_FINGERMOTION, 1, returnX + 20 * unit, returnY);
			finger(SDL_FINGERUP, 1, returnX + 20 * unit, returnY);
			require(gui.inGameMenu == GameGUI::IGM_MAIN,
					"A dragged pause button must not activate");
			SDL_Event mouse{};
			mouse.type = SDL_MOUSEBUTTONDOWN;
			mouse.button.button = SDL_BUTTON_LEFT;
			mouse.button.x = int(returnX);
			mouse.button.y = int(returnY);
			gui.processEvent(&mouse);
			mouse.type = SDL_MOUSEBUTTONUP;
			gui.processEvent(&mouse);
			require(!gui.inGameMenu, "Mouse activates the same visible Return control");
			tap(gfx->getW() * 5.5f / 6, gfx->getH() - 24 * unit);
			gui.drawAll(0);
			finger(SDL_FINGERDOWN, 1, returnX, returnY);
			finger(SDL_FINGERDOWN, 2, returnX, returnY);
			finger(SDL_FINGERUP, 2, returnX, returnY);
			finger(SDL_FINGERUP, 1, returnX, returnY);
			require(
				gui.inGameMenu == GameGUI::IGM_MAIN,
				"Switching back to touch consumes the whole gesture before accepting an action");
			gui.drawAll(0);
			gfx->nextFrame();
			tap(returnX, returnY);
			require(gui.inGameMenu == GameGUI::IGM_NONE && gui.orderQueue.empty(),
					"Return resumes without leaking a world order");
			tap(gfx->getW() * 2.5f / 6,
				gfx->getH() - 24 * unit); // Close the inspector before the next orientation.
			gui.clearSelection();
		}

		int workerSlot = 0;
		while (workerSlot < Unit::MAX_COUNT && gui.localTeam->myUnits[workerSlot])
			++workerSlot;
		require(workerSlot < Unit::MAX_COUNT, "Repair fixture has a free worker slot");
		gui.localTeam->myUnits[workerSlot] = new Unit(0, 0, workerSlot, WORKER, gui.localTeam, 3);
		bool constructionSpace = false;
		for (int y = 0; y < gui.game.map.getH() && !constructionSpace; ++y)
			for (int x = 0; x < gui.game.map.getW() && !constructionSpace; ++x)
			{
				building->posX = x;
				building->posY = y;
				constructionSpace = building->isHardSpaceForBuildingSite(Building::REPAIR) &&
									building->isHardSpaceForBuildingSite(Building::UPGRADE);
			}
		require(constructionSpace, "Fixture has space for repair and upgrade");

		auto fixture = [&](const char *name, int slot)
		{
			auto *b =
				new Building(0, 0, slot, globalContainer->buildingsTypes.getTypeNum(name, 0, false),
							 gui.localTeam, &globalContainer->buildingsTypes, 1, 1);
			gui.localTeam->myBuildings[slot] = b;
			return b;
		};
		auto *swarm = fixture("swarm", 4);
		auto *clearing = fixture("clearingflag", 5);
		auto *exploring = fixture("explorationflag", 6);
		auto *wall = fixture("stonewall", 7);
		auto openActions = [&](Building *b)
		{
			gui.setSelection(GameGUI::BUILDING_SELECTION, b);
			gui.touch->panelOpen = true;
			gui.drawAll(0);
			gui.touch->actionScroll = 0;
			gui.orderQueue.clear();
		};
		for (auto [width, height] : {std::pair{320, 568}, {568, 320}})
		{
			const int oldW = gfx->getW(), oldH = gfx->getH();
			SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()), width, height);
			SDL_Event resized{};
			resized.type = SDL_WINDOWEVENT;
			resized.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
			GAGCore::GraphicContext::translateMouseEvent(&resized);
			gui.viewportResized(oldW, oldH, gfx->getW(), gfx->getH());
			openActions(swarm);
			const auto content = gui.touch->panelContent();
			for (size_t i = 0; i < gui.touch->buildingActions().size(); ++i)
			{
				const auto box = gui.touch->buildingActionRect(i);
				require(box.y >= content.y && box.y + box.h <= content.y + content.h,
						"Swarm controls must fit without inspector scrolling");
			}
			gfx->printScreen(width < height ? "touch-swarm-portrait.bmp"
											: "touch-swarm-landscape.bmp");
			const auto checksum = gui.game.checkSum();
			for (int type = 0; type < NB_UNIT_TYPE; ++type)
			{
				const auto before = gui.displayedRatio(*swarm);
				pressAction(0, type, 1);
				pressAction(0, type, 1);
				require(gui.orderQueue.size() == 2,
						"Rapid production taps queue exactly two orders");
				for (int delta : {1, 2})
				{
					auto order =
						std::dynamic_pointer_cast<OrderModifySwarm>(gui.orderQueue.front());
					gui.orderQueue.pop_front();
					require(order && order->gid == swarm->gid,
							"Production uses the shared order and building");
					for (int i = 0; i < NB_UNIT_TYPE; ++i)
						require(order->ratio[i] == before[i] + (i == type ? delta : 0),
								"Ratio edits preserve other pending values");
				}
				auto values = gui.displayedRatio(*swarm);
				values[type] = MAX_RATIO_RANGE;
				gui.pendingFor(swarm->gid).pendingRatio = values;
				pressAction(0, type, 1);
				require(gui.orderQueue.empty(), "Maximum ratio tap emits no order");
				values[type] = 0;
				gui.pendingFor(swarm->gid).pendingRatio = values;
				pressAction(0, type, -1);
				require(gui.orderQueue.empty(), "Zero ratio tap emits no order");
			}
			require(gui.game.checkSum() == checksum, "Ratio UI does not mutate the simulation");
			gui.drawAll(0);
			gfx->printScreen(width < height ? "touch-actions-portrait.bmp"
											: "touch-actions-landscape.bmp");
			gfx->nextFrame();
			openActions(clearing);
			for (int resource = 0; resource < BASIC_COUNT; ++resource)
				if (resource != STONE)
				{
					const bool before = gui.displayedClearingResource(*clearing, resource);
					pressAction(1, resource);
					pressAction(1, resource);
					require(gui.orderQueue.size() == 2,
							"Clearing toggles each queue exactly one order");
					for (bool value : {!before, before})
					{
						auto order = std::dynamic_pointer_cast<OrderModifyClearingFlag>(
							gui.orderQueue.front());
						gui.orderQueue.pop_front();
						require(order && order->gid == clearing->gid &&
									order->clearingResources[resource] == value,
								"Clearing toggle uses pending state");
					}
				}
			for (auto *flag : {rangeFlag, exploring})
			{
				openActions(flag);
				const int count =
					flag == rangeFlag ? NB_UNIT_LEVELS : EXPLORATION_FLAG_OPTION_COUNT;
				for (int level = 0; level < count; ++level)
				{
					const int previous = gui.displayedMinLevelToFlag(*flag);
					pressAction(2, level);
					require(gui.orderQueue.size() == size_t(previous != level),
							"Requirement changes suppress no-ops");
					if (previous != level)
					{
						auto order = std::dynamic_pointer_cast<OrderModifyMinLevelToFlag>(
							gui.orderQueue.front());
						gui.orderQueue.clear();
						require(order && order->gid == flag->gid && order->minLevelToFlag == level,
								"Flag requirement preserves shared order format");
					}
				}
			}
			openActions(wall);
			require(gui.touch->allocationRect().h ==
						InGameTouchTheme::inspectorHeader * gfx->logicalUnitsPerPoint(),
					"Unified inspector retains its identity header");
			pressAction(4);
			require(gui.orderQueue.empty() && gui.touch->confirmDestroy,
					"Destroy first enters confirmation");
			pressAction(5);
			require(gui.orderQueue.empty() && !gui.touch->confirmDestroy,
					"Destruction can be canceled");
			pressAction(4);
			pressAction(4);
			require(gui.orderQueue.size() == 1 &&
						std::dynamic_pointer_cast<OrderDelete>(gui.orderQueue.front()),
					"Confirmed destruction emits one shared order");
			gui.orderQueue.clear();
			auto p = actionPoint(4, 0);
			finger(SDL_FINGERDOWN, 1, p.x, p.y);
			wall->buildingState = Building::WAITING_FOR_DESTRUCTION;
			finger(SDL_FINGERUP, 1, p.x, p.y);
			require(gui.orderQueue.empty(), "A state transition cancels the held action");
			pressAction(4);
			require(gui.orderQueue.size() == 1 &&
						std::dynamic_pointer_cast<OrderCancelDelete>(gui.orderQueue.front()),
					"Pending destruction can be canceled by touch");
			gui.orderQueue.clear();
			wall->buildingState = Building::ALIVE;
			openActions(building);
			for (bool repair : {true, false})
			{
				building->hp = building->type->hpMax - (repair ? 1 : 0);
				const auto stateBefore = gui.game.checkSum();
				pressAction(3);
				require(gui.orderQueue.size() == 1, "Repair/upgrade starts with exactly one order");
				auto order = std::dynamic_pointer_cast<OrderConstruction>(gui.orderQueue.front());
				require(order && order->gid == building->gid,
						"Repair/upgrade uses shared construction order");
				gui.orderQueue.clear();
				require(gui.game.checkSum() == stateBefore,
						"Construction requests do not mutate simulation");
			}
			building->hp = building->type->hpMax - 1;
			auto repairPoint = actionPoint(3, 0);
			finger(SDL_FINGERDOWN, 1, repairPoint.x, repairPoint.y);
			building->hp = building->type->hpMax;
			finger(SDL_FINGERUP, 1, repairPoint.x, repairPoint.y);
			require(gui.orderQueue.empty(), "Healing must not turn a held Repair into Upgrade");
			for (auto state : {Building::REPAIR, Building::UPGRADE})
			{
				building->constructionResultState = state;
				pressAction(3);
				require(
					gui.orderQueue.size() == 1 &&
						std::dynamic_pointer_cast<OrderCancelConstruction>(gui.orderQueue.front()),
					"Construction cancellation uses shared order");
				gui.orderQueue.clear();
			}
			building->constructionResultState = Building::NO_CONSTRUCTION;
			gui.clearSelection();
			gui.touch->panelOpen = false;
		}

		for (const auto *key : {"[Actions]", "[Info]", "[Minimap]", "[Fast forward]",
								"[Hide keyboard]", "[shutdown save failed]", "[menu]",
								"[Editor tools]", "[Editor map]", "[Pan map]", "[Edit map]"})
			require(!GAGCore::Toolkit::getStringTable()->getString(key).empty(),
					"New interface translations must not be blank");

		auto tr = [](const char *key)
		{ return std::string(GAGCore::Toolkit::getStringTable()->getString(key)); };
		auto pressDialog = [&](const std::string &key)
		{
			auto *dialog = gui.activeDialog();
			require(dialog != nullptr, ("No dialog for action: " + key).c_str());
			gui.drawAll(0);
			gfx->nextFrame();
			dialog->host().scrollIntoView(key);
			gui.drawAll(0);
			gfx->nextFrame();
			const auto r = dialog->host().bounds(key);
			tap(r.x + r.w / 2, r.y + r.h / 2);
		};
		for (auto [width, height] : {std::pair{320, 568}, {568, 320}})
		{
			SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()), width, height);
			SDL_Event resized{};
			resized.type = SDL_WINDOWEVENT;
			resized.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
			GAGCore::GraphicContext::translateMouseEvent(&resized);
			gui.viewportResized(800, 600, gfx->getW(), gfx->getH());
			gui.openMainMenu();
			pressDialog("options");
			require(gui.inGameMenu == GameGUI::IGM_OPTION, "Phone menu opens options");
			pressDialog("text-size/2");
			require(globalContainer->settings.mobileDialogTextPercent == 150,
					"Dialog text size applies from the options dialog");
			pressDialog("text-size/0");
			require(globalContainer->settings.mobileDialogTextPercent == 100,
					"Dialog text size restores");
			const bool muted = globalContainer->settings.mute;
			pressDialog("mute");
			require(globalContainer->settings.mute != muted, "Mute toggles from the options dialog");
			pressDialog("mute");
			pressDialog("ok");
			require(!gui.inGameMenu, "Options footer remains reachable");
			gui.openDialog(GameGUI::IGM_OBJECTIVES, std::make_unique<InGameObjectivesScreen>(&gui, false));
			auto *objectives = static_cast<InGameObjectivesScreen *>(gui.gameMenuScreen.get());
			const int hintsTab = gui.game.missionBriefing.empty() ? 1 : 2;
			pressDialog("objectives/tab/" + std::to_string(hintsTab));
			require(objectives->tab() == InGameObjectivesScreen::HINTS, "Objectives tabs switch by touch");
			pressDialog("ok");
			require(!gui.inGameMenu, "Objectives tabs and footer work by touch");
			gui.openDialog(GameGUI::IGM_SAVE,
						   std::make_unique<LoadSaveDialog>("games", "game", false, tr("[save game]"), "Phone",
															glob2FilenameToName, glob2NameToFilename));
			pressDialog("name");
			SDL_Event text{};
			text.type = SDL_TEXTINPUT;
			std::strcpy(text.text.text, "2");
			gui.processEvent(&text);
			require(std::string(static_cast<LoadSaveDialog *>(gui.gameMenuScreen.get())->getName()) == "Phone2",
					"Save filename edits through the dialog");
			pressDialog("cancel");
			require(!gui.inGameMenu, "Save cancellation is always reachable");
			gui.touch->menuAction(1);
			require(bool(gui.typingInputScreen), "Tactical chat action opens the composer");
			gui.drawAll(0);
			gfx->nextFrame();
			for (const auto &key : {"send", "close"})
			{
				const auto r = gui.typingInputScreen->host().bounds(key);
				require(r.w >= 48 * gfx->logicalUnitsPerPoint() &&
							r.h >= 48 * gfx->logicalUnitsPerPoint(),
						"Chat icons retain 48-point tap targets");
				require(!gui.typingInputScreen->host().find(key)->accessibleText().empty(),
						"Chat icons retain translated names");
			}
			gfx->printScreen("chat-icons-" + std::to_string(width) + ".bmp");
			SDL_Event composition{};
			composition.type = SDL_TEXTEDITING;
			std::strcpy(composition.edit.text, "provisional");
			gui.processEvent(&composition);
			SDL_Event enter{};
			enter.type = SDL_KEYDOWN;
			enter.key.keysym.sym = SDLK_RETURN;
			gui.orderQueue.clear();
			gui.processEvent(&enter);
			require(gui.typingInputScreen && gui.typingInputScreen->getText().empty() &&
						gui.orderQueue.empty(),
					"IME candidate confirmation must not send chat or mutate the committed draft");
			text = {};
			text.type = SDL_TEXTINPUT;
			std::strcpy(text.text.text, "Hello");
			gui.processEvent(&text);
			gui.orderQueue.clear();
			pressDialog("send");
			require(!gui.typingInputScreen && gui.orderQueue.size() == 1 &&
						std::dynamic_pointer_cast<MessageOrder>(gui.orderQueue.front()),
					"Chat sends once through shared orders");
			gui.orderQueue.clear();
			gui.touch->menuAction(1);
			gui.processEvent(&text);
			pressDialog("close");
			require(!gui.typingInputScreen && gui.orderQueue.empty(),
					"Closing the chat draft sends no message");
		}

		{
			GAGGUI::ScreenStack stack(*gfx);
			auto results = std::make_unique<EndGameScreen>(&gui);
			auto *view = results.get();
			stack.push(std::move(results));
			stack.frame(SDL_GetTicks(), {});
			require(!view->host().interactiveNodes().empty(), "Results exposes the shared chart controls");
			view->activateResultControl(100);
			require(view->metricPickerOpen(), "Metric selector opens an explicit dropdown");
			SDL_Event key{};
			key.type = SDL_KEYDOWN;
			key.key.keysym.sym = SDLK_END;
			view->handleExecutionEvent(key);
			key.key.keysym.sym = SDLK_RETURN;
			view->handleExecutionEvent(key);
			require(view->selectedMetric == 35 && !view->metricPickerOpen(),
					"Every metric is selectable without cycling pages");
			if (!view->teams.empty())
			{
				const int team = view->teams.front().teamNum;
				view->activateResultControl(200);
				view->selectMetric(0);
				for (size_t i = 0; i < view->teams.size(); ++i)
					if (view->teams[i].teamNum == team)
						require(!view->teams[i].enabled,
								"Metric changes preserve team filtering across sorting");
			}
			view->activateResultControl(101);
			require(view->expandedChart, "Chart expansion is available");
			stack.frame(SDL_GetTicks(), {});
			// Enter confirms a metric while its picker is open (checked above),
			// but retains the desktop finish shortcut on the results screen itself.
			key.key.keysym.sym = SDLK_RETURN;
			view->handleExecutionEvent(key);
			require(!view->isExecutionRunning(), "Enter closes results after chart interaction");
			stack.frame(SDL_GetTicks(), {});
		}

		// Labels may change length and word order; action IDs and queued orders must not.
		auto *strings = GAGCore::Toolkit::getStringTable();
		const int originalLanguage = strings->getLang();
		gui.setSelection(GameGUI::BUILDING_SELECTION, building);
		gui.touch->panelOpen = true;
		const auto originalActions = gui.touch->buildingActions();
		const auto originalOrders = gui.orderQueue.size();
		const auto originalChecksum = gui.game.checkSum();
		for (const char *language : {"de", "ja"})
		{
			strings->setLang(strings->getLangCode(language));
			for (auto [width, height] : {std::pair{320, 568}, {568, 320}})
			{
				const int oldWidth = gfx->getW(), oldHeight = gfx->getH();
				SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()), width, height);
				SDL_Event resize{};
				resize.type = SDL_WINDOWEVENT;
				resize.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
				GAGCore::GraphicContext::translateMouseEvent(&resize);
				gui.viewportResized(oldWidth, oldHeight, gfx->getW(), gfx->getH());
				gui.drawAll(0);
				const auto actions = gui.touch->buildingActions();
				require(actions.size() == originalActions.size(),
						"Localization changed building actions");
				for (size_t i = 0; i < actions.size(); ++i)
					require(actions[i].kind == originalActions[i].kind &&
								actions[i].value == originalActions[i].value,
							"Localization changed building action dispatch");
				const std::string suffix = std::string(language) + "-" + std::to_string(width);
				gfx->printScreen(("localized-inspector-" + suffix + ".bmp").c_str());
				gfx->nextFrame();
				gui.clearSelection();
				gui.touch->showStatistics = true;
				gui.displayMode = GameGUI::STAT_TEXT_VIEW;
				gui.touch->panelOpen = true;
				gui.drawAll(0);
				gfx->printScreen(("localized-statistics-" + suffix + ".bmp").c_str());
				gfx->nextFrame();
				gui.touch->showStatistics = false;
				gui.setSelection(GameGUI::BUILDING_SELECTION, building);
				editorInteractions();
				editorFileInteractions();
				editorAreaNameInteractions();
				ScriptEditorScreen script(&gui.game);
				script.attach(*gfx);
				gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), GAGCore::Color(0, 0, 0));
				script.draw(0);
				gfx->printScreen(("localized-keyboard-" + suffix + ".bmp").c_str());
				gfx->nextFrame();
				GAGGUI::ScreenStack resultsStack(*gfx);
				auto results = std::make_unique<EndGameScreen>(&gui);
				auto *resultsView = results.get();
				resultsStack.push(std::move(results));
				gfx->printScreen("localized-results-" + suffix + ".bmp");
				resultsStack.frame(SDL_GetTicks(), {});
				resultsView->activateResultControl(101);
				require(resultsView->expandedChart, "Localized results retain chart expansion");
				gfx->printScreen("localized-chart-" + suffix + ".bmp");
				resultsStack.frame(SDL_GetTicks(), {});
			}
		}
		strings->setLang(originalLanguage);
		require(gui.orderQueue.size() == originalOrders && gui.game.checkSum() == originalChecksum,
				"Localized presentation mutated game state or queued commands");
		gui.clearSelection();
		std::puts("PASS localized German/Japanese inspectors and editor interactions in both phone "
				  "orientations");

		globalContainer->replayReader = std::make_unique<ReplayReader>();
		require(globalContainer->replayReader->loadReplay("replays/touch-preview.replay"),
				"Replay reader loads");
		globalContainer->replaying = true;
		globalContainer->replayVisibleTeams = 0xffffffff;
		auto replayMap = Engine::loadMapHeader("replays/touch-preview.replay");
		require(gui.loadFromHeaders(replayMap, players, true, true, false,
									"replays/touch-preview.replay"),
				"Replay world loads");
		gui.localTeamNo = 0;
		gui.localPlayer = 0;
		gui.adjustLocalTeam();
		for (auto [width, height] : {std::pair{320, 568}, {568, 320}})
		{
			SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()), width, height);
			SDL_Event resized{};
			resized.type = SDL_WINDOWEVENT;
			resized.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
			GAGCore::GraphicContext::translateMouseEvent(&resized);
			gui.viewportResized(800, 600, gfx->getW(), gfx->getH());
			gui.drawAll(0);
			gfx->nextFrame();
			const auto before = gui.game.checkSum();
			const double unit = gfx->logicalUnitsPerPoint(), y = gfx->getH() - 24 * unit;
			gui.gamePaused = false;
			globalContainer->replayFastForward = false;
			gui.orderQueue.clear();
			tap(gfx->getW() / 12.0, y);
			require(gui.gamePaused, "Replay toolbar pauses");
			tap(gfx->getW() / 4.0, y);
			require(!gui.gamePaused && globalContainer->replayFastForward,
					"Replay speed resumes fast playback");
			tap(gfx->getW() / 4.0, y);
			require(!globalContainer->replayFastForward, "Replay speed returns to normal");
			gui.touch->menuAction(30);
			require(globalContainer->replayFastForward,
					"Replay menu speed uses shared playback state");
			require(gui.orderQueue.empty() && !gui.toolManager.getOrder() &&
						gui.game.checkSum() == before,
					"Replay controls issue no simulation orders");
			if (gui.inGameMenu)
				gui.closeDialog();
		}
		globalContainer->replaying = false;
		globalContainer->replayReader.reset();
		editorInteractions();
		editorFileInteractions();
		editorAreaNameInteractions();
	}

	// Touch scroll physics in a match: the map coasts after a flick and wraps
	// across the seam, a touch or suspendInput stops it, the phone build palette
	// coasts and rubber-bands, a mouse drag has no momentum, and nothing reaches
	// the simulation. Time is explicit: event timestamps and gui.step(now).
	static void scrollPhysics()
	{
		GameGUI gui;
		auto map = Engine::loadMapHeader("maps/balanced.map");
		GameHeader players;
		players.setNumberOfPlayers(1);
		players.getBasePlayer(0) = BasePlayer(0, "Touch", 0, BasePlayer::P_LOCAL);
		require(gui.loadFromHeaders(map, players, true, true), "Fixture load failed");
		gui.localTeamNo = 0;
		gui.localPlayer = 0;
		gui.adjustLocalTeam();
		gui.viewportX = gui.viewportY = 0;
		gui.updateCamera();
		auto *gfx = globalContainer->gfx;
		const double mapWidth = gui.game.map.getW() * 32.0;
		Uint64 now = 5000;
		auto finger = [&](Uint32 type, float x, float y)
		{
			SDL_Event event{};
			event.type = type;
			event.tfinger.timestamp = Uint32(now);
			event.tfinger.touchId = 7;
			event.tfinger.fingerId = 1;
			event.tfinger.x = x / gfx->getW();
			event.tfinger.y = y / gfx->getH();
			gui.processEvent(&event);
		};
		auto frame = [&](Uint64 ms)
		{
			now += ms;
			gui.step({}, now);
		};
		// Four moves 16 ms apart, then an immediate release.
		auto flick = [&](float x, float y, float dx, float dy)
		{
			finger(SDL_FINGERDOWN, x, y);
			for (int i = 1; i <= 4; ++i)
			{
				frame(16);
				finger(SDL_FINGERMOTION, x + dx * i, y + dy * i);
			}
			finger(SDL_FINGERUP, x + dx * 4, y + dy * 4);
		};
		auto placeCamera = [&](double x, double y)
		{
			gui.camera.originX = x;
			gui.camera.originY = y;
			gui.camera.normalize();
			gui.viewportX = gui.camera.tileX();
			gui.viewportY = gui.camera.tileY();
		};
		const auto checksum = gui.game.checkSum();
		require(!gui.touch->scrollAnimating(), "Nothing moves before a gesture");
		// A rightward flick moves the origin left; from 300 px it coasts past zero.
		placeCamera(300, 300);
		flick(400, 300, 40, 0);
		require(gui.touch->scrollAnimating(), "A flick keeps the map coasting");
		double previous = gui.camera.originX, travelled = 0;
		int frames = 0;
		bool wrapped = false;
		while (gui.touch->scrollAnimating() && frames < 600)
		{
			frame(16);
			++frames;
			const double delta = MapCamera::wrap(previous - gui.camera.originX, mapWidth);
			require(delta >= 0 && delta < mapWidth / 2, "Coasting keeps the flick's direction");
			require(gui.viewportX == (gui.camera.tileX() & gui.game.map.getMaskW()),
					"The tile viewport follows the camera while coasting");
			if (gui.camera.originX > previous)
				wrapped = true;
			travelled += delta;
			previous = gui.camera.originX;
		}
		require(frames > 1 && frames < 600, "Coasting ends on its own");
		require(travelled > 200, "Coasting carries the map well past the drag");
		require(wrapped, "Coasting crosses the toroidal seam");
		require(gui.game.checkSum() == checksum, "Coasting never touches the simulation");
		// A touch catches the map where it is and is not a pan.
		placeCamera(1000, 300);
		flick(400, 300, 40, 0);
		frame(16);
		require(gui.touch->scrollAnimating(), "The map coasts again");
		finger(SDL_FINGERDOWN, 400, 300);
		require(!gui.touch->scrollAnimating(), "A finger stops the coasting map");
		const double held = gui.camera.originX;
		frame(100);
		require(std::abs(gui.camera.originX - held) < 1e-6, "The stopped map stays under the finger");
		finger(SDL_FINGERMOTION, 412, 300);
		frame(80); // the finger rests before lifting
		finger(SDL_FINGERUP, 412, 300);
		frame(16);
		require(!gui.touch->scrollAnimating(), "A finger that rests before lifting leaves no momentum");
		require(std::abs(MapCamera::wrap(held - gui.camera.originX, mapWidth) - 12) < 1e-6, "The drag after the stop still pans");
		// Suspending input stops coasting too.
		flick(400, 300, 0, 40);
		frame(16);
		require(gui.touch->scrollAnimating(), "A vertical flick coasts");
		gui.suspendInput();
		require(!gui.touch->scrollAnimating(), "suspendInput stops coasting");
		const double suspended = gui.camera.originY;
		frame(100);
		require(std::abs(gui.camera.originY - suspended) < 1e-6, "Nothing moves after suspendInput");
		require(gui.game.checkSum() == checksum, "Touch navigation leaves the simulation alone");

		// The phone HUD: the build palette coasts, rubber-bands and springs back.
		SDL_setenv("GLOB2_MOBILE_UI", "1", 1);
		gfx->setResponsiveViewport(true, 800, 600);
		auto resizeWindow = [&](int width, int height)
		{
			const int oldW = gfx->getW(), oldH = gfx->getH();
			SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()), width, height);
			SDL_Event resize{};
			resize.type = SDL_WINDOWEVENT;
			resize.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
			GAGCore::GraphicContext::translateMouseEvent(&resize);
			gui.viewportResized(oldW, oldH, gfx->getW(), gfx->getH());
		};
		resizeWindow(568, 320);
		require(gui.touch->usesHUD(), "Phone HUD must be active");
		gui.clearSelection();
		gui.displayMode = GameGUI::CONSTRUCTION_VIEW;
		gui.touch->panelOpen = true;
		gui.touch->panelScroll = 0;
		gui.touch->clampScroll();
		// The phone panel is sized to its palette, so its range is the end itself:
		// pulling stretches the palette past the end and releasing springs back.
		const double maximum = gui.touch->panelAxis.axis.maximum();
		const auto panel = gui.touch->panelContent();
		// The panel's left margin has no palette items, whose touch starts a placement.
		const float px = float(panel.x + 1), py = float(panel.y + panel.h / 2);
		require(gui.touch->interfaceRegion({px, py}) == 3, "The panel margin is the panel region");
		for (float y : {py + 60, py, py - 30, py + 40, py - 40})
			require(!gui.touch->paletteItemAt({px, y}), "The gestures avoid palette items");
		finger(SDL_FINGERDOWN, px, py + 60);
		frame(16);
		finger(SDL_FINGERMOTION, px, py);
		require(gui.touch->panelScroll > maximum, "Pulling past the end stretches the palette");
		require(gui.touch->panelScroll < maximum + 60, "The stretch is shorter than the finger's move");
		const double stretched = gui.touch->panelScroll;
		frame(16);
		finger(SDL_FINGERMOTION, px, py - 30);
		require(gui.touch->panelScroll > stretched, "Pulling further stretches further");
		frame(80); // the finger rests before lifting
		finger(SDL_FINGERUP, px, py - 30);
		require(gui.touch->scrollAnimating(), "Released stretched content springs back");
		frames = 0;
		previous = gui.touch->panelScroll;
		while (gui.touch->scrollAnimating() && frames++ < 600)
		{
			frame(16);
			require(gui.touch->panelScroll >= maximum, "The spring never overshoots back");
			require(gui.touch->panelScroll <= previous + 1e-9, "The spring only returns");
			previous = gui.touch->panelScroll;
		}
		require(frames < 600, "The spring settles");
		require(gui.touch->panelScroll == maximum, "The palette settles at its end");
		// A flick reaches the end at once and hands its speed to the spring.
		flick(px, py + 40, 0, -20);
		require(gui.touch->scrollAnimating(), "A flick keeps the palette moving");
		bool overshot = false;
		frames = 0;
		while (gui.touch->scrollAnimating() && frames++ < 600)
		{
			frame(16);
			require(gui.touch->panelScroll >= maximum, "The flick never leaves the range on the wrong side");
			require(gui.touch->panelScroll < maximum + panel.h, "The flick's stretch stays inside the panel");
			overshot = overshot || gui.touch->panelScroll > maximum;
		}
		require(overshot && frames < 600, "The flick overshoots the end and settles");
		require(gui.touch->panelScroll == maximum, "The palette rests at its end after the flick");
		// A touch mid-bounce holds the stretch, and lifting lets it finish.
		flick(px, py + 40, 0, -20);
		frame(16);
		require(gui.touch->panelScroll > maximum && gui.touch->scrollAnimating(), "mid-bounce");
		finger(SDL_FINGERDOWN, px, py);
		require(!gui.touch->scrollAnimating(), "A touch stops the bounce");
		const double caught = gui.touch->panelScroll;
		frame(100);
		require(gui.touch->panelScroll == caught, "The stopped palette holds its stretch");
		finger(SDL_FINGERUP, px, py);
		require(gui.touch->scrollAnimating(), "Lifting lets the stretch spring back");
		frames = 0;
		while (gui.touch->scrollAnimating() && frames++ < 600)
			frame(16);
		require(gui.touch->panelScroll == maximum, "The palette returns to its end");
		// A mouse drag (a synthetic finger) scrolls the palette without momentum.
		auto mouse = [&](Uint32 type, int x, int y)
		{
			SDL_Event event{};
			event.type = type;
			event.common.timestamp = Uint32(now);
			if (type == SDL_MOUSEMOTION)
			{
				event.motion.x = x;
				event.motion.y = y;
				event.motion.state = SDL_BUTTON_LMASK;
			}
			else
			{
				event.button.button = SDL_BUTTON_LEFT;
				event.button.x = x;
				event.button.y = y;
			}
			gui.processEvent(&event);
		};
		gui.touch->panelScroll = 0;
		gui.touch->clampScroll();
		mouse(SDL_MOUSEBUTTONDOWN, int(px), int(py + 40));
		for (int i = 1; i <= 4; ++i)
		{
			frame(16);
			mouse(SDL_MOUSEMOTION, int(px), int(py + 40 - 20 * i));
		}
		mouse(SDL_MOUSEBUTTONUP, int(px), int(py - 40));
		require(gui.touch->panelScroll == maximum, "A mouse drag neither stretches nor coasts");
		require(!gui.touch->scrollAnimating(), "A mouse drag has no momentum");
		require(gui.selectionMode == GameGUI::NO_SELECTION, "Panel gestures never start a placement");
		require(gui.game.checkSum() == checksum, "HUD scrolling leaves the simulation alone");
		SDL_setenv("GLOB2_MOBILE_UI", "0", 1);
		resizeWindow(800, 600);
	}

	// The phone editor: touch pans move the camera by the finger distance (no
	// whole-tile steps), the map coasts and wraps after a flick, a pinch zooms
	// by the finger ratio, and the tool tray coasts.
	static void editorScrollPhysics()
	{
		MapEdit editor;
		require(editor.load("maps/balanced.map"), "Editor fixture loads");
		editor.phone = std::make_unique<PhoneEditor>(editor);
		auto &touch = *editor.phone;
		auto *gfx = globalContainer->gfx;
		const double unit = gfx->logicalUnitsPerPoint();
		touch.chooseMode(0);
		touch.prepare();
		editor.updateCamera();
		Uint32 tick = 9000;
		auto finger = [&](Uint32 kind, int id, GAGCore::ViewPoint p)
		{
			SDL_Event event{};
			event.type = kind;
			event.tfinger.timestamp = tick;
			event.tfinger.touchId = 19;
			event.tfinger.fingerId = id;
			event.tfinger.x = p.x / gfx->getW();
			event.tfinger.y = p.y / gfx->getH();
			touch.event(event);
		};
		auto frame = [&](Uint32 ms)
		{
			tick += ms;
			touch.advance(tick);
		};
		auto placeCamera = [&](double x, double y)
		{
			editor.updateCamera();
			editor.camera.originX = x;
			editor.camera.originY = y;
			editor.camera.normalize();
			editor.viewportX = editor.camera.tileX();
			editor.viewportY = editor.camera.tileY();
		};
		const double mapWidth = editor.game.map.getW() * 32.0;
		auto checksum = [&] { return editor.game.checkSum(nullptr, nullptr, nullptr, true); };
		const auto before = checksum();
		const GAGCore::ViewPoint start{touch.content.x + touch.content.w / 2,
									   touch.content.y + touch.content.h / 2};
		// A short drag pans by exactly the finger distance.
		placeCamera(300, 300);
		finger(SDL_FINGERDOWN, 1, start);
		frame(16);
		finger(SDL_FINGERMOTION, 1, {start.x + 10 * unit, start.y});
		require(std::abs(MapCamera::wrap(300 - editor.camera.originX, mapWidth) - 10 * unit) < 1e-6,
				"A ten point drag pans the editor map by ten points");
		require(editor.viewportX == (editor.camera.tileX() & editor.game.map.wMask),
				"The editor tile viewport follows its camera");
		frame(80);
		finger(SDL_FINGERUP, 1, {start.x + 10 * unit, start.y});
		require(!touch.animating(), "A finger that rests before lifting leaves no momentum");
		// A flick coasts, wraps across the seam and stops; terrain is untouched.
		placeCamera(200, 300);
		finger(SDL_FINGERDOWN, 1, start);
		for (int i = 1; i <= 4; ++i)
		{
			frame(16);
			finger(SDL_FINGERMOTION, 1, {start.x + 40 * unit * i, start.y});
		}
		finger(SDL_FINGERUP, 1, {start.x + 160 * unit, start.y});
		require(touch.animating(), "A flick keeps the editor map coasting");
		double previous = editor.camera.originX;
		int frames = 0;
		bool wrapped = false;
		while (touch.animating() && frames < 600)
		{
			frame(16);
			++frames;
			const double delta = MapCamera::wrap(previous - editor.camera.originX, mapWidth);
			require(delta >= 0 && delta < mapWidth / 2, "Editor coasting keeps its direction");
			require(editor.viewportX == (editor.camera.tileX() & editor.game.map.wMask),
					"The editor tile viewport follows the coasting camera");
			if (editor.camera.originX > previous)
				wrapped = true;
			previous = editor.camera.originX;
		}
		require(frames > 1 && frames < 600 && wrapped, "Editor coasting wraps and ends on its own");
		require(checksum() == before, "Coasting never touches the map data");
		// A touch catches the map where it is.
		placeCamera(1000, 300);
		finger(SDL_FINGERDOWN, 1, start);
		for (int i = 1; i <= 4; ++i)
		{
			frame(16);
			finger(SDL_FINGERMOTION, 1, {start.x + 40 * unit * i, start.y});
		}
		finger(SDL_FINGERUP, 1, {start.x + 160 * unit, start.y});
		frame(16);
		require(touch.animating(), "The editor map coasts again");
		finger(SDL_FINGERDOWN, 1, start);
		require(!touch.animating(), "A finger stops the coasting editor map");
		const double held = editor.camera.originX;
		frame(100);
		require(std::abs(editor.camera.originX - held) < 1e-6, "The stopped editor map stays put");
		finger(SDL_FINGERMOTION, 1, {start.x + 12 * unit, start.y});
		frame(80);
		finger(SDL_FINGERUP, 1, {start.x + 12 * unit, start.y});
		require(!touch.animating(), "A rested release leaves the editor map still");
		// A pinch zooms by the finger ratio.
		editor.camera.zoom = 1;
		finger(SDL_FINGERDOWN, 1, start);
		finger(SDL_FINGERDOWN, 2, {start.x + 100, start.y});
		frame(16);
		finger(SDL_FINGERMOTION, 2, {start.x + 150, start.y});
		require(std::abs(editor.camera.zoom - 1.5) < 0.01, "A 1.5x pinch zooms the editor map 1.5x");
		finger(SDL_FINGERUP, 2, {start.x + 150, start.y});
		finger(SDL_FINGERUP, 1, start);
		touch.cancel();
		// The tool tray: pulling past its end stretches it and releasing springs
		// back; a flick overshoots the end and settles there; a mouse drag does
		// neither. (The fixture's palettes fit the tray, so the end is at zero.)
		touch.chooseMode(0);
		touch.prepare();
		const double trayEnd = touch.maximum;
		const auto row = touch.rows.front().rect;
		const GAGCore::ViewPoint at{row.x + row.w / 2, row.y + row.h / 2};
		require(touch.hit(at) == 0, "The gesture starts on the first tray item");
		finger(SDL_FINGERDOWN, 1, at);
		frame(16);
		finger(SDL_FINGERMOTION, 1, {at.x - 40 * unit, at.y});
		require(touch.offset > trayEnd && touch.offset < trayEnd + 40 * unit,
				"Pulling past the end stretches the tray by less than the finger moved");
		const double stretched = touch.offset;
		frame(16);
		finger(SDL_FINGERMOTION, 1, {at.x - 70 * unit, at.y});
		require(touch.offset > stretched, "Pulling further stretches the tray further");
		frame(80);
		finger(SDL_FINGERUP, 1, {at.x - 70 * unit, at.y});
		require(touch.animating(), "The released tray springs back");
		frames = 0;
		double last = touch.offset;
		while (touch.animating() && frames++ < 600)
		{
			frame(16);
			require(touch.offset >= trayEnd && touch.offset <= last + 1e-9, "The tray spring only returns");
			last = touch.offset;
		}
		require(frames < 600 && touch.offset == trayEnd, "The tray settles at its end");
		touch.prepare();
		require(touch.offset == trayEnd, "Layout keeps the tray at its end");
		finger(SDL_FINGERDOWN, 1, at);
		for (int i = 1; i <= 4; ++i)
		{
			frame(16);
			finger(SDL_FINGERMOTION, 1, {at.x - 20 * unit * i, at.y});
		}
		finger(SDL_FINGERUP, 1, {at.x - 80 * unit, at.y});
		require(touch.animating(), "A flick keeps the tray moving");
		bool overshot = false;
		frames = 0;
		while (touch.animating() && frames++ < 600)
		{
			frame(16);
			require(touch.offset >= trayEnd && touch.offset < trayEnd + touch.tray.w, "The flick's stretch stays inside the tray");
			overshot = overshot || touch.offset > trayEnd;
		}
		require(overshot && frames < 600 && touch.offset == trayEnd, "The flick overshoots the end and settles there");
		// A mouse drag on the tray (device -1) neither stretches nor coasts.
		SDL_Event mouse{};
		mouse.type = SDL_MOUSEBUTTONDOWN;
		mouse.common.timestamp = tick;
		mouse.button.button = SDL_BUTTON_LEFT;
		mouse.button.x = int(at.x);
		mouse.button.y = int(at.y);
		touch.event(mouse);
		for (int i = 1; i <= 4; ++i)
		{
			frame(16);
			SDL_Event motion{};
			motion.type = SDL_MOUSEMOTION;
			motion.common.timestamp = tick;
			motion.motion.x = int(at.x - 20 * unit * i);
			motion.motion.y = int(at.y);
			touch.event(motion);
			require(touch.offset == trayEnd, "A mouse drag past the end does not stretch the tray");
		}
		mouse.type = SDL_MOUSEBUTTONUP;
		mouse.common.timestamp = tick;
		mouse.button.x = int(at.x - 80 * unit);
		touch.event(mouse);
		require(!touch.animating() && touch.offset == trayEnd, "A mouse drag has no momentum");
		require(checksum() == before, "Tray scrolling never touches the map data");
	}
};
TEST_SUITE("GameGUITouch")
{
	GLOB2_TEST_CASE("touch scroll momentum on the map; the HUD palette and the editor", "[display]")
	{
		SDL_setenv("GLOB2_MOBILE_UI", "0", 1);
		glob2test::GlobalsOptions options{.display = true, .loadStrings = true, .width = 800, .height = 600,
		                                  .screenFlags = GAGCore::GraphicContext::PORTABLEGPU};
		glob2test::HeadlessGlobals globals(options);
		REQUIRE(SDLNet_Init() == 0);
		GameGUITouchHarness::scrollPhysics();
		SDL_setenv("GLOB2_MOBILE_UI", "0", 1);
		GameGUITouchHarness::editorScrollPhysics();
		SDLNet_Quit();
		SDL_setenv("GLOB2_MOBILE_UI", "0", 1);
	}
	TEST_CASE("actual gameplay touch; toroidal pan; preview; confirmation; validation; cancellation and duplicate suppression [display][artifacts][writes-preferences]")
	{
		// Exercise the legacy mouse sidebar first, even on touch-capable hosts;
		// run() explicitly switches to the phone presentation for the touch cases.
		SDL_setenv("GLOB2_MOBILE_UI", "0", 1);
		glob2test::GlobalsOptions options{.display = true, .loadStrings = true, .width = 800, .height = 600,
		                                  .screenFlags = GAGCore::GraphicContext::PORTABLEGPU};
		glob2test::HeadlessGlobals globals(options);
		REQUIRE(SDLNet_Init() == 0);
		verifyTouchFontRaster();
		GameGUITouchHarness::run();
		SDLNet_Quit();
		SDL_setenv("GLOB2_MOBILE_UI", "0", 1);
		glob2test::retainFromProfile(".bmp");
		std::puts("PASS: actual gameplay touch, toroidal pan, preview, confirmation, validation, "
				  "cancellation and duplicate suppression");
	}
}
