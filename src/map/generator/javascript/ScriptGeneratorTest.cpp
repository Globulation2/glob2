// SPDX-License-Identifier: GPL-3.0-or-later
#include "GeneratorPackage.h"
#include "ToolkitBinding.h"
#include "GenerationService.h"
#include "EngineFixtures.h"
#include "MapGeneratorFrameworkChecks.h"
#include "CustomGamePreferences.h"
#include "CustomGameRules.h"
#include <nlohmann/json.hpp>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include "Sha256.h"

namespace JSGen = MapGeneration::JavaScript;
using Json = nlohmann::json;
namespace
{
std::string package(const std::string &source, unsigned revision = 1)
{
	return Json{{"formatVersion", 1},
				{"manifest",
				 {{"id", "test:generator"},
				  {"name", "Test landscape"},
				  {"apiVersion", 1},
				  {"revision", revision},
				  {"tags", {"terrain:natural"}},
				  {"controls", Json::array()}}},
				{"modules", {{"generator.js", source}}}}
		.dump();
}
struct RestoreCatalog
{
	std::shared_ptr<const GeneratorRegistry> previous = GeneratorRegistry::activeSnapshot();
	~RestoreCatalog() { GeneratorRegistry::publish(previous); }
};
std::string run(const std::string &body)
{
	try
	{
		auto p = JSGen::Package::parse(package("export function generate(c){" + body + "}"));
		GenerationRequest request;
		GenerationContext context(request);
		return JSGen::invoke(*p, "generate", request, nullptr, &context);
	}
	catch (const std::exception &error)
	{
		return error.what();
	}
}

} // namespace
TEST_CASE("Package identity controls modules and installation rollback" *
		  doctest::test_suite("ScriptGenerator"))
{
	RestoreCatalog restore;
	Online::MemoryStorage storage;
	JSGen::Library library(storage);
	auto source = package("export function generate(c){}");
	library.put(source);
	library.publish();
	auto id = GeneratorRegistry::active().idOf("test:generator");
	GenerationRequest old;
	old.setMethodDefaults(id);
	auto oldHash = old.definition().packageHash;
	CHECK_THROWS(library.put(source));
	auto checkpoint = library.checkpoint();
	library.put(package("export function generate(c){c.stage('updated');}", 2), "test:generator");
	library.publish();
	CHECK(old.definition().packageHash == oldHash);
	CHECK(GeneratorRegistry::active().at(id).packageHash != oldHash);
	storage.failWrites = true;
	CHECK_THROWS(library.remove("test:generator"));
	CHECK(library.entries().size() == 1);
	storage.failWrites = false;
	library.rollback(checkpoint);
	library.publish();
	CHECK(GeneratorRegistry::active().at(id).packageHash == oldHash);
	library.remove("test:generator");
	library.publish();
	CHECK(GeneratorRegistry::active().find(id) == nullptr);
	CHECK(old.definition().packageHash == oldHash);
	auto j = Json::parse(source);
	j["manifest"]["apiVersion"] = 2;
	CHECK_THROWS(JSGen::Package::parse(j.dump()));
	j = Json::parse(source);
	j["manifest"]["hasStartingColonies"] = false;
	CHECK_THROWS(JSGen::Package::parse(j.dump()));
	j = Json::parse(source);
	j["manifest"]["controls"] = {{{"id", "huge"},
								  {"label", "Huge"},
								  {"minimum", INT32_MIN},
								  {"maximum", INT32_MAX},
								  {"default", 0},
								  {"searchRange", {0, 1}}}};
	CHECK_THROWS(JSGen::Package::parse(j.dump()));
	j = Json::parse(source);
	j["modules"]["../escape.js"] = "";
	CHECK_THROWS(JSGen::Package::parse(j.dump()));
	j = Json::parse(source);
	j["modules"]["generator.js"] = "import './absent.js';export function generate(){}";
	CHECK_THROWS(JSGen::Package::parse(j.dump()));
}
TEST_CASE("Full toolkit records buffers geometry callbacks and solver" *
		  doctest::test_suite("ScriptGenerator"))
{
	glob2test::HeadlessGlobals globals;
	auto error = run(R"(
        const t=c.torus, m=c.mask();
        if(t.at(-1,-1)!==t.size()-1)throw Error('torus wrap');
        c.toolkit.Drawing.fillRectangle(m,t,{x0:0,y0:0,x1:4,y1:4});
        if(m.get(t.at(2,2))!==1 || m.get(t.at(5,5))!==0)throw Error('rectangle');
        let visits=0;
        c.toolkit.Drawing.forEachTileInPolygon(t,[{x:0,y:0},{x:64,y:0},{x:64,y:64},{x:0,y:64}],i=>{visits++;});
        if(visits!==16)throw Error('polygon callback');
        const edge=c.toolkit.Tessellation.Edge({cells:[3,5],corners:[7,9]});
        if(edge.cells.get(1)!==5)throw Error('fixed array');edge.cells.set(0,11);
        if(edge.cells.toArray()[0]!==11||edge.cells.clone().get(1)!==5)throw Error('fixed array alias');
        const clone=m.clone();clone.set(0,0);if(m.get(0)!==1)throw Error('buffer clone');
        let x=10,old,best;
        const report=c.toolkit.Solve.anneal({moves:20,from:1e-10,to:1e-10,stream:"test-anneal"},()=>{old=x;x--;return true;},()=>x*x,()=>{x=old;},()=>{best=x;},()=>{x=best;});
        if(x!==0 || report.best!==0 || report.attempted!==20)throw Error('solver');
        const p=c.toolkit.Grid.stepsFrom(t,m);if(p.length!==t.size())throw Error('flood');
    )");
	INFO(error);
	CHECK(error.empty());
	CHECK(!run("c.mask().get(-1);").empty());
	CHECK(!run("let "
			   "saved;c.toolkit.ScoredSettlements.chooseScoredSettlements([[0,1,2,3]],(game,"
			   "context,sites)=>{saved=c.stream('trial');return false;},()=> '');saved.next();")
			   .empty());
	CHECK(run("c.toolkit.Noise.GenerationNoise(1).reseed(4294967295);").empty());
	CHECK(run("c.toolkit.Farmland.FarmBridges({spacing:0});").empty());
	CHECK(run("c.toolkit.Raster.RasterFit({width:0,height:0});").empty());
	CHECK(!run("c.toolkit.Points.nearestSiteLabels(c.torus,[],0,[],[],0);").empty());
	CHECK(run("const "
			  "p=c.toolkit.Channels.SandFord({x:0,y:0,alongX:1,alongY:0,acrossX:0,acrossY:1,span:4,"
			  "halfWidth:2}).offsets(c.torus,3,4,0,0);if(p.along!==3||p.across!==4)throw "
			  "Error('output references');")
			  .empty());
	CHECK(!run("c.cachedDesign('async',async()=>1);").empty());
	CHECK(!run("c.toolkit.Drawing.forEachTileInPolygon(c.torus,[{x:0,y:0},{x:64,y:0},{x:64,y:64}],"
			   "async()=>{});")
			   .empty());
	CHECK(!run("c.toolkit.Grid.Axes().uOf(1);").empty());
	CHECK(!run("c.toolkit.Tessellation.Tessellation().cellAt(0,0);").empty());
	CHECK(!run("c.toolkit.Orbits.Symmetry({elements:[{a:1,b:0,c:0,d:1}]}).tile(0,0,0);").empty());
	CHECK(!run("c.toolkit.Raster.RasterFit({num:0,width:1,height:1}).cellsOf(0,0);").empty());
	CHECK(!run("c.toolkit.Grid.axesFor(0,0).at(1,1);").empty());
	CHECK(!run("c.toolkit.HeightMap.HeightMap(64,64,c.stream('memory')).makeCraters(1,20000,4);")
			   .empty());
	CHECK(!run("c.toolkit.Drawing.fillRectangle(c.mask(),c.torus,{x0:NaN});").empty());
	CHECK(!run("c.toolkit.Drawing.forEachTileInPolygon(c.torus,[{x:0,y:0},{x:64,y:0},{x:64,y:64}],("
			   ")=>{throw Error('callback failure');});")
			   .empty());
}
TEST_CASE("Graph ownership optional callbacks and pointer outputs" *
		  doctest::test_suite("ScriptGenerator"))
{
	glob2test::HeadlessGlobals globals;
	auto error = run(R"(
        const k=c.toolkit;
        const t=k.Tessellation.squareTessellation(64,64,16);
        const want=t.distance2(0,1);
        const g=k.GraphMaze.cellGraph(k.Tessellation.squareTessellation(64,64,16));
        if(g.distance2(0,1)!==want)throw Error('graph dependency');
        const literal=k.GraphMaze.cellGraph({cells:t.cells,edges:t.edges,corners:t.corners,t:t.t,shape:t.shape,columns:t.columns,rows:t.rows});
        if(literal.distance2(0,1)!==want)throw Error('literal graph dependency');
        const tree=k.RecursiveGeometry.partitionRegions({x0:0,y0:0,x1:64,y1:64},2,1,1,undefined);
        if(!tree.regions.length)throw Error('optional stop');
        k.HierarchicalCrossings.selectCrossings(c.torus,0,[],[],[],0,0,0,1);
        const m=c.mask(c.torus.size(),1);let called=0;
        const pick=k.Points.farthestSites(c.torus,m,m,2,'prefer',1,i=>{called++;return true;},0);
        if(!called||pick.result.length!==2||pick.rejected!==0)throw Error('callback pointer');
        const symmetry=k.Orbits.translationSymmetry(64,64,2,0);
        if(!(symmetry.spacing2>0))throw Error('output pointer');
    )");
	INFO(error);
	CHECK(error.empty());
}
TEST_CASE("Native scratch tables respect the allocation budget" *
		  doctest::test_suite("ScriptGenerator"))
{
	auto crossings = run(R"(
        const t=c.torus,m=c.mask(),labels=c.integers();
        for(let y=0;y<t.h;y+=3)for(let x=0;x<t.w;x+=3)m.set(t.at(x,y),1);
        c.toolkit.Channels.crossingsPerLabel(t,m,labels,1000000);
    )");
	INFO(crossings);
	CHECK(crossings.find("memory") != std::string::npos);
	auto territories = run(R"(
        const seeds=Array.from({length:10000},()=>[0]);
        c.toolkit.Territories.growTerritories(c.torus,c.mask(c.torus.size(),1),seeds,i=>0);
    )");
	INFO(territories);
	CHECK(territories.find("memory") != std::string::npos);
	auto pairs = run(R"(
        const sites=Array.from({length:10000},()=>({x:0,y:0}));
        c.toolkit.Points.packLandforms(c.torus,sites,{headingSteps:0,stretchPercent:100},[],1,0,10);
    )");
	INFO(pairs);
	CHECK(pairs.find("memory") != std::string::npos);
	auto copies = run(R"(
        const t=c.toolkit.Tessellation.squareTessellation(256,256,8);
        while(true)c.toolkit.Tessellation.Tessellation(t);
    )");
	INFO(copies);
	CHECK(copies.find("budget") != std::string::npos);
	auto constructors = run("for(let i=0;i<20000;i++)c.toolkit.Noise.GenerationNoise(1);");
	INFO(constructors);
	CHECK(constructors.find("budget") != std::string::npos);
}
TEST_CASE("Native work and interpreter resource failures are sticky" *
		  doctest::test_suite("ScriptGenerator"))
{
	auto p = JSGen::Package::parse(package("export function generate(){}"));
	GenerationRequest request;
	GenerationContext context(request);
	JSGen::Binding runtime(*p, request, nullptr, &context, false, false);
	CHECK_THROWS_AS(runtime.charge(runtime.fuel + 1), JSGen::ResourceError);
	CHECK_THROWS_AS(runtime.charge(0), JSGen::ResourceError);
	CHECK_THROWS_AS(runtime.chargeNative(0), JSGen::ResourceError);
	CHECK_THROWS_AS(runtime.allocate(129 * 1024 * 1024), JSGen::ResourceError);
	auto error = run("while(true){}");
	INFO(error);
	CHECK(!error.empty());
}
TEST_CASE("Script terrain and colonies go through engine world validation" *
		  doctest::test_suite("ScriptGenerator"))
{
	glob2test::HeadlessGlobals globals;
	auto p = JSGen::Package::parse(package(R"(export function generate(c){
        const t=c.torus, terrain=c.mask(t.size(),2);
        c.toolkit.Sketch.writeVertices(terrain);
        c.addTeams();
        if(!c.toolkit.Pipeline.settleColonies("test",team=>terrain,team=>({x:32+team*32,y:32})))return 'Cannot place colonies';
        c.toolkit.Pipeline.secureStartingCrops(t);
    })"));
	auto entries = GeneratorRegistry::builtins().entries();
	entries.push_back(p->definition(1000000));
	GeneratorRegistry registry(std::move(entries));
	GenerationService service(registry);
	GenerationRequest request;
	request.setMethodDefaults(1000000, registry);
	request.seed = 91;
	Game first(nullptr), second(nullptr);
	auto a = service.generate(first, request, true);
	INFO(a.diagnostic());
	CHECK(a);
	auto b = service.generate(second, request);
	INFO(b.diagnostic());
	CHECK(b);
	if (a && b)
		CHECK(mapFingerprint(first) == mapFingerprint(second));
	auto bad = JSGen::Package::parse(package("export function generate(c){}"));
	GeneratorRegistry broken({bad->definition(1000000)});
	Game invalid(nullptr);
	auto refused = GenerationService(broken).generate(invalid, request);
	CHECK(refused.error == GenerationError::InvalidWorld);
	auto blankJson = Json::parse(package(
		"export function "
		"generate(c){c.addTeams();c.toolkit.Sketch.writeVertices(c.mask(c.torus.size(),2));}"));
	blankJson["manifest"]["editorOnly"] = true;
	blankJson["manifest"]["hasStartingColonies"] = false;
	auto blank = JSGen::Package::parse(blankJson.dump());
	GeneratorRegistry blanks({blank->definition(1000000)});
	GenerationRequest blankRequest;
	blankRequest.setMethodDefaults(1000000, blanks);
	Game empty(nullptr);
	CHECK(GenerationService(blanks).generate(empty, blankRequest));
	CHECK(empty.mapHeader.getNumberOfTeams() == 1);
}

