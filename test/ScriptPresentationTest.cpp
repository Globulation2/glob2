// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "BinaryStream.h"
#include "StreamBackend.h"

struct ScriptPresentationFixture
{
	static const std::string &text(const GameGUI &gui) { return gui.scriptText; }
	static bool pending(const GameGUI &gui) { return gui.scriptTextUpdated; }
	static void acknowledge(GameGUI &gui) { gui.scriptTextUpdated = false; }
	static unsigned hidden(const GameGUI &gui) { return gui.hiddenGUIElements; }
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
	script.step(source, world.gui);
	CHECK(ScriptPresentationFixture::text(world.gui) == "Francais");
	CHECK(ScriptPresentationFixture::pending(world.gui));
	ScriptPresentationFixture::acknowledge(world.gui);
	script.step(source, world.gui);
	CHECK_FALSE(ScriptPresentationFixture::pending(world.gui));
	globals->settings.language = "en";
	script.step(source, world.gui);
	CHECK(ScriptPresentationFixture::text(world.gui) == "English");
	CHECK(ScriptPresentationFixture::pending(world.gui));
	script.step("export function step(c,s){return [{type:'hideMessage'}];}", world.gui);
	CHECK(ScriptPresentationFixture::text(world.gui).empty());
	CHECK_FALSE(ScriptPresentationFixture::pending(world.gui));
	script.step(source, world.gui);
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
