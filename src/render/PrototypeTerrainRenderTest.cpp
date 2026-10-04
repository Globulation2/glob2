// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "scene/SceneMap.h"
#include "render/SoftwareTerrainCache.h"
#include <SDL3_image/SDL_image.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#include <algorithm>
#include <cstring>
#include <memory>

namespace {
std::vector<Uint8> pixels(bool gpu)
{
    auto *gfx=globalContainer->gfx;
    GAGCore::Sprite::flushBatches(gfx);
    if(gpu) {
        glFinish();GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
        std::vector<Uint8> pixels(viewport[2]*viewport[3]*4);
        glReadPixels(viewport[0],viewport[1],viewport[2],viewport[3],GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        return pixels;
    }
    auto *surface=SDL_ConvertSurface(gfx->getSDLSurface(),SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surface);
    std::vector<Uint8> pixels(surface->w*surface->h*4);
    for(int y=0;y<surface->h;++y)std::memcpy(pixels.data()+y*surface->w*4,static_cast<Uint8*>(surface->pixels)+y*surface->pitch,surface->w*4);
    SDL_DestroySurface(surface);
    return pixels;
}
void run(bool gpu)
{
    glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true,.width=640,.height=480,
        .screenFlags=gpu?Uint32(GAGCore::GraphicContext::USEGPU):0u});
    globals->settings.highResolutionArtwork=false;
    glob2test::HeadlessGame fixture(glob2test::GameOptions{.wDec=5,.hDec=5,.discovered=true});
    auto &map=fixture.game.map;
    // A single corner must render its edge even though its flat IDs are ordinary sand/grass.
    map.setUMatPos(2,2,COBBLESTONE,1);
    map.setUMatPos(7,4,ICE,3);
    map.setUMatPos(12,7,COBBLESTONE,5);
    SceneMap scene;scene.extract(map);
    REQUIRE(scene.hasPrototypeTerrainLayers());
    SoftwareTerrainCache cache;
    CHECK_FALSE(cache.prepare(scene,*globals->terrain,0,0,15,14,0,0,fixture.team->me,true));
    const auto clear=[&](){globals->gfx->setClipRect();globals->gfx->drawFilledRect(0,0,640,480,17,29,41);};
    clear();
    for(int y=0;y<=14;++y)for(int x=0;x<=15;++x) {
        Uint16 layers[3];const int count=map.prototypeTerrainLayers(x,y,layers);
        if(count) for(int n=0;n<count;++n) globals->gfx->drawSprite(x*32,y*32,globals->terrain,layers[n]);
        else {const int id=map.getTerrain(x,y);if(id<256)globals->gfx->drawSprite(x*32,y*32,globals->terrain,id);}
    }
    globals->gfx->finishDrawingSprite(globals->terrain,255);
    const auto expected=pixels(gpu);
    clear();
    fixture.game.drawMapTerrain(0,0,15,14,0,0,0,Game::DRAW_WHOLE_MAP,scene);
    const auto actual=pixels(gpu);
    CHECK(actual==expected);
    CHECK(std::any_of(actual.begin(),actual.end(),[](Uint8 p){return p>200;}));
    if(!gpu) REQUIRE(IMG_SavePNG(globals->gfx->getSDLSurface(),(glob2test::artifactDir()/"prototype-terrain.png").string().c_str()));
    else {
        GLint v[4];glGetIntegerv(GL_VIEWPORT,v);
        std::vector<Uint8> flipped(actual.size());
        for(int y=0;y<v[3];++y)std::copy_n(actual.data()+y*v[2]*4,v[2]*4,flipped.data()+(v[3]-1-y)*v[2]*4);
        auto *surface=SDL_CreateSurfaceFrom(v[2],v[3],SDL_PIXELFORMAT_RGBA32,flipped.data(),v[2]*4);
        REQUIRE(surface);
        REQUIRE(IMG_SavePNG(surface,(glob2test::artifactDir()/"prototype-terrain.png").string().c_str()));SDL_DestroySurface(surface);
        CHECK(glGetError()==GL_NO_ERROR);
    }
}
}
TEST_SUITE("PrototypeTerrainRender") {
TEST_CASE("flat terrain and single-corner overlays match direct software drawing [display][artifacts]"){run(false);}
TEST_CASE("flat terrain and single-corner overlays match direct OpenGL drawing [display][artifacts]"){run(true);}
}
