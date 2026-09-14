// SPDX-License-Identifier: GPL-3.0-or-later
// Run via test/run-game-speed-tests.py; requires a working OpenGL display.
#include "GlobalContainer.h"
#include "SettingsScreen.h"
#include "GameGUIDialog.h"
#include "Engine.h"
#include "KeyboardManager.h"
#include "ReplayReader.h"
#include "GameGUIKeyActions.h"
#include <GUISelector.h>
#include <GUIButton.h>
#include <GUIText.h>
#include <GUIList.h>
#include <StringTable.h>
#include <SDL_net.h>
#include <cassert>
#include <fstream>
#include <iostream>

GlobalContainer *globalContainer = NULL;
using namespace GAGGUI;

struct TestSettingsScreen : SettingsScreen {
    TestSettingsScreen() { gfx=globalContainer->gfx; dispatchInit(); selectCategory(Category::Gameplay); }
    Row speed() { for(const auto& r:rows()) if(r.id=="gameplay.speed") return r; assert(false); return {}; }
    // The presets start below 1x, so a speed value and its row index differ by
    // GAME_SPEED_MINIMUM. Callers pass the speed; the row index is derived.
    void select(int value) { assert(changeSetting("gameplay.speed",value-Settings::GAME_SPEED_MINIMUM)); }
    int speedRow(int value) const { return value-Settings::GAME_SPEED_MINIMUM; }
};

static Uint32 resumeGame(Uint32, void* data) {
    SDL_Event event={};
    event.type=SDL_KEYDOWN;
    event.key.keysym.sym=*static_cast<SDL_Keycode*>(data);
    SDL_PushEvent(&event);
    return 0;
}

