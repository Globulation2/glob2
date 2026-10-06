// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GameDiagnostics.h"
#include "DatasetWriter.h"
#include "Order.h"
#include "ai/model/BuildingProjection.h"
#include "MetricCatalog.h"
#include "render/scene/SceneExtract.h"
#include "AIMaximaPlacement.h"
#include "AIMaxima.h"
#include "AI.h"
#include "Player.h"
#include <nlohmann/json.hpp>
#include <SDL3_image/SDL_image.h>
#include <fstream>
#include <limits>
#include <BinaryStream.h>
#include <StreamBackend.h>

TEST_SUITE("GameDiagnostics")
{
TEST_CASE("field reader rejects malformed counts and retains unsigned food values [artifacts]")
{
	const auto path=(glob2test::artifactDir()/"input.field").string();
	const auto write=[&](const std::string& text) { std::ofstream out(path); out<<text; out.close(); REQUIRE(out.good()); };
	write("2 1\n-2147483648 4294967295\n");
	const auto field=MapRender::readField(path);
	CHECK(field.values[1] == 4294967295LL);
	CHECK(field.values[0] == -2147483648LL);
	for (const auto& text : {"0 1", "2147483647 2147483647", "2 1 1", "1 1 1 2", "1 1 1 junk", "1 1 9223372036854775808"})
	{
		write(text); CHECK_THROWS_AS(MapRender::readField(path),std::invalid_argument);
	}
	CHECK(MapRender::alpha(-1,1)==0);
	CHECK(MapRender::alpha(0,0)==0);
	CHECK(MapRender::alpha(std::numeric_limits<std::int64_t>::max(),std::numeric_limits<std::int64_t>::max())==220);
	CHECK(MapRender::alpha(1,2)==110);
}
TEST_CASE("controller capture follows simulation cadence and preserves unsigned values")
{
	GameDiagnostics::FieldSink sink;
	sink.interval=10; sink.enabled=true;
	AIMaximaPlacement::WorldState world; world.reset(2,1); world.tick=7;
	for (auto& field:sink.fields) {field.width=2;field.height=1;field.values.resize(2);}
	world.tiles[0].foodOpportunity=std::numeric_limits<uint32_t>::max();
	sink.capture(world);
	REQUIRE(sink.captured); CHECK(sink.plannerTick==7); CHECK(sink.nextTick==10);
	CHECK(sink.fields[2].values[0]==4294967295LL);
	sink.captured=false; sink.tick=9; sink.capture(world); CHECK_FALSE(sink.captured);
	sink.tick=10; sink.capture(world); CHECK(sink.captured);
}
TEST_CASE("private diagnostic reservation survives owner ticks and serialization [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.loadDefaultRace=true});
    GameHeader header;header.setNumberOfPlayers(1);
    header.getBasePlayer(0)=BasePlayer(0,"diagnostics",0,BasePlayer::playerTypeFromImplementationID(AI::MAXIMA));
    fixture.game.setGameHeader(header);
    GameDiagnostics::Session session(fixture.game,(glob2test::artifactDir()/"private-captures").string(),10,false);
    auto* maxima=dynamic_cast<AIMaxima::Maxima*>(fixture.game.players[0]->ai->aiImplementation);
    REQUIRE(maxima);
    const auto stable=maxima->fieldDiagnostics;
    session.beginTick(fixture.game);
    auto clone=session.reserveCapture(0,0);
    REQUIRE(clone);CHECK(clone!=stable);CHECK_FALSE(stable->enabled);
    CHECK_FALSE(session.reserveCapture(0,0));
    fixture.game.stepCounter=8;session.beginTick(fixture.game);
    CHECK(clone->tick==0);CHECK(clone->enabled);
    AIMaximaPlacement::WorldState state;state.reset(fixture.game.map.getW(),fixture.game.map.getH());state.tick=23;
    state.tiles[0].foodOpportunity=std::numeric_limits<std::uint32_t>::max();
    clone->capture(state);REQUIRE(clone->captured);CHECK_FALSE(stable->captured);
    clone->fields[0].values[0]=std::numeric_limits<std::int64_t>::min();
    clone->fields[1].values[0]=std::numeric_limits<std::int64_t>::max();
    auto* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream out(backend);clone->save(&out);out.flush();
    const auto bytes=backend->takeContents();
    GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));in.seekFromStart(0);
    GameDiagnostics::FieldSink restored;REQUIRE(restored.load(&in));
    CHECK(restored.tick==0);CHECK(restored.nextTick==10);CHECK(restored.plannerTick==23);
    CHECK(restored.fields[0].values[0]==std::numeric_limits<std::int64_t>::min());
    CHECK(restored.fields[1].values[0]==std::numeric_limits<std::int64_t>::max());
    CHECK(restored.fields[2].values[0]==4294967295LL);
    {
        const size_t fieldBytes=size_t(fixture.game.map.getW())*fixture.game.map.getH()*5*sizeof(std::int64_t);
        GameDiagnostics::Session resumed(fixture.game,(glob2test::artifactDir()/"adopt-private").string(),10,false,2*fieldBytes);
        REQUIRE(resumed.adoptCapture(restored));
        CHECK(resumed.adoptCapture(restored)); // idempotent setup registration
        auto another=restored;another.tick=1;
        CHECK_FALSE(resumed.adoptCapture(another));
        resumed.cancelCaptures(0);
        CHECK(resumed.adoptCapture(another));
        resumed.cancelCaptures(0);
    }
    maxima->fieldDiagnostics=stable;
    session.publishCapture(restored);CHECK(stable->captured);CHECK(stable->tick==0);
    restored.fields[0].values[0]=5;CHECK(stable->fields[0].values[0]!=5);
    session.completeTick(fixture.game);REQUIRE(session.pending());session.drain();
    fixture.game.stepCounter=9;session.beginTick(fixture.game);CHECK_FALSE(session.reserveCapture(0,9));
    fixture.game.stepCounter=10;session.beginTick(fixture.game);CHECK(session.reserveCapture(0,10)!=nullptr);
    CHECK(maxima->fieldDiagnostics==stable);
    // Declared counts cannot bypass the bounded field layout on reload.
    restored.fields[0].width=1;
    auto* invalidBackend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream bad(invalidBackend);restored.save(&bad);bad.flush();
    const auto invalid=invalidBackend->takeContents();
    GAGCore::BinaryInputStream malformed(new GAGCore::MemoryStreamBackend(invalid.data(),invalid.size()));malformed.seekFromStart(0);
    CHECK_FALSE(restored.load(&malformed));
}
TEST_CASE("diagnostic budget counts private captures across the delay horizon [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.loadDefaultRace=true});
    GameHeader header;header.setNumberOfPlayers(1);
    header.getBasePlayer(0)=BasePlayer(0,"diagnostics",0,BasePlayer::playerTypeFromImplementationID(AI::MAXIMA));
    fixture.game.setGameHeader(header);
    const size_t bytes=32*32*5*sizeof(std::int64_t);
    GameDiagnostics::Session session(fixture.game,(glob2test::artifactDir()/"bounded-private").string(),1,false,2*bytes);
    session.beginTick(fixture.game);
    auto first=session.reserveCapture(0,0);REQUIRE(first);
    // An interval shorter than the eight-tick scheduling horizon cannot admit
    // another full field allocation while the first private output is held.
    fixture.game.stepCounter=1;session.beginTick(fixture.game);
    CHECK_FALSE(session.reserveCapture(0,1));
    session.completeTick(fixture.game);REQUIRE(session.pending());session.drain();
    session.publishCapture(*first);first.reset(); // an uncaptured invocation releases its reservation
    fixture.game.stepCounter=2;session.beginTick(fixture.game);
    auto second=session.reserveCapture(0,2);REQUIRE(second);
    second.reset(); // cancellation joins the stream before discarding its output
    session.cancelCaptures(0);
    fixture.game.stepCounter=3;session.beginTick(fixture.game);
    CHECK(session.reserveCapture(0,3)!=nullptr);
}
TEST_CASE("controllers sharing a team publish distinct complete captures and recover after failures [artifacts]")
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.loadDefaultRace=true});
	GameHeader header;header.setNumberOfPlayers(2);header.setRandomSeed(19);
	for (int p=0;p<2;++p)
		header.getBasePlayer(p)=BasePlayer(p,"diagnostics",0,BasePlayer::playerTypeFromImplementationID(AI::MAXIMA));
	world.game.setGameHeader(header);
	const auto path=glob2test::artifactDir()/"captures";
	std::filesystem::remove_all(path);std::filesystem::create_directories(path);
	{std::ofstream blocked(path/"diagnostics");REQUIRE(blocked);blocked<<"blocked";blocked.close();REQUIRE(blocked.good());}
	GameDiagnostics::Session session(world.game,(path/"diagnostics").string(),10,false);
	const auto capture=[&] {
		session.beginTick(world.game);
		AIMaximaPlacement::WorldState state;state.reset(world.game.map.getW(),world.game.map.getH());
		for (int p=0;p<2;++p)
		{
			auto* maxima=dynamic_cast<AIMaxima::Maxima*>(world.game.players[p]->ai->aiImplementation);
			REQUIRE(maxima);REQUIRE(maxima->fieldDiagnostics);maxima->fieldDiagnostics->capture(state);
		}
		session.completeTick(world.game);REQUIRE(session.pending());session.drain();CHECK_FALSE(session.pending());
	};
	capture();CHECK(std::filesystem::is_regular_file(path/"diagnostics"));
	std::filesystem::remove(path/"diagnostics");world.game.stepCounter=10;capture();session.finish();
	CHECK(std::filesystem::exists(path/"diagnostics/tick-0000010.player0.team0/capture.json"));
	CHECK(std::filesystem::exists(path/"diagnostics/tick-0000010.player1.team0/capture.json"));
	std::ifstream summary(path/"diagnostics/summary.json");nlohmann::json status;summary>>status;
	CHECK(status["completed"]==2);CHECK(status["failed"]==2);
	// A smaller injected budget exercises the same production admission path.
	GameDiagnostics::Session limited(world.game,(path/"limited").string(),10,false,1024);
	world.game.stepCounter=20;
	limited.beginTick(world.game);limited.completeTick(world.game);REQUIRE(limited.pending());limited.drain();limited.finish();
	std::ifstream final(path/"limited/summary.json");final>>status;CHECK(status["skipped"]==2);
}
TEST_CASE("capped scene export survives repeated graphics lifetimes and write failures [artifacts]")
{
	for (int lifetime=0; lifetime<2; ++lifetime)
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.wDec=5,.hDec=4,.teams=2,.discovered=true,.loadDefaultRace=true});
		REQUIRE(world.addBuilding("inn",4,4)); REQUIRE(world.addUnit(WORKER,12,8));
		world.game.map.setResource(18,10,WHEAT,1);
		Scene scene; SceneRequest request; request.includePanels=false;
		extractScene(world.game,request,scene);
		const auto checksum=world.game.checkSum();
		const auto path=(glob2test::artifactDir()/("render-"+std::to_string(lifetime)+".png")).string();
		MapRender::toPng(scene,path,128);
		SDL_Surface* png=IMG_Load(path.c_str()); REQUIRE(png);
		CHECK(png->w==128); CHECK(png->h==64); SDL_DestroySurface(png);
		auto* gfx=globals->gfx; auto* surface=gfx->getSDLSurface(); const auto window=gfx->windowID();
		gfx->setClipRect(11,12,30,20);
		MapRender::Field wrong;wrong.width=1;wrong.height=1;wrong.values={1};
		CHECK_THROWS(MapRender::toPng(scene,path,128,&wrong));
		CHECK_THROWS(MapRender::toPng(scene,path+"/bad.png",128));
		CHECK_THROWS(MapRender::toPng(scene,path,0));
		CHECK_THROWS(MapRender::toPng(scene,path,8193));
		CHECK(gfx->getSDLSurface()==surface); CHECK(gfx->windowID()==window);
		int x,y,w,h;gfx->getClipRect(&x,&y,&w,&h);CHECK(x==11);CHECK(y==12);CHECK(w==30);CHECK(h==20);
		CHECK(world.game.checkSum()==checksum);
	}
}
TEST_CASE("portrait exports cover the whole map after dimension rounding [artifacts]")
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.wDec=4,.hDec=6,.teams=1,.discovered=true,.loadDefaultRace=true});
	Scene scene;SceneRequest request;request.includePanels=false;extractScene(world.game,request,scene);
	const auto path=(glob2test::artifactDir()/"portrait.png").string();
	MapRender::toPng(scene,path,9);
	SDL_Surface* image=IMG_Load(path.c_str());REQUIRE(image);
	CHECK(image->w==2);CHECK(image->h==9);
	Uint8 red,green,blue,alpha;
	REQUIRE(SDL_ReadSurfacePixel(image,0,8,&red,&green,&blue,&alpha));CHECK(alpha==255);
	SDL_DestroySurface(image);
}
TEST_CASE("offscreen pass restores transformed drawing after exceptions [display]")
{
	for (const auto flags:{0u,unsigned(GAGCore::GraphicContext::PORTABLEGPU),unsigned(GAGCore::GraphicContext::USEGPU)})
	{
#ifndef HAVE_OPENGL
		if(flags==GAGCore::GraphicContext::USEGPU)continue;
#endif
		GAGCore::GraphicContext gfx(320,240,flags,"Offscreen lifetime");
		auto* surface=gfx.getSDLSurface();const auto generation=gfx.getGLContextGeneration();
		gfx.setClipRect(11,12,30,20); gfx.setUITransform(1.25f,2,3,nullptr);
		SDL_Surface* target=SDL_CreateSurface(32,32,SDL_PIXELFORMAT_RGBA32);REQUIRE(target);
		REQUIRE(SDL_FillSurfaceRect(target,nullptr,0));
		gfx.drawToSurface(target,0.5f,[&]{gfx.drawFilledRect(0,0,32,32,255,0,0);});
		CHECK_THROWS(gfx.drawToSurface(target,0.5f,[&]{throw std::runtime_error("test failure");}));
		Uint8 red,green,blue,alpha;
		REQUIRE(SDL_ReadSurfacePixel(target,15,15,&red,&green,&blue,&alpha));CHECK(red==255);CHECK(green==0);
		REQUIRE(SDL_ReadSurfacePixel(target,17,17,&red,&green,&blue,&alpha));CHECK(red==0);
		CHECK(gfx.getSDLSurface()==surface);CHECK(gfx.getGLContextGeneration()==generation);CHECK(gfx.getOptionFlags()==flags);
		gfx.setUITransform();int x,y,w,h;gfx.getClipRect(&x,&y,&w,&h);CHECK(x==11);CHECK(y==12);CHECK(w==30);CHECK(h==20);
		gfx.setClipRect();gfx.drawFilledRect(0,0,10,10,0,255,0);gfx.nextFrame();
		SDL_DestroySurface(target);
	}
}
}

