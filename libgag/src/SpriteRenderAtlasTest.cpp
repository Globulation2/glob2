// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <SpriteRenderAtlas.h>
#include <array>

using namespace GAGCore;
namespace {
using Surface=std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)>;
Surface image(int width,int height,SDL_Color color) {
    Surface result(SDL_CreateSurface(width,height,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
    REQUIRE(result);REQUIRE(SDL_FillSurfaceRect(result.get(),nullptr,SDL_MapSurfaceRGBA(result.get(),color.r,color.g,color.b,color.a)));return result;
}
SDL_Color color(SDL_Surface *pixels,int x,int y) {
    SDL_Color value;REQUIRE(SDL_ReadSurfacePixel(pixels,x,y,&value.r,&value.g,&value.b,&value.a));return value;
}
}
TEST_SUITE("SpriteRenderAtlas") {
    TEST_CASE("variable cells preserve straight alpha and edge gutters without source mutation") {
        auto first=image(1,2,{200,10,50,128}),second=image(3,1,{20,180,230,0});
        SDL_BlendMode before;REQUIRE(SDL_GetSurfaceBlendMode(first.get(),&before));
        std::array<SpriteAtlasSource,2> sources{{{first.get(),{1,7,1,2}},{second.get(),{2,9,3,1}}}};
        CHECK_FALSE(prepareSpriteRenderAtlas(sources,2048,false));
        const auto initial=spriteAtlasMemoryBytes();auto page=prepareSpriteRenderAtlas(sources,2048,true);REQUIRE(page);
        CHECK(page->pixels->w==10);CHECK(page->pixels->h==4);CHECK(spriteAtlasMemoryBytes()==initial+160);
        REQUIRE(page->regions.size()==2);CHECK(page->regions[0].x==1);CHECK(page->regions[1].x==6);
        for(unsigned index=0;index<2;++index){const auto expected=index?SDL_Color{20,180,230,0}:SDL_Color{200,10,50,128};const auto region=page->regions[index];
            for(int y=-1;y<=region.h;++y)for(int x=-1;x<=region.w;++x){const auto actual=color(page->pixels.get(),region.x+x,region.y+y);
                CHECK(actual.r==expected.r);CHECK(actual.g==expected.g);CHECK(actual.b==expected.b);CHECK(actual.a==expected.a);}}
        SDL_BlendMode after;REQUIRE(SDL_GetSurfaceBlendMode(first.get(),&after));CHECK(before==after);
        CHECK(page->stamps[0].revision==7);CHECK(page->stamps[1].identity==2);
        page.reset();CHECK(spriteAtlasMemoryBytes()==initial);
    }
    TEST_CASE("replacement pages own original pixels while bounded promotion can defer and retry") {
        const auto initial=spriteAtlasMemoryBytes();auto source=image(1,1,{250,0,0,255});
        std::array<SpriteAtlasSource,1> inputs{{{source.get(),{11,1,1,1}}}};
        auto old=prepareSpriteRenderAtlas(inputs,2048,false);REQUIRE(old);
        REQUIRE(SDL_FillSurfaceRect(source.get(),nullptr,SDL_MapSurfaceRGBA(source.get(),0,250,0,255)));inputs[0].stamp.revision=2;
        auto replacement=prepareSpriteRenderAtlas(inputs,2048,false);REQUIRE(replacement);
        CHECK(color(old->pixels.get(),1,1).r==250);CHECK(color(replacement->pixels.get(),1,1).g==250);
        old.reset();replacement.reset();CHECK(spriteAtlasMemoryBytes()==initial);
        auto pressure=reserveSpriteAtlasMemory(64*1024*1024-initial);REQUIRE(pressure);CHECK_FALSE(prepareSpriteRenderAtlas(inputs,2048,false));
        CHECK(spriteAtlasMemoryBytes()==64*1024*1024);pressure.reset();CHECK(prepareSpriteRenderAtlas(inputs,2048,false));CHECK(spriteAtlasMemoryBytes()==initial);
    }
    TEST_CASE("packing honors both backend limits and the conservative maximum") {
        auto source=image(2047,1,{1,2,3,255});std::array<SpriteAtlasSource,1> inputs{{{source.get(),{1,1,2047,1}}}};
        const auto initial=spriteAtlasMemoryBytes();CHECK_FALSE(prepareSpriteRenderAtlas(inputs,16384,true));
        source=image(1,1,{1,2,3,255});inputs[0].pixels=source.get();CHECK_FALSE(prepareSpriteRenderAtlas(inputs,2,true));
        inputs[0].stamp.identity=0;CHECK_FALSE(prepareSpriteRenderAtlas(inputs,2048,true));CHECK(spriteAtlasMemoryBytes()==initial);
    }
}
