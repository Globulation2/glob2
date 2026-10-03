// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "script/ScriptRuntime.h"
#include "quickjs.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace Script;
namespace
{
Result execute(const std::string &source, Value state = Value::object(), bool initialize = false)
{
	Host h;
	h.tick = 27;
	h.team = 0;
	h.random = [] { return 123456789u; };
	h.query = [](const auto &, const auto &, const QueryBudget &) { return Value::array(); };
	state.fields.erase(std::remove_if(state.fields.begin(), state.fields.end(),
									  [](const auto &field)
									  { return field.first == "__glob2_globals"; }),
					   state.fields.end());
	return makeRuntime()->invoke(source, state, initialize, h);
}
void rejects(const std::string &source)
{
	bool rejected = false;
	try
	{
		execute(source);
	}
	catch (const HostFailure &ex)
	{
		std::cerr << "Unexpected host failure: " << ex.what() << '\n';
		GLOB2_REQUIRE(false, "JavaScript contract");
	}
	catch (const std::exception &ex)
	{
		rejected = true;
		std::cout << "rejected: " << ex.what() << '\n';
	}
	if (!rejected)
		std::cerr << "Unexpectedly accepted: " << source << '\n';
	GLOB2_REQUIRE(rejected, "JavaScript contract");
}
} // namespace
TEST_CASE("JavaScript persistent value encoding and Unicode" *
		  doctest::test_suite("JavaScriptRuntime"))
{
	Value value = Value::object()
					  .set("negativeZero", -0.0)
					  .set("first", std::string("a\0b", 3))
					  .set("second", 3);
	auto decoded = Value::decode(value.encode());
	GLOB2_REQUIRE(decoded.encode() == value.encode(), "JavaScript contract");
	GLOB2_REQUIRE(std::signbit(decoded.get("negativeZero").number), "JavaScript contract");
	auto unicode = execute(
		R"(export function step(c,s){s.value='a\0b\ud800\udc00\udfff';s['k\0z']='v\ud800';})");
	auto unicodeNext = execute(
		R"(export function step(c,s){s.ok=s.value.length===6 && s.value.charCodeAt(5)===57343 && s['k\0z'].charCodeAt(1)===55296;})",
		Value::decode(unicode.state.encode()));
	GLOB2_REQUIRE(unicodeNext.state.get("ok").number == 1, "JavaScript contract");
}

TEST_CASE("JavaScript legacy draft callback argument" * doctest::test_suite("JavaScriptRuntime"))
{
	auto result = execute(
		"let hidden=0; export function init(ctx,s){s.started=true;} export function "
		"step(ctx,s){s.count=(s.count??0)+1;s.hidden=++hidden;s.random=ctx.random();return null;}",
		Value::object(), true);
	GLOB2_REQUIRE(result.state.get("count").number == 1, "JavaScript contract");
	GLOB2_REQUIRE(result.state.get("started").number == 1, "JavaScript contract");
	auto next = execute(
		"let hidden=0; export function step(ctx,s){s.count++;s.hidden=++hidden;return null;}",
		Value::decode(result.state.encode()));
	GLOB2_REQUIRE(next.state.get("count").number == 2, "JavaScript contract");
	GLOB2_REQUIRE(next.state.get("hidden").number == 1, "JavaScript contract");
}

