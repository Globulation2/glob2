// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Engine.h"
#include "GlobalContainer.h"
#include <SDL3_net/SDL_net.h>
#include <cstdio>
#include <stdexcept>
#include <filesystem>

TEST_SUITE("PortableGame")
{
	TEST_CASE("the portable renderer loads and draws a complete 50-tick game scene")
	{
		glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display = true, .loadStrings = true, .width = 800, .height = 600, .screenFlags = GAGCore::GraphicContext::PORTABLEGPU});
		REQUIRE(NET_Init());
		globalContainer->automaticEndingGame=true;
		globalContainer->automaticEndingSteps=50;
		globalContainer->automaticGameGlobalEndConditions=true;
		{
		    Engine engine;
		    if(engine.initCampaign("maps/balanced.map")!=Engine::EE_NO_ERROR) throw std::runtime_error("Fixture load failed");
		    Uint64 now=1000;
		    engine.beginSession(now);
		    unsigned frames=0;
		    while(engine.stepSession(now,{})) {
		        if(++frames>1000) throw std::runtime_error("Fixture stalled");
		        if(frames==40) globalContainer->gfx->printScreen("portable-scene.bmp");
		        engine.drawSession();
		        now+=40;
		    }
		    if(!std::filesystem::exists(std::filesystem::path(SDL_getenv("GLOB2_USER_DATA_DIR"))/"portable-scene.bmp"))
		        throw std::runtime_error("Scene screenshot was not produced");
		    engine.finishSession();
		}
		NET_Quit();
	}
}
