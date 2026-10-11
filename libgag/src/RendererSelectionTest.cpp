// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <RendererSelection.h>
#include <GraphicContext.h>
#include <RenderBackend.h>
#include <RenderDevice.h>
#include <Toolkit.h>
#include <algorithm>

using namespace GAGCore;
namespace {
class AdmissionProbe final : public RenderDevice {
    std::unique_ptr<RenderDevice> owned;
public:
    bool reject = false;
    explicit AdmissionProbe(std::unique_ptr<RenderDevice> device):owned(std::move(device)){}
    RenderDeviceCapabilities capabilities() const override{return owned->capabilities();}
    bool canBeginFrame() override{return owned->canBeginFrame();}
    RenderSubmission submit(RenderFrame frame) override{if(reject){reject=false;throw RenderAdmissionDeferred("injected temporary upload pressure");}return owned->submit(std::move(frame));}
    RenderCompletion completion(RenderSubmission s) override{return owned->completion(s);}
    void present() override{owned->present();} void discardFrame() override{owned->discardFrame();}
    void reset() override{owned->reset();} std::uint64_t generation() const override{return owned->generation();}
    RenderTelemetry telemetry() const override{return owned->telemetry();}
    bool capture(CaptureRequest r) override{return owned->capture(std::move(r));}
    bool pollCapture(CaptureResult &r) override{return owned->pollCapture(r);}
    SDL_Surface *captureSynchronously(CaptureRequest r) override{return owned->captureSynchronously(std::move(r));}
    void cancelCaptures() override{owned->cancelCaptures();} void outputSize(int &w,int &h) override{owned->outputSize(w,h);}
};
class AdmissionContext final : public GraphicContext {
public:
    using GraphicContext::GraphicContext;
    AdmissionProbe &installProbe(){
        renderer->flush();deviceRenderer.reset();
        auto probe=std::make_unique<AdmissionProbe>(std::move(presentationDevice));auto *result=probe.get();
        presentationDevice=std::move(probe);
        deviceRenderer=makeDeviceRenderBackend(*presentationDevice,*deviceResources,getW(),getH());renderer=deviceRenderer.get();
        return *result;
    }
};
}
TEST_SUITE("RendererSelection")
{
    TEST_CASE("stable names aliases and startup precedence")
    {
        for (auto backend : RendererBackends) CHECK(parseRendererBackend(rendererBackendName(backend)) == backend);
        CHECK(parseRendererBackend("gpu") == RendererBackend::LegacyGL);
        CHECK(parseRendererBackend("sdl") == RendererBackend::PortableSDL);
        CHECK_FALSE(parseRendererBackend("unknown"));
        CHECK(resolveRendererPreference(RendererBackend::Software, {}, {}) == RendererBackend::Software);
        CHECK(resolveRendererPreference(RendererBackend::Software, {}, RendererBackend::PortableSDL) == RendererBackend::PortableSDL);
        CHECK(resolveRendererPreference(RendererBackend::Software, RendererBackend::Auto, RendererBackend::PortableSDL) == RendererBackend::Auto);
    }
    TEST_CASE("public device hold preserves graphics generation and offscreen captures [display]")
    {
        for (auto backend : {RendererBackend::ProgrammableGL, RendererBackend::SDLGPU}) {
            GraphicContext::setInitialRendererBackend(RendererBackend::Software);
            GraphicContext gfx(64,48,GraphicContext::RESIZABLE|GraphicContext::NOAUDIO,"offscreen progress hold");
            const auto choices=gfx.rendererAvailability();
            const auto available=std::find_if(choices.begin(),choices.end(),[&](const auto &choice){return choice.backend==backend && choice.available;});
            if(available==choices.end()){MESSAGE("Device hold coverage unavailable for ",rendererBackendName(backend));continue;}
            REQUIRE(gfx.requestBackendSwitch(backend));gfx.nextFrame();
            INFO(gfx.backendSwitchStatus().reason);
            REQUIRE(gfx.backendSwitchStatus().active==backend);
            auto *device=gfx.renderDevice();REQUIRE(device);
            auto *resources=gfx.renderResources();REQUIRE(resources);
            gfx.drawFilledRect(0,0,64,48,Color(190,20,30));gfx.nextFrame();
            const auto generation=gfx.getGLContextGeneration();
            const auto deviceGeneration=device->generation();
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(SDL_CreateSurface(4,4,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
            REQUIRE(pixels);
            TextureDescriptor descriptor;descriptor.budgetClass=TextureDescriptor::BudgetClass::TerrainPage;
            const auto handle=resources->create(pixels.get(),1,descriptor);REQUIRE(handle);
            const auto target=resources->snapshot(handle);
            REQUIRE(gfx.beginDeviceTarget(target,4,4));
            DrawPacket clear;clear.kind=DrawPacket::Kind::Clear;clear.clearColor=true;clear.clearValue={0,1,0,1};
            REQUIRE(gfx.submitDevicePacket(std::move(clear)));gfx.endDeviceTarget();
            REQUIRE(gfx.flushDeviceFrame());REQUIRE(gfx.deferDeviceFrame());
            CHECK_FALSE(gfx.flushDeviceFrame());gfx.nextFrame();
            CHECK(gfx.renderDevice()==device);CHECK(gfx.getGLContextGeneration()==generation);
            CHECK(device->generation()==deviceGeneration);
            CaptureRequest main;main.width=64;main.height=48;
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> captured(device->captureSynchronously(main),SDL_DestroySurface);
            REQUIRE(captured);Uint8 r,g,b,a;
            REQUIRE(SDL_ReadSurfacePixel(captured.get(),32,24,&r,&g,&b,&a));CHECK(r==190);CHECK(g==20);CHECK(b==30);
            CaptureRequest offscreen;offscreen.target=target;offscreen.width=offscreen.height=4;
            captured.reset(device->captureSynchronously(offscreen));REQUIRE(captured);
            REQUIRE(SDL_ReadSurfacePixel(captured.get(),2,2,&r,&g,&b,&a));CHECK(r==0);CHECK(g==255);CHECK(b==0);
            gfx.drawFilledRect(0,0,64,48,Color(30,40,170));REQUIRE(gfx.flushDeviceFrame());gfx.nextFrame();
            CHECK(gfx.getGLContextGeneration()==generation);captured.reset(device->captureSynchronously(main));REQUIRE(captured);
            REQUIRE(SDL_ReadSurfacePixel(captured.get(),32,24,&r,&g,&b,&a));CHECK(r==30);CHECK(g==40);CHECK(b==170);
            REQUIRE(resources->erase(handle));
        }
    }

    TEST_CASE("public admission abort retains target leases and advances content epoch [display]")
    {
        for(auto backend:{RendererBackend::ProgrammableGL,RendererBackend::SDLGPU}){
            GraphicContext::setInitialRendererBackend(RendererBackend::Software);
            AdmissionContext gfx(64,48,GraphicContext::NOAUDIO,"bounded upload retry");
            auto choices=gfx.rendererAvailability();
            if(std::none_of(choices.begin(),choices.end(),[&](const auto &c){return c.backend==backend&&c.available;}))continue;
            REQUIRE(gfx.requestBackendSwitch(backend));gfx.nextFrame();REQUIRE(gfx.backendSwitchStatus().active==backend);
            auto &probe=gfx.installProbe();auto *resources=gfx.renderResources();REQUIRE(resources);
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(SDL_CreateSurface(4,4,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
            REQUIRE(pixels);TextureDescriptor descriptor;descriptor.budgetClass=TextureDescriptor::BudgetClass::TerrainPage;
            const auto handle=resources->create(pixels.get(),1,descriptor);REQUIRE(handle);const auto target=resources->snapshot(handle);
            auto paintTarget=[&](SDL_FColor color){REQUIRE(gfx.beginDeviceTarget(target,4,4));DrawPacket p;p.kind=DrawPacket::Kind::Clear;p.clearColor=true;p.clearValue=color;const bool admitted=gfx.submitDevicePacket(std::move(p));gfx.endDeviceTarget();return admitted;};
            REQUIRE(paintTarget({0,1,0,1}));gfx.drawFilledRect(0,0,64,48,Color(180,20,30));gfx.nextFrame();
            const auto generation=gfx.getGLContextGeneration();const auto deviceGeneration=probe.generation();
            const auto epoch=gfx.renderTargetContentRevision();probe.reject=true;
            // Fill the real adapter to trigger its automatic 4096-command flush.
            bool admitted=true;for(unsigned i=0;i<4097 && admitted;++i){DrawPacket p;p.kind=DrawPacket::Kind::Barrier;admitted=gfx.submitDevicePacket(std::move(p));}
            CHECK_FALSE(admitted);CHECK_FALSE(gfx.flushDeviceFrame());gfx.nextFrame();
            CHECK(gfx.getGLContextGeneration()==generation);CHECK(probe.generation()==deviceGeneration);
            CHECK(gfx.renderTargetContentRevision()==epoch+1);CHECK(resources->snapshot(handle)==target);
            REQUIRE(paintTarget({0,0,1,1}));gfx.drawFilledRect(0,0,64,48,Color(20,170,40));gfx.nextFrame();
            CHECK(gfx.renderTargetContentRevision()==epoch+1);CHECK(resources->snapshot(handle)==target);
            CaptureRequest capture;capture.target=target;capture.width=capture.height=4;
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> restored(probe.captureSynchronously(capture),SDL_DestroySurface);
            REQUIRE(restored);Uint8 r,g,b,a;REQUIRE(SDL_ReadSurfacePixel(restored.get(),2,2,&r,&g,&b,&a));CHECK(r==0);CHECK(g==0);CHECK(b==255);CHECK(a==255);
            REQUIRE(resources->erase(handle));
        }
    }

    TEST_CASE("native switching preserves surfaces commits preferences and rejects unavailable choices [display]")
    {
        GraphicContext::setInitialRendererBackend(RendererBackend::Software);
        GraphicContext gfx(640, 480, GraphicContext::RESIZABLE | GraphicContext::NOAUDIO, "renderer switch");
        DrawableSurface texture(16, 16);
        texture.drawFilledRect(0, 0, 16, 16, Color(255, 0, 0));
        RendererBackend saved = RendererBackend::Auto;
        unsigned commits = 0;
        gfx.setBackendPreferenceCallback([&](auto backend) { saved = backend; ++commits; });
        for (auto backend : {RendererBackend::PortableSDL, RendererBackend::Software, RendererBackend::PortableSDL, RendererBackend::Software}) {
            gfx.drawFilledRect(0, 0, gfx.getW(), gfx.getH(), Color(0, 0, 0));
            gfx.drawSurface(20, 20, &texture);
            REQUIRE(gfx.requestBackendSwitch(backend));
            gfx.nextFrame();
            REQUIRE(gfx.backendSwitchStatus().phase == RendererSwitchPhase::Complete);
            REQUIRE(gfx.backendSwitchStatus().active == backend);
            CHECK(saved == backend);
            CHECK(gfx.getW() == 640);
            CHECK(gfx.getH() == 480);
            gfx.drawSurface(20, 20, &texture);
        }
        for (const auto &choice : gfx.rendererAvailability()) {
            if (!choice.available || choice.backend == RendererBackend::Auto || choice.backend == RendererBackend::WebGPU) continue;
            for (auto backend : {choice.backend, RendererBackend::Software}) {
                gfx.drawFilledRect(0,0,gfx.getW(),gfx.getH(),Color(0,0,0));
                gfx.drawSurface(20,20,&texture);
                REQUIRE(gfx.requestBackendSwitch(backend));
                gfx.nextFrame();
                INFO(gfx.backendSwitchStatus().reason);
                REQUIRE(gfx.backendSwitchStatus().phase == RendererSwitchPhase::Complete);
                REQUIRE(gfx.backendSwitchStatus().active == backend);
                gfx.drawSurface(20,20,&texture);
                const auto identity = gfx.renderingIdentity();
                CHECK(identity.backend == (backend == RendererBackend::LegacyGL ? "opengl" :
                    backend == RendererBackend::PortableSDL ? "sdl" : std::string(rendererBackendName(backend))));
                if (backend == RendererBackend::ProgrammableGL) CHECK(identity.glIdentityAvailable);
                CHECK(saved == backend);
            }
        }
        const auto beforeAuto = commits;
        REQUIRE(gfx.requestBackendSwitch(RendererBackend::Auto));
        const auto queuedSerial=gfx.backendSwitchStatus().serial;
        gfx.nextFrame();
        CHECK(gfx.backendSwitchStatus().serial>queuedSerial);
        CHECK(gfx.backendSwitchStatus().phase == RendererSwitchPhase::Complete);
        CHECK(gfx.backendSwitchStatus().active == RendererBackend::Software);
        CHECK(saved == RendererBackend::Auto);
        CHECK(commits == beforeAuto + 1);
        gfx.nextFrame();
        CHECK(commits == beforeAuto + 1);
        REQUIRE(gfx.requestBackendSwitch(RendererBackend::Software));gfx.nextFrame();
        const auto successfulCommits = commits;
        CHECK(commits >= 4);
        CHECK_FALSE(gfx.requestBackendSwitch(RendererBackend::WebGPU));
        CHECK(gfx.backendSwitchStatus().active == RendererBackend::Software);
        CHECK(saved == RendererBackend::Software);
        CHECK(commits == successfulCommits);
    }
}