TEST_CASE("JavaScript pinned Math bits and numeric edges" *
		  doctest::test_suite("JavaScriptRuntime"))
{
	auto math = execute(
		"export function "
		"step(ctx,s){s.sin=Math.sin(1);s.pow=2**0.5;s.same=s.pow===Math.pow(2,0.5);s.negative=-0;s."
		"remainder=-5%2;s.values=[Math.acos(.5),Math.acosh(2),Math.asin(.5),Math.asinh(2),Math."
		"atan(2),Math.atan2(1,-1),Math.atanh(.5),Math.cos(2),Math.cosh(2),Math.exp(2),Math.expm1(."
		"1),Math.log(2),Math.log1p(.1),Math.log2(3),Math.log10(3),Math.sinh(2),Math.sqrt(2),Math."
		"tan(2),Math.tanh(2),Math.cbrt(2),Math.hypot(2,3)];return null;}");
	GLOB2_REQUIRE(math.state.get("same").number == 1, "JavaScript contract");
	GLOB2_REQUIRE(math.state.get("remainder").number == -1, "JavaScript contract");
	GLOB2_REQUIRE(std::signbit(math.state.get("negative").number), "JavaScript contract");
	const std::uint64_t expected[] = {0x3ff0c152382d7366, 0x3ff5124271980434, 0x3fe0c152382d7366,
									  0x3ff719218313d087, 0x3ff1b6e192ebbe44, 0x4002d97c7f3321d2,
									  0x3fe193ea7aad030a, 0xbfdaa22657537205, 0x400e18fa0df2d9bc,
									  0x401d8e64b8d4ddae, 0x3fbaec7b35a00d3a, 0x3fe62e42fefa39ef,
									  0x3fb8663f793c46c7, 0x3ff95c01a39fbd68, 0x3fde8927964fd5fd,
									  0x400d03cf63b6e1a0, 0x3ff6a09e667f3bcd, 0xc0017af62e0950f8,
									  0x3feed9505e1bc3d4, 0x3ff428a2f98d728b, 0x400cd82b446159f3};
	for (unsigned i = 0; i < 21; ++i)
		GLOB2_REQUIRE(std::bit_cast<std::uint64_t>(math.state.get("values").items[i].number) ==
						  expected[i],
					  "JavaScript contract");
	std::cout << "numeric-bits";
	for (const auto &item : math.state.get("values").items)
		std::cout << ' ' << std::hex << std::bit_cast<std::uint64_t>(item.number);
	std::cout << std::dec << '\n';
	auto edges =
		execute("export function "
				"step(c,s){s.edges=[Math.sin(1e300),Math.cos(1e300),Math.exp(-745),Math.log(Number."
				"MIN_VALUE),Math.sqrt(Number.MIN_VALUE),Math.pow(Number.MIN_VALUE,.5),Math.atan2(-"
				"0,-0)];s.infinity=Math.pow(-0,-3)===-Infinity;s.json=JSON.parse('0."
				"1000000000000000055511151231257827021181583404541015625')===.1;}");
	GLOB2_REQUIRE(edges.state.get("infinity").number == 1 && edges.state.get("json").number == 1,
				  "JavaScript contract");
	const std::uint64_t edgeExpected[] = {
		0xbfea2c16b010e385, 0xbfe2699022adc4c1, 1, 0xc0874385446d71c3, 0x1e60000000000000,
		0x1e60000000000000, 0xc00921fb54442d18};
	for (unsigned i = 0; i < 7; ++i)
		GLOB2_REQUIRE(std::bit_cast<std::uint64_t>(edges.state.get("edges").items[i].number) ==
						  edgeExpected[i],
					  "JavaScript contract");
	std::cout << "edge-bits";
	for (const auto &item : edges.state.get("edges").items)
		std::cout << ' ' << std::hex << std::bit_cast<std::uint64_t>(item.number);
	std::cout << std::dec << '\n';
}

