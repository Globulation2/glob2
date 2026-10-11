// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <SDLGraphicContext.h>
#include <GUIStyle.h>
#include <Toolkit.h>
#include <SurfaceRaster.h>
#include <RenderBackend.h>
namespace GAGCore {std::unique_ptr<RenderBackend> makeSDLSoftwareGeometryBackend(SDL_Surface *);}
#include <array>
#include <cstring>
#include <valarray>
#include <vector>
#include <type_traits>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif

namespace
{
using namespace GAGCore;
std::array<int,3> pixel(DrawableSurface& surface,int x,int y)
{
    SDL_Surface* raw=surface.getSDLSurface();
    Uint32 value=0;
    std::memcpy(&value,static_cast<Uint8*>(raw->pixels)+y*raw->pitch+x*SDL_BYTESPERPIXEL(raw->format),SDL_BYTESPERPIXEL(raw->format));
    Uint8 r,g,b; SDL_GetRGB(value,SDL_GetPixelFormatDetails(raw->format),SDL_GetSurfacePalette(raw),&r,&g,&b);
    return {r,g,b};
}
void black(DrawableSurface& surface)
{
    surface.setClipRect(); surface.drawFilledRect(0,0,surface.getW(),surface.getH(),Color(0,0,0));
}
struct ContextAccess : GraphicContext
{
    static RenderBackend *&backend(GraphicContext &context) {
        auto member = &ContextAccess::renderer;
        return context.*member;
    }
};
template<class Alpha>
void compareAlphaMapChunks(GraphicContext &context, float divisor)
{
    constexpr int mapW=40, mapH=20;
    std::valarray<Alpha> map(mapW*mapH);
    for (size_t i=0;i<map.size();++i) map[i]=Alpha((i*37)%256);
    if constexpr (std::is_same_v<Alpha,float>) map/=255.0f;
    auto actual=std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)>(
        SDL_CreateSurface(context.getW(),context.getH(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
    auto reference=std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)>(
        SDL_CreateSurface(context.getW(),context.getH(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
    REQUIRE(actual); REQUIRE(reference);
    REQUIRE(SDL_FillSurfaceRect(actual.get(),nullptr,SDL_MapSurfaceRGBA(actual.get(),20,30,40,255)));
    REQUIRE(SDL_FillSurfaceRect(reference.get(),nullptr,SDL_MapSurfaceRGBA(reference.get(),20,30,40,255)));
    auto batched=makeSDLSoftwareGeometryBackend(actual.get());
    auto unbatched=makeSDLSoftwareGeometryBackend(reference.get());
    const SDL_Rect clip{7,9,171,111};
    context.setClipRect(clip.x,clip.y,clip.w,clip.h);
    batched->clip(&clip);
    unbatched->clip(&clip);
    struct Restore {
        GraphicContext &context; RenderBackend *previous;
        ~Restore() {ContextAccess::backend(context)=previous;}
    } restore{context,ContextAccess::backend(context)};
    ContextAccess::backend(context)=batched.get();
    context.drawAlphaMap(map,mapW,mapH,2,3,5,5,Color(60,130,230));
    batched->flush();
    for(int j=0;j<mapH-1;++j)for(int i=0;i<mapW-1;++i) {
        auto vertex=[](float x,float y,float alpha){return SDL_Vertex{{x,y},{60/255.f,130/255.f,230/255.f,alpha},{0,0}};};
        const float a=map[j*mapW+i]/divisor,b=map[j*mapW+i+1]/divisor;
        const float c=map[(j+1)*mapW+i+1]/divisor,d=map[(j+1)*mapW+i]/divisor;
        const float x=2+i*5,y=3+j*5;
        const auto va=vertex(x,y,a),vb=vertex(x+5,y,b),vc=vertex(x+5,y+5,c),vd=vertex(x,y+5,d);
        const auto center=vertex(x+2.5f,y+2.5f,(a+b+c+d)/4);
        const SDL_Vertex fan[]={center,va,vb,center,vb,vc,center,vc,vd,center,vd,va};
        unbatched->triangles(fan);
    }
    unbatched->flush();
    CHECK(batched->operations().triangles==3);
    CHECK(unbatched->operations().triangles==(mapW-1)*(mapH-1));
    for(int y=0;y<actual->h;++y)
        CHECK(std::memcmp(static_cast<const Uint8 *>(actual->pixels)+y*actual->pitch,
            static_cast<const Uint8 *>(reference->pixels)+y*reference->pitch,size_t(actual->w)*4)==0);
}
}
TEST_SUITE("SurfaceCoverage")
{
#ifdef HAVE_OPENGL
    TEST_CASE("legacy float alpha chunks preserve original fans and integer centers [display]")
    {
        glob2test::ToolkitScope toolkit;
        GraphicContext::setInitialRendererBackend(RendererBackend::LegacyGL);
        auto *context=Toolkit::initGraphic(384,192,GraphicContext::USEGPU|GraphicContext::NOAUDIO,"legacy alpha chunks");
        constexpr int width=100,height=50;
        std::valarray<float> map(width*height);
        for(size_t i=0;i<map.size();++i) map[i]=float((i*37)%256)/255.f;
        auto capture=[] {
            GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
            std::vector<Uint8> pixels(size_t(viewport[2])*viewport[3]*4);
            glReadPixels(viewport[0],viewport[1],viewport[2],viewport[3],GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            REQUIRE(glGetError()==GL_NO_ERROR);return pixels;
        };
        auto background=[&] {
            context->setClipRect();context->drawFilledRect(0,0,384,192,Color(20,30,40));
            context->setClipRect(7,9,281,151);
        };
        background();glEnable(GL_BLEND);glDisable(GL_TEXTURE_2D);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        const Color color(60,130,230);
        const float r=255.f*color.r,g=255.f*color.g,b=255.f*color.b;
        for(int y=0;y<height-1;++y)for(int x=0;x<width-1;++x) {
            const float a=map[y*width+x],d=map[y*width+x+1];
            const float c=map[(y+1)*width+x+1],bl=map[(y+1)*width+x];
            glBegin(GL_TRIANGLE_FAN);
            glColor4f(r,color.g,color.b,((a+d)/2+(bl+c)/2)/2);glVertex2f(2+x*3+1,3+y*3+1);
            glColor4f(r,g,b,a);glVertex2f(2+x*3,3+y*3);
            glColor4f(r,g,b,bl);glVertex2f(2+x*3,3+(y+1)*3);
            glColor4f(r,g,b,c);glVertex2f(2+(x+1)*3,3+(y+1)*3);
            glColor4f(r,g,b,d);glVertex2f(2+(x+1)*3,3+y*3);
            glColor4f(r,g,b,a);glVertex2f(2+x*3,3+y*3);glEnd();
        }
        const auto reference=capture();background();
        context->drawAlphaMap(map,width,height,2,3,3,3,color);
        CHECK(capture()==reference);
    }
#endif
    TEST_CASE("bounded alpha map chunks preserve clipped cell interpolation [display]")
    {
        glob2test::ToolkitScope toolkit;
        GraphicContext::setInitialRendererBackend(RendererBackend::Software);
        auto *context=Toolkit::initGraphic(192,128,GraphicContext::NOAUDIO,"alpha map batching");
        compareAlphaMapChunks<float>(*context,1.0f);
        compareAlphaMapChunks<unsigned char>(*context,255.0f);
    }
    TEST_CASE("backing pixel fills and blits retain native clipping and queued order")
    {
        glob2test::ToolkitScope toolkit; Toolkit::initGraphic(96,96,0,"pixel operations");
        DrawableSurface target(64,48), source(4,4);
        black(target);
        source.drawFilledRect(0,0,4,4,Color(0,255,0));
        auto backend = makeSoftwareRenderBackend(target.getSDLSurface());
        backend->nativeLogicalSize(32,24);
        const SDL_Rect bounds{4,4,16,12};
        backend->transform(.5f,0,0,&bounds);
        const SDL_FColor blue{0,0,1,1};
        const SDL_Vertex triangle[] = {{{0,0},blue,{}},{{32,0},blue,{}},{{0,32},blue,{}}};
        backend->triangles(triangle);
        REQUIRE(backend->fillPixels({0,0,64,48},{255,0,0,255}));
        CHECK(pixel(target,12,12)==std::array<int,3>{255,0,0});
        CHECK(pixel(target,6,6)==std::array<int,3>{0,0,0});
        backend->triangles(triangle);
        REQUIRE(backend->blitPixels(source.getSDLSurface(),{0,0,4,4},{0,0,64,48},255));
        CHECK(pixel(target,12,12)==std::array<int,3>{0,255,0});
        CHECK(pixel(target,6,6)==std::array<int,3>{0,0,0});
    }

    TEST_CASE("float and byte alpha grids agree and respect clip rectangles")
    {
        glob2test::ToolkitScope toolkit; Toolkit::initGraphic(64,64,0,"surface coverage");
        DrawableSurface floating(16,16),bytes(16,16);
        black(floating); black(bytes);
        floating.setClipRect(3,3,7,7); bytes.setClipRect(3,3,7,7);
        const std::valarray<float> floats{1,0,1,0,1,0,1,0,1};
        const std::valarray<unsigned char> alphas{255,0,255,0,255,0,255,0,255};
        floating.drawAlphaMap(floats,3,3,2,2,4,4,Color(255,40,20));
        bytes.drawAlphaMap(alphas,3,3,2,2,4,4,Color(255,40,20));
        for (int y=0; y<16; ++y) for (int x=0; x<16; ++x)
        {
            CHECK(pixel(floating,x,y)==pixel(bytes,x,y));
            const bool painted=x>=3 && y>=3 && x<10 && y<10 && ((x<6)==(y<6));
            CHECK(pixel(floating,x,y)==(painted ? std::array<int,3>{255,40,20} : std::array<int,3>{0,0,0}));
        }
    }

    TEST_CASE("scaled partial blits preserve crop coordinates and untouched pixels")
    {
        glob2test::ToolkitScope toolkit; Toolkit::initGraphic(64,64,0,"surface coverage");
        DrawableSurface source(4,4),target(12,12);
        black(source); black(target);
        source.drawFilledRect(1,1,1,1,Color(255,0,0));
        source.drawFilledRect(2,1,1,1,Color(0,255,0));
        source.drawFilledRect(1,2,1,1,Color(0,0,255));
        source.drawFilledRect(2,2,1,1,Color(255,255,255));
        target.setClipRect(4,4,4,4);
        target.drawSurface(3,3,6,6,&source,1,1,2,2,255);
        for (int y=0; y<12; ++y) for (int x=0; x<12; ++x)
        {
            const auto expected=x>=4 && y>=4 && x<8 && y<8
                ? pixel(source,x<6 ? 1:2,y<6 ? 1:2) : std::array<int,3>{0,0,0};
            const auto actual=pixel(target,x,y);
            INFO("pixel "<<x<<","<<y<<" actual "<<actual[0]<<","<<actual[1]<<","<<actual[2]<<" expected "<<expected[0]<<","<<expected[1]<<","<<expected[2]);
            CHECK(actual==expected);
        }
        target.setClipRect();
        target.drawSurface(0,0,0,6,&source,0,0,4,4,255);
        CHECK(pixel(target,0,0)==std::array<int,3>{0,0,0});
    }

    TEST_CASE("legacy progress style paints exact empty half and full interiors")
    {
        glob2test::ToolkitScope toolkit; Toolkit::initGraphic(64,64,0,"surface coverage");
        GAGGUI::Style style;
        DrawableSurface target(40,30);
        for (int value : {0,50,100})
        {
            black(target); style.drawProgressBar(&target,2,2,22,value,100);
            CHECK(pixel(target,2,2)!=std::array<int,3>{0,0,0});
            for (int x=3; x<23; ++x)
                CHECK((pixel(target,x,4)==std::array<int,3>{255,255,255})==(x<3+value/5));
        }
        CHECK(style.getStyleMetric(GAGGUI::Style::STYLE_METRIC_PROGRESS_BAR_HEIGHT)==22);
        CHECK(style.getStyleMetric(GAGGUI::Style::STYLE_METRIC_LIST_SCROLLBAR_WIDTH)==22);
    }
    TEST_CASE("skin linear filtering preserves coverage and clipped sampling")
    {
        glob2test::ToolkitScope toolkit; Toolkit::initGraphic(64,64,0,"skin filter");
        DrawableSurface source(2,1),full(9,5),clipped(9,5);
        auto *raw=static_cast<Uint32 *>(source.getSDLSurface()->pixels);
        raw[0]=0x00ffffff;raw[1]=0xffff0000;
        black(full);black(clipped);clipped.setClipRect(3,1,3,3);
        const SDL_Rect src{0,0,2,1},dst{1,1,6,3};
        SurfaceRaster::skinBlit(full.getSDLSurface(),source.getSDLSurface(),src,dst,128);
        SurfaceRaster::skinBlit(clipped.getSDLSurface(),source.getSDLSurface(),src,dst,128);
        for(int y=0;y<5;++y)for(int x=0;x<9;++x) {
            CHECK(pixel(clipped,x,y)==(x>=3 && x<6 && y>=1 && y<4?pixel(full,x,y):std::array<int,3>{0,0,0}));
        }
        CHECK(pixel(full,4,2)==std::array<int,3>{85,0,0});
        CHECK(pixel(full,6,2)==std::array<int,3>{128,0,0});
    }

    TEST_CASE("skin filtering through SDL and CPU backends preserves premultiplied edges")
    {
        glob2test::ToolkitScope toolkit;Toolkit::initGraphic(64,64,0,"skin backends");
        DrawableSurface source(2,1),cpu(9,5),sdl(9,5);black(cpu);black(sdl);
        auto *raw=static_cast<Uint32 *>(source.getSDLSurface()->pixels);raw[0]=0x00ffffff;raw[1]=0xffff0000;
        auto raster=makeSoftwareRenderBackend(cpu.getSDLSurface());
        auto portable=makeSDLSoftwareGeometryBackend(sdl.getSDLSurface());
        for(auto *backend:{raster.get(),portable.get()}) {
            backend->transform(2,1,1,nullptr);
            const SDL_Rect clip{1,0,2,2};backend->clip(&clip);
            backend->blitLinear(&source,source.getSDLSurface(),1,{0,0,2,1},{0,0,3,1.5},128);
            backend->flush();
        }
        for(int y=0;y<5;++y)for(int x=0;x<9;++x) {
            const auto a=pixel(cpu,x,y),b=pixel(sdl,x,y);
            for(unsigned c=0;c<3;++c)CHECK(std::abs(a[c]-b[c])<=3);
        }
        CHECK(pixel(cpu,4,2)==std::array<int,3>{85,0,0});
    }

}
