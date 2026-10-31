// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ScopedEnvironment.h"
#include "Introduction.h"
#include "LessonCatalogScreen.h"
#include "TutorialGuidance.h"
#include "Engine.h"
#include "Application.h"
#include "IntroductionOfferScreen.h"
#include "MainMenuScreen.h"
#include "GameGUITouch.h"
#include "GlobalContainer.h"
#include "ui/FrontendUI.h"
#include <GraphicContext.h>
#include <Toolkit.h>

TEST_CASE("guidance adapts to desktop touch and large text without consuming map input [display][artifacts]" * doctest::test_suite("IntroductionUI"))
{
    for (const auto &[width,height,touch,textSize] : {std::tuple{1024,768,false,100},
        std::tuple{400,800,true,100},std::tuple{800,400,true,100},
        std::tuple{400,800,true,150},std::tuple{800,400,true,150}}) {
        glob2test::ScopedEnvironment mobile("GLOB2_MOBILE_UI",touch ? "1" : "0");
        glob2test::GlobalsOptions options{.display=true,.loadStrings=true,.width=width,.height=height};
        glob2test::HeadlessGlobals globals(options);
        globals->settings.setTextSizePercent(textSize);
        Engine engine;
        REQUIRE(engine.initCampaignTask((glob2test::sourceRoot()/Introduction::Map).string()).run());
        engine.prepareRun(); engine.beginSession(0);
        CHECK(engine.gui.game.teams[0]->liveUnits.size()==13);
        engine.gui.game.setWaitingOnMask(0);
        for (int tick=0;tick<64;++tick) engine.gui.game.step(0);
        glob2test::drawGUI(engine.gui,0);
        const auto &session=engine.gui.drawnScene().panels.hud.state();
        INFO("briefing=" << engine.gui.game.missionBriefing << " objectives=" << engine.gui.game.objectives.getNumberOfObjectives()
            << " local=" << engine.gui.localTeamNo << " session=" << (session.missionBriefing ? *session.missionBriefing : "absent"));
        REQUIRE(Introduction::active(session));
        REQUIRE(session.tutorial);
        auto *card=engine.gui.tutorialGuidance.get(); REQUIRE(card);
        const auto capture=[&](const char *suffix) {
            glob2test::drawGUI(engine.gui,0);
            globals->gfx->nextFrame();
            REQUIRE(SDL_SaveBMP(globals->gfx->completedFrame(),
                (glob2test::artifactDir()/(std::to_string(width)+"-"+std::to_string(textSize)+"-"+suffix+".bmp")).string().c_str()));
        };
        capture("scout");
        auto bounds=card->panelBounds();
        CHECK(bounds.x>=0); CHECK(bounds.y>=0);
        CHECK(bounds.x+bounds.w<=globals->gfx->getW());
        CHECK(bounds.y+bounds.h<=globals->gfx->getH());
        if (touch) {
            const auto hud=engine.gui.touch->hudLayout();
            CHECK(bounds.y+bounds.h<hud.actions.y);
            CHECK(bounds.h<height*.5);
            // The default instruction and both actions fit without scrolling.
            const auto instruction=card->host().bounds("intro/instruction");
            CHECK(instruction.y>=bounds.y);
            CHECK(instruction.y+instruction.h<=bounds.y+bounds.h);
        }
        SDL_Event outside{};outside.type=SDL_EVENT_MOUSE_BUTTON_DOWN;
        outside.button.button=SDL_BUTTON_LEFT;outside.button.x=globals->gfx->getW()-1;outside.button.y=globals->gfx->getH()-1;
        CHECK_FALSE(card->handle(outside));
        const auto click=[&](const char *key) {
            card->host().layoutIfNeeded();
            const auto r=card->host().bounds(key); REQUIRE(r.w>0);
            CHECK(r.y>=card->panelBounds().y);
            CHECK(r.y+r.h<=card->panelBounds().y+card->panelBounds().h);
            if (touch) {
                CHECK(r.w>=44); CHECK(r.h>=44);
                SDL_Event e{};e.type=SDL_EVENT_FINGER_DOWN;
                e.tfinger.x=float(r.x+r.w/2)/width;e.tfinger.y=float(r.y+r.h/2)/height;
                CHECK(card->handle(e));e.type=SDL_EVENT_FINGER_UP;CHECK(card->handle(e));
                card->update(1);return;
            }
            SDL_Event e{};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_LEFT;
            e.button.x=r.x+r.w/2;e.button.y=r.y+r.h/2;
            CHECK(card->handle(e));e.type=SDL_EVENT_MOUSE_BUTTON_UP;CHECK(card->handle(e));
            card->update(1);
        };
        card->host().scrollIntoView("intro/destination"); card->update(1);
        const double originalX=engine.gui.camera.originX;
        click("intro/destination");
        CHECK(engine.gui.camera.originX!=originalX);
        CHECK_FALSE(engine.gui.game.objectives.isObjectiveComplete(0));
        capture("destination");
        click("intro/collapse");capture("collapsed");
        click("intro/collapse");
        card->host().scrollIntoView("intro/help");card->update(2);
        click("intro/help");capture("help");
        CHECK(card->host().find("intro/help-text")!=nullptr);
        CHECK_FALSE(engine.gui.game.objectives.isObjectiveComplete(0));
        for (int i=0;i<5;++i) engine.gui.game.objectives.setObjectiveComplete(i);
        // Exercise completion presentation without replaying the full lesson in
        // every viewport; real-order completion is covered by Introduction.
        auto completionSource=engine.gui.game.mapscript.getMapScript();
        const auto callback=completionSource.find("function step(ctx)"); REQUIRE(callback!=std::string::npos);
        completionSource.replace(callback,18,"function originalStep(ctx)");
        completionSource+="\nfunction step(ctx){originalStep(ctx);tutorialState.stepId='complete';return originalStep(ctx);}";
        MapScriptError error;
        REQUIRE(engine.gui.game.mapscript.replaceSource(MapScript::JavaScript,completionSource,error));
        for(int i=0;i<32;++i) engine.gui.game.step(0);
        capture("complete");
        CHECK(Introduction::stage(engine.gui.drawnScene().panels.hud.state())==5);
        card->host().scrollIntoView("intro/replay");card->update(3);
        click("intro/replay");
        REQUIRE(engine.gui.inGameMenu==GameGUI::IGM_TUTORIAL_RESTART);
        engine.gui.gameMenuScreen->finish(InGameConfirmScreen::CANCEL);
        engine.gui.processGameMenu(nullptr);
        CHECK_FALSE(engine.gui.flushOutgoingAndExit);
        CHECK(engine.gui.toLoadGameFileName.empty());
        CHECK(engine.gui.game.objectives.isObjectiveComplete(4));
        engine.gui.gameMenuScreen->finish(InGameMainScreen::TUTORIAL_RESTART);
        engine.gui.processGameMenu(nullptr);
        REQUIRE(engine.gui.inGameMenu==GameGUI::IGM_TUTORIAL_RESTART);
        engine.gui.gameMenuScreen->finish(InGameConfirmScreen::CONFIRM);
        engine.gui.processGameMenu(nullptr);
        CHECK(engine.gui.flushOutgoingAndExit);
        CHECK(engine.gui.toLoadGameFileName==Introduction::Map);
    }
}

