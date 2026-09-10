// SPDX-License-Identifier: GPL-3.0-or-later
#include "GlobalContainer.h"
#include "SettingsScreen.h"
#include "FrontendTheme.h"
#include "GameGUIKeyActions.h"
#include "GameGUIDialog.h"
#include "KeyboardManager.h"
#include "FileManager.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <SDL_net.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

GlobalContainer* globalContainer=nullptr;
using namespace GAGCore;
using namespace GAGGUI;
struct NativeSettings : SettingsScreen {
    bool failNextDisplay=false;
    NativeSettings(){gfx=globalContainer->gfx;dispatchInit();}
    bool applyDisplayMode(int w,int h,Uint32 flags) override {
        if(failNextDisplay){failNextDisplay=false;return false;}
        return SettingsScreen::applyDisplayMode(w,h,flags);
    }
    Row row(const std::string& id){for(auto r:rows())if(r.id==id)return r;assert(false);return {};}
    void key(SDL_Keycode k,Uint16 modifiers=0){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=k;e.key.keysym.mod=modifiers;onSDLEvent(&e);}
    void click(const std::string& id){auto r=row(id);SDL_Event e{};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;e.button.x=r.control.x+10;e.button.y=r.control.y+10;onSDLEvent(&e);}
    void capture(const std::string& path){dispatchPaint();dispatchPaint();const auto name=std::filesystem::path(path).filename().string();
        globalContainer->gfx->printScreen(name);
        std::filesystem::copy_file(Toolkit::getFileManager()->getDir(0)+"/"+name,path,std::filesystem::copy_options::overwrite_existing);}
};
static std::string readFile(const std::string& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
static void testDropdown()
{
    Dropdown menu;auto* font=Toolkit::getFont("standard");
    auto key=[&](SDL_Keycode k){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=k;return menu.handleEvent(e);};
    const SDL_Rect available{16,16,608,380},anchor{200,110,180,36};
    menu.open(anchor,available,{"Windowed","Fullscreen"},0,font);
    assert(menu.isOpen() && menu.bounds().y==anchor.y+anchor.h+2);
    key(SDLK_DOWN);assert(menu.isOpen() && menu.highlighted()==1);
    assert(key(SDLK_RETURN)==1 && !menu.isOpen());
    menu.open(anchor,available,{"Windowed","Fullscreen"},0,font);
    key(SDLK_ESCAPE);assert(!menu.isOpen());
    menu.open(anchor,available,{"Windowed","Fullscreen"},0,font);
    SDL_Event click{};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_LEFT;click.button.x=18;click.button.y=18;
    assert(menu.handleEvent(click)==-1 && !menu.isOpen());
    menu.open(anchor,available,{"Windowed","Fullscreen"},0,font);
    auto item=menu.itemBounds(1);click.button.x=item.x+8;click.button.y=item.y+8;
    assert(menu.handleEvent(click)==1 && !menu.isOpen());
    std::vector<std::string> choices;for(int i=0;i<80;++i)choices.push_back("A deliberately long translated option to exercise wrapping "+std::to_string(i));
    menu.open({420,340,180,36},available,choices,79,font);
    auto box=menu.bounds();assert(box.x>=available.x && box.y>=available.y && box.x+box.w<=624 && box.y+box.h<=396);
    item=menu.itemBounds(79);assert(item.y>=box.y && item.y+item.h<=box.y+box.h);
    key(SDLK_HOME);assert(menu.highlighted()==0);item=menu.itemBounds(0);assert(item.y>=box.y);
    SDL_Event wheel{};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.y=-2;menu.handleEvent(wheel);
    assert(menu.isOpen() && menu.highlighted()==0 && menu.itemBounds(0).y<item.y);
    key(SDLK_END);assert(key(SDLK_RETURN)==79);
}
int main(int argc,char** argv)
{
    assert(argc==6 && std::string(argv[1]).find("glob2-settings-test-")==0);
    globalContainer=new GlobalContainer(argv[1]);
    auto& s=globalContainer->settings;
    s.screenWidth=std::stoi(argv[2]);s.screenHeight=std::stoi(argv[3]);
    s.screenFlags=std::string(argv[4])=="gl"?GraphicContext::USEGPU:0;
    s.mute=true;s.language="en";s.defaultFlagRadius[0]=0;globalContainer->load();assert(SDLNet_Init()==0);
    const auto profile=Toolkit::getFileManager()->getDir(0);
    const auto originalArtwork=s.highResolutionArtwork;
    if(auto* window=SDL_GL_GetCurrentWindow()){
        int ww,wh,dw,dh;SDL_GetWindowSize(window,&ww,&wh);SDL_GL_GetDrawableSize(window,&dw,&dh);
        std::cout<<"Window "<<ww<<"x"<<wh<<", drawable "<<dw<<"x"<<dh<<"\n";
    }
    std::filesystem::create_directories(argv[5]);
    {
        FrontendTheme theme;
        FrontendScope frontend;
        testDropdown();
        NativeSettings screen;
        for(int category=0;category<6;++category){
            screen.selectCategory(SettingsScreen::Category(category));
            for(const auto& r:screen.rows()){
                if(r.id.empty())continue;
                assert(r.control.w>0 && r.control.h>0);
                assert(r.control.x>=r.bounds.x && r.control.x+r.control.w<=r.bounds.x+r.bounds.w);
                assert(r.control.y>=r.bounds.y && r.control.y+r.control.h<=r.bounds.y+r.bounds.h);
            }
            screen.capture(std::string(argv[5])+"/category-"+std::to_string(category)+".bmp");
        }
        screen.selectCategory(SettingsScreen::Category::Display);
        const auto windowMode=screen.row("display.mode").number;
        screen.activateSetting("display.mode");
        assert(screen.row("graphics.torus").kind==SettingsScreen::Kind::Toggle);
        screen.capture(std::string(argv[5])+"/window-mode-dropdown.bmp");
        screen.key(SDLK_DOWN);screen.key(SDLK_ESCAPE);
        assert(screen.row("display.mode").number==windowMode && !screen.displayConfirmationPending());
        screen.activateSetting("display.resolution");
        screen.capture(std::string(argv[5])+"/resolution-dropdown.bmp");screen.key(SDLK_ESCAPE);
        screen.selectCategory(SettingsScreen::Category::Gameplay);
        for(const auto& row:screen.rows())assert(row.id!="graphics.torus" && row.id!="gameplay.torus");
        screen.selectCategory(SettingsScreen::Category::Buildings);
        const auto defaults=s.defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][1];
        for(int tab=0;tab<4;++tab){screen.activateSetting("buildings.tab."+std::to_string(tab));screen.capture(std::string(argv[5])+"/buildings-"+std::to_string(tab)+".bmp");}
        assert(s.defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][1]==defaults);
        assert(s.defaultFlagRadius[0]==0);
        assert(screen.row("radius.0").value=="Default");
        assert(screen.changeSetting("radius.0",3));
        assert(s.defaultFlagRadius[0]==3 && s.defaultFlagRadius[1]==4);
        screen.selectCategory(SettingsScreen::Category::Audio);
        assert(!screen.row("audio.music").enabled);
        screen.click("audio.mute");assert(!s.mute);
        screen.key(SDLK_TAB);screen.key(SDLK_RIGHT);screen.onTimer(SDL_GetTicks()+400);
        assert(screen.changeSetting("audio.music",123));
        screen.finishInteraction();Settings loaded;loaded.load();assert(loaded.musicVolume==123 && !loaded.mute);
        screen.selectCategory(SettingsScreen::Category::Display);
        s.optionFlags|=0x80;assert(screen.changeSetting("graphics.detail",1));assert(s.optionFlags & 0x80);
        assert(screen.changeSetting("graphics.detail",0));assert(s.optionFlags==0x80);
        assert(screen.row("graphics.renderer").kind==SettingsScreen::Kind::Choice);
        screen.activateSetting("graphics.renderer");screen.key(SDLK_ESCAPE);
        screen.capture(std::string(argv[5])+"/graphics-renderer.bmp");
        if(s.screenFlags & GraphicContext::USEGPU){
            assert(screen.changeSetting("graphics.renderer",0));assert(screen.restartRequired());
            loaded.load();assert(!(loaded.screenFlags & GraphicContext::USEGPU));
            assert(screen.changeSetting("graphics.renderer",1));assert(!screen.restartRequired());
        }else{
            int oldWidth=s.screenWidth;
            auto resolution=screen.row("display.resolution");int index=-1;
            for(size_t i=0;i<resolution.choices.size();++i)if(resolution.choices[i].rfind("800 × 600",0)==0)index=i;
            assert(index>=0);
            screen.failNextDisplay=true;assert(screen.changeSetting("display.resolution",index));
            assert(!screen.displayConfirmationPending() && globalContainer->gfx->getW()==oldWidth);
            assert(screen.changeSetting("display.resolution",index));
            assert(screen.displayConfirmationPending());assert(s.screenWidth==oldWidth);
            screen.capture(std::string(argv[5])+"/display-confirm.bmp");
            screen.confirmDisplay(false);assert(globalContainer->gfx->getW()==oldWidth);
            screen.changeSetting("display.resolution",index);screen.onTimer(SDL_GetTicks()+16000);
            assert(!screen.displayConfirmationPending() && globalContainer->gfx->getW()==oldWidth);
            screen.changeSetting("display.resolution",index);screen.confirmDisplay(true);loaded.load();assert(loaded.screenWidth==800);
        }
        screen.selectCategory(SettingsScreen::Category::Gameplay);
        assert(screen.changeSetting("gameplay.speed",4));loaded.load();assert(loaded.gameSpeed==4);
        const auto before=readFile(profile+"/preferences.txt");
        const auto permissions=std::filesystem::status(profile+"/preferences.txt").permissions();
        const auto directoryTarget=profile+"/atomic-directory";
        std::filesystem::create_directory(directoryTarget);
        assert(!Toolkit::getFileManager()->writeFileAtomic(directoryTarget,"must not replace directory"));
        assert(std::filesystem::is_directory(directoryTarget));
        std::filesystem::create_directory(profile+"/preferences.txt.tmp");
        assert(screen.changeSetting("gameplay.speed",5));assert(screen.saveFailed());assert(readFile(profile+"/preferences.txt")==before);
        screen.capture(std::string(argv[5])+"/save-error.bmp");
        std::filesystem::remove(profile+"/preferences.txt.tmp");screen.finishInteraction();assert(!screen.saveFailed());loaded.load();assert(loaded.gameSpeed==5);
        assert(std::filesystem::status(profile+"/preferences.txt").permissions()==permissions);
        screen.selectCategory(SettingsScreen::Category::Player);
        screen.activateSetting("player.name");screen.key(SDLK_a,KMOD_CTRL);
        SDL_Event input{};input.type=SDL_TEXTINPUT;strcpy(input.text.text,"New player");screen.onSDLEvent(&input);screen.key(SDLK_RETURN);
        loaded.load();assert(loaded.getUsername()=="New player");
        screen.activateSetting("player.name");screen.key(SDLK_BACKSPACE);screen.key(SDLK_ESCAPE);assert(s.getUsername()=="New player");
        screen.changeSetting("player.language",Toolkit::getStringTable()->getLangCode("fr"));
        for(int c=0;c<6;++c){screen.selectCategory(SettingsScreen::Category(c));screen.capture(std::string(argv[5])+"/french-"+std::to_string(c)+".bmp");}
        screen.selectCategory(SettingsScreen::Category::Player);screen.changeSetting("player.language",Toolkit::getStringTable()->getLangCode("en"));
        screen.selectCategory(SettingsScreen::Category::Controls);
        screen.activateSetting("keys.add.0");screen.key(SDLK_F12);screen.activateSetting("binding.save");
        KeyboardManager check(GameGUIShortcuts);bool found=false;for(const auto& b:check.getKeyboardShortcuts())found|=b.getKeyPress(0).getKey()=="f12";assert(found);
        screen.activateSetting("keys.add.1");screen.key(SDLK_F12);screen.activateSetting("binding.save");
        assert(screen.row("conflict.replace").enabled);screen.capture(std::string(argv[5])+"/key-conflict.bmp");
        screen.activateSetting("conflict.cancel");screen.activateSetting("binding.cancel");
        screen.activateSetting("keys.add.1");screen.key(SDLK_F11);screen.activateSetting("binding.advanced");screen.activateSetting("binding.addkey");screen.key(SDLK_F10);
        screen.activateSetting("binding.addkey");screen.key(SDLK_F9);
        assert(screen.changeSetting("binding.trigger.2",1));
        screen.capture(std::string(argv[5])+"/key-sequence.bmp");screen.activateSetting("binding.save");
        check=KeyboardManager(GameGUIShortcuts);found=false;for(const auto& b:check.getKeyboardShortcuts())if(b.getKeyPress(0).getKey()=="f11"){assert(b.getKeyPressCount()==3 && !b.getKeyPress(2).getPressed());found=true;}assert(found);
        // A single-key prefix must conflict with the saved three-key sequence.
        screen.activateSetting("keys.add.2");screen.key(SDLK_F11);screen.activateSetting("binding.save");
        assert(screen.row("conflict.replace").enabled);screen.activateSetting("conflict.replace");
        check=KeyboardManager(GameGUIShortcuts);int matches=0;
        for(const auto& b:check.getKeyboardShortcuts())if(b.getKeyPress(0).getKey()=="f11"){++matches;assert(b.getAction()==2 && b.getKeyPressCount()==1);}assert(matches==1);
        const std::string keyFile=GameGUIKeyActions::getConfigurationFile();
        const auto savedKeys=readFile(profile+"/"+keyFile);
        std::filesystem::create_directory(profile+"/"+keyFile+".tmp");
        screen.activateSetting("keys.add.3");screen.key(SDLK_F8);screen.activateSetting("binding.save");
        assert(screen.saveFailed() && readFile(profile+"/"+keyFile)==savedKeys);
        std::filesystem::remove(profile+"/"+keyFile+".tmp");screen.finishInteraction();assert(!screen.saveFailed());
        screen.activateSetting("keys.mode.1");screen.capture(std::string(argv[5])+"/editor-shortcuts.bmp");
        assert(s.highResolutionArtwork==originalArtwork);
        screen.selectCategory(SettingsScreen::Category::Display);
        screen.changeSetting("graphics.artwork",!originalArtwork);
        Settings saved;saved.load();assert(saved.highResolutionArtwork==!originalArtwork);
        screen.changeSetting("graphics.artwork",originalArtwork);
        screen.selectCategory(SettingsScreen::Category::Display);
        const bool previousTorus=s.automaticTorus;
        screen.changeSetting("graphics.torus",!previousTorus);
        saved.load();assert(saved.automaticTorus==!previousTorus);
        screen.changeSetting("graphics.torus",previousTorus);
        screen.done();
    }
    std::cout<<"PASS: layout, persistence, automatic saving, display confirmation, building defaults, bindings, localization\n";
    delete globalContainer;SDLNet_Quit();return 0;
}
