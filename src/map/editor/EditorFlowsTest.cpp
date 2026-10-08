// SPDX-License-Identifier: GPL-3.0-or-later
// Editor flows that guard unsaved work: quit and window close, replacing,
// sharing and rerolling the map, overwrite confirmation, fertility progress
// and dialogs that must not mark an unchanged map modified.
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "EditorDock.h"
#include "MapEditorScreen.h"
#include "LoadSaveDialog.h"
#include <FileManager.h>
#include <ScreenStack.h>
#include <Toolkit.h>
#include <filesystem>

namespace
{
glob2test::GlobalsOptions displayOptions()
{
	return {.display = true, .loadStrings = true, .width = 1024, .height = 768,
			.screenFlags = GAGCore::GraphicContext::PORTABLEGPU};
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

std::unique_ptr<MapEdit> blankEditor()
{
	auto editor = std::make_unique<MapEdit>();
	blank(*editor);
	return editor;
}

SDL_Event quitEvent()
{
	SDL_Event event{};
	event.type = SDL_EVENT_QUIT;
	return event;
}

SDL_Event escapeEvent()
{
	SDL_Event event{};
	event.type = SDL_EVENT_KEY_DOWN;
	event.key.key = SDLK_ESCAPE;
	event.key.scancode = SDL_SCANCODE_ESCAPE;
	return event;
}

void poll(MapEdit &editor)
{
	SDL_Event event{};
	event.type = SDL_EVENT_USER;
	editor.delegateMenu(event);
}

// Drive the editor until its save dialog closes (fertility runs in slices).
void finishSave(MapEdit &editor, Uint32 &tick)
{
	// The map file is written on a background thread; give it wall time.
	for (int frame = 0; frame < 5000 && editor.showingSave; ++frame)
	{
		editor.advanceEditing({}, tick += 33);
		SDL_Delay(1);
	}
	REQUIRE_FALSE(editor.showingSave);
}

// Paint the editor and its open card without advancing the card.
void capture(MapEdit &editor, const char *name)
{
	auto *gfx = globalContainer->gfx;
	editor.drawMap(0, 0, gfx->getW(), gfx->getH());
	editor.drawMenuEyeCandy();
	editor.drawDock(SDL_GetTicks());
	editor.drawFlowOverlays();
	editor.drawDialog();
	gfx->printScreen(glob2test::artifactDirFromWorkingDirectory() + "/" + name);
	gfx->nextFrame();
}

void removeMap(const std::string &file)
{
	auto *manager = GAGCore::Toolkit::getFileManager();
	for (const auto &path : {file, file + ".gz"})
	{
		std::error_code ignored;
		std::filesystem::remove(std::filesystem::path(manager->getDir(0)) / path, ignored);
	}
}
} // namespace

TEST_SUITE("EditorFlows")
{
	TEST_CASE("window close quits a clean editor and asks in the editor when it is dirty [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		{
			GAGGUI::ScreenStack screens(*globals->gfx);
			screens.push(std::make_unique<MapEditorScreen>(screens, blankEditor()));
			screens.frame(0, {});
			CHECK_FALSE(screens.quitIntercepted());
			screens.frame(33, {quitEvent()});
			CHECK_FALSE(screens.running());
			CHECK(screens.result() == GAGGUI::Screen::QUIT_APPLICATION);
		}
		{
			auto owned = blankEditor();
			MapEdit &editor = *owned;
			GAGGUI::ScreenStack screens(*globals->gfx);
			screens.push(std::make_unique<MapEditorScreen>(screens, std::move(owned)));
			screens.frame(0, {});
			editor.mapHasBeenModified();
			REQUIRE(screens.quitIntercepted());
			screens.frame(33, {quitEvent()});
			REQUIRE(screens.running());
			CHECK(dynamic_cast<MapEditorScreen *>(screens.top()));
			REQUIRE(editor.confirmation());
			CHECK(editor.pendingConfirm() == MapEdit::ConfirmPurpose::QuitApplication);
			CHECK(editor.confirmation()->choiceCount() == 3);
			capture(editor, "editor-quit-card.bmp");
			// The decision card publishes the keys of the former quit prompt.
			editor.confirmation()->draw(0);
			for (const char *key : {"choice/0", "choice/1", "choice/2"})
				CHECK(editor.confirmation()->host().bounds(key).w > 0);
			// Escape is the safe answer: keep editing, still dirty.
			screens.frame(66, {escapeEvent()});
			screens.frame(99, {});
			CHECK(screens.running());
			CHECK_FALSE(editor.confirmation());
			CHECK(editor.hasUnsavedChanges());
			// Don't save ends the application.
			screens.frame(132, {quitEvent()});
			REQUIRE(editor.confirmation());
			editor.confirmation()->choose(1);
			screens.frame(165, {});
			screens.frame(198, {});
			CHECK_FALSE(screens.running());
			CHECK(screens.result() == GAGGUI::Screen::QUIT_APPLICATION);
		}
	}