TEST_CASE("first-run recommendation can be declined and stays dismissed [display][writes-preferences]" * doctest::test_suite("IntroductionUI"))
{
    glob2test::ScopedEnvironment mobile("GLOB2_MOBILE_UI","0");
    glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=1024,.height=768});
    globals->settings.audioPreferenceChosen=true;
    globals->settings.thumbSideChosen=true;
    globals->settings.introductionOffered=false;
    Application application;
    application.screens.frame(0,{});
    auto *offer=dynamic_cast<IntroductionOfferScreen*>(application.screens.top());
    REQUIRE(offer);
    const auto bounds=offer->host().bounds("intro/menu"); REQUIRE(bounds.w>0);
    SDL_Event down{};down.type=SDL_EVENT_MOUSE_BUTTON_DOWN;down.button.button=SDL_BUTTON_LEFT;
    down.button.x=bounds.x+bounds.w/2;down.button.y=bounds.y+bounds.h/2;
    SDL_Event up=down;up.type=SDL_EVENT_MOUSE_BUTTON_UP;
    application.screens.frame(40,{down,up});
    for (Uint32 tick=80;tick<400 && !dynamic_cast<MainMenuScreen*>(application.screens.top());tick+=40)
        application.screens.frame(tick,{});
    REQUIRE(dynamic_cast<MainMenuScreen*>(application.screens.top()));
    CHECK(globals->settings.introductionOffered);
    Settings saved; saved.load(); CHECK(saved.introductionOffered);
    application.choose(MainMenuScreen::INTRODUCTION);
    application.screens.frame(440,{});
    REQUIRE(dynamic_cast<IntroductionOfferScreen*>(application.screens.top()));
    CHECK(globals->settings.introductionOffered);
}

