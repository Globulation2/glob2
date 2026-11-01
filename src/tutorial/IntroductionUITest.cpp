// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ScopedEnvironment.h"
#include "LessonCatalogScreen.h"
#include "TutorialGuidance.h"
#include "Engine.h"
#include "Application.h"
#include "IntroductionOfferScreen.h"
#include "MainMenuScreen.h"
#include "CampaignSelectorScreen.h"
#include "GameGUITouch.h"
#include "GlobalContainer.h"
#include "ui/FrontendUI.h"
#include <GraphicContext.h>
#include <Toolkit.h>

TEST_CASE("guidance releases keyboard focus when returning to gameplay [display]" * doctest::test_suite("IntroductionUI"))
{
    for (const bool touch : {false,true}) {
        const int width=touch ? 400 : 1024,height=touch ? 800 : 768;
        glob2test::ScopedEnvironment mobile("GLOB2_MOBILE_UI",touch ? "1" : "0");
        glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=width,.height=height});
        int destinations=0;
        Tutorial::Guidance card([&](int navigation){if(navigation==Tutorial::ShowDestination) ++destinations;});
        auto model=std::make_shared<Tutorial::Model>();
        model->lessonId="focus-test";model->stepId="first";model->count=1;
        model->title="[intro goal scout]";model->instruction="[intro instruction scout]";
        model->touchInstruction="[intro compact scout]";
        model->destination=Tutorial::Destination{0,0,""};
        card.attach(*globals->gfx);card.model(model);card.update(0);
        const auto key=[&](SDL_Keycode code) {
            SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.key=code;
            return card.handle(event);
        };
        const auto focus=[&] {CHECK(key(SDLK_TAB));CHECK(card.host().keyboardFocused());};
        const auto gameplayKeys=[&] {
            CHECK_FALSE(card.host().keyboardFocused());
            CHECK_FALSE(key(SDLK_RETURN));CHECK_FALSE(key(SDLK_SPACE));
        };
        focus();
        SDL_Event mouse{};mouse.type=SDL_EVENT_MOUSE_BUTTON_DOWN;mouse.button.button=SDL_BUTTON_LEFT;
        mouse.button.x=width-1;mouse.button.y=height-1;
        REQUIRE_FALSE(card.panelBounds().contains(GAGGUI::ui::Point{width-1,height-1}));
        CHECK_FALSE(card.handle(mouse));gameplayKeys();
        mouse.type=SDL_EVENT_MOUSE_BUTTON_UP;CHECK_FALSE(card.handle(mouse));
        focus();
        SDL_Event finger{};finger.type=SDL_EVENT_FINGER_DOWN;finger.tfinger.touchID=19;finger.tfinger.fingerID=1;
        finger.tfinger.x=float(width-1)/width;finger.tfinger.y=float(height-1)/height;
        CHECK_FALSE(card.handle(finger));gameplayKeys();
        finger.type=SDL_EVENT_FINGER_UP;CHECK_FALSE(card.handle(finger));
        focus();CHECK_FALSE(key(SDLK_ESCAPE));gameplayKeys();
        focus();SDL_Event lost{};lost.type=SDL_EVENT_WINDOW_FOCUS_LOST;
        CHECK_FALSE(card.handle(lost));gameplayKeys();
        card.update(1);CHECK(destinations==0);
        // Returning focus to the card still allows ordinary keyboard activation.
        card.host().focus("intro/destination",true);CHECK(key(SDLK_RETURN));
        card.update(2);CHECK(destinations==1);
    }
}