	TEST_CASE("menu quit with unsaved changes uses the in-editor card and save continues to quit [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		MapEdit editor;
		blank(editor);
		editor.beginEditing();
		editor.mapHasBeenModified();
		editor.performAction("open menu screen");
		capture(editor, "editor-menu.bmp");
		editor.performAction("close menu screen");
		editor.performAction("quit editor");
		Uint32 tick = 1000;
		REQUIRE(editor.advanceEditing({}, tick += 33));
		REQUIRE(editor.needsQuitDecision());
		CHECK(editor.pendingConfirm() == MapEdit::ConfirmPurpose::Quit);
		editor.resolveQuitDecision(0);
		REQUIRE(editor.showingSave);
		CHECK(editor.doQuitAfterLoadSave);
		const std::string name = "Editor flows quit fixture";
		editor.loadSaveScreen->setName(name);
		editor.loadSaveScreen->confirmPresentedFile();
		poll(editor);
		REQUIRE(editor.fertilityRequested);
		// Fertility runs in a progress card over the map, then the save completes.
		editor.advanceEditing({}, tick += 33);
		CHECK(editor.fertilityProgress());
		capture(editor, "editor-fertility-progress.bmp");
		bool running = true;
		for (int frame = 0; frame < 5000 && running; ++frame)
		{
			running = editor.advanceEditing({}, tick += 33);
			SDL_Delay(1);
		}
		CHECK_FALSE(running);
		CHECK(editor.editingReturnCode() == 0);
		CHECK_FALSE(editor.hasUnsavedChanges());
		const auto saved = editor.savedMapFile();
		REQUIRE_FALSE(saved.empty());
		MapEdit restored;
		CHECK(restored.load(saved));
		CHECK(restored.game.map.getW() == editor.game.map.getW());
		removeMap(saved);
	}

	TEST_CASE("cancelling fertility during a save keeps the save dialog with a message [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		MapEdit editor;
		blank(editor);
		editor.beginEditing();
		editor.mapHasBeenModified();
		editor.performAction("open save screen");
		editor.loadSaveScreen->setName("Editor flows cancelled fixture");
		editor.loadSaveScreen->confirmPresentedFile();
		poll(editor);
		Uint32 tick = 1000;
		editor.advanceEditing({}, tick += 33);
		REQUIRE(editor.fertilityProgress());
		editor.advanceEditing({escapeEvent()}, tick += 33);
		CHECK_FALSE(editor.fertilityProgress());
		REQUIRE(editor.showingSave);
		CHECK_FALSE(editor.loadSaveScreen->finished());
		CHECK(editor.loadSaveScreen->filePresentation().failed);
		CHECK_FALSE(editor.loadSaveScreen->filePresentation().status.empty());
		CHECK(editor.hasUnsavedChanges());
	}

	TEST_CASE("sharing a never-saved map saves it first and then shares the saved file [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		MapEdit editor;
		blank(editor);
		editor.beginEditing();
		REQUIRE(editor.savedMapFile().empty());
		editor.performAction("share map");
		REQUIRE(editor.confirmation());
		CHECK(editor.pendingConfirm() == MapEdit::ConfirmPurpose::ShareSaveFirst);
		CHECK(editor.confirmation()->choiceCount() == 2);
		CHECK(editor.takeShareRequest().empty());
		editor.confirmation()->choose(0);
		poll(editor);
		REQUIRE(editor.showingSave);
		editor.loadSaveScreen->setName("Editor flows share fixture");
		editor.loadSaveScreen->confirmPresentedFile();
		poll(editor);
		Uint32 tick = 1000;
		finishSave(editor, tick);
		const auto shared = editor.takeShareRequest();
		CHECK_FALSE(shared.empty());
		CHECK(shared == editor.savedMapFile());
		CHECK_FALSE(editor.hasUnsavedChanges());
		CHECK_FALSE(editor.lastStatus().empty());
		// A clean saved map shares at once; a dirty one offers the saved version.
		editor.performAction("share map");
		CHECK_FALSE(editor.confirmation());
		CHECK(editor.takeShareRequest() == shared);
		editor.mapHasBeenModified();
		editor.performAction("share map");
		REQUIRE(editor.confirmation());
		CHECK(editor.confirmation()->choiceCount() == 3);
		editor.confirmation()->choose(1);
		poll(editor);
		CHECK(editor.takeShareRequest() == shared);
		editor.finishShare(true);
		CHECK_FALSE(editor.lastStatus().empty());
		removeMap(shared);
	}