TEST_CASE("Script resource placement uses the world catalog and bounded brushes" *
		  doctest::test_suite("ScriptGenerator"))
{
	glob2test::HeadlessGlobals globals;
	Game game(nullptr);
	game.map.setSize(6, 6);
	GenerationRequest request;
	request.wDec = 6;
	request.hDec = 6;
	GenerationContext context(request);
	const auto invoke = [&](const std::string &body)
	{
		auto p = JSGen::Package::parse(package("export function generate(c){" + body + "}"));
		return JSGen::invoke(*p, "generate", request, &game, &context);
	};
	auto error = invoke("c.toolkit.Sketch.writeVertices(c.mask(c.torus.size(),2));c.setResource(0,"
						"c.resourceType('rice'),0);");
	INFO(error);
	CHECK(error.empty());
	auto rice = game.map.resourceRegistry().find("rice");
	REQUIRE(rice);
	CHECK(resourceIndex(*rice) >= 8);
	CHECK(game.map.getResource(0, 0).type == resourceIndex(*rice));
	CHECK_THROWS(invoke("c.setResource(0,c.resourceType('rice'),64);"));
	CHECK_THROWS(invoke("c.setResource(0,65535,0);"));
	CHECK_THROWS(invoke("c.resourceType('absent:resource');"));
}
TEST_CASE("Request immutability RNG streams and module isolation" *
		  doctest::test_suite("ScriptGenerator"))
{
	CHECK(run(R"(
        if(typeof Date!=='undefined'||typeof fetch!=='undefined'||typeof setTimeout!=='undefined')throw Error('unexpected host API');
        if(!Object.isFrozen(c.request)||!Object.isFrozen(c.request.options))throw Error('mutable request');
        const a=c.stream('test').next();if(!Number.isInteger(a))throw Error('RNG');
        const seen=[];c.shuffle(seen,'empty');
        if(c.cachedDesign('one',()=>42)!==42||c.cachedDesign('one',()=>0)!==42)throw Error('cache');
        const obj=c.toolkit.Solve.Objective();obj.add('term',2,3);
        if(obj.terms().get(0).name!=='term')throw Error('objective terms');
    )")
			  .empty());
	CHECK(!run("c.toolkit.LatticeNoise.periodicNoise(0,64,8,c.stream('invalid'));").empty());
	auto parsed = Json::parse(package(
		"let n=0;export function generate(){if(++n!==1)throw Error('shared module state');}"));
	parsed["modules"]["helper.js"] = "export const value=42;";
	parsed["modules"]["generator.js"] = "import {value} from './helper.js';let n=0;export function "
										"generate(){if(value!==42||++n!==1)throw Error('state');}";
	auto p = JSGen::Package::parse(parsed.dump());
	GenerationRequest r;
	GenerationContext c(r);
	CHECK(JSGen::invoke(*p, "generate", r, nullptr, &c).empty());
	CHECK(JSGen::invoke(*p, "generate", r, nullptr, &c).empty());
	CHECK(JSGen::Package::parse(p->canonical)->hash == p->hash);
}
TEST_CASE("Custom preferences persist string identity and tolerate missing packages" *
		  doctest::test_suite("ScriptGenerator"))
{
	RestoreCatalog restore;
	Online::MemoryStorage storage;
	JSGen::Library library(storage);
	library.put(package("export function generate(){}"));
	library.publish();
	CustomGamePreferences prefs;
	prefs.setup.generator.setMethodDefaults(GeneratorRegistry::active().idOf("test:generator"));
	const auto text = prefs.encode();
	CHECK(text.find("test:generator") != std::string::npos);
	CustomGamePreferences restored;
	REQUIRE(restored.decode(text));
	CHECK(std::string(restored.setup.generator.definition().id) == "test:generator");
	library.remove("test:generator");
	library.publish();
	CustomGamePreferences missing;
	REQUIRE(missing.decode(text));
	CHECK(missing.setup.generator.definition().apiVersion == 0);
}
TEST_CASE("Optional JavaScript examples generate repeatable validated worlds [slow]" *
		  doctest::test_suite("ScriptGenerator"))
{
	glob2test::HeadlessGlobals globals;
	for (const char *name : {"swamp", "forts", "even-ground"})
	{
		auto p = JSGen::Package::load(
			(glob2test::sourceRoot() / "data/generators/examples" / name).string());
		auto entries = GeneratorRegistry::builtins().entries();
		entries.push_back(p->definition(1000000));
		GeneratorRegistry registry(std::move(entries));
		GenerationRequest request;
		request.setMethodDefaults(1000000, registry);
		request.seed = 91;
		Game first(nullptr), second(nullptr);
		const auto a = GenerationService(registry).generate(first, request, true);
		INFO(name);
		INFO(a.diagnostic());
		REQUIRE(a);
		const auto b = GenerationService(registry).generate(second, request);
		INFO(b.diagnostic());
		REQUIRE(b);
		CHECK(mapFingerprint(first) == mapFingerprint(second));
		CHECK(a.packageHash == p->hash);
		CHECK(a.apiVersion == 1);
	}
}

