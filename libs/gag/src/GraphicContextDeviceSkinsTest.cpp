// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <GraphicContext.h>
#include <RenderDevice.h>
#include <SpriteDrawBatch.h>
#include <SkinMesh.h>
#include <SkinModel.h>
#include <Toolkit.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <memory>
using namespace GAGCore;
TEST_SUITE("GraphicContextDeviceSkins") {
    TEST_CASE("live skin adapters preserve assets and scoped transforms across backend switching [display]") {
        glob2test::ToolkitScope toolkit;
        GraphicContext::setInitialRendererBackend(RendererBackend::Software);
        auto *gfx=Toolkit::initGraphic(640,480,GraphicContext::NOAUDIO,"shared skin adapters");
        const auto fixture=nlohmann::json::parse(glob2test::readFile(glob2test::sourceRoot()/"test/fixtures/skins/rig.json"));
        const auto hex=fixture["hex"].get<std::string>(); std::vector<std::uint8_t> bytes;
        for(std::size_t i=0;i<hex.size();i+=2) bytes.push_back(std::stoul(hex.substr(i,2),nullptr,16));
        std::string error;const auto model=SkinModel::decode(bytes,error);REQUIRE_MESSAGE(model,error);
        const auto mesh=SkinMesh::fromModel(model,0);
        auto baked=SkinMesh::fromModel(model,0);baked.model.reset();
        for(unsigned frame=0;frame<SkinClipFrames;++frame) {
            std::vector<float> pose;REQUIRE(model->evaluate(0,frame,pose));
            baked.poses.insert(baked.poses.end(),pose.begin(),pose.end());
        }
        DrawableSurface paint(512,512),material(512,512);
        paint.drawFilledRect(0,0,512,512,Color(220,160,80));
        material.drawFilledRect(0,0,512,512,Color(1,1,1));
        std::vector<RendererBackend> families;
        for(const auto &choice:gfx->rendererAvailability()) if(choice.available &&
            (choice.backend==RendererBackend::ProgrammableGL || choice.backend==RendererBackend::SDLGPU)) families.push_back(choice.backend);
        if(families.empty()) { WARN("No programmable device available in this build");return; }
        for(auto backend:families) {
            INFO("backend=" << int(backend));
            REQUIRE(gfx->requestBackendSwitch(backend));gfx->nextFrame();
            INFO(gfx->backendSwitchStatus().reason);REQUIRE(gfx->backendSwitchStatus().phase==RendererSwitchPhase::Complete);
            REQUIRE(gfx->rendererCapabilities().liveSkins);
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> exportPixels(
                SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
            REQUIRE(exportPixels);
            gfx->drawToSurface(exportPixels.get(), 1, [&] {
                CHECK_FALSE(gfx->rendererCapabilities().liveSkins);
                CHECK_FALSE(gfx->rendererCapabilities().torus);
                CHECK(gfx->renderDevice() == nullptr);
                CHECK_FALSE(gfx->drawSkinMesh(mesh, 0, paint, material, 0, 0, 0, 32, 32));
                gfx->drawFilledRect(0, 0, 32, 32, Color(15, 25, 35));
            });
            CHECK(gfx->rendererCapabilities().liveSkins);
            CHECK(gfx->renderDevice() != nullptr);
            unsigned visibleFrames=0;
            for(unsigned frame:{0u,17u,86u}) {
                INFO("skin frame=" << frame);
                gfx->beginMapTransform(1.5f,7,9,0,0,320,240);
                gfx->prepareSkinMeshes({{&mesh,frame,&paint,&material,0}});
                gfx->endMapTransform();
                std::vector<std::uint8_t> rgba;REQUIRE(gfx->readSkinMesh({&mesh,frame,&paint,&material,0},rgba));
                REQUIRE(rgba.size()==128*128*4);
                unsigned covered=0;for(std::size_t i=3;i<rgba.size();i+=4) covered+=rgba[i]!=0;
                // The analytic fixture folds to subpixel geometry at some headings.
                visibleFrames+=covered>0;
                std::vector<std::uint8_t> reference;REQUIRE(gfx->readSkinMesh({&baked,frame,&paint,&material,0},reference));
                REQUIRE(reference.size()==rgba.size());
                unsigned maximum=0,differences=0;
                for(std::size_t i=0;i<rgba.size();++i) {
                    const unsigned delta=std::abs(int(rgba[i])-int(reference[i]));
                    maximum=std::max(maximum,delta);differences+=delta!=0;
                }
                CHECK(maximum<=2);CHECK(differences<=64);
                REQUIRE(gfx->drawSkinMesh(mesh,frame,paint,material,0,20,20,64,64,{},173));
                gfx->nextFrame();
            }
            CHECK(visibleFrames>=2);
            // Both the source mutation and subsequent switch must invalidate
            // cached atlas pixels without recreating the logical appearance.
            paint.drawFilledRect(0,0,512,512,Color(40,180,90));
            std::vector<std::uint8_t> changed;REQUIRE(gfx->readSkinMesh({&mesh,0,&paint,&material,0},changed));
            REQUIRE(gfx->requestBackendSwitch(RendererBackend::Software));gfx->nextFrame();
            REQUIRE(gfx->backendSwitchStatus().active==RendererBackend::Software);
            REQUIRE(gfx->requestBackendSwitch(backend));gfx->nextFrame();
            std::vector<std::uint8_t> restored;REQUIRE(gfx->readSkinMesh({&mesh,0,&paint,&material,0},restored));
            CHECK(restored==changed);
            gfx->nextFrame();
        }
    }
}

TEST_SUITE("RendererAssetLifetime") {
    TEST_CASE("deleted drawables retire device identities even during CPU exports [display]") {
        glob2test::ToolkitScope toolkit;
        GraphicContext::setInitialRendererBackend(RendererBackend::Software);
        auto *gfx=Toolkit::initGraphic(640,480,GraphicContext::NOAUDIO,"drawable lifetime");
        for(const auto &choice:gfx->rendererAvailability()) {
            if(!choice.available || (choice.backend!=RendererBackend::ProgrammableGL && choice.backend!=RendererBackend::SDLGPU))continue;
            REQUIRE(gfx->requestBackendSwitch(choice.backend));gfx->nextFrame();
            REQUIRE(gfx->backendSwitchStatus().phase==RendererSwitchPhase::Complete);
            gfx->drawFilledRect(0,0,640,480,Color(0,0,0));
            alignas(DrawableSurface) std::array<std::byte,sizeof(DrawableSurface)> storage;
            auto *source=std::construct_at(reinterpret_cast<DrawableSurface*>(storage.data()),8,8);
            source->drawFilledRect(0,0,8,8,Color(255,0,0));
            gfx->drawSurface(10,0,source);
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> exported(SDL_CreateSurface(8,8,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
            REQUIRE(exported);
            gfx->drawToSurface(exported.get(),1,[&]{std::destroy_at(source);});
            source=std::construct_at(reinterpret_cast<DrawableSurface*>(storage.data()),8,8);
            source->drawFilledRect(0,0,8,8,Color(0,255,0));
            gfx->drawSurface(20,0,source);
            std::destroy_at(source);
            source=std::construct_at(reinterpret_cast<DrawableSurface*>(storage.data()),8,8);
            source->drawFilledRect(0,0,8,8,Color(0,0,255));
            Sprite sprite;
            {
                SpriteDrawBatch batch(gfx,&sprite);
                gfx->drawSurface(30,0,source);
                source->drawFilledRect(0,0,8,8,Color(255,255,0));
                gfx->drawSurface(40,0,source);
                std::destroy_at(source);
            }
            gfx->nextFrame();
            CaptureRequest request;request.width=640;request.height=480;
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(gfx->renderDevice()->captureSynchronously(request),SDL_DestroySurface);
            REQUIRE(pixels);
            Uint8 r,g,b,a;
            REQUIRE(SDL_ReadSurfacePixel(pixels.get(),12,4,&r,&g,&b,&a));CHECK(r==255);CHECK(g==0);
            REQUIRE(SDL_ReadSurfacePixel(pixels.get(),22,4,&r,&g,&b,&a));CHECK(r==0);CHECK(g==255);
            REQUIRE(SDL_ReadSurfacePixel(pixels.get(),32,4,&r,&g,&b,&a));CHECK(r==0);CHECK(b==255);
            REQUIRE(SDL_ReadSurfacePixel(pixels.get(),42,4,&r,&g,&b,&a));CHECK(r==255);CHECK(g==255);CHECK(b==0);
            REQUIRE(gfx->requestBackendSwitch(RendererBackend::Software));gfx->nextFrame();
        }
    }

    TEST_CASE("device-released pixels draw from retained snapshots across resets until a switch [display]") {
        glob2test::ToolkitScope toolkit;
        GraphicContext::setInitialRendererBackend(RendererBackend::Software);
        auto *gfx=Toolkit::initGraphic(640,480,GraphicContext::NOAUDIO,"device pixel release");
        unsigned tested=0;
        for(const auto &choice:gfx->rendererAvailability()) {
            if(!choice.available || (choice.backend!=RendererBackend::ProgrammableGL && choice.backend!=RendererBackend::SDLGPU))continue;
            INFO("backend=" << int(choice.backend));
            REQUIRE(gfx->requestBackendSwitch(choice.backend));gfx->nextFrame();
            REQUIRE(gfx->backendSwitchStatus().phase==RendererSwitchPhase::Complete);
            DrawableSurface source(8,8);
            source.drawFilledRect(0,0,8,8,Color(255,0,0));
            CHECK_FALSE(source.releaseDevicePixels()); // Nothing uploaded yet.
            gfx->drawFilledRect(0,0,640,480,Color(0,0,0));
            gfx->drawSurface(10,0,&source);
            REQUIRE(source.releaseDevicePixels());
            CHECK_FALSE(source.hasPixels());
            CHECK(source.deviceReleased());
            CHECK(source.deviceBacked());
            CHECK(source.getW()==8);CHECK(source.getH()==8);
            CHECK(gfx->retainedDeviceSnapshotBytes()>=8*8*4);
            const auto drawnRed=[&](int x) {
                gfx->drawFilledRect(0,0,640,480,Color(0,0,0));
                gfx->drawSurface(x,0,&source);
                gfx->nextFrame();
                CaptureRequest request;request.width=640;request.height=480;
                std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(gfx->renderDevice()->captureSynchronously(request),SDL_DestroySurface);
                REQUIRE(pixels);
                Uint8 r,g,b,a;
                REQUIRE(SDL_ReadSurfacePixel(pixels.get(),x+4,4,&r,&g,&b,&a));
                CHECK(r==255);CHECK(g==0);CHECK(b==0);
            };
            drawnRed(20);
            // A device reset keeps the adapter's snapshots: the surface still draws.
            SDL_Event reset{};reset.type=SDL_EVENT_RENDER_DEVICE_RESET;
            GraphicContext::translateMouseEvent(&reset);
            CHECK(source.deviceBacked());
            drawnRed(30);
            // A backend switch takes the snapshots: owners must reload.
            REQUIRE(gfx->requestBackendSwitch(RendererBackend::Software));gfx->nextFrame();
            CHECK_FALSE(source.deviceBacked());
            CHECK(gfx->retainedDeviceSnapshotBytes()==0);
            ++tested;
        }
        if(!tested) WARN("No programmable device available in this build");
    }

    TEST_CASE("an offscreen pass draws a drawable rebuilt at a freed drawable's address [display]") {
        // Whole-map renders stream one terrain page at a time, each allocated where the last was freed.
        glob2test::ToolkitScope toolkit;
        GraphicContext::setInitialRendererBackend(RendererBackend::Software);
        auto *gfx=Toolkit::initGraphic(640,480,GraphicContext::NOAUDIO,"offscreen drawable reuse");
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> canvas(SDL_CreateSurface(16,8,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
        REQUIRE(canvas);
        alignas(DrawableSurface) std::array<std::byte,sizeof(DrawableSurface)> storage;
        gfx->drawToSurface(canvas.get(),1,[&]{
            for(int page=0;page<2;++page) {
                auto *source=std::construct_at(reinterpret_cast<DrawableSurface*>(storage.data()),8,8);
                source->drawFilledRect(0,0,8,8,page==0 ? Color(255,0,0) : Color(0,255,0));
                gfx->drawSurface(page*8,0,source);
                std::destroy_at(source);
            }
        });
        Uint8 r,g,b,a;
        REQUIRE(SDL_ReadSurfacePixel(canvas.get(),4,4,&r,&g,&b,&a));CHECK(r==255);CHECK(g==0);
        REQUIRE(SDL_ReadSurfacePixel(canvas.get(),12,4,&r,&g,&b,&a));CHECK(r==0);CHECK(g==255);
    }
}

namespace {
// A building-like (rotationMap) or unit-like (dynamicTeamColor cache) frame
// whose team layer holds many hues, greys and partial alpha.
struct TeamLayerSprite : Sprite {
    explicit TeamLayerSprite(bool dynamic) {
        dynamicTeamColor = dynamic;
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> base(SDL_CreateSurface(16,16,SDL_PIXELFORMAT_ARGB8888),SDL_DestroySurface),
            team(SDL_CreateSurface(16,16,SDL_PIXELFORMAT_ARGB8888),SDL_DestroySurface);
        REQUIRE(base); REQUIRE(team);
        for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
            REQUIRE(SDL_WriteSurfacePixel(base.get(),x,y,Uint8(90+x*4),Uint8(60+y*5),30,Uint8(x<2?0:255)));
            const Uint8 alpha=Uint8(y<4?0:y<8?128+x*8:255);
            REQUIRE(SDL_WriteSurfacePixel(team.get(),x,y,Uint8(x*17),Uint8(255-y*9),Uint8((x*y*7)&255),alpha));
        }
        images.push_back(new DrawableSurface(base.get()));
        rotated.push_back(new RotatedImage(new DrawableSurface(team.get())));
        experimentImages.push_back(nullptr); experimentRotated.push_back(nullptr);
    }
    bool coloured() const { return !rotated[0]->rotationMap.empty() || getTeamColorCacheEntries()!=0; }
    // The CPU path: the base layer, then the team layer's recoloured copy.
    void drawCPU(GraphicContext *gfx, float x, float y, float w, float h, Uint8 alpha=255) {
        gfx->drawSurface(x,y,w,h,images[0],alpha); gfx->drawSurface(x,y,w,h,getRotatedSurface(0),alpha);
    }
};
}

TEST_SUITE("DeviceTeamColor") {
    TEST_CASE("device team layers recolour on the GPU without CPU copies and match them [display]") {
        glob2test::ToolkitScope toolkit;
        GraphicContext::setInitialRendererBackend(RendererBackend::Software);
        constexpr int Size=128;
        auto *gfx=Toolkit::initGraphic(Size,Size,GraphicContext::NOAUDIO,"device team colour");
        unsigned tested=0;
        for(const auto &choice:gfx->rendererAvailability()) {
            if(!choice.available || (choice.backend!=RendererBackend::ProgrammableGL && choice.backend!=RendererBackend::SDLGPU))continue;
            INFO("backend=" << int(choice.backend));
            REQUIRE(gfx->requestBackendSwitch(choice.backend));gfx->nextFrame();
            REQUIRE(gfx->backendSwitchStatus().phase==RendererSwitchPhase::Complete);
            if(!gfx->hasDeviceTeamColor()) { WARN("Device shaders lack the team-colour material");continue; }
            auto capture=[&]{
                gfx->nextFrame();
                CaptureRequest request;request.width=Size;request.height=Size;
                return std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)>(gfx->renderDevice()->captureSynchronously(request),SDL_DestroySurface);
            };
            for(bool dynamic:{false,true}) {
                INFO("dynamic=" << dynamic);
                TeamLayerSprite sprite(dynamic);sprite.setBaseColor(Color(40,90,230));
                gfx->drawFilledRect(0,0,Size,Size,Color(0,0,0));
                gfx->drawSprite(8,8,&sprite,0);
                gfx->drawSprite(32,8,32,32,&sprite,0);
                gfx->drawSprite(70.f,8.f,48.f,48.f,&sprite,0,160);
                // A swimmer's radial fade draws the team layer as a faded grid.
                const RadialFade fade{40,90,4,20,0.2f};
                gfx->setRadialFade(fade);gfx->drawSprite(24.f,74.f,32.f,32.f,&sprite,0);gfx->setRadialFade({});
                auto shader=capture();REQUIRE(shader);
                CHECK_FALSE(sprite.coloured());
                gfx->drawFilledRect(0,0,Size,Size,Color(0,0,0));
                sprite.drawCPU(gfx,8,8,16,16);sprite.drawCPU(gfx,32,8,32,32);
                sprite.drawCPU(gfx,70,8,48,48,160);
                gfx->setRadialFade(fade);sprite.drawCPU(gfx,24,74,32,32);gfx->setRadialFade({});
                auto cpu=capture();REQUIRE(cpu);
                CHECK(sprite.coloured());
                int maximum=0;unsigned differing=0;
                for(int y=0;y<Size;++y) for(int x=0;x<Size;++x) {
                    Uint8 a[4],b[4];
                    REQUIRE(SDL_ReadSurfacePixel(shader.get(),x,y,&a[0],&a[1],&a[2],&a[3]));
                    REQUIRE(SDL_ReadSurfacePixel(cpu.get(),x,y,&b[0],&b[1],&b[2],&b[3]));
                    for(int c=0;c<4;++c){const int delta=std::abs(int(a[c])-int(b[c]));maximum=std::max(maximum,delta);differing+=delta!=0;}
                }
                MESSAGE("backend " << int(choice.backend) << " dynamic " << dynamic << ": " << differing << " channels differ, max " << maximum);
                // SDL GPU follows the CPU's float operations exactly (lavapipe: no
                // channel differs). Mesa's GL compiler may still fuse or reorder a
                // float operation, moving a truncated channel by one level.
                if(choice.backend==RendererBackend::SDLGPU) CHECK(differing==0);
                CHECK(maximum<=1);
            }
            ++tested;
            REQUIRE(gfx->requestBackendSwitch(RendererBackend::Software));gfx->nextFrame();
        }
        if(!tested) WARN("No device renderer with the team-colour material in this build");
    }
}

TEST_SUITE("PresentationTrim") {
    TEST_CASE("low-memory trims free idle device snapshots over frames and keep drawing [display]") {
        glob2test::ToolkitScope toolkit;
        GraphicContext::setInitialRendererBackend(RendererBackend::Software);
        auto *gfx=Toolkit::initGraphic(640,480,GraphicContext::NOAUDIO,"presentation trim");
        // Nothing retired: an immediate trim of retired HD frames returns at once.
        Sprite::trimRetiredHighResolution(true);
        unsigned tested=0;
        for(const auto &choice:gfx->rendererAvailability()) {
            if(!choice.available || (choice.backend!=RendererBackend::ProgrammableGL && choice.backend!=RendererBackend::SDLGPU))continue;
            INFO("backend=" << int(choice.backend));
            REQUIRE(gfx->requestBackendSwitch(choice.backend));gfx->nextFrame();
            REQUIRE(gfx->backendSwitchStatus().phase==RendererSwitchPhase::Complete);
            auto *resources=gfx->renderResources();REQUIRE(resources);
            std::vector<std::unique_ptr<DrawableSurface>> idle;
            for(int i=0;i<100;++i) {
                idle.push_back(std::make_unique<DrawableSurface>(8,8));
                idle.back()->drawFilledRect(0,0,8,8,Color(0,0,255));
                gfx->drawSurface(i%64*8,i/64*8+100,idle.back().get());
            }
            DrawableSurface view(8,8);view.drawFilledRect(0,0,8,8,Color(255,0,0));
            const auto drawView=[&]{
                gfx->drawFilledRect(0,0,640,480,Color(0,0,0));
                gfx->drawSurface(10,0,&view);
                gfx->nextFrame();
            };
            for(int frame=0;frame<4;++frame) drawView();
            const auto before=resources->retainedBytes();
            REQUIRE(before>=101*8*8*4);
            gfx->trimPresentationMemory(PresentationTrim::LowMemory);
            CHECK(gfx->presentationTrimPending());
            // Bounded: one step frees at most 64 snapshots.
            gfx->servicePresentationTrim();
            CHECK(resources->retainedBytes()>=before-64*8*8*4);
            CHECK(resources->retainedBytes()<before);
            for(int frame=0;frame<8 && gfx->presentationTrimPending();++frame) { drawView(); gfx->servicePresentationTrim(); }
            CHECK_FALSE(gfx->presentationTrimPending());
            CHECK(resources->retainedBytes()<=before-100*8*8*4);
            CHECK(resources->retainedBytes()>=8*8*4); // The view's own texture stays.
            drawView();
            CaptureRequest request;request.width=640;request.height=480;
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(gfx->renderDevice()->captureSynchronously(request),SDL_DestroySurface);
            REQUIRE(pixels);
            Uint8 r,g,b,a;
            REQUIRE(SDL_ReadSurfacePixel(pixels.get(),14,4,&r,&g,&b,&a));CHECK(r==255);CHECK(g==0);
            // Trimmed snapshots come back from their owners' pixels.
            gfx->drawFilledRect(0,0,640,480,Color(0,0,0));
            gfx->drawSurface(20,0,idle.front().get());
            gfx->nextFrame();
            pixels.reset(gfx->renderDevice()->captureSynchronously(request));REQUIRE(pixels);
            REQUIRE(SDL_ReadSurfacePixel(pixels.get(),24,4,&r,&g,&b,&a));CHECK(b==255);CHECK(r==0);
            // Backgrounded, everything restorable goes at once.
            gfx->trimPresentationMemory(PresentationTrim::Background);
            CHECK_FALSE(gfx->presentationTrimPending());
            CHECK(resources->retainedBytes()==0);
            idle.clear();
            REQUIRE(gfx->requestBackendSwitch(RendererBackend::Software));gfx->nextFrame();
            ++tested;
        }
        if(!tested) WARN("No programmable device available in this build");
    }
}