TEST_CASE("JavaScript restricted capabilities and invalid values" *
		  doctest::test_suite("JavaScriptRuntime"))
{
	rejects("export function step(ctx,s){s.instance=new (class Example {})();}");
	rejects("export function step(ctx,s){s[Symbol('key')]={};}");
	rejects("export function step(){while(true){}}");
	rejects("export function step(){try{while(true){}}catch(e){return null;}}");
	rejects("export function step(ctx,s){s.self=s;}");
	rejects("export function step(ctx,s){s.value=NaN;}");
	rejects("export function step(ctx,s){Object.defineProperty(s,'x',{get(){while(true){}}});}");
	rejects("export function step(){return (()=>{}).constructor('return globalThis')();}");
	rejects("export function step(){return /a/;}");
	rejects("export async function step(){return null;}");
	rejects("for await(const x of []){} export function step(){}");
	rejects("export function step(){return 100n;}");
	rejects("export function step(){return import('os');}");
	rejects("import * as os from 'os';export function step(){return os;}");
	rejects("export function step(){return new Array(10000000).reverse();}");
	bool ordinaryFailure = false;
	try
	{
		execute("export function step(){throw 'out of memory';}");
	}
	catch (const HostFailure &)
	{
		GLOB2_REQUIRE(false, "JavaScript contract");
	}
	catch (const std::runtime_error &)
	{
		ordinaryFailure = true;
	}
	GLOB2_REQUIRE(ordinaryFailure, "JavaScript contract");
}

TEST_CASE("JavaScript generated native work is bounded" * doctest::test_suite("JavaScriptRuntime"))
{
	rejects("export function step(){let s='x';for(let i=0;i<30;i++)s=s+s;return null;}");
	rejects("export function step(){let a='x'.repeat(10000),b='x'.repeat(10000);for(let "
			"i=0;i<1000;i++){if(a!==b)throw 1;}return null;}");
	rejects("export function step(){return 'x'.repeat(40000000);}");
	rejects("export function step(){return 'x'.padStart(40000000);}");
	rejects("export function step(){return Array.prototype.forEach.call({get length(){return "
			"1e9;}},()=>{});}");
	rejects("export function step(){let a={x:'x'};for(let i=0;i<22;i++)a={x:a,y:a};return "
			"JSON.stringify(a);}");
	rejects(
		"export function step(){let a=[1];for(let i=0;i<22;i++)a=[a,a];return a.flat(Infinity);}");
	auto ordinaryArray = execute("export function step(c,s){let a=[];for(let "
								 "i=0;i<1000;i++)a.push(i);s.last=a.slice().map(x=>x+1).pop();}");
	GLOB2_REQUIRE(ordinaryArray.state.get("last").number == 1000, "JavaScript contract");
	for (const char *method :
		 {"indexOf", "lastIndexOf", "includes", "split", "replace", "replaceAll"})
		rejects(
			std::string("export function step(c,s){let "
						"a='a'.repeat(100000),b='a'.repeat(50000)+'b';s.result=String.prototype.") +
			method + ".call({toString(){return a;}},{toString(){return b;}}" +
			(std::string(method) == "split" ? ",1e9" : ",'x'") + ");}");
	rejects("export function step(){let a=[],v='a'.repeat(2000);for(let "
			"i=0;i<1000;i++)a.push(v);return a.join('');}");
	rejects("export function step(){let a=[],v='a'.repeat(2000);for(let "
			"i=0;i<1000;i++)a.push(v+i);a.sort();}");
	rejects("export function step(){JSON.parse('['.repeat(100)+'0'+']'.repeat(100));}");
	rejects(
		"export function step(){let a=0;for(let i=0;i<100;i++)a=[a];return JSON.stringify(a);}");
	rejects(
		"export function step(){let a=[0];for(let i=0;i<100;i++)a=[a];return a.flat(Infinity);}");
	rejects("export function step(){return " + std::string(1000, '(') + "0" +
			std::string(1000, ')') + ";}");
	rejects("export function step(){return " + std::string(1000, '!') + "0;}");
	auto containers = execute(
		"export function step(c,s){s.slice='x'.repeat(1000).slice(0,1);s.parsed=JSON.parse(' "
		"'.repeat(1000)+'0');let a=new Set(),b=new Map();for(let "
		"i=0;i<150;i++){a.add(i);b.set(i,i);}s.sizes=[a.size,b.size];}");
	GLOB2_REQUIRE(containers.state.get("slice").text == "x" &&
					  containers.state.get("parsed").number == 0 &&
					  containers.state.get("sizes").items[1].number == 150,
				  "JavaScript contract");
}

