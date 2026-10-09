// SPDX-License-Identifier: GPL-3.0-or-later
#include "ToolkitBinding.h"
#include "ToolkitRecords.h"
#include "GenerationWork.h"
#include "EngineFixtures.h"
#include <nlohmann/json.hpp>

namespace JSGen = MapGeneration::JavaScript;
namespace
{
std::shared_ptr<const JSGen::Package> package(const std::string &body = {})
{
	return JSGen::Package::parse(
		nlohmann::json{{"formatVersion", 1},
					   {"manifest",
						{{"id", "test:binding"},
						 {"name", "Binding test"},
						 {"apiVersion", 1},
						 {"revision", 1},
						 {"tags", {"terrain:natural"}},
						 {"controls", nlohmann::json::array()}}},
					   {"modules", {{"generator.js", "export function generate(c){" + body + "}"}}}}
			.dump());
}
std::string invoke(const std::string &body)
{
	auto p = package(body);
	GenerationRequest request;
	GenerationContext context(request);
	try
	{
		return JSGen::invoke(*p, "generate", request, nullptr, &context);
	}
	catch (const std::exception &error)
	{
		return error.what();
	}
}
} // namespace

TEST_CASE("Repeated point handles share one owned prototype" *
		  doctest::test_suite("ScriptGenerator"))
{
	auto p = package();
	GenerationRequest request;
	GenerationContext context(request);
	JSGen::Binding runtime(*p, request, nullptr, &context, false, false);
	MapGeneratorPoint point(7, 11);
	const MapGeneratorPoint readonly(13, 17);
	Script::JSValueOwner first(runtime.ctx, runtime.handle(&point));
	Script::JSValueOwner prototype(runtime.ctx, JS_GetPrototype(runtime.ctx, first.get()));
	for (unsigned repeat = 0; repeat < 4; ++repeat)
	{
		Script::JSValueOwner value(runtime.ctx, repeat % 2 ? runtime.handle(&readonly)
															 : runtime.handle(&point));
		Script::JSValueOwner again(runtime.ctx, JS_GetPrototype(runtime.ctx, value.get()));
		CHECK(JS_VALUE_GET_PTR(again.get()) == JS_VALUE_GET_PTR(prototype.get()));
		CHECK(runtime.prototypes.size() == 1);
		Script::JSValueOwner x(runtime.ctx, JS_GetPropertyStr(runtime.ctx, value.get(), "x"));
		runtime.check(x.get());
		CHECK(runtime.read<int>(x.get()) == (repeat % 2 ? 13 : 7));
	}
}

TEST_CASE("Moved callback arguments retain their converted storage" *
		  doctest::test_suite("ScriptGenerator"))
{
	auto p = package();
	GenerationRequest request;
	GenerationContext context(request);
	JSGen::Binding runtime(*p, request, nullptr, &context, false, false);
	Script::JSValueOwner text(runtime.ctx, runtime.write(std::string("converted argument")));
	Script::JSValueOwner fn(runtime.ctx, runtime.write(std::function<std::string(std::string)>(
											 [](std::string value) { return value; })));
	auto arg = text.get();
	Script::JSValueOwner result(runtime.ctx, JS_Call(runtime.ctx, fn.get(), JS_UNDEFINED, 1, &arg));
	runtime.check(result.get());
	CHECK(runtime.read<std::string>(result.get()) == "converted argument");

	JSGen::Argument<int *> original(runtime, JS_NewInt32(runtime.ctx, 7));
	auto oldAddress = original.get();
	JSGen::Argument<int *> moved(std::move(original));
	CHECK(moved.get() != oldAddress);
	CHECK(*moved.get() == 7);
}

