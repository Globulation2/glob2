// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "script/ScriptRuntime.h"
#include "quickjs.h"
#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace Script;
namespace
{
Result execute(const std::string& source,Value state=Value::object(),bool initialize=false)
{
 Host h;h.tick=27;h.team=0;h.random=[]{return 123456789u;};h.query=[](const auto&,const auto&,const QueryBudget&){return Value::array();};return makeRuntime()->invoke(source,state,initialize,h);
}
void rejects(const std::string& source)
{
 bool rejected=false;try{execute(source);}catch(const HostFailure& ex){std::cerr<<"Unexpected host failure: "<<ex.what()<<'\n';GLOB2_REQUIRE(false, "JavaScript contract");}catch(const std::exception& ex){rejected=true;std::cout<<"rejected: "<<ex.what()<<'\n';}if(!rejected)std::cerr<<"Unexpectedly accepted: "<<source<<'\n';GLOB2_REQUIRE(rejected, "JavaScript contract");
}
}
TEST_CASE("JavaScript runtime sandbox and resource limits and numeric profile" * doctest::test_suite("JavaScriptRuntime"))
{
 Value value=Value::object().set("negativeZero",-0.0).set("first",std::string("a\0b",3)).set("second",3);
 auto decoded=Value::decode(value.encode());GLOB2_REQUIRE(decoded.encode()==value.encode(), "JavaScript contract");GLOB2_REQUIRE(std::signbit(decoded.get("negativeZero").number), "JavaScript contract");
 auto unicode=execute(R"(export function step(c,s){s.value='a\0b\ud800\udc00\udfff';s['k\0z']='v\ud800';})");
 auto unicodeNext=execute(R"(export function step(c,s){s.ok=s.value.length===6 && s.value.charCodeAt(5)===57343 && s['k\0z'].charCodeAt(1)===55296;})",Value::decode(unicode.state.encode()));GLOB2_REQUIRE(unicodeNext.state.get("ok").number==1, "JavaScript contract");
 auto result=execute("let hidden=0; export function init(ctx,s){s.started=true;} export function step(ctx,s){s.count=(s.count??0)+1;s.hidden=++hidden;s.random=ctx.random();return null;}",Value::object(),true);
 GLOB2_REQUIRE(result.state.get("count").number==1, "JavaScript contract");GLOB2_REQUIRE(result.state.get("started").number==1, "JavaScript contract");
 auto next=execute("let hidden=0; export function step(ctx,s){s.count++;s.hidden=++hidden;return null;}",Value::decode(result.state.encode()));GLOB2_REQUIRE(next.state.get("count").number==2, "JavaScript contract");GLOB2_REQUIRE(next.state.get("hidden").number==1, "JavaScript contract");
 auto math=execute("export function step(ctx,s){s.sin=Math.sin(1);s.pow=2**0.5;s.same=s.pow===Math.pow(2,0.5);s.negative=-0;s.remainder=-5%2;s.values=[Math.acos(.5),Math.acosh(2),Math.asin(.5),Math.asinh(2),Math.atan(2),Math.atan2(1,-1),Math.atanh(.5),Math.cos(2),Math.cosh(2),Math.exp(2),Math.expm1(.1),Math.log(2),Math.log1p(.1),Math.log2(3),Math.log10(3),Math.sinh(2),Math.sqrt(2),Math.tan(2),Math.tanh(2),Math.cbrt(2),Math.hypot(2,3)];return null;}");
 GLOB2_REQUIRE(math.state.get("same").number==1, "JavaScript contract");GLOB2_REQUIRE(math.state.get("remainder").number==-1, "JavaScript contract");GLOB2_REQUIRE(std::signbit(math.state.get("negative").number), "JavaScript contract");
 const std::uint64_t expected[]={0x3ff0c152382d7366,0x3ff5124271980434,0x3fe0c152382d7366,0x3ff719218313d087,0x3ff1b6e192ebbe44,0x4002d97c7f3321d2,0x3fe193ea7aad030a,0xbfdaa22657537205,0x400e18fa0df2d9bc,0x401d8e64b8d4ddae,0x3fbaec7b35a00d3a,0x3fe62e42fefa39ef,0x3fb8663f793c46c7,0x3ff95c01a39fbd68,0x3fde8927964fd5fd,0x400d03cf63b6e1a0,0x3ff6a09e667f3bcd,0xc0017af62e0950f8,0x3feed9505e1bc3d4,0x3ff428a2f98d728b,0x400cd82b446159f3};
 for(unsigned i=0;i<21;++i)GLOB2_REQUIRE(std::bit_cast<std::uint64_t>(math.state.get("values").items[i].number)==expected[i], "JavaScript contract");
 std::cout<<"numeric-bits";for(const auto& item:math.state.get("values").items)std::cout<<' '<<std::hex<<std::bit_cast<std::uint64_t>(item.number);std::cout<<std::dec<<'\n';
 auto edges=execute("export function step(c,s){s.edges=[Math.sin(1e300),Math.cos(1e300),Math.exp(-745),Math.log(Number.MIN_VALUE),Math.sqrt(Number.MIN_VALUE),Math.pow(Number.MIN_VALUE,.5),Math.atan2(-0,-0)];s.infinity=Math.pow(-0,-3)===-Infinity;s.json=JSON.parse('0.1000000000000000055511151231257827021181583404541015625')===.1;}");
 GLOB2_REQUIRE(edges.state.get("infinity").number==1 && edges.state.get("json").number==1, "JavaScript contract");
 const std::uint64_t edgeExpected[]={0xbfea2c16b010e385,0xbfe2699022adc4c1,1,0xc0874385446d71c3,0x1e60000000000000,0x1e60000000000000,0xc00921fb54442d18};
 for(unsigned i=0;i<7;++i)GLOB2_REQUIRE(std::bit_cast<std::uint64_t>(edges.state.get("edges").items[i].number)==edgeExpected[i], "JavaScript contract");
 std::cout<<"edge-bits";for(const auto& item:edges.state.get("edges").items)std::cout<<' '<<std::hex<<std::bit_cast<std::uint64_t>(item.number);std::cout<<std::dec<<'\n';
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
 bool ordinaryFailure=false;try{execute("export function step(){throw 'out of memory';}");}catch(const HostFailure&){GLOB2_REQUIRE(false, "JavaScript contract");}catch(const std::runtime_error&){ordinaryFailure=true;}GLOB2_REQUIRE(ordinaryFailure, "JavaScript contract");
 rejects("export function step(){let s='x';for(let i=0;i<30;i++)s=s+s;return null;}");
 rejects("export function step(){let a='x'.repeat(10000),b='x'.repeat(10000);for(let i=0;i<1000;i++){if(a!==b)throw 1;}return null;}");
 rejects("export function step(){return 'x'.repeat(40000000);}");
 rejects("export function step(){return 'x'.padStart(40000000);}");
 rejects("export function step(){return Array.prototype.forEach.call({get length(){return 1e9;}},()=>{});}");
 rejects("export function step(){let a={x:'x'};for(let i=0;i<22;i++)a={x:a,y:a};return JSON.stringify(a);}");
 rejects("export function step(){let a=[1];for(let i=0;i<22;i++)a=[a,a];return a.flat(Infinity);}");
 auto ordinaryArray=execute("export function step(c,s){let a=[];for(let i=0;i<1000;i++)a.push(i);s.last=a.slice().map(x=>x+1).pop();}");GLOB2_REQUIRE(ordinaryArray.state.get("last").number==1000, "JavaScript contract");
 for(const char* method:{"indexOf","lastIndexOf","includes","split","replace","replaceAll"})
  rejects(std::string("export function step(c,s){let a='a'.repeat(100000),b='a'.repeat(50000)+'b';s.result=String.prototype.")+method+".call({toString(){return a;}},{toString(){return b;}}"+(std::string(method)=="split"?",1e9":",'x'")+");}");
 rejects("export function step(){let a=[],v='a'.repeat(2000);for(let i=0;i<1000;i++)a.push(v);return a.join('');}");
 rejects("export function step(){let a=[],v='a'.repeat(2000);for(let i=0;i<1000;i++)a.push(v+i);a.sort();}");
 rejects("export function step(){JSON.parse('['.repeat(100)+'0'+']'.repeat(100));}");
 rejects("export function step(){let a=0;for(let i=0;i<100;i++)a=[a];return JSON.stringify(a);}");
 rejects("export function step(){let a=[0];for(let i=0;i<100;i++)a=[a];return a.flat(Infinity);}");
 rejects("export function step(){return "+std::string(1000,'(')+"0"+std::string(1000,')')+";}");
 rejects("export function step(){return "+std::string(1000,'!')+"0;}");
 auto containers=execute("export function step(c,s){s.slice='x'.repeat(1000).slice(0,1);s.parsed=JSON.parse(' '.repeat(1000)+'0');let a=new Set(),b=new Map();for(let i=0;i<150;i++){a.add(i);b.set(i,i);}s.sizes=[a.size,b.size];}");GLOB2_REQUIRE(containers.state.get("slice").text=="x" && containers.state.get("parsed").number==0 && containers.state.get("sizes").items[1].number==150, "JavaScript contract");
 rejects("export function step(c,s){s.a=new Array(900000);}");
 rejects("export function step(c,s){s.a=Array(200000).fill(null);}");
 auto sparseWithNames="export function step(c,s){let a=Array(10000);for(let i=0;i<10000;i++)a['k'+i]=null;s.a=a;}";rejects(sparseWithNames);
 std::string encodedArray(1,char(Value::Array));encodedArray.append("\0\3\15\100",4);encodedArray.append(200000,char(Value::Null));bool nativeBound=false;try{Value::decode(encodedArray);}catch(const std::runtime_error&){nativeBound=true;}GLOB2_REQUIRE(nativeBound, "JavaScript contract");
 rejects("export function step(){let "+std::string(1000,'[')+"x"+std::string(1000,']')+"=[];}");
 JSRuntime* raw=JS_NewRuntime();JSContext* context=JS_NewContext(raw);GLOB2_REQUIRE(context, "JavaScript contract");JS_SetMemoryLimit(raw,1);
 std::string big(4096,'x');JSValue failed=JS_NewStringLen(context,big.data(),big.size());GLOB2_REQUIRE(JS_IsException(failed) && JS_Glob2HostFailure(raw), "JavaScript contract");
 JS_FreeValue(context,JS_GetException(context));JS_FreeContext(context);JS_FreeRuntime(raw);
 auto absent=execute("export function step(ctx,s){s.secure=[typeof Date,typeof performance,typeof fetch,typeof eval,typeof Function,typeof Promise,typeof WeakRef,typeof SharedArrayBuffer,typeof Array.fromAsync].every(x=>x==='undefined');return null;}");GLOB2_REQUIRE(absent.state.get("secure").number==1, "JavaScript contract");
 std::cout<<"JavaScript runtime tests passed\n";
}