TEST_CASE("JavaScript native tree bounds and parser depth" *
		  doctest::test_suite("JavaScriptRuntime"))
{
	rejects("export function step(c,s){s.a=new Array(900000);}");
	rejects("export function step(c,s){s.a=Array(200000).fill(null);}");
	auto sparseWithNames = "export function step(c,s){let a=Array(10000);for(let "
						   "i=0;i<10000;i++)a['k'+i]=null;s.a=a;}";
	rejects(sparseWithNames);
	std::string encodedArray(1, char(Value::Array));
	encodedArray.append("\0\3\15\100", 4);
	encodedArray.append(200000, char(Value::Null));
	bool nativeBound = false;
	try
	{
		Value::decode(encodedArray);
	}
	catch (const std::runtime_error &)
	{
		nativeBound = true;
	}
	GLOB2_REQUIRE(nativeBound, "JavaScript contract");
	rejects("export function step(){let " + std::string(1000, '[') + "x" + std::string(1000, ']') +
			"=[];}");
}

TEST_CASE("JavaScript fatal native allocation is sticky" * doctest::test_suite("JavaScriptRuntime"))
{
	JSRuntime *raw = JS_NewRuntime();
	JSContext *context = JS_NewContext(raw);
	GLOB2_REQUIRE(context, "JavaScript contract");
	JS_SetMemoryLimit(raw, 1);
	std::string big(4096, 'x');
	JSValue failed = JS_NewStringLen(context, big.data(), big.size());
	GLOB2_REQUIRE(JS_IsException(failed) && JS_Glob2HostFailure(raw), "JavaScript contract");
	JS_FreeValue(context, JS_GetException(context));
	JS_FreeContext(context);
	JS_FreeRuntime(raw);
}

TEST_CASE("JavaScript unavailable capabilities" * doctest::test_suite("JavaScriptRuntime"))
{
	auto absent = execute(
		"export function step(ctx,s){s.secure=[typeof Date,typeof performance,typeof fetch,typeof "
		"eval,typeof Function,typeof Promise,typeof WeakRef,typeof SharedArrayBuffer,typeof "
		"Array.fromAsync].every(x=>x==='undefined');return null;}");
	GLOB2_REQUIRE(absent.state.get("secure").number == 1, "JavaScript contract");
	std::cout << "JavaScript runtime tests passed\n";
}

TEST_CASE("JavaScript hypot ARM64 and x86-64 regression" * doctest::test_suite("JavaScriptRuntime"))
{
	auto result = execute("export function "
						  "step(c,s){s.hypot=Math.hypot(1.2154874465220262,1.8249387819142753);s."
						  "branch=s.hypot===2.192672180328695;}");
	CHECK(std::bit_cast<std::uint64_t>(result.state.get("hypot").number) ==
		  UINT64_C(0x40018a97b64ae2d6));
	CHECK(result.state.get("branch").number == 1);
}

TEST_CASE("JavaScript globals persist and restore without an explicit state object" *
		  doctest::test_suite("JavaScriptRuntime"))
{
	const std::string source = R"(
let calls = 0;
const memory = {ticks: []};
function step(ctx) {
  calls++;
  memory.ticks.push(ctx.tick);
  return {calls, ticks: memory.ticks.slice(), arguments: arguments.length};
}
)";
	Host host;
	auto runtime = makeRuntime();
	Value snapshot = Value::object();
	for (unsigned tick = 0; tick < 4; ++tick)
	{
		host.tick = tick;
		auto result = runtime->invoke(source, snapshot, tick == 0, host);
		CHECK(result.effects.get("calls").number == tick + 1);
		CHECK(result.effects.get("ticks").items.size() == tick + 1);
		CHECK(result.effects.get("arguments").number == 1);
		snapshot = Value::decode(result.state.encode());
		if (tick == 1)
			runtime = makeRuntime();
	}
}

