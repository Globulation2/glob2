// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <RendererSelection.h>
#include <GraphicContext.h>
#include <RenderBackend.h>
#include <RenderDevice.h>
#include <Toolkit.h>
#include <algorithm>
#include <vector>
#include <string>

using namespace GAGCore;
namespace {
class AdmissionProbe final : public RenderDevice {
    std::unique_ptr<RenderDevice> owned;
public:
    bool reject = false;
    bool held = false;
    std::vector<bool> presents; // Whether each present() was held.
    explicit AdmissionProbe(std::unique_ptr<RenderDevice> device):owned(std::move(device)){}
    RenderDeviceCapabilities capabilities() const override{return owned->capabilities();}
    bool canBeginFrame() override{return owned->canBeginFrame();}
    RenderSubmission submit(RenderFrame frame) override{if(reject){reject=false;throw RenderAdmissionDeferred("injected temporary upload pressure");}return owned->submit(std::move(frame));}
    RenderCompletion completion(RenderSubmission s) override{return owned->completion(s);}
    void present() override{presents.push_back(held);owned->present();} void discardFrame() override{owned->discardFrame();}
    void setPresentationHeld(bool h) override{held=h;owned->setPresentationHeld(h);}
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
            // The device draws at the window's pixel size (twice 64x48 on Retina).
            int outputW=0,outputH=0;device->outputSize(outputW,outputH);REQUIRE(outputW>0);
            CaptureRequest main;main.width=outputW;main.height=outputH;
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> captured(device->captureSynchronously(main),SDL_DestroySurface);
            REQUIRE(captured);Uint8 r,g,b,a;
            REQUIRE(SDL_ReadSurfacePixel(captured.get(),outputW/2,outputH/2,&r,&g,&b,&a));CHECK(r==190);CHECK(g==20);CHECK(b==30);
            CaptureRequest offscreen;offscreen.target=target;offscreen.width=offscreen.height=4;
            captured.reset(device->captureSynchronously(offscreen));REQUIRE(captured);
            REQUIRE(SDL_ReadSurfacePixel(captured.get(),2,2,&r,&g,&b,&a));CHECK(r==0);CHECK(g==255);CHECK(b==0);
            gfx.drawFilledRect(0,0,64,48,Color(30,40,170));REQUIRE(gfx.flushDeviceFrame());gfx.nextFrame();
            CHECK(gfx.getGLContextGeneration()==generation);captured.reset(device->captureSynchronously(main));REQUIRE(captured);
            REQUIRE(SDL_ReadSurfacePixel(captured.get(),outputW/2,outputH/2,&r,&g,&b,&a));CHECK(r==30);CHECK(g==40);CHECK(b==170);
            REQUIRE(resources->erase(handle));
        }
    }

    // Startup draws the first screen's first frames without showing them, so
    // every renderer settles before the player sees it (Application). Held
    // frames are still drawn, submitted and completed; the hold ends by itself.
    TEST_CASE("held presentation draws and completes frames without showing them, then presents [display]")
    {
        for (auto backend : {RendererBackend::ProgrammableGL, RendererBackend::SDLGPU}) {
            GraphicContext::setInitialRendererBackend(RendererBackend::Software);
            AdmissionContext gfx(64,48,GraphicContext::RESIZABLE|GraphicContext::NOAUDIO,"held presentation");
            const auto choices=gfx.rendererAvailability();
            if(std::none_of(choices.begin(),choices.end(),[&](const auto &c){return c.backend==backend && c.available;})){
                MESSAGE("Held presentation coverage unavailable for ",rendererBackendName(backend));continue;}
            REQUIRE(gfx.requestBackendSwitch(backend));gfx.nextFrame();
            REQUIRE(gfx.backendSwitchStatus().active==backend);
            auto &probe=gfx.installProbe();
            // As a screen draws: a frame begins once the device admits it.
            const auto draw=[&](Color color){
                for(int attempt=0;attempt<2000 && !gfx.beginRenderFrame();++attempt) SDL_Delay(1);
                gfx.drawFilledRect(0,0,64,48,color);gfx.nextFrame();
            };
            gfx.holdPresentation(3);
            CHECK(gfx.heldPresentationFrames()==3);
            for(int frame=0;frame<5;++frame) draw(Color(Uint8(40*frame),90,140));
            INFO(rendererBackendName(backend));
            std::string sequence;for(bool h:probe.presents)sequence+=h?'H':'P';
            INFO("presents ",sequence);
            INFO("active ",rendererBackendName(gfx.backendSwitchStatus().active)," reason ",gfx.backendSwitchStatus().reason);
            CHECK(probe.presents==std::vector<bool>{true,true,true,false,false});
            CHECK_FALSE(probe.held); // Released after the held frames.
            CHECK(gfx.heldPresentationFrames()==0);
            // A held frame is drawn in full: its pixels are the device's output.
            gfx.holdPresentation(1);
            draw(Color(200,30,60));
            // The device draws at the window's pixel size (twice 64x48 on Retina).
            int w=0,h=0;gfx.renderDevice()->outputSize(w,h);REQUIRE(w>0);
            CaptureRequest main;main.width=w;main.height=h;
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> captured(gfx.renderDevice()->captureSynchronously(main),SDL_DestroySurface);
            REQUIRE(captured);Uint8 r,g,b,a;
            REQUIRE(SDL_ReadSurfacePixel(captured.get(),w/2,h/2,&r,&g,&b,&a));CHECK(r==200);CHECK(g==30);CHECK(b==60);
        }
    }

    // On HiDPI displays a window's pixels exceed its points. Device renderers
    // have no GL context to measure them through, and SDL GPU once drew a
    // half-resolution frame that the swapchain stretched. On a 1x display both
    // sizes agree, so this guards HiDPI machines (Retina Macs, phones).
    TEST_CASE("device renderers draw at the window's pixel size, from startup and after a switch [display]")
    {
        for (auto backend : {RendererBackend::ProgrammableGL, RendererBackend::SDLGPU}) {
            for (bool atStartup : {true, false}) {
                GraphicContext::setInitialRendererBackend(atStartup ? backend : RendererBackend::Software);
                GraphicContext gfx(64, 48, GraphicContext::NOAUDIO, "device pixel size");
                if (!atStartup) {
                    const auto choices = gfx.rendererAvailability();
                    if (std::none_of(choices.begin(), choices.end(), [&](const auto &c) { return c.backend == backend && c.available; })) {
                        MESSAGE("Pixel size coverage unavailable for ", rendererBackendName(backend));
                        continue;
                    }
                    REQUIRE(gfx.requestBackendSwitch(backend)); gfx.nextFrame();
                }
                if (gfx.backendSwitchStatus().active != backend) {
                    MESSAGE("Pixel size coverage unavailable for ", rendererBackendName(backend));
                    continue;
                }
                auto *device = gfx.renderDevice(); REQUIRE(device);
                int pixelsW = 0, pixelsH = 0, deviceW = 0, deviceH = 0;
                REQUIRE(SDL_GetWindowSizeInPixels(SDL_GetWindowFromID(gfx.windowID()), &pixelsW, &pixelsH));
                device->outputSize(deviceW, deviceH);
                INFO(rendererBackendName(backend), atStartup ? " at startup" : " after a switch");
                CHECK(deviceW == pixelsW); CHECK(deviceH == pixelsH);
            }
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
