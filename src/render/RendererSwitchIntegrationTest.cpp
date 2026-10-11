// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Glob2Test.h"
#include <GraphicContext.h>
#include <RendererSelection.h>
#include <RenderDevice.h>
#include <TorusView.h>
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <vector>

using namespace GAGCore;
TEST_SUITE("RendererSwitchIntegration")
{
    GLOB2_TEST_CASE("all ordered gameplay and torus backend pairs preserve camera and simulation", "[display][isolated][writes-preferences][slow]")
    {
        glob2test::HeadlessGlobals globals({.display=true, .loadStrings=true, .width=640, .height=480,
            .beforeLoad=[](GlobalContainer &container) { container.settings.rendererBackend = RendererBackend::Software; }});
        auto *gfx = globals->gfx;
        std::vector<RendererBackend> backends{RendererBackend::Software};
        for (const auto &choice : gfx->rendererAvailability())
            if (choice.available && choice.backend != RendererBackend::Auto && choice.backend != RendererBackend::WebGPU &&
                choice.backend != RendererBackend::Software) backends.push_back(choice.backend);
        struct Pair {RendererBackend from,to; bool torus;};
        std::vector<Pair> schedule;
        // Two visits to every ordered pair exercise restoration and retired
        // resources repeatedly, including no-op requests and capability fallback.
        for (unsigned repeat=0;repeat<2;++repeat)
            for (bool torus : {false,true})
                for (auto from : backends) for (auto to : backends) schedule.push_back({from,to,torus});
        std::vector<Uint32> reference;
        std::array<std::set<std::pair<RendererBackend,RendererBackend>>,2> visited;
        std::map<RendererBackend,std::size_t> deviceHighWater;
        unsigned torusDraws=0;
        bool torusAvailable=false;
        for (bool switching : {false, true}) {
            glob2test::HeadlessGame fixture({.wDec=5, .hDec=5, .discovered=true, .seed=1024});
            fixture.gui.init(); fixture.addUnit(WORKER, 12, 12);
            auto &gui = fixture.gui;
            TorusView view;
            for (std::size_t tick=0;tick<schedule.size();++tick) {
                const auto pair=schedule[tick];
                INFO("pair ",rendererBackendName(pair.from)," -> ",rendererBackendName(pair.to)," torus=",pair.torus," tick=",tick);
                fixture.step();
                const auto before = fixture.checksum();
                const int cameraX = gui.getViewportX(), cameraY = gui.getViewportY();
                auto unchanged=[&] {
                    CHECK(fixture.checksum()==before);
                    CHECK(gui.getViewportX()==cameraX); CHECK(gui.getViewportY()==cameraY);
                };
                auto bounded=[&] {
                    if (auto *device=gfx->renderDevice()) {
                        const auto bytes=device->telemetry().resourceBytes;
                        CHECK(bytes<=256u*1024u*1024u);
                        auto &maximum=deviceHighWater[gfx->backendSwitchStatus().active]; maximum=std::max(maximum,bytes);
                    }
                    if (auto *resources=gfx->renderResources()) {
                        CHECK(resources->retainedBytes()<=256u*1024u*1024u);
                        CHECK(resources->retainedGeometryBytes()<=64u*1024u*1024u);
                    }
                };
                auto paint=[&] {
                    glob2test::drawGUI(gui,0);
                    if (!switching || !pair.torus) { view.reset(); return; }
                    const bool supported=gfx->rendererCapabilities().torus;
                    torusAvailable|=supported;
                    if (supported && !view.enabled()) view.toggle();
                    int vx=cameraX,vy=cameraY;
                    const bool drawn=view.draw(glob2test::sceneOf(fixture.game),&gui,0,0,vx,vy,gfx->getW(),gfx->getH());
                    CHECK(drawn==supported);
                    if (drawn) ++torusDraws;
                    unchanged();
                };
                auto change=[&](RendererBackend backend) {
                    REQUIRE(gfx->requestBackendSwitch(backend));
                    const auto deadline=SDL_GetTicks()+30000;
                    // An intentional torus capture hold retains the completed
                    // main image. Switching waits for a coherent capture frame,
                    // so continue presentation without stepping the simulation.
                    for(;;) {
                        gfx->nextFrame();unchanged();bounded();
                        const auto status=gfx->backendSwitchStatus();
                        INFO(status.reason);
                        if(status.phase==RendererSwitchPhase::Complete)break;
                        REQUIRE(status.phase!=RendererSwitchPhase::Failed);
                        REQUIRE(SDL_GetTicks()<deadline);
                        if(status.phase==RendererSwitchPhase::Queued)paint();
                        SDL_Delay(1);
                    }
                    INFO(gfx->backendSwitchStatus().reason);
                    REQUIRE(gfx->backendSwitchStatus().phase==RendererSwitchPhase::Complete);
                    REQUIRE(gfx->backendSwitchStatus().active==backend);
                    unchanged(); bounded();
                };
                if (switching && gfx->backendSwitchStatus().active!=pair.from) {
                    glob2test::drawGUI(gui,0); change(pair.from);
                }
                paint();
                if (switching) {
                    change(pair.to);
                    // Draw the retained scene again after commit, including a
                    // fresh torus activation when the source could not draw it.
                    paint(); gfx->nextFrame(); bounded();
                    visited[pair.torus].insert({pair.from,pair.to});
                } else gfx->nextFrame();
                unchanged();
                if (switching) CHECK(before==reference[tick]); else reference.push_back(before);
            }
        }
        CHECK(visited[0].size()==backends.size()*backends.size());
        CHECK(visited[1].size()==backends.size()*backends.size());
        if (torusAvailable) CHECK(torusDraws>0);
    }
}