TEST_CASE("JavaScript main restores aliases cycles undefined and numeric globals" *
		  doctest::test_suite("JavaScriptRuntime"))
{
	const std::string source = R"(
var count = 0;
let value = {n: 0};
const alias = value;
let absent;
const special = [-0, NaN, Infinity, -Infinity];
value.self = value;
const increment = () => ++count;
function main(ctx) {
  value.n++;
  return {count: increment(), n: alias.n,
    same: value === alias && value.self === value,
    absent: absent === undefined,
    numbers: Object.is(special[0], -0) && Number.isNaN(special[1]) &&
      special[2] === Infinity && special[3] === -Infinity};
}
)";
	Host host;
	Value snapshot = Value::object();
	for (unsigned tick = 0; tick < 4; ++tick)
	{
		auto result = makeRuntime()->invoke(source, snapshot, tick == 0, host);
		CHECK(result.effects.get("count").number == tick + 1);
		CHECK(result.effects.get("n").number == tick + 1);
		CHECK(result.effects.get("same").number == 1);
		CHECK(result.effects.get("absent").number == 1);
		CHECK(result.effects.get("numbers").number == 1);
		snapshot = Value::decode(result.state.encode());
	}
}

TEST_CASE("JavaScript rejected callbacks roll back global mutations" *
		  doctest::test_suite("JavaScriptTransactions"))
{
	const std::string source = R"(
let calls = 0;
function step(ctx) {
  calls++;
  if (ctx.tick === 2) throw new Error('rejected');
  return {calls};
}
)";
	Host host;
	auto runtime = makeRuntime();
	auto accepted = runtime->invoke(source, Value::object(), true, host);
	host.tick = 2;
	CHECK_THROWS(runtime->invoke(source, accepted.state, false, host));
	host.tick = 3;
	auto resumed = runtime->invoke(source, accepted.state, false, host);
	CHECK(resumed.effects.get("calls").number == 2);
	// A host can reject an otherwise valid return before committing its effects.
	auto rejected = runtime->invoke(source, resumed.state, false, host);
	runtime->discard();
	auto retry = runtime->invoke(source, resumed.state, false, host);
	CHECK(retry.state.encode() == rejected.state.encode());
}

TEST_CASE("JavaScript global object descriptors survive save and restore" *
		  doctest::test_suite("JavaScriptRuntime"))
{
	const std::string source = R"(
const items = [];
items.length = 4;
items[2] = {count: 0};
Object.defineProperty(items[2], 'hidden', {value: 19, enumerable: false});
Object.seal(items[2]);
const dictionary = Object.create(null);
dictionary.item = items[2];
function step(ctx) {
  items[2].count++;
  return {count: items[2].count, hole: !(0 in items), length: items.length,
    hidden: items[2].hidden, sealed: Object.isSealed(items[2]),
    enumerable: Object.keys(items[2]).length === 1,
    nullPrototype: Object.getPrototypeOf(dictionary) === null && dictionary.item === items[2]};
}
)";
	Host host;
	Value snapshot = Value::object();
	for (unsigned tick = 0; tick < 3; ++tick)
	{
		auto result = makeRuntime()->invoke(source, snapshot, tick == 0, host);
		CHECK(result.effects.get("count").number == tick + 1);
		CHECK(result.effects.get("hole").number == 1);
		CHECK(result.effects.get("length").number == 4);
		CHECK(result.effects.get("hidden").number == 19);
		CHECK(result.effects.get("sealed").number == 1);
		CHECK(result.effects.get("enumerable").number == 1);
		CHECK(result.effects.get("nullPrototype").number == 1);
		snapshot = Value::decode(result.state.encode());
	}
}

