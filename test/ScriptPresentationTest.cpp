// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "WinningConditions.h"
#include <FileManager.h>
#include <Toolkit.h>
#include <fstream>

struct ScriptPresentationFixture
{
	static const std::string &text(const GameGUI &gui) { return gui.scriptText; }
	static bool pending(const GameGUI &gui) { return gui.scriptTextUpdated; }
	static void acknowledge(GameGUI &gui) { gui.scriptTextUpdated = false; }
	static unsigned hidden(const GameGUI &gui) { return gui.hiddenGUIElements; }
	static bool swallowingSpace(const GameGUI &gui) { return gui.swallowSpaceKey; }
};

TEST_CASE("JavaScript presentation publishes only changed localized messages" *
		  doctest::test_suite("JavaScriptPresentation"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	globals->settings.language = "fr";
	glob2test::HeadlessGame world;
	Script::JavaScriptMap script;
	const std::string source =
		R"(export function step(c,s){return [{type:'message',text:'English'}, {type:'messageTranslated',language:'fr',text:'Francais'}];})";
	script.step(source, world.game, world.gui);
	CHECK(ScriptPresentationFixture::text(world.gui) == "Francais");
	CHECK(ScriptPresentationFixture::pending(world.gui));
	ScriptPresentationFixture::acknowledge(world.gui);
	script.step(source, world.game, world.gui);
	CHECK_FALSE(ScriptPresentationFixture::pending(world.gui));
	globals->settings.language = "en";
	script.step(source, world.game, world.gui);
	CHECK(ScriptPresentationFixture::text(world.gui) == "English");
	CHECK(ScriptPresentationFixture::pending(world.gui));
	script.step("export function step(c,s){return [{type:'hideMessage'}];}", world.game, world.gui);
	CHECK(ScriptPresentationFixture::text(world.gui).empty());
	CHECK_FALSE(ScriptPresentationFixture::pending(world.gui));
	script.step(source, world.game, world.gui);
	CHECK(ScriptPresentationFixture::pending(world.gui));
	world.gui.init();
	CHECK(ScriptPresentationFixture::text(world.gui).empty());
}

TEST_CASE("JavaScript load restores presentation without a callback or history entry" *
		  doctest::test_suite("JavaScriptPresentation"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	glob2test::HeadlessGame world;
	auto &script = world.game.mapscript;
	script.setMapScriptMode(MapScript::JavaScript);
	script.setMapScript(
		R"(export function step(c,s){s.calls=(s.calls??0)+1;return [{type:'message',text:'Saved'}, {type:'buildingChoice',name:'swarm',enabled:false}, {type:'flagChoice',name:'warflag',enabled:false}, {type:'guiElement',id:1,enabled:false}];})");
	REQUIRE(script.compileCode());
	script.syncStep(&world.gui);
	auto checksum = script.checkSum();
	auto *memory = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream output(memory);
	world.gui.save(&output, "script presentation");
	output.flush();
	auto bytes = memory->takeContents();
	GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
	input.seekFromStart(0);
	GameGUI loaded;
	REQUIRE(loaded.load(&input));
	CHECK(loaded.game.mapscript.checkSum() == checksum);
	CHECK(ScriptPresentationFixture::text(loaded) == "Saved");
	CHECK_FALSE(ScriptPresentationFixture::pending(loaded));
	CHECK_FALSE(loaded.isBuildingEnabled("swarm"));
	CHECK_FALSE(loaded.isFlagEnabled("warflag"));
	CHECK((ScriptPresentationFixture::hidden(loaded) & 2) == 2);
	loaded.game.mapscript.syncStep(&loaded);
	CHECK_FALSE(ScriptPresentationFixture::pending(loaded));
	CHECK(loaded.game.mapscript.checkSum() != checksum);
}

TEST_CASE("Leaving a JavaScript scenario resets choices before loading a legacy map" *
		  doctest::test_suite("JavaScriptPresentation"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	glob2test::HeadlessGame world;
	world.gui.init();
	auto &mapScript = world.game.mapscript;
	mapScript.setMapScriptMode(MapScript::JavaScript);
	mapScript.setMapScript(
		R"(function step(ctx) { return [{type:'message',text:'Previous scenario'}, {type:'buildingChoice',name:'swarm',enabled:false}, {type:'flagChoice',name:'warflag',enabled:false}, {type:'guiElement',id:1,enabled:false}]; })");
	REQUIRE(mapScript.compileCode());
	mapScript.syncStep(&world.gui);
	REQUIRE_FALSE(world.gui.isBuildingEnabled("swarm"));
	REQUIRE_FALSE(world.gui.isFlagEnabled("warflag"));
	REQUIRE(ScriptPresentationFixture::hidden(world.gui) != 0);

	GAGCore::BinaryInputStream input(glob2OpenMapOrSaveInputStreamBackend(
		*GAGCore::Toolkit::getFileManager(), "maps/Sand_River.map.gz"));
	REQUIRE(world.gui.load(&input));
	CHECK(world.game.mapscript.getMapScriptMode() == MapScript::USL);
	CHECK(world.gui.isBuildingEnabled("swarm"));
	CHECK(world.gui.isFlagEnabled("warflag"));
	CHECK(ScriptPresentationFixture::hidden(world.gui) == 0);
	CHECK(ScriptPresentationFixture::text(world.gui).empty());
	CHECK_FALSE(ScriptPresentationFixture::pending(world.gui));
}

TEST_CASE("JavaScript leaves stored SGSL execution and victory conditions dormant" *
		  doctest::test_suite("JavaScriptPresentation"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	glob2test::HeadlessGame world;
	world.gui.init();
	auto &legacy = world.game.sgslScript;
	legacy.sourceCode = R"(show("Legacy scenario") win(0) loose(0) space)";
	REQUIRE(legacy.compileScript(&world.game).type == ErrorReport::ET_OK);
	world.game.scriptSyncStep();
	REQUIRE(legacy.isTextShown);
	REQUIRE(ScriptPresentationFixture::swallowingSpace(world.gui));
	WinningConditionScript victory;
	REQUIRE(victory.hasTeamWon(0, &world.game));
	REQUIRE(victory.hasTeamLost(0, &world.game));
	auto serializedLegacy = [&] {
		auto *storage = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(storage);
		legacy.save(&output, &world.game);
		return storage->takeContents();
	};
	const auto retained = serializedLegacy();
	world.gui.setIsSpaceSet(true);
	auto &mapScript = world.game.mapscript;
	mapScript.setMapScriptMode(MapScript::JavaScript);
	mapScript.setMapScript(
		R"(let calls=0; function step(ctx) { calls++; return [{type:'message',text:'JavaScript scenario'}]; })");
	REQUIRE(mapScript.compileCode());
	CHECK_FALSE(world.game.legacyScriptActive());
	CHECK_FALSE(ScriptPresentationFixture::swallowingSpace(world.gui));
	CHECK_FALSE(world.gui.isSpaceSet());
	CHECK_FALSE(victory.hasTeamWon(0, &world.game));
	CHECK_FALSE(victory.hasTeamLost(0, &world.game));
	for (unsigned i = 0; i < 3; ++i)
		world.game.scriptSyncStep();
	CHECK(ScriptPresentationFixture::text(world.gui) == "JavaScript scenario");
	CHECK(serializedLegacy() == retained);

	mapScript.reset();
	CHECK(world.game.legacyScriptActive());
	CHECK(victory.hasTeamWon(0, &world.game));
	CHECK(victory.hasTeamLost(0, &world.game));
	CHECK(legacy.sourceCode == R"(show("Legacy scenario") win(0) loose(0) space)");
}

TEST_CASE("JavaScript does not advance or display a retained SGSL timer" *
		  doctest::test_suite("JavaScriptPresentation"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	glob2test::HeadlessGame world;
	world.gui.init();
	auto &legacy = world.game.sgslScript;
	REQUIRE(legacy.compileScript(&world.game, "timer(9) space").type == ErrorReport::ET_OK);
	world.game.scriptSyncStep();
	REQUIRE(world.game.legacyScriptTimer() == 9);
	auto &mapScript = world.game.mapscript;
	mapScript.setMapScriptMode(MapScript::JavaScript);
	mapScript.setMapScript("function step(ctx) { return []; }");
	REQUIRE(mapScript.compileCode());
	world.game.scriptSyncStep();
	CHECK(world.game.legacyScriptTimer() == 0);
	CHECK(legacy.getMainTimer() == 9);
	mapScript.reset();
	world.game.scriptSyncStep();
	CHECK(world.game.legacyScriptTimer() == 8);
}


TEST_CASE("Failed prepared map replacements preserve active globals and language" *
		  doctest::test_suite("JavaScriptPresentation"))
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world;
	auto &map = world.game.mapscript;
	map.setMapScriptMode(MapScript::JavaScript);
	map.setMapScript("let calls=0; function step(ctx) { calls++; return []; }");
	REQUIRE(map.compileCode());
	map.syncStep(&world.gui);
	const auto source = map.getMapScript();
	const auto checksum = map.checkSum();
	MapScriptError error;
	glob2test::TempDir overlay("broken-usl-preparation");
	const auto badRuntime = overlay.path / "data/usl/Language/Runtime/invalid-prepared-runtime.usl";
	std::filesystem::create_directories(badRuntime.parent_path());
	{
		std::ofstream file(badRuntime);
		file << "this is not valid USL !!!!";
		REQUIRE(file.good());
	}
	GAGCore::Toolkit::getFileManager()->addDir(overlay.path.string());
	CHECK_FALSE(map.prepareSource(MapScript::USL, "", error));
	CHECK(error.getMessage().find("USL runtime") != std::string::npos);
	CHECK(map.getMapScriptMode() == MapScript::JavaScript);
	CHECK(map.getMapScript() == source);
	CHECK(map.checkSum() == checksum);
	std::filesystem::remove(badRuntime);
	CHECK_FALSE(map.replaceSource(MapScript::USL, "this is not valid USL !!!!", error));
	CHECK(map.getMapScriptMode() == MapScript::JavaScript);
	CHECK(map.getMapScript() == source);
	CHECK(map.checkSum() == checksum);
	CHECK_FALSE(map.replaceSource(MapScript::JavaScript, "function step( {", error));
	CHECK(map.getMapScriptMode() == MapScript::JavaScript);
	CHECK(map.getMapScript() == source);
	CHECK(map.checkSum() == checksum);
	REQUIRE(map.replaceSource(MapScript::USL, "", error));
	CHECK(map.getMapScriptMode() == MapScript::USL);
	CHECK(map.getMapScript().empty());
	world.gui.setIsSpaceSet(true);
	CHECK_FALSE(map.replaceSource(MapScript::JavaScript, "function step( {", error));
	CHECK(map.getMapScriptMode() == MapScript::USL);
	CHECK(map.getMapScript().empty());
	CHECK(world.gui.isSpaceSet());
	REQUIRE(map.replaceSource(MapScript::JavaScript, source, error));
	CHECK_FALSE(world.gui.isSpaceSet());
}

TEST_CASE("Prepared SGSL exchanges rebind stories in both runtimes" *
		  doctest::test_suite("JavaScriptPresentation"))
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world;
	MapScriptSGSL first;
	MapScriptSGSL second;
	first.sourceCode = R"(show("First") timer(7) win(0) space)";
	second.sourceCode = R"(show("Second") timer(4) loose(0) space)";
	REQUIRE(first.compileScript(&world.game).type == ErrorReport::ET_OK);
	REQUIRE(second.compileScript(&world.game).type == ErrorReport::ET_OK);
	first.swap(second);
	first.syncStep(world.game, world.gui, world.gui.clientRequests);
	CHECK(first.textShown == "Second");
	CHECK(first.getMainTimer() == 4);
	CHECK(first.hasTeamLost(0));
	CHECK_FALSE(first.hasTeamWon(0));
	CHECK_FALSE(second.isTextShown);
	CHECK(second.getMainTimer() == 0);
	second.syncStep(world.game, world.gui, world.gui.clientRequests);
	CHECK(second.textShown == "First");
	CHECK(second.getMainTimer() == 7);
	CHECK_FALSE(second.hasTeamWon(0));
	for (int tick = 0; tick < 7; ++tick)
		second.syncStep(world.game, world.gui, world.gui.clientRequests);
	CHECK(second.getMainTimer() == 0);
	CHECK(second.hasTeamWon(0));
	CHECK_FALSE(second.hasTeamLost(0));
	CHECK(first.getMainTimer() == 4);
	CHECK(first.textShown == "Second");
}
