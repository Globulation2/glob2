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