TEST_CASE("Frozen controls survive package replacement and catalogs are released" *
		  doctest::test_suite("ScriptGenerator"))
{
	RestoreCatalog restore;
	Online::MemoryStorage storage;
	JSGen::Library library(storage);
	auto j = Json::parse(package("export function generate(){}"));
	j["manifest"]["controls"] = {{{"id", "old-control"},
								  {"label", "Old"},
								  {"minimum", 0},
								  {"maximum", 10},
								  {"default", 5},
								  {"searchRange", {0, 10}}}};
	library.put(j.dump());
	library.publish();
	CustomGamePreferences prefs;
	prefs.setup.generator.setMethodDefaults(GeneratorRegistry::active().idOf("test:generator"));
	std::weak_ptr<const GeneratorRegistry> prior = prefs.setup.generator.catalog;
	j["manifest"]["revision"] = 2;
	j["manifest"]["controls"][0]["id"] = "new-control";
	library.put(j.dump(), "test:generator");
	library.publish();
	CHECK(prefs.encode().find("old-control 5") != std::string::npos);
	CHECK(prefs.encode().find("new-control") == std::string::npos);
	prefs.setup.generator.setMethodDefaults(GeneratorRegistry::active().idOf("test:generator"));
	CHECK(prior.expired());
}

TEST_CASE("Conversion failures release intermediate QuickJS containers" *
		  doctest::test_suite("ScriptGenerator"))
{
	auto p = JSGen::Package::parse(package("export function generate(){}"));
	GenerationRequest r;
	GenerationContext c(r);
	JSGen::Binding runtime(*p, r, nullptr, &c, false, false);
	Script::JSValueOwner data(runtime.ctx, runtime.write(std::vector<int>(16, 3)));
	Script::JSValueOwner fn(runtime.ctx, JS_GetPropertyStr(runtime.ctx, data.get(), "toArray"));
	runtime.fuel = 1;
	Script::JSValueOwner result(runtime.ctx,
								JS_Call(runtime.ctx, fn.get(), data.get(), 0, nullptr));
	CHECK(JS_IsException(result.get()));
	CHECK(runtime.exhausted);
}

