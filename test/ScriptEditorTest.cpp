#include <Environment.h>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ScriptEditorScreen.h"
#include "MapEdit.h"
#include "gui/LoadSaveDialog.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <algorithm>
#include <fstream>
#include <tuple>

using Language = ScriptEditorScreen::Language;

TEST_CASE("JavaScript maps reopen in the editor without resetting live globals" *
		  doctest::test_suite("ScriptEditor"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	globals->settings.optionFlags &= ~GlobalContainer::OPTION_MAP_EDIT_USE_USL;
	glob2test::HeadlessGame world;
	auto &map = world.game.mapscript;
	map.setMapScriptMode(MapScript::JavaScript);
	map.setMapScript("let calls=0; function step(ctx) { calls++; return []; }");
	REQUIRE(map.compileCode());
	map.syncStep(&world.gui);
	const auto checksum = map.checkSum();
	const auto source = map.getMapScript();
	ScriptEditorScreen editor(&world.game);
	CHECK(editor.language() == Language::JavaScript);
	CHECK(editor.scriptText() == source);
	// Compile checks syntax only, including top-level code that would throw.
	editor.setScriptText("throw new Error('do not evaluate'); function main(ctx) { return []; }");
	CHECK(editor.compile());
	CHECK(map.getMapScript() == source);
	CHECK(map.getMapScriptMode() == MapScript::JavaScript);
	CHECK(map.checkSum() == checksum);
	editor.selectLanguage(Language::USL);
	editor.setScriptText("");
	CHECK(editor.compile());
	CHECK(map.checkSum() == checksum);
}

TEST_CASE("Script language drafts survive switching and invalid JavaScript cannot commit" *
		  doctest::test_suite("ScriptEditor"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	globals->settings.optionFlags &= ~GlobalContainer::OPTION_MAP_EDIT_USE_USL;
	glob2test::HeadlessGame world;
	world.game.sgslScript.sourceCode = "// retained legacy script\n";
	ScriptEditorScreen editor(&world.game);
	CHECK(editor.language() == Language::SGSL);
	const auto legacy = editor.scriptText();
	editor.selectLanguage(Language::JavaScript);
	CHECK(editor.scriptText().find("function step(ctx)") != std::string::npos);
	editor.setScriptText("function step( {");
	editor.confirm();
	CHECK_FALSE(editor.finished());
	CHECK_FALSE(editor.compilationText().empty());
	CHECK(world.game.mapscript.getMapScriptMode() == MapScript::USL);
	CHECK(world.game.mapscript.getMapScript().empty());
	editor.selectLanguage(Language::SGSL);
	CHECK(editor.scriptText() == legacy);
	editor.selectLanguage(Language::JavaScript);
	CHECK(editor.scriptText() == "function step( {");
	CHECK(editor.compilationText().empty());
	editor.setScriptText("let calls=0; function main(ctx) { calls++; return []; }");
	editor.confirm();
	CHECK(editor.finished());
	CHECK(editor.result() == ScriptEditorScreen::OK);
	CHECK(world.game.mapscript.getMapScriptMode() == MapScript::JavaScript);
	CHECK(world.game.mapscript.getMapScript() == editor.scriptText());
	CHECK(world.game.sgslScript.sourceCode == legacy);
	world.game.mapscript.syncStep(&world.gui);
}

TEST_CASE("Cancelling script drafts preserves the source, globals and scenario text" *
		  doctest::test_suite("ScriptEditor"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	glob2test::HeadlessGame world;
	auto &map = world.game.mapscript;
	map.setMapScriptMode(MapScript::JavaScript);
	map.setMapScript("let calls=0; function step(ctx) { calls++; return []; }");
	REQUIRE(map.compileCode());
	map.syncStep(&world.gui);
	const auto source = map.getMapScript();
	const auto checksum = map.checkSum();
	world.game.missionBriefing = "Keep this briefing";
	ScriptEditorScreen editor(&world.game);
	editor.setScriptText("function main(ctx) { return []; }");
	REQUIRE(editor.compile());
	editor.selectLanguage(Language::USL);
	editor.setScriptText("");
	REQUIRE(editor.compile());
	SDL_Event cancel{};
	cancel.type = SDL_EVENT_KEY_DOWN;
	cancel.key.key = SDLK_ESCAPE;
	editor.event(cancel);
	CHECK(editor.finished());
	CHECK(editor.result() == ScriptEditorScreen::CANCEL);
	CHECK(map.getMapScriptMode() == MapScript::JavaScript);
	CHECK(map.getMapScript() == source);
	CHECK(map.checkSum() == checksum);
	CHECK(world.game.missionBriefing == "Keep this briefing");
}

TEST_CASE(
	"Editor saves and reloads the selected JavaScript map mode and source [display][artifacts]" *
	doctest::test_suite("ScriptEditor"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	options.display = true;
	glob2test::HeadlessGlobals globals(options);
	globals->settings.optionFlags &= ~GlobalContainer::OPTION_MAP_EDIT_USE_USL;
	MapEdit map;
	REQUIRE(map.load("maps/balanced.map"));
	const auto legacy = map.game.sgslScript.sourceCode;
	ScriptEditorScreen editor(&map.game);
	editor.selectLanguage(Language::JavaScript);
	editor.setScriptText("let calls=0; function step(ctx) { calls++; return []; }");
	editor.confirm();
	REQUIRE(editor.finished());
	const auto path = glob2test::artifactDir() / "javascript-editor.map.gz";
	REQUIRE(map.save(path.string(), "JavaScript editor"));
	MapEdit restored;
	REQUIRE(restored.load(path.string()));
	CHECK(restored.game.mapscript.getMapScriptMode() == MapScript::JavaScript);
	CHECK(restored.game.mapscript.getMapScript() == editor.scriptText());
	CHECK(restored.game.sgslScript.sourceCode == legacy);
	ScriptEditorScreen reopened(&restored.game);
	CHECK(reopened.language() == Language::JavaScript);
	CHECK(reopened.scriptText() == editor.scriptText());
	reopened.selectLanguage(Language::SGSL);
	reopened.confirm();
	CHECK(reopened.finished());
	CHECK(restored.game.mapscript.getMapScriptMode() == MapScript::USL);
	CHECK(restored.game.mapscript.getMapScript().empty());
	CHECK(restored.game.sgslScript.sourceCode == legacy);
	const auto sgslPath = glob2test::artifactDir() / "javascript-editor-sgsl.map.gz";
	REQUIRE(restored.save(sgslPath.string(), "Restored SGSL editor"));
	MapEdit legacyRestored;
	REQUIRE(legacyRestored.load(sgslPath.string()));
	CHECK(legacyRestored.game.mapscript.getMapScriptMode() == MapScript::USL);
	CHECK(legacyRestored.game.mapscript.getMapScript().empty());
	CHECK(legacyRestored.game.sgslScript.sourceCode == legacy);
	ScriptEditorScreen sgslReopened(&legacyRestored.game);
	CHECK(sgslReopened.language() == Language::SGSL);
	sgslReopened.selectLanguage(Language::USL);
	sgslReopened.confirm();
	CHECK(sgslReopened.finished());
	CHECK(legacyRestored.game.mapscript.getMapScriptMode() == MapScript::USL);
	CHECK(legacyRestored.game.mapscript.getMapScript().empty());
}

TEST_CASE("JavaScript script file dialogs use js and leave loading as a draft" *
		  doctest::test_suite("ScriptEditor"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	glob2test::HeadlessGame world;
	ScriptEditorScreen editor(&world.game);
	editor.selectLanguage(Language::JavaScript);
	const std::string source = "let visits=0; function step(ctx) { visits++; return []; }";
	editor.setScriptText(source);
	editor.loadSave(false);
	REQUIRE(editor.fileDialog());
	editor.fileDialog()->setName("Editor JavaScript fixture");
	editor.fileDialog()->confirmPresentedFile();
	const std::string file = editor.fileDialog()->getFileName();
	CHECK(file.ends_with(".js"));
	editor.finishFileDialog();
	CHECK_FALSE(editor.fileDialog());
	editor.setScriptText("function step(ctx) { return []; }");
	editor.loadSave(true);
	REQUIRE(editor.fileDialog());
	const auto presented = editor.fileDialog()->filePresentation();
	const auto found =
		std::find(presented.files.begin(), presented.files.end(), "Editor JavaScript fixture");
	REQUIRE(found != presented.files.end());
	editor.fileDialog()->selectPresentedFile(int(found - presented.files.begin()));
	editor.fileDialog()->confirmPresentedFile();
	editor.finishFileDialog();
	CHECK(editor.scriptText() == source);
	CHECK(world.game.mapscript.getMapScript().empty());
	CHECK(world.game.mapscript.getMapScriptMode() == MapScript::USL);
	CHECK(filenameToName("scripts/Map_Name.js") == "Map Name");
	CHECK(filenameToName("scripts/Map_Name.usl") == "Map Name");
	CHECK(filenameToName("scripts/Map_Name.sgsl") == "Map Name");
	CHECK(filenameToName("scripts/notes.txt") == "notes.txt");
}

TEST_CASE("JavaScript file loading preserves embedded NULs for validation" *
		  doctest::test_suite("ScriptEditor"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	glob2test::HeadlessGame world;
	ScriptEditorScreen editor(&world.game);
	editor.selectLanguage(Language::JavaScript);
	std::string source = "function step(ctx) { return []; }";
	source.push_back('\0');
	source += "invalid source tail";
	editor.setScriptText(source);
	editor.loadSave(false);
	REQUIRE(editor.fileDialog());
	editor.fileDialog()->setName("Editor NUL fixture");
	editor.fileDialog()->confirmPresentedFile();
	editor.finishFileDialog();
	editor.setScriptText("function step(ctx) { return []; }");
	editor.loadSave(true);
	REQUIRE(editor.fileDialog());
	const auto presented = editor.fileDialog()->filePresentation();
	const auto found = std::find(presented.files.begin(), presented.files.end(), "Editor NUL fixture");
	REQUIRE(found != presented.files.end());
	editor.fileDialog()->selectPresentedFile(int(found - presented.files.begin()));
	editor.fileDialog()->confirmPresentedFile();
	editor.finishFileDialog();
	CHECK(editor.scriptText() == source);
	CHECK_FALSE(editor.compile());
	CHECK_FALSE(editor.compilationText().empty());
	CHECK(world.game.mapscript.getMapScript().empty());
}

TEST_CASE("Editing legacy SGSL retains the released USL and SGSL payload pairing" *
		  doctest::test_suite("ScriptEditor"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	glob2test::HeadlessGame world;
	auto &map = world.game.mapscript;
	map.setMapScript("def retained := 1\n");
	REQUIRE(map.compileCode());
	const auto source = map.getMapScript();
	ScriptEditorScreen editor(&world.game);
	editor.selectLanguage(Language::SGSL);
	editor.setScriptText("show(\"Edited legacy SGSL source\")\n");
	editor.confirm();
	CHECK(editor.finished());
	CHECK(map.getMapScriptMode() == MapScript::USL);
	CHECK(map.getMapScript() == source);
	CHECK(world.game.sgslScript.sourceCode == editor.scriptText());
}

TEST_CASE("JavaScript to SGSL prepares both runtimes before committing" *
		  doctest::test_suite("ScriptEditor"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	glob2test::HeadlessGame world;
	auto& legacy = world.game.sgslScript;
	legacy.sourceCode = R"(show("Retained legacy") timer(9) space)";
	REQUIRE(legacy.compileScript(&world.game).type == ErrorReport::ET_OK);
	legacy.syncStep(world.game, world.gui, world.gui.clientRequests);
	auto legacySnapshot = [&] {
		auto* storage = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(storage);
		legacy.save(&output, &world.game);
		return storage->takeContents();
	};
	const auto retainedLegacy = legacySnapshot();
	auto& map = world.game.mapscript;
	map.setMapScriptMode(MapScript::JavaScript);
	map.setMapScript("let calls=0; function step(ctx) { calls++; return []; }");
	REQUIRE(map.compileCode());
	map.syncStep(&world.gui);
	const auto source = map.getMapScript();
	const auto checksum = map.checkSum();
	const auto legacyChecksum = legacy.checkSum();
	world.game.missionBriefing = "Retain briefing on failed preparation";
	ScriptEditorScreen editor(&world.game);
	editor.selectLanguage(Language::SGSL);
	editor.setScriptText(R"(show("Committed legacy") timer(3) win(0) space)");

	// A real broken runtime resource makes even an empty USL backend fail to
	// prepare. The overlay is disposable; neither installed nor tracked data changes.
	glob2test::TempDir overlay("broken-usl-runtime");
	const auto badRuntime = overlay.path / "data/usl/Language/Runtime/invalid-editor-runtime.usl";
	std::filesystem::create_directories(badRuntime.parent_path());
	{
		std::ofstream file(badRuntime);
		file << "this is not valid USL !!!!";
		REQUIRE(file.good());
	}
	GAGCore::Toolkit::getFileManager()->addDir(overlay.path.string());
	editor.confirm();
	CHECK_FALSE(editor.finished());
	CHECK(editor.compilationText().find("USL runtime") != std::string::npos);
	CHECK(map.getMapScriptMode() == MapScript::JavaScript);
	CHECK(map.getMapScript() == source);
	CHECK(map.checkSum() == checksum);
	CHECK(legacy.checkSum() == legacyChecksum);
	CHECK(legacySnapshot() == retainedLegacy);
	CHECK(world.game.missionBriefing == "Retain briefing on failed preparation");
	CHECK_FALSE(world.game.legacyScriptActive());

	std::filesystem::remove(badRuntime);
	editor.confirm();
	REQUIRE(editor.finished());
	CHECK(map.getMapScriptMode() == MapScript::USL);
	CHECK(map.getMapScript().empty());
	CHECK(map.checkSum() == 0);
	CHECK(legacy.sourceCode == editor.scriptText());
	CHECK(world.game.legacyScriptActive());
	// confirm() destroyed both candidates. Story owner pointers must now refer
	// to the live SGSL object for its presentation, timer and win state writes.
	world.game.scriptSyncStep();
	CHECK(legacy.isTextShown);
	CHECK(legacy.textShown == "Committed legacy");
	CHECK(legacy.getMainTimer() == 3);
	CHECK_FALSE(legacy.hasTeamWon(0)); // SGSL delays victory until its timer expires.
	for (int tick = 0; tick < 3; ++tick)
		world.game.scriptSyncStep();
	CHECK(legacy.getMainTimer() == 0);
	CHECK(legacy.hasTeamWon(0));
}

TEST_CASE("Script language dropdown selects JavaScript on desktop and phone [display][artifacts]" *
		  doctest::test_suite("ScriptEditor"))
{
	glob2test::GlobalsOptions options;
	options.display = true;
	options.loadStrings = true;
	options.width = 800;
	options.height = 600;
	options.screenFlags = GAGCore::GraphicContext::RESIZABLE;
	glob2test::HeadlessGlobals globals(options);
	glob2test::HeadlessGame world;
	auto *gfx = globals->gfx;
	for (const auto &[width, height, touch] :
		 {std::tuple{800, 600, false}, {390, 844, true}, {844, 390, true}})
	{
		GAGCore::setProcessEnvironment("GLOB2_MOBILE_UI", touch ? "1" : "0", 1);
		SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()), width, height);
		SDL_Event resize{};
		resize.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
		GAGCore::GraphicContext::translateMouseEvent(&resize);
		ScriptEditorScreen editor(&world.game);
		editor.attach(*gfx);
		editor.draw(0);
		const auto choice = editor.host().bounds("script/language");
		REQUIRE_FALSE(choice.empty());
		CHECK(editor.host().presentation().dialog.contains(choice));
		editor.host().tapAt(choice.center());
		editor.draw(0);
		REQUIRE(editor.host().popupOpen());
		const auto option = editor.host().bounds("popup/2");
		REQUIRE_FALSE(option.empty());
		editor.host().tapAt(option.center());
		gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), GAGCore::Color(0, 0, 0));
		editor.draw(0);
		CHECK(editor.language() == Language::JavaScript);
		CHECK_FALSE(editor.host().popupOpen());
		CHECK(editor.scriptText().find("function step(ctx)") != std::string::npos);
		gfx->printScreen(glob2test::artifactDirFromWorkingDirectory() + "/javascript-editor-" +
						 std::to_string(width) + "x" + std::to_string(height) + ".bmp");
		gfx->nextFrame();
	}
	GAGCore::setProcessEnvironment("GLOB2_MOBILE_UI", "0", 1);
}