TEST_CASE("catalog and second lesson use generic guidance and restart navigation [display][artifacts]" * doctest::test_suite("IntroductionUI"))
{
    glob2test::ScopedEnvironment mobile("GLOB2_MOBILE_UI","1");
    glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=400,.height=800});
    Tutorial::LessonCatalogScreen catalog;
    REQUIRE(catalog.entries.size()>=10);
    CHECK(catalog.entries[1].map=="campaigns/food-supply.map.gz");
    Engine engine;REQUIRE(engine.initCampaignTask(catalog.entries[1].map).run());
    engine.prepareRun();engine.beginSession(0);engine.gui.game.setWaitingOnMask(0);
    for(int i=0;i<64;++i) engine.gui.game.step(0);
    glob2test::drawGUI(engine.gui,0);
    auto model=Tutorial::view(engine.gui.drawnScene().panels.hud.state());REQUIRE(model);
    CHECK(model->count==2);CHECK(model->lessonId=="food-supply");CHECK(model->stepId=="build");
    auto *card=engine.gui.tutorialGuidance.get();REQUIRE(card);
    CHECK(card->host().find("intro/help")!=nullptr);
    CHECK(card->host().find("intro/destination")==nullptr);
    globals->gfx->nextFrame();
    REQUIRE(SDL_SaveBMP(globals->gfx->completedFrame(),(glob2test::artifactDir()/"food-supply.bmp").string().c_str()));
    engine.gui.openMainMenu();
    engine.gui.gameMenuScreen->finish(InGameMainScreen::TUTORIAL_RESTART);engine.gui.processGameMenu(nullptr);
    REQUIRE(engine.gui.inGameMenu==GameGUI::IGM_TUTORIAL_RESTART);
    engine.gui.gameMenuScreen->finish(InGameConfirmScreen::CONFIRM);engine.gui.processGameMenu(nullptr);
    CHECK(engine.gui.toLoadGameFileName=="campaigns/food-supply.map.gz");
    engine.gui.isRunning=false;
    const auto request=engine.finishSessionForHost();REQUIRE(request);
    CHECK(request->campaign);CHECK(request->filename=="campaigns/food-supply.map.gz");
}

TEST_CASE("expanded lesson catalog and guidance fit both phone orientations [display][artifacts]" * doctest::test_suite("IntroductionUI"))
{
    for(const auto &[width,height,text]:{std::tuple{400,800,100},std::tuple{800,400,100},std::tuple{400,800,150},std::tuple{800,400,150}}) {
        glob2test::ScopedEnvironment mobile("GLOB2_MOBILE_UI","1");
        glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=width,.height=height});
        globals->settings.setTextSizePercent(text);
        {
            GAGGUI::ScreenStack stack(*globals->gfx);
            stack.push(std::make_unique<Tutorial::LessonCatalogScreen>());stack.frame(0,{});
            auto *catalog=dynamic_cast<Tutorial::LessonCatalogScreen*>(stack.top());REQUIRE(catalog);
            catalog->host().scrollIntoView("tutorial/lesson/9");stack.frame(40,{});
            const auto last=catalog->host().bounds("tutorial/lesson/9");
            CHECK(last.y>=0);CHECK(last.bottom()<=height);CHECK(last.h>=44);
            if(text==100) {
                globals->gfx->nextFrame();
                REQUIRE(SDL_SaveBMP(globals->gfx->completedFrame(),(glob2test::artifactDir()/(std::to_string(width)+"-catalog.bmp")).string().c_str()));
            }
        }
        Tutorial::LessonCatalogScreen catalog;REQUIRE(catalog.entries.size()>=10);
        for(size_t i=2;i<catalog.entries.size();++i) {
            CAPTURE(width);CAPTURE(height);CAPTURE(text);CAPTURE(i);
            Engine engine;REQUIRE(engine.initCampaignTask((glob2test::sourceRoot()/catalog.entries[i].map).string()).run());
            engine.prepareRun();engine.beginSession(0);engine.gui.game.setWaitingOnMask(0);
            for(int t=0;t<64;++t)engine.gui.game.step(0);
            glob2test::drawGUI(engine.gui,0);
            auto *card=engine.gui.tutorialGuidance.get();REQUIRE(card);
            auto bounds=card->panelBounds();
            CHECK(bounds.x>=0);CHECK(bounds.y>=0);CHECK(bounds.right()<=width);
            CHECK(bounds.bottom()<engine.gui.touch->hudLayout().actions.y);
            auto instruction=card->host().bounds("intro/instruction");
            CHECK(instruction.bottom()<=bounds.bottom());
            const auto help=card->host().bounds("intro/help");CHECK(help.w>=44);CHECK(help.h>=44);
            if(text==100) {
                globals->gfx->nextFrame();
                const auto path=glob2test::artifactDir()/(std::to_string(width)+"-"+std::to_string(i)+".bmp");
                REQUIRE(SDL_SaveBMP(globals->gfx->completedFrame(),path.string().c_str()));
            }
        }
    }
}
