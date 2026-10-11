// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <cstring>
#include <SDLGPUDevice.h>
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#ifdef GLOB2_TEST_COMPARE_GL
#include <ProgrammableGLDevice.h>
#endif
#include <array>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <fstream>

using namespace GAGCore;
namespace
{
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
Surface surface(int width, int height)
{
    return Surface(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
}
SDL_Color pixel(SDL_Surface *surface, int x, int y)
{
    SDL_Color value{};
    REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &value.r, &value.g, &value.b, &value.a));
    return value;
}
void nearPixel(SDL_Surface *actual, SDL_Surface *reference, int x, int y, int tolerance = 2)
{
    const auto a = pixel(actual, x, y), b = pixel(reference, x, y);
    INFO("pixel " << x << "," << y);
    CHECK(std::abs(int(a.r) - b.r) <= tolerance);
    CHECK(std::abs(int(a.g) - b.g) <= tolerance);
    CHECK(std::abs(int(a.b) - b.b) <= tolerance);
    CHECK(std::abs(int(a.a) - b.a) <= tolerance);
}
CaptureResult waitCapture(RenderDevice &device)
{
    CaptureResult result;
    const auto deadline = SDL_GetTicks() + 10000;
    while (!device.pollCapture(result) && SDL_GetTicks() < deadline) SDL_Delay(1);
    REQUIRE(result.request.identity != 0);
    return result;
}
DrawPacket fill(SDL_FRect rect, SDL_Color color)
{
    DrawPacket packet; packet.kind = DrawPacket::Kind::Fill; packet.destination = rect; packet.color = color; return packet;
}
DrawPacket blit(TextureSnapshot texture, SDL_FRect rect)
{
    DrawPacket packet; packet.kind = DrawPacket::Kind::Blit; packet.texture = std::move(texture);
    packet.source = {0, 0, packet.texture->descriptor.width, packet.texture->descriptor.height}; packet.destination = rect; return packet;
}
struct Video
{
    Video() { REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO)); }
    ~Video() { SDL_QuitSubSystem(SDL_INIT_VIDEO); }
};
}
TEST_SUITE("SDLGPUDevice")
{
    GLOB2_TEST_CASE("Texture memory follows allocation lifetime across repeated leases and failed frames", "[display][isolated]")
    {
        const char *shaders = std::getenv("GLOB2_TEST_SHADER_DIRECTORY"); REQUIRE(shaders);
        Video video; auto device = makeSDLGPUDevice(nullptr, 16, 16, shaders);
        RenderResources resources; auto pixels = surface(8, 8); REQUIRE(pixels);
        auto texture = resources.snapshot(resources.create(pixels.get(), 1)); REQUIRE(texture);
        RenderFrame frame; frame.identity = 1; frame.logicalWidth = frame.logicalHeight = 16;
        frame.packets.assign(1000, blit(texture, {0, 0, 8, 8}));
        const auto submission = device->submit(frame); REQUIRE(submission); device->present();
        Surface captured(device->captureSynchronously({}), SDL_DestroySurface); REQUIRE(captured);
        REQUIRE(device->completion(submission) == RenderCompletion::Complete);
        // Screen, committed screen, white pixel and one shared texture. Neither
        // repeated packet leases nor completed staging allocations add residency.
        const std::size_t expected = 2 * 16 * 16 * 4 + 4 + 8 * 8 * 4;
        CHECK(device->telemetry().resourceBytes == expected);
        auto fresh = resources.snapshot(resources.create(pixels.get(), 2)); REQUIRE(fresh);
        DrawPacket clear; clear.kind = DrawPacket::Kind::Clear; clear.target = fresh;
        clear.clearColor = clear.clearDepth = true;
        frame.packets = {clear, blit(fresh, {0, 0, 8, 8}), fill({0, 0, -1, 8}, {})};
        CHECK_THROWS_AS(device->submit(frame), std::invalid_argument);
        device->trimRetiredResources();
        CHECK(device->telemetry().resourceBytes == 2 * 16 * 16 * 4 + 4);
        // Releasing the failed allocation must not poison later admission.
        frame.packets = {blit(fresh, {0, 0, 8, 8})};
        REQUIRE(device->submit(frame)); device->present();
        device->reset();
        CHECK(device->telemetry().resourceBytes == 2 * 16 * 16 * 4 + 4);
    }

    GLOB2_TEST_CASE("Expanded rectangles do not consume recorded triangle capacity", "[display][isolated]")
    {
        const char *shaders = std::getenv("GLOB2_TEST_SHADER_DIRECTORY"); REQUIRE(shaders);
        Video video; auto device = makeSDLGPUDevice(nullptr, 16, 16, shaders);
        RenderFrame frame; frame.logicalWidth = frame.logicalHeight = 16; frame.identity = 1;
        frame.packets.push_back(fill({0, 0, 16, 16}, {0, 0, 255, 255}));
        DrawPacket triangles; triangles.kind = DrawPacket::Kind::Triangles;
        // Fill the recorded budget with degenerate triangles, then one visible
        // triangle. The background rectangle contributes six additional vertices.
        triangles.vertices.resize(RenderFrame::VertexCapacity / 3 * 3);
        const std::array<SDL_Vertex, 3> visible{{{{2, 2}, {1, 0, 0, 1}, {}},
            {{14, 2}, {1, 0, 0, 1}, {}}, {{2, 14}, {1, 0, 0, 1}, {}}}};
        std::copy(visible.begin(), visible.end(), triangles.vertices.end() - 3);
        frame.vertexCount = triangles.vertices.size(); frame.packets.push_back(std::move(triangles));
        REQUIRE(device->submit(std::move(frame))); device->present();
        Surface image(device->captureSynchronously({}), SDL_DestroySurface); REQUIRE(image);
        CHECK(pixel(image.get(), 4, 4).r == 255);
        CHECK(pixel(image.get(), 15, 15).b == 255);
        RenderFrame oversized; oversized.logicalWidth = oversized.logicalHeight = 16;
        DrawPacket invalid; invalid.kind = DrawPacket::Kind::Triangles;
        invalid.vertices.resize(RenderFrame::VertexCapacity + 2);
        oversized.packets.push_back(std::move(invalid));
        // An understated vertexCount cannot bypass the recorded geometry bound.
        CHECK_THROWS_AS(device->submit(std::move(oversized)), std::length_error);
    }

    GLOB2_TEST_CASE("Vulkan batches the bounded primitive stream without crossing barriers", "[display][isolated]")
    {
        const char *shaders = std::getenv("GLOB2_TEST_SHADER_DIRECTORY"); REQUIRE(shaders);
        Video video; auto device = makeSDLGPUDevice(nullptr, 64, 64, shaders);
        RenderFrame frame; frame.logicalWidth = frame.logicalHeight = 64; frame.identity = 1;
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x)
            frame.packets.push_back(fill({float(x), float(y), 1, 1}, {Uint8(x * 4), Uint8(y * 4), Uint8(x + y), 255}));
        REQUIRE(frame.packets.size() == RenderFrame::CommandCapacity);
        auto before = device->telemetry(); REQUIRE(device->submit(std::move(frame))); device->present();
        CHECK(device->telemetry().packets - before.packets == RenderFrame::CommandCapacity);
        CHECK(device->telemetry().batches - before.batches == 1);
        Surface image(device->captureSynchronously({}), SDL_DestroySurface); REQUIRE(image);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            const auto color = pixel(image.get(), x, y);
            CHECK(color.r == x * 4); CHECK(color.g == y * 4); CHECK(color.b == x + y); CHECK(color.a == 255);
        }
        RenderFrame separated; separated.logicalWidth = separated.logicalHeight = 64; separated.identity = 2;
        separated.packets.push_back(fill({0, 0, 64, 64}, {255, 0, 0, 255}));
        separated.packets.push_back(fill({0, 0, 32, 64}, {0, 255, 0, 128}));
        DrawPacket barrier; barrier.kind = DrawPacket::Kind::Barrier; separated.packets.push_back(barrier);
        separated.packets.push_back(fill({0, 0, 16, 64}, {0, 0, 255, 128}));
        before = device->telemetry(); REQUIRE(device->submit(std::move(separated))); device->present();
        CHECK(device->telemetry().batches - before.batches == 2);
        image.reset(device->captureSynchronously({})); REQUIRE(image);
        const auto layered = pixel(image.get(), 8, 8);
        CHECK(std::abs(int(layered.r) - 63) <= 1); CHECK(std::abs(int(layered.g) - 64) <= 1);
        CHECK(std::abs(int(layered.b) - 128) <= 1); CHECK(layered.a == 255);
    }

    GLOB2_TEST_CASE("Vulkan reused primitive passes match separated state and target changes", "[display][isolated]")
    {
        const char *shaders = std::getenv("GLOB2_TEST_SHADER_DIRECTORY"); REQUIRE(shaders);
        Video video; auto device = makeSDLGPUDevice(nullptr, 32, 24, shaders);
        RenderResources resources;
        auto source = surface(4, 2), blank = surface(16, 8); REQUIRE(source); REQUIRE(blank);
        REQUIRE(SDL_FillSurfaceRect(blank.get(), nullptr, SDL_MapSurfaceRGBA(blank.get(), 0, 0, 0, 0)));
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x)
            REQUIRE(SDL_WriteSurfacePixel(source.get(), x, y, x < 2 ? 237 : 31, y ? 117 : 203, x < 2 ? 41 : 219, x ? 173 : 255));
        const auto texture = resources.snapshot(resources.create(source.get(), 1));
        TextureDescriptor desc; desc.alpha = TextureDescriptor::Alpha::Premultiplied;
        const auto target = resources.snapshot(resources.create(blank.get(), 1, desc));
        std::vector<DrawPacket> packets;
        packets.push_back(fill({0, 0, 32, 24}, {19, 37, 59, 255}));
        auto overlay = fill({1, 1, 25, 20}, {210, 30, 100, 128}); packets.push_back(overlay);
        overlay.color = {20, 200, 70, 93}; overlay.destination = {8, 3, 23, 17}; packets.push_back(overlay);
        auto first = blit(texture, {0, 0, 12, 10}); first.source = {0, 0, 2, 2}; first.linear = true; packets.push_back(first);
        auto second = first; second.source = {2, 0, 2, 2}; second.destination = {10, 1, 12, 10}; second.alpha = 127;
        second.clip = SDL_Rect{12, 2, 8, 7}; packets.push_back(second);
        auto small = fill({0, 0, 32, 24}, {255, 255, 0, 255}); small.viewport = SDL_Rect{16, 12, 16, 12}; packets.push_back(small);
        auto normal = fill({24, 16, 8, 8}, {0, 0, 255, 255}); packets.push_back(normal); // restores the default viewport and scissor
        DrawPacket clear; clear.kind = DrawPacket::Kind::Clear; clear.clearColor = true; clear.clearValue = {0, 0, 0, 0};
        clear.target = target; packets.push_back(clear);
        auto offscreen = fill({0, 0, 16, 8}, {200, 80, 30, 128}); offscreen.target = target;
        offscreen.targetLogicalWidth = 16; offscreen.targetLogicalHeight = 8; packets.push_back(offscreen);
        auto sampled = blit(target, {2, 13, 12, 8}); sampled.alpha = 191; packets.push_back(sampled);
        auto clippedClear = clear; clippedClear.target.reset(); clippedClear.clearValue = {.1f, .6f, .3f, 1};
        clippedClear.targetScissor = SDL_Rect{0, 20, 5, 4}; packets.push_back(clippedClear);
        auto render = [&](bool barriers) {
            RenderFrame frame; frame.logicalWidth = 32; frame.logicalHeight = 24;
            for (const auto &packet : packets) {
                frame.packets.push_back(packet);
                if (barriers) { DrawPacket barrier; barrier.kind = DrawPacket::Kind::Barrier; frame.packets.push_back(barrier); }
            }
            REQUIRE(device->submit(std::move(frame))); device->present();
            return Surface(device->captureSynchronously({}), SDL_DestroySurface);
        };
        const auto reference = render(true), batched = render(false); REQUIRE(reference); REQUIRE(batched);
        for (int y = 0; y < 24; ++y) for (int x = 0; x < 32; ++x) nearPixel(batched.get(), reference.get(), x, y, 0);
        CHECK(pixel(batched.get(), 30, 22).b == 255);
        CHECK(pixel(batched.get(), 1, 22).g == 153);
    }

    GLOB2_TEST_CASE("physical scissors preserve fractional alpha seams", "[display][isolated]")
    {
        const char *shaders=std::getenv("GLOB2_TEST_SHADER_DIRECTORY"); REQUIRE(shaders); Video video;auto owner=makeSDLGPUDevice(nullptr,16,16,shaders);auto &device=*owner;
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

    GLOB2_TEST_CASE("explicit Vulkan trim restores live immutable source pixels", "[display][isolated]")
    {
        {
            const char *shaders = std::getenv("GLOB2_TEST_SHADER_DIRECTORY");
            if (!shaders) { MESSAGE("Vulkan shaders unavailable; trim coverage omitted"); return; }
            Video video; auto gpu = makeSDLGPUDevice(nullptr, 16, 16, shaders); auto &device = *gpu;
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
            CHECK(device.telemetry().resourceBytes <= pendingBytes); // completed chunk may already retire
            device.present();
            const auto deadline = SDL_GetTicks() + 5000;
            while (device.completion(submission) == RenderCompletion::Pending && SDL_GetTicks() < deadline) SDL_Delay(1);
            REQUIRE(device.completion(submission) == RenderCompletion::Complete);
            device.trimRetiredResources();
            // The first trim may observe an already signalled fence and evict
            // this source. Measure retirement from its initially resident bytes,
            // rather than requiring a second eviction after faster submission.
            CHECK(device.telemetry().resourceBytes + 64 * 64 * 4 <= pendingBytes);
            REQUIRE(source); // restoration source remains logically owned
            REQUIRE(draw(2)); device.present();
            CaptureRequest request; request.identity = 91; request.frameIdentity = 2;
            request.width = request.height = 16;
            Surface result(device.captureSynchronously(request), SDL_DestroySurface); REQUIRE(result);
            for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
                Uint8 r,g,b,a; REQUIRE(SDL_ReadSurfacePixel(result.get(),x,y,&r,&g,&b,&a));
                CHECK(r==31); CHECK(g==117); CHECK(b==203); CHECK(a==255);
            }
        }
    }

    GLOB2_TEST_CASE("Vulkan trims completed retired targets while capture leases remain", "[display][isolated]")
    {
        const char *shaders = std::getenv("GLOB2_TEST_SHADER_DIRECTORY");
        if (!shaders) { MESSAGE("Vulkan shaders unavailable; retirement coverage omitted"); return; }
        Video video; auto gpu = makeSDLGPUDevice(nullptr, 16, 16, shaders); auto &device = *gpu;
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

    GLOB2_TEST_CASE("Vulkan primitives preserve software pixels and capture identity", "[display][isolated]")
    {
        const char *shaders = std::getenv("GLOB2_TEST_SHADER_DIRECTORY");
        if (!shaders) { MESSAGE("Set GLOB2_TEST_SHADER_DIRECTORY to generated SPIR-V assets; Vulkan coverage omitted"); return; }
        Video video;
        auto reference = surface(64, 48), source = surface(2, 2); REQUIRE(reference); REQUIRE(source);
        REQUIRE(SDL_WriteSurfacePixel(source.get(), 0, 0, 255, 0, 0, 255));
        REQUIRE(SDL_WriteSurfacePixel(source.get(), 1, 0, 0, 255, 0, 255));
        REQUIRE(SDL_WriteSurfacePixel(source.get(), 0, 1, 0, 0, 255, 255));
        REQUIRE(SDL_WriteSurfacePixel(source.get(), 1, 1, 255, 255, 0, 128));
        RenderResources resources;
        const auto texture = resources.create(source.get(), 1);
        RenderFrame frame; frame.identity = 17; frame.logicalWidth = 64; frame.logicalHeight = 48;
        frame.packets.push_back(fill({0, 0, 64, 48}, {20, 40, 60, 255}));
        frame.packets.push_back(fill({2, 2, 15, 12}, {200, 20, 80, 128}));
        frame.packets.push_back(blit(resources.snapshot(texture), {20, 0, 16, 16}));
        auto translucent = blit(resources.snapshot(texture), {40, 0, 16, 16}); translucent.alpha = 128;
        frame.packets.push_back(translucent);
        auto clipped = fill({0, 0, 20, 20}, {0, 255, 255, 255});
        clipped.transform = {2, 2, 22, SDL_Rect{0, 0, 64, 48}}; clipped.clip = SDL_Rect{2, 2, 4, 4};
        frame.packets.push_back(clipped);
        DrawPacket triangle; triangle.kind = DrawPacket::Kind::Triangles;
        triangle.vertices = {{{30, 20}, {1, 0, 0, 1}, {0, 0}}, {{48, 20}, {1, 0, 0, 1}, {1, 0}}, {{30, 38}, {1, 0, 0, 1}, {0, 1}}};
        frame.vertexCount = triangle.vertices.size(); frame.packets.push_back(triangle);
        auto texturedTriangle = triangle; texturedTriangle.texture = resources.snapshot(texture);
        texturedTriangle.vertices = {{{42, 30}, {1, 1, 1, 1}, {0, 0}}, {{58, 30}, {1, 1, 1, 1}, {1, 0}}, {{42, 46}, {1, 1, 1, 1}, {0, 1}}};
        frame.vertexCount += texturedTriangle.vertices.size(); frame.packets.push_back(texturedTriangle);
        DrawPacket screenTriangle; screenTriangle.kind = DrawPacket::Kind::ScreenTriangles;
        screenTriangle.transform = {2, 10, 10, {}};
        screenTriangle.vertices = {{{18, 34}, {0, 1, 0, 1}, {}}, {{26, 34}, {0, 1, 0, 1}, {}}, {{18, 42}, {0, 1, 0, 1}, {}}};
        frame.vertexCount += screenTriangle.vertices.size(); frame.packets.push_back(screenTriangle);
        auto software = makeSoftwareRenderDevice(reference.get());
        auto gpu = makeSDLGPUDevice(nullptr, 64, 48, shaders);
        CHECK(gpu->capabilities().asynchronousCapture);
        CHECK(gpu->capabilities().offscreenTargets);
        REQUIRE(software->submit(frame));
        const auto submitted = gpu->submit(frame); REQUIRE(submitted);
        CaptureRequest request; request.identity = 91; request.frameIdentity = 17; request.recordingGeneration = 3;
        request.timestamp = 123456; request.context = "chapter at submission";
        REQUIRE(gpu->capture(request));
        auto capture = waitCapture(*gpu); REQUIRE(capture.pixels); CHECK_FALSE(capture.cancelled);
        CHECK(capture.request.timestamp == 123456); CHECK(capture.request.context == request.context);
        CHECK(capture.request.frameIdentity == 17); CHECK(capture.request.recordingGeneration == 3);
        CHECK(capture.request.width == 64); CHECK(capture.request.height == 48);
        CHECK(gpu->completion(submitted) == RenderCompletion::Complete);
        for (int y = 0; y < 48; ++y) for (int x = 0; x < 64; ++x) {
            // SDL software and hardware differ on triangle edge ownership.
            // Cover rectangle interiors/exteriors and triangle interior separately.
            if (x >= 29 && x <= 49 && y >= 19 && y <= 39) continue;
            if (x >= 41 && x <= 59 && y >= 29 && y <= 47) continue;
            if (x >= 17 && x <= 27 && y >= 33 && y <= 43) continue;
            nearPixel(capture.pixels.get(), reference.get(), x, y);
        }
        nearPixel(capture.pixels.get(), reference.get(), 32, 24);
        nearPixel(capture.pixels.get(), reference.get(), 40, 22);
        nearPixel(capture.pixels.get(), reference.get(), 46, 36);
        for (const auto point : {SDL_Point{45, 32}, SDL_Point{53, 32}, SDL_Point{45, 40}, SDL_Point{20, 36}})
            nearPixel(capture.pixels.get(), reference.get(), point.x, point.y);
    }
    GLOB2_TEST_CASE("Vulkan discarded segments restore last presented frame", "[display][isolated]")
    {
        const char *shaders=std::getenv("GLOB2_TEST_SHADER_DIRECTORY"); if(!shaders)return;
        Video video; auto gpu=makeSDLGPUDevice(nullptr,16,8,shaders);
        RenderFrame frame; frame.identity=1; frame.logicalWidth=16; frame.logicalHeight=8;
        frame.packets={fill({0,0,16,8},{0,255,0,255})}; REQUIRE(gpu->submit(frame)); gpu->present();
        frame.identity=2; frame.packets={fill({0,0,16,8},{255,0,0,255})}; REQUIRE(gpu->submit(frame));
        frame.packets={fill({0,0,8,8},{0,0,255,255})}; REQUIRE(gpu->submit(frame)); gpu->discardFrame();
        CaptureRequest request; request.identity=1;
        Surface restored(gpu->captureSynchronously(request),SDL_DestroySurface); REQUIRE(restored);
        CHECK(pixel(restored.get(),2,2).g==255); CHECK(pixel(restored.get(),12,2).g==255);
        frame.identity=3; frame.packets={fill({0,0,16,8},{255,0,0,255})}; REQUIRE(gpu->submit(frame)); gpu->present();
        Surface next(gpu->captureSynchronously(request),SDL_DestroySurface); REQUIRE(next); CHECK(pixel(next.get(),2,2).r==255);
        REQUIRE(gpu->capture(request)); auto capture=waitCapture(*gpu); REQUIRE(capture.pixels);
        CHECK(gpu->telemetry().cpuSubmitNanoseconds>0); CHECK(gpu->telemetry().cpuPresentNanoseconds>0);
        CHECK(gpu->telemetry().cpuCaptureNanoseconds>0); CHECK(gpu->telemetry().captureCompletions==1);
        CHECK(gpu->telemetry().captureLatencyNanoseconds>0);
    }
    GLOB2_TEST_CASE("Vulkan retains revisions offscreen targets and bounded readbacks", "[display][isolated]")
    {
        const char *shaders = std::getenv("GLOB2_TEST_SHADER_DIRECTORY");
        if (!shaders) { MESSAGE("Vulkan shader assets unavailable; coverage omitted"); return; }
        Video video;
        auto source = surface(4, 4); REQUIRE(source);
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, SDL_MapSurfaceRGBA(source.get(), 255, 0, 0, 255)));
        RenderResources resources;
        const auto handle = resources.create(source.get(), 1); auto old = resources.snapshot(handle);
        auto gpu = makeSDLGPUDevice(nullptr, 16, 8, shaders);
        RenderFrame first; first.identity = 1; first.logicalWidth = 16; first.logicalHeight = 8;
        first.packets = {fill({0, 0, 16, 8}, {0, 0, 0, 255}), blit(old, {0, 0, 4, 4})};
        const auto oldSubmission = gpu->submit(first); REQUIRE(oldSubmission);
        REQUIRE(SDL_FillSurfaceRect(source.get(), nullptr, SDL_MapSurfaceRGBA(source.get(), 0, 255, 0, 255)));
        REQUIRE(resources.update(handle, source.get(), 2)); auto newer = resources.snapshot(handle);
        RenderFrame second; second.identity = 2; second.logicalWidth = 16; second.logicalHeight = 8;
        second.packets = {blit(newer, {4, 0, 4, 4})};
        REQUIRE(gpu->submit(second)); REQUIRE(resources.erase(handle));
        CaptureRequest request; request.identity = 1; request.frameIdentity = 2;
        REQUIRE(gpu->capture(request)); ++request.identity; REQUIRE(gpu->capture(request));
        ++request.identity; REQUIRE(gpu->capture(request)); ++request.identity; CHECK_FALSE(gpu->capture(request));
        auto capture = waitCapture(*gpu); REQUIRE(capture.pixels);
        CHECK(pixel(capture.pixels.get(), 1, 1).r == 255); CHECK(pixel(capture.pixels.get(), 5, 1).g == 255);
        CHECK(gpu->telemetry().captureDrops == 1);
        // Explicit screenshots are independent of the full recording queue.
        Surface screenshot(gpu->captureSynchronously(request), SDL_DestroySurface);
        REQUIRE(screenshot); CHECK(pixel(screenshot.get(), 1, 1).r == 255);
        gpu->cancelCaptures();
        for (int i = 0; i < 2; ++i) { auto cancelled = waitCapture(*gpu); CHECK(cancelled.cancelled); CHECK_FALSE(cancelled.pixels); }
        // Render a new GPU image into a logical texture, then sample its GPU contents.
        TextureDescriptor targetDescriptor; targetDescriptor.alpha = TextureDescriptor::Alpha::Premultiplied;
        const auto targetHandle = resources.create(source.get(), 3, targetDescriptor); auto target = resources.snapshot(targetHandle);
        RenderFrame offscreen; offscreen.identity = 3; offscreen.logicalWidth = 16; offscreen.logicalHeight = 8;
        auto painted = fill({0, 0, 16, 8}, {0, 0, 255, 255}); painted.target = target;
        offscreen.packets = {painted, blit(target, {8, 0, 4, 4})}; REQUIRE(gpu->submit(offscreen));
        request.identity = 10; request.frameIdentity = 3; REQUIRE(gpu->capture(request));
        auto offscreenCapture = waitCapture(*gpu); REQUIRE(offscreenCapture.pixels);
        CHECK(pixel(offscreenCapture.pixels.get(), 9, 1).b == 255);
        CHECK(pixel(source.get(), 1, 1).g == 255); // CPU/simulation source was never mutated.
        auto premultPixels = surface(1,1); REQUIRE(premultPixels);
        REQUIRE(SDL_WriteSurfacePixel(premultPixels.get(),0,0,128,0,0,128));
        TextureDescriptor premultDescriptor; premultDescriptor.alpha=TextureDescriptor::Alpha::Premultiplied;
        auto premultTexture=resources.snapshot(resources.create(premultPixels.get(),1,premultDescriptor));
        RenderFrame premultFrame; premultFrame.identity=30; premultFrame.logicalWidth=16; premultFrame.logicalHeight=8;
        premultFrame.packets={fill({0,0,16,8},{0,0,0,255}),blit(premultTexture,{0,0,16,8})}; premultFrame.packets.back().linear=true;
        REQUIRE(gpu->submit(premultFrame)); CaptureRequest premultCapture; premultCapture.identity=30;
        Surface premultResult(gpu->captureSynchronously(premultCapture),SDL_DestroySurface); REQUIRE(premultResult); CHECK(pixel(premultResult.get(),3,3).r==128);

        auto transparent = surface(4, 4); REQUIRE(transparent);
        REQUIRE(SDL_FillSurfaceRect(transparent.get(), nullptr, 0));
        const auto translucentTarget = resources.snapshot(resources.create(transparent.get(), 4, targetDescriptor));
        RenderFrame alphaTarget; alphaTarget.identity = 4; alphaTarget.logicalWidth = 16; alphaTarget.logicalHeight = 8;
        auto partial = fill({0, 0, 16, 8}, {0, 255, 0, 128}); partial.target = translucentTarget;
        auto sampled = blit(translucentTarget, {12, 0, 4, 4}); sampled.alpha = 128;
        alphaTarget.packets = {partial, sampled}; REQUIRE(gpu->submit(alphaTarget));
        request.identity = 11; request.frameIdentity = 4; REQUIRE(gpu->capture(request));
        auto alphaCapture = waitCapture(*gpu); REQUIRE(alphaCapture.pixels);
        CHECK(std::abs(int(pixel(alphaCapture.pixels.get(), 13, 1).g) - 64) <= 1);
        // Rejected self-sampling leaves previously rendered target allocations usable.
        RenderFrame invalid; invalid.identity = 5; invalid.logicalWidth = 16; invalid.logicalHeight = 8;
        auto self = blit(target, {0, 0, 4, 4}); self.target = target; invalid.packets = {self};
        CHECK_THROWS_AS(gpu->submit(invalid), std::invalid_argument);
        RenderFrame restored; restored.identity = 6; restored.logicalWidth = 16; restored.logicalHeight = 8;
        restored.packets = {blit(target, {8, 4, 4, 4})}; REQUIRE(gpu->submit(restored));
        request.identity = 12; request.frameIdentity = 6;
        Surface restoredCapture(gpu->captureSynchronously(request), SDL_DestroySurface); REQUIRE(restoredCapture);
        CHECK(pixel(restoredCapture.get(), 9, 5).b == 255);
        gpu->reset(); CHECK(gpu->completion(oldSubmission) == RenderCompletion::Cancelled);
        CHECK(resources.snapshot(targetHandle));
        CHECK(sdlGPUUnavailableReason(shaders,"vulkan").empty());
        CHECK_FALSE(sdlGPUUnavailableReason("missing-native-shader-fixture","vulkan").empty());
        for(const char *driver:{"metal","direct3d12"})if(!sdlGPUUnavailableReason(shaders,driver).empty())
            CHECK_THROWS_AS(makeSDLGPUDevice(nullptr,16,8,shaders,driver),std::runtime_error);
        CHECK_THROWS_AS(makeSDLGPUDevice(nullptr, 16, 8, shaders, "unsupported"), std::invalid_argument);
    }
    GLOB2_TEST_CASE("Vulkan linear skin sampling clamps atlas frames and premultiplies", "[display][isolated]")
    {
        const char *shaders = std::getenv("GLOB2_TEST_SHADER_DIRECTORY");
        if (!shaders) { MESSAGE("Vulkan shader assets unavailable; coverage omitted"); return; }
        Video video;
        auto source = surface(4, 1), reference = surface(32, 16); REQUIRE(source); REQUIRE(reference);
        REQUIRE(SDL_WriteSurfacePixel(source.get(), 0, 0, 0, 255, 0, 255));
        REQUIRE(SDL_WriteSurfacePixel(source.get(), 1, 0, 255, 0, 0, 128));
        REQUIRE(SDL_WriteSurfacePixel(source.get(), 2, 0, 0, 0, 255, 0));
        REQUIRE(SDL_WriteSurfacePixel(source.get(), 3, 0, 0, 255, 0, 255));
        RenderResources resources; const auto handle = resources.create(source.get(), 1);
        RenderFrame frame; frame.identity = 1; frame.logicalWidth = 32; frame.logicalHeight = 16;
        auto sprite = blit(resources.snapshot(handle), {0, 0, 32, 16});
        sprite.source = {1, 0, 2, 1}; sprite.linear = true; sprite.alpha = 128;
        frame.packets = {fill({0, 0, 32, 16}, {40, 40, 40, 255}), sprite};
        auto gpu = makeSDLGPUDevice(nullptr, 32, 16, shaders);
        auto software = makeSoftwareRenderDevice(reference.get());
        REQUIRE(gpu->submit(frame)); REQUIRE(software->submit(frame));
        CaptureRequest request; request.identity = 1; request.frameIdentity = 1; REQUIRE(gpu->capture(request));
        auto capture = waitCapture(*gpu); REQUIRE(capture.pixels);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 32; ++x)
            nearPixel(capture.pixels.get(), reference.get(), x, y, 3);
    }

    GLOB2_TEST_CASE("Vulkan window presentation resize and repeated reset retain resources", "[display][isolated]")
    {
        const char *shaders = std::getenv("GLOB2_TEST_SHADER_DIRECTORY");
        if (!shaders) { MESSAGE("Vulkan shader assets unavailable; coverage omitted"); return; }
        Video video;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Renderer Vulkan test", 32, 24, 0), SDL_DestroyWindow);
        REQUIRE(window);
        auto gpu = makeSDLGPUDevice(window.get(), 32, 24, shaders);
        for (int iteration = 0; iteration < 8; ++iteration) {
            const int w = iteration % 2 ? 48 : 32, h = iteration % 2 ? 32 : 24;
            REQUIRE(SDL_SetWindowSize(window.get(), w, h));
            for (int i = 0; i < 5; ++i) { SDL_PumpEvents(); SDL_Delay(5); }
            RenderFrame frame; frame.identity = iteration + 1; frame.logicalWidth = w; frame.logicalHeight = h;
            frame.packets = {fill({0, 0, float(w), float(h)}, {10, 20, 200, 255})};
            REQUIRE(gpu->submit(frame)); gpu->present();
            CaptureRequest request; request.identity = iteration + 1; request.frameIdentity = frame.identity;
            Surface capture(gpu->captureSynchronously(request), SDL_DestroySurface); REQUIRE(capture);
            int outputWidth, outputHeight; gpu->outputSize(outputWidth, outputHeight);
            CHECK(capture->w == outputWidth); CHECK(capture->h == outputHeight);
            CHECK(pixel(capture.get(), capture->w - 1, capture->h - 1).b == 200);
            gpu->reset();
        }
    }

    GLOB2_TEST_CASE("Vulkan world geometry preserves depth repeat clouds particles and cached lifetimes", "[display][isolated]")
    {
        const char *shaders = std::getenv("GLOB2_TEST_SHADER_DIRECTORY");
        if (!shaders) { MESSAGE("Set GLOB2_TEST_SHADER_DIRECTORY; native torus coverage omitted"); return; }
        Video video; auto gpu = makeSDLGPUDevice(nullptr, 64, 48, shaders);
        REQUIRE(gpu->capabilities().mesh3D); REQUIRE(gpu->capabilities().torus);
        RenderResources resources;
        auto source = surface(2, 1); REQUIRE(source);
        REQUIRE(SDL_WriteSurfacePixel(source.get(), 0, 0, 255, 0, 0, 255));
        REQUIRE(SDL_WriteSurfacePixel(source.get(), 1, 0, 0, 0, 255, 255));
        TextureDescriptor descriptor; descriptor.width = 2; descriptor.height = 1;
        descriptor.addressU = TextureDescriptor::Address::Repeat;
        auto texture = resources.create(source.get(), 1, descriptor);
        const std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
        auto makeMesh = [&](float z, float u) {
            std::array<WorldVertex, 4> vertices{};
            const float positions[4][2]{{-1,-1},{1,-1},{1,1},{-1,1}};
            for (int i = 0; i < 4; ++i) {
                auto &v = vertices[i]; v.position[0] = positions[i][0]; v.position[1] = positions[i][1];
                v.position[2] = z; v.position[3] = 1; v.color[0] = v.color[1] = v.color[2] = 1;
                v.uv[0] = u; v.uv[1] = .5f; v.normal[2] = 1;
            }
            auto handle = resources.createGeometry(GeometryLayout::World48, std::as_bytes(std::span(vertices)), indices, 1);
            auto snapshot = resources.snapshot(handle); REQUIRE(resources.erase(handle)); return snapshot;
        };
        DrawPacket front; front.kind = DrawPacket::Kind::MeshTriangles; front.material = DrawPacket::Material::WorldLit;
        front.texture = resources.snapshot(texture); front.geometry = makeMesh(.25f, 1.25f);
        front.indexed = true; front.count = 6; front.depth = DrawPacket::Depth::Less; front.depthWrite = true;
        front.blend = DrawPacket::Blend::Opaque; front.cull = DrawPacket::Cull::Back;
        front.world.projection = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        auto behind = front; behind.geometry = makeMesh(.75f, 1.75f);
        DrawPacket clear; clear.kind = DrawPacket::Kind::Clear; clear.clearColor = clear.clearDepth = true;
        RenderFrame frame; frame.logicalWidth = 64; frame.logicalHeight = 48; frame.identity = 70;
        frame.packets = {clear, front, behind}; REQUIRE(gpu->submit(frame));
        CaptureRequest request; request.identity = 70;
        Surface pixels(gpu->captureSynchronously(request), SDL_DestroySurface); REQUIRE(pixels);
        CHECK(pixel(pixels.get(), 30, 20).r == 255); CHECK(pixel(pixels.get(), 30, 20).b == 0);
        const auto uploaded = gpu->telemetry().uploadBytes;
        REQUIRE(gpu->submit(frame)); CHECK(gpu->telemetry().uploadBytes == uploaded);
        // The nearer cloud reads a new immutable revision; old world packets retain red/blue.
        REQUIRE(SDL_WriteSurfacePixel(source.get(), 0, 0, 0, 255, 0, 128));
        REQUIRE(resources.update(texture, source.get(), 2));
        auto cloud = front; cloud.material = DrawPacket::Material::Cloud; cloud.texture = resources.snapshot(texture);
        cloud.geometry = makeMesh(.1f, .25f); cloud.depthWrite = false; cloud.blend = DrawPacket::Blend::SourceOver;
        frame.identity = 71; frame.packets.push_back(cloud); REQUIRE(gpu->submit(frame));
        pixels.reset(gpu->captureSynchronously(request)); REQUIRE(pixels);
        CHECK(std::abs(int(pixel(pixels.get(), 30, 20).r) - 127) <= 1);
        CHECK(std::abs(int(pixel(pixels.get(), 30, 20).g) - 128) <= 1);
        DrawPacket particle; particle.kind = DrawPacket::Kind::Triangles; particle.material = DrawPacket::Material::SmoothPoint;
        particle.blend = DrawPacket::Blend::Additive;
        const SDL_FColor tint{0,0,1,1};
        particle.vertices = {{{16,12},tint,{0,0}},{{48,12},tint,{1,0}},{{48,36},tint,{1,1}},
                             {{16,12},tint,{0,0}},{{48,36},tint,{1,1}},{{16,36},tint,{0,1}}};
        frame.identity = 72; frame.packets = {clear, particle}; REQUIRE(gpu->submit(frame));
        pixels.reset(gpu->captureSynchronously(request)); REQUIRE(pixels);
        CHECK(pixel(pixels.get(), 32, 24).b == 255); CHECK(pixel(pixels.get(), 17, 13).b == 0);
        frame.identity = 73; front.viewport = SDL_Rect{16,12,32,24}; frame.packets = {clear, front};
        REQUIRE(gpu->submit(frame)); pixels.reset(gpu->captureSynchronously(request)); REQUIRE(pixels);
        CHECK(pixel(pixels.get(), 30, 20).r == 255); CHECK(pixel(pixels.get(), 5, 5).r == 0);
        auto clipped = clear; clipped.clearDepth = false; clipped.clip = SDL_Rect{0,16,64,32}; clipped.clearValue = {0,1,0,1};
        frame.identity = 74; frame.packets = {clear, front, clipped}; REQUIRE(gpu->submit(frame));
        pixels.reset(gpu->captureSynchronously(request)); REQUIRE(pixels);
        CHECK(pixel(pixels.get(), 30, 13).r == 255); CHECK(pixel(pixels.get(), 30, 20).g == 255);
        frame.packets = {clear, front};
        auto invalid = front; invalid.world.projection[0] = std::numeric_limits<float>::quiet_NaN();
        auto invalidFrame = frame; invalidFrame.packets = {invalid}; CHECK_THROWS_AS(gpu->submit(invalidFrame), std::invalid_argument);
        auto partialDepth = clear; partialDepth.clip = SDL_Rect{0,0,32,24}; invalidFrame.packets = {partialDepth};
        CHECK_THROWS_AS(gpu->submit(invalidFrame), std::invalid_argument);
        pixels.reset(gpu->captureSynchronously(request)); REQUIRE(pixels); CHECK(pixel(pixels.get(), 30, 13).r == 255);
        REQUIRE(resources.erase(texture)); gpu->reset(); REQUIRE(gpu->submit(frame));
        pixels.reset(gpu->captureSynchronously(request)); REQUIRE(pixels); CHECK(pixel(pixels.get(), 30, 20).r == 255);
    }

    GLOB2_TEST_CASE("Vulkan authoritative skin catalogue rig fur depth and atlas capture", "[display][isolated]")
    {
        const char *shaders = std::getenv("GLOB2_TEST_SHADER_DIRECTORY");
        if (!shaders) { MESSAGE("Set GLOB2_TEST_SHADER_DIRECTORY; native skin coverage omitted"); return; }
        Video video;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> comparisonWindow(nullptr,SDL_DestroyWindow);
        std::unique_ptr<RenderDevice> gpu;
#ifdef GLOB2_TEST_COMPARE_GL
        REQUIRE(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3)); REQUIRE(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3));
        REQUIRE(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE));
        comparisonWindow.reset(SDL_CreateWindow("Same hardware skin proof",128,128,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN)); REQUIRE(comparisonWindow);
        gpu=makeProgrammableGLDevice(comparisonWindow.get(),shaders);
