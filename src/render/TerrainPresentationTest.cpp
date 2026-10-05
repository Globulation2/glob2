// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#include "EngineFixtures.h"
#include "TerrainPresentation.h"
#include "scene/SceneMap.h"
#include "SoftwareTerrainCache.h"
#include "MapThumbnail.h"
#include "MapImage.h"
#include "GenerationRequest.h"
#include <SDL3_image/SDL_image.h>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif
#include <algorithm>
#include <cstring>

namespace {
std::vector<Uint8> terrainPixels(bool gpu)
{
    auto *gfx=globalContainer->gfx;
    GAGCore::Sprite::flushBatches(gfx);
#ifdef HAVE_OPENGL
    if(gpu) {
        glFinish(); GLint viewport[4]; glGetIntegerv(GL_VIEWPORT,viewport);
        std::vector<Uint8> result(viewport[2]*viewport[3]*4);
        glReadPixels(0,0,viewport[2],viewport[3],GL_RGBA,GL_UNSIGNED_BYTE,result.data());
        return result;
    }
#endif
    auto *surface=SDL_ConvertSurface(gfx->getSDLSurface(),SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surface);
    std::vector<Uint8> result(surface->w*surface->h*4);
    for(int y=0;y<surface->h;++y)
        std::memcpy(result.data()+y*surface->w*4,static_cast<Uint8*>(surface->pixels)+y*surface->pitch,surface->w*4);
    SDL_DestroySurface(surface);
    return result;
}
void layeredCache(bool gpu)
{
    glob2test::HeadlessGlobals globals({.display=true,.width=640,.height=480,
        .screenFlags=gpu?Uint32(GAGCore::GraphicContext::USEGPU):0u});
    glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.discovered=true});
    auto &game=fixture.game;
    auto &map=game.map;
    // Exercise every edge mask, torus neighbors, water backdrop, and mixed layers.
    for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
        if(x<3) map.setTerrain(x,y,256);
        if((x+y*3)%7==0) map.setCellTerrain(x,y,ICE);
        else if((x*5+y)%11==0) map.setCellTerrain(x,y,TRAIL);
    }
    SceneMap scene;scene.extract(map);
    CHECK(scene.terrainLayerCapacity()==TerrainLayers::Capacity);
    SoftwareTerrainCache cache;
    auto clear=[&] {
        globals->gfx->setClipRect();
        globals->gfx->drawFilledRect(0,0,640,480,17,29,41);
        game.drawMapWater(640,480,29,30,19);
    };
    const auto compare=[&] {
        clear();
        for(int y=0;y<=14;++y) for(int x=0;x<=19;++x)
        {
            const auto layers=scene.terrainLayersAt(x+29,y+30);
            for(int layer=0;layer<TerrainLayers::Capacity;++layer)
                if(layers.frames[layer]>=0) {
                    auto *sprite=globals->terrainLayerSprite(layers.materials[layer],layers.backdrop[layer]);
                    globals->gfx->drawSprite(x*32,y*32,sprite,layers.frames[layer]);
                    globals->gfx->finishDrawingSprite(sprite,255);
                }
        }
        const auto expected=terrainPixels(gpu);
        clear();
        if(gpu) game.drawMapTerrain(0,0,19,14,29,30,0,Game::DRAW_WHOLE_MAP,scene);
        else {
            REQUIRE(cache.prepare(scene,*globals->terrain,0,0,19,14,29,30,fixture.team->me,true));
            cache.draw(*globals->gfx);
        }
        CHECK(terrainPixels(gpu)==expected);
    };
    const auto checksum=fixture.checksum();
    compare(); compare();
    CHECK(fixture.checksum()==checksum);
    if(!gpu) CHECK(cache.cacheHits()>0);
    const auto before=scene.terrainLayersAt(0,0);
    map.setCellTerrain(0,0,TRAIL);
    CHECK(scene.terrainLayersAt(0,0)==before);
    scene.extract(map);compare();
    map.game = nullptr;
    map.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[{"key":"test:custom","name":"Custom ice","base":"grass","properties":{},"appearance":"ice"}]})");
    const auto custom=*map.terrainRegistry().find("test:custom");
    map.setCellTerrain(0,0,custom);
    scene.extract(map);compare();compare();
    const auto previousRegistry=scene.frozenTerrainRegistry();
    map.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[{"key":"test:custom","name":"Custom road","base":"grass","properties":{},"appearance":"road"}]})");
    CHECK(scene.frozenTerrainRegistry()==previousRegistry);
    CHECK(previousRegistry->appearance(custom)==ICE);
    scene.extract(map);compare();compare();
    CHECK(scene.terrainRegistry().appearance(custom)==TRAIL);
    map.setGame(&game);
    // Swap the loaded asset binding after warming the cache. Frame numbers
    // stay identical: both renderer paths must observe material-owned sprites,
    // including overlays, and reject geometry cached with the previous asset.
    GAGCore::Sprite alternate;
    alternate.images.resize(globals->terrain->getFrameCount());
    alternate.rotated.resize(alternate.images.size(),nullptr);
    // Loaded sprites maintain equally sized native and optional HD layers.
    // The GPU draw path probes both even when this fixture has no HD artwork.
    alternate.experimentImages.resize(alternate.images.size(),nullptr);
    alternate.experimentRotated.resize(alternate.images.size(),nullptr);
    for(auto &frame : alternate.images) {
        frame=new GAGCore::DrawableSurface(32,32);
        frame->drawFilledRect(0,0,32,32,201,23,189);
    }
    auto *original=globals->terrainSprites[ICE];
    globals->terrainSprites[ICE]=&alternate;
    compare();compare();
    globals->terrainSprites[ICE]=original;
    compare();
    if(!gpu) REQUIRE(IMG_SavePNG(globals->gfx->getSDLSurface(),(glob2test::artifactDir()/"terrain-layers.png").string().c_str()));
}
}
TEST_SUITE("TerrainPresentation") {
TEST_CASE("whole-cell layers preserve software terrain caching [display][artifacts]") { layeredCache(false); }
#ifdef HAVE_OPENGL
TEST_CASE("whole-cell layers preserve OpenGL terrain caching [display]") { layeredCache(true); }
#endif
TEST_CASE("tile animation and optional backgrounds use presentation metadata") {
    auto p=terrainPresentation(ICE);
    p.animationFrames=3;p.animationTicks=4;
    p.backdropSprite="test-background";p.backdropFirstFrame=7;p.backdropFrames=2;p.backdropTicks=3;
    for(int time=0;time<24;++time) {
        const auto layers=terrainBaseLayers(ICE,p.firstFrame+2,p,time);
        CHECK(layers.frames[0]==7+(time/3)%2);
        CHECK(layers.backdrop[0]);CHECK(layers.materials[0]==ICE);
        CHECK(layers.frames[1]==p.firstFrame+2+((time/4)%3)*p.variants);
        CHECK_FALSE(layers.backdrop[1]);CHECK(layers.materials[1]==ICE);
    }
}
TEST_CASE("every side mask is decorative and preserves material identity") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.wDec=4,.hDec=4});
    auto &map=fixture.game.map;
    constexpr int dx[4]={0,1,0,-1},dy[4]={-1,0,1,0};
    for(unsigned mask=0;mask<16;++mask) {
        map.setCellTerrain(8,8,TRAIL);
        for(int side=0;side<4;++side) map.setCellTerrain(8+dx[side],8+dy[side],mask&(1u<<side)?ICE:TRAIL);
        SceneMap scene;scene.extract(map);
        const auto layers=scene.terrainLayersAt(8,8);
        CHECK(scene.terrainTypeAt(8,8)==TRAIL);
        CHECK(layers.frames[1]==(mask?terrainPresentation(ICE).edgeFirstFrame+int(mask)-1:-1));
    }
}
TEST_CASE("image import keeps whole-cell material edges out of legacy gameplay [artifacts]") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.wDec=6,.hDec=6,.teams=0});
    GAGCore::DrawableSurface image(64,64);
    const auto paint=[&](int x,int y,int w,int h,TerrainType type) {
        const auto c=terrainPresentation(type).image;
        image.drawFilledRect(x,y,w,h,c.r,c.g,c.b);
    };
    paint(0,0,64,64,GRASS);
    paint(4,4,12,12,WATER); paint(8,8,1,1,TRAIL);
    paint(0,8,3,8,WATER); paint(60,8,4,8,WATER); paint(0,10,1,1,ICE);
    paint(20,10,1,1,TRAIL);
    image.drawFilledRect(45,45,2,2,255,255,255);
    const auto filename=(glob2test::artifactDir()/"terrain-import-edges.png").string();
    REQUIRE(IMG_SavePNG(image.getSDLSurface(),filename.c_str()));
    GenerationRequest request;request.wDec=request.hDec=6;request.nbWorkers=1;request.seed=901;
    MapImageImportReport report;
    importMapImage(fixture.game,filename,request,1,report,0);
    const auto &map=fixture.game.map;
    CHECK(map.terrainTypeAt(8,8)==TRAIL);
    CHECK(map.terrainTypeAt(7,8)==WATER);
    CHECK(map.terrainTypeAt(8,7)==WATER);
    CHECK(map.terrainTypeAt(7,7)==WATER);
    CHECK_FALSE(map.terrainPropertiesAt(7,8).walkable);
    CHECK(map.terrainTypeAt(0,10)==ICE);
    CHECK(map.terrainTypeAt(63,10)==WATER);
    CHECK(map.terrainTypeAt(20,10)==TRAIL);
    CHECK(map.terrainTypeAt(19,10)==GRASS);
    CHECK(map.terrainPropertiesAt(19,10).buildable);
    // An ordinary grass/water boundary still receives the legacy shore repair.
    CHECK(map.terrainTypeAt(15,5)!=WATER);
    fixture.game.map.rebuildTerrain();
    CHECK(map.terrainTypeAt(7,8)==WATER);
    CHECK(map.terrainTypeAt(8,7)==WATER);
    CHECK(map.terrainTypeAt(7,7)==WATER);
    CHECK(map.terrainTypeAt(63,10)==WATER);
    CHECK(map.terrainTypeAt(19,10)==GRASS);
}
TEST_CASE("export uses registered whole-cell material colors [artifacts]") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.teams=0});
    auto &map=fixture.game.map;
    map.setCellTerrain(1,1,ICE);map.setCellTerrain(2,1,TRAIL);
    const auto filename=(glob2test::artifactDir()/"terrain-colors.png").string();
    exportMapImage(fixture.game,filename);
    auto *source=IMG_Load(filename.c_str());REQUIRE(source);
    auto *image=SDL_ConvertSurface(source,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(source);REQUIRE(image);
    for(int x=1;x<=2;++x) {
        const auto c=terrainPresentation(x==1?ICE:TRAIL).image;
        const auto *pixel=static_cast<Uint8*>(image->pixels)+image->pitch+x*4;
        CHECK(pixel[0]==c.r);CHECK(pixel[1]==c.g);CHECK(pixel[2]==c.b);
    }
    SDL_DestroySurface(image);
}
}
