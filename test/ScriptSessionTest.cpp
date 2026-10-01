// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Engine.h"
#include "script/ScriptRuntime.h"
#include "AIJavaScript.h"
#include "Player.h"

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
	CHECK_FALSE(engine.multiplayer);
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
