// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Engine.h"
#include "ScriptRuntime.h"
#include "AIJavaScript.h"
#include "Player.h"
#include "GameLoadScreen.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>

TEST_CASE("JavaScript fatal failure tears down a live engine session" *
		  doctest::test_suite("JavaScriptSession"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	Engine engine;
	auto initial = glob2test::inflated("test/fixtures/javascript/profile1-initial.game.gz");
	REQUIRE(engine.initCustom(initial.string()) == Engine::EE_NO_ERROR);
	engine.beginSession(0);
	const auto tick = engine.gui.game.stepCounter;
	struct FailingRuntime : Script::Runtime
	{
		void validate(const std::string &) override {}
		Script::Result invoke(const std::string &, const Script::Value &, bool,
							  Script::Host &) override
		{
			throw Script::HostFailure("injected fatal resource failure during AI callback");
		}
	};
	auto *controller =
		static_cast<AIJavaScript *>(engine.gui.game.players[0]->ai->aiImplementation);
	controller->runtime = std::make_unique<FailingRuntime>();
	CHECK_THROWS_AS(engine.stepSession(40, {}), Script::HostFailure);
	CHECK_FALSE(engine.gui.isRunning);
	CHECK_FALSE(engine.net);
	CHECK_FALSE(engine.session);
	CHECK(engine.gui.game.stepCounter == tick);
	CHECK_THROWS_AS(engine.stepSession(80, {}), std::logic_error);
}

TEST_CASE("JavaScript deterministic scenario failure ends the engine session" *
		  doctest::test_suite("JavaScriptSession"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	globals->structuredHeadless = true;
	Engine engine;
	auto initial = glob2test::inflated("test/fixtures/javascript/profile1-initial.game.gz");
	REQUIRE(engine.initCustom(initial.string()) == Engine::EE_NO_ERROR);
	auto &script = engine.gui.game.mapscript;
	script.setMapScript("export function step(c,s){s.bad=true;return [{type:'unknown'}];}");
	REQUIRE(script.compileCode());
	engine.beginSession(0);
	bool stopped = false;
	for (unsigned tick = 1; tick <= 40 && !stopped; ++tick)
	{
		try
		{
			engine.stepSession(tick * 40, {});
		}
		catch (const Script::ScenarioFailure &)
		{
			stopped = true;
		}
	}
	REQUIRE(stopped);
	CHECK_FALSE(engine.gui.isRunning);
	CHECK_FALSE(engine.net);
	CHECK_FALSE(engine.session);
}

TEST_CASE("JavaScript initialization errors reach headless and UI load diagnostics" *
		  doctest::test_suite("JavaScriptSession"))
{
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	const auto initial = glob2test::inflated("test/fixtures/javascript/profile1-initial.game.gz");
	const auto invalid = glob2test::artifactDir() / "invalid-embedded-ai.game";
	{
		GameGUI original;
		GAGCore::BinaryInputStream input(glob2OpenMapOrSaveInputStreamBackend(
			*GAGCore::Toolkit::getFileManager(), initial.string()));
		REQUIRE(original.load(&input));
		original.game.gameHeader.setAIConfig(0, Script::config("function step( {"));
		GAGCore::BinaryOutputStream output(
			GAGCore::Toolkit::getFileManager()->openOutputStreamBackend(invalid.string()));
		REQUIRE(output.isValid());
		original.save(&output, "Invalid embedded AI");
	}
	Engine engine;
	CHECK(engine.initCustom(invalid.string()) == Engine::EE_CANT_LOAD_MAP);
	CHECK(engine.getInitializationDiagnostic().find("SyntaxError") != std::string::npos);
	CHECK_FALSE(engine.net);
	GameLoadScreen loading([filename = invalid.string()](Engine &next) {
		return next.initCustomTask(filename);
	});
	// Pump the loader without a rendering surface; its failure diagnostic is
	// consumed by the same MessageScreen path on desktop and mobile hosts.
	loading.run = true;
	for (unsigned attempt = 0; attempt < 1000 && loading.isExecutionRunning(); ++attempt)
		loading.onTimer(attempt);
	REQUIRE_FALSE(loading.isExecutionRunning());
	CHECK(loading.finishExecution() == 2);
	CHECK(loading.failureMessage().find("SyntaxError") != std::string::npos);
	CHECK(loading.failureMessage().find("Unexpected token") != std::string::npos);
	// A subsequent ordinary initialization must not retain a previous error.
	CHECK(engine.initCustom(initial.string()) == Engine::EE_NO_ERROR);
	CHECK(engine.getInitializationDiagnostic().empty());
}
