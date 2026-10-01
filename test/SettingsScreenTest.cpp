// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include <vector>
#include <string>
#include <utility>
#include <iterator>
#include "GlobalContainer.h"
#include "SettingsScreen.h"
#include "FrontendTheme.h"
#include "GameGUIKeyActions.h"
#include "KeyboardManager.h"
#include "FileManager.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <SDL_net.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <regex>

using namespace GAGCore;
using namespace GAGGUI;
namespace
{
struct NativeSettings : SettingsScreen {
    bool failNextDisplay=false;
    NativeSettings(){beginExecution(globalContainer->gfx);}
    bool applyDisplayMode(int w,int h,Uint32 flags) override {
        if(failNextDisplay){failNextDisplay=false;return false;}
        return SettingsScreen::applyDisplayMode(w,h,flags);
    }
    Row row(const std::string& id){for(auto r:rows())if(r.id==id)return r;REQUIRE_MESSAGE(false, "no settings row "<<id);return {};}
    void event(SDL_Event e){handleExecutionEvent(e);}
    void key(SDL_Keycode k,Uint16 modifiers=0){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=k;e.key.keysym.mod=modifiers;handleExecutionEvent(e);}
    void click(const std::string& id){auto r=row(id);host().tapAt({r.control.x+10,r.control.y+10});}
    void capture(const std::string& path){paintFrame(SDL_GetTicks());const auto name=std::filesystem::path(path).filename().string();
        globalContainer->gfx->printScreen(name);paintFrame(SDL_GetTicks());globalContainer->gfx->nextFrame();
        std::filesystem::copy_file(Toolkit::getFileManager()->getDir(0)+"/"+name,path,std::filesystem::copy_options::overwrite_existing);}
};
static std::string readFile(const std::string& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
// The expanded-wording matrix entry: lengthen every settings string in the disposable
// profile's English table so long labels have to wrap and clip correctly.
static void writeExpandedStrings()
{
    std::ifstream source(glob2test::sourceRoot()/"data/texts.en.txt");
    std::vector<std::string> lines;
    for(std::string line;std::getline(source,line);)lines.push_back(line);
    const std::regex slot("%[0-9]");
    for(size_t i=0;i+1<lines.size();i+=2)
        if(lines[i].rfind("[settings ",0)==0 && lines[i+1].size()>12)
            // Expand the wording without duplicating substitution slots:
            // StringTable requires the key's placeholder count to match.
            lines[i+1]+=" \xE2\x80\x94 "+std::regex_replace(lines[i+1],slot,"value");
    const auto destination=glob2test::profileDir()/"data/texts.en.txt";
    std::filesystem::create_directories(destination.parent_path());
    std::ofstream out(destination);
    for(const auto& line:lines)out<<line<<'\n';
}

static void run(int width,int height,bool gl,bool expanded)
{
    if(expanded)writeExpandedStrings();
    glob2test::GlobalsOptions options{.display=true,.loadStrings=true,.width=width,.height=height,.screenFlags=gl?Uint32(GraphicContext::USEGPU):0u};
    options.beforeLoad=[](GlobalContainer& globals){globals.settings.language="en";globals.settings.defaultFlagRadius[0]=0;};
    glob2test::HeadlessGlobals globals(options);
    auto& s=globalContainer->settings;
    REQUIRE(SDLNet_Init()==0);
    const std::string output=glob2test::artifactDir().string();
    const auto profile=Toolkit::getFileManager()->getDir(0);
    const auto originalArtwork=s.highResolutionArtwork;
    if(auto* window=SDL_GL_GetCurrentWindow()){
        int ww,wh,dw,dh;SDL_GetWindowSize(window,&ww,&wh);SDL_GL_GetDrawableSize(window,&dw,&dh);
        std::cout<<"Window "<<ww<<"x"<<wh<<", drawable "<<dw<<"x"<<dh<<"\n";
    }
    {
        FrontendTheme theme;
        FrontendScope frontend;
        NativeSettings screen;
        for(auto category:screen.visibleCategories()){
            screen.selectCategory(category);
            for(const auto& r:screen.rows()){
                if(r.id.empty())continue;
                if(!(r.control.w>0 && r.control.h>0)){std::cerr<<"row without bounds: "<<r.id<<"\n";
                    std::function<void(GAGGUI::ui::Node&,int)> dump=[&](GAGGUI::ui::Node& n,int d){std::cerr<<std::string(size_t(d)*2,' ')<<n.name()<<" "<<n.key<<" "<<n.bounds.x<<","<<n.bounds.y<<" "<<n.bounds.w<<"x"<<n.bounds.h<<"\n";for(auto& c:n.children)dump(*c,d+1);};
                    dump(*screen.host().root(),0);}
                REQUIRE((r.control.w>0 && r.control.h>0));
                REQUIRE((r.control.x>=r.bounds.x && r.control.x+r.control.w<=r.bounds.x+r.bounds.w));
                REQUIRE((r.control.y>=r.bounds.y && r.control.y+r.control.h<=r.bounds.y+r.bounds.h));
            }
            screen.capture(output+"/category-"+std::to_string(int(category))+".bmp");
        }
        screen.selectCategory(SettingsScreen::Category::Display);
        const auto windowMode=screen.row("display.mode").number;
        screen.activateSetting("display.mode");
        REQUIRE(screen.row("graphics.torus").kind==SettingsScreen::Kind::Toggle);
        screen.capture(output+"/window-mode-dropdown.bmp");
        screen.key(SDLK_DOWN);screen.key(SDLK_ESCAPE);
        REQUIRE((screen.row("display.mode").number==windowMode && !screen.displayConfirmationPending()));
        screen.activateSetting("display.resolution");
        screen.capture(output+"/resolution-dropdown.bmp");screen.key(SDLK_ESCAPE);
        screen.selectCategory(SettingsScreen::Category::Gameplay);
        for(const auto& row:screen.rows())REQUIRE((row.id!="graphics.torus" && row.id!="gameplay.torus"));
        screen.selectCategory(SettingsScreen::Category::Buildings);
        const auto defaults=s.defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][1];
        for(int tab=0;tab<4;++tab){screen.activateSetting("buildings.tab."+std::to_string(tab));screen.capture(output+"/buildings-"+std::to_string(tab)+".bmp");}
        REQUIRE(s.defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][1]==defaults);
        REQUIRE(s.defaultFlagRadius[0]==0);
        REQUIRE(screen.row("radius.0").value=="Default");
        REQUIRE(screen.changeSetting("radius.0",3));
        REQUIRE((s.defaultFlagRadius[0]==3 && s.defaultFlagRadius[1]==4));
        screen.selectCategory(SettingsScreen::Category::Audio);
        REQUIRE(!screen.row("audio.music").enabled);
        screen.click("audio.mute");REQUIRE(!s.mute);
        screen.key(SDLK_TAB);screen.key(SDLK_RIGHT);screen.onTimer(SDL_GetTicks()+400);
        REQUIRE(screen.changeSetting("audio.music",123));
        screen.finishInteraction();Settings loaded;loaded.load();REQUIRE((loaded.musicVolume==123 && !loaded.mute));
        screen.selectCategory(SettingsScreen::Category::Display);
        s.optionFlags|=0x80;REQUIRE(screen.changeSetting("graphics.detail",1));REQUIRE((s.optionFlags & 0x80));
        REQUIRE(screen.changeSetting("graphics.detail",0));REQUIRE(s.optionFlags==0x80);
        REQUIRE(screen.row("graphics.renderer").kind==SettingsScreen::Kind::Choice);
        screen.activateSetting("graphics.renderer");screen.key(SDLK_ESCAPE);
        screen.capture(output+"/graphics-renderer.bmp");
        if(s.screenFlags & GraphicContext::USEGPU){
            REQUIRE(screen.changeSetting("graphics.renderer",0));REQUIRE(screen.restartRequired());
            loaded.load();REQUIRE(!(loaded.screenFlags & GraphicContext::USEGPU));
            REQUIRE(screen.changeSetting("graphics.renderer",1));REQUIRE(!screen.restartRequired());
        }else{
            int oldWidth=s.screenWidth;
            auto resolution=screen.row("display.resolution");int index=-1;
            for(size_t i=0;i<resolution.choices.size();++i)if(resolution.choices[i].rfind("800 × 600",0)==0)index=i;
            REQUIRE(index>=0);
            screen.failNextDisplay=true;REQUIRE(screen.changeSetting("display.resolution",index));
            REQUIRE((!screen.displayConfirmationPending() && globalContainer->gfx->getW()==oldWidth));
            REQUIRE(screen.changeSetting("display.resolution",index));
            REQUIRE(screen.displayConfirmationPending());REQUIRE(s.screenWidth==oldWidth);
            screen.capture(output+"/display-confirm.bmp");
            screen.confirmDisplay(false);REQUIRE(globalContainer->gfx->getW()==oldWidth);
            screen.changeSetting("display.resolution",index);screen.onTimer(SDL_GetTicks()+16000);
            REQUIRE((!screen.displayConfirmationPending() && globalContainer->gfx->getW()==oldWidth));
            screen.changeSetting("display.resolution",index);screen.confirmDisplay(true);loaded.load();REQUIRE(loaded.screenWidth==800);
        }
        {
            // The collapsed control shows the chosen entry, not the scale in use.
            auto scale=screen.row("display.uiscale");
            REQUIRE((scale.kind==SettingsScreen::Kind::Choice && scale.value.rfind("Match the desktop",0)==0));
            int index=-1;for(size_t i=0;i<scale.choices.size();++i)if(scale.choices[i]=="175 %")index=i;
            REQUIRE((index>=0 && screen.changeSetting("display.uiscale",index)));
            REQUIRE(screen.row("display.uiscale").value=="175 %");
            loaded.load();REQUIRE(loaded.uiScale==175);
            // Software mode applies it in place; a GPU context waits for a restart.
            REQUIRE(screen.restartRequired()==bool(s.screenFlags & GraphicContext::USEGPU));
            screen.capture(output+"/interface-scale.bmp");
            REQUIRE(screen.changeSetting("display.uiscale",0));
            REQUIRE((screen.row("display.uiscale").value.rfind("Match the desktop",0)==0 && !screen.restartRequired()));
        }
        screen.selectCategory(SettingsScreen::Category::Gameplay);
        REQUIRE(screen.changeSetting("gameplay.speed",4-Settings::GAME_SPEED_MINIMUM));loaded.load();REQUIRE(loaded.gameSpeed==4);
        REQUIRE(s.autosaveGames);REQUIRE(screen.changeSetting("gameplay.autosave",0));loaded.load();REQUIRE(!loaded.autosaveGames);
        REQUIRE(screen.changeSetting("gameplay.autosave",1));loaded.load();REQUIRE(loaded.autosaveGames);
        // Experiments: one toggle per registry entry, saved as its key.
        screen.selectCategory(SettingsScreen::Category::Experiments);
        REQUIRE(screen.row("experiments.guard-area-balancing").kind==SettingsScreen::Kind::Toggle);
        REQUIRE(!s.experiments.has(ExperimentId::GuardAreaBalancing));
        REQUIRE(screen.changeSetting("experiments.guard-area-balancing",1));loaded.load();REQUIRE(loaded.experiments.has(ExperimentId::GuardAreaBalancing));
        REQUIRE(readFile(profile+"/preferences.txt").find("experiments=guard-area-balancing\n")!=std::string::npos);
        screen.capture(output+"/experiments.bmp");
        REQUIRE(screen.changeSetting("experiments.guard-area-balancing",0));loaded.load();REQUIRE(loaded.experiments.empty());
        screen.selectCategory(SettingsScreen::Category::Gameplay);
        const auto before=readFile(profile+"/preferences.txt");
        const auto permissions=std::filesystem::status(profile+"/preferences.txt").permissions();
        const auto directoryTarget=profile+"/atomic-directory";
        std::filesystem::create_directory(directoryTarget);
        REQUIRE(!Toolkit::getFileManager()->writeFileAtomic(directoryTarget,"must not replace directory"));
        REQUIRE(std::filesystem::is_directory(directoryTarget));
        std::filesystem::rename(profile+"/preferences.txt", profile+"/preferences.backup");
        std::filesystem::create_directory(profile+"/preferences.txt");
        REQUIRE(screen.changeSetting("gameplay.speed",5-Settings::GAME_SPEED_MINIMUM));REQUIRE(screen.saveFailed());REQUIRE(readFile(profile+"/preferences.backup")==before);
        screen.capture(output+"/save-error.bmp");
        std::filesystem::remove(profile+"/preferences.txt");
        std::filesystem::rename(profile+"/preferences.backup", profile+"/preferences.txt");
        screen.finishInteraction();REQUIRE(!screen.saveFailed());loaded.load();REQUIRE(loaded.gameSpeed==5);
        REQUIRE(std::filesystem::status(profile+"/preferences.txt").permissions()==permissions);
        screen.selectCategory(SettingsScreen::Category::Player);
        screen.activateSetting("player.name");screen.key(SDLK_a,KMOD_CTRL);
        SDL_Event input{};input.type=SDL_TEXTINPUT;strcpy(input.text.text,"New player");screen.event(input);screen.key(SDLK_RETURN);
        loaded.load();REQUIRE(loaded.getUsername()=="New player");
        screen.activateSetting("player.name");screen.key(SDLK_BACKSPACE);screen.key(SDLK_ESCAPE);REQUIRE(s.getUsername()=="New player");
        screen.changeSetting("player.language",Toolkit::getStringTable()->getLangCode("fr"));
        for(auto c:screen.visibleCategories()){screen.selectCategory(c);screen.capture(output+"/french-"+std::to_string(int(c))+".bmp");}
        screen.selectCategory(SettingsScreen::Category::Player);screen.changeSetting("player.language",Toolkit::getStringTable()->getLangCode("en"));
        screen.selectCategory(SettingsScreen::Category::Controls);
        screen.activateSetting("keys.add.0");screen.key(SDLK_F12);screen.activateSetting("binding.save");
        KeyboardManager check(GameGUIShortcuts);bool found=false;for(const auto& b:check.getKeyboardShortcuts())found|=b.getKeyPress(0).getKey()=="f12";REQUIRE(found);
        screen.activateSetting("keys.add.1");screen.key(SDLK_F12);screen.activateSetting("binding.save");
        REQUIRE(screen.row("conflict.replace").enabled);screen.capture(output+"/key-conflict.bmp");
        screen.activateSetting("conflict.cancel");screen.activateSetting("binding.cancel");
        screen.activateSetting("keys.add.1");screen.key(SDLK_F11);screen.activateSetting("binding.advanced");screen.activateSetting("binding.addkey");screen.key(SDLK_F10);
        screen.activateSetting("binding.addkey");screen.key(SDLK_F9);
        REQUIRE(screen.changeSetting("binding.trigger.2",1));
        screen.capture(output+"/key-sequence.bmp");screen.activateSetting("binding.save");
        check=KeyboardManager(GameGUIShortcuts);found=false;for(const auto& b:check.getKeyboardShortcuts())if(b.getKeyPress(0).getKey()=="f11"){REQUIRE((b.getKeyPressCount()==3 && !b.getKeyPress(2).getPressed()));found=true;}REQUIRE(found);
        // A single-key prefix must conflict with the saved three-key sequence.
        screen.activateSetting("keys.add.2");screen.key(SDLK_F11);screen.activateSetting("binding.save");
        REQUIRE(screen.row("conflict.replace").enabled);screen.activateSetting("conflict.replace");
        check=KeyboardManager(GameGUIShortcuts);int matches=0;
        for(const auto& b:check.getKeyboardShortcuts())if(b.getKeyPress(0).getKey()=="f11"){++matches;REQUIRE((b.getAction()==2 && b.getKeyPressCount()==1));}REQUIRE(matches==1);
        const std::string keyFile=GameGUIKeyActions::getConfigurationFile();
        const auto savedKeys=readFile(profile+"/"+keyFile);
        std::filesystem::rename(profile+"/"+keyFile, profile+"/"+keyFile+".backup");
        std::filesystem::create_directory(profile+"/"+keyFile);
        screen.activateSetting("keys.add.3");screen.key(SDLK_F8);screen.activateSetting("binding.save");
        REQUIRE((screen.saveFailed() && readFile(profile+"/"+keyFile+".backup")==savedKeys));
        std::filesystem::remove(profile+"/"+keyFile);
        std::filesystem::rename(profile+"/"+keyFile+".backup", profile+"/"+keyFile);
        screen.finishInteraction();REQUIRE(!screen.saveFailed());
        screen.activateSetting("keys.mode.1");screen.capture(output+"/editor-shortcuts.bmp");
        REQUIRE(s.highResolutionArtwork==originalArtwork);
        screen.selectCategory(SettingsScreen::Category::Display);
        screen.changeSetting("graphics.artwork",!originalArtwork);
        Settings saved;saved.load();REQUIRE(saved.highResolutionArtwork==!originalArtwork);
        screen.changeSetting("graphics.artwork",originalArtwork);
        screen.selectCategory(SettingsScreen::Category::Display);
        const bool previousTorus=s.automaticTorus;
        screen.changeSetting("graphics.torus",!previousTorus);
        saved.load();REQUIRE(saved.automaticTorus==!previousTorus);
        screen.changeSetting("graphics.torus",previousTorus);
        for (const auto& [value,name] : std::vector<std::pair<int,std::string>>{{1,"compact"},{2,"spacious"},{0,"automatic"}}) {
            REQUIRE(screen.changeSetting("display.presentation",value));
            saved.load();REQUIRE(saved.interfacePresentation==name);
        }
        const auto sample=profile+"/presentation-preference.txt";
        for (const char* contents : {"interfacePresentation=invalid\n", "username=Legacy profile\n"}) {
            {std::ofstream file(sample);file<<contents;}
            saved.interfacePresentation="compact";saved.load(sample);
            REQUIRE(saved.interfacePresentation=="automatic");
        }
        std::filesystem::remove(sample);
        screen.done();
    }
    std::cout<<"PASS: layout, persistence, automatic saving, display confirmation, building defaults, bindings, localization\n";
    SDLNet_Quit();
}
}