TEST_CASE("Owned classes preserve borrowed context and RNG expiration" *
		  doctest::test_suite("ScriptGenerator"))
{
	glob2test::HeadlessGlobals globals;
	const std::string trial =
		"c.toolkit.ScoredSettlements.chooseScoredSettlements([[0,1,2,3]],()=>{";
	const std::string finish = "return false;},()=> '');";
	auto brief = invoke("let saved;" + trial + "saved=c.toolkit.Solve.Brief('probe');" + finish +
						"saved.target('after',0,1);");
	CHECK(brief.find("Expired callback handle") != std::string::npos);
	auto height = invoke("let saved;" + trial +
						 "saved=c.toolkit.HeightMap.HeightMap(16,16,c.stream('probe'));" + finish +
						 "saved.makePlain(1);");
	CHECK(height.find("Expired callback handle") != std::string::npos);
	// A class borrowing the outer stream remains valid even if constructed in a trial.
	auto outer = invoke("const random=c.stream('outer');let saved;" + trial +
						"saved=c.toolkit.HeightMap.HeightMap(16,16,random);" + finish +
						"saved.makePlain(1);");
	INFO(outer);
	CHECK(outer.empty());
}

TEST_CASE("Every operation registration path shares the handle budget" *
		  doctest::test_suite("ScriptGenerator"))
{
	auto error = invoke(R"(
        const g=c.toolkit.GraphMaze.CellGraph({distance2:(a,b)=>a+b});
        for(let i=0;i<31000;i++)void g.distance2;
    )");
	INFO(error);
	CHECK(error.find("Too many toolkit operation handles") != std::string::npos);
}

TEST_CASE("By value copies reserve native payloads before allocation" *
		  doctest::test_suite("ScriptGenerator"))
{
	auto p = package();
	GenerationRequest request;
	GenerationContext context(request);
	JSGen::Binding runtime(*p, request, nullptr, &context, false, false);
	Script::JSValueOwner buffer(runtime.ctx, runtime.write(std::vector<int>(1024, 7)));
	JSGen::Argument<std::vector<int>> value(runtime, buffer.get());
	runtime.allocate(128 * 1024 * 1024 - runtime.nativeBytes - 512);
	CHECK_THROWS_AS(value.get(), JSGen::ResourceError);
	CHECK(runtime.exhausted);
}

TEST_CASE("Nested buffer get fill and set reserve before copying" *
		  doctest::test_suite("ScriptGenerator"))
{
	using Rows = std::vector<std::vector<int>>;
	for (const auto *method : {"get", "fill", "set"})
	{
		auto p = package();
		GenerationRequest request;
		GenerationContext context(request);
		JSGen::Binding runtime(*p, request, nullptr, &context, false, false);
		Script::JSValueOwner rows(
			runtime.ctx,
			runtime.write(
				Rows(16, std::vector<int>(method == std::string_view("get") ? 1024 : 1, 3))));
		Script::JSValueOwner source(runtime.ctx, runtime.write(std::vector<int>(1024, 7)));
		Script::JSValueOwner fn(runtime.ctx, JS_GetPropertyStr(runtime.ctx, rows.get(), method));
		const auto index = JS_NewUint32(runtime.ctx, 0);
		std::array<JSValue, 2> args{index, source.get()};
		int count = method == std::string_view("set") ? 2 : 1;
		if (method == std::string_view("fill"))
			args[0] = source.get();
		const auto remaining = method == std::string_view("fill") ? 8192 : 512;
		runtime.allocate(128 * 1024 * 1024 - runtime.nativeBytes - remaining);
		// Isolate native reservations from interpreter call-frame allocation.
		JS_SetMemoryLimit(runtime.runtime, 128 * 1024 * 1024);
		Script::JSValueOwner result(runtime.ctx,
									JS_Call(runtime.ctx, fn.get(), rows.get(), count, args.data()));
		INFO(method);
		CHECK(JS_IsException(result.get()));
		CHECK(runtime.exhausted);
		// No partially filled or overwritten destination survives a failed reservation.
		for (const auto &row : runtime.native<Rows>(rows.get()))
			CHECK(row.front() == 3);
	}
}