#else
        gpu=makeSDLGPUDevice(nullptr,128,128,shaders);
#endif
        REQUIRE(gpu->capabilities().skinMaterials);
        RenderResources resources; auto paint = surface(1,1), material = surface(1,1), atlas = surface(256,128);
        REQUIRE(paint); REQUIRE(material); REQUIRE(atlas);
        SDL_FillSurfaceRect(atlas.get(), nullptr, 0);
        REQUIRE(SDL_WriteSurfacePixel(paint.get(),0,0,237,146,82,255));
        REQUIRE(SDL_WriteSurfacePixel(material.get(),0,0,0,0,0,255));
        auto paintHandle = resources.create(paint.get(),1), materialHandle = resources.create(material.get(),1);
        TextureDescriptor desc; desc.budgetClass = TextureDescriptor::BudgetClass::SkinAtlas; desc.alpha = TextureDescriptor::Alpha::Premultiplied; desc.depthAttachment = true;
        auto atlasHandle = resources.create(atlas.get(),1,desc); auto target = resources.snapshot(atlasHandle);
        std::vector<float> baked, rigged;
        const auto vertex = [](double radius, double angle) {
            const double x = radius * std::cos(angle), y = radius * std::sin(angle), z = std::sqrt(std::max(0.,1-radius*radius));
            return std::array<float,18>{float(x*.8),float(y*.8),float(-z*.25),float(x),float(y),float(z),float(x*.5+.5),float(y*.5+.5),float(x*.5+.5),float(y*.5+.5),0,0,0,0,1,0,0,0};
        };
        for (int r=0;r<8;++r) for (int a=0;a<32;++a) {
            const double angle = a * 3.14159265358979323846 / 16, nextAngle = (a+1) * 3.14159265358979323846 / 16;
            const std::array points{vertex(double(r)/8,angle),vertex(double(r+1)/8,angle),vertex(double(r+1)/8,nextAngle),vertex(double(r)/8,nextAngle)};
            for (int i : {0,1,2,0,2,3}) { baked.insert(baked.end(),points[i].begin(),points[i].begin()+10); rigged.insert(rigged.end(),points[i].begin(),points[i].end()); }
        }
        // Use actual WebGL upload bytes when the cross-dialect proof supplied them.
        const auto readFixture = [&](const char *name, std::vector<float> &vertices) {
            std::ifstream input(std::string(shaders)+"/"+name,std::ios::binary|std::ios::ate);
            if (!input) return;
            const auto bytes=input.tellg(); REQUIRE(bytes==std::streamoff(vertices.size()*sizeof(float)));
            input.seekg(0); input.read(reinterpret_cast<char*>(vertices.data()),bytes); REQUIRE(input.good());
        };
        readFixture("webgl-skin-vertices.f32",baked); readFixture("webgl-rig-vertices.f32",rigged);
        auto bodyGeometry = resources.snapshot(resources.createGeometry(GeometryLayout::Skin40,std::as_bytes(std::span(baked)),{},1));
        auto rigGeometry = resources.snapshot(resources.createGeometry(GeometryLayout::SkinRig72,std::as_bytes(std::span(rigged)),{},1));
        const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        auto pose = std::make_shared<SkinPose>(); pose->view = identity; pose->normalView = {1,0,0,0,1,0,0,0,1};
        for (int i=0;i<32;++i) std::copy(identity.begin(),identity.end(),pose->bones.begin()+i*16);
        std::uint64_t frameIdentity = 100;
        auto render = [&](bool rig, unsigned id, int shells=0, float shift=0, float depthShift=0, bool preserve=false, int tile=0) {
            REQUIRE(SDL_WriteSurfacePixel(material.get(),0,0,Uint8(id),0,0,255)); REQUIRE(resources.update(materialHandle,material.get(),++frameIdentity));
            RenderFrame frame; frame.identity = frameIdentity; frame.logicalWidth = frame.logicalHeight = 128;
            DrawPacket depth; depth.kind=DrawPacket::Kind::Clear; depth.target=target; depth.clearDepth=true;
            DrawPacket color=depth; color.clearDepth=false; color.clearColor=true; color.clearValue={0,0,0,0}; color.clip=SDL_Rect{tile*128,0,128,128}; color.targetLogicalWidth=256; color.targetLogicalHeight=128;
            if (!preserve) frame.packets={depth,color};
            auto deformed = std::make_shared<SkinPose>(*pose); deformed->bones[12]=shift; deformed->view[14]=depthShift;
            for (int k=0;k<=shells;++k) {
                DrawPacket packet; packet.kind=DrawPacket::Kind::MeshTriangles; packet.target=target;
                packet.material=rig?DrawPacket::Material::SkinRig:DrawPacket::Material::Skin;
                packet.geometry=rig?rigGeometry:bodyGeometry; packet.count=Uint32(baked.size()/10);
                packet.texture=resources.snapshot(paintHandle); packet.secondTexture=resources.snapshot(materialHandle);
                packet.depth=DrawPacket::Depth::LessEqual; packet.depthWrite=true; packet.blend=DrawPacket::Blend::Opaque;
                packet.viewport=SDL_Rect{tile*128,0,128,128}; packet.targetLogicalWidth=256; packet.targetLogicalHeight=128;
                auto uniforms=std::make_shared<SkinMaterial>(); uniforms->shell=float(k)/8; uniforms->furLength=.05f; uniforms->shellDepth=.002f; uniforms->region={0,0}; uniforms->pose=deformed; packet.skin=uniforms;
                frame.packets.push_back(std::move(packet));
            }
            REQUIRE(gpu->submit(frame)); CaptureRequest request; request.identity=frame.identity; request.target=target; request.pixelRegion=SDL_Rect{tile*128,0,128,128};
            return Surface(gpu->captureSynchronously(request),SDL_DestroySurface);
        };
        const auto difference = [](SDL_Surface *a, SDL_Surface *b) { std::size_t count=0; for(int y=0;y<a->h;++y)for(int x=0;x<a->w*4;++x) count+=static_cast<Uint8*>(a->pixels)[y*a->pitch+x]!=static_cast<Uint8*>(b->pixels)[y*b->pitch+x]; return count; };
        for (unsigned id=0;id<22;++id) {
            INFO("material id " << id);
            auto body=render(false,id), rest=render(true,id), moved=render(true,id,0,.15f);
            REQUIRE(body); REQUIRE(rest); REQUIRE(moved);
            std::size_t covered=0; for(int y=0;y<128;++y)for(int x=0;x<128;++x) covered+=static_cast<Uint8*>(body->pixels)[y*body->pitch+x*4+3]!=0;
            CHECK(covered>1000); CHECK(difference(body.get(),rest.get())<=100); CHECK(difference(rest.get(),moved.get())>1000);
            MESSAGE("Vulkan material " << id << " coverage=" << covered << " restDifference=" << difference(body.get(),rest.get()) << " deformationDifference=" << difference(rest.get(),moved.get()));
            if (const char *directory = std::getenv("GLOB2_TEST_CAPTURE_DIRECTORY")) {
                const std::string prefix = std::string(directory) + "/material-" + std::to_string(id);
                REQUIRE(SDL_SaveBMP(body.get(),(prefix+"-body.bmp").c_str()));
                REQUIRE(SDL_SaveBMP(moved.get(),(prefix+"-rig.bmp").c_str()));
            }
            if (id==3 || id==18) { auto fur=render(false,id,8); CHECK(difference(body.get(),fur.get())>100); }
        }
        auto near=render(false,0); auto occluded=render(true,1,0,0,.8f,true); CHECK(difference(near.get(),occluded.get())==0);
        auto right=render(false,5,0,0,0,false,1);
        CaptureRequest leftRequest; leftRequest.identity=999; leftRequest.target=target; leftRequest.pixelRegion=SDL_Rect{0,0,128,128};
        Surface preserved(gpu->captureSynchronously(leftRequest),SDL_DestroySurface); REQUIRE(preserved); CHECK(difference(near.get(),preserved.get())==0);
        REQUIRE(gpu->capture(leftRequest)); auto async=waitCapture(*gpu); CHECK(async.request.width==128); CHECK(async.request.pixelRegion->x==0); CHECK(difference(preserved.get(),async.pixels.get())==0);
        leftRequest.pixelRegion=SDL_Rect{250,0,128,128}; CHECK_FALSE(gpu->capture(leftRequest)); CHECK_THROWS_AS(gpu->captureSynchronously(leftRequest),std::invalid_argument);
    }

    GLOB2_TEST_CASE("Vulkan completed immutable uploads can be evicted and restored", "[display][isolated]")
    {
        const char *shaders=std::getenv("GLOB2_TEST_SHADER_DIRECTORY"); if(!shaders)return;
        Video video; auto gpu=makeSDLGPUDevice(nullptr,16,8,shaders); RenderResources resources;
        auto large=surface(4096,4096),small=surface(2048,2048); REQUIRE(large); REQUIRE(small);
        REQUIRE(SDL_FillSurfaceRect(large.get(),nullptr,SDL_MapSurfaceRGBA(large.get(),255,0,0,255)));
        REQUIRE(SDL_FillSurfaceRect(small.get(),nullptr,SDL_MapSurfaceRGBA(small.get(),0,255,0,255)));
        auto red=resources.snapshot(resources.create(large.get(),1)); REQUIRE(red);
        // Exercise the device independently of the registry's separate CPU
        // class limit while preserving real immutable restoration snapshots.
        TextureDescriptor temporaryClass; temporaryClass.budgetClass=TextureDescriptor::BudgetClass::SkinAtlas;
        auto cpuGreen=resources.snapshot(resources.create(small.get(),1,temporaryClass)); REQUIRE(cpuGreen);
        auto mutableGreen=std::make_shared<TextureRevision>(*cpuGreen); mutableGreen->descriptor.budgetClass=TextureDescriptor::BudgetClass::General;
        TextureSnapshot green=mutableGreen;
        RenderFrame frame; frame.identity=1;frame.logicalWidth=16;frame.logicalHeight=8;
        CaptureRequest request;request.identity=1;
        for(const auto &texture:{red,green,red}) {
            frame.packets={blit(texture,{0,0,16,8})}; REQUIRE(gpu->submit(frame));
            Surface captured(gpu->captureSynchronously(request),SDL_DestroySurface); REQUIRE(captured);
            CHECK(pixel(captured.get(),3,3).r==(texture==red?255:0));
            CHECK(pixel(captured.get(),3,3).g==(texture==green?255:0));
            gpu->present();
        }
    }
    GLOB2_TEST_CASE("Vulkan four atlas pages charge depth to aggregate only", "[display][isolated]")
    {
        const char *shaders=std::getenv("GLOB2_TEST_SHADER_DIRECTORY"); if(!shaders)return;
        Video video; auto gpu=makeSDLGPUDevice(nullptr,16,8,shaders); RenderResources resources;
        auto pixels=surface(2048,2048); REQUIRE(pixels); REQUIRE(SDL_FillSurfaceRect(pixels.get(),nullptr,0));
        TextureDescriptor descriptor;descriptor.alpha=TextureDescriptor::Alpha::Premultiplied;
        descriptor.budgetClass=TextureDescriptor::BudgetClass::SkinAtlas;
        std::array<TextureSnapshot,4> pages;
        RenderFrame frame;frame.identity=1;frame.logicalWidth=2048;frame.logicalHeight=2048;
        for(std::size_t i=0;i<pages.size();++i) {
            pages[i]=resources.snapshot(resources.create(pixels.get(),1,descriptor));REQUIRE(pages[i]);
            DrawPacket depth;depth.kind=DrawPacket::Kind::Clear;depth.target=pages[i];depth.clearDepth=true;frame.packets.push_back(depth);
            auto paint=fill({0,0,2048,2048},{Uint8(40+i*30),0,0,255});paint.target=pages[i];frame.packets.push_back(paint);
        }
        REQUIRE(gpu->submit(frame));gpu->present();CHECK(gpu->telemetry().resourceBytes<=256*1024*1024);
        for(std::size_t i=0;i<pages.size();++i) {
            CaptureRequest request;request.identity=1;request.target=pages[i];request.pixelRegion=SDL_Rect{1000,1000,1,1};
            Surface captured(gpu->captureSynchronously(request),SDL_DestroySurface);REQUIRE(captured);
            CHECK(pixel(captured.get(),0,0).r==Uint8(40+i*30));
        }
    }
    GLOB2_TEST_CASE("Vulkan four depth atlases coexist with full4096 authored terrain mip uploads", "[display][isolated]")
    {
        const char *shaders=std::getenv("GLOB2_TEST_SHADER_DIRECTORY");if(!shaders)return;
        Video video;auto gpu=makeSDLGPUDevice(nullptr,16,16,shaders);RenderResources resources;
        auto atlas=surface(2048,2048);REQUIRE(atlas);REQUIRE(SDL_FillSurfaceRect(atlas.get(),nullptr,0));
        TextureDescriptor atlasDescriptor;atlasDescriptor.alpha=TextureDescriptor::Alpha::Premultiplied;
        atlasDescriptor.budgetClass=TextureDescriptor::BudgetClass::SkinAtlas;
        std::array<TextureSnapshot,4> pages;RenderFrame frame;frame.identity=1;frame.logicalWidth=frame.logicalHeight=16;
        for(unsigned i=0;i<pages.size();++i) {
            pages[i]=resources.snapshot(resources.create(atlas.get(),1,atlasDescriptor));
            DrawPacket clear;clear.kind=DrawPacket::Kind::Clear;clear.target=pages[i];clear.clearDepth=true;
            frame.packets.push_back(clear);auto paint=fill({0,0,2048,2048},{Uint8(40+i*30),0,0,255});paint.target=pages[i];frame.packets.push_back(paint);
        }
        const auto atlasPackets=frame.packets;
        REQUIRE(gpu->submit(frame));gpu->present();
        CaptureRequest request;request.pixelRegion=SDL_Rect{0,0,1,1};Surface captured(gpu->captureSynchronously(request),SDL_DestroySurface);REQUIRE(captured);
        auto base=surface(4096,4096);REQUIRE(base);
        std::vector<std::vector<std::byte>> pixels;std::vector<RenderMipLevel> levels;
        for(int size=4096;size;size/=2) {
            pixels.emplace_back(std::size_t(size)*size*4);
            for(std::size_t offset=0;offset<pixels.back().size();offset+=4) {
                pixels.back()[offset]=std::byte(29+(levels.size())*10);pixels.back()[offset+1]=std::byte(171);pixels.back()[offset+2]=std::byte(87);pixels.back()[offset+3]=std::byte(255);
            }
            levels.push_back({size,size,SDL_PIXELFORMAT_RGBA32,pixels.back()});
        }
        TextureDescriptor descriptor;descriptor.budgetClass=TextureDescriptor::BudgetClass::TerrainPage;
        descriptor.mips=TextureDescriptor::Mips::Generated;descriptor.sampling=TextureDescriptor::Sampling::Linear;
        auto terrain=resources.snapshot(resources.create(base.get(),1,descriptor,levels));REQUIRE(terrain);
        auto packet=blit(terrain,{0,0,16,16});packet.linear=true;frame.identity=2;frame.packets={packet};
        bool accepted=false;unsigned deferred=0;std::size_t peak=0;const auto uploadsBefore=gpu->telemetry().uploadBytes;
        for(unsigned attempt=0;attempt<1000 && !accepted;++attempt) {
            try {accepted=bool(gpu->submit(frame));} catch(const RenderAdmissionDeferred &) {++deferred;gpu->discardFrame();}
            peak=std::max(peak,gpu->telemetry().resourceBytes);CHECK(peak<=256*1024*1024);
            if(!accepted)SDL_Delay(1); // Test polling only; production upload must never wait.
        }
        INFO("Deferred upload attempts: ",deferred);REQUIRE(accepted);CHECK(deferred>0);gpu->present();
        MESSAGE("Combined atlas/mip upload deferred attempts=",deferred," peak bytes=",peak);
        CHECK(gpu->telemetry().uploadBytes-uploadsBefore>=terrain->bytes);
        CHECK(gpu->telemetry().uploadBytes-uploadsBefore<terrain->bytes+1024*1024);
        captured.reset(gpu->captureSynchronously(request));REQUIRE(captured);CHECK(pixel(captured.get(),0,0).r==109);CHECK(pixel(captured.get(),0,0).g==171);
        // Verify the last authored level and opposite base-level upload rows.
        packet.destination={0,0,1,1};frame.identity=3;frame.packets={packet};REQUIRE(gpu->submit(frame));gpu->present();
        captured.reset(gpu->captureSynchronously(request));REQUIRE(captured);CHECK(pixel(captured.get(),0,0).r==149);
        for(const SDL_Rect region: {SDL_Rect{0,0,1,1},SDL_Rect{4095,4095,1,1}}) {
            packet.linear=true;packet.source=region;frame.identity++;frame.packets={packet};REQUIRE(gpu->submit(frame));gpu->present();
            captured.reset(gpu->captureSynchronously(request));REQUIRE(captured);CHECK(pixel(captured.get(),0,0).r==29);
        }
        for(unsigned i=0;i<pages.size();++i) {
            request.target=pages[i];captured.reset(gpu->captureSynchronously(request));REQUIRE(captured);CHECK(pixel(captured.get(),0,0).r==Uint8(40+i*30));
        }
        // Cold restoration must also progress when atlas uploads and terrain
        // first appear together in one unsubmitted presentation frame.
        gpu=makeSDLGPUDevice(nullptr,16,16,shaders);frame.identity=1;frame.packets=atlasPackets;
        packet=blit(terrain,{0,0,16,16});packet.linear=true;frame.packets.push_back(packet);accepted=false;deferred=0;peak=0;
        for(unsigned attempt=0;attempt<1000 && !accepted;++attempt) {
            try {accepted=bool(gpu->submit(frame));}catch(const RenderAdmissionDeferred &){++deferred;gpu->discardFrame();}
            peak=std::max(peak,gpu->telemetry().resourceBytes);CHECK(peak<=256*1024*1024);if(!accepted)SDL_Delay(1);
        }
        INFO("Cold combined deferred attempts: ",deferred);REQUIRE(accepted);gpu->present();
        MESSAGE("Cold combined upload deferred attempts=",deferred," peak bytes=",peak);
        request.target={};captured.reset(gpu->captureSynchronously(request));REQUIRE(captured);CHECK(pixel(captured.get(),0,0).r==109);
        for(unsigned i=0;i<pages.size();++i) {
            request.target=pages[i];captured.reset(gpu->captureSynchronously(request));REQUIRE(captured);CHECK(pixel(captured.get(),0,0).r==Uint8(40+i*30));
        }
    }
    GLOB2_TEST_CASE("Vulkan atlas and general caches have separate bounded budgets", "[display][isolated]")
    {
        const char *shaders = std::getenv("GLOB2_TEST_SHADER_DIRECTORY"); if (!shaders) return;
        Video video; auto gpu=makeSDLGPUDevice(nullptr,32,24,shaders); RenderResources resources;
        auto atlas=surface(4096,4096), general=surface(2048,2048), extra=surface(1,1); REQUIRE(atlas); REQUIRE(general); REQUIRE(extra);
        SDL_FillSurfaceRect(atlas.get(),nullptr,0); SDL_FillSurfaceRect(general.get(),nullptr,SDL_MapSurfaceRGBA(general.get(),0,255,0,255));
        TextureDescriptor descriptor; descriptor.budgetClass=TextureDescriptor::BudgetClass::SkinAtlas;
        auto atlasTexture=resources.snapshot(resources.create(atlas.get(),1,descriptor));
        auto generalTexture=resources.snapshot(resources.create(general.get(),1));
        DrawPacket clear; clear.kind=DrawPacket::Kind::Clear; clear.target=atlasTexture; clear.clearColor=clear.clearDepth=true; clear.clearValue={0,0,1,1};
        RenderFrame frame; frame.identity=1; frame.logicalWidth=32; frame.logicalHeight=24; frame.packets={clear,blit(generalTexture,{0,0,32,24})};
        REQUIRE(gpu->submit(frame)); CHECK(gpu->telemetry().resourceBytes<=256*1024*1024);
        CaptureRequest request; request.identity=1; request.target=atlasTexture; request.pixelRegion=SDL_Rect{4000,4000,1,1};
        Surface captured(gpu->captureSynchronously(request),SDL_DestroySurface); REQUIRE(captured); CHECK(pixel(captured.get(),0,0).b==255);
        // Bypass the registry's same class guard to test independent device admission.
        auto overflow=std::make_shared<TextureRevision>(*resources.snapshot(resources.create(extra.get(),1)));
        overflow->descriptor.budgetClass=TextureDescriptor::BudgetClass::SkinAtlas;
        frame.packets={blit(overflow,{0,0,1,1})};
        CHECK_THROWS_AS(gpu->submit(frame),RenderAdmissionDeferred); CHECK(gpu->telemetry().resourceBytes<=256*1024*1024);
        request.target={}; request.pixelRegion={}; captured.reset(gpu->captureSynchronously(request)); REQUIRE(captured); CHECK(pixel(captured.get(),12,12).g==255);
    }

    GLOB2_TEST_CASE("Vulkan terrain composition and reduction preserve canonical CPU bytes", "[display][isolated]")
    {
        const char *shaders=std::getenv("GLOB2_TEST_SHADER_DIRECTORY");if(!shaders)return;
        Video video;auto gpu=makeSDLGPUDevice(nullptr,48,48,shaders);
        if(!gpu->capabilities().terrainComposition){MESSAGE("Terrain shader assets unavailable; composition coverage omitted");return;}
        RenderResources resources;
        constexpr int size=32;
        std::array<TextureSnapshot,4> materials;
        std::array<Surface,4> source{surface(3,3),surface(16,16),surface(32,32),surface(64,64)};
        for(unsigned slot=0;slot<4;++slot) {
            REQUIRE(source[slot]);
            for(int y=0;y<source[slot]->h;++y)for(int x=0;x<source[slot]->w;++x)
                REQUIRE(SDL_WriteSurfacePixel(source[slot].get(),x,y,Uint8((x*19+y*7+slot*71)%256),Uint8((x*13+y*29+slot*31)%256),Uint8((x*43+y*3+slot*17)%256),Uint8((x*11+y*23+slot*83)%256)));
            materials[slot]=resources.snapshot(resources.create(source[slot].get(),1));REQUIRE(materials[slot]);
        }
        auto coverage=std::make_shared<CoverageData>();coverage->identity=19;coverage->revision=1;coverage->width=coverage->height=size;coverage->samples.resize(size*size);
        for(unsigned i=0;i<coverage->samples.size();++i) {
            auto &sample=coverage->samples[i];sample.others={std::uint16_t(9000+i%400),std::uint16_t(12000+i%700),std::uint16_t(15000+i%300)};
            sample.margin=std::uint16_t(i%73);sample.slots=Uint8((i%4)|(((i/4)%4)<<2)|(((i/16)%4)<<4));
        }
        std::array<SeamConstants,4> seams;
        for(unsigned i=0;i<4;++i) {seams[i].height=i;seams[i].cast=35+i*15;seams[i].castWidth=40+i*3;seams[i].fringe=70+i*10;seams[i].fringeWidth=60+i*3;seams[i].fringeColor={Uint8(i*3),Uint8(12+i),Uint8(240-i*40)};}
        std::uint64_t frameId=0;
        for(bool uniform:{false,true})for(bool missing:{false,true})for(bool pma:{false,true})for(unsigned divisor:{1u,2u,4u,8u,16u,32u}) {
            INFO("uniform="<<uniform<<" missing="<<missing<<" pma="<<pma<<" divisor="<<divisor);
            auto data=std::make_shared<WeightedComposeData>();data->materials=materials;data->seams=seams;data->paletteCount=uniform?1:4;data->compositionSize=size;data->reductionDivisor=divisor;
            if(missing)data->materials[uniform?0:2]={};
            if(!uniform)data->coverage=coverage;
            const int output=size/divisor;data->targetRect={3,4,output,output};
            auto targetPixels=surface(output+7,output+9);REQUIRE(targetPixels);REQUIRE(SDL_FillSurfaceRect(targetPixels.get(),nullptr,SDL_MapSurfaceRGBA(targetPixels.get(),7,9,11,255)));
            TextureDescriptor descriptor;descriptor.alpha=pma?TextureDescriptor::Alpha::Premultiplied:TextureDescriptor::Alpha::Straight;descriptor.budgetClass=TextureDescriptor::BudgetClass::TerrainPage;
            auto target=resources.snapshot(resources.create(targetPixels.get(),++frameId,descriptor));REQUIRE(target);
            DrawPacket packet;packet.kind=DrawPacket::Kind::WeightedCompose;packet.target=target;packet.weightedCompose=data;
            RenderFrame frame;frame.identity=frameId;frame.logicalWidth=48;frame.logicalHeight=48;frame.packets={packet};REQUIRE(gpu->submit(frame));gpu->present();
            CaptureRequest request;request.identity=1;request.target=target;Surface actual(gpu->captureSynchronously(request),SDL_DestroySurface);REQUIRE(actual);
            const auto outside=pixel(actual.get(),0,0);CHECK(outside.r==7);CHECK(outside.g==9);CHECK(outside.b==11);CHECK(outside.a==255);
            std::array<std::array<unsigned,4>,size*size> composed{};
            for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x) {
                auto &out=composed[y*size+x];
                auto fetch=[&](unsigned slot){return data->materials[slot]?pixel(source[slot].get(),x*source[slot]->w/size,y*source[slot]->h/size):SDL_Color{0,0,0,0};};
                if(uniform) {const auto value=fetch(0);out={value.r,value.g,value.b,value.a};continue;}
                const auto &sample=coverage->samples[y*size+x];unsigned weights[4]{},rest=65536,other=0;
                for(unsigned slot=0;slot<4;++slot)if(slot!=(sample.slots&3)){weights[slot]=sample.others[other++];rest-=weights[slot];}
                weights[sample.slots&3]=rest;unsigned alpha=0;std::uint64_t rgb[3]{};
                for(unsigned slot=0;slot<4;++slot) {const auto value=fetch(slot);const unsigned a=weights[slot]*value.a;alpha+=a;rgb[0]+=std::uint64_t(value.r)*a;rgb[1]+=std::uint64_t(value.g)*a;rgb[2]+=std::uint64_t(value.b)*a;}
                unsigned dominant=(sample.slots>>2)&3,neighbor=(sample.slots>>4)&3;const auto &self=seams[dominant],&adjacent=seams[neighbor];int shade=256,tint=0;
                if(neighbor!=dominant) {if(adjacent.cast && adjacent.height>self.height && sample.margin<adjacent.castWidth)shade-=adjacent.cast*(adjacent.castWidth-sample.margin)/adjacent.castWidth;if(adjacent.fringe && sample.margin<adjacent.fringeWidth)tint=adjacent.fringe*(adjacent.fringeWidth-sample.margin)/adjacent.fringeWidth;}
                unsigned channels[3];for(unsigned k=0;k<3;++k){unsigned value=unsigned(alpha?rgb[k]/alpha:0)*unsigned(shade)>>8;channels[k]=value+(unsigned(adjacent.fringeColor[k])-value)*unsigned(tint)/256;}
                const std::uint32_t packed=(((alpha+32768)/65536)<<24)|(channels[0]<<16)|(channels[1]<<8)|channels[2];out={(packed>>16)&255,(packed>>8)&255,packed&255,packed>>24};
            }
            for(int y=0;y<output;++y)for(int x=0;x<output;++x) {
                unsigned rgb[3]{},alpha=0,count=divisor*divisor;std::array<unsigned,4> expected{};
                if(divisor==1)expected=composed[y*size+x];
                else {for(unsigned dy=0;dy<divisor;++dy)for(unsigned dx=0;dx<divisor;++dx){const auto &v=composed[(y*divisor+dy)*size+x*divisor+dx];alpha+=v[3];for(unsigned k=0;k<3;++k)rgb[k]+=v[k]*v[3];}expected[3]=(alpha+count/2)/count;for(unsigned k=0;k<3;++k)expected[k]=alpha?(rgb[k]+alpha/2)/alpha:0;}
                if(pma)for(unsigned k=0;k<3;++k)expected[k]=(expected[k]*expected[3]+127)/255;
                const auto found=pixel(actual.get(),x+3,y+4);INFO("pixel "<<x<<","<<y);CHECK(found.r==expected[0]);CHECK(found.g==expected[1]);CHECK(found.b==expected[2]);CHECK(found.a==expected[3]);
            }
            resources.erase(target->handle);
        }
        CHECK(gpu->telemetry().resourceBytes<=256*1024*1024);
    }

    GLOB2_TEST_CASE("Vulkan uploads authored mip levels and samples trilinear without generation", "[display][isolated]")
    {
        const char *shaders=std::getenv("GLOB2_TEST_SHADER_DIRECTORY");if(!shaders)return;
        Video video;auto gpu=makeSDLGPUDevice(nullptr,16,16,shaders);RenderResources resources;
        auto base=surface(8,8);REQUIRE(base);
        std::array<std::vector<std::byte>,4> pixels;
        std::array<RenderMipLevel,4> levels;
        const std::array<SDL_Color,4> colors{{{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255}}};
        for(unsigned level=0;level<4;++level) {
            const int size=8>>level;pixels[level].resize(size*size*4);
            for(int index=0;index<size*size;++index) {pixels[level][index*4]=std::byte(colors[level].r);pixels[level][index*4+1]=std::byte(colors[level].g);pixels[level][index*4+2]=std::byte(colors[level].b);pixels[level][index*4+3]=std::byte(colors[level].a);}
            levels[level]={size,size,SDL_PIXELFORMAT_RGBA32,pixels[level]};
        }
        TextureDescriptor descriptor;descriptor.mips=TextureDescriptor::Mips::Generated;descriptor.sampling=TextureDescriptor::Sampling::Linear;
        auto handle=resources.create(base.get(),1,descriptor,levels);REQUIRE(handle);auto snapshot=resources.snapshot(handle);REQUIRE(snapshot);CHECK(snapshot->mips.size()==3);CHECK(snapshot->bytes==340);
        RenderFrame frame;frame.logicalWidth=frame.logicalHeight=16;std::uint64_t identity=0;
        for(unsigned level=0;level<4;++level) {
            const int size=8>>level;auto packet=blit(snapshot,{0,0,float(size),float(size)});packet.linear=true;
            frame.identity=++identity;frame.packets={fill({0,0,16,16},{0,0,0,255}),packet};REQUIRE(gpu->submit(frame));gpu->present();
            CaptureRequest request;request.identity=1;Surface capture(gpu->captureSynchronously(request),SDL_DestroySurface);REQUIRE(capture);
            const auto actual=pixel(capture.get(),size/2,size/2);CHECK(actual.r==colors[level].r);CHECK(actual.g==colors[level].g);CHECK(actual.b==colors[level].b);
        }
        auto cropped=blit(snapshot,{0,0,1,1});cropped.source={2,2,4,4};cropped.linear=true;
        frame.identity=++identity;frame.packets={fill({0,0,16,16},{0,0,0,255}),cropped};REQUIRE(gpu->submit(frame));gpu->present();
        CaptureRequest croppedRequest;croppedRequest.identity=1;Surface croppedCapture(gpu->captureSynchronously(croppedRequest),SDL_DestroySurface);REQUIRE(croppedCapture);
        CHECK(pixel(croppedCapture.get(),0,0).r==0);CHECK(pixel(croppedCapture.get(),0,0).g==0);CHECK(pixel(croppedCapture.get(),0,0).b==255);
        auto fractional=blit(snapshot,{0,0,3,3});fractional.linear=true;frame.identity=++identity;frame.packets={fill({0,0,16,16},{0,0,0,255}),fractional};REQUIRE(gpu->submit(frame));gpu->present();
        CaptureRequest request;request.identity=1;Surface capture(gpu->captureSynchronously(request),SDL_DestroySurface);REQUIRE(capture);const auto interpolation=pixel(capture.get(),1,1);
        CHECK(interpolation.r==0);CHECK(interpolation.g>130);CHECK(interpolation.g<170);CHECK(interpolation.b>80);CHECK(interpolation.b<120);
        for(auto &levelPixels:pixels)for(std::size_t index=3;index<levelPixels.size();index+=4)levelPixels[index]=std::byte(128);
        REQUIRE(resources.update(handle,base.get(),2,descriptor,levels));auto translucent=resources.snapshot(handle);REQUIRE(translucent);
        auto half=blit(translucent,{0,0,4,4});half.linear=true;frame.identity=++identity;frame.packets={fill({0,0,16,16},{0,0,0,255}),half};REQUIRE(gpu->submit(frame));gpu->present();
        capture.reset(gpu->captureSynchronously(request));REQUIRE(capture);CHECK(pixel(capture.get(),2,2).g==128);CHECK(pixel(capture.get(),2,2).a==255);
        // The old snapshot retains its original authored chain after mutation.
        half.texture=snapshot;frame.identity=++identity;frame.packets={fill({0,0,16,16},{0,0,0,255}),half};REQUIRE(gpu->submit(frame));gpu->present();
        capture.reset(gpu->captureSynchronously(request));REQUIRE(capture);CHECK(pixel(capture.get(),2,2).g==255);
        auto malformed=std::make_shared<TextureRevision>(*snapshot);malformed->mips.pop_back();frame.packets={blit(malformed,{0,0,4,4})};CHECK_THROWS_AS(gpu->submit(frame),std::invalid_argument);
    }

}
