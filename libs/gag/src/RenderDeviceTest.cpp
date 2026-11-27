// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <RenderDevice.h>
#include <deque>
#include <array>
#include <cstring>
#include <string_view>
#include <utility>

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
    bool failSubmission=false, offscreen=false, bc3=false, astc=false;
    std::vector<CaptureRequest> captureRequests;
    RenderDeviceCapabilities capabilities() const override { RenderDeviceCapabilities c;c.offscreenTargets=offscreen;c.bc3Textures=bc3;c.astc4x4Textures=astc;return c; }
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
    TEST_CASE("additive blits add filtered light and never join ordinary blits")
    {
        auto source=surface(1,1);paint(source.get(),40,80,120);DelayedDevice device;RenderResources resources;
        auto backend=makeDeviceRenderBackend(device,resources,2,1);
        backend->blit(source.get(),source.get(),1,false,{0,0,1,1},{0,0,1,1},200);
        REQUIRE(backend->blitAdditive(source.get(),source.get(),1,{0,0,1,1},{1,0,1,1},128));
        backend->blit(source.get(),source.get(),1,false,{0,0,1,1},{0,0,1,1},200);
        backend->present();
        REQUIRE(device.pending.size()==1);const auto &packets=device.pending.front().frame.packets;REQUIRE(packets.size()==3);
        CHECK(packets[0].blend==DrawPacket::Blend::Automatic);
        CHECK(packets[1].blend==DrawPacket::Blend::Additive);CHECK(packets[1].linear);CHECK(packets[1].alpha==128);
        CHECK(packets[1].texture==packets[0].texture);
        CHECK_FALSE(packets[0].compatible(packets[1]));CHECK_FALSE(packets[1].compatible(packets[2]));
    }
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

    TEST_CASE("compressed revisions keep only the blocks of levels at least 4x4 and charge them")
    {
      // BC3 and ASTC 4x4 share the 16-byte 4x4 block size and these rules.
      for(const auto format:{TextureCompression::BC3,TextureCompression::ASTC4x4}) {
        CAPTURE(int(format));
        auto source=surface(6,8);paint(source.get(),255,0,0);
        // An 8x16 chain: 8x16, 4x8 carry blocks; 2x4 and 1x2 cannot, and 1x1 ends it.
        std::array<std::vector<std::byte>,5> blocks{std::vector<std::byte>(128,std::byte{1}),std::vector<std::byte>(32,std::byte{2}),{},{},{}};
        std::array<RenderMipLevel,5> levels{{{8,16},{4,8},{2,4},{1,2},{1,1}}};
        for(std::size_t i=0;i<levels.size();++i){levels[i].compression=format;levels[i].blocks=blocks[i];}
        TextureDescriptor descriptor;descriptor.compression=format;
        RenderResources resources(160);auto handle=resources.create(source.get(),3,descriptor,levels);REQUIRE(handle);
        auto revision=resources.snapshot(handle);REQUIRE(revision);
        CHECK_FALSE(revision->pixels);CHECK(revision->mips.empty());REQUIRE(revision->blocks.size()==2);
        CHECK(revision->blocks[1][0]==std::byte{2});CHECK(revision->bytes==160);CHECK(resources.retainedBytes()==160);
        CHECK(revision->descriptor.width==8);CHECK(revision->descriptor.height==16);
        CHECK(revision->descriptor.alpha==TextureDescriptor::Alpha::Premultiplied);
        CHECK(revision->descriptor.mips==TextureDescriptor::Mips::Generated);
        CHECK(resources.erase(handle));revision.reset();CHECK(resources.retainedBytes()==0);
        RenderResources smaller(159);CHECK_FALSE(smaller.create(source.get(),3,descriptor,levels));
        blocks[1].pop_back();levels[1].blocks=blocks[1];
        CHECK_THROWS_AS(resources.create(source.get(),3,descriptor,levels),std::invalid_argument);
        CHECK(resources.retainedBytes()==0);
        // A level in the other block format cannot join the chain.
        blocks[1].push_back(std::byte{2});levels[1].blocks=blocks[1];
        levels[1].compression=format==TextureCompression::BC3?TextureCompression::ASTC4x4:TextureCompression::BC3;
        CHECK_THROWS_AS(resources.create(source.get(),3,descriptor,levels),std::invalid_argument);
      }
    }

    TEST_CASE("device adapters store prepared blocks only for devices that sample them")
    {
        auto original=surface(4,4);paint(original.get(),255,0,0);
        std::vector<std::byte> base(64,std::byte{255}),half(16,std::byte{255}),last(4,std::byte{255}),blocks(16,std::byte{7});
        for(const auto format:{TextureCompression::BC3,TextureCompression::ASTC4x4}) {
          std::array<RenderMipLevel,3> levels{{{4,4,SDL_PIXELFORMAT_RGBA32,base,format,blocks},
              {2,2,SDL_PIXELFORMAT_RGBA32,half},{1,1,SDL_PIXELFORMAT_RGBA32,last}}};
          // Neither format, the other one only, the chain's one, and both.
          for(const auto [bc3,astc]:{std::pair{false,false},{true,false},{false,true},{true,true}}) {
            CAPTURE(int(format));CAPTURE(bc3);CAPTURE(astc);
            DelayedDevice device;device.bc3=bc3;device.astc=astc;RenderResources resources;
            const bool sampled=format==TextureCompression::BC3?bc3:astc;
            auto backend=makeDeviceRenderBackend(device,resources,2,1);
            backend->blitMipmapped(original.get(),original.get(),1,levels,{0,0,4,4},{0,0,2,1},255);backend->present();
            REQUIRE(device.pending.size()==1);const auto &texture=device.pending.front().frame.packets.at(0).texture;
            CHECK(texture->descriptor.compression==(sampled?format:TextureCompression::Uncompressed));
            CHECK(bool(texture->pixels)==!sampled);CHECK(texture->blocks.size()==(sampled?1u:0u));
            CHECK(texture->bytes==(sampled?16u:84u));
          }
        }
        std::array<RenderMipLevel,3> levels{{{4,4,SDL_PIXELFORMAT_RGBA32,base,TextureCompression::ASTC4x4,blocks},
            {2,2,SDL_PIXELFORMAT_RGBA32,half},{1,1,SDL_PIXELFORMAT_RGBA32,last}}};
        // A chain that kept only its ASTC blocks cannot reach a device without
        // them, even one that samples BC3.
        levels[0].pixels={};DelayedDevice device;device.bc3=true;RenderResources resources;
        auto backend=makeDeviceRenderBackend(device,resources,2,1);
        CHECK_THROWS_AS(backend->blitMipmapped(original.get(),original.get(),1,levels,{0,0,4,4},{0,0,2,1},255),std::invalid_argument);
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
    TEST_CASE("draws admit textures in the selected device budget class")
    {
        // Terrain pages count against the terrain page class, so a view's pages
        // cannot exhaust the general class every frame and hold presentation.
        auto page=surface(1,1),sprite=surface(1,1);paint(page.get(),255,0,0);paint(sprite.get(),0,0,255);
        DelayedDevice device;RenderResources resources;
        auto backend=makeDeviceRenderBackend(device,resources,2,1);
        backend->textureBudgetClass(TextureDescriptor::BudgetClass::TerrainPage);
        backend->blit(page.get(),page.get(),1,false,{0,0,1,1},{0,0,1,1},255);
        backend->textureBudgetClass(TextureDescriptor::BudgetClass::General);
        backend->blit(sprite.get(),sprite.get(),1,false,{0,0,1,1},{1,0,1,1},255);
        // A key admitted in another class takes a new resource, not a relabelled one.
        backend->blit(page.get(),page.get(),1,false,{0,0,1,1},{1,0,1,1},255);backend->present();
        REQUIRE(device.pending.size()==1);const auto &packets=device.pending.front().frame.packets;REQUIRE(packets.size()==3);
        CHECK(packets[0].texture->descriptor.budgetClass==TextureDescriptor::BudgetClass::TerrainPage);
        CHECK(packets[1].texture->descriptor.budgetClass==TextureDescriptor::BudgetClass::General);
        CHECK(packets[2].texture->descriptor.budgetClass==TextureDescriptor::BudgetClass::General);
        CHECK(packets[0].texture->handle!=packets[2].texture->handle);
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
    TEST_CASE("retained snapshots draw released sources across eviction, reset and recovery")
    {
        auto target = surface(); auto red = surface(); REQUIRE(target); REQUIRE(red);
        std::array<Surface, 3> others{surface(), surface(), surface()};
        paint(red.get(), 255, 0, 0);
        for (std::size_t i = 0; i < others.size(); ++i) { REQUIRE(others[i]); paint(others[i].get(), 0, Uint8(80 * (i + 1)), 0); }
        // Room for three 8-byte snapshots; at most half of the class is retained.
        RenderResources resources(24, 8); auto device = makeSoftwareRenderDevice(target.get());
        auto backend = makeDeviceRenderBackend(*device, resources, 2, 1);
        const int key = 0; const SDL_Rect src{0, 0, 1, 1};
        CHECK_FALSE(retainDeviceTexture(*backend, &key, 1)); // No snapshot yet.
        backend->blit(&key, red.get(), 1, true, src, {0, 0, 1, 1}, 255);
        CHECK_FALSE(retainDeviceTexture(*backend, &key, 2)); // Another revision.
        REQUIRE(retainDeviceTexture(*backend, &key, 1));
        CHECK(retainedDeviceTextureBytes(*backend) == 8);
        red.reset(); // The owner frees its pixels.
        backend->blit(others[0].get(), others[0].get(), 1, true, src, {0, 0, 1, 1}, 255);
        CHECK_FALSE(retainDeviceTexture(*backend, others[0].get(), 1)); // Over half the class.
        const auto drawsRed = [&](RenderBackend &adapter) {
            REQUIRE(adapter.blitCached(&key, src, {1, 0, 1, 1}, 255));
            adapter.present();
            CHECK(pixel(target.get(), 1).r == 255);
            CHECK(pixel(target.get(), 1).g == 0);
        };
        // Pressure evicts only the snapshots their owners can re-create.
        for (int cycle = 0; cycle < 6; ++cycle)
            for (auto &other : others) {
                backend->blit(other.get(), other.get(), 1, true, src, {0, 0, 1, 1}, 255);
                CHECK(resources.retainedBytes() <= 24);
                CHECK(backend->hasTexture(&key, 1));
            }
        drawsRed(*backend);
        backend->reset(); // Device reset: GPU copies go, snapshots stay.
        drawsRed(*backend);
        // Context recovery: a replacement adapter adopts every snapshot.
        auto replacementDevice = makeSoftwareRenderDevice(target.get());
        auto replacement = makeDeviceRenderBackend(*replacementDevice, resources, 2, 1);
        CHECK_FALSE(replacement->hasTexture(&key, 1));
        adoptDeviceTextures(*backend, *replacement);
        CHECK_FALSE(backend->hasTexture(&key, 1));
        const auto held = resources.retainedBytes();
        backend.reset(); device.reset();
        CHECK(resources.retainedBytes() == held);
        CHECK(retainedDeviceTextureBytes(*replacement) == 8);
        drawsRed(*replacement);
        replacement->forget(&key);
        CHECK_FALSE(replacement->blitCached(&key, src, {1, 0, 1, 1}, 255));
        CHECK(retainedDeviceTextureBytes(*replacement) == 0);
        replacement.reset();
        CHECK(resources.retainedBytes() == 0);
    }
    TEST_CASE("snapshot trims erase only idle textures, a bounded number a call, and redraw from pixels")
    {
        auto target = surface(); REQUIRE(target);
        std::array<Surface, 4> sources{surface(), surface(), surface(), surface()};
        for (std::size_t i = 0; i < sources.size(); ++i) { REQUIRE(sources[i]); paint(sources[i].get(), Uint8(60 * (i + 1)), 0, 0); }
        RenderResources resources; auto device = makeSoftwareRenderDevice(target.get());
        auto backend = makeDeviceRenderBackend(*device, resources, 2, 1);
        const SDL_Rect src{0, 0, 1, 1};
        for (auto &source : sources) backend->blit(source.get(), source.get(), 1, true, src, {0, 0, 1, 1}, 255);
        backend->present();
        backend->blit(sources[3].get(), sources[3].get(), 1, true, src, {0, 0, 1, 1}, 255);
        backend->present();
        CHECK(resources.retainedBytes() == 4 * 8);
        // Three were not drawn in the last presentation; one a call.
        CHECK(trimDeviceTextures(*backend, 1, 1) == 1);
        CHECK(resources.retainedBytes() == 3 * 8);
        CHECK(trimDeviceTextures(*backend, 8, 1) == 2);
        CHECK(trimDeviceTextures(*backend, 8, 1) == 0);
        CHECK(resources.retainedBytes() == 8);
        // An erased snapshot is re-created from its owner's pixels.
        backend->blit(sources[0].get(), sources[0].get(), 1, true, src, {1, 0, 1, 1}, 255);
        backend->present();
        CHECK(pixel(target.get(), 1).r == 60);
        CHECK(trimDeviceTextures(*backend, 8, 0) == 2); // Zero idle frames: everything.
        CHECK(resources.retainedBytes() == 0);
        CHECK(trimDeviceTextures(*makeSoftwareRenderBackend(target.get()), 8, 0) == 0);
    }
    TEST_CASE("mobile budgets are smaller yet admit the largest single texture of each class twice")
    {
        const auto desktop = RenderBudget::desktop(), mobile = RenderBudget::mobile();
        constexpr std::size_t MiB = 1024 * 1024;
        CHECK(desktop.total == 512 * MiB);
        CHECK(desktop.classes == std::array<std::size_t, 4>{256 * MiB, 64 * MiB, 128 * MiB, 32 * MiB});
        CHECK(mobile.total < desktop.total);
        for (std::size_t i = 0; i < 4; ++i) { CHECK(mobile.classes[i] <= desktop.classes[i]); CHECK(mobile.classes[i] <= mobile.total); }
        // The authored HD terrain block (22.4 MiB with mips), skin atlas pages
        // (2048x2048) and 16 downsampled 1024x1024 terrain pages.
        CHECK(mobile.classLimit(TextureDescriptor::BudgetClass::General) >= 2 * 22.4 * MiB);
        CHECK(mobile.classLimit(TextureDescriptor::BudgetClass::SkinAtlas) >= 2 * 2048 * 2048 * 4);
        CHECK(mobile.classLimit(TextureDescriptor::BudgetClass::TerrainPage) >= 16 * 1024 * 1024 * 4);
        RenderResources resources(RenderBudget{64, {16, 8, 32, 8}, 64});
        CHECK(resources.byteLimit() == 64);
        CHECK(resources.classLimit(TextureDescriptor::BudgetClass::SkinAtlas) == 8);
        auto wide = surface(4, 1); REQUIRE(wide); // 16 bytes
        TextureDescriptor skin; skin.budgetClass = TextureDescriptor::BudgetClass::SkinAtlas;
        CHECK_FALSE(resources.create(wide.get(), 1, skin));
        CHECK(resources.create(wide.get(), 1));
        CHECK_FALSE(resources.create(wide.get(), 1)); // General is full.
    }
    TEST_CASE("large budgets hold at least their standard ones, and every budget has a name")
    {
        constexpr std::size_t MiB = 1024 * 1024;
        const std::pair<RenderBudget, RenderBudget> pairs[] = {
            {RenderBudget::desktop(), RenderBudget::desktopLarge()}, {RenderBudget::mobile(), RenderBudget::mobileLarge()}};
        for (const auto &[standard, large] : pairs) {
            CAPTURE(RenderBudget::name(large));
            CHECK(large.total > standard.total);
            CHECK(large.geometry >= standard.geometry);
            for (std::size_t i = 0; i < 4; ++i) {
                CHECK(large.classes[i] >= standard.classes[i]);
                CHECK(large.classes[i] <= large.total);
            }
            CHECK(large.terrain.resident >= standard.terrain.resident);
            CHECK(large.terrain.cache >= standard.terrain.cache);
            CHECK(large.terrain.direct >= standard.terrain.direct);
            CHECK(large.terrain.cache >= large.terrain.resident);
        }
        // A High phone's General class leaves HD frames (3/8 of it) more than
        // the 18 MiB the standard phone budget does.
        CHECK(RenderBudget::mobileLarge().classLimit(TextureDescriptor::BudgetClass::General) / 8 * 3 >= 48 * MiB);
        // The large phone budget stays below the standard desktop one.
        CHECK(RenderBudget::mobileLarge().total < RenderBudget::desktop().total);
        for (const char *name : {"desktop", "mobile", "desktop-large", "mobile-large"}) {
            CAPTURE(name);
            REQUIRE(RenderBudget::named(name));
            CHECK(std::string_view(RenderBudget::name(*RenderBudget::named(name))) == name);
        }
        CHECK_FALSE(RenderBudget::named("huge"));
        auto custom = RenderBudget::desktop();
        custom.total += MiB;
        CHECK(std::string_view(RenderBudget::name(custom)) == "custom");
        // The test main chose the platform's budget first; later choices change nothing.
        const auto before = platformRenderBudget();
        CHECK_FALSE(selectPlatformRenderBudget(RenderBudget::desktopLarge()));
        CHECK(platformRenderBudget() == before);
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
        // Sizes come from the budget under test, and both platform budgets are
        // checked on every platform, so desktop runs also guard the mobile one.
        using BudgetClass = TextureDescriptor::BudgetClass;
        constexpr int width = 4096;
        const auto rowsWithin = [](std::size_t bytes) { return int(bytes / (4 * width)); };
        for (const bool mobile : {false, true}) {
            CAPTURE(mobile);
            const RenderBudget budget = mobile ? RenderBudget::mobile() : RenderBudget::desktop();
            RenderResources resources(budget); TextureDescriptor descriptor;
            // Each class refuses a texture one row past its own limit and admits one at it.
            for (const auto category : {BudgetClass::General, BudgetClass::SkinAtlas, BudgetClass::TerrainPage, BudgetClass::TerrainMask}) {
                CAPTURE(int(category));
                const auto limit = budget.classLimit(category);
                REQUIRE(limit % (4 * width) == 0); REQUIRE(limit <= budget.total);
                descriptor.budgetClass = category;
                {
                    auto over = surface(width, rowsWithin(limit) + 1); REQUIRE(over);
                    CHECK_FALSE(resources.create(over.get(), 1, descriptor));
                }
                auto fits = surface(width, rowsWithin(limit)); REQUIRE(fits);
                auto handle = resources.create(fits.get(), 1, descriptor); REQUIRE(handle);
                CHECK(resources.retainedBytes() == std::size_t(fits->pitch) * fits->h);
                CHECK(resources.erase(handle)); CHECK(resources.retainedBytes() == 0);
            }
            // Limits are independent: a texture past the skin atlas limit still
            // fits the larger terrain page class.
            REQUIRE(budget.classLimit(BudgetClass::SkinAtlas) < budget.classLimit(BudgetClass::TerrainPage));
            auto large = surface(width, rowsWithin(budget.classLimit(BudgetClass::SkinAtlas)) + 1); REQUIRE(large);
            descriptor.budgetClass = BudgetClass::SkinAtlas;
            CHECK_FALSE(resources.create(large.get(), 1, descriptor));
            descriptor.budgetClass = BudgetClass::TerrainPage;
            auto terrain = resources.create(large.get(), 1, descriptor); REQUIRE(terrain);
            CHECK(resources.retainedBytes() == std::size_t(large->pitch) * large->h);
            CHECK(resources.erase(terrain)); CHECK(resources.retainedBytes() == 0);
        }
        // An explicit total caps every class.
        auto small = surface(); RenderResources bounded(4); TextureDescriptor descriptor;
        descriptor.budgetClass = TextureDescriptor::BudgetClass::SkinAtlas;
        CHECK_FALSE(bounded.create(small.get(), 1, descriptor));
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
