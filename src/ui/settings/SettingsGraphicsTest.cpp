// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Settings.h"
#include "HandPreferenceScreen.h"
#include "CommandLine.h"
#include "ScopedEnvironment.h"
#include "ui/FrontendUI.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <fstream>
#include <array>

namespace
{
const std::array<bool Settings::*, 8> effects = {
	&Settings::clouds, &Settings::cloudShadows, &Settings::buildingParticles,
	&Settings::fullMagicEffects, &Settings::translucentPanels, &Settings::translucentPathLines,
	&Settings::smoothProgressIndicators, &Settings::decorativeAnimations};
}
TEST_SUITE("SettingsGraphics")
{
    TEST_CASE("gradient settings preserve explicit variants and CLI precedence")
    {
        using namespace gradient_kernel;
        glob2test::ScopedEnvironment environment("GLOB2_GRADIENT_BACKEND", "");
        glob2test::HeadlessGlobals globals;
        const std::string file="gradient-backend-test.txt";
        const auto path=std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0))/file;
        Settings settings;
        for(const auto backend:{Backend::Scalar,Backend::SSE2,Backend::NEON,Backend::OpenCL,Backend::Vulkan,Backend::WebGPU}){
            settings.gradientBackend=backend;settings.gradientAlgorithm=backend==Backend::Vulkan || backend==Backend::WebGPU?Algorithm::Jacobi:Algorithm::Frozen8;settings.gradientVerify=true;
            REQUIRE(settings.save(file));Settings loaded;loaded.load(file);
            CHECK(loaded.gradientBackend==backend);CHECK(loaded.gradientConfiguration()==settings.gradientConfiguration());
        }
        for(bool legacy:{false,true}){
            {std::ofstream out(path);out<<"gpuCompute="<<legacy<<'\n';}
            settings.load(file);CHECK(settings.gradientConfiguration().backend==(legacy?Backend::OpenCL:Backend::CPU));
        }
        globals->applyCommand(Cli::parse({"play","--gpu=off","--gradient-backend=vulkan","--gradient-algorithm=jacobi","--gradient-verify=on"}));
        CHECK(globals->gradientConfiguration().backend==Backend::Vulkan);
        CHECK(globals->gradientConfiguration().algorithm==Algorithm::Jacobi);
        CHECK(globals->gradientConfiguration().verify);
        globals->applyCommand(Cli::parse({"play","--gradient-backend=scalar","--gradient-algorithm=jacobi"}));
        CHECK_THROWS_AS(globals->gradientConfiguration(),std::invalid_argument);
        std::filesystem::remove(path);
    }
    TEST_CASE("verification environment and explicit CLI override saved preference")
    {
        glob2test::ScopedEnvironment backend("GLOB2_GRADIENT_BACKEND", "");
        glob2test::ScopedEnvironment algorithm("GLOB2_GRADIENT_ALGORITHM", "");
        glob2test::HeadlessGlobals globals;
        globals->settings.gradientVerify=false;
        for(const char* value:{"1","on","true"}) {
            glob2test::ScopedEnvironment verify("GLOB2_GRADIENT_VERIFY",value);
            CHECK(globals->gradientConfiguration().verify);
            globals->applyCommand(Cli::parse({"play","--gradient-verify=off"}));
            CHECK_FALSE(globals->gradientConfiguration().verify);
            globals->gradientVerifyOverride.reset();
        }
        globals->settings.gradientVerify=true;
        for(const char* value:{"0","off","false","unknown"}) {
            glob2test::ScopedEnvironment verify("GLOB2_GRADIENT_VERIFY",value);
            CHECK_FALSE(globals->gradientConfiguration().verify);
            globals->applyCommand(Cli::parse({"play","--gradient-verify=on"}));
            CHECK(globals->gradientConfiguration().verify);
            globals->gradientVerifyOverride.reset();
        }
        glob2test::ScopedEnvironment absent("GLOB2_GRADIENT_VERIFY", "");
        CHECK(globals->gradientConfiguration().verify);
    }
    TEST_CASE("first-launch hand choice persists and migrates existing side preferences")
    {
        glob2test::HeadlessGlobals globals;
        const std::string file = "hand-preference-test.txt";
        const auto path = std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / file;
        Settings settings;
        CHECK_FALSE(settings.thumbSideChosen);
        for (int side : {Settings::THUMB_LEFT, Settings::THUMB_RIGHT}) {
            { std::ofstream out(path); out << "thumbSide=" << side << '\n'; }
            settings.load(file);
            CHECK(settings.thumbSideChosen);
            CHECK(settings.thumbSide == side);
            settings.thumbSideChosen = false;
            REQUIRE(settings.save(file));
            settings.load(file);
            CHECK_FALSE(settings.thumbSideChosen);
            settings.thumbSideChosen = true;
            REQUIRE(settings.save(file));
            settings.load(file);
            CHECK(settings.thumbSideChosen);
            CHECK(settings.thumbSide == side);
        }
        for (const char *invalid : {"clouds=1\n", "thumbSide=99\n", "thumbSide=0\nthumbSideChosen=invalid\n"}) {
            { std::ofstream out(path); out << invalid; }
            settings.load(file);
            CHECK_FALSE(settings.thumbSideChosen);
        }
        std::filesystem::remove(path);
    }

	TEST_CASE("hand-choice screen saves both sides and retains a choice after write failure [writes-preferences]")
	{
		glob2test::HeadlessGlobals globals;
		auto &settings = globals->settings;
		HandPreferenceScreen invalid;
		invalid.choose(-1);
		CHECK_FALSE(settings.thumbSideChosen);
		for (int side : {Settings::THUMB_LEFT, Settings::THUMB_RIGHT}) {
			HandPreferenceScreen screen;
			screen.choose(side);
			CHECK(settings.thumbSideChosen);
			CHECK(settings.thumbSide == side);
			Settings loaded;
			loaded.load();
			CHECK(loaded.thumbSideChosen);
			CHECK(loaded.thumbSide == side);
		}
		const auto path = std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / "preferences.txt";
		std::filesystem::remove(path);
		std::filesystem::create_directory(path);
		settings.thumbSideChosen = false;
		settings.thumbSide = Settings::THUMB_RIGHT;
		HandPreferenceScreen failed;
		failed.choose(Settings::THUMB_LEFT);
		CHECK_FALSE(settings.thumbSideChosen);
		CHECK(settings.thumbSide == Settings::THUMB_RIGHT);
		std::filesystem::remove(path);
	}

	TEST_CASE("illustrated hand choices activate through their phone overlay [display][writes-preferences]")
	{
		glob2test::ScopedEnvironment touch("GLOB2_MOBILE_UI", "1");
		glob2test::GlobalsOptions options{.display = true, .loadStrings = true, .width = 390, .height = 844,
			.screenFlags = GAGCore::GraphicContext::PORTABLEGPU};
		glob2test::HeadlessGlobals globals(options);
		for (int side : {Settings::THUMB_LEFT, Settings::THUMB_RIGHT}) {
			globals->settings.thumbSideChosen = false;
			HandPreferenceScreen screen;
			screen.beginExecution(globals->gfx);
			screen.paintFrame(SDL_GetTicks());
			const auto bounds = screen.host().bounds(side == Settings::THUMB_LEFT ? "hand.grip.left" : "hand.grip.right");
			REQUIRE_FALSE(bounds.empty());
			screen.host().tapAt(bounds.center());
			CHECK(globals->settings.thumbSideChosen);
			CHECK(globals->settings.thumbSide == side);
			CHECK_FALSE(screen.isExecutionRunning());
			screen.finishExecution();
		}
	}

    TEST_CASE("GPU local preference preserves graphical and headless defaults")
    {
        glob2test::ScopedEnvironment backend("GLOB2_GRADIENT_BACKEND", "");
        glob2test::HeadlessGlobals globals;
        const std::string file = "gpu-compute-test.txt";
        const auto path = std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / file;
        Settings settings;
        CHECK_FALSE(settings.gpuComputeEnabled());
        CHECK(settings.gpuComputeEnabled(true));
        REQUIRE(settings.save(file));
        settings.load(file);
        CHECK_FALSE(settings.gpuCompute.has_value());
        CHECK(settings.gpuComputeEnabled(true));
        for (bool enabled : {false, true}) {
            settings.gpuCompute = enabled;
            settings.setGraphicsDetail(!enabled);
            REQUIRE(settings.save(file));
            Settings loaded; loaded.load(file);
            REQUIRE(loaded.gpuCompute.has_value());
            CHECK(loaded.gpuComputeEnabled() == enabled);
            CHECK(loaded.gpuComputeEnabled(true) == enabled);
        }
        for (const char* value : {"", "auto", "true", "2", "1oops"}) {
            { std::ofstream out(path); out << "gpuCompute=" << value << '\n'; }
            settings.gpuCompute = false;
            settings.load(file);
            CHECK_FALSE(settings.gpuCompute.has_value());
            CHECK_FALSE(settings.gpuComputeEnabled());
            CHECK(settings.gpuComputeEnabled(true));
        }
        { std::ofstream out(path); out << "clouds=1\n"; }
        settings.gpuCompute = true;
        settings.load(file);
        CHECK_FALSE(settings.gpuCompute.has_value());

        globals->settings.gpuCompute.reset();
        globals->applyCommand(Cli::parse({"game", "repeat", "saved.game.gz", "--display"}));
        CHECK_FALSE(globals->gpuComputeEnabled());
        globals->applyCommand(Cli::parse({"game", "repeat", "saved.game.gz"}));
        CHECK(globals->gpuComputeEnabled());
        globals->settings.gpuCompute = false;
        CHECK_FALSE(globals->gpuComputeEnabled());
        globals->applyCommand(Cli::parse({"game", "repeat", "saved.game.gz", "--gpu=on"}));
        CHECK(globals->gpuComputeEnabled());
        CHECK_FALSE(*globals->settings.gpuCompute);
        globals->applyCommand(Cli::parse({"game", "repeat", "saved.game.gz", "--gpu=off"}));
        CHECK_FALSE(globals->gpuComputeEnabled());
        globals->gpuComputeOverride.reset();
        globals->settings.gpuCompute = true;
        {
            glob2test::ScopedEnvironment cpu("GLOB2_GRADIENT_BACKEND", "cpu");
            CHECK_FALSE(globals->gpuComputeEnabled());
            globals->gpuComputeOverride = true;
            CHECK(globals->gpuComputeEnabled());
        }
        globals->gpuComputeOverride.reset();
        globals->settings.gpuCompute = false;
        {
            glob2test::ScopedEnvironment gpu("GLOB2_GRADIENT_BACKEND", "opencl");
            CHECK(globals->gpuComputeEnabled());
            globals->gpuComputeOverride = false;
            CHECK_FALSE(globals->gpuComputeEnabled());
        }
        std::filesystem::remove(path);
    }

    TEST_CASE("renderer preference migrates flags and round trips without startup overrides")
    {
        glob2test::HeadlessGlobals globals;
        const std::string file = "renderer-preference-test.txt";
        const auto path = std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / file;
        Settings settings;
        CHECK(settings.rendererBackend == GAGCore::RendererBackend::Auto);
        for (const auto &[flags, expected] : std::array{
            std::pair{0, GAGCore::RendererBackend::Software},
            std::pair{int(GAGCore::GraphicContext::USEGPU), GAGCore::RendererBackend::LegacyGL},
            std::pair{int(GAGCore::GraphicContext::PORTABLEGPU), GAGCore::RendererBackend::PortableSDL}}) {
            { std::ofstream out(path); out << "screenFlags=" << flags << '\n'; }
            settings.load(file);
            CHECK(settings.rendererBackend == expected);
        }
        for (auto backend : GAGCore::RendererBackends) {
            settings.rendererBackend = backend;
            REQUIRE(settings.save(file));
            Settings loaded; loaded.load(file);
            CHECK(loaded.rendererBackend == backend);
        }
        { std::ofstream out(path); out << "rendererBackend=invalid\nscreenFlags=1\n"; }
        settings.load(file);
        CHECK(settings.rendererBackend == GAGCore::RendererBackend::Auto);
        std::filesystem::remove(path);
    }
    TEST_CASE("render ceiling defaults migrates validates and round trips")
    {
        glob2test::HeadlessGlobals globals;
        const std::string file = "render-fps-test.txt";
        const auto path = std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / file;
        Settings settings;
        CHECK(settings.targetRenderFps == 60);
        for (int fps : Settings::RENDER_FPS_PRESETS) {
            settings.targetRenderFps = fps;
            settings.setGraphicsDetail(false);
            CHECK(settings.targetRenderFps == fps);
            REQUIRE(settings.save(file));
            Settings loaded; loaded.load(file);
            CHECK(loaded.targetRenderFps == fps);
        }
        for (const char *value : {"", "garbage", "60oops", "-1", "26", "9999999999999999999"}) {
            { std::ofstream out(path); out << "targetRenderFps=" << value << '\n'; }
            settings.targetRenderFps = 0;
            settings.load(file);
            CHECK(settings.targetRenderFps == 60);
        }
        { std::ofstream out(path); out << "optionFlags=129\n"; }
        settings.targetRenderFps = 0;
        settings.load(file);
        CHECK(settings.targetRenderFps == 60);
        std::filesystem::remove(path);
    }

    TEST_CASE("edge scrolling defaults migrate and independent modes round trip")
    {
        glob2test::HeadlessGlobals globals;
        const std::string file = "edge-scroll-test.txt";
        const auto path = std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / file;
        { std::ofstream out(path); out << "scrollWheelEnabled=1\n"; }
        Settings legacy; legacy.load(file);
        CHECK(legacy.edgeScrollingEnabled(true));
        CHECK_FALSE(legacy.edgeScrollingEnabled(false));
        for (bool fullscreen : {false, true})
            for (bool windowed : {false, true})
            {
                legacy.edgeScrollFullscreen = fullscreen;
                legacy.edgeScrollWindowed = windowed;
                REQUIRE(legacy.save(file));
                Settings loaded; loaded.load(file);
                CHECK(loaded.edgeScrollingEnabled(true) == fullscreen);
                CHECK(loaded.edgeScrollingEnabled(false) == windowed);
            }
        std::filesystem::remove(path);
    }
	TEST_CASE("text size applies to desktop and touch independently of interface scale")
	{
		glob2test::HeadlessGlobals globals;
		const double touchBase = Glob2UI::frontendTheme().touchTextScale;
		CHECK(touchBase == doctest::Approx(1.15));
		for (int percent : {100,125,150})
		{
			globals->settings.setTextSizePercent(percent);
			// Desktop: authored pixels times the preference, whatever the scale.
			auto desktop = Glob2UI::Presentation::forSurface(800, 600, 1.5, false);
			Glob2UI::applyTextSize(desktop, touchBase);
			CHECK(desktop.textUnit == doctest::Approx(percent / 100.0));
			// Touch: points, so the same physical size on every logical surface.
			for (double unit : {1.0, 1.54, 2.05})
			{
				auto touch = Glob2UI::Presentation::forSurface(800, 600, unit, true);
				Glob2UI::applyTextSize(touch, touchBase);
				CHECK(touch.textUnit == doctest::Approx(unit * 1.15 * percent / 100.0));
				CHECK(touch.textPt(100) == touch.pt(100 * percent / 100.0));
			}
		}
		globals->settings.setTextSizePercent(100);
	}
	TEST_CASE("legacy preferences migrate and independent effects round trip")
	{
		glob2test::HeadlessGlobals globals;
		const std::string file = "graphics-test.txt";
		const auto path = std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) / file;
		for (bool reduced : {false, true})
		{
			{ std::ofstream out(path); out << "optionFlags=" << (128 + reduced) << '\n'; }
			Settings s; s.load(file);
			for (auto field : effects) CHECK(s.*field == !reduced);
			CHECK(s.optionFlags == 128);
			CHECK(s.showColonySkins);
			CHECK(s.save(file));
			Settings reloaded; reloaded.load(file);
			for (auto field : effects) CHECK(reloaded.*field == !reduced);
		}
		{ std::ofstream out(path); out << "optionFlags=129\nclouds=1\ntranslucentPanels=1\n"; }
		Settings mixed; mixed.load(file);
		CHECK(mixed.clouds); CHECK(!mixed.cloudShadows); CHECK(mixed.translucentPanels);
		CHECK(mixed.optionFlags == 128);
		mixed.setGraphicsDetail(true);
		for (auto field : effects) CHECK(mixed.*field);
		mixed.setGraphicsDetail(false);
		for (auto field : effects) CHECK(!(mixed.*field));
		CHECK(mixed.optionFlags == 128);
		for (size_t i = 0; i < effects.size(); ++i)
		{
			mixed.setGraphicsDetail(false); mixed.*effects[i] = true;
			REQUIRE(mixed.save(file));
			Settings s; s.load(file);
			for (size_t j = 0; j < effects.size(); ++j) CHECK(s.*effects[j] == (i == j));
		}
		{ std::ofstream out(path); out << "clouds=0\n"; }
		Settings defaults; defaults.load(file);
		CHECK(!defaults.clouds);
#ifndef __EMSCRIPTEN__
		CHECK(defaults.cloudShadows); CHECK(defaults.fullMagicEffects);
#endif
		defaults.showColonySkins = false;
        defaults.setGraphicsDetail(true);
        CHECK_FALSE(defaults.showColonySkins);
        REQUIRE(defaults.save(file));
        Settings hidden; hidden.load(file);
        CHECK_FALSE(hidden.showColonySkins);
		std::filesystem::remove(path);
	}
}
