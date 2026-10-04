// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "hive/HiveWorker.h"
#include <fstream>
#include <sstream>
using Hive::Json;
namespace
{
Json snapshot()
{
	return {
		{"team", 0},
		{"tick", 50},
		{"width", 2},
		{"height", 1},
		{"teams", Json::array({{{"id", 0}, {"alive", true}, {"resources", Json::array({2, 3})}},
							   {{"id", 1}, {"alive", true}}})},
		{"units",
		 Json::array({{{"id", 1}, {"generation", 2}, {"team", 0}, {"type", 0}, {"medical", 1}}})},
		{"buildings", Json::array()},
		{"buildingTypes", Json::array()},
		{"tiles", Json::array({{{"x", 0}, {"y", 0}, {"visible", true}, {"explored", true}},
							   {{"x", 1}, {"y", 0}, {"visible", false}, {"explored", false}}})}};
}
Json run(std::string source, Json state = nullptr, bool initialized = false)
{
	return Hive::invoke({{"source", source},
						 {"snapshot", snapshot()},
						 {"state", state},
						 {"initialized", initialized}});
}
} // namespace
TEST_CASE("Hive Mind exposes only copied player observations" * doctest::test_suite("HiveMind"))
{
	auto r = run(
		R"(function step(ctx){ctx.myTeam=1;return {output:{team:ctx.game.teams()[1],hidden:ctx.game.unit({id:9,generation:1}),stale:ctx.game.unit({id:1,generation:1}),units:ctx.game.units({team:1}),tile:ctx.game.map.tile(1,0),scenario:typeof ctx.game.objectives,network:typeof fetch,files:typeof require}};})");
	REQUIRE(r.at("ok") == true);
	auto out = r.at("output");
	CHECK(out.at("team").size() == 2);
	CHECK(out.at("hidden").is_null());
	CHECK(out.at("stale").is_null());
	CHECK(out.at("units").empty());
	CHECK(out.at("tile").size() == 4);
	CHECK(out.at("scenario") == "undefined");
	CHECK(out.at("network") == "undefined");
	CHECK(out.at("files") == "undefined");
}
TEST_CASE("Hive Mind commits globals only through successful results" *
		  doctest::test_suite("HiveMind"))
{
	const std::string source = "let n=0;function step(ctx){n++;return {output:n};}";
	auto first = run(source);
	REQUIRE(first.at("ok") == true);
	CHECK(first.at("output") == 1);
	auto second = run(source, first.at("state"), true);
	REQUIRE(second.at("ok") == true);
	CHECK(second.at("output") == 2);
	auto failed =
		run("let n=0;function step(ctx){n++;throw Error('fail');}", first.at("state"), true);
	CHECK(failed.at("ok") == false);
	CHECK_FALSE(failed.contains("state"));
	CHECK(run(source, first.at("state"), true).at("output") == 2);
}
TEST_CASE("Hive Mind bounds work output batches and wake storms" * doctest::test_suite("HiveMind"))
{
	for (const auto *source :
		 {"function step(){while(true){}}", "function step(){return {output:'x'.repeat(65537)}}",
		  "function step(){return {orders:Array(33).fill({})}}",
		  "function step(ctx){for(let i=0;i<5;i++)ctx.wakeAgent({key:'x',reason:'test'});return "
		  "{}}",
		  "function step(){return {output:NaN}}",
		  "function step(ctx){return {output:ctx.game.objectives()}}"})
	{
		CAPTURE(source);
		CHECK(run(source).at("ok") == false);
	}
}
TEST_CASE("Hive Mind examples execute against the contract" * doctest::test_suite("HiveMind"))
{
	for (const auto *name : {"report", "food-watch", "production"})
	{
		std::ifstream file(glob2test::sourceRoot() /
						   (std::string("examples/hive-mind/") + name + ".js"));
		REQUIRE(file.good());
		std::stringstream source;
		source << file.rdbuf();
		auto result = run(source.str());
		CAPTURE(name);
		CAPTURE(result.dump());
		REQUIRE(result.at("ok") == true);
		if (std::string(name) == "food-watch")
		{
			CHECK(result.at("wakes").size() == 1);
			CHECK(run(source.str(), result.at("state"), true).at("wakes").empty());
		}
	}
}
TEST_CASE("Hive Mind replacement migration is preflighted and cannot emit effects" *
		  doctest::test_suite("HiveMind"))
{
	auto input = Json{{"source", "function step(){return {output:1}}"},
					  {"snapshot", snapshot()},
					  {"previousState", nullptr},
					  {"migration", "function step(){return {output:null}}"}};
	CHECK(Hive::invoke(input).at("ok") == true);
	input["migration"] = "function step(){return {orders:[{type:'delete'}]}}";
	CHECK(Hive::invoke(input).at("ok") == false);
}