TEST_CASE("Installed generator library writes to a fresh user profile [writes-preferences]" *
		  doctest::test_suite("ScriptGenerator"))
{
	glob2test::HeadlessGlobals globals;
	auto storage = Online::makeUserDirectoryStorage();
	JSGen::Library library(*storage);
	library.put(package("export function generate(){}"));
	JSGen::Library loaded(*storage);
	CHECK(loaded.entries().contains("test:generator"));
	loaded.remove("test:generator");
}

TEST_CASE("Frozen selections retain defaults and rules after package removal" *
		  doctest::test_suite("ScriptGenerator"))
{
	RestoreCatalog restore;
	Online::MemoryStorage storage;
	JSGen::Library library(storage);
	auto source = Json::parse(package("export function generate(){}"));
	source["manifest"]["controls"] = {{{"id", "layout"},
									   {"label", "Layout"},
									   {"minimum", 0},
									   {"maximum", 10},
									   {"default", 5},
									   {"searchRange", {0, 10}}}};
	library.put(source.dump());
	library.publish();
	const auto local = CustomGameSetup::landscapeCatalog(false);
	const auto online = CustomGameSetup::landscapeCatalog(true);
	const auto id = local->idOf("test:generator");
	CHECK(online->find(id) == nullptr);
	CHECK(online->entries().size() == GeneratorRegistry::builtins().entries().size());
	CustomGameSetup setup;
	setup.generator.setMethodDefaults(id, local);
	setup.generator.control("layout").set(setup.generator, 9);
	library.remove("test:generator");
	library.publish();
	GenerationRequest reset;
	reset.setMethodDefaults(id, setup.generator.catalogSnapshot());
	CHECK(reset.option("layout") == 5);
	CHECK(reset.catalog == local);
	CHECK(setup.generator.control("layout").get(setup.generator) == 9);
	const auto *workers = CustomGameRules::find("workers");
	REQUIRE(workers);
	CHECK_NOTHROW(workers->set(setup, 3));
	CHECK(workers->get(setup) == 3);
	CHECK(CustomGameRules::minimum(*workers, setup) == 1);
	CHECK(CustomGameRules::maximum(*workers, setup) == 8);
	CHECK(CustomGameSetup::landscapeCatalog(false)->find(id) == nullptr);
	CHECK(std::string(setup.generator.definition().id) == "test:generator");
}

