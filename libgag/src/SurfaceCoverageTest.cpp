// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ScopedEnvironment.h"
#include <SDLGraphicContext.h>
#include <GUIStyle.h>
#include <Toolkit.h>
#include <SurfaceRaster.h>
#include <RenderBackend.h>
#include <PresentationWork.h>
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
    TEST_CASE("execution modes keep immediate default and legacy override compatibility")
    {
        glob2test::ScopedEnvironment restore("GLOB2_RENDER_EXECUTION", "immediate");
        glob2test::unsetEnv("GLOB2_RENDER_EXECUTION");
        CHECK(renderExecution() == RenderExecution::Immediate);
        for (const char *value : {"immediate", "auto"})
        {
            glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", value);
            CHECK(renderExecution() == RenderExecution::Immediate);
        }
        {
            glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "serial");
            CHECK(renderExecution() == RenderExecution::Serial);
        }
        {
            glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "parallel");
            CHECK(renderExecution() == RenderExecution::Parallel);
        }
        {
            glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "automatic");
            CHECK_THROWS_WITH_AS(renderExecution(),
                                 "GLOB2_RENDER_EXECUTION must be immediate, serial or parallel",
                                 std::invalid_argument);
        }
    }

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
        for (const char *execution : {"immediate", "serial", "parallel"})
        {
            glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
            INFO("execution " << execution);
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
            // DrawableSurface borrows the backend's target without owning its
            // queue. Fence the owner before observing those pixels directly.
            backend->flush();
            CHECK(pixel(target,12,12)==std::array<int,3>{255,0,0});
            CHECK(pixel(target,6,6)==std::array<int,3>{0,0,0});
            backend->triangles(triangle);
            REQUIRE(backend->blitPixels(source.getSDLSurface(),{0,0,4,4},{0,0,64,48},255));
            backend->flush();
            CHECK(pixel(target,12,12)==std::array<int,3>{0,255,0});
            CHECK(pixel(target,6,6)==std::array<int,3>{0,0,0});
            CHECK((backend->operations().rasterBatches > 0) ==
                  (std::strcmp(execution, "immediate") != 0));
        }
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

    TEST_CASE("packed raster jobs bound references and preserve disjoint tile painter order")
    {
        struct ReverseJobs : PresentationWork
        {
            std::vector<std::size_t> counts;
            void executePresentationJobs(std::size_t count,
                                         const std::function<void(std::size_t)> &job) override
            {
                counts.push_back(count);
                // Reverse callback order makes accidental inter-job overlap visible.
                for (std::size_t index = count; index > 0; --index) job(index - 1);
            }
        } dispatcher;
        PresentationWorkScope scope(dispatcher);
        constexpr int tileCount = 20, edge = 128, width = tileCount * edge;
        for (const bool tiny : {false, true})
            for (const unsigned perTile : {1u, 2u, 16u})
            {
                INFO("tiny " << tiny << " commands per tile " << perTile);
                using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
                Surface actual(SDL_CreateSurface(width, edge, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
                Surface expected(SDL_CreateSurface(width, edge, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
                REQUIRE(actual); REQUIRE(expected);
                REQUIRE(SDL_FillSurfaceRect(actual.get(), nullptr, 0xff173b61));
                REQUIRE(SDL_FillSurfaceRect(expected.get(), nullptr, 0xff173b61));
                const auto draw = [&](RenderBackend &backend)
                {
                    for (int tile = 0; tile < tileCount; ++tile)
                        for (unsigned command = 0; command < perTile; ++command)
                            REQUIRE(backend.fillPixels(
                                {tile * edge, 0, tiny ? 1 : edge, tiny ? 1 : edge},
                                {Uint8(tile * 13), Uint8(command * 17), 191, 79}));
                    backend.flush();
                };
                {
                    glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "immediate");
                    auto backend = makeSoftwareRenderBackend(expected.get());
                    draw(*backend);
                }
                glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "parallel");
                auto backend = makeSoftwareRenderBackend(actual.get());
                const auto before = dispatcher.counts.size();
                draw(*backend);
                const auto operations = backend->operations();
                const unsigned tilesPerJob = 16 / perTile;
                const unsigned expectedJobs = (tileCount + tilesPerJob - 1) / tilesPerJob;
                REQUIRE(dispatcher.counts.size() == before + 1);
                CHECK(dispatcher.counts.back() == expectedJobs);
                CHECK(operations.rasterJobs == expectedJobs);
                CHECK(operations.rasterTiles == tileCount);
                CHECK(operations.rasterReferences == tileCount * perTile);
                CHECK(operations.rasterSerialBatches == 0);
                for (int y = 0; y < edge; ++y)
                    CHECK(std::memcmp(static_cast<const Uint8 *>(actual->pixels) + y * actual->pitch,
                                      static_cast<const Uint8 *>(expected->pixels) + y * expected->pitch,
                                      std::size_t(width) * 4) == 0);
            }
    }
    TEST_CASE("dense raster tiles retain serial fallback before job packing")
    {
        struct NoJobs : PresentationWork
        {
            void executePresentationJobs(std::size_t,
                                         const std::function<void(std::size_t)> &) override
            { FAIL("dense tiles must not dispatch optional work"); }
        } dispatcher;
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "parallel");
        PresentationWorkScope scope(dispatcher);
        using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
        Surface target(SDL_CreateSurface(128, 128, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        REQUIRE(target);
        auto backend = makeSoftwareRenderBackend(target.get());
        for (int i = 0; i < 17; ++i)
            REQUIRE(backend->fillPixels({0, 0, 1, 1}, {31, 77, Uint8(i), 255}));
        backend->flush();
        CHECK(backend->operations().rasterSerialBatches == 1);
        CHECK(backend->operations().rasterJobs == 0);
        Uint32 pixel = 0;
        std::memcpy(&pixel, target->pixels, 4);
        CHECK(pixel == 0xff1f4d10);
    }

    TEST_CASE("revisioned source captures share crops and retain pending old revisions")
    {
        for (const char *execution : {"serial", "parallel"})
        {
            glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", execution);
            const auto available = RenderWorkReservation::available();
            {
                using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
                Surface target(SDL_CreateSurface(16, 4, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
                Surface source(SDL_CreateSurface(4, 2, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
                REQUIRE(target); REQUIRE(source);
                REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0xff000000));
                REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, 0xffff0000));
                const SDL_Rect right{2, 0, 2, 2};
                REQUIRE(SDL_FillSurfaceRect(source.get(), &right, 0xff0000ff));
                auto backend = makeSoftwareRenderBackend(target.get());
                const int identity = 0;
                REQUIRE(backend->blitPixels(&identity, source.get(), 1, {0,0,4,2}, {0,0,4,2}, 255));
                REQUIRE(backend->blitPixels(&identity, source.get(), 1, right, {2,0,2,2}, 255));
                CHECK(backend->operations().rasterSnapshotBytes == 32);
                CHECK(backend->operations().rasterSourceReuses == 1);
                REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, 0xff00ff00));
                REQUIRE(backend->blitPixels(&identity, source.get(), 2, {0,0,2,2}, {4,0,2,2}, 255));
                CHECK(backend->operations().rasterSourceRetainedBytes == 48);
                backend->present();
                CHECK(backend->operations().rasterSourceRetainedBytes == 16);
                const auto read = [&](int x) { Uint32 value; std::memcpy(&value, static_cast<Uint8*>(target->pixels)+x*4, 4); return value; };
                CHECK(read(0) == 0xffff0000);
                CHECK(read(2) == 0xff0000ff);
                CHECK(read(4) == 0xff00ff00);
                REQUIRE(backend->blitPixels(&identity, source.get(), 2, {1,0,1,1}, {6,0,1,1}, 255));
                backend->present();
                CHECK(read(6) == 0xff00ff00);
                // A crop outside the retained region takes a new immutable copy.
                REQUIRE(backend->blitPixels(&identity, source.get(), 2, right, {8,0,2,2}, 255));
                backend->present();
                CHECK(read(8) == 0xff00ff00);
                CHECK(backend->operations().rasterSnapshotBytes == 64);
                CHECK(backend->operations().rasterSourceReuses == 2);
                backend->forget(&identity);
                CHECK(backend->operations().rasterSourceRetainedBytes == 0);
            }
            CHECK(RenderWorkReservation::available() == available);
        }
    }
    TEST_CASE("raw and null-key sources freeze each capture independently")
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "serial");
        using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
        Surface target(SDL_CreateSurface(8, 2, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        Surface source(SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        REQUIRE(target); REQUIRE(source);
        auto backend = makeSoftwareRenderBackend(target.get());
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, 0xffff0000));
        REQUIRE(backend->blitPixels(source.get(), {0,0,2,2}, {0,0,2,2}, 255));
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, 0xff0000ff));
        REQUIRE(backend->blitPixels(nullptr, source.get(), 1, {0,0,2,2}, {2,0,2,2}, 255));
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, 0xff00ff00));
        REQUIRE(backend->blitPixels(nullptr, source.get(), 1, {0,0,2,2}, {4,0,2,2}, 255));
        source.reset();
        backend->present();
        const auto read = [&](int x) { Uint32 value; std::memcpy(&value, static_cast<Uint8*>(target->pixels)+x*4, 4); return value; };
        CHECK(read(0) == 0xffff0000);
        CHECK(read(2) == 0xff0000ff);
        CHECK(read(4) == 0xff00ff00);
        CHECK(backend->operations().rasterSourceReuses == 0);
        CHECK(backend->operations().rasterSnapshotBytes == 48);
        CHECK(backend->operations().rasterSourceRetainedBytes == 0);
    }
    TEST_CASE("keyed borrowed sources reuse owned copies and invalidate geometry or forgotten identities")
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "serial");
        using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
        std::array<Uint32,4> pixels{0xffff0000,0xffff0000,0xffff0000,0xffff0000};
        Surface target(SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        Surface view(SDL_CreateSurfaceFrom(2,2,SDL_PIXELFORMAT_ARGB8888,pixels.data(),8), SDL_DestroySurface);
        REQUIRE(target); REQUIRE(view);
        auto backend = makeSoftwareRenderBackend(target.get());
        const int identity = 0;
        REQUIRE(backend->blitPixels(&identity, view.get(), 1, {0,0,2,2}, {0,0,2,2}, 255));
        backend->present();
        REQUIRE(backend->blitPixels(&identity, view.get(), 1, {0,0,2,2}, {2,0,2,2}, 255));
        backend->present();
        CHECK(backend->operations().rasterSourceReuses == 1);
        pixels.fill(0xff0000ff);
        REQUIRE(backend->blitPixels(&identity, view.get(), 2, {0,0,2,2}, {4,0,2,2}, 255));
        backend->present();
        backend->forget(&identity);
        pixels.fill(0xff00ff00);
        // Reuse the exact key and pixel address, as a new source lifetime may.
        REQUIRE(backend->blitPixels(&identity, view.get(), 1, {0,0,2,2}, {0,3,2,2}, 255));
        backend->present();
        Surface resized(SDL_CreateSurfaceFrom(4,1,SDL_PIXELFORMAT_ARGB8888,pixels.data(),16), SDL_DestroySurface);
        REQUIRE(resized);
        pixels.fill(0xffaa00bb);
        // Geometry is independently part of the contract even if a caller's
        // revision remains unchanged while replacing an SDL descriptor.
        REQUIRE(backend->blitPixels(&identity, resized.get(), 1, {0,0,4,1}, {0,6,4,1}, 255));
        backend->present();
        const auto read = [&](int x,int y) { Uint32 value; std::memcpy(&value, static_cast<Uint8*>(target->pixels)+y*target->pitch+x*4, 4); return value; };
        CHECK(read(0,0) == 0xffff0000);
        CHECK(read(2,0) == 0xffff0000);
        CHECK(read(4,0) == 0xff0000ff);
        CHECK(read(0,3) == 0xff00ff00);
        CHECK(read(0,6) == 0xffaa00bb);
        CHECK(backend->operations().rasterSourceReuses == 1);
        CHECK(backend->operations().rasterSnapshotBytes == 64);
        backend->reset();
        CHECK(backend->operations().rasterSourceRetainedBytes == 0);
    }
    TEST_CASE("aliased revisioned views observe queued drawing and never reuse source captures")
    {
        for (const bool flipped : {false,true})
        {
            glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "serial");
            using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
            Surface target(SDL_CreateSurface(8,2,SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
            REQUIRE(target);
            Surface view(SDL_CreateSurfaceFrom(2,2,SDL_PIXELFORMAT_ARGB8888,target->pixels,target->pitch), SDL_DestroySurface);
            REQUIRE(view);
            if (flipped) {
                view->pixels = static_cast<Uint8*>(target->pixels)+target->pitch;
                view->pitch = -target->pitch;
            }
            auto backend = makeSoftwareRenderBackend(target.get());
            const int identity = 0;
            REQUIRE(backend->fillPixels({0,0,2,2},{255,0,0,255}));
            REQUIRE(backend->blitPixels(&identity, view.get(), 1, {0,0,2,2}, {2,0,2,2}, 255));
            REQUIRE(backend->fillPixels({0,0,2,2},{0,0,255,255}));
            REQUIRE(backend->blitPixels(&identity, view.get(), 1, {0,0,2,2}, {4,0,2,2}, 255));
            backend->present();
            Uint32 red,blue;
            std::memcpy(&red, static_cast<Uint8*>(target->pixels)+2*4,4);
            std::memcpy(&blue, static_cast<Uint8*>(target->pixels)+4*4,4);
            CHECK(red == 0xffff0000);
            CHECK(blue == 0xff0000ff);
            CHECK(backend->operations().rasterSourceReuses == 0);
            CHECK(backend->operations().rasterSourceRetainedBytes == 0);
        }
    }
    TEST_CASE("source cache pressure bounds live copies and leaves a small warm set after present")
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "serial");
        const auto available = RenderWorkReservation::available();
        {
            using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
            Surface target(SDL_CreateSurface(8,8,SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
            REQUIRE(target);
            auto backend = makeSoftwareRenderBackend(target.get());
            std::vector<Surface> sources;
            for (unsigned i=0;i<21;++i)
            {
                sources.emplace_back(SDL_CreateSurface(256,256,SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
                REQUIRE(sources.back());
                REQUIRE(SDL_FillSurfaceRect(sources.back().get(),nullptr,0xff000000+i));
                REQUIRE(backend->blitPixels(sources.back().get(), sources.back().get(), 1,
                                            {0,0,256,256}, {0,0,1,1}, 255));
                CHECK(backend->operations().rasterSourceRetainedBytes <= 4u*1024u*1024u);
                CHECK(RenderWorkReservation::available() >= available-6u*1024u*1024u);
            }
            CHECK(backend->operations().rasterBatches > 0); // Pressure finished an earlier pinned segment.
            backend->present();
            CHECK(backend->operations().rasterSourceRetainedBytes == 1024u*1024u);
            const auto copied = backend->operations().rasterSnapshotBytes;
            REQUIRE(backend->blitPixels(sources.back().get(), sources.back().get(), 1,
                                        {0,0,256,256}, {0,0,1,1}, 255));
            CHECK(backend->operations().rasterSnapshotBytes == copied);
            CHECK(backend->operations().rasterSourceReuses == 1);
            REQUIRE(backend->blitPixels(sources.front().get(), sources.front().get(), 1,
                                        {0,0,256,256}, {1,0,1,1}, 255));
            CHECK(backend->operations().rasterSnapshotBytes == copied+256u*256u*4u);
            backend->discardFrame();
            CHECK(backend->operations().rasterSourceRetainedBytes == 0);
        }
        CHECK(RenderWorkReservation::available() == available);
    }

    TEST_CASE("native frame completion trims cached sources after map transform restores immediate drawing [display]")
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "serial");
        const auto available = RenderWorkReservation::available();
        {
            glob2test::ToolkitScope toolkit;
            auto *context = Toolkit::initGraphic(96,96,0,"source frame boundary");
            REQUIRE(context);
            std::vector<std::unique_ptr<DrawableSurface>> sources;
            for (unsigned i=0;i<5;++i)
            {
                sources.push_back(std::make_unique<DrawableSurface>(256,256));
                sources.back()->drawFilledRect(0,0,256,256,Color(31+int(i),77,191));
            }
            context->beginMapTransform(1,1,0,0,0,96,96);
            for (unsigned i=0;i<sources.size();++i)
                context->drawSurface(int(i)*12,0,8,8,sources[i].get());
            context->endMapTransform();
            REQUIRE(context->backendOperations().rasterSourceRetainedBytes == 5u*256u*256u*4u);
            context->nextFrame();
            CHECK(context->backendOperations().rasterSourceRetainedBytes == 1024u*1024u);
            CHECK(pixel(*context,2,2) == std::array<int,3>{31,77,191});
            CHECK(pixel(*context,50,2) == std::array<int,3>{35,77,191});
        }
        CHECK(RenderWorkReservation::available() == available);
    }

    TEST_CASE("large revisioned sources share requested regions without copying offscreen pixels")
    {
        glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "serial");
        const auto available = RenderWorkReservation::available();
        {
            glob2test::ToolkitScope toolkit;
            Toolkit::initGraphic(64,64,0,"large source crops");
            DrawableSurface source(512,1024), target(32,16);
            source.drawFilledRect(0,0,512,1024,Color(31,77,191));
            black(target);
            auto backend = makeSoftwareRenderBackend(target.getSDLSurface());
            const SDL_Rect crop{127,510,8,4};
            for (int x : {0,16})
                REQUIRE(backend->blitPixels(&source,source.getSDLSurface(),source.contentRevision(),
                                           crop,{x,0,8,4},255));
            backend->flush();
            CHECK(backend->operations().rasterSnapshotBytes == 8u*4u*4u);
            CHECK(backend->operations().rasterSourceReuses == 1);
            CHECK(backend->operations().rasterSourceRetainedBytes == 8u*4u*4u);
            backend->forget(&source);
            CHECK(backend->operations().rasterSourceRetainedBytes == 0);
            CHECK(pixel(target,2,2) == std::array<int,3>{31,77,191});
            CHECK(pixel(target,18,2) == std::array<int,3>{31,77,191});
        }
        CHECK(RenderWorkReservation::available() == available);
    }

}
