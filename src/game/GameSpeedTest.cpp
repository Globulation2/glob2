// SPDX-License-Identifier: GPL-3.0-or-later
// Game speed settings, controls and playback; the display cases need OpenGL.
#include "EngineFixtures.h"
#include "ScopedEnvironment.h"
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include "GlobalContainer.h"
#include "SettingsScreen.h"
#include "GameGUIDialog.h"
#include "Engine.h"
#include "EngineTiming.h"
#include "TurnProtocol.h"
#include "KeyboardManager.h"
#include "ReplayReader.h"
#include "GameGUIKeyActions.h"
#include <StringTable.h>
#include <SDL3_net/SDL_net.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>

using namespace GAGGUI;

namespace
{
struct TestSettingsScreen : SettingsScreen {
    TestSettingsScreen() { beginExecution(globalContainer->gfx); selectCategory(Category::Gameplay); }
    Row speed() { for(const auto& r:rows()) if(r.id=="gameplay.speed") return r; REQUIRE_MESSAGE(false, "no gameplay.speed row"); return {}; }
    void select(int value) { REQUIRE(changeSetting("gameplay.speed", value-Settings::GAME_SPEED_MINIMUM)); }
    int speedRow(int value) const { return value-Settings::GAME_SPEED_MINIMUM; }
};

static Uint32 SDLCALL resumeGame(void* data, SDL_TimerID, Uint32) {
    SDL_Event event={};
    event.type=SDL_EVENT_KEY_DOWN;
    event.key.key=*static_cast<SDL_Keycode*>(data);
    SDL_PushEvent(&event);
    return 0;
}

glob2test::GlobalsOptions displayOptions()
{
    glob2test::GlobalsOptions options{.display=true,.loadStrings=true,.width=640,.height=480,.screenFlags=GAGCore::GraphicContext::USEGPU};
    options.beforeLoad=[](GlobalContainer& globals){globals.settings.gameSpeed=0;};
    return options;
}
}

