// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <cstring>
#include <ProgrammableGLDevice.h>
#include <SpriteRenderAtlas.h>
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#if defined(HAVE_OPENGL) || defined(GLOB2_PROGRAMMABLE_GL) || defined(__EMSCRIPTEN__)
#include <SDL3/SDL_opengl.h>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
using namespace GAGCore;
namespace
{
struct GLFixture
{
    SDL_Window *window = nullptr;
    std::unique_ptr<RenderDevice> device;
    std::string shaderDirectory;
    GLFixture(bool es = false)
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, es ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, es ? 0 : 3);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        window = SDL_CreateWindow("Programmable renderer test", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!window) { SDL_QuitSubSystem(SDL_INIT_VIDEO); throw std::runtime_error(SDL_GetError()); }
        const char *directory = std::getenv("GLOB2_RENDERER_SHADER_DIR");
#ifdef GLOB2_RENDER_SHADER_DIR
        if (!directory) directory = GLOB2_RENDER_SHADER_DIR;
#else
        if (!directory) directory = "data/renderer-shaders";
#endif
        shaderDirectory=directory;
        try { device = makeProgrammableGLDevice(window, directory); }
        catch (...) { SDL_DestroyWindow(window); SDL_QuitSubSystem(SDL_INIT_VIDEO); throw; }
    }
    ~GLFixture() { device.reset(); SDL_DestroyWindow(window); SDL_QuitSubSystem(SDL_INIT_VIDEO); }
};
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
Surface surface(int w = 16, int h = 16) { return Surface(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface); }
void equal(SDL_Surface *reference, SDL_Surface *actual, int tolerance = 0)
{
    REQUIRE(reference->w == actual->w); REQUIRE(reference->h == actual->h);
    int differing = 0, maximum = 0;
    for (int y = 0; y < reference->h; ++y) for (int x = 0; x < reference->w; ++x) {
        Uint8 r[4], a[4];
        REQUIRE(SDL_ReadSurfacePixel(reference, x, y, &r[0], &r[1], &r[2], &r[3]));
        REQUIRE(SDL_ReadSurfacePixel(actual, x, y, &a[0], &a[1], &a[2], &a[3]));
        int error = 0; for (int channel = 0; channel < 4; ++channel) error = std::max(error, std::abs(int(r[channel]) - int(a[channel])));
        if (error > tolerance) ++differing;
        maximum = std::max(maximum, error);
    }
    INFO("Differing pixels=" << differing << " maximum channel error=" << maximum);
    CHECK(differing == 0);
}
void scene(RenderBackend &backend, SDL_Surface *source)
{
    backend.fill({0, 0, 16, 16}, {12, 24, 48, 255});
    backend.fill({1, 1, 6, 4}, {255, 0, 0, 255});
    backend.fill({2, 2, 5, 5}, {0, 255, 0, 128});
    const SDL_Rect clip{3, 3, 8, 7}; backend.clip(&clip);
    backend.blit(source, source, 1, true, {0, 0, 2, 2}, {3, 3, 8, 8}, 255);
    backend.clip(nullptr);
    const SDL_Rect bounds{0, 0, 16, 16}; backend.transform(2, 1, 1, &bounds);
    backend.fill({5, 5, 2, 2}, {0, 0, 255, 255});
    backend.transform(1, 0, 0, nullptr); backend.present();
}
}
TEST_SUITE("ProgrammableGLDevice")
{
    GLOB2_TEST_CASE("ordered primitive segments share uploads across state and point changes", "[display]")
    {
        GLFixture fixture;
        DrawPacket background; background.destination={0,0,16,16}; background.color={190,20,30,255}; background.opaque=true;
        DrawPacket overlay=background; overlay.destination={2,2,12,12}; overlay.color={20,190,30,128}; overlay.opaque=false;
        DrawPacket point; point.kind=DrawPacket::Kind::Triangles; point.material=DrawPacket::Material::SmoothPoint;
        point.blend=DrawPacket::Blend::Additive;
        const SDL_FColor color{.1f,.2f,.8f,.5f};
        const SDL_Vertex a{{4,4},color,{0,0}},b{{12,4},color,{1,0}},c{{12,12},color,{1,1}},d{{4,12},color,{0,1}};
        point.vertices={a,b,c,a,c,d};
        RenderFrame frame; frame.identity=1;frame.logicalWidth=frame.logicalHeight=16;
        frame.packets={background,overlay,point}; frame.vertexCount=6;
        const auto batches=fixture.device->telemetry().batches;
        REQUIRE(fixture.device->submit(frame));
        auto bufferSize=reinterpret_cast<void(APIENTRY *)(GLenum,GLenum,GLint *)>(SDL_GL_GetProcAddress("glGetBufferParameteriv"));
        REQUIRE(bufferSize); GLint uploadedBytes=0;bufferSize(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&uploadedBytes);
        // The real GL buffer contains all three ordered ranges, not just the
        // last draw. No state change caused a separate orphan/upload.
        CHECK(uploadedBytes==18*int(sizeof(SDL_Vertex)));
        CHECK(fixture.device->telemetry().batches==batches+3);
        fixture.device->present();Surface combined(fixture.device->captureSynchronously({}),SDL_DestroySurface);REQUIRE(combined);
        DrawPacket barrier;barrier.kind=DrawPacket::Kind::Barrier;
        frame.identity=2;frame.packets={background,barrier,overlay,barrier,point};
        REQUIRE(fixture.device->submit(std::move(frame)));fixture.device->present();
        Surface separated(fixture.device->captureSynchronously({}),SDL_DestroySurface);REQUIRE(separated);
        equal(combined.get(),separated.get());
    }
    GLOB2_TEST_CASE("cloud revisions and cached world uniforms change pixels on the next frame", "[display]")
    {
        GLFixture fixture;RenderResources resources;
        auto pixels=surface(2,1);SDL_FillSurfaceRect(pixels.get(),nullptr,SDL_MapSurfaceRGBA(pixels.get(),255,0,0,255));
        auto texture=resources.create(pixels.get(),1);REQUIRE(texture);
        const std::array<WorldVertex,3> vertices{{{{-1,-1,.5f,1},{1,1,1},{0,0},{0,0,1}},
            {{0,-1,.5f,1},{1,1,1},{0,0},{0,0,1}},{{-1,1,.5f,1},{1,1,1},{0,0},{0,0,1}}}};
        auto geometry=resources.createGeometry(GeometryLayout::World48,std::as_bytes(std::span(vertices)),{},1);REQUIRE(geometry);
        DrawPacket clear;clear.kind=DrawPacket::Kind::Clear;clear.clearColor=true;clear.clearValue={0,0,0,1};
        DrawPacket cloud;cloud.kind=DrawPacket::Kind::MeshTriangles;cloud.material=DrawPacket::Material::Cloud;
        cloud.geometry=resources.snapshot(geometry);cloud.count=3;cloud.texture=resources.snapshot(texture);cloud.blend=DrawPacket::Blend::Opaque;
        cloud.world.projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};cloud.world.sunSpecular={0,0,1,0};
        RenderFrame frame;frame.identity=1;frame.logicalWidth=frame.logicalHeight=16;frame.packets={clear,cloud};
        REQUIRE(fixture.device->submit(frame));fixture.device->present();
        Surface before(fixture.device->captureSynchronously({}),SDL_DestroySurface);REQUIRE(before);
        SDL_FillSurfaceRect(pixels.get(),nullptr,SDL_MapSurfaceRGBA(pixels.get(),0,255,0,255));
        REQUIRE(resources.update(texture,pixels.get(),2));cloud.texture=resources.snapshot(texture);
        cloud.world.projection[12]=1;cloud.world.sunSpecular={0,0,-1,0};cloud.world.uvTransform={1,1,.25f,0};
        frame.identity=2;frame.packets={clear,cloud};REQUIRE(fixture.device->submit(std::move(frame)));fixture.device->present();
        Surface after(fixture.device->captureSynchronously({}),SDL_DestroySurface);REQUIRE(after);
        Uint8 r,g,b,a;REQUIRE(SDL_ReadSurfacePixel(before.get(),2,10,&r,&g,&b,&a));CHECK(r>0);CHECK(g==0);
        REQUIRE(SDL_ReadSurfacePixel(after.get(),2,10,&r,&g,&b,&a));CHECK(r==0);CHECK(g==0);
        REQUIRE(SDL_ReadSurfacePixel(after.get(),10,10,&r,&g,&b,&a));CHECK(r==0);CHECK(g>0);
    }
    GLOB2_TEST_CASE("RGBA upload shortcut preserves color-key and color-space conversion", "[display]")
    {
        GLFixture fixture;RenderResources resources;
        for(bool colorKey:{false,true}) {
            auto source=surface(1,1);const auto color=SDL_MapSurfaceRGBA(source.get(),64,128,192,255);
            SDL_FillSurfaceRect(source.get(),nullptr,color);
            if(colorKey)REQUIRE(SDL_SetSurfaceColorKey(source.get(),true,color));
            else REQUIRE(SDL_SetSurfaceColorspace(source.get(),SDL_COLORSPACE_SRGB_LINEAR));
            const auto handle=resources.create(source.get(),1);REQUIRE(handle);
            // Exercise an owned restoration surface whose metadata still needs
            // SDL conversion, not the registry's already-converted duplicate.
            auto snapshot=std::make_shared<TextureRevision>(*resources.snapshot(handle));
            snapshot->pixels=std::shared_ptr<SDL_Surface>(source.release(),SDL_DestroySurface);
            Surface converted(SDL_ConvertSurface(snapshot->pixels.get(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);REQUIRE(converted);
            DrawPacket background;background.destination={0,0,16,16};background.color={0,0,0,255};background.opaque=true;
            DrawPacket image;image.kind=DrawPacket::Kind::Blit;image.texture=snapshot;image.source={0,0,1,1};image.destination={0,0,16,16};
            RenderFrame frame;frame.identity=1;frame.logicalWidth=frame.logicalHeight=16;frame.packets={background,image};
            REQUIRE(fixture.device->submit(std::move(frame)));fixture.device->present();
            Surface actual(fixture.device->captureSynchronously({}),SDL_DestroySurface);REQUIRE(actual);
            Uint8 r,g,b,a,er,eg,eb,ea;REQUIRE(SDL_ReadSurfacePixel(converted.get(),0,0,&er,&eg,&eb,&ea));
            REQUIRE(SDL_ReadSurfacePixel(actual.get(),8,8,&r,&g,&b,&a));
            CHECK(r==Uint8((unsigned(er)*ea+127)/255));CHECK(g==Uint8((unsigned(eg)*ea+127)/255));CHECK(b==Uint8((unsigned(eb)*ea+127)/255));
        }
    }
    GLOB2_TEST_CASE("ordered upload segments flush at vertex capacity without losing following draws", "[display]")
    {
        GLFixture fixture;
        DrawPacket triangles;triangles.kind=DrawPacket::Kind::Triangles;
        triangles.vertices.resize(RenderFrame::VertexCapacity-4);
        for(auto &vertex:triangles.vertices)vertex={{0,0},{1,0,0,1},{0,0}};
        DrawPacket fill;fill.destination={0,0,16,16};fill.color={20,190,30,255};fill.opaque=true;
        RenderFrame frame;frame.identity=1;frame.logicalWidth=frame.logicalHeight=16;
        frame.vertexCount=triangles.vertices.size();frame.packets={std::move(triangles),fill};
        REQUIRE(fixture.device->submit(std::move(frame)));fixture.device->present();
        Surface actual(fixture.device->captureSynchronously({}),SDL_DestroySurface);REQUIRE(actual);
        Uint8 r,g,b,a;REQUIRE(SDL_ReadSurfacePixel(actual.get(),8,8,&r,&g,&b,&a));
        CHECK(r==20);CHECK(g==190);CHECK(b==30);CHECK(a==255);
    }
    GLOB2_TEST_CASE("physical scissors preserve fractional alpha seams", "[display][isolated]")
    {
        GLFixture fixture; auto &device=*fixture.device;
        auto render=[&](bool partition) {
            RenderFrame frame;frame.logicalWidth=23;frame.logicalHeight=19;
            DrawPacket base;base.kind=DrawPacket::Kind::Fill;base.destination={0,0,23,19};base.color={0,0,0,255};frame.packets.push_back(base);
            auto packet=base;packet.color={200,90,40,128};
            if(partition){packet.targetScissor=SDL_Rect{0,0,8,16};frame.packets.push_back(packet);packet.targetScissor=SDL_Rect{8,0,8,16};}
            frame.packets.push_back(packet);REQUIRE(device.submit(std::move(frame)));device.present();return Surface(device.captureSynchronously({}),SDL_DestroySurface);
        };
        auto whole=render(false),partitioned=render(true);REQUIRE(whole);REQUIRE(partitioned);
        for(int y=0;y<16;++y)for(int x=0;x<16;++x){Uint8 a[4],b[4];REQUIRE(SDL_ReadSurfacePixel(whole.get(),x,y,&a[0],&a[1],&a[2],&a[3]));REQUIRE(SDL_ReadSurfacePixel(partitioned.get(),x,y,&b[0],&b[1],&b[2],&b[3]));CHECK(std::memcmp(a,b,4)==0);}
        RenderFrame invalid;invalid.logicalWidth=16;invalid.logicalHeight=16;DrawPacket packet;packet.kind=DrawPacket::Kind::Fill;packet.destination={0,0,16,16};packet.targetScissor=SDL_Rect{0,0,-1,16};invalid.packets.push_back(packet);CHECK_THROWS_AS(device.submit(std::move(invalid)),std::invalid_argument);
    }

    GLOB2_TEST_CASE("GL trims completed retired targets while capture leases remain", "[display]")
    {
        for (bool es : {false, true}) {
            GLFixture fixture(es); auto &device = *fixture.device;
        RenderResources resources;
        auto pixels = surface(64, 64); REQUIRE(pixels);
        TextureDescriptor descriptor; descriptor.depthAttachment = true;
        descriptor.budgetClass = TextureDescriptor::BudgetClass::TerrainPage;
        const auto handle = resources.create(pixels.get(), 1, descriptor); REQUIRE(handle);
        auto snapshot = resources.snapshot(handle);
        std::weak_ptr<const TextureRevision> lifetime = snapshot;
        const auto generation = device.generation();
        DrawPacket clear; clear.kind = DrawPacket::Kind::Clear;
        clear.target = snapshot; clear.clearColor = clear.clearDepth = true;
        clear.clearValue = {111 / 255.f, 67 / 255.f, 23 / 255.f, 1};
        RenderFrame frame; frame.identity = 1; frame.logicalWidth = frame.logicalHeight = 16;
        frame.packets.push_back(std::move(clear));
        const auto submission = device.submit(std::move(frame)); REQUIRE(submission); device.present();
        const auto deadline = SDL_GetTicks() + 5000;
        while (device.completion(submission) == RenderCompletion::Pending && SDL_GetTicks() < deadline) SDL_Delay(1);
        REQUIRE(device.completion(submission) == RenderCompletion::Complete);
        const auto resident = device.telemetry().resourceBytes;
        device.trimRetiredResources();
        CHECK(device.telemetry().resourceBytes == resident); // logical owner still live
        CaptureRequest request; request.identity = 71; request.frameIdentity = 1;
        request.width = request.height = 64; request.target = std::move(snapshot);
        REQUIRE(device.capture(std::move(request)));
        REQUIRE(resources.erase(handle));
        device.trimRetiredResources();
        CHECK_FALSE(lifetime.expired()); // queued capture owns the retiring target
        CHECK(device.telemetry().resourceBytes >= resident);
        CaptureResult result;
        const auto captureDeadline = SDL_GetTicks() + 5000;
        while (!device.pollCapture(result) && SDL_GetTicks() < captureDeadline) SDL_Delay(1);
        REQUIRE(result.pixels); CHECK_FALSE(result.cancelled);
        Uint8 r, g, b, a;
        REQUIRE(SDL_ReadSurfacePixel(result.pixels.get(), 32, 32, &r, &g, &b, &a));
        CHECK(r == 111); CHECK(g == 67); CHECK(b == 23); CHECK(a == 255);
        result.request.target.reset(); result.pixels.reset();
        const auto beforeTrim = device.telemetry().resourceBytes;
        device.trimRetiredResources();
        CHECK(lifetime.expired());
        CHECK(device.telemetry().resourceBytes + 64 * 64 * 4 <= beforeTrim);
        CHECK(device.generation() == generation); CHECK(device.telemetry().captureDrops == 0);
        const auto trimmed = device.telemetry().resourceBytes;
        device.trimRetiredResources(); CHECK(device.telemetry().resourceBytes == trimmed);
        }
    }

    GLOB2_TEST_CASE("explicit GL trim restores live immutable source pixels", "[display]")
    {
        for (bool es : {false, true}) {
            GLFixture fixture(es); auto &device = *fixture.device;
            RenderResources resources;
            auto pixels = surface(64, 64); REQUIRE(pixels);
            REQUIRE(SDL_FillSurfaceRect(pixels.get(), nullptr,
                SDL_MapSurfaceRGBA(pixels.get(), 31, 117, 203, 255)));
            const auto handle = resources.create(pixels.get(), 1); REQUIRE(handle);
            const auto source = resources.snapshot(handle);
            const auto draw = [&](std::uint64_t identity) {
                DrawPacket packet; packet.kind = DrawPacket::Kind::Blit;
                packet.texture = source; packet.source = {0, 0, 64, 64};
                packet.destination = {0, 0, 16, 16};
                RenderFrame frame; frame.identity = identity;
                frame.logicalWidth = frame.logicalHeight = 16;
                frame.packets.push_back(std::move(packet));
                const auto submission = device.submit(std::move(frame)); REQUIRE(submission);
                return submission;
            };
            const auto submission = draw(1);
            const auto pendingBytes = device.telemetry().resourceBytes;
            device.trimRetiredResources();
            CHECK(device.telemetry().resourceBytes == pendingBytes); // current unpresented lease
            device.present();
            const auto deadline = SDL_GetTicks() + 5000;
            while (device.completion(submission) == RenderCompletion::Pending && SDL_GetTicks() < deadline) SDL_Delay(1);
            REQUIRE(device.completion(submission) == RenderCompletion::Complete);
            const auto before = device.telemetry().resourceBytes;
            device.trimRetiredResources();
            CHECK(device.telemetry().resourceBytes + 64 * 64 * 4 <= before);
            REQUIRE(source); // restoration source remains logically owned
            REQUIRE(draw(2)); device.present();
            CaptureRequest request; request.identity = 91; request.frameIdentity = 2;
            request.width = request.height = 16;
            Surface result(device.captureSynchronously(request), SDL_DestroySurface); REQUIRE(result);
            auto expected = surface(); REQUIRE(expected);
            REQUIRE(SDL_FillSurfaceRect(expected.get(), nullptr,
                SDL_MapSurfaceRGBA(expected.get(), 31, 117, 203, 255)));
            equal(expected.get(), result.get());
        }
    }

    GLOB2_TEST_CASE("minification samples authored CPU mip levels without driver regeneration", "[display]")
    {
        for(bool es:{false,true}){
            GLFixture fixture(es);RenderResources resources;auto original=surface(3,3);
            std::vector<std::byte> base(64),half(16),last(4);
            for(unsigned pixel=0;pixel<16;++pixel){base[pixel*4]=std::byte{255};base[pixel*4+3]=std::byte{255};}
            for(unsigned pixel=0;pixel<4;++pixel){half[pixel*4+1]=std::byte{255};half[pixel*4+3]=std::byte{255};}
            last[2]=std::byte{255};last[3]=std::byte{255};
            std::array<RenderMipLevel,3> levels{{{4,4,SDL_PIXELFORMAT_RGBA32,base},{2,2,SDL_PIXELFORMAT_RGBA32,half},{1,1,SDL_PIXELFORMAT_RGBA32,last}}};
            auto backend=makeDeviceRenderBackend(*fixture.device,resources,16,16);backend->fill({0,0,16,16},{0,0,0,255});
            backend->blitMipmapped(original.get(),original.get(),1,levels,{0,0,4,4},{0,0,1,1},255);
            backend->present();Surface actual(backend->capture(),SDL_DestroySurface);Uint8 r,g,b,a;
            REQUIRE(SDL_ReadSurfacePixel(actual.get(),0,0,&r,&g,&b,&a));CHECK(r==0);CHECK(g==0);CHECK(b==255);CHECK(a==255);
            CHECK(fixture.device->telemetry().uploadBytes>=84);
        }
    }

    GLOB2_TEST_CASE("shared CPU pages batch different sprite frames and retain queued revisions", "[display]")
    {
        for(bool es:{false,true}){
            GLFixture fixture(es);RenderResources resources;auto a=surface(1,1),b=surface(1,1);
            REQUIRE(SDL_FillSurfaceRect(a.get(),nullptr,SDL_MapSurfaceRGBA(a.get(),240,0,0,255)));
            REQUIRE(SDL_FillSurfaceRect(b.get(),nullptr,SDL_MapSurfaceRGBA(b.get(),0,240,0,255)));
            std::array<SpriteAtlasSource,2> inputs{{{a.get(),{1,1,1,1}},{b.get(),{2,1,1,1}}}};
            auto page=prepareSpriteRenderAtlas(inputs,2048,false);REQUIRE(page);
            auto handle=resources.create(page->pixels.get(),1);REQUIRE(handle);auto old=resources.snapshot(handle);
            DrawPacket first;first.kind=DrawPacket::Kind::Blit;first.texture=old;first.source=page->regions[0];first.destination={0,0,8,16};
            DrawPacket second=first;second.source=page->regions[1];second.destination={8,0,8,16};
            RenderFrame frame;frame.logicalWidth=frame.logicalHeight=16;frame.packets={first,second};
            const auto before=fixture.device->telemetry().batches;REQUIRE(fixture.device->submit(std::move(frame)));fixture.device->present();
            CHECK(fixture.device->telemetry().batches==before+1);
            CaptureRequest capture;Surface actual(fixture.device->captureSynchronously(capture),SDL_DestroySurface);Uint8 r,g,blue,alpha;
            REQUIRE(SDL_ReadSurfacePixel(actual.get(),2,2,&r,&g,&blue,&alpha));CHECK(r==240);CHECK(g==0);
            REQUIRE(SDL_ReadSurfacePixel(actual.get(),12,2,&r,&g,&blue,&alpha));CHECK(r==0);CHECK(g==240);
            REQUIRE(SDL_FillSurfaceRect(a.get(),nullptr,SDL_MapSurfaceRGBA(a.get(),0,0,240,255)));inputs[0].stamp.revision=2;
            auto changed=prepareSpriteRenderAtlas(inputs,2048,false);REQUIRE(changed);REQUIRE(resources.update(handle,changed->pixels.get(),2));
            first.texture=old;second.texture=resources.snapshot(handle);second.source=changed->regions[0];
            frame={};frame.logicalWidth=frame.logicalHeight=16;frame.packets={first,second};old.reset();page.reset();
            REQUIRE(fixture.device->submit(std::move(frame)));fixture.device->present();actual.reset(fixture.device->captureSynchronously(capture));
            REQUIRE(SDL_ReadSurfacePixel(actual.get(),2,2,&r,&g,&blue,&alpha));CHECK(r==240);CHECK(blue==0);
            REQUIRE(SDL_ReadSurfacePixel(actual.get(),12,2,&r,&g,&blue,&alpha));CHECK(r==0);CHECK(blue==240);
        }
    }
    GLOB2_TEST_CASE("terrain rejects unallocated target feedback before changing the retained frame", "[display]")
    {
        for(bool es:{false,true}){
            GLFixture fixture(es);RenderResources resources;auto backend=makeDeviceRenderBackend(*fixture.device,resources,16,16);
            backend->fill({0,0,16,16},{20,80,160,255});backend->present();Surface before(backend->capture(),SDL_DestroySurface);
            const auto retainedBytes=fixture.device->telemetry().resourceBytes;
            auto source=surface(32,32);auto handle=resources.create(source.get(),1);REQUIRE(handle);
            auto composition=std::make_shared<WeightedComposeData>();composition->compositionSize=32;composition->paletteCount=1;
            composition->reductionDivisor=1;composition->targetRect={0,0,32,32};composition->materials[0]=resources.snapshot(handle);
            DrawPacket packet;packet.kind=DrawPacket::Kind::WeightedCompose;packet.target=resources.snapshot(handle);packet.weightedCompose=composition;
            RenderFrame frame;frame.logicalWidth=frame.logicalHeight=16;frame.packets.push_back(packet);
            CHECK_THROWS_AS(fixture.device->submit(std::move(frame)),std::invalid_argument);
            Surface after(backend->capture(),SDL_DestroySurface);equal(before.get(),after.get());
            CHECK(fixture.device->telemetry().resourceBytes==retainedBytes);
        }
    }
    GLOB2_TEST_CASE("integer terrain composition preserves seam wrapping orientation reduction and PMA", "[display]")
    {
        // Expected bytes come from the CPU compositor's packed-ARGB arithmetic:
        // fringe wrap carries into alpha (255/2 rounds128, then packed OR makes129).
        const std::array<SDL_Color,4> sourceColors{{{200,50,10,255},{20,180,230,200},{10,150,100,160},{240,30,90,0}}};
        const std::array<SDL_Color,4> straight{{{50,112,2,129},{5,145,57,101},{2,137,25,81},{0,100,0,0}}};
        const std::array<SDL_Color,4> premultiplied{{{25,57,1,129},{2,57,23,101},{1,44,8,81},{0,0,0,0}}};
        for(bool es:{false,true}){
            GLFixture fixture(es);REQUIRE(fixture.device->capabilities().terrainComposition);RenderResources resources;
            auto input=surface(2,2);for(unsigned i=0;i<4;++i){const auto c=sourceColors[i];REQUIRE(SDL_WriteSurfacePixel(input.get(),i%2,i/2,c.r,c.g,c.b,c.a));}
            const auto material=resources.create(input.get(),1);
            auto mask=std::make_shared<CoverageData>();mask->identity=19;mask->revision=1;mask->width=mask->height=32;mask->samples.resize(1024);
            for(auto &sample:mask->samples){sample.others={32768,0,0};sample.margin=1;sample.slots=16;}
            auto backend=makeDeviceRenderBackend(*fixture.device,resources,16,16);
            for(bool pma:{false,true})for(unsigned divisor:{1u,2u,4u,8u,16u,32u}){
                auto pixels=surface(40,40);REQUIRE(SDL_FillSurfaceRect(pixels.get(),nullptr,SDL_MapSurfaceRGBA(pixels.get(),1,2,3,255)));
                TextureDescriptor descriptor;descriptor.alpha=pma?TextureDescriptor::Alpha::Premultiplied:TextureDescriptor::Alpha::Straight;descriptor.budgetClass=TextureDescriptor::BudgetClass::TerrainPage;
                const auto page=resources.create(pixels.get(),1,descriptor);REQUIRE(page);
                auto data=std::make_shared<WeightedComposeData>();data->paletteCount=2;data->materials[0]=resources.snapshot(material);data->coverage=mask;
                data->compositionSize=32;data->reductionDivisor=divisor;data->targetRect={3,4,int(32/divisor),int(32/divisor)};
                data->seams[1]={1,256,2,256,2,{0,200,0}};
                DrawPacket packet;packet.kind=DrawPacket::Kind::WeightedCompose;packet.target=resources.snapshot(page);packet.weightedCompose=data;
                REQUIRE(recordDevicePacket(*backend,packet));backend->present();CHECK(didPresentDeviceFrame(*backend));
                CaptureRequest request;request.target=resources.snapshot(page);Surface actual(fixture.device->captureSynchronously(request),SDL_DestroySurface);
                for(int y=0;y<data->targetRect.h;++y)for(int x=0;x<data->targetRect.w;++x){
                    auto c=divisor==32?(pma?SDL_Color{7,39,8,78}:SDL_Color{23,129,26,78}):(pma?premultiplied:straight)[(y*divisor>=16)*2+(x*divisor>=16)];
                    if(divisor>1 && c.a==0)c.r=c.g=c.b=0;
                    Uint8 r,g,b,a;REQUIRE(SDL_ReadSurfacePixel(actual.get(),x+3,y+4,&r,&g,&b,&a));CHECK(r==c.r);CHECK(g==c.g);CHECK(b==c.b);CHECK(a==c.a);
                }
                Uint8 r,g,b,a;REQUIRE(SDL_ReadSurfacePixel(actual.get(),0,0,&r,&g,&b,&a));CHECK(r==1);CHECK(g==2);CHECK(b==3);CHECK(a==255);
                if(pma){
                    auto referencePixels=surface(40,40);REQUIRE(SDL_FillSurfaceRect(referencePixels.get(),nullptr,SDL_MapSurfaceRGBA(referencePixels.get(),1,2,3,255)));
                    for(int y=0;y<data->targetRect.h;++y)for(int x=0;x<data->targetRect.w;++x){const auto c=divisor==32?SDL_Color{23,129,26,78}:straight[(y*divisor>=16)*2+(x*divisor>=16)];
                        REQUIRE(SDL_WriteSurfacePixel(referencePixels.get(),x+3,y+4,c.r,c.g,c.b,c.a));}
                    Surface argbPixels(SDL_ConvertSurface(referencePixels.get(),SDL_PIXELFORMAT_ARGB8888),SDL_DestroySurface);
                    Surface referenceOutput(SDL_CreateSurface(16,16,SDL_PIXELFORMAT_ARGB8888),SDL_DestroySurface);auto software=makeSoftwareRenderBackend(referenceOutput.get());
                    software->fill({0,0,16,16},{40,80,120,255});software->blitLinear(argbPixels.get(),argbPixels.get(),1,data->targetRect,{0,0,16,16},180);software->present();
                    backend->fill({0,0,16,16},{40,80,120,255});DrawPacket blit;blit.kind=DrawPacket::Kind::Blit;blit.texture=resources.snapshot(page);
                    blit.source=data->targetRect;blit.destination={0,0,16,16};blit.alpha=180;blit.linear=true;blit.blend=DrawPacket::Blend::Premultiplied;
                    REQUIRE(recordDevicePacket(*backend,blit));backend->present();Surface presentation(backend->capture(),SDL_DestroySurface);equal(referenceOutput.get(),presentation.get(),2);
                }

            }
        }
    }
    GLOB2_TEST_CASE("full skin atlas color capacity admits independently bounded depth storage", "[display]")
    {
        GLFixture fixture;RenderResources resources;auto pixels=surface(2048,2048);REQUIRE(pixels);
        REQUIRE(SDL_ClearSurface(pixels.get(),0,0,0,0));
        TextureDescriptor descriptor;descriptor.budgetClass=TextureDescriptor::BudgetClass::SkinAtlas;
        descriptor.alpha=TextureDescriptor::Alpha::Premultiplied;descriptor.depthAttachment=true;
        std::array<TextureHandle,4> pages{};auto backend=makeDeviceRenderBackend(*fixture.device,resources,16,16);
        for(unsigned i=0;i<pages.size();++i){pages[i]=resources.create(pixels.get(),1,descriptor);REQUIRE(pages[i]);
            REQUIRE(pushDeviceTarget(*backend,resources.snapshot(pages[i]),2048,2048));
            DrawPacket clear;clear.kind=DrawPacket::Kind::Clear;clear.clearColor=clear.clearDepth=true;clear.clearValue={float(40+i)/255,20.f/255,30.f/255,1};
            REQUIRE(recordDevicePacket(*backend,clear));popDeviceTarget(*backend);}
        backend->present();CHECK(didPresentDeviceFrame(*backend));CHECK(resources.retainedBytes()==64*1024*1024);
        for(unsigned i=0;i<pages.size();++i){CaptureRequest request;request.target=resources.snapshot(pages[i]);request.pixelRegion=SDL_Rect{0,0,1,1};
            Surface result(fixture.device->captureSynchronously(request),SDL_DestroySurface);Uint8 r,g,b,a;
            REQUIRE(SDL_ReadSurfacePixel(result.get(),0,0,&r,&g,&b,&a));CHECK(r==40+i);CHECK(a==255);}
        CHECK(fixture.device->telemetry().resourceBytes>=128*1024*1024);
    }
    GLOB2_TEST_CASE("factory recreation restores retained logical frame and texture revisions", "[display]")
    {
        GLFixture fixture;RenderResources resources;auto pixels=surface(2,2);
        REQUIRE(SDL_FillSurfaceRect(pixels.get(),nullptr,SDL_MapSurfaceRGBA(pixels.get(),43,160,210,255)));
        const auto texture=resources.create(pixels.get(),11);const auto revision=resources.snapshot(texture);
        RenderFrame retained;retained.identity=37;retained.logicalWidth=retained.logicalHeight=16;
        DrawPacket fill;fill.kind=DrawPacket::Kind::Fill;fill.destination={0,0,16,16};fill.color={8,12,24,255};retained.packets.push_back(fill);
        DrawPacket sprite;sprite.kind=DrawPacket::Kind::Blit;sprite.texture=revision;sprite.source={0,0,2,2};sprite.destination={2,3,9,10};retained.packets.push_back(sprite);
        REQUIRE(fixture.device->submit(retained));fixture.device->present();Surface before(fixture.device->captureSynchronously({}),SDL_DestroySurface);
        fixture.device.reset();
        REQUIRE(resources.snapshot(texture)==revision);CHECK(revision->contentRevision==11);
        fixture.device=makeProgrammableGLDevice(fixture.window,fixture.shaderDirectory);
        REQUIRE(fixture.device->submit(retained));fixture.device->present();Surface after(fixture.device->captureSynchronously({}),SDL_DestroySurface);
        equal(before.get(),after.get());CHECK(fixture.device->telemetry().uploadBytes==16);
    }
    GLOB2_TEST_CASE("malformed advertised shader rejects candidate while prior factory remains usable", "[display]")
    {
        GLFixture fixture;
        const auto path=std::filesystem::temp_directory_path()/std::filesystem::path("glob2-bad-renderer-"+std::to_string(SDL_GetPerformanceCounter()));
        struct Remove {std::filesystem::path path;~Remove(){std::error_code error;std::filesystem::remove_all(path,error);}} remove{path};
        std::filesystem::create_directories(path/"desktop-gl");
        for(const auto &entry:std::filesystem::directory_iterator(std::filesystem::path(fixture.shaderDirectory)/"desktop-gl"))
            if(entry.is_regular_file())std::filesystem::copy_file(entry.path(),path/"desktop-gl"/entry.path().filename());
        {std::ofstream broken(path/"desktop-gl"/"skin.frag");broken<<"#version 330 core\nthis is malformed GLSL\n";}
        CHECK_THROWS_AS(makeProgrammableGLDevice(fixture.window,path.string()),std::runtime_error);
        RenderFrame frame;frame.identity=1;frame.logicalWidth=frame.logicalHeight=16;DrawPacket fill;fill.kind=DrawPacket::Kind::Fill;
        fill.destination={0,0,16,16};fill.color={70,130,190,255};frame.packets.push_back(fill);
        REQUIRE(fixture.device->submit(std::move(frame)));fixture.device->present();Surface restored(fixture.device->captureSynchronously({}),SDL_DestroySurface);
        Uint8 r,g,b,a;REQUIRE(SDL_ReadSurfacePixel(restored.get(),8,8,&r,&g,&b,&a));CHECK(r==70);CHECK(g==130);CHECK(b==190);
    }
    GLOB2_TEST_CASE("explicit straight and premultiplied uploads retain identical compositing", "[display]")
    {
        GLFixture fixture;RenderResources resources;auto backend=makeDeviceRenderBackend(*fixture.device,resources,16,16);
        auto pixels=surface(1,1);REQUIRE(SDL_FillSurfaceRect(pixels.get(),nullptr,SDL_MapSurfaceRGBA(pixels.get(),128,0,0,128)));
        auto straight=resources.create(pixels.get(),1);TextureDescriptor descriptor;descriptor.alpha=TextureDescriptor::Alpha::Premultiplied;
        REQUIRE(SDL_FillSurfaceRect(pixels.get(),nullptr,SDL_MapSurfaceRGBA(pixels.get(),64,0,0,128)));
        auto premultiplied=resources.create(pixels.get(),2,descriptor);
        for(bool linear:{false,true}){
            backend->fill({0,0,16,16},{0,0,0,255});
            for(int variant=0;variant<2;++variant){DrawPacket packet;packet.kind=DrawPacket::Kind::Blit;
                packet.texture=resources.snapshot(variant?premultiplied:straight);packet.source={0,0,1,1};packet.destination={float(variant*8),0,8,16};packet.linear=linear;
                REQUIRE(recordDevicePacket(*backend,packet));}
            backend->present();Surface result(backend->capture(),SDL_DestroySurface);Uint8 r,g,b,a;
            for(int x:{2,10}){REQUIRE(SDL_ReadSurfacePixel(result.get(),x,8,&r,&g,&b,&a));CHECK(std::abs(int(r)-64)<=1);CHECK(a==255);}
        }
    }
    GLOB2_TEST_CASE("authoritative skin rig matches fixed geometry and cropped capture retains target", "[display]")
    {
        GLFixture fixture(true); REQUIRE(fixture.device->capabilities().skinMaterials);
        RenderResources resources; auto backend=makeDeviceRenderBackend(*fixture.device,resources,16,16);
        auto pixels=surface(4,4); REQUIRE(SDL_FillSurfaceRect(pixels.get(),nullptr,SDL_MapSurfaceRGBA(pixels.get(),20,220,80,255)));
        SDL_Rect quadrant{0,0,2,2}; REQUIRE(SDL_FillSurfaceRect(pixels.get(),&quadrant,SDL_MapSurfaceRGBA(pixels.get(),180,110,60,255)));
        auto paint=resources.create(pixels.get(),1); REQUIRE(SDL_FillSurfaceRect(pixels.get(),nullptr,SDL_MapSurfaceRGBA(pixels.get(),0,0,0,255)));
        auto materials=resources.create(pixels.get(),2); auto atlasPixels=surface(16,16);
        TextureDescriptor descriptor; descriptor.budgetClass=TextureDescriptor::BudgetClass::SkinAtlas; descriptor.depthAttachment=true;
        auto atlas=resources.create(atlasPixels.get(),1,descriptor); REQUIRE(atlas);
        std::array<std::array<float,10>,6> fixed{{{{-1.25f,1.25f,0,0,0,1,0,0,0,0}},{{-1.25f,-1.25f,0,0,0,1,0,1,0,1}},{{1.25f,1.25f,0,0,0,1,1,0,1,0}},
            {{1.25f,1.25f,0,0,0,1,1,0,1,0}},{{-1.25f,-1.25f,0,0,0,1,0,1,0,1}},{{1.25f,-1.25f,0,0,0,1,1,1,1,1}}}};
        std::array<std::array<float,18>,6> rig{};
        for(std::size_t i=0;i<fixed.size();++i){std::copy(fixed[i].begin(),fixed[i].end(),rig[i].begin());rig[i][14]=1;}
        auto fixedGeometry=resources.createGeometry(GeometryLayout::Skin40,std::as_bytes(std::span(fixed)),{},1);
        auto rigGeometry=resources.createGeometry(GeometryLayout::SkinRig72,std::as_bytes(std::span(rig)),{},1);
        auto pose=std::make_shared<SkinPose>();
        const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        for(int i=0;i<32;++i)std::copy(identity.begin(),identity.end(),pose->bones.begin()+i*16);
        pose->view=identity; pose->normalView={1,0,0,0,1,0,0,0,1};
        auto material=std::make_shared<SkinMaterial>(); material->pose=pose;
        REQUIRE(pushDeviceTarget(*backend,resources.snapshot(atlas),16,16));
        DrawPacket clear; clear.kind=DrawPacket::Kind::Clear; clear.clearColor=clear.clearDepth=true; REQUIRE(recordDevicePacket(*backend,clear));
        for(int variant=0;variant<2;++variant){
            DrawPacket packet; packet.kind=DrawPacket::Kind::MeshTriangles; packet.material=variant?DrawPacket::Material::SkinRig:DrawPacket::Material::Skin;
            packet.skin=material;packet.geometry=resources.snapshot(variant?rigGeometry:fixedGeometry);packet.count=6;
            packet.texture=resources.snapshot(paint);packet.secondTexture=resources.snapshot(materials);packet.viewport=SDL_Rect{variant*8,0,8,16};
            packet.depth=DrawPacket::Depth::LessEqual;packet.depthWrite=true;packet.blend=DrawPacket::Blend::Opaque;
            REQUIRE(recordDevicePacket(*backend,packet));
        }
        popDeviceTarget(*backend);backend->present();
        CaptureRequest request;request.target=resources.snapshot(atlas);request.pixelRegion=SDL_Rect{0,0,8,16};request.timestamp=17;
        REQUIRE(fixture.device->capture(request));
        Surface left(fixture.device->captureSynchronously(request),SDL_DestroySurface);
        request.pixelRegion=SDL_Rect{8,0,8,16};Surface right(fixture.device->captureSynchronously(request),SDL_DestroySurface);equal(left.get(),right.get());
        Uint8 r,g,b,a;REQUIRE(SDL_ReadSurfacePixel(left.get(),4,8,&r,&g,&b,&a));CHECK(r>50);CHECK(a==255);
        resources.erase(atlas);CaptureResult completed;const auto deadline=SDL_GetTicks()+2000;
        while(!fixture.device->pollCapture(completed)&&SDL_GetTicks()<deadline)SDL_Delay(1);
        REQUIRE(completed.pixels);CHECK(completed.request.timestamp==17);equal(left.get(),completed.pixels.get());
        request.pixelRegion=SDL_Rect{-1,0,8,16};CHECK_FALSE(fixture.device->capture(request));
    }
    GLOB2_TEST_CASE("world sampling preserves offscreen top-left orientation and fractional target size", "[display]")
    {
        GLFixture fixture(true); REQUIRE(fixture.device->capabilities().mesh3D);
        RenderResources resources; auto backend = makeDeviceRenderBackend(*fixture.device, resources, 16, 16);
        auto pixels = surface(4,4); REQUIRE(pixels); REQUIRE(SDL_FillSurfaceRect(pixels.get(),nullptr,0));
        auto image = resources.create(pixels.get(),1); auto target = resources.snapshot(image);
        REQUIRE(pushDeviceTarget(*backend,target,16.25f,16.25f));
        backend->fill({0,0,16.25f,8.125f},{255,0,0,255});
        backend->fill({0,8.125f,16.25f,8.125f},{0,255,0,255}); popDeviceTarget(*backend);
        std::array<WorldVertex,6> vertices{{{{-1,1,.5f,1},{1,1,1},{0,0},{0,0,1}},
            {{-1,-1,.5f,1},{1,1,1},{0,1},{0,0,1}}, {{1,1,.5f,1},{1,1,1},{1,0},{0,0,1}},
            {{1,1,.5f,1},{1,1,1},{1,0},{0,0,1}}, {{-1,-1,.5f,1},{1,1,1},{0,1},{0,0,1}},
            {{1,-1,.5f,1},{1,1,1},{1,1},{0,0,1}}}};
        auto geometry = resources.createGeometry(GeometryLayout::World48,std::as_bytes(std::span(vertices)),{},1); REQUIRE(geometry);
        DrawPacket packet; packet.kind=DrawPacket::Kind::MeshTriangles; packet.material=DrawPacket::Material::WorldLit;
        packet.geometry=resources.snapshot(geometry); packet.texture=target; packet.count=6; packet.blend=DrawPacket::Blend::Opaque;
        packet.world.projection={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        REQUIRE(recordDevicePacket(*backend,packet)); backend->present(); Surface result(backend->capture(),SDL_DestroySurface);
        Uint8 r,g,b,a; REQUIRE(SDL_ReadSurfacePixel(result.get(),8,2,&r,&g,&b,&a)); CHECK(r==255); CHECK(g==0);
        REQUIRE(SDL_ReadSurfacePixel(result.get(),8,13,&r,&g,&b,&a)); CHECK(g==255); CHECK(r==0);
    }
    GLOB2_TEST_CASE("world depth clipping and overlay state preserve ordered rendering", "[display]")
    {
        GLFixture fixture; REQUIRE(fixture.device->capabilities().torus);
        RenderResources resources; auto backend = makeDeviceRenderBackend(*fixture.device, resources, 16, 16);
        backend->fill({0, 0, 16, 16}, {255, 255, 0, 255});
        DrawPacket clear; clear.kind = DrawPacket::Kind::Clear; clear.clearDepth = true;
        REQUIRE(recordDevicePacket(*backend, clear));
        clear.clearDepth = false; clear.clearColor = true; clear.clearValue = {0, 0, 0, 1}; clear.clip = SDL_Rect{0, 2, 16, 14};
        REQUIRE(recordDevicePacket(*backend, clear));
        auto mesh = [&](float depth, float r, float g) {
            std::array<WorldVertex, 3> vertices{{{{-1,-1,depth,1},{r,g,0},{0,0},{0,0,1}},
                {{1,-1,depth,1},{r,g,0},{1,0},{0,0,1}}, {{0,1,depth,1},{r,g,0},{.5f,1},{0,0,1}}}};
            auto handle = resources.createGeometry(GeometryLayout::World48, std::as_bytes(std::span(vertices)), {}, 1);
            REQUIRE(handle);
            DrawPacket packet; packet.kind = DrawPacket::Kind::MeshTriangles; packet.material = DrawPacket::Material::WorldLit;
            packet.geometry = resources.snapshot(handle); packet.count = 3;
            packet.world.projection = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            packet.depth = DrawPacket::Depth::LessEqual; packet.depthWrite = true;
            packet.blend = DrawPacket::Blend::Opaque; packet.clip = SDL_Rect{0,2,16,14};
            REQUIRE(recordDevicePacket(*backend, packet)); resources.erase(handle);
        };
        mesh(.25f,1,0); mesh(.75f,0,1);
        backend->fill({7,7,2,2},{0,0,255,255}); backend->present();
        Surface result(backend->capture(), SDL_DestroySurface); REQUIRE(result); Uint8 r,g,b,a;
        REQUIRE(SDL_ReadSurfacePixel(result.get(),8,0,&r,&g,&b,&a)); CHECK(r==255); CHECK(g==255);
        REQUIRE(SDL_ReadSurfacePixel(result.get(),8,10,&r,&g,&b,&a)); CHECK(r==255); CHECK(g==0);
        REQUIRE(SDL_ReadSurfacePixel(result.get(),8,8,&r,&g,&b,&a)); CHECK(b==255);
        CHECK(resources.retainedGeometryBytes()==0);
    }
    GLOB2_TEST_CASE("desktop core primitives match CPU reference with alpha tolerance", "[display]")
    {
        GLFixture fixture;
        auto target = surface(); auto source = surface(2, 2); REQUIRE(target); REQUIRE(source);
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, SDL_MapSurfaceRGBA(source.get(), 255, 255, 0, 255)));
        const SDL_Rect lower{0, 1, 2, 1}; REQUIRE(SDL_FillSurfaceRect(source.get(), &lower, SDL_MapSurfaceRGBA(source.get(), 255, 0, 255, 255)));
        RenderResources gpuResources, cpuResources;
        auto cpuDevice = makeSoftwareRenderDevice(target.get());
        auto gpu = makeDeviceRenderBackend(*fixture.device, gpuResources, 16, 16);
        auto cpu = makeDeviceRenderBackend(*cpuDevice, cpuResources, 16, 16);
        scene(*gpu, source.get()); scene(*cpu, source.get());
        Surface capture(gpu->capture(), SDL_DestroySurface); equal(target.get(), capture.get(), 1);
        CHECK(programmableGLContext(*fixture.device));
        CHECK(fixture.device->telemetry().batches < fixture.device->telemetry().packets);
    }
    GLOB2_TEST_CASE("GLES300 shaders draw retained partial submissions", "[display]")
    {
        GLFixture fixture(true);
        RenderResources resources; auto backend = makeDeviceRenderBackend(*fixture.device, resources, 16, 16);
        backend->fill({0, 0, 16, 16}, {255, 0, 0, 255}); backend->flush();
        backend->fill({8, 0, 8, 16}, {0, 255, 0, 255}); backend->present();
        Surface result(backend->capture(), SDL_DestroySurface);
        Uint8 r, g, b, a;
        REQUIRE(SDL_ReadSurfacePixel(result.get(), 1, 1, &r, &g, &b, &a)); CHECK(r == 255);
        REQUIRE(SDL_ReadSurfacePixel(result.get(), 9, 1, &r, &g, &b, &a)); CHECK(g == 255);
    }
    GLOB2_TEST_CASE("linear sampling premultiplies transparent edge colours", "[display]")
    {
        GLFixture fixture;
        Surface target(SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        Surface source(SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface); REQUIRE(target); REQUIRE(source);
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, SDL_MapSurfaceRGBA(source.get(), 0, 255, 0, 0)));
        const SDL_Rect visible{0, 0, 1, 2}; REQUIRE(SDL_FillSurfaceRect(source.get(), &visible, SDL_MapSurfaceRGBA(source.get(), 255, 0, 0, 255)));
        RenderResources gpuResources, cpuResources; auto cpuDevice = makeSoftwareRenderDevice(target.get());
        auto gpu = makeDeviceRenderBackend(*fixture.device, gpuResources, 16, 16); auto cpu = makeDeviceRenderBackend(*cpuDevice, cpuResources, 16, 16);
        for (auto *backend : {gpu.get(), cpu.get()}) {
            backend->fill({0, 0, 16, 16}, {0, 0, 64, 255});
            backend->blitLinear(source.get(), source.get(), 1, {0, 0, 2, 2}, {2, 2, 12, 12}, 128);
            backend->present();
        }
        Surface result(gpu->capture(), SDL_DestroySurface); equal(target.get(), result.get(), 2);
    }
    GLOB2_TEST_CASE("linear atlas bounds isolate cropped neighbours", "[display]")
    {
        GLFixture fixture;
        Surface target(SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        Surface source(SDL_CreateSurface(4, 2, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface); REQUIRE(target); REQUIRE(source);
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, SDL_MapSurfaceRGBA(source.get(), 0, 255, 0, 255)));
        const SDL_Rect red{1, 0, 1, 2}; REQUIRE(SDL_FillSurfaceRect(source.get(), &red, SDL_MapSurfaceRGBA(source.get(), 255, 0, 0, 255)));
        RenderResources gpuResources, cpuResources; auto cpuDevice = makeSoftwareRenderDevice(target.get());
        auto gpu = makeDeviceRenderBackend(*fixture.device, gpuResources, 16, 16); auto cpu = makeDeviceRenderBackend(*cpuDevice, cpuResources, 16, 16);
        for (auto *backend : {gpu.get(), cpu.get()}) {
            backend->fill({0, 0, 16, 16}, {0, 0, 64, 255});
            backend->blitLinear(source.get(), source.get(), 1, red, {2, 2, 12, 12}, 128);
            backend->present();
        }
        Surface result(gpu->capture(), SDL_DestroySurface); equal(target.get(), result.get(), 2);
    }
    GLOB2_TEST_CASE("many bounded command flushes remain one presentation frame", "[display]")
    {
        GLFixture fixture; RenderResources resources; auto backend = makeDeviceRenderBackend(*fixture.device, resources, 16, 16);
        for (std::size_t i = 0; i < RenderFrame::CommandCapacity * 12; ++i)
            backend->fill({0, 0, 16, 16}, {Uint8(i % 256), 0, 0, 255});
        backend->present();
        Surface result(backend->capture(), SDL_DestroySurface); Uint8 r, g, b, a;
        REQUIRE(SDL_ReadSurfacePixel(result.get(), 0, 0, &r, &g, &b, &a)); CHECK(r == 255);
        CHECK(fixture.device->telemetry().batches == 12);
        CHECK(fixture.device->telemetry().packets == RenderFrame::CommandCapacity * 12);
    }
    GLOB2_TEST_CASE("offscreen targets preserve CPU orientation and later sampling", "[display]")
    {
        GLFixture fixture; RenderResources resources; auto initial = surface(2, 2); REQUIRE(initial);
        REQUIRE(SDL_FillSurfaceRect(initial.get(), nullptr, SDL_MapSurfaceRGBA(initial.get(), 255, 0, 0, 255)));
        const SDL_Rect lower{0, 1, 2, 1}; REQUIRE(SDL_FillSurfaceRect(initial.get(), &lower, SDL_MapSurfaceRGBA(initial.get(), 0, 255, 0, 255)));
        const auto handle = resources.create(initial.get(), 1); auto snapshot = resources.snapshot(handle);
        RenderFrame offscreen; offscreen.logicalWidth = 2; offscreen.logicalHeight = 2;
        DrawPacket paint; paint.target = snapshot; paint.destination = {0, 0, 1, 1}; paint.color = {0, 0, 255, 255};
        offscreen.packets.push_back(paint); REQUIRE(fixture.device->submit(std::move(offscreen)));
        RenderFrame frame; frame.logicalWidth = 16; frame.logicalHeight = 16;
        DrawPacket show; show.kind = DrawPacket::Kind::Blit; show.texture = snapshot; show.source = {0, 0, 2, 2}; show.destination = {0, 0, 16, 16}; show.opaque = true;
        frame.packets.push_back(show); REQUIRE(fixture.device->submit(std::move(frame))); fixture.device->present();
        Surface captured(fixture.device->captureSynchronously({}), SDL_DestroySurface); Uint8 r, g, b, a;
        REQUIRE(SDL_ReadSurfacePixel(captured.get(), 1, 1, &r, &g, &b, &a)); CHECK(b == 255);
        REQUIRE(SDL_ReadSurfacePixel(captured.get(), 9, 1, &r, &g, &b, &a)); CHECK(r == 255);
        REQUIRE(SDL_ReadSurfacePixel(captured.get(), 1, 9, &r, &g, &b, &a)); CHECK(g == 255);
    }
    GLOB2_TEST_CASE("translucent offscreen pixels retain premultiplied representation", "[display]")
    {
        GLFixture fixture; RenderResources resources; auto initial = surface(2, 2); REQUIRE(initial);
        REQUIRE(SDL_FillSurfaceRect(initial.get(), nullptr, SDL_MapSurfaceRGBA(initial.get(), 0, 0, 0, 0)));
        TextureDescriptor descriptor; descriptor.alpha=TextureDescriptor::Alpha::Premultiplied;
        const auto handle = resources.create(initial.get(), 1, descriptor); auto snapshot = resources.snapshot(handle);
        RenderFrame offscreen; offscreen.logicalWidth = 2; offscreen.logicalHeight = 2;
        DrawPacket paint; paint.target = snapshot; paint.destination = {0, 0, 2, 2}; paint.color = {255, 0, 0, 128};
        offscreen.packets.push_back(paint); REQUIRE(fixture.device->submit(std::move(offscreen)));
        RenderFrame frame; frame.logicalWidth = 16; frame.logicalHeight = 16;
        DrawPacket show; show.kind = DrawPacket::Kind::Blit; show.texture = snapshot; show.source = {0, 0, 2, 2}; show.destination = {0, 0, 16, 16}; show.alpha = 128;
        frame.packets.push_back(show); REQUIRE(fixture.device->submit(std::move(frame))); fixture.device->present();
        Surface captured(fixture.device->captureSynchronously({}), SDL_DestroySurface); Uint8 r, g, b, a;
        REQUIRE(SDL_ReadSurfacePixel(captured.get(), 1, 1, &r, &g, &b, &a));
        CHECK(std::abs(int(r) - 64) <= 1); CHECK(g == 0); CHECK(b == 0); CHECK(a == 255);
    }
    GLOB2_TEST_CASE("capture queues retain timestamps orientation and screenshot independence", "[display]")
    {
        GLFixture fixture; RenderResources resources; auto backend = makeDeviceRenderBackend(*fixture.device, resources, 16, 16);
        backend->fill({0, 0, 16, 8}, {255, 0, 0, 255}); backend->fill({0, 8, 16, 8}, {0, 255, 0, 255}); backend->present();
        CaptureRequest request; request.identity = 42; request.timestamp = 12345; request.context = "menu"; request.recordingGeneration = 7;
        REQUIRE(fixture.device->capture(request)); REQUIRE(fixture.device->capture(request)); REQUIRE(fixture.device->capture(request));
        CHECK_FALSE(fixture.device->capture(request));
        Surface screenshot(backend->capture(), SDL_DestroySurface); REQUIRE(screenshot);
        CaptureResult result;
        const auto deadline = SDL_GetTicks() + 2000;
        while (!fixture.device->pollCapture(result) && SDL_GetTicks() < deadline) SDL_Delay(1);
        REQUIRE(result.pixels); CHECK(result.request.identity == 42); CHECK(result.request.timestamp == 12345);
        CHECK(result.request.context == "menu"); CHECK(result.request.recordingGeneration == 7);
        equal(screenshot.get(), result.pixels.get());
        fixture.device->cancelCaptures(); CHECK_FALSE(fixture.device->pollCapture(result));
        CHECK(fixture.device->telemetry().captureDrops == 3);
    }
    GLOB2_TEST_CASE("discarded partial frame restores prior output and retains admitted capture", "[display]")
    {
        GLFixture fixture;
        RenderResources resources; auto backend=makeDeviceRenderBackend(*fixture.device,resources,16,16);
        backend->fill({0,0,16,16},{220,20,10,255});backend->present();
        Surface previous(backend->capture(),SDL_DestroySurface);
        CaptureRequest ticket;ticket.timestamp=73;REQUIRE(fixture.device->capture(ticket));
        backend->fill({0,0,16,16},{10,200,30,255});backend->flush();fixture.device->discardFrame();
        Surface restored(fixture.device->captureSynchronously({}),SDL_DestroySurface);equal(previous.get(),restored.get());
        CaptureResult result;const auto deadline=SDL_GetTicks()+2000;
        while(!fixture.device->pollCapture(result)&&SDL_GetTicks()<deadline)SDL_Delay(1);
        REQUIRE(result.pixels);CHECK(result.request.timestamp==73);equal(previous.get(),result.pixels.get());
        backend->fill({0,0,16,16},{10,20,240,255});backend->present();
        Surface next(backend->capture(),SDL_DestroySurface);Uint8 r,g,b,a;REQUIRE(SDL_ReadSurfacePixel(next.get(),8,8,&r,&g,&b,&a));CHECK(b==240);
    }
    GLOB2_TEST_CASE("old texture revisions queued draws and device generation survive mutation", "[display]")
    {
        GLFixture fixture; RenderResources resources; auto backend = makeDeviceRenderBackend(*fixture.device, resources, 16, 16);
        auto source = surface(1, 1); REQUIRE(source);
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, SDL_MapSurfaceRGBA(source.get(), 255, 0, 0, 255)));
        backend->blit(source.get(), source.get(), 1, true, {0, 0, 1, 1}, {0, 0, 8, 16}, 255);
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, SDL_MapSurfaceRGBA(source.get(), 0, 255, 0, 255)));
        backend->blit(source.get(), source.get(), 2, true, {0, 0, 1, 1}, {8, 0, 8, 16}, 255);
        backend->forget(source.get()); source.reset(); backend->present();
        Surface captured(backend->capture(), SDL_DestroySurface); Uint8 r, g, b, a;
        REQUIRE(SDL_ReadSurfacePixel(captured.get(), 1, 1, &r, &g, &b, &a)); CHECK(r == 255);
        REQUIRE(SDL_ReadSurfacePixel(captured.get(), 9, 1, &r, &g, &b, &a)); CHECK(g == 255);
        RenderFrame frame; frame.logicalWidth = 16; frame.logicalHeight = 16;
        auto submission = fixture.device->submit(std::move(frame)); REQUIRE(submission);
        const auto generation = fixture.device->generation(); fixture.device->reset();
        CHECK(fixture.device->generation() == generation + 1);
        CHECK(fixture.device->completion(submission) == RenderCompletion::Cancelled);
    }
}

#endif