TEST_CASE("JavaScript saved NaNs have canonical bits across architectures" *
		  doctest::test_suite("JavaScriptRuntime"))
{
	const std::string source = R"(
let calls = 0;
let numbers = [];
function step(ctx) {
  calls++;
  const zero = ctx.tick - ctx.tick;
  numbers = [NaN, -NaN, zero / zero, Math.sqrt(-1), Math.log(-1)];
  return {calls, nan: numbers.every(Number.isNaN)};
}
)";
	Host host;
	auto continuous = makeRuntime();
	Value checkpoint = Value::object();
	for (unsigned tick = 0; tick < 3; ++tick)
	{
		host.tick = tick;
		auto uninterrupted = continuous->invoke(source, checkpoint, tick == 0, host);
		auto reloaded = makeRuntime()->invoke(source, Value::decode(checkpoint.encode()),
										 tick == 0, host);
		CHECK(uninterrupted.state.encode() == reloaded.state.encode());
		CHECK(uninterrupted.effects.get("calls").number == tick + 1);
		CHECK(uninterrupted.effects.get("nan").number == 1);
		unsigned nanCount = 0;
		for (const auto &node : uninterrupted.state.get("__glob2_globals").get("nodes").items)
			for (const auto &property : node.get("properties").items)
			{
				const auto &token = property.items[1];
				if (token.kind == Value::Array && token.items.size() == 2 &&
					token.items[0].text == "number")
				{
					CHECK(token.items[1].text == "7ff8000000000000");
					++nanCount;
				}
			}
		CHECK(nanCount == 5);
		checkpoint = std::move(uninterrupted.state);
	}
}

TEST_CASE("JavaScript unsaveable global values fail explicitly" *
		  doctest::test_suite("JavaScriptRuntime"))
{
	rejects("let counter=(()=>{let n=0;return ()=>++n;})(); function step(){return counter();}");
	rejects("let callback;function step(){callback=()=>1;}");
	rejects("class Counter {static #n=0;static next(){return ++this.#n;}} function step(){return "
			"Counter.next();}");
	rejects("let data;function step(){data=new Map();}");
	rejects("function helper(){} const data=helper.prototype;function step(){return "
			"{same:data===helper.prototype};}");
	rejects("function helper(){} helper.next=(()=>{let n=0;return ()=>++n;})();function "
			"step(){return helper.next();}");
	rejects("function helper(){} helper.data=new Map();function step(){helper.data.set(1,2);}");
	rejects("let data;function step(){data=new (class {})();}");
	rejects("function step(){Math.extra=1;}");
}

#ifndef __EMSCRIPTEN__
TEST_CASE("JavaScript persistent contexts can migrate between serial workers" *
		  doctest::test_suite("JavaScriptRuntime"))
{
	auto runtime = makeRuntime();
	const std::string source = "let calls=0; function step(){return {calls:++calls};}";
	Host host;
	Result first;
	std::exception_ptr failure;
	std::thread worker(
		[&]
		{
			try
			{
				first = runtime->invoke(source, Value::object(), true, host);
			}
			catch (...)
			{
				failure = std::current_exception();
			}
		});
	worker.join();
	if (failure)
		std::rethrow_exception(failure);
	auto second = runtime->invoke(source, first.state, false, host);
	CHECK(first.effects.get("calls").number == 1);
	CHECK(second.effects.get("calls").number == 2);
}
#endif

TEST_CASE("JavaScript malformed automatic global snapshots are rejected" *
		  doctest::test_suite("JavaScriptRuntime"))
{
	Host host;
	const std::string source = "let count=0;function step(){return {count:++count};}";
	auto saved = makeRuntime()->invoke(source, Value::object(), true, host).state;
	auto corrupted = saved;
	corrupted.set("__glob2_globals", Value::object());
	CHECK_THROWS(makeRuntime()->invoke(source, corrupted, false, host));
	corrupted = saved;
	auto graph = corrupted.get("__glob2_globals");
	graph.fields.erase(graph.fields.begin());
	corrupted.set("__glob2_globals", graph);
	CHECK_THROWS(makeRuntime()->invoke(source, corrupted, false, host));
	CHECK(makeRuntime()->invoke(source, saved, false, host).effects.get("count").number == 2);
}

