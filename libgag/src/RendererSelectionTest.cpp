// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <RendererSelection.h>
#include <GraphicContext.h>
#include <RenderBackend.h>
#include <Toolkit.h>

using namespace GAGCore;
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