TEST_CASE("Library mutations retain memory and disk when storage throws" *
		  doctest::test_suite("ScriptGenerator"))
{
	struct ThrowingStorage : Online::MemoryStorage
	{
		bool throwWrites = false;
		bool write(const std::string &path, const std::string &bytes) override
		{
			if (throwWrites)
				throw std::runtime_error("Storage unavailable");
			return MemoryStorage::write(path, bytes);
		}
	} storage;
	JSGen::Library library(storage);
	library.put(package("export function generate(){}"));
	const auto initial = library.checkpoint();
	library.put(package("export function generate(){return 'updated';}", 2), "test:generator");
	const auto updated = library.checkpoint();
	const auto files = storage.files;
	storage.throwWrites = true;
	CHECK_THROWS(library.remove("test:generator"));
	CHECK(library.checkpoint() == updated);
	CHECK_THROWS(library.rollback(initial));
	CHECK(library.checkpoint() == updated);
	CHECK_THROWS(library.put(package("export function generate(){}", 3), "test:generator"));
	CHECK(library.checkpoint() == updated);
	CHECK(storage.files == files);
	storage.throwWrites = false;
	library.rollback(initial);
	CHECK(library.checkpoint() == initial);
}

TEST_CASE("Online generator installation verifies hashes and persists provenance transactionally" *
		  doctest::test_suite("ScriptGenerator"))
{
	Online::MemoryStorage storage;
	JSGen::Library library(storage);
	auto p = JSGen::Package::parse(package("export function generate(){}"));
	JSGen::LibraryOrigin origin{"https://example.test", "11111111-1111-4111-8111-111111111111",
								"22222222-2222-4222-8222-222222222222", p->hash, p->hash};
	library.put(p->canonical, "", &origin);
	JSGen::Library restored(storage);
	REQUIRE(restored.origins().contains(p->id));
	CHECK(restored.origins().at(p->id).versionId == origin.versionId);
	const auto checkpoint = library.checkpoint();
	auto bad = origin;
	bad.fileHash = std::string(64, '0');
	CHECK_THROWS(library.put(p->canonical, p->id, &bad));
	CHECK(library.checkpoint() == checkpoint);
	library.put(package("export function generate(){return 'refused';}", 2), p->id);
	CHECK(library.origins().empty());
	library.rollback(checkpoint);
	CHECK(library.origins().at(p->id).versionId == origin.versionId);
}

