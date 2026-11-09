// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptCorpus.h"
#include "ScriptRuntime.h"
#include <bit>
#include <cfenv>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>

namespace
{
// A corpus row's number; profile 1 returns non-finite results as their names.
double rowNumber(const Script::Value &value)
{
	if (value.kind == Script::Value::Number)
		return value.number;
	REQUIRE(value.kind == Script::Value::String);
	if (value.text == "NaN")
		return std::numeric_limits<double>::quiet_NaN();
	REQUIRE((value.text == "Infinity" || value.text == "-Infinity"));
	return value.text[0] == '-' ? -HUGE_VAL : HUGE_VAL;
}
bool sameNumber(double a, double b)
{
	return std::isnan(a) ? std::isnan(b) : std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
} // namespace

TEST_CASE("JavaScript shared numeric corpus [sim-trace]" * doctest::test_suite("JavaScriptNumbers"))
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
	std::map<std::string, Script::Value> rows;
	for (const auto &batch : results.items)
		for (const auto &row : batch.get("results").items)
		{
			REQUIRE(row.items.size() == 2);
			REQUIRE(rows.emplace(row.items[0].text, row.items[1]).second);
		}
	CHECK(rows.size() == total);
	// The operations IEEE 754 rounds exactly must give the host's own result on
	// every input, whatever the platform: the corpus edges (signed zeros,
	// subnormals, extremes, ties) and the fixed-seed values. negate/i is -x, so it
	// recovers each input x and its partner y = edges[(7i+3) mod n].
	const size_t edges = 55;
	std::vector<double> x(edges);
	for (size_t i = 0; i < edges; ++i)
	{
		const auto negated = rows.find("negate/" + std::to_string(i));
		REQUIRE(negated != rows.end());
		x[i] = -rowNumber(negated->second);
	}
	REQUIRE(!rows.contains("negate/" + std::to_string(edges)));
	unsigned exact = 0;
	const auto expect = [&](const std::string &name, double expected)
	{
		CAPTURE(name);
		const auto row = rows.find(name);
		REQUIRE(row != rows.end());
		const double actual = rowNumber(row->second);
		INFO("actual " << std::hex << std::bit_cast<std::uint64_t>(actual) << " expected "
					   << std::bit_cast<std::uint64_t>(expected));
		CHECK(sameNumber(actual, expected));
		++exact;
	};
	for (size_t i = 0; i < edges; ++i)
	{
		const auto n = std::to_string(i);
		const double a = x[i], b = x[(i * 7 + 3) % edges];
		expect("add/" + n, a + b);
		expect("subtract/" + n, a - b);
		expect("multiply/" + n, a * b);
		expect("divide/" + n, a / b);
		expect("remainder/" + n, std::fmod(a, b));
		expect("sqrt/" + n, std::sqrt(a));
		expect("abs/" + n, std::fabs(a));
		expect("floor/" + n, std::floor(a));
		expect("ceil/" + n, std::ceil(a));
		expect("trunc/" + n, std::trunc(a));
		expect("fround/" + n, double(float(a)));
	}
	CHECK(exact == 11 * edges);
	// Results pinned to their mathematically correct values on every platform.
	expect("hypot/architecture-regression", std::bit_cast<double>(std::uint64_t(0x40018a97b64ae2d6)));
	CHECK(rows.at("hypot/decision").kind == Script::Value::Boolean);
	CHECK(rows.at("hypot/decision").number == 1);
	expect("hypot/no-arguments", 0.0);
	expect("hypot/inf-nan", HUGE_VAL);
	expect("sumPrecise/cancellation", 1.0);
	expect("sumPrecise/negative-zero", -0.0);
	expect("Number/-0", -0.0);
	expect("Number/4.9406564584124654e-324", std::numeric_limits<double>::denorm_min());
	expect("Number/1.7976931348623157e308", std::numeric_limits<double>::max());
	expect("Number/1e309", HUGE_VAL);
	expect("JSON.parse/0.1", 0.1);
	glob2test::script::retain("numeric-profile1", results);
}

TEST_CASE("JavaScript shared saved global number corpus [sim-trace]" *
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
		// The snapshot keeps each edge value: canonical NaN, both infinities as their
		// bits, signed zeros and the extreme finite numbers as numbers.
		const auto &nodes = uninterrupted.state.get("__glob2_globals").get("nodes").items;
		const Script::Value *numbers = nullptr;
		for (const auto &property : nodes.at(0).get("properties").items)
			if (property.items.at(0).text == "numbers")
				numbers = &nodes.at(size_t(property.items.at(1).items.at(1).number));
		REQUIRE(numbers);
		std::vector<std::string> saved;
		for (const auto &property : numbers->get("properties").items)
		{
			const auto &value = property.items.at(1);
			std::ostringstream text;
			if (value.kind == Script::Value::Array)
				text << value.items.at(0).text << ' ' << value.items.at(1).text;
			else
				text << std::hex << std::bit_cast<std::uint64_t>(value.number);
			saved.push_back(property.items.at(0).text + "=" + text.str());
		}
		const std::vector<std::string> edges = {
			"0=number 7ff8000000000000", "1=number 7ff8000000000000", "2=number 7ff8000000000000",
			"3=number 7ff8000000000000", "4=number 7ff8000000000000", "5=number 7ff0000000000000",
			"6=number fff0000000000000", "7=8000000000000000",			"8=0",
			"9=1",						 "10=8000000000000001",		   "11=7fefffffffffffff",
			"length=4028000000000000"};
		CHECK(saved == edges);
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
