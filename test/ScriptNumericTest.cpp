// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptCorpus.h"
#include "script/ScriptRuntime.h"
#include <cfenv>

TEST_CASE("JavaScript shared numeric corpus" * doctest::test_suite("JavaScriptNumbers"))
{
	Script::Host host;
	unsigned seed = 0x12345678;
	host.random = [&]
	{
		seed ^= seed << 13;
		seed ^= seed >> 17;
		seed ^= seed << 5;
		return seed;
	};
	host.query = [](const auto &, const auto &, const auto &) { return Script::Value(); };
	auto source = glob2test::readFile(glob2test::fixture("javascript/numeric-corpus.js"));
	auto results = Script::Value::array();
	unsigned batches = 1, total = 0;
	for (unsigned batch = 0; batch < batches; ++batch)
	{
		CAPTURE(batch);
		host.tick = batch;
		auto runtime = Script::makeRuntime();
		runtime->invoke(source, Script::Value::object(), false, host);
		auto data = runtime->inspectGlobals();
		batches = data.integer("batchCount", 1, 100);
		total += data.get("results").items.size();

		results.items.push_back(std::move(data));
	}
	REQUIRE(total > 2000);
	glob2test::script::retain("numeric-profile1", results);
}

TEST_CASE("JavaScript shared saved global number corpus" *
		  doctest::test_suite("JavaScriptNumbers"))
{
	Script::Host host;
	const auto source =
		glob2test::readFile(glob2test::fixture("javascript/global-number-corpus.js"));
	auto continuous = Script::makeRuntime();
	auto checkpoint = Script::Value::object();
	auto snapshots = Script::Value::array();
	for (unsigned tick = 0; tick < 4; ++tick)
	{
		CAPTURE(tick);
		host.tick = tick;
		auto uninterrupted = continuous->invoke(source, checkpoint, tick == 0, host);
		auto reloaded = Script::makeRuntime()->invoke(
			source, Script::Value::decode(checkpoint.encode()), tick == 0, host);
		CHECK(uninterrupted.state.encode() == reloaded.state.encode());
		CHECK(uninterrupted.effects.encode() == reloaded.effects.encode());
		CHECK(uninterrupted.effects.get("calls").number == tick + 1);
		CHECK(uninterrupted.effects.get("nan").number == 1);
		CHECK(uninterrupted.effects.get("infinities").number == 1);
		CHECK(uninterrupted.effects.get("zeros").number == 1);
		checkpoint = std::move(uninterrupted.state);
		snapshots.items.push_back(checkpoint);
	}
	// Retain the actual serialized state, not a lossy diagnostic view of NaNs.
	glob2test::script::retain("global-numbers-profile1", snapshots);
}

TEST_CASE("JavaScript caught host resource failures remain fatal" *
		  doctest::test_suite("JavaScriptTransactions"))
{
	Script::Host host;
	host.random = [] { return 0u; };
	const auto source =
		"export function "
		"step(c,s){s.changed=true;try{c.game.teams();}catch(e){s.caught=true;}return null;}";
	auto state = Script::Value::object().set("original", true);
	auto encoded = state.encode();
	for (bool allocation : {false, true})
	{
		CAPTURE(allocation);
		host.query = [&](const auto &, const auto &, const auto &) -> Script::Value
		{
			if (allocation)
				throw std::bad_alloc();
			throw Script::HostFailure("injected host failure");
		};
		CHECK_THROWS_AS(Script::makeRuntime()->invoke(source, state, false, host),
						Script::HostFailure);
		CHECK(state.encode() == encoded);
	}
}

TEST_CASE("JavaScript initialization rejects unsupported rounding mode" *
		  doctest::test_suite("JavaScriptTransactions"))
{
#ifdef __EMSCRIPTEN__
	// WebAssembly has a fixed nearest-even arithmetic mode; changing it is not
	// an available host operation. Test the supported mode explicitly.
	CHECK(std::fegetround() == FE_TONEAREST);
	CHECK_NOTHROW(Script::makeRuntime()->validate("export function step(){}"));
#else
	const int original = std::fegetround();
	struct Restore
	{
		int mode;
		~Restore() { std::fesetround(mode); }
	} restore{original};
	REQUIRE(std::fesetround(FE_DOWNWARD) == 0);
	CHECK_THROWS_AS(Script::makeRuntime()->validate("export function step(){}"),
					Script::HostFailure);
#endif
}