TEST_CASE("mobile guidance announces recovery hints without opening Help [display]" * doctest::test_suite("IntroductionUI"))
{
    for (const auto &[width,height,textSize] : {std::tuple{400,800,100},std::tuple{800,400,100},
                                               std::tuple{400,800,150},std::tuple{800,400,150}}) {
        glob2test::ScopedEnvironment mobile("GLOB2_MOBILE_UI","1");
        glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=width,.height=height});
        globals->settings.setTextSizePercent(textSize);
        Tutorial::Guidance card([](int){});
        card.mobileBounds({8,64,width-16,height-128});
        auto model=std::make_shared<Tutorial::Model>();
        model->lessonId="hint-test";model->stepId="first";model->count=5;
        model->title="[intro goal scout]";model->touchInstruction="[intro compact scout]";
        model->destination=Tutorial::Destination{0,0,""};
        card.attach(*globals->gfx);card.model(model);card.update(0);
        REQUIRE(card.host().find("intro/instruction"));
        const auto collapse=card.host().bounds("intro/collapse");
        SDL_Event event{};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.button=SDL_BUTTON_LEFT;
        event.button.x=collapse.x+collapse.w/2;event.button.y=collapse.y+collapse.h/2;
        REQUIRE(card.handle(event));event.type=SDL_EVENT_MOUSE_BUTTON_UP;REQUIRE(card.handle(event));
        card.update(1);CHECK_FALSE(card.host().find("intro/instruction"));
        auto hinted=std::make_shared<Tutorial::Model>(*model);hinted->hint="[intro hint retry]";
        card.model(hinted);card.update(2);
        REQUIRE(card.host().find("tutorial/hint"));
        CHECK_FALSE(card.host().find("intro/help-text"));
        const auto panel=card.panelBounds(),body=card.host().bounds("tutorial/hint/first/[intro hint retry]");
        const auto hint=card.host().bounds("tutorial/hint");
        CHECK(body.h>0);CHECK(body.y>=panel.y);CHECK(body.y+body.h<=panel.y+panel.h);
        CHECK(hint.y>=body.y);CHECK(hint.y<body.y+body.h);
        CHECK(panel.h<height*.5);
        for(const auto *action: {"intro/destination","intro/help","intro/collapse"}) {
            const auto bounds=card.host().bounds(action);
            CHECK(bounds.w>=44);CHECK(bounds.h>=44);
            CHECK(bounds.x>=panel.x);CHECK(bounds.x+bounds.w<=panel.x+panel.w);
        }
        // Removing the hint restores the same compact instruction-only body.
        card.model(model);card.update(3);
        CHECK(card.host().find("intro/instruction"));CHECK_FALSE(card.host().find("tutorial/hint"));
    }
}