	TEST_CASE("loading another map asks about unsaved changes first [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		MapEdit editor;
		blank(editor);
		editor.performAction("open load screen");
		CHECK(editor.showingLoad);
		CHECK_FALSE(editor.confirmation());
		editor.performAction("close load screen");
		editor.mapHasBeenModified();
		editor.performAction("open load screen");
		CHECK_FALSE(editor.showingLoad);
		REQUIRE(editor.confirmation());
		CHECK(editor.pendingConfirm() == MapEdit::ConfirmPurpose::LoadUnsaved);
		editor.confirmation()->choose(2);
		poll(editor);
		CHECK_FALSE(editor.showingLoad);
		editor.performAction("open load screen");
		editor.confirmation()->choose(1);
		poll(editor);
		CHECK(editor.showingLoad);
		CHECK(editor.hasUnsavedChanges());
	}

	TEST_CASE("rerolling the terrain look from the menu asks first [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		MapEdit editor;
		blank(editor);
		editor.game.map.setTerrainSeed(7);
		editor.performAction("request reroll terrain look");
		REQUIRE(editor.confirmation());
		CHECK(editor.pendingConfirm() == MapEdit::ConfirmPurpose::RerollTerrain);
		CHECK(editor.confirmation()->cancelChoice() == 1);
		SDL_Event escape = escapeEvent();
		editor.processEvent(escape);
		CHECK_FALSE(editor.confirmation());
		CHECK(editor.game.map.terrainSeed() == 7);
		CHECK_FALSE(editor.hasUnsavedChanges());
		for (int attempt = 0; attempt < 4 && editor.game.map.terrainSeed() == 7; ++attempt)
		{
			editor.performAction("request reroll terrain look");
			editor.confirmation()->choose(0);
			poll(editor);
		}
		CHECK(editor.game.map.terrainSeed() != 7);
		CHECK(editor.hasUnsavedChanges());
	}

	TEST_CASE("teams scenario and area-name dialogs mark the map modified only for real changes [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		MapEdit editor;
		blank(editor);
		editor.performAction("add team");
		editor.hasMapBeenModified = false;
		const auto colour = editor.game.mapHeader.getBaseTeam(1).color;
		editor.game.mapHeader.getBaseTeam(1).color = GAGCore::Color(1, 2, 3);
		editor.performAction("open teams editor");
		REQUIRE(editor.teamsEditor);
		editor.teamsEditor->event(escapeEvent());
		poll(editor);
		CHECK_FALSE(editor.teamsEditor);
		CHECK_FALSE(editor.hasUnsavedChanges());
		CHECK(editor.game.mapHeader.getBaseTeam(1).color == GAGCore::Color(1, 2, 3));
		editor.game.mapHeader.getBaseTeam(1).color = colour;

		editor.performAction("open teams editor");
		editor.teamsEditor->confirm();
		poll(editor);
		CHECK_FALSE(editor.hasUnsavedChanges());
		editor.performAction("open teams editor");
		editor.teamsEditor->setActive(1, !editor.teamsEditor->slot(1).active);
		editor.teamsEditor->confirm();
		poll(editor);
		CHECK(editor.hasUnsavedChanges());

		editor.hasMapBeenModified = false;
		editor.performAction("open scenario editor");
		REQUIRE(editor.scriptEditor);
		editor.scriptEditor->event(escapeEvent());
		poll(editor);
		CHECK_FALSE(editor.scriptEditor);
		CHECK_FALSE(editor.hasUnsavedChanges());
		editor.performAction("open scenario editor");
		editor.scriptEditor->confirm();
		poll(editor);
		CHECK_FALSE(editor.hasUnsavedChanges());

		editor.performAction("open area name");
		REQUIRE(editor.areaName);
		editor.areaName->setText("Changed but cancelled");
		editor.areaName->event(escapeEvent());
		poll(editor);
		CHECK_FALSE(editor.areaName);
		CHECK_FALSE(editor.hasUnsavedChanges());
		CHECK(editor.game.map.getAreaName(editor.areaNumber->getIndex()) != "Changed but cancelled");
		editor.performAction("update script area number");
		CHECK_FALSE(editor.hasUnsavedChanges());
		editor.performAction("open area name");
		editor.areaName->setText("North lake");
		editor.areaName->confirm();
		poll(editor);
		CHECK(editor.game.map.getAreaName(editor.areaNumber->getIndex()) == "North lake");
		CHECK(editor.hasUnsavedChanges());
	}