int main(int argc, char** argv) {
    assert((argc==2 || (argc==3 && std::string(argv[2])=="--settings-only")) && std::string(argv[1]).find("glob2-speed-test-")==0);
    globalContainer=new GlobalContainer(argv[1]);
    auto& settings=globalContainer->settings;
    settings=Settings();
    assert(settings.gameSpeed==Settings::GAME_SPEED_NORMAL);
    assert(settings.getGameSpeedStepDuration()==40);
    assert(settings.getGameSpeedRenderInterval()==1);
    assert(settings.getGameSpeedText()=="1x");
    int previous=161;
    for(int i=Settings::GAME_SPEED_MINIMUM;i<=Settings::GAME_SPEED_MAXIMUM;++i) {
        settings.gameSpeed=i;
        assert(settings.getGameSpeedStepDuration()<previous);
        assert(settings.getGameSpeedRenderInterval()>=1);
        previous=settings.getGameSpeedStepDuration();
        settings.save("speed-roundtrip.txt");
        Settings loaded; loaded.load("speed-roundtrip.txt");
        assert(loaded.gameSpeed==i);
    }
    assert(previous==0);
    for(int i=0;i<3;++i) {
        settings.gameSpeed=i-3;
        const int durations[]={160,80,53};
        const char* labels[]={"0.25x","0.5x","0.75x"};
        assert(settings.getGameSpeedStepDuration()==durations[i]);
        assert(settings.getGameSpeedRenderInterval()==1);
        assert(settings.getGameSpeedText()==labels[i]);
    }
    settings.gameSpeed=10;
    settings.changeGameSpeed(1); assert(settings.gameSpeed==10);
    settings.changeGameSpeed(-100); assert(settings.gameSpeed==-3);
    settings.changeGameSpeed(-1); assert(settings.gameSpeed==-3);
    const std::string profile=globalContainer->fileManager->getDir(0);
    for(int invalid:{-99,999}) {
        { std::ofstream f(profile+"/speed-invalid.txt"); f<<"gameSpeed="<<invalid<<"\n"; }
        Settings loaded; loaded.load("speed-invalid.txt");
        assert(loaded.gameSpeed==(invalid<0?-3:10));
    }
    { std::ofstream f(profile+"/speed-legacy.txt"); f<<"musicVolume=70\n"; }
    Settings legacy; legacy.load("speed-legacy.txt"); assert(legacy.gameSpeed==0);
    std::cout<<"PASS: all presets, bounds, legacy settings, persistence\n";

    settings.screenWidth=640; settings.screenHeight=480;
    settings.screenFlags=GraphicContext::USEGPU; settings.mute=true; settings.gameSpeed=0;
    globalContainer->load();
    assert(SDLNet_Init()==0);
    {
        TestSettingsScreen screen;
        assert(screen.speed().number==screen.speedRow(Settings::GAME_SPEED_NORMAL));
        for(int speed=Settings::GAME_SPEED_MINIMUM;speed<0;++speed) {
            screen.select(speed);
            assert(settings.gameSpeed==speed);
            assert(screen.speed().value==settings.getGameSpeedText());
        }
        const int music=settings.musicVolume, voice=settings.voiceVolume;
        screen.select(10);
        assert(settings.gameSpeed==10);
        assert(settings.musicVolume==music && settings.voiceVolume==voice);
        assert(screen.speed().value=="Maximum");
        screen.selectCategory(SettingsScreen::Category::Controls);
        for(const auto& r:screen.rows()) assert(r.id!="gameplay.speed");
        screen.selectCategory(SettingsScreen::Category::Player);
        const int french=Toolkit::getStringTable()->getLangCode("fr");
        assert(screen.changeSetting("player.language",french));
        screen.selectCategory(SettingsScreen::Category::Gameplay);
        assert(screen.speed().value=="Maximale");
        screen.selectCategory(SettingsScreen::Category::Player);
        screen.changeSetting("player.language",Toolkit::getStringTable()->getLangCode("en"));
        screen.done();
        Settings loaded; loaded.load(); assert(loaded.gameSpeed==10);

    }
    {
        TestSettingsScreen screen;
        screen.select(7);
        screen.done();
        Settings loaded; loaded.load(); assert(loaded.gameSpeed==7);
    }
    {
        TestSettingsScreen screen;
        assert(screen.speed().number==screen.speedRow(7));
        assert(screen.speed().value=="8x");
    }
    {
        GameGUI gui;
        InGameOptionScreen screen(&gui);
        assert(screen.gameSpeed->getValue()==10);
        screen.gameSpeed->setValue(13);
        screen.onAction(screen.gameSpeed, VALUE_CHANGED, 10, 0);
        assert(settings.gameSpeed==10);
        assert(screen.gameSpeedText->getText()=="Game speed: Maximum");
    }
    { Settings loaded; loaded.load(); assert(loaded.gameSpeed==10); }
    {
        GameGUI gui;
        auto map=Engine::loadMapHeader("maps/balanced.map");
        GameHeader header;
        header.setNumberOfPlayers(1);
        header.setRandomSeed(123456);
        header.getBasePlayer(0)=BasePlayer(0,"Test",0,BasePlayer::P_LOCAL);
        assert(gui.loadFromHeaders(map,header,true,true));
        gui.localPlayer=gui.localTeamNo=0;
        gui.adjustLocalTeam();
        gui.adjustInitialViewport();
        assert(gui.canChangeGameSpeed());
        SDL_Event key={}; key.type=SDL_KEYDOWN;
        key.key.keysym.sym=SDLK_MINUS; key.key.keysym.mod=KMOD_CTRL;
        gui.processEvent(&key); assert(settings.gameSpeed==9);
        gui.game.gameHeader.getBasePlayer(0).type=BasePlayer::P_IP;
        assert(!gui.canChangeGameSpeed());
        gui.processEvent(&key); assert(settings.gameSpeed==9);
        {
            InGameOptionScreen screen(&gui);
            assert(!screen.gameSpeed->visible);
            assert(screen.gameSpeedText->getText()=="Game speed: 1x (multiplayer)");
            screen.gameSpeed->setValue(0);
            screen.onAction(screen.gameSpeed,VALUE_CHANGED,0,0);
            assert(settings.gameSpeed==9);
        }
        globalContainer->replaying=true;
        assert(gui.canChangeGameSpeed());
        gui.processEvent(&key); assert(settings.gameSpeed==8);
        globalContainer->replaying=false;
        gui.game.gameHeader.getBasePlayer(0).type=BasePlayer::P_LOCAL;
        // Same wall time with very different GUI call rates should scroll equally.
        int distance[2];
        for(int pass=0;pass<2;++pass) {
            // Drain startup/window events and the preceding pass before measuring.
            gui.step();
            SDL_Event mouse={}; mouse.type=SDL_MOUSEMOTION;
            mouse.motion.x=0; mouse.motion.y=200;
            gui.processEvent(&mouse);
            const int before=gui.viewportX;
            const Uint64 start=SDL_GetTicks64();
            while(SDL_GetTicks64()-start<480) {
                SDL_PushEvent(&mouse);
                gui.step();
                SDL_Delay(pass==0?40:1);
            }
            // Account for the final sleep in both cadence measurements.
            SDL_PushEvent(&mouse);
            gui.step();
            distance[pass]=(before-gui.viewportX)&gui.game.map.getMaskW();
        }
        std::cerr<<"Camera distances: "<<distance[0]<<"/"<<distance[1]<<std::endl;
        assert(distance[0]>=10 && distance[0]<=14);
        assert(std::abs(distance[0]-distance[1])<=2);
        std::cout<<"PASS: multiplayer controls, replay eligibility, camera cadence "
                 <<distance[0]<<"/"<<distance[1]<<" cells\n";
    }
    KeyboardManager keyboard(GameGUIShortcuts); keyboard.loadDefaultShortcuts();
    SDL_Keysym key={}; key.sym=SDLK_EQUALS; key.mod=KMOD_CTRL;
    assert(keyboard.getAction(KeyPress(key,true))==GameGUIKeyActions::IncreaseGameSpeed);
    key.sym=SDLK_MINUS;
    assert(keyboard.getAction(KeyPress(key,true))==GameGUIKeyActions::DecreaseGameSpeed);
    std::cout<<"PASS: main menu presets, language refresh, categories, automatic saving, reopening, in-game slider, shortcuts\n";
    if(argc==3){delete globalContainer;SDLNet_Quit();return 0;}
    Uint64 normal=0, maximum=0;
    for(int speed:{0,10}) {
        settings.gameSpeed=speed;
        Engine engine;
        assert(engine.initCampaign("maps/balanced.map")==Engine::EE_NO_ERROR);
        globalContainer->automaticEndingGame=true;
        globalContainer->automaticEndingSteps=50;
        globalContainer->automaticGameGlobalEndConditions=true;
        Uint64 start=SDL_GetTicks64();
        engine.run();
        const Uint64 elapsed=SDL_GetTicks64()-start;
        if(speed==0) normal=elapsed; else maximum=elapsed;
        std::cout<<"Engine speed="<<speed<<" elapsed="<<elapsed<<"ms\n";
    }
    assert(normal>=1500 && maximum<normal);
    std::cout<<"PASS: live engine runs faster at Maximum\n";
    // Exercise hard pause through a configurable, portable shortcut.
    KeyboardShortcut hardPause;
    hardPause.interpret("<f12>=hard pause",GameGUIShortcuts);
    keyboard.getKeyboardShortcuts().push_back(hardPause);
    keyboard.saveKeyboardLayout();
    for(SDL_Keycode key:{SDLK_p,SDLK_F12}) {
        settings.gameSpeed=10;
        Engine engine;
        assert(engine.initCampaign("maps/balanced.map")==Engine::EE_NO_ERROR);
        resumeGame(0,&key);
        const SDL_TimerID timer=SDL_AddTimer(240,resumeGame,&key);
        assert(timer);
        const Uint64 start=SDL_GetTicks64();
        engine.run();
        SDL_RemoveTimer(timer);
        const Uint64 elapsed=SDL_GetTicks64()-start;
        std::cout<<"Pause key="<<key<<" elapsed="<<elapsed<<"ms"<<std::endl;
        assert(elapsed>=200 && elapsed<3000);
    }
    std::cout<<"PASS: pause and hard pause accept resume input at Maximum\n";
    // The last run recorded a replay; stop playback before its end screen.
    for(int mode=0;mode<3;++mode) {
        settings.gameSpeed=mode==1?10:0;
        Engine engine;
        assert(engine.loadReplay("replays/last_game.replay")==Engine::EE_NO_ERROR);
        std::cerr<<"Replay length: "<<globalContainer->replayReader->getNumStepsTotal()<<std::endl;
        globalContainer->replayFastForward=mode==2;
        globalContainer->automaticEndingSteps=25;
        const Uint64 start=SDL_GetTicks64();
        engine.run();
        const Uint64 elapsed=SDL_GetTicks64()-start;
        if(mode==0) assert(elapsed>=800);
        else assert(elapsed<800);
    }
    std::cout<<"PASS: replay playback at 1x, Maximum and fast-forward\n";
    delete globalContainer;
    SDLNet_Quit();
}