namespace
{
struct DatasetFixtureReader
{
    std::string bytes;
    size_t cursor=0;
    Uint32 number(unsigned width)
    {
        REQUIRE(cursor+width<=bytes.size());
        Uint32 value=0;
        for(unsigned i=0;i<width;++i)value|=Uint32(static_cast<unsigned char>(bytes[cursor++]))<<(8*i);
        return value;
    }
    std::string text(size_t count)
    {
        REQUIRE(cursor+count<=bytes.size());
        const auto result=bytes.substr(cursor,count);cursor+=count;return result;
    }
};
}

TEST_SUITE("DatasetWriter")
{
TEST_CASE("GDS2 embeds the catalog, projects unique model counts and preserves wide concrete spatial IDs [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.teams=2,.header=true});
    auto snapshot=nlohmann::json::parse(world.game.buildingsTypes.snapshotJson());
    auto prototype=snapshot["variants"][world.game.buildingsTypes.getFinishedTypeNum("stonewall")];
    prototype["previous"]="";prototype["next"]="";
    prototype["semantics"]["repairable"]=false;
    prototype["semantics"]["placeable"]=true;
    prototype["semantics"]["instantPlacement"]=true;
    while(snapshot["variants"].size()<=300)
    {
        const auto id=snapshot["variants"].size();auto variant=prototype;
        variant["id"]=id;variant["key"]="dataset.variant."+std::to_string(id);
        variant["properties"]["type"]="dataset-"+std::to_string(id);
        snapshot["variants"].push_back(variant);
    }
    world.game.buildingsTypes.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();
    auto* own=world.game.addBuilding(4,4,300,0,0,0);REQUIRE(own);
    world.game.map.setBuilding(4,4,1,1,own->gid);
    auto* hidden=world.game.addBuilding(8,8,299,1,0,0);REQUIRE(hidden);
    world.game.map.setBuilding(8,8,1,1,hidden->gid);
    auto* stat=world.team->stats.getLatestStat();
    stat->buildingCountByVariant.assign(world.game.buildingsTypes.size(),0);
    stat->buildingCountByVariant[300]=1;
    stat->numberBuildingPerType[0]=99; // legacy metadata must not leak into model counts
    const auto path=(glob2test::artifactDir()/"catalog.dataset").string();
    DatasetWriter writer;REQUIRE(writer.open(path));
    OrderModifyBuilding order(own->gid,0);order.sender=0;
    writer.writeRecord(42,order,world.game);writer.close();
    std::ifstream in(path,std::ios::binary);
    DatasetFixtureReader file{std::string(std::istreambuf_iterator<char>(in),{})};
    CHECK(file.text(4)=="GDS2");CHECK(file.number(4)==1);
    const auto metadata=nlohmann::json::parse(file.text(file.number(4)));
    CHECK(metadata["buildingCatalog"]["hash"]==world.game.buildingsTypes.fingerprint());
    CHECK(metadata["buildingCatalog"]["snapshot"]==nlohmann::json::parse(world.game.buildingsTypes.snapshotJson()));
    CHECK(metadata["modelChannels"].size()==301);
    CHECK(file.number(4)==42);CHECK(file.number(1)==0);CHECK(file.number(1)==order.getOrderType());
    DatasetFixtureReader state{file.text(file.number(4))};
    CHECK(state.number(4)==1);state.number(4);state.number(4);
    for(int i=0;i<MAX_NB_RESOURCES+NB_UNIT_TYPE;++i)state.number(4);
    for(int i=0;i<ModelBuildingProjection::Count;++i)
        CHECK(state.number(4)==(i==ModelBuildingProjection::PassiveGround ? 1u : 0u));
    const auto width=state.number(4),height=state.number(4);
    CHECK(width==32);CHECK(height==32);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
    {
        for(int i=0;i<4;++i)state.number(1);
        const auto mine=state.number(2),enemy=state.number(2);state.number(1);
        CHECK(mine==(x==4 && y==4 ? 301u : 0u));
        CHECK(enemy==0); // undiscovered enemy remains private
    }
    CHECK(state.cursor==state.bytes.size());
    CHECK(file.number(4)==unsigned(order.getDataLength()));
    CHECK(file.text(order.getDataLength())==std::string(reinterpret_cast<const char*>(order.getData()),order.getDataLength()));
    CHECK(file.cursor==file.bytes.size());
}

TEST_CASE("reader migration distinguishes the retained GDS1 cell layout")
{
    // A complete one-record old file: the metadata-less header and two
    // one-byte family values must retain their original meaning.
    const auto u32=[](std::string& out,Uint32 n) {for(int i=0;i<4;++i)out+=char(n>>(8*i));};
    std::string state;u32(state,1);
    for(int i=0;i<2+MAX_NB_RESOURCES+NB_UNIT_TYPE+13;++i)u32(state,0);
    u32(state,1);u32(state,1);
    const unsigned char cell[]={2,5,1,0,13,6,2};
    state.append(reinterpret_cast<const char*>(cell),sizeof(cell));
    std::string bytes="GDS1";u32(bytes,1);u32(bytes,42);
    bytes+=char(0);bytes+=char(ORDER_MODIFY_BUILDING);u32(bytes,state.size());
    bytes+=state;u32(bytes,0);
    DatasetFixtureReader old{bytes};
    CHECK(old.text(4)=="GDS1");CHECK(old.number(4)==1);
    CHECK(old.number(4)==42);CHECK(old.number(1)==0);CHECK(old.number(1)==ORDER_MODIFY_BUILDING);
    DatasetFixtureReader observation{old.text(old.number(4))};
    CHECK(observation.number(4)==1);
    for(int i=0;i<2+MAX_NB_RESOURCES+NB_UNIT_TYPE+13;++i)observation.number(4);
    CHECK(observation.number(4)==1);CHECK(observation.number(4)==1);
    for(int i=0;i<4;++i)observation.number(1);
    CHECK(observation.number(1)==13);CHECK(observation.number(1)==6);CHECK(observation.number(1)==2);
    CHECK(observation.cursor==observation.bytes.size());
    CHECK(old.number(4)==0);CHECK(old.cursor==old.bytes.size());
}

TEST_CASE("per-game metric bands retain labels and count every finished ground variant exactly once")
{
    BuildingsTypes catalog;catalog.initLegacy();
    auto* type=catalog.get(catalog.getFinishedTypeNum("inn"));
    type->presentation.displayName="Combined services";type->shortTypeNum=500;
    const int inn=catalog.getFinishedTypeNum("inn");
    auto metrics=Stats::catalogForBuildings(catalog);
    const auto& buildings=metrics[Stats::findMetric("buildings")];
    GameplayMeasurements sample;sample.variants.resize(catalog.size());sample.variants[inn].count=4;
    double total=0;bool label=false;
    for(const auto& band:buildings.bands) {total+=band.value(sample);label|=band.labelKey.find("Combined services")!=std::string::npos;}
    CHECK(total==4);CHECK(label);
    const auto captured=buildings.bands;
    catalog.initLegacy(); // labels/closures own their values, no descriptor lifetime dependency
    for(size_t i=0;i<captured.size();++i)CHECK(buildings.bands[i].labelKey==captured[i].labelKey);
    size_t completed=0;for(size_t i=0;i<catalog.size();++i)
        completed+=!catalog.get(i)->isBuildingSite && catalog.get(i)->semantics.occupiesGround;
    CHECK(buildings.bands.size()==completed);
}
}
