// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <RenderDevice.h>
#include <deque>
#include <array>
#include <cstring>

using namespace GAGCore;
namespace
{
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
Surface surface(int width = 2, int height = 1)
{
    return Surface(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
}
void paint(SDL_Surface *surface, Uint8 r, Uint8 g, Uint8 b)
{
    REQUIRE(SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGBA(surface, r, g, b, 255)));
}
SDL_Color pixel(SDL_Surface *surface, int x)
{
    Uint8 r, g, b, a;
    REQUIRE(SDL_ReadSurfacePixel(surface, x, 0, &r, &g, &b, &a));
    return {r, g, b, a};
}
// A backend test double deliberately retains work across frames. This catches
// use-after-mutation/deletion hidden by the immediate software implementation.
class DelayedDevice final : public RenderDevice
{
  public:
    struct Pending { RenderSubmission submission; RenderFrame frame; };
    std::deque<Pending> pending;
    std::uint64_t epoch = 1, issued = 0, completed = 0;
    bool failSubmission=false, offscreen=false;
    std::vector<CaptureRequest> captureRequests;
    RenderDeviceCapabilities capabilities() const override { RenderDeviceCapabilities c;c.offscreenTargets=offscreen;return c; }
    RenderSubmission submit(RenderFrame frame) override
    {
        if(failSubmission)throw std::runtime_error("Injected device loss");
        if (pending.size() == 3) return {};
        RenderSubmission submission{++issued, epoch};
        pending.push_back({submission, std::move(frame)}); return submission;
    }
    void finish() { completed = pending.front().submission.identity; pending.pop_front(); }
    RenderCompletion completion(RenderSubmission submission) override
    {
        if (!submission || submission.deviceGeneration != epoch || submission.identity > issued) return RenderCompletion::Cancelled;
        return submission.identity <= completed ? RenderCompletion::Complete : RenderCompletion::Pending;
    }
    void present() override {}
    void reset() override { pending.clear(); ++epoch; }
    std::uint64_t generation() const override { return epoch; }
    RenderTelemetry telemetry() const override { return {}; }
    bool capture(CaptureRequest) override { return false; }
    SDL_Surface *captureSynchronously(CaptureRequest request) override {captureRequests.push_back(request);return SDL_CreateSurface(2,1,SDL_PIXELFORMAT_RGBA32);}
    bool pollCapture(CaptureResult &) override { return false; }
    void cancelCaptures() override {}
    void outputSize(int &width, int &height) override { width = 2; height = 1; }
};
}
TEST_SUITE("RenderDevice")
{
    TEST_CASE("physical scissors partition fractional CPU geometry without double blending")
    {
        auto render=[](bool partition) {
            auto target=surface(16,16);auto device=makeSoftwareRenderDevice(target.get());
            RenderFrame frame;frame.identity=1;frame.logicalWidth=23;frame.logicalHeight=19;
            DrawPacket packet;packet.kind=DrawPacket::Kind::Fill;packet.destination={0,0,23,19};packet.color={200,90,40,128};
            if(partition){packet.targetScissor=SDL_Rect{0,0,8,16};frame.packets.push_back(packet);packet.targetScissor=SDL_Rect{8,0,8,16};}
            frame.packets.push_back(packet);REQUIRE(device->submit(std::move(frame)));device->present();
            return Surface(device->captureSynchronously({}),SDL_DestroySurface);
        };
        auto whole=render(false),partitioned=render(true);REQUIRE(whole);REQUIRE(partitioned);
        for(int y=0;y<16;++y)for(int x=0;x<16;++x){Uint8 a[4],b[4];REQUIRE(SDL_ReadSurfacePixel(whole.get(),x,y,&a[0],&a[1],&a[2],&a[3]));REQUIRE(SDL_ReadSurfacePixel(partitioned.get(),x,y,&b[0],&b[1],&b[2],&b[3]));CHECK(std::memcmp(a,b,4)==0);}
    }
    TEST_CASE("physical scissors follow target scopes and exclude foreign target packets")
    {
        DelayedDevice device;device.offscreen=true;RenderResources resources;auto pixels=surface();
        auto a=resources.snapshot(resources.create(pixels.get(),1)),b=resources.snapshot(resources.create(pixels.get(),2));
        auto backend=makeDeviceRenderBackend(device,resources,16,16);SDL_Rect scissor{2,3,7,8};
        REQUIRE(setDeviceTargetScissor(*backend,&scissor));backend->fill({0,0,16,16},{255,0,0,128});
        REQUIRE(pushDeviceTarget(*backend,a,2,1));backend->fill({0,0,2,1},{0,255,0,255});
        REQUIRE(setDeviceTargetScissor(*backend,&scissor));DrawPacket foreign;foreign.kind=DrawPacket::Kind::Fill;foreign.target=b;REQUIRE(recordDevicePacket(*backend,foreign));
        popDeviceTarget(*backend);backend->fill({0,0,16,16},{255,0,0,128});backend->flush();
        REQUIRE(device.pending.size()==1);const auto &packets=device.pending.front().frame.packets;REQUIRE(packets.size()==4);
        REQUIRE(packets[0].targetScissor);CHECK_FALSE(packets[1].targetScissor);CHECK_FALSE(packets[2].targetScissor);REQUIRE(packets[3].targetScissor);CHECK(packets[3].targetScissor->x==2);
        CHECK_FALSE(packets[0].compatible(packets[1]));SDL_Rect invalid{0,0,-1,1};CHECK_THROWS_AS(setDeviceTargetScissor(*backend,&invalid),std::invalid_argument);
    }

    TEST_CASE("intentional presentation hold preserves committed pixels and capture identity")
    {
        auto output=surface();auto device=makeSoftwareRenderDevice(output.get());RenderResources resources;
        auto backend=makeDeviceRenderBackend(*device,resources,2,1);
        backend->fill({0,0,2,1},{200,0,0,255});backend->present();
        const auto identity=submittedDeviceFrameIdentity(*backend);
        const auto generation=device->generation();
        backend->fill({0,0,2,1},{0,200,0,255});
        REQUIRE(deferDeviceFrame(*backend));CHECK_FALSE(didRecordDeviceFrame(*backend));
        backend->fill({0,0,2,1},{0,0,200,255}); // later commands cannot leak into held output
        backend->present();CHECK_FALSE(didPresentDeviceFrame(*backend));
        CHECK(didIntentionallyDeferDeviceFrame(*backend));CHECK(submittedDeviceFrameIdentity(*backend)==identity);
        CHECK(device->generation()==generation);
        Surface captured(backend->capture(),SDL_DestroySurface);REQUIRE(captured);CHECK(pixel(captured.get(),0).r==200);
        backend->fill({0,0,2,1},{0,0,180,255});backend->present();
        CHECK(didPresentDeviceFrame(*backend));CHECK_FALSE(didIntentionallyDeferDeviceFrame(*backend));
        captured.reset(backend->capture());REQUIRE(captured);CHECK(pixel(captured.get(),0).b==180);
        REQUIRE(deferDeviceFrame(*backend));backend->present();backend->reset();
        CHECK_FALSE(didIntentionallyDeferDeviceFrame(*backend));CHECK(submittedDeviceFrameIdentity(*backend)==0);
    }
    TEST_CASE("ordinary device failure cannot become an intentional presentation hold")
    {
        DelayedDevice device;RenderResources resources;auto backend=makeDeviceRenderBackend(device,resources,2,1);
        device.failSubmission=true;backend->fill({0,0,2,1},{200,0,0,255});
        CHECK_FALSE(deferDeviceFrame(*backend));CHECK_FALSE(didRecordDeviceFrame(*backend));
        CHECK_THROWS_AS(backend->present(),std::runtime_error);CHECK_FALSE(didIntentionallyDeferDeviceFrame(*backend));
        backend->reset();CHECK_FALSE(didIntentionallyDeferDeviceFrame(*backend));
    }

    TEST_CASE("capture identity remains the last committed frame across empty presentation and recreation")
    {
        auto output=surface();auto device=makeSoftwareRenderDevice(output.get());RenderResources resources;
        auto backend=makeDeviceRenderBackend(*device,resources,2,1);
        CHECK(submittedDeviceFrameIdentity(*backend)==0);
        backend->fill({0,0,2,1},{10,80,160,255});backend->present();
        const auto committed=submittedDeviceFrameIdentity(*backend);REQUIRE(committed!=0);
        backend->present();CHECK(submittedDeviceFrameIdentity(*backend)==committed);
        Surface captured(backend->capture(),SDL_DestroySurface);REQUIRE(captured);CHECK(pixel(captured.get(),0).b==160);
        backend->reset();CHECK(submittedDeviceFrameIdentity(*backend)==0);
        backend->fill({0,0,2,1},{0,0,230,255});backend->present();captured.reset(backend->capture());REQUIRE(captured);CHECK(pixel(captured.get(),0).b==230);
        backend.reset();device=makeSoftwareRenderDevice(output.get());backend=makeDeviceRenderBackend(*device,resources,2,1);
        CHECK(submittedDeviceFrameIdentity(*backend)==0);backend->fill({0,0,2,1},{220,0,0,255});backend->present();
        CHECK(submittedDeviceFrameIdentity(*backend)==1);captured.reset(backend->capture());REQUIRE(captured);CHECK(pixel(captured.get(),0).r==220);
    }
    TEST_CASE("discarded frame capture metadata returns to the last presented identity")
    {
        DelayedDevice device;RenderResources resources;auto backend=makeDeviceRenderBackend(device,resources,2,1);
        for(int index=0;index<3;++index){backend->fill({0,0,2,1},{255,0,0,255});backend->present();}
        const auto committed=submittedDeviceFrameIdentity(*backend);CHECK(committed==3);
        backend->fill({0,0,2,1},{0,0,255,255});backend->flush();CHECK_FALSE(didRecordDeviceFrame(*backend));
        backend->present();CHECK_FALSE(didPresentDeviceFrame(*backend));CHECK(submittedDeviceFrameIdentity(*backend)==committed);
        Surface captured(backend->capture(),SDL_DestroySurface);REQUIRE(captured);REQUIRE(device.captureRequests.size()==1);
        CHECK(device.captureRequests.back().frameIdentity==committed);
        backend->reset();CHECK(submittedDeviceFrameIdentity(*backend)==0);
    }
    TEST_CASE("authored mip revisions own padded pixels and charge every supplied level")
    {
        auto source=surface(3,3); paint(source.get(),255,0,0);
        std::vector<std::byte> base(64,std::byte{0}), half(16,std::byte{0}), last(4,std::byte{0});
        base[1]=std::byte{80};base[3]=std::byte{128};half[2]=std::byte{200};half[3]=std::byte{255};last[0]=std::byte{90};last[3]=std::byte{255};
        std::array<RenderMipLevel,3> levels{{{4,4,SDL_PIXELFORMAT_RGBA32,base},{2,2,SDL_PIXELFORMAT_RGBA32,half},{1,1,SDL_PIXELFORMAT_RGBA32,last}}};
        RenderResources resources(84);auto handle=resources.create(source.get(),7,{},levels);REQUIRE(handle);
        auto revision=resources.snapshot(handle);REQUIRE(revision);CHECK(revision->descriptor.width==4);CHECK(revision->descriptor.height==4);
        CHECK(revision->descriptor.mips==TextureDescriptor::Mips::Generated);CHECK(revision->pixels->format==SDL_PIXELFORMAT_RGBA32);
        REQUIRE(revision->mips.size()==2);CHECK(revision->bytes==84);CHECK(resources.retainedBytes()==84);
        half[2]=std::byte{0};last[0]=std::byte{0};CHECK(revision->mips[0].rgba[2]==std::byte{200});CHECK(revision->mips[1].rgba[0]==std::byte{90});
        CHECK(resources.erase(handle));CHECK(resources.retainedBytes()==84);revision.reset();CHECK(resources.retainedBytes()==0);
        RenderResources smaller(83);CHECK_FALSE(smaller.create(source.get(),7,{},levels));CHECK(smaller.retainedBytes()==0);
        CHECK_THROWS_AS(resources.create(source.get(),7,{},std::span(levels).first(2)),std::invalid_argument);
        levels[1].width=3;CHECK_THROWS_AS(resources.create(source.get(),7,{},levels),std::invalid_argument);
        TextureDescriptor descriptor;descriptor.mips=TextureDescriptor::Mips::Generated;CHECK_THROWS_AS(resources.create(source.get(),7,descriptor),std::invalid_argument);
    }

    TEST_CASE("software device preserves nearest authored mip fallback with owned RGBA pixels")
    {
        auto output=surface();auto device=makeSoftwareRenderDevice(output.get());RenderResources resources;
        auto original=surface(1,1);paint(original.get(),255,0,0);
        std::array<std::byte,4> bytes{std::byte{0},std::byte{0},std::byte{255},std::byte{255}};
        std::array<RenderMipLevel,1> levels{{{1,1,SDL_PIXELFORMAT_RGBA32,bytes}}};
        auto backend=makeDeviceRenderBackend(*device,resources,2,1);
        backend->blitMipmapped(original.get(),original.get(),1,levels,{0,0,1,1},{0,0,2,1},255);
        bytes[2]=std::byte{0};CHECK_NOTHROW(backend->present());CHECK(didPresentDeviceFrame(*backend));
        CHECK(pixel(output.get(),0).b==255);CHECK(pixel(output.get(),1).b==255);CHECK(pixel(output.get(),0).r==0);
    }
    TEST_CASE("authored upload promotion preserves queued ordinary resource identity")
    {
        auto original=surface(1,1);paint(original.get(),255,0,0);DelayedDevice device;RenderResources resources;
        auto backend=makeDeviceRenderBackend(device,resources,2,1);backend->blit(original.get(),original.get(),1,false,{0,0,1,1},{0,0,1,1},255);
        std::array<std::byte,4> bytes{std::byte{0},std::byte{0},std::byte{255},std::byte{255}};
        std::array<RenderMipLevel,1> levels{{{1,1,SDL_PIXELFORMAT_RGBA32,bytes}}};
        backend->blitMipmapped(original.get(),original.get(),1,levels,{0,0,1,1},{1,0,1,1},255);backend->present();
        REQUIRE(device.pending.size()==1);const auto &packets=device.pending.front().frame.packets;REQUIRE(packets.size()==2);
        CHECK(packets[0].texture->handle!=packets[1].texture->handle);CHECK(packets[0].texture->descriptor.mips==TextureDescriptor::Mips::Disabled);
        CHECK(packets[1].texture->descriptor.mips==TextureDescriptor::Mips::Generated);CHECK(pixel(packets[0].texture->pixels.get(),0).r==255);
        CHECK(pixel(packets[1].texture->pixels.get(),0).b==255);
    }
    TEST_CASE("implicit capacity submission defers fatal device errors to presentation boundary")
    {
        DelayedDevice device;RenderResources resources;auto backend=makeDeviceRenderBackend(device,resources,2,1);
        device.failSubmission=true;
        CHECK_NOTHROW([&]{for(std::size_t i=0;i<=RenderFrame::CommandCapacity;++i)backend->fill({0,0,2,1},{255,0,0,255});}());
        CHECK_FALSE(didRecordDeviceFrame(*backend));CHECK(device.pending.empty());
        CHECK_THROWS_WITH(backend->present(),"Injected device loss");CHECK_FALSE(didPresentDeviceFrame(*backend));
        backend->reset();device.failSubmission=false;
        backend->fill({0,0,2,1},{0,0,255,255});backend->present();CHECK(didPresentDeviceFrame(*backend));REQUIRE(device.pending.size()==1);
    }
    TEST_CASE("software discard restores committed pixels and next frame remains usable")
    {
        auto output=surface();paint(output.get(),0,0,0);auto device=makeSoftwareRenderDevice(output.get());
        RenderResources resources;auto backend=makeDeviceRenderBackend(*device,resources,2,1);
        backend->fill({0,0,2,1},{255,0,0,255});backend->present();
        backend->fill({0,0,2,1},{0,255,0,255});backend->flush();device->discardFrame();
        CHECK(pixel(output.get(),0).r==255);CHECK(pixel(output.get(),0).g==0);
        backend->fill({0,0,2,1},{0,0,255,255});backend->present();CHECK(pixel(output.get(),0).b==255);
        CHECK(device->telemetry().cpuSubmitNanoseconds>0);
    }
    TEST_CASE("bounded admission failure aborts frame without throwing and later admission resumes")
    {
        DelayedDevice device;RenderResources resources;auto backend=makeDeviceRenderBackend(device,resources,2,1);
        for(int i=0;i<3;++i){backend->fill({0,0,2,1},{255,0,0,255});backend->flush();}
        REQUIRE(device.pending.size()==3);
        backend->fill({0,0,2,1},{0,255,0,255});backend->flush();CHECK_FALSE(didRecordDeviceFrame(*backend));
        backend->fill({0,0,2,1},{0,0,255,255});backend->present();CHECK_FALSE(didPresentDeviceFrame(*backend));
        CHECK(device.pending.empty());backend->fill({0,0,2,1},{0,0,255,255});backend->present();
        CHECK(didPresentDeviceFrame(*backend));REQUIRE(device.pending.size()==1);
    }
    TEST_CASE("in-flight revision pressure defers mutation with bounded owned pixels")
    {
        DelayedDevice device;RenderResources resources(8);auto source=surface();paint(source.get(),255,0,0);
        auto backend=makeDeviceRenderBackend(device,resources,2,1);const SDL_Rect bounds{0,0,2,1};
        backend->blit(source.get(),source.get(),1,true,bounds,{0,0,2,1},255);backend->flush();
        REQUIRE(device.pending.size()==1);paint(source.get(),0,255,0);
        backend->blit(source.get(),source.get(),2,true,bounds,{0,0,2,1},255);
        CHECK_FALSE(didRecordDeviceFrame(*backend));CHECK(resources.retainedBytes()==8);
        CHECK(pixel(device.pending.front().frame.packets.front().texture->pixels.get(),0).r==255);
        backend->present();CHECK_FALSE(didPresentDeviceFrame(*backend));CHECK(resources.retainedBytes()==0);
        backend->blit(source.get(),source.get(),2,true,bounds,{0,0,2,1},255);backend->present();
        CHECK(didPresentDeviceFrame(*backend));REQUIRE(device.pending.size()==1);
        CHECK(pixel(device.pending.front().frame.packets.front().texture->pixels.get(),0).g==255);
    }
    TEST_CASE("queued drawing retains pixels across mutation deletion and slot reuse")
    {
        auto source = surface(); REQUIRE(source);
        paint(source.get(), 255, 0, 0);
        RenderResources resources;
        DelayedDevice device;
        auto backend = makeDeviceRenderBackend(device, resources, 2, 1);
        const SDL_Rect src{0, 0, 1, 1};
        backend->blit(source.get(), source.get(), 1, true, src, {0, 0, 1, 1}, 255);
        paint(source.get(), 0, 255, 0);
        backend->blit(source.get(), source.get(), 2, true, src, {1, 0, 1, 1}, 255);
        backend->forget(source.get());
        backend->flush();
        REQUIRE(device.pending.size() == 1);
        auto &packets = device.pending.front().frame.packets;
        REQUIRE(packets.size() == 2);
        CHECK(pixel(packets[0].texture->pixels.get(), 0).r == 255);
        CHECK(pixel(packets[1].texture->pixels.get(), 0).g == 255);
        const auto stale = packets[0].texture->handle;
        auto replacement = resources.create(source.get(), 3);
        CHECK(replacement.slot == stale.slot);
        CHECK(replacement.generation != stale.generation);
        CHECK_FALSE(resources.snapshot(stale));
        CHECK(resources.retainedBytes() == 24);
        const auto submission = device.pending.front().submission;
        CHECK(device.completion(submission) == RenderCompletion::Pending);
        device.finish();
        CHECK(device.completion(submission) == RenderCompletion::Complete);
        CHECK(resources.retainedBytes() == 8);
        device.reset();
        CHECK(device.completion(submission) == RenderCompletion::Cancelled);
        CHECK(resources.snapshot(replacement)); // Device loss leaves logical pixels intact.
    }
    TEST_CASE("software command adapter renders old and new revisions before forgetting sources")
    {
        auto source = surface(); auto target = surface(); REQUIRE(source); REQUIRE(target);
        RenderResources resources;
        auto device = makeSoftwareRenderDevice(target.get());
        auto backend = makeDeviceRenderBackend(*device, resources, 2, 1);
        paint(source.get(), 255, 0, 0);
        const SDL_Rect src{0, 0, 1, 1};
        backend->blit(source.get(), source.get(), 1, true, src, {0, 0, 1, 1}, 255);
        paint(source.get(), 0, 255, 0);
        backend->blit(source.get(), source.get(), 2, true, src, {1, 0, 1, 1}, 255);
        backend->forget(source.get()); source.reset();
        backend->present();
        CHECK(pixel(target.get(), 0).r == 255);
        CHECK(pixel(target.get(), 1).g == 255);
        CHECK(resources.retainedBytes() == 0);
        CHECK(device->telemetry().packets == 2);
    }
    TEST_CASE("CPU texture cache evicts inactive assets within a small registry budget")
    {
        auto target = surface(); auto red = surface(); auto green = surface(); auto blue = surface();
        REQUIRE(target); REQUIRE(red); REQUIRE(green); REQUIRE(blue);
        paint(red.get(), 255, 0, 0); paint(green.get(), 0, 255, 0); paint(blue.get(), 0, 0, 255);
        RenderResources resources(16, 2); auto device = makeSoftwareRenderDevice(target.get());
        auto backend = makeDeviceRenderBackend(*device, resources, 2, 1);
        const SDL_Rect src{0, 0, 1, 1};
        for (int cycle = 0; cycle < 20; ++cycle) {
            for (auto *source : {red.get(), green.get(), blue.get()}) {
                backend->blit(source, source, 1, true, src, {0, 0, 1, 1}, 255);
                backend->present();
                CHECK(resources.retainedBytes() <= 16);
                CHECK(pixel(target.get(), 0).r == pixel(source, 0).r);
                CHECK(pixel(target.get(), 0).g == pixel(source, 0).g);
                CHECK(pixel(target.get(), 0).b == pixel(source, 0).b);
            }
        }
    }
    TEST_CASE("transient source views with no asset identity cannot reuse old pixels")
    {
        auto source = surface(); auto target = surface(); REQUIRE(source); REQUIRE(target);
        RenderResources resources;
        auto device = makeSoftwareRenderDevice(target.get());
        auto backend = makeDeviceRenderBackend(*device, resources, 2, 1);
        const SDL_Rect src{0, 0, 1, 1};
        paint(source.get(), 255, 0, 0);
        backend->blit(nullptr, source.get(), 0, true, src, {0, 0, 1, 1}, 255);
        paint(source.get(), 0, 255, 0);
        backend->blit(nullptr, source.get(), 0, true, src, {1, 0, 1, 1}, 255);
        backend->present();
        CHECK(pixel(target.get(), 0).r == 255);
        CHECK(pixel(target.get(), 1).g == 255);
        CHECK(resources.retainedBytes() == 0);
    }
    TEST_CASE("command capacity flushes while adjacent batching preserves painter order and barriers")
    {
        DelayedDevice device; RenderResources resources;
        auto backend = makeDeviceRenderBackend(device, resources, 2, 1);
        for (std::size_t i = 0; i < RenderFrame::CommandCapacity + 1; ++i)
            backend->fill({float(i), 0, 1, 1}, {255, 0, 0, 255});
        backend->flush();
        REQUIRE(device.pending.size() == 2);
        CHECK(device.pending[0].frame.packets.size() == 4096);
        CHECK(device.pending[1].frame.packets.size() == 1);
        CHECK(device.pending[0].frame.packets.back().destination.x == 4095);
        CHECK(device.pending[1].frame.packets.front().destination.x == 4096);
        RenderFrame frame;
        DrawPacket a; DrawPacket b; b.clip = SDL_Rect{0, 0, 1, 1};
        DrawPacket barrier; barrier.kind = DrawPacket::Kind::Barrier;
        frame.packets = {a, a, b, a, barrier, a};
        CHECK(frame.batchEnd(0) == 2);
        CHECK(frame.batchEnd(2) == 3);
        CHECK(frame.batchEnd(3) == 4);
        CHECK(frame.batchEnd(4) == 5);
    }
    TEST_CASE("bounded resource and upload admission retains in-flight revisions until cancellation")
    {
        auto source = surface(); REQUIRE(source);
        RenderResources resources(16, 1);
        auto handle = resources.create(source.get(), 1); REQUIRE(handle);
        auto old = resources.snapshot(handle);
        CHECK(resources.update(handle, source.get(), 2));
        CHECK_FALSE(resources.update(handle, source.get(), 3));
        CHECK(resources.snapshot(handle)->contentRevision == 2);
        RenderUploadQueue uploads(8, 1);
        CHECK(uploads.enqueue({old, 7}));
        CHECK_FALSE(uploads.enqueue({resources.snapshot(handle), 7}));
        old.reset();
        CHECK(resources.retainedBytes() == 16);
        uploads.cancel();
        CHECK(resources.retainedBytes() == 8);
        CHECK(resources.update(handle, source.get(), 3));
        CHECK_FALSE(resources.create(source.get(), 4));
        CHECK(resources.erase(handle));
        CHECK_FALSE(resources.snapshot(handle));
        CHECK(resources.retainedBytes() == 0);
    }
    TEST_CASE("oversized geometry is rejected and cancellation releases unsent pixels")
    {
        std::vector<RenderFrame> submitted;
        RenderFrameRecorder recorder([&](RenderFrame frame) { submitted.push_back(std::move(frame)); });
        recorder.begin(1, 2, 1);
        DrawPacket oversized; oversized.vertices.resize(RenderFrame::VertexCapacity + 1);
        CHECK_THROWS_AS(recorder.append(std::move(oversized)), std::length_error);
        CHECK(recorder.size() == 0);
        auto source = surface(); REQUIRE(source);
        RenderResources resources;
        auto handle = resources.create(source.get(), 1);
        DrawPacket packet; packet.texture = resources.snapshot(handle); recorder.append(std::move(packet));
        resources.erase(handle);
        CHECK(resources.retainedBytes() == 8);
        recorder.cancel();
        CHECK(resources.retainedBytes() == 0);
        CHECK(submitted.empty());
    }
    TEST_CASE("recorded primitive composites inherit map transforms and clipping")
    {
        auto target=surface(8,2),source=surface(1,1);paint(target.get(),0,0,0);paint(source.get(),255,0,0);
        RenderResources resources;auto device=makeSoftwareRenderDevice(target.get());auto backend=makeDeviceRenderBackend(*device,resources,8,2);
        auto image=resources.create(source.get(),1);const SDL_Rect clip{1,0,1,2};backend->transform(1,2,0,nullptr);backend->clip(&clip);
        DrawPacket packet;packet.kind=DrawPacket::Kind::Blit;packet.texture=resources.snapshot(image);packet.source={0,0,1,1};packet.destination={0,0,4,2};packet.opaque=true;
        REQUIRE(recordDevicePacket(*backend,packet));backend->present();CHECK(pixel(target.get(),3).r==255);CHECK(pixel(target.get(),2).r==0);CHECK(pixel(target.get(),4).r==0);
    }
    TEST_CASE("texture categories enforce independent limits and explicit total budgets")
    {
        auto large=surface(4096,4097); REQUIRE(large);
        RenderResources resources; TextureDescriptor descriptor;
        CHECK_FALSE(resources.create(large.get(),1,descriptor));
        descriptor.budgetClass=TextureDescriptor::BudgetClass::SkinAtlas;
        CHECK_FALSE(resources.create(large.get(),1,descriptor));
        descriptor.budgetClass=TextureDescriptor::BudgetClass::TerrainPage;
        auto terrain=resources.create(large.get(),1,descriptor); REQUIRE(terrain);
        CHECK(resources.retainedBytes()==std::size_t(large->pitch)*large->h);
        CHECK(resources.erase(terrain)); CHECK(resources.retainedBytes()==0);
        auto small=surface(); RenderResources bounded(4); descriptor.budgetClass=TextureDescriptor::BudgetClass::SkinAtlas;
        CHECK_FALSE(bounded.create(small.get(),1,descriptor));
    }
    TEST_CASE("rig geometry rejects invalid GPU palette indexing")
    {
        RenderResources resources; std::array<float,18> vertex{}; vertex[14]=1;
        vertex[10]=32; CHECK_THROWS_AS(resources.createGeometry(GeometryLayout::SkinRig72,std::as_bytes(std::span(vertex)),{},1),std::invalid_argument);
        vertex[10]=.5f; CHECK_THROWS_AS(resources.createGeometry(GeometryLayout::SkinRig72,std::as_bytes(std::span(vertex)),{},1),std::invalid_argument);
        vertex[10]=0; vertex[14]=-1; CHECK_THROWS_AS(resources.createGeometry(GeometryLayout::SkinRig72,std::as_bytes(std::span(vertex)),{},1),std::invalid_argument);
    }
    TEST_CASE("software cropped capture uses submitted target pixels and admission metadata")
    {
        auto target=surface(2,2); REQUIRE(target); paint(target.get(),255,0,0);
        auto device=makeSoftwareRenderDevice(target.get()); CaptureRequest request;request.pixelRegion=SDL_Rect{1,1,1,1};request.timestamp=19;
        REQUIRE(device->capture(request)); CaptureResult result;REQUIRE(device->pollCapture(result));
        CHECK(result.request.width==1);CHECK(result.request.height==1);CHECK(result.request.timestamp==19);CHECK(pixel(result.pixels.get(),0).r==255);
        request.pixelRegion=SDL_Rect{1,1,2,1}; CHECK_FALSE(device->capture(request));
    }
    TEST_CASE("persistent geometry rejects invalid ranges and retains deleted generations")
    {
        RenderResources resources;
        std::array<WorldVertex,3> vertices{};
        const std::array<std::uint32_t,3> invalid{0,1,3};
        CHECK_THROWS_AS(resources.createGeometry(GeometryLayout::World48,std::as_bytes(std::span(vertices)),invalid,1),std::invalid_argument);
        const std::array<std::uint32_t,3> indices{0,1,2};
        auto handle=resources.createGeometry(GeometryLayout::World48,std::as_bytes(std::span(vertices)),indices,1); REQUIRE(handle);
        auto queued=resources.snapshot(handle); REQUIRE(queued);
        CHECK(resources.erase(handle)); CHECK_FALSE(resources.snapshot(handle));
        auto replacement=resources.createGeometry(GeometryLayout::World48,std::as_bytes(std::span(vertices)),indices,2); REQUIRE(replacement);
        CHECK(replacement.slot==handle.slot); CHECK(replacement.generation!=handle.generation);
        CHECK(queued->contentRevision==1); CHECK(queued->indices[2]==2);
        CHECK(resources.retainedGeometryBytes()==2*(sizeof(vertices)+sizeof(indices)));
        queued.reset(); CHECK(resources.retainedGeometryBytes()==sizeof(vertices)+sizeof(indices));
        resources.erase(replacement); CHECK(resources.retainedGeometryBytes()==0);
    }
    TEST_CASE("capture metadata survives completion and bounded capture queue resets")
    {
        auto target = surface(); REQUIRE(target);
        auto device = makeSoftwareRenderDevice(target.get());
        for (std::uint64_t i = 1; i <= 3; ++i) {
            CaptureRequest request; request.identity = i; request.frameIdentity = 23;
            request.timestamp = 100 + i; request.recordingGeneration = 7; request.context = "editor";
            CHECK(device->capture(std::move(request)));
        }
        CHECK_FALSE(device->capture({}));
        CaptureResult result;
        REQUIRE(device->pollCapture(result));
        CHECK(result.request.timestamp == 101);
        CHECK(result.request.frameIdentity == 23);
        CHECK(result.request.recordingGeneration == 7);
        CHECK(result.request.context == "editor");
        CHECK(result.request.width == 2);
        REQUIRE(result.pixels);
        device->reset();
        CHECK_FALSE(device->pollCapture(result));
        CHECK(device->telemetry().captureDrops == 3);
    }
}