// The configuration matrix the old settings wrapper script drove: four OpenGL window
// sizes, one software renderer and the expanded English wording.
TEST_SUITE("Settings")
{
	TEST_CASE("layout; persistence; display confirmation; bindings and localization at 640x480 in OpenGL [display:1600x1400][artifacts][writes-preferences]") { run(640, 480, true, false); }
	TEST_CASE("layout; persistence; display confirmation; bindings and localization at 800x600 in OpenGL [display:1600x1400][artifacts][writes-preferences]") { run(800, 600, true, false); }
	TEST_CASE("layout; persistence; display confirmation; bindings and localization at 1000x700 in OpenGL [display:1600x1400][artifacts][writes-preferences]") { run(1000, 700, true, false); }
	TEST_CASE("layout; persistence; display confirmation; bindings and localization at 1280x900 in OpenGL [display:1600x1400][artifacts][writes-preferences]") { run(1280, 900, true, false); }
	TEST_CASE("layout; persistence; display confirmation; bindings and localization at 1000x700 in software rendering [display:1600x1400][artifacts][writes-preferences]") { run(1000, 700, false, false); }
	TEST_CASE("layout; persistence; display confirmation; bindings and localization with expanded wording at 640x480 in OpenGL [display:1600x1400][artifacts][writes-preferences]") { run(640, 480, true, true); }
}