TEST_CASE("Nested buffer fills are accounted and returned rows own their data" *
		  doctest::test_suite("ScriptGenerator"))
{
	using Rows = std::vector<std::vector<int>>;
	auto p = package();
	GenerationRequest request;
	GenerationContext context(request);
	JSGen::Binding runtime(*p, request, nullptr, &context, false, false);
	Script::JSValueOwner rows(runtime.ctx, runtime.write(Rows(3)));
	Script::JSValueOwner source(runtime.ctx, runtime.write(std::vector<int>(1024, 7)));
	Script::JSValueOwner fill(runtime.ctx, JS_GetPropertyStr(runtime.ctx, rows.get(), "fill"));
	auto arg = source.get();
	const auto before = runtime.nativeBytes;
	Script::JSValueOwner filled(runtime.ctx, JS_Call(runtime.ctx, fill.get(), rows.get(), 1, &arg));
	runtime.check(filled.get());
	CHECK(runtime.nativeBytes >= before + 3 * 1024 * sizeof(int));
	CHECK(runtime.native<Rows>(rows.get()).at(2).at(1023) == 7);
	Script::JSValueOwner get(runtime.ctx, JS_GetPropertyStr(runtime.ctx, rows.get(), "get"));
	auto index = JS_NewUint32(runtime.ctx, 0);
	Script::JSValueOwner copied(runtime.ctx,
								JS_Call(runtime.ctx, get.get(), rows.get(), 1, &index));
	runtime.check(copied.get());
	runtime.native<std::vector<int>>(copied.get(), true).at(0) = 9;
	CHECK(runtime.native<Rows>(rows.get()).at(0).at(0) == 7);
}

TEST_CASE("Boolean buffer proxies become JavaScript booleans" *
		  doctest::test_suite("ScriptGenerator"))
{
	auto p = package();
	GenerationRequest request;
	GenerationContext context(request);
	JSGen::Binding runtime(*p, request, nullptr, &context, false, false);
	Script::JSValueOwner buffer(runtime.ctx, runtime.write(std::vector<bool>{true, false}));
	Script::JSValueOwner get(runtime.ctx, JS_GetPropertyStr(runtime.ctx, buffer.get(), "get"));
	auto index = JS_NewUint32(runtime.ctx, 0);
	Script::JSValueOwner value(runtime.ctx,
							   JS_Call(runtime.ctx, get.get(), buffer.get(), 1, &index));
	runtime.check(value.get());
	CHECK(runtime.read<bool>(value.get()));
	Script::JSValueOwner toArray(runtime.ctx,
								 JS_GetPropertyStr(runtime.ctx, buffer.get(), "toArray"));
	Script::JSValueOwner array(runtime.ctx,
							   JS_Call(runtime.ctx, toArray.get(), buffer.get(), 0, nullptr));
	runtime.check(array.get());
	Script::JSValueOwner second(runtime.ctx, JS_GetPropertyUint32(runtime.ctx, array.get(), 1));
	CHECK_FALSE(runtime.read<bool>(second.get()));
}

TEST_CASE("All named stream consumers share creation limits and accounting" *
		  doctest::test_suite("ScriptGenerator"))
{
	for (const auto *draw : {"c.stream('rng'+i);", "c.bounded('rng'+i,2);",
							 "c.shuffle([0,1],'rng'+i);", "c.toolkit.Solve.accept(1,1,'rng'+i);"})
	{
		auto error = invoke("for(let i=0;i<257;i++){" + std::string(draw) + "}");
		INFO(draw);
		CHECK(error.find("RNG stream limit exceeded") != std::string::npos);
	}
	auto p = package();
	GenerationRequest request;
	GenerationContext context(request);
	JSGen::Binding runtime(*p, request, nullptr, &context, false, false);
	MapGeneration::GenerationWork work{&runtime, [](void *host, uint64_t n)
									   { static_cast<JSGen::Binding *>(host)->chargeNative(n); },
									   [](void *host, uint64_t bytes)
									   { static_cast<JSGen::Binding *>(host)->allocate(bytes); }};
	{
		MapGeneration::GenerationWorkScope scope(work);
		const auto before = runtime.nativeBytes;
		context.bounded("counted", 2);
		CHECK(runtime.nativeBytes >= before + sizeof(std::mt19937));
		const auto after = runtime.nativeBytes;
		context.bounded("counted", 2);
		CHECK(runtime.nativeBytes == after);
	}
	// Native generators have no script-only cardinality/name restriction.
	GenerationContext native(request);
	for (int i = 0; i < 257; ++i)
		native.stream(std::string(129, 'x') + std::to_string(i));
	CHECK(native.namedStreams().size() == 257);
}