TEST_CASE("JavaScript profile two metadata and bundled export aliases" *
		  doctest::test_suite("JavaScriptRuntime"))
{
	const std::string source = R"(
 var memory={steps:0};
 function metadata2(){return {apiVersion:2,name:'Bundled test',version:'1.0'};}
 function step2(ctx){memory.steps++;ctx.telemetry.set('test.steps',memory.steps);}
 export {metadata2 as metadata, step2 as step};
 )";
	auto metadata = inspectAI(source);
	CHECK(metadata.apiVersion == 2);
	CHECK(metadata.name == "Bundled test");
	Host host;
	host.profile = 2;
	host.tick = 10;
	host.team = 0;
	host.random = [] { return 42u; };
	host.query = [](const auto &, const auto &, const QueryBudget &) { return Value(); };
	auto runtime = makeRuntime();
	auto first = runtime->invoke(source, Value::object(), true, host);
	CHECK(first.telemetry.get("test.steps").get("value").number == 1);
	auto second = makeRuntime()->invoke(source, Value::decode(first.state.encode()), false, host);
	CHECK(second.telemetry.get("test.steps").get("value").number == 2);
	CHECK(profileFromConfig(config(source, 2)) == 2);
	CHECK_THROWS(inspectAI(
		"export function metadata(){return {apiVersion:99,name:'bad'}} export function step(){}"));
	CHECK_THROWS(
		inspectAI("export function metadata(){return {apiVersion:2}} export function step(){}"));
	CHECK_THROWS(inspectAI("export function metadata(){return {apiVersion:2,name:Math.random()}} "
						   "export function step(){}"));
	CHECK(inspectAI("function step(){}").apiVersion == 1);
	CHECK_THROWS(inspectAI("function bundledStep(){} export {bundledStep as step};"));
}

TEST_CASE("JavaScript managed properties coalesce and reject persistent handles" *
		  doctest::test_suite("JavaScriptRuntime"))
{
	Host host;
	host.profile = 2;
	host.team = 0;
	host.nextAction = 10;
	host.query = [](const std::string &name, const auto &args, const QueryBudget &)
	{
		if (name == "building")
			return Value::object()
				.set("id", 0)
				.set("generation", 1)
				.set("team", 0)
				.set("shortType", 8)
				.set("virtual", true)
				.set("x", 1)
				.set("y", 2)
				.set("workers", 3);
		if (name == "desired")
			return Value::object();
		if (name == "validateAction" && args[0].get("type").text == "workers")
			args[0].integer("workers", 0, 20);
		return Value();
	};
	auto result = makeRuntime()->invoke(R"(export function step(ctx){
  let a=ctx.game.building({id:0,generation:1}), b=ctx.game.building({id:0,generation:1});
  if(a!==b)throw Error('identity');
  a.x=8; a.y=9; a.workers=5;
  try {a.workers=-1;} catch(e) {}
  if(a.workers!==5 || a.observed.workers!==3)throw Error('desired');
 })",
										Value::object(), false, host);
	CHECK(result.commands.items.size() == 2);
	CHECK(result.commands.items[0].get("x").number == 8);
	CHECK(result.commands.items[0].get("y").number == 9);
	CHECK(result.commands.items[1].get("workers").number == 5);
	CHECK_THROWS(makeRuntime()->invoke(
		"let saved; export function step(ctx){saved=ctx.game.building({id:0,generation:1});}",
		Value::object(), false, host));
}

TEST_CASE("JavaScript metadata cannot mutate game globals" *
		  doctest::test_suite("JavaScriptRuntime"))
{
	const std::string source = "let counter=0; export function metadata(){counter=99;return "
							   "{apiVersion:2,name:'Isolated'};} export function "
							   "step(ctx){ctx.telemetry.set('test.counter',counter);}";
	CHECK(inspectAI(source).apiVersion == 2);
	Host host;
	host.profile = 2;
	auto result = makeRuntime()->invoke(source, Value::object(), true, host);
	CHECK(result.telemetry.get("test.counter").get("value").number == 0);
}