TEST_SUITE("GameSpeed")
{
TEST_CASE("presets; bounds; legacy settings and persistence")
{
    glob2test::HeadlessGlobals globals;
    auto& settings=globalContainer->settings;
    settings=Settings();
    REQUIRE(settings.gameSpeed==Settings::GAME_SPEED_NORMAL);
    REQUIRE(Turn::DEFAULT_TICK_RATE_MILLIHZ==1000 * GAME_TICKS_PER_SECOND);
    REQUIRE(settings.getGameSpeedStepDuration()==33);
    REQUIRE(settings.getGameSpeedStepDurationNs()==GAME_TICK_NS);
    REQUIRE(30 * settings.getGameSpeedStepDurationNs()==999999990ULL);
    REQUIRE(settings.getGameSpeedRenderInterval()==1);
    REQUIRE(settings.getGameSpeedText()=="1x");
    int previous=134;
    for(int i=Settings::GAME_SPEED_MINIMUM;i<=Settings::GAME_SPEED_MAXIMUM;++i) {
        settings.gameSpeed=i;
        REQUIRE(settings.getGameSpeedStepDuration()<previous);
        REQUIRE(settings.getGameSpeedRenderInterval()>=1);
        previous=settings.getGameSpeedStepDuration();
        if(i==1) REQUIRE(settings.getGameSpeedStepDurationNs()==26666666ULL);
        settings.save("speed-roundtrip.txt");
        Settings loaded; loaded.load("speed-roundtrip.txt");
        REQUIRE(loaded.gameSpeed==i);
    }
    REQUIRE(previous==0);
    for(int i=0;i<3;++i) {
        settings.gameSpeed=Settings::GAME_SPEED_MINIMUM+i;
        const int durations[]={133,66,44};
        const char* labels[]={"0.25x","0.5x","0.75x"};
        REQUIRE(settings.getGameSpeedStepDuration()==durations[i]);
        REQUIRE(settings.getGameSpeedRenderInterval()==1);
        REQUIRE(settings.getGameSpeedText()==labels[i]);
    }
    settings.gameSpeed=10;
    settings.changeGameSpeed(1); REQUIRE(settings.gameSpeed==10);
    settings.changeGameSpeed(-100); REQUIRE(settings.gameSpeed==Settings::GAME_SPEED_MINIMUM);
    settings.changeGameSpeed(-1); REQUIRE(settings.gameSpeed==Settings::GAME_SPEED_MINIMUM);
    const std::string profile=globalContainer->fileManager->getDir(0);
    for(int invalid:{-99,999}) {
        { std::ofstream f(profile+"/speed-invalid.txt"); f<<"gameSpeed="<<invalid<<"\n"; }
        Settings loaded; loaded.load("speed-invalid.txt");
        REQUIRE(loaded.gameSpeed==(invalid<0?Settings::GAME_SPEED_MINIMUM:10));
    }
    { std::ofstream f(profile+"/speed-legacy.txt"); f<<"musicVolume=70\n"; }
    Settings legacy; legacy.load("speed-legacy.txt"); REQUIRE(legacy.gameSpeed==0);
    std::cout<<"PASS: all presets, bounds, legacy settings, persistence\n";
}

TEST_CASE("settings screen; in-game slider; shortcuts and camera cadence [display][writes-preferences]")
{
    glob2test::ScopedEnvironment desktop("GLOB2_MOBILE_UI", "0");
    glob2test::HeadlessGlobals globals(displayOptions());
    auto& settings=globalContainer->settings;
    REQUIRE(NET_Init());
    {
        TestSettingsScreen screen;
        REQUIRE(screen.speed().number==screen.speedRow(Settings::GAME_SPEED_NORMAL));
        for(int speed=Settings::GAME_SPEED_MINIMUM;speed<0;++speed) {
            screen.select(speed);
            REQUIRE(settings.gameSpeed==speed);
            REQUIRE(screen.speed().value==settings.getGameSpeedText());
        }
        const int music=settings.musicVolume, voice=settings.voiceVolume;
        screen.select(10);
        REQUIRE(settings.gameSpeed==10);
        REQUIRE((settings.musicVolume==music && settings.voiceVolume==voice));
        REQUIRE(screen.speed().value=="Maximum");
        screen.selectCategory(SettingsScreen::Category::Controls);
        for(const auto& r:screen.rows()) REQUIRE(r.id!="gameplay.speed");
        screen.selectCategory(SettingsScreen::Category::Player);
        const int french=Toolkit::getStringTable()->getLangCode("fr");
        REQUIRE(screen.changeSetting("player.language",french));
        screen.selectCategory(SettingsScreen::Category::Gameplay);
        REQUIRE(screen.speed().value=="Maximale");
        screen.selectCategory(SettingsScreen::Category::Player);
        screen.changeSetting("player.language",Toolkit::getStringTable()->getLangCode("en"));
        screen.done();
        Settings loaded; loaded.load(); REQUIRE(loaded.gameSpeed==10);

    }
    {
        TestSettingsScreen screen;
        screen.select(7);
        screen.done();
        Settings loaded; loaded.load(); REQUIRE(loaded.gameSpeed==7);
    }
    {
        TestSettingsScreen screen;
        REQUIRE(screen.speed().number==screen.speedRow(7));
        REQUIRE(screen.speed().value=="8x");
    }
    {
        GameGUI gui;
        InGameOptionScreen screen(&gui);
        REQUIRE((screen.adjustableGameSpeed && settings.gameSpeed==7));
        screen.setGameSpeed(13);
        REQUIRE(settings.gameSpeed==10);
        REQUIRE(screen.gameSpeedText()=="Game speed: Maximum");
    }
    { Settings loaded; loaded.load(); REQUIRE(loaded.gameSpeed==10); }
    {
        GameGUI gui;
        auto map=Engine::loadMapHeader("maps/balanced.map");
        GameHeader header;
        header.setNumberOfPlayers(1);
        header.setRandomSeed(123456);
        header.getBasePlayer(0)=BasePlayer(0,"Test",0,BasePlayer::P_LOCAL);
        REQUIRE(gui.loadFromHeaders(map,header,true,true));
        gui.localPlayer=gui.localTeamNo=0;
        gui.adjustLocalTeam();
        // Camera dimensions come from the presented snapshot.
        gui.prepareLocalPresentation();
        gui.adjustInitialViewport();
        REQUIRE(gui.canChangeGameSpeed());
        SDL_Event key={}; key.type=SDL_EVENT_KEY_DOWN;
        key.key.key=SDLK_MINUS; key.key.mod=SDL_KMOD_CTRL;
        gui.processEvent(&key); REQUIRE(settings.gameSpeed==9);
        gui.game.gameHeader.getBasePlayer(0).type=BasePlayer::P_IP;
        REQUIRE(!gui.canChangeGameSpeed());
        gui.processEvent(&key); REQUIRE(settings.gameSpeed==9);
        {
            InGameOptionScreen screen(&gui);
            REQUIRE(!screen.adjustableGameSpeed);
            REQUIRE(screen.gameSpeedText()=="Game speed: 1x (multiplayer)");
            screen.setGameSpeed(0);
            REQUIRE(settings.gameSpeed==9);
        }
        globalContainer->replaying=true;
        REQUIRE(gui.canChangeGameSpeed());
        gui.processEvent(&key); REQUIRE(settings.gameSpeed==8);
        globalContainer->replaying=false;
        gui.game.gameHeader.getBasePlayer(0).type=BasePlayer::P_LOCAL;
        // The top bar's chevrons: a left click steps through 1x, 2x, 4x, 8x and
        // maximum and wraps; a right click steps back down.
        settings.gameSpeed=-2;
        REQUIRE(gui.litSpeedChevrons()==0);
        SDL_Event click={}; click.type=SDL_EVENT_MOUSE_BUTTON_DOWN;
        click.button.button=SDL_BUTTON_LEFT; click.button.x=gui.topBarSpeedX()+10; click.button.y=8;
        int lit=1;
        for(int expected:{0,3,5,7,10,0}) {
            gui.processEvent(&click);
            REQUIRE(settings.gameSpeed==expected);
            REQUIRE(gui.litSpeedChevrons()==(lit>5?1:lit)); ++lit;
        }
        { Settings loaded; loaded.load(); REQUIRE(loaded.gameSpeed==0); }
        settings.gameSpeed=6;
        click.button.button=SDL_BUTTON_RIGHT;
        for(int expected:{5,3,0,0}) { gui.processEvent(&click); REQUIRE(settings.gameSpeed==expected); }
        gui.game.gameHeader.getBasePlayer(0).type=BasePlayer::P_IP;
        click.button.button=SDL_BUTTON_LEFT;
        gui.processEvent(&click); REQUIRE(settings.gameSpeed==0);
        gui.game.gameHeader.getBasePlayer(0).type=BasePlayer::P_LOCAL;
        REQUIRE(gui.targetTickRate()==doctest::Approx(30));
        gui.game.gameHeader.getBasePlayer(0).type=BasePlayer::P_IP;
        gui.recordTick(SDL_GetTicks(), 40);
        REQUIRE(gui.targetTickRate()==25);
        gui.recordTick(SDL_GetTicks(), GAME_TICK_MS);
        REQUIRE(gui.targetTickRate()==doctest::Approx(1000.0 / GAME_TICK_MS));
        gui.game.gameHeader.getBasePlayer(0).type=BasePlayer::P_LOCAL;
        settings.gameSpeed=8;
        // Windowed edge scrolling is opt-in; changing it takes effect even
        // when the last motion event left an active edge velocity.
        REQUIRE_FALSE(bool(globalContainer->gfx->getOptionFlags() & GraphicContext::FULLSCREEN));
        SDL_Event edge{}; edge.type = SDL_EVENT_MOUSE_MOTION;
        edge.motion.x = 0; edge.motion.y = 200;
        Uint64 edgeTime = SDL_GetTicks();
        gui.step({edge}, edgeTime);
        const int stationaryX = gui.viewportX;
        gui.step({}, edgeTime + 80);
        REQUIRE(gui.viewportX == stationaryX);
        settings.edgeScrollWindowed = true;
        gui.step({edge}, edgeTime + 80);
        gui.step({}, edgeTime + 160);
        REQUIRE(gui.viewportX != stationaryX);
        settings.edgeScrollWindowed = false;
        const int stoppedX = gui.viewportX;
        gui.step({}, edgeTime + 240);
        REQUIRE(gui.viewportX == stoppedX);
        settings.edgeScrollWindowed = true;
        // The same elapsed time with different GUI call rates should scroll equally.
        int distance[2];
        for(int pass=0;pass<2;++pass) {
            SDL_Event mouse={}; mouse.type=SDL_EVENT_MOUSE_MOTION;
            mouse.motion.x=0; mouse.motion.y=200;
            const int before=gui.viewportX;
            const Uint64 start=SDL_GetTicks();
            // The scroll timer starts when the GUI is built; catch it up with the pointer
            // centred so a slow machine does not scroll the backlog on the first edge step.
            SDL_Event resting={}; resting.type=SDL_EVENT_MOUSE_MOTION;
            resting.motion.x=200; resting.motion.y=200;
            gui.step({resting}, start);
            gui.step({mouse}, start);
            const int cadence=pass==0?40:1;
            for(int elapsed=cadence;elapsed<=480;elapsed+=cadence)
                gui.step({}, start+elapsed);
            distance[pass]=(before-gui.viewportX)&gui.game.map.getMaskW();
        }
        std::cerr<<"Camera distances: "<<distance[0]<<"/"<<distance[1]<<std::endl;
        REQUIRE((distance[0]>=10 && distance[0]<=14));
        REQUIRE(std::abs(distance[0]-distance[1])<=2);
        SDL_Event centered{};
        centered.type = SDL_EVENT_MOUSE_MOTION;
        centered.motion.x = 200; centered.motion.y = 200;
        SDL_Event held{};
        held.type = SDL_EVENT_KEY_DOWN;
        held.key.key = SDLK_LEFT;
        held.key.scancode = SDL_SCANCODE_LEFT;
        const Uint64 inputStart = SDL_GetTicks() + 40;
        gui.step({centered, held}, inputStart);
        const int heldX = gui.viewportX;
        gui.step({}, inputStart + 40);
        REQUIRE(gui.viewportX == ((heldX - 1) & gui.game.map.getMaskW()));
        SDL_Event focus{};
        focus.type = SDL_EVENT_WINDOW_RESIZED;
        focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        gui.step({focus}, inputStart + 80);
        const int releasedX = gui.viewportX;
        gui.step({}, inputStart + 120);
        focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        gui.step({focus}, inputStart + 160);
        REQUIRE(gui.viewportX == releasedX);
        std::cout << "PASS: supplied input, held-key scrolling and focus cleanup\n";
        std::cout<<"PASS: multiplayer controls, replay eligibility, camera cadence "
                 <<distance[0]<<"/"<<distance[1]<<" cells\n";
    }
    KeyboardManager keyboard(GameGUIShortcuts); keyboard.loadDefaultShortcuts();
    SDL_KeyboardEvent key={}; key.key=SDLK_EQUALS; key.mod=SDL_KMOD_CTRL;
    REQUIRE(keyboard.getAction(KeyPress(key,true))==GameGUIKeyActions::IncreaseGameSpeed);
    key.key=SDLK_MINUS;
    REQUIRE(keyboard.getAction(KeyPress(key,true))==GameGUIKeyActions::DecreaseGameSpeed);
    std::cout<<"PASS: main menu presets, language refresh, categories, automatic saving, reopening, in-game slider, shortcuts\n";
    NET_Quit();
}

TEST_CASE("live engine speed; pause; hard pause and replay playback [display][writes-preferences]")
{
    glob2test::HeadlessGlobals globals(displayOptions());
    auto& settings=globalContainer->settings;
    REQUIRE(NET_Init());
    KeyboardManager keyboard(GameGUIShortcuts); keyboard.loadDefaultShortcuts();
    std::string output;
    {
        // The engine prints one simulation checksum per run; compare them below.
        glob2test::CapturedStdout captured;
        Uint64 normal=0, maximum=0;
        for(int speed:{0,10}) {
            settings.gameSpeed=speed;
            Engine engine;
            REQUIRE(engine.initCampaign("maps/balanced.map")==Engine::EE_NO_ERROR);
            globalContainer->automaticEndingGame=true;
            globalContainer->automaticEndingSteps=50;
            globalContainer->automaticGameGlobalEndConditions=true;
            Uint64 start=SDL_GetTicks();
            engine.run();
            const Uint64 elapsed=SDL_GetTicks()-start;
            if(speed==0) normal=elapsed; else maximum=elapsed;
            std::cout<<"Engine speed="<<speed<<" elapsed="<<elapsed<<"ms\n";
        }
        REQUIRE((normal>=1500 && maximum<normal));
        std::cout<<"PASS: live engine runs faster at Maximum\n";
        // Exercise hard pause through a configurable, portable shortcut.
        KeyboardShortcut hardPause;
        hardPause.interpret("<f12>=hard pause",GameGUIShortcuts);
        keyboard.getKeyboardShortcuts().push_back(hardPause);
        keyboard.saveKeyboardLayout();
        for(SDL_Keycode key:{SDLK_P,SDLK_F12}) {
            settings.gameSpeed=10;
            Engine engine;
            REQUIRE(engine.initCampaign("maps/balanced.map")==Engine::EE_NO_ERROR);
            resumeGame(&key,0,0);
            const SDL_TimerID timer=SDL_AddTimer(240,resumeGame,&key);
            REQUIRE(timer);
            const Uint64 start=SDL_GetTicks();
            engine.run();
            SDL_RemoveTimer(timer);
            const Uint64 elapsed=SDL_GetTicks()-start;
            std::cout<<"Pause key="<<key<<" elapsed="<<elapsed<<"ms"<<std::endl;
            REQUIRE_MESSAGE((elapsed>=200 && elapsed<3000),
                "pause key=" << key << " elapsed=" << elapsed << "ms");
        }
        std::cout<<"PASS: pause and hard pause accept resume input at Maximum\n";
        // The last run recorded a replay; stop playback before its end screen.
        Uint64 replayNormal=0;
        for(int mode=0;mode<3;++mode) {
            settings.gameSpeed=mode==1?10:0;
            Engine engine;
            REQUIRE(engine.loadReplay("replays/last_game.replay")==Engine::EE_NO_ERROR);
            std::cerr<<"Replay length: "<<globalContainer->replayReader->getNumStepsTotal()<<std::endl;
            globalContainer->replayFastForward=mode==2;
            globalContainer->automaticEndingSteps=25;
            const Uint64 start=SDL_GetTicks();
            engine.run();
            const Uint64 elapsed=SDL_GetTicks()-start;
            std::cout<<"Replay mode="<<mode<<" elapsed="<<elapsed<<"ms\n";
            if(mode==0) { REQUIRE(elapsed>=800); replayNormal=elapsed; }
            // Compare the same replay on this renderer/build: instrumented
            // decoding and drawing can exceed a fixed throughput deadline.
            else REQUIRE_MESSAGE(elapsed<replayNormal,
                "accelerated replay mode="<<mode<<" took "<<elapsed
                <<"ms versus normal "<<replayNormal<<"ms");
        }
        std::cout<<"PASS: replay playback at 1x, Maximum and fast-forward\n";
        output=captured.text();
    }
    std::cout<<output;
    std::vector<std::string> checksums;
    const std::regex pattern("nox::gui\\.game\\.checkSum\\(\\) = ([0-9a-f]+)");
    for(std::sregex_iterator it(output.begin(),output.end(),pattern),end;it!=end;++it)checksums.push_back((*it)[1]);
    REQUIRE_MESSAGE(checksums.size()==7, "missing engine checksums");
    REQUIRE_MESSAGE(std::set<std::string>(checksums.begin(),checksums.begin()+4).size()==1, "speed or pause changed the game state");
    REQUIRE_MESSAGE(std::set<std::string>(checksums.begin()+4,checksums.end()).size()==1, "playback speed changed the replay state");
    NET_Quit();
}
}
