// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "hive/HiveWorker.h"
#include "ProgramSchedule.h"
#include <map>
#include <nlohmann/json.hpp>
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
		{"terrainTypes", Json::array({{{"id", 0}, {"name", "grass"}, {"buildable", true}}})},
		{"resourceTypes", Json::array({{{"id", 0}, {"key", "wood"}, {"name", "Wood"}}})},
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
TEST_CASE("Hive Mind reads the match's terrain and resource rule tables" * doctest::test_suite("HiveMind"))
{
	// What a tile's corners and resource type mean: public rules, not map contents.
	auto r = run(R"(function step(ctx){return {output:{terrain:ctx.game.terrainTypes()[0],resource:ctx.game.resourceTypes()[0].key}};})");
	REQUIRE(r.at("ok") == true);
	CHECK(r.at("output").at("terrain").at("buildable") == true);
	CHECK(r.at("output").at("resource") == "wood");
}
TEST_CASE("Hive Mind helpers: names, filters, placement, scans, events and wrap" * doctest::test_suite("HiveMind"))
{
	// A 6x4 map: grass everywhere (buildable), wheat at (1,0), water corners at (5,*),
	// the colony's 2x2 Inn site at (2,2), an enemy warrior and the colony's worker.
	Json tiles = Json::array();
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 6; ++x)
		{
			const int terrain = x == 5 ? 1 : 0;
			tiles.push_back({{"x", x}, {"y", y}, {"visible", true}, {"explored", true}, {"observedTick", 1},
							 {"corners", {terrain, terrain, terrain, terrain}},
							 {"resource", {{"type", x == 1 && y == 0 ? 0 : 65535}, {"variety", 0}, {"amount", x == 1 && y == 0 ? 9 : 0}}},
							 {"groundUnit", 65535}, {"airUnit", 65535}, {"building", 65535}});
		}
	Json snapshot = {
		{"team", 0}, {"tick", 500}, {"width", 6}, {"height", 4},
		{"teams", Json::array({{{"id", 0}, {"alive", true}, {"allies", 1}}, {{"id", 1}, {"alive", true}}})},
		{"units", Json::array({{{"id", 1}, {"generation", 1}, {"team", 0}, {"type", 0}, {"x", 0}, {"y", 0}},
							   {{"id", 2}, {"generation", 1}, {"team", 1}, {"type", 2}, {"x", 4}, {"y", 3}}})},
		{"buildings", Json::array({{{"id", 7}, {"generation", 1}, {"team", 0}, {"type", 1}, {"shortType", 1}, {"x", 2}, {"y", 2}, {"assigned", 1}}})},
		{"buildingTypes", Json::array({{{"id", 0}, {"name", "inn"}, {"level", 0}, {"site", false}, {"virtual", false}, {"width", 2}, {"height", 2}},
									   {{"id", 1}, {"name", "inn"}, {"level", 0}, {"site", true}, {"virtual", false}, {"width", 2}, {"height", 2}},
									   {{"id", 2}, {"name", "warflag"}, {"level", 0}, {"site", false}, {"virtual", true}, {"width", 1}, {"height", 1}}})},
		{"terrainTypes", Json::array({{{"id", 0}, {"name", "grass"}, {"buildable", true}}, {{"id", 1}, {"name", "water"}, {"buildable", false}}})},
		{"resourceTypes", Json::array({{{"id", 0}, {"key", "wheat"}, {"name", "Wheat"}}})},
		{"events", Json::array({{{"kind", "unit_attacked"}, {"x", 4}, {"y", 3}, {"count", 1}, {"tick", 400}},
								{{"kind", "building_completed"}, {"x", 2}, {"y", 2}, {"count", 1}, {"tick", 490}}})},
		{"tiles", tiles}};
	const auto r = Hive::invoke({{"source", R"(function step(ctx){
		const g=ctx.game, m=g.map;
		return {output:{
			mine:g.units({team:'mine'}).length, enemies:g.units({team:'enemies'}).map(u=>u.id),
			warriors:g.units({type:'warrior'}).length, inns:g.buildings({type:'inn'}).length,
			assigned:g.buildings({team:'mine'})[0].assigned,
			innType:g.buildingType('inn').id, flagType:g.buildingType('warflag').id,
			events:g.events(450).map(e=>e.kind),
			wheat:m.tile(1,0).resource.key,
			scan:m.scan(0,0,6,1,['resource','buildable','terrain']),
			free:m.canPlace('inn',0,1).ok, blocked:m.canPlace('inn',4,0).ok, onSite:m.canPlace('inn',1,1).ok,
			why:[m.canPlace('inn',4,0).reason, m.canPlace('inn',1,1).reason, m.canPlace('inn',0,0).reason],
			names:[g.units({team:'mine'})[0].typeName===undefined, g.buildingType('inn',{level:3})],
			spot:m.findPlacement('inn',{x:3,y:1,radius:3}),
			delta:m.delta({x:5,y:0},{x:0,y:3}), distance:m.distance(0,0,5,0)}};})"},
		{"snapshot", snapshot}, {"state", nullptr}, {"initialized", false}});
	INFO(r.dump());
	REQUIRE(r.at("ok") == true);
	const auto out = r.at("output");
	CHECK(out.at("mine") == 1);
	CHECK(out.at("enemies") == Json::array({2}));
	CHECK(out.at("warriors") == 1);
	CHECK(out.at("inns") == 1);
	CHECK(out.at("assigned") == 1);
	CHECK(out.at("innType") == 1); // the construction site a click places
	CHECK(out.at("flagType") == 2);
	CHECK(out.at("events") == Json::array({"building_completed"}));
	CHECK(out.at("wheat") == "wheat");
	CHECK(out.at("scan").at("resource") == Json::array({65535, 0, 65535, 65535, 65535, 65535}));
	CHECK(out.at("scan").at("buildable") == Json::array({1, 0, 1, 1, 1, 0}));
	CHECK(out.at("scan").at("terrain") == Json::array({0, 0, 0, 0, 0, 1}));
	CHECK(out.at("free") == true);
	CHECK(out.at("blocked") == false); // its right column is water
	CHECK(out.at("onSite") == false);	 // overlaps the colony's Inn site
	// The reason names the tile and what is in the way.
	CHECK(out.at("why") == Json{"tile (5, 0): unbuildable terrain (water)", "tile (2, 2): one of your buildings",
								 "tile (1, 0): a resource (wheat)"});
	// A known name without that level is null (an unknown name throws, below).
	CHECK(out.at("names")[1].is_null());
	CHECK(out.at("spot").at("x") == 2); // nearest ring, scanned top-left first
	CHECK(out.at("spot").at("y") == 0);
	// The map wraps: from x=5 to x=0 is one step right; from y=0 to y=3 one step up.
	CHECK(out.at("delta").at("dx") == 1);
	CHECK(out.at("delta").at("dy") == -1);
	CHECK(out.at("distance") == 1);
}
TEST_CASE("Hive Mind runs get a larger work budget than simulation scripts" * doctest::test_suite("HiveMind"))
{
	// About 90,000 loop iterations fit the simulation scripts' budget; colony scripts run
	// outside the simulation and get room for whole-map scans.
	auto r = run("function step(ctx){let s=0;for(let i=0;i<2000000;i++)s+=i;return {output:s>0};}");
	REQUIRE(r.at("ok") == true);
	CHECK(r.at("output") == true);
	auto over = run("function step(ctx){let s=0;for(let i=0;i<100000000;i++)s+=i;return {output:s};}");
	CHECK(over.at("ok") == false);
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

TEST_CASE("Hive Mind serves due programs fairly after slow workers without catch-up bursts" *
		  doctest::test_suite("HiveMind"))
{
	struct Program
	{
		bool paused = false;
		unsigned nextTick = 0;
		Hive::Json definition = {{"intervalTicks", 25}};
	};
	std::map<std::string, Program> programs;
	programs["a"] = {};
	programs["b"] = {};
	programs["c"] = {};
	CHECK(Hive::scheduleDue(programs, 0) == "a");
	CHECK(Hive::scheduleDue(programs, 50) == "b");
	CHECK(Hive::scheduleDue(programs, 100) == "c");
	CHECK(Hive::scheduleDue(programs, 150) == "a");
	CHECK(programs.at("a").nextTick == 175);
	programs.at("b").paused = true;
	CHECK(Hive::scheduleDue(programs, 150) == "c");
	CHECK(Hive::scheduleDue(programs, 150).empty());
	CHECK(Hive::scheduleDue(programs, 175) == "a");
}
TEST_CASE("Hive Mind results publish integral script numbers as JSON integers" *
		  doctest::test_suite("HiveMind"))
{
	const std::string source = "let n=0;function step(ctx){n++;return {output:{tick:ctx.tick,n,"
							   "half:0.5,big:2**60,ids:[1,2]}};}";
	auto r = run(source);
	REQUIRE(r.at("ok") == true);
	const auto &output = r.at("output");
	// Agents read these records: an ID or tick must not come back as 50.0.
	CHECK(output.at("tick").is_number_integer());
	CHECK(output.at("ids").dump() == "[1,2]");
	CHECK(output.at("half") == 0.5);
	// Beyond 2^53 a JavaScript number is not an exact integer; keep it a double.
	CHECK(output.at("big").is_number_float());
	// Checkpoints round-trip through the same conversion.
	CHECK(r.at("state").dump().find(".0") == std::string::npos);
	CHECK(run(source, r.at("state"), true).at("output").at("n") == 2);
}
TEST_CASE("Hive Mind explains which invocation limit a result exceeded" *
		  doctest::test_suite("HiveMind"))
{
	const auto diagnostic = [](const char *source)
	{
		const auto r = run(source);
		CHECK(r.at("ok") == false);
		return r.value("diagnostic", std::string());
	};
	CHECK(diagnostic("function step(){return {orders:Array(33).fill(null)}}") ==
		  "Return at most 32 orders per invocation (returned 33)");
	CHECK(diagnostic("function step(){return {output:'x'.repeat(65537)}}") ==
		  "Returned output and orders exceed 64 KiB");
	CHECK(diagnostic("function step(){return {orders:{type:'delete'}}}") ==
		  "orders must be an array of order descriptors");
}
TEST_CASE("Hive Mind helper argument errors name the argument and its range" *
		  doctest::test_suite("HiveMind"))
{
	const auto failure = [](const char *source)
	{
		const auto r = run(source);
		CHECK(r.at("ok") == false);
		return r.dump();
	};
	// Not "Integer outside range": which argument, its allowed range and what was passed.
	CHECK(failure("function step(ctx){ctx.game.map.scan(0,0,300,1);return {};}")
			  .find("scan width must be a whole number 1..256 (got 300") != std::string::npos);
	CHECK(failure("function step(ctx){ctx.game.map.scan(0,'a',2,1);return {};}")
			  .find("scan y must be a whole number -32768..32767 (got \\\"a\\\")") != std::string::npos);
}
TEST_CASE("Hive Mind refuses unknown names and filter keys, listing the valid ones" *
		  doctest::test_suite("HiveMind"))
{
	Json types = Json::array({{{"id", 0}, {"name", "inn"}, {"level", 0}, {"site", true}, {"virtual", false}, {"width", 2}, {"height", 2}},
							  {{"id", 1}, {"name", "warflag"}, {"level", 0}, {"site", false}, {"virtual", true}, {"width", 1}, {"height", 1}}});
	auto base = snapshot();
	base["buildingTypes"] = types;
	const auto failure = [&](const char *source)
	{
		const auto r = Hive::invoke({{"source", source}, {"snapshot", base}, {"state", nullptr}, {"initialized", false}});
		CHECK(r.at("ok") == false);
		return r.value("diagnostic", std::string());
	};
	CHECK(failure("function step(ctx){return {output:ctx.game.buildingType('tower')};}")
			  .find("Unknown building or flag \"tower\"; names: inn, warflag") != std::string::npos);
	CHECK(failure("function step(ctx){return {output:ctx.game.map.findPlacement('castle',{x:0,y:0})};}")
			  .find("names: inn, warflag") != std::string::npos);
	CHECK(failure("function step(ctx){return {output:ctx.game.units({team:1, kind:'warrior'})};}")
			  .find("units filter has an unknown key \"kind\"; valid keys: team, type, offset, limit") != std::string::npos);
	CHECK(failure("function step(ctx){return {output:ctx.game.buildings({remembered:1})};}")
			  .find("remembered must be true or false") != std::string::npos);
}