TEST_CASE("guidance preserves pointer ownership across map drags and concurrent touches [display]" * doctest::test_suite("IntroductionUI"))
{
    glob2test::ScopedEnvironment mobile("GLOB2_MOBILE_UI","1");
    glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=400,.height=800});
    int destinations=0;
    Tutorial::Guidance card([&](int destination){if(destination==Tutorial::ShowDestination) ++destinations;});
    auto model=std::make_shared<Tutorial::Model>();
    model->lessonId="pointer-test";model->stepId="first";model->count=1;
    model->title="[intro goal scout]";model->touchInstruction="[intro compact scout]";
    model->destination=Tutorial::Destination{0,0,""};
    card.attach(*globals->gfx);card.model(model);card.update(0);
    const auto target=card.host().bounds("intro/destination");REQUIRE(target.w>0);
    const GAGGUI::ui::Point inside{target.x+target.w/2,target.y+target.h/2};
    const GAGGUI::ui::Point outside{399,799};REQUIRE_FALSE(card.panelBounds().contains(outside));
    const auto finger=[&](Uint32 type,SDL_FingerID id,GAGGUI::ui::Point point) {
        SDL_Event event{};event.type=type;event.tfinger.touchID=19;event.tfinger.fingerID=id;
        event.tfinger.x=float(point.x)/400;event.tfinger.y=float(point.y)/800;
        return event;
    };
    // Map ownership survives crossing into the card and lifting there.
    CHECK_FALSE(card.handle(finger(SDL_EVENT_FINGER_DOWN,1,outside)));
    CHECK_FALSE(card.handle(finger(SDL_EVENT_FINGER_MOTION,1,inside)));
    CHECK_FALSE(card.handle(finger(SDL_EVENT_FINGER_UP,1,inside)));
    CHECK(destinations==0);
    // A second finger on the card must not steal the map finger's release.
    CHECK_FALSE(card.handle(finger(SDL_EVENT_FINGER_DOWN,1,outside)));
    CHECK(card.handle(finger(SDL_EVENT_FINGER_DOWN,2,inside)));
    CHECK_FALSE(card.handle(finger(SDL_EVENT_FINGER_MOTION,1,inside)));
    CHECK_FALSE(card.handle(finger(SDL_EVENT_FINGER_UP,1,inside)));
    CHECK(card.handle(finger(SDL_EVENT_FINGER_UP,2,inside)));
    card.update(1);CHECK(destinations==1);
    // The card keeps its own drag until release, even outside its bounds.
    CHECK(card.handle(finger(SDL_EVENT_FINGER_DOWN,2,inside)));
    CHECK(card.handle(finger(SDL_EVENT_FINGER_MOTION,2,outside)));
    CHECK(card.handle(finger(SDL_EVENT_FINGER_UP,2,outside)));
    card.update(2);CHECK(destinations==1);
    // Focus loss cancels old ownership, so its delayed UP cannot activate UI.
    CHECK(card.handle(finger(SDL_EVENT_FINGER_DOWN,2,inside)));
    SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;CHECK_FALSE(card.handle(focus));
    CHECK_FALSE(card.handle(finger(SDL_EVENT_FINGER_UP,2,inside)));
    card.update(3);CHECK(destinations==1);
    CHECK(card.handle(finger(SDL_EVENT_FINGER_DOWN,2,inside)));
    CHECK(card.handle(finger(SDL_EVENT_FINGER_UP,2,inside)));
    card.update(4);CHECK(destinations==2);
    // Mouse drags obey the same ownership rule as touch gestures.
    SDL_Event mouse{};mouse.type=SDL_EVENT_MOUSE_BUTTON_DOWN;mouse.button.button=SDL_BUTTON_LEFT;
    mouse.button.x=outside.x;mouse.button.y=outside.y;CHECK_FALSE(card.handle(mouse));
    SDL_Event motion{};motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.state=SDL_BUTTON_LMASK;
    motion.motion.x=inside.x;motion.motion.y=inside.y;CHECK_FALSE(card.handle(motion));
    mouse.type=SDL_EVENT_MOUSE_BUTTON_UP;mouse.button.x=inside.x;mouse.button.y=inside.y;
    CHECK_FALSE(card.handle(mouse));card.update(5);CHECK(destinations==2);
    mouse.type=SDL_EVENT_MOUSE_BUTTON_DOWN;CHECK(card.handle(mouse));
    motion.motion.x=outside.x;motion.motion.y=outside.y;CHECK(card.handle(motion));
    CHECK_FALSE(card.handle(focus));
    mouse.type=SDL_EVENT_MOUSE_BUTTON_UP;CHECK_FALSE(card.handle(mouse));
    card.update(6);CHECK(destinations==2);
}

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
        REQUIRE(engine.initCampaignTask((glob2test::contentRoot()/Tutorial::IntroductionMap).string()).run());
        engine.prepareRun(); engine.beginSession(0);
        CHECK(engine.gui.game.teams[0]->liveUnits.size()==13);
        engine.gui.game.setWaitingOnMask(0);
        for (int tick=0;tick<64;++tick) engine.gui.game.step(0);
        glob2test::drawGUI(engine.gui,0);
        const auto &session=engine.gui.drawnScene().panels.hud.state();
        INFO("briefing=" << engine.gui.game.missionBriefing << " objectives=" << engine.gui.game.objectives.getNumberOfObjectives()
            << " local=" << engine.gui.localTeamNo << " session=" << (session.missionBriefing ? *session.missionBriefing : "absent"));
        REQUIRE(session.tutorial);
        CHECK(session.tutorial->lessonId=="introduction");
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
        REQUIRE(engine.gui.drawnScene().panels.hud.state().tutorial);
        CHECK(engine.gui.drawnScene().panels.hud.state().tutorial->complete());
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
        CHECK(engine.gui.toLoadGameFileName==Tutorial::IntroductionMap);
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
    CHECK(application.screens.top() != nullptr);
    auto *main=static_cast<MainMenuScreen*>(application.screens.top());
    CHECK(main->host().find("menu/1")!=nullptr);
    CHECK(main->host().find("menu/10")==nullptr);
    CHECK(globals->settings.introductionOffered);
    Settings saved; saved.load(); CHECK(saved.introductionOffered);
    application.choose(MainMenuScreen::TUTORIAL);
    application.screens.frame(440,{});
    REQUIRE(dynamic_cast<Tutorial::LessonCatalogScreen*>(application.screens.top()));
    CHECK(globals->settings.introductionOffered);
}

TEST_CASE("retired tutorial progress stays on disk without appearing as a campaign [display]" * doctest::test_suite("IntroductionUI"))
{
    glob2test::HeadlessGlobals globals({.display=true,.loadStrings=true,.width=640,.height=480});
    Campaign campaign;
    REQUIRE(campaign.load((glob2test::sourceRoot()/"test/fixtures/campaign/Browser_Test_Campaign.txt").string()));
    REQUIRE(campaign.getMapCount()==2);
    REQUIRE(campaign.save(true));
    campaign.setName("Tutorial Campaign");
    REQUIRE(campaign.save(true));
    CampaignSelectorScreen saved(true);
    CHECK(std::find(saved.names.begin(),saved.names.end(),"Browser_Test_Campaign")!=saved.names.end());
    CHECK(std::find(saved.names.begin(),saved.names.end(),"Tutorial_Campaign")==saved.names.end());
    Campaign retained;
    CHECK(retained.load("games/Tutorial_Campaign.txt"));
    CHECK(retained.getMapCount()==2);
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
            Engine engine;REQUIRE(engine.initCampaignTask((glob2test::contentRoot()/catalog.entries[i].map).string()).run());
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