TEST_CASE("Shared generated worlds continue identically without installed generator code [golden]" *
		  doctest::test_suite("ScriptGenerator"))
{
	glob2test::HeadlessGlobals globals;
	auto p = JSGen::Package::parse(package(R"(export function generate(c){
        const t=c.torus, terrain=c.mask(t.size(),2);
        c.toolkit.Sketch.writeVertices(terrain);c.addTeams();
        if(!c.toolkit.Pipeline.settleColonies("shared",team=>terrain,team=>({x:24+team*48,y:24})))return 'Cannot place colonies';
        c.toolkit.Pipeline.secureStartingCrops(t);
    })"));
	GeneratorRegistry registry({p->definition(1000000)});
	std::string trace;
	for (const unsigned seed : {19u, 91u})
	{
		GenerationRequest request;
		request.setMethodDefaults(1000000, registry);
		request.seed = seed;
		request.wDec = 7;
		request.hDec = 6;
		request.nbTeams = 2;
		Game first(nullptr), repeat(nullptr);
		REQUIRE(GenerationService(registry).generate(first, request));
		REQUIRE(GenerationService(registry).generate(repeat, request));
		REQUIRE(mapFingerprint(first) == mapFingerprint(repeat));
		trace +=
			"world " + std::to_string(seed) + " " + std::to_string(mapFingerprint(first)) + "\n";
		GameHeader header;
		header.setNumberOfPlayers(2);
		header.setRandomSeed(seed);
		for (int team = 0; team < 2; ++team)
			header.getBasePlayer(team) = BasePlayer(team, "Shared map", team, BasePlayer::P_LOCAL);
		first.setGameHeader(header, true);
		first.setWaitingOnMask(0);
		for (int tick = 0; tick < 64; ++tick)
			first.syncStep(0);
		auto *memory = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(memory);
		first.save(&output, false, "Shared generated world");
		output.flush();
		const auto bytes = memory->takeContents();
		glob2test::writeFile(glob2test::artifactDir() / ("seed-" + std::to_string(seed) + ".game"),
							 bytes);
		Game restored(nullptr);
		GAGCore::BinaryInputStream input(
			new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
		input.seekFromStart(0);
		REQUIRE(restored.load(&input));
		restored.setWaitingOnMask(0);
		auto components = [](Game &g)
		{
			std::vector<Uint32> state, buildings, units;
			g.checkSum(&state, &buildings, &units, true);
			state.erase(state.begin());
			state.insert(state.end(), buildings.begin(), buildings.end());
			state.insert(state.end(), units.begin(), units.end());
			return state;
		};
		for (int tick = 0; tick < 256; ++tick)
		{
			CAPTURE(seed); CAPTURE(tick);
			first.syncStep(0);
			restored.syncStep(0);
			REQUIRE(components(first) == components(restored));
			REQUIRE(first.syncRandom == restored.syncRandom);
			trace += std::to_string(seed) + " " + std::to_string(tick + 65);
			for (const auto value : components(first))
				trace += " " + std::to_string(value);
			trace += "\n";
		}
	}
	glob2test::writeFile(glob2test::artifactDir() / "shared-generators.trace", trace);
	glob2test::expectGolden("generators/shared-generator-trace.sha256", Online::Sha256::hex(trace) + "\n");
}