	TEST_CASE("terrain strokes mark the fertility overlay stale until it is refreshed [display][artifacts]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		MapEdit editor;
		blank(editor);
		editor.beginEditing();
		editor.isFertilityOn = true;
		editor.performAction("compute fertility");
		Uint32 tick = 1000;
		for (int frame = 0; frame < 2000 && (editor.fertilityRequested || editor.fertilityProgress()); ++frame)
			editor.advanceEditing({}, tick += 33);
		REQUIRE_FALSE(editor.fertilityOverlayStale());
		editor.performAction("select water");
		editor.mouseX = 4 * 32 + 16;
		editor.mouseY = 4 * 32 + 16;
		editor.performAction("terrain drag start");
		editor.performAction("terrain drag end");
		CHECK(editor.fertilityOverlayStale());
		capture(editor, "editor-fertility-stale.bmp");
		// Docked editors refresh from the dock's fertility controls; the chip
		// over the map is for presentations without a dock.
		REQUIRE(editor.dock);
		const auto chip = editor.fertilityChipRect();
		SDL_Event down{};
		down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
		down.button.button = SDL_BUTTON_LEFT;
		down.button.x = float(chip.x + chip.width / 2);
		down.button.y = float(chip.y + chip.height / 2);
		CHECK_FALSE(editor.handleFlowEvent(down));
		editor.dock->showTab(EditorDock::Tab::Terrain, true);
		editor.dock->update(tick);
		auto &host = editor.dock->host();
		host.layoutIfNeeded();
		REQUIRE(host.find("dock/fertility/refresh"));
		host.scrollIntoView("dock/fertility/refresh");
		host.layoutIfNeeded();
		const auto refresh = host.bounds("dock/fertility/refresh");
		host.tapAt({refresh.x + refresh.w / 2, refresh.y + refresh.h / 2});
		CHECK(editor.fertilityRequested);
		for (int frame = 0; frame < 2000 && (editor.fertilityRequested || editor.fertilityProgress()); ++frame)
			editor.advanceEditing({}, tick += 33);
		CHECK_FALSE(editor.fertilityOverlayStale());
		CHECK(editor.isFertilityOn);
	}

	TEST_CASE("saving over an existing file asks inline, except for the document's own file [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		glob2test::TempDir scratch;
		std::filesystem::create_directories(scratch.path / "games");
		glob2test::writeFile(scratch.path / "games" / "Existing.game", "fixture");
		GAGCore::Toolkit::getFileManager()->addDir(scratch.path.string());
		{
			LoadSaveDialog dialog("games", "game", false, "Save game");
			dialog.setName("Existing");
			dialog.confirmPresentedFile();
			CHECK_FALSE(dialog.finished());
			CHECK(dialog.filePresentation().confirmingOverwrite);
			dialog.attach(*globals->gfx);
			dialog.draw(0);
			CHECK(dialog.host().bounds("overwrite").w > 0);
			CHECK(dialog.host().bounds("overwrite/cancel").w > 0);
			dialog.event(escapeEvent());
			CHECK_FALSE(dialog.finished());
			CHECK_FALSE(dialog.filePresentation().confirmingOverwrite);
			dialog.confirmPresentedFile();
			dialog.confirmOverwrite();
			CHECK(dialog.finished());
			CHECK(dialog.result() == LoadSaveDialog::OK);
		}
		{
			LoadSaveDialog dialog("games", "game", false, "Save game");
			dialog.setName("Existing");
			dialog.confirmPresentedFile();
			dialog.setName("Brand new");
			CHECK_FALSE(dialog.filePresentation().confirmingOverwrite);
			dialog.confirmPresentedFile();
			CHECK(dialog.finished());
		}
		{
			LoadSaveDialog dialog("games", "game", false, "Save game");
			dialog.allowOverwriteOf("Existing");
			dialog.setName("Existing");
			dialog.confirmPresentedFile();
			CHECK(dialog.finished());
		}
	}

	TEST_CASE("definition imports offer the device picker and explain an empty folder [display]")
	{
		glob2test::HeadlessGlobals globals(displayOptions());
		MapEdit editor;
		blank(editor);
		editor.performAction("import resource definitions");
		REQUIRE(editor.loadSaveScreen);
		const auto files = editor.loadSaveScreen->filePresentation();
		CHECK(files.deviceImport == GAGCore::ApplicationHost::canImportFiles());
		// Device bytes take the same JSON path as a file from the folder:
		// invalid definitions are rejected without touching the map.
		CHECK_THROWS(editor.importResourceJson("not JSON"));
		CHECK_FALSE(editor.hasUnsavedChanges());
	}
}
