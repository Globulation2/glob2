// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GameDiagnostics.h"
#include "scene/SceneExtract.h"
#include "AIMaximaPlacement.h"
#include "AIMaxima.h"
#include "AI.h"
#include "Player.h"
#include <nlohmann/json.hpp>
#include <SDL3_image/SDL_image.h>
#include <fstream>
#include <limits>

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
TEST_CASE("controllers sharing a team publish distinct complete captures and recover after failures [artifacts]")
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.loadDefaultRace=true});
	GameHeader header;header.setNumberOfPlayers(2);header.setRandomSeed(19);
	for (int p=0;p<2;++p)
		header.getBasePlayer(p)=BasePlayer(p,"diagnostics",0,BasePlayer::playerTypeFromImplementationID(AI::MAXIMA));
	world.game.setGameHeader(header);
	const auto path=glob2test::artifactDir()/"captures";
	std::filesystem::create_directories(path);{std::ofstream blocked(path/"diagnostics");blocked<<"blocked";}
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
