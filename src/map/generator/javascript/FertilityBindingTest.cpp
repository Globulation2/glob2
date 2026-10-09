// SPDX-License-Identifier: GPL-3.0-or-later
#include "GeneratorPackage.h"
#include "ToolkitBinding.h"
#include "EngineFixtures.h"
#include <nlohmann/json.hpp>

namespace
{
std::string runFertility(const std::string &body)
{
	using namespace MapGeneration::JavaScript;
	using Json = nlohmann::json;
	try
	{
		auto package = Package::parse(Json{
			{"formatVersion", 1},
			{"manifest",
			 {{"id", "test:fertility"},
			  {"name", "Fertility contract"},
			  {"apiVersion", 1},
			  {"revision", 1},
			  {"tags", {"terrain:natural"}},
			  {"controls", Json::array()}}},
			{"modules",
			 {{"generator.js", "export function generate(c){const F=c.toolkit.FertilityField;" +
								   body +
								   "}"}}}}.dump());
		GenerationRequest request;
		GenerationContext context(request);
		return invoke(*package, "generate", request, nullptr, &context);
	}
	catch (const std::exception &error)
	{
		return error.what();
	}
}
} // namespace

TEST_CASE("Fertility binding rejects incomplete fields and mismatched masks" *
		  doctest::test_suite("ScriptGenerator"))
{
	CHECK(runFertility("F.Field().at(0,0);").find("rebuilt") != std::string::npos);
	CHECK(runFertility("F.Field().rebuild(64,64,[],[]);").find("width * height") !=
		  std::string::npos);
	CHECK(runFertility("F.Field().rebuildWeighted(64,64,[],[]);").find("width * height") !=
		  std::string::npos);
	const std::string setup = "const f=F.Field(); f.rebuild(2,2,[1,0,0,0],[0,0,0,0]);";
	CHECK(runFertility(setup + "f.gate([]);").find("mask") != std::string::npos);
	CHECK(runFertility(setup + "f.multiplyLocal([]);").find("mask") != std::string::npos);
	CHECK(runFertility("F.Field().rebuild(2,2,[1,0,0,0],[0,0,0,0],99);").find("path") !=
		  std::string::npos);
	CHECK(runFertility(
			  setup + "if(f.values().length!==4 || f.at(-1,-1)<=0)throw Error('invalid field');") ==
		  "");
}

TEST_CASE("Fertility binding reserves native work and memory before rebuilding" *
		  doctest::test_suite("ScriptGenerator"))
{
	glob2test::HeadlessGlobals globals;
	// Zero sources take the cheapest native path. The conservative reservations
	// still bound repeated native calls, including an exception caught by script.
	const auto work = runFertility("const z=c.mask(512*512); const f=F.Field(); try{for(let "
								   "i=0;i<12;i++)f.rebuild(512,512,z,z);}catch(e){}");
	INFO(work);
	CHECK(work.find("budget") != std::string::npos);
	// Narrow rows have proportionally more native padding; this envelope reaches
	// the cumulative allocation budget before exhausting the native work budget.
	const auto memory = runFertility("const z=c.mask(1024); const f=F.Field(); for(let "
									 "i=0;i<1000;i++)f.rebuild(1,1024,z,z);");
	INFO(memory);
	CHECK(memory.find("memory budget") != std::string::npos);
}

TEST_CASE("Toolkit consumers reject empty fertility arguments" *
		  doctest::test_suite("ScriptGenerator"))
{
	glob2test::HeadlessGlobals globals;
	for (const auto *field : {"F.Field()", "{}"})
	{
		const auto mean = runFertility("c.toolkit.Growth.meanFertilityAround(" +
									   std::string(field) + ",c.torus,0,1);");
		INFO(mean);
		CHECK(mean.find("rebuilt") != std::string::npos);
		const auto share =
			runFertility("c.toolkit.Growth.wateredShare(" + std::string(field) + ",[1]);");
		INFO(share);
		CHECK(share.find("rebuilt") != std::string::npos);
	}
}

TEST_CASE("Script control mutations cannot bypass native domain contracts" *
		  doctest::test_suite("ScriptGenerator"))
{
	const std::string control = "const ctl=c.toolkit.Farmland.cropCrossingsControl();";
	for (const auto *mutation :
		 {"ctl.step=0;ctl.values();", "ctl.step=0;ctl.normalize(1);",
		  "ctl.kind=0;ctl.maximum=1000000;ctl.values();", "ctl.allowedValues=[1,0];ctl.values();",
		  "ctl.searchAllowedValues=[3];ctl.searchValues();"})
	{
		const auto error = runFertility(control + mutation);
		INFO(error);
		CHECK(error.find("control") != std::string::npos);
	}
	const auto shift = runFertility(
		"const ctl=c.toolkit.Terrain.heightFieldResourceControls().get(0);ctl.powerOfTwo="
		"true;ctl.maximum=30;ctl.step=1;ctl.defaultValue=0;ctl.searchRange=null;ctl.displayValue("
		"31);");
	INFO(shift);
	CHECK(shift.find("0..30") != std::string::npos);
}

TEST_CASE("Site graphs share distance captures and account repeated graph copies" *
		  doctest::test_suite("ScriptGenerator"))
{
	glob2test::HeadlessGlobals globals;
	const std::string setup =
		"const sites=Array.from({length:8192},(_,i)=>({x:i%64,y:Math.trunc(i/64)}));const "
		"neighbours=Array.from({length:8192},()=>[]);const "
		"g=c.toolkit.GraphMaze.cellGraph(c.torus,sites,neighbours);";
	const auto extracted = runFertility(
		setup + "const callbacks=[];for(let i=0;i<1000;i++)callbacks[i]=g.distance2;for(let "
				"i=0;i<1000;i++)if(callbacks[i](0,1)!==1)throw Error('distance changed');");
	INFO(extracted);
	CHECK(extracted.empty());
	const auto copied =
		runFertility(setup + "for(let i=0;i<500;i++)c.toolkit.GraphMaze.CellGraph(g);");
	INFO(copied);
	CHECK(copied.find("budget") != std::string::npos);
}
