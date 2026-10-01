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
		auto result = Script::makeRuntime()->invoke(
			source, Script::Value::object().set("batch", batch), false, host);
		batches = result.state.integer("batchCount", 1, 100);
		total += result.state.get("results").items.size();
		CHECK(result.effects.kind == Script::Value::Null);
		results.items.push_back(std::move(result.state));
	}
	REQUIRE(total > 2000);
	glob2test::script::retain("numeric-profile1", results);
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
