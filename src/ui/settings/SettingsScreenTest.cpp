// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/ThemeCatalog.h"
#include "EngineFixtures.h"
#include "ScopedEnvironment.h"
#include <vector>
#include <string>
#include <utility>
#include <iterator>
#include "GlobalContainer.h"
#include "SettingsScreen.h"
#include "SoundMixer.h"
#include "FrontendTheme.h"
#include "GameGUIKeyActions.h"
#include "KeyboardManager.h"
#include "FileManager.h"
#include <ScrollPhysics.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <SDL3_net/SDL_net.h>
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
    void key(SDL_Keycode k,Uint16 modifiers=0){SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.key=k;e.key.mod=modifiers;handleExecutionEvent(e);}
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
    glob2test::ScopedEnvironment desktop("GLOB2_MOBILE_UI", "0");
    if(expanded)writeExpandedStrings();
    glob2test::GlobalsOptions options{.display=true,.loadStrings=true,.width=width,.height=height,.screenFlags=gl?Uint32(GraphicContext::USEGPU):0u};
    options.beforeLoad=[](GlobalContainer& globals){globals.settings.language="en";globals.settings.defaultFlagRadius[0]=0;};
    glob2test::HeadlessGlobals globals(options);
    auto& s=globalContainer->settings;
    REQUIRE(NET_Init());
    const std::string output=glob2test::artifactDir().string();
    const auto profile=Toolkit::getFileManager()->getDir(0);
    const auto originalArtwork=s.highResolutionArtwork;
    if(auto* window=SDL_GL_GetCurrentWindow()){
        int ww,wh,dw,dh;SDL_GetWindowSize(window,&ww,&wh);SDL_GetWindowSizeInPixels(window,&dw,&dh);
        std::cout<<"Window "<<ww<<"x"<<wh<<", drawable "<<dw<<"x"<<dh<<"\n";
    }
    {
        FrontendTheme theme;
        FrontendScope frontend;
        NativeSettings screen;
        for(auto category:screen.visibleCategories()){
            screen.selectCategory(category);
            for(const auto& r:screen.rows()){
                if(category==SettingsScreen::Category::Online){
                    CHECK(r.label.find("[settings ")==std::string::npos);
                    CHECK(r.help.find("[settings ")==std::string::npos);
                }
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
        REQUIRE((screen.row("display.mode").number==windowMode && !screen.restartRequired()));
        for (const auto& row : screen.rows()) REQUIRE(row.id != "display.resolution");
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
        CHECK(screen.row("audio.set").label == "Music set");
        CHECK(screen.row("audio.set").value == "Random each match");
        REQUIRE(screen.changeSetting("audio.set", 1));
        screen.finishInteraction();
        REQUIRE(!s.musicSet.empty());
        REQUIRE(s.musicSet == globalContainer->mix->getMusicSet());
        REQUIRE(screen.changeSetting("audio.set", 0));
        screen.finishInteraction();
        REQUIRE(s.musicSet.empty());
        screen.click("audio.mute");REQUIRE(!s.mute);
        screen.key(SDLK_TAB);screen.key(SDLK_RIGHT);screen.onTimer(SDL_GetTicks()+400);
        REQUIRE(screen.changeSetting("audio.music",123));
        screen.finishInteraction();Settings loaded;loaded.load();REQUIRE((loaded.musicVolume==123 && !loaded.mute));
        screen.selectCategory(SettingsScreen::Category::Controls);
        REQUIRE(screen.row("controls.edgefullscreen").number == 1);
        REQUIRE(screen.row("controls.edgewindowed").number == 0);
        REQUIRE(screen.changeSetting("controls.edgefullscreen", 0));
        REQUIRE(screen.changeSetting("controls.edgewindowed", 1));
        loaded.load();
        REQUIRE_FALSE(loaded.edgeScrollFullscreen);
        REQUIRE(loaded.edgeScrollWindowed);
        REQUIRE(screen.changeSetting("controls.edgefullscreen", 1));
        REQUIRE(screen.changeSetting("controls.edgewindowed", 0));
        for(const char* id:{"controls.momentum","controls.bounce","controls.mapmomentum"}){
            REQUIRE(screen.row(id).kind==SettingsScreen::Kind::Slider);REQUIRE(screen.row(id).value=="50%");REQUIRE(screen.row(id).maximum==100);
        }
        REQUIRE(screen.changeSetting("controls.momentum",0));REQUIRE(s.touchScrollMomentum==0);REQUIRE(!GAGCore::ScrollPresets::widget().momentum);
        REQUIRE(screen.changeSetting("controls.bounce",80));REQUIRE(s.touchScrollBounce==80);REQUIRE(GAGCore::ScrollPresets::widget().rubberBandCoefficient>0.56);
        REQUIRE(screen.changeSetting("controls.mapmomentum",10));REQUIRE(s.mapScrollMomentum==10);REQUIRE(GAGCore::ScrollPresets::mapViewport().decelerationRate<0.998);
        REQUIRE(screen.row("controls.bounce").value=="80%");
        screen.onTimer(SDL_GetTicks()+400);loaded.load();
        REQUIRE((loaded.touchScrollMomentum==0 && loaded.touchScrollBounce==80 && loaded.mapScrollMomentum==10));
        for(const char* id:{"controls.momentum","controls.bounce","controls.mapmomentum"})REQUIRE(screen.changeSetting(id,50));
        screen.onTimer(SDL_GetTicks()+400);loaded.load();REQUIRE(loaded.touchScrollMomentum==50);
        REQUIRE(GAGCore::ScrollPresets::widget().momentum);
        screen.selectCategory(SettingsScreen::Category::Display);
        s.optionFlags |= 0x80;
        for (const char* id : {"graphics.skins", "graphics.clouds", "graphics.shadows", "graphics.particles", "graphics.magic", "graphics.panels", "graphics.paths", "graphics.indicators", "graphics.animation", "graphics.unitmotion"}) {
            REQUIRE(screen.changeSetting(id,0)); REQUIRE(s.optionFlags==0x80);
            REQUIRE(screen.changeSetting(id,1)); REQUIRE(s.optionFlags==0x80);
        }
        REQUIRE(s.showColonySkins);
        REQUIRE(screen.changeSetting("graphics.skins",0));
        REQUIRE_FALSE(s.showColonySkins);
        Settings skinSaved; skinSaved.load(); REQUIRE_FALSE(skinSaved.showColonySkins);
        REQUIRE(screen.changeSetting("graphics.skins",1));
        REQUIRE((s.clouds && s.cloudShadows && s.buildingParticles && s.fullMagicEffects));
        REQUIRE(screen.changeSetting("graphics.clouds",0));
        REQUIRE((!s.clouds && s.cloudShadows && s.highResolutionArtwork==originalArtwork));
        Settings effectSaved; effectSaved.load(); REQUIRE((!effectSaved.clouds && effectSaved.cloudShadows));
        for (int index : {1,2,0}) {
            REQUIRE(screen.changeSetting("display.textsize",index));
            Settings savedText; savedText.load(); REQUIRE(savedText.textSizePercent==100+25*index);
            screen.paintFrame(SDL_GetTicks());
            screen.host().scrollIntoView("display.textsize");
            screen.capture(output+"/text-size-"+std::to_string(index)+".bmp");
        }
        {
            // Menu and in-game themes are chosen apart, apply at once and persist.
            const auto& themes=Glob2UI::ThemeCatalog::shared().themes();
            REQUIRE(themes.size()>=6);
            REQUIRE(screen.row("display.menutheme").choices.size()==themes.size());
            REQUIRE(screen.row("display.menutheme").value=="Light");
            REQUIRE(screen.row("display.gametheme").value=="Dark");
            int classic=-1;
            for (int i=0;i<int(themes.size());++i) if (themes[i].id=="classic") classic=i;
            REQUIRE(classic>=0);
            REQUIRE(screen.changeSetting("display.menutheme",classic));
            REQUIRE((s.menuTheme=="classic" && s.gameTheme=="dark"));
            REQUIRE(Glob2UI::frontendTheme().id=="classic");
            REQUIRE(Glob2UI::inGameTheme().id=="dark");
            screen.paintFrame(SDL_GetTicks());
            screen.host().scrollIntoView("display.menutheme");
            screen.capture(output+"/menu-theme-classic.bmp");
            Settings savedTheme; savedTheme.load(); REQUIRE(savedTheme.menuTheme=="classic");
            REQUIRE(screen.changeSetting("display.menutheme",0));
            REQUIRE(Glob2UI::frontendTheme().id=="light");
        }
        REQUIRE(screen.row("graphics.renderer").kind==SettingsScreen::Kind::Choice);
        screen.activateSetting("graphics.renderer");screen.key(SDLK_ESCAPE);
        screen.capture(output+"/graphics-renderer.bmp");
        if(s.screenFlags & GraphicContext::USEGPU){
            REQUIRE(screen.changeSetting("graphics.renderer",0));REQUIRE(screen.restartRequired());
            loaded.load();REQUIRE(!(loaded.screenFlags & GraphicContext::USEGPU));
            REQUIRE(screen.changeSetting("graphics.renderer",1));REQUIRE(!screen.restartRequired());
        }
        {
            const auto previousFlags=s.screenFlags;
            screen.failNextDisplay=true;
            REQUIRE(screen.changeSetting("display.mode",1));
            REQUIRE(s.screenFlags==previousFlags);
            if (glob2test::fullscreenEnabled()) {
                REQUIRE(screen.changeSetting("display.mode",1));
                REQUIRE((globalContainer->gfx->getOptionFlags() & GraphicContext::FULLSCREEN));
                REQUIRE((s.screenFlags & ~GraphicContext::FULLSCREEN)==(previousFlags & ~GraphicContext::FULLSCREEN));
                loaded.load(); REQUIRE((loaded.screenFlags & GraphicContext::FULLSCREEN));
                REQUIRE(!screen.restartRequired());
                REQUIRE(screen.changeSetting("display.mode",0));
                REQUIRE(!(globalContainer->gfx->getOptionFlags() & GraphicContext::FULLSCREEN));
                loaded.load(); REQUIRE(!(loaded.screenFlags & GraphicContext::FULLSCREEN));
            } else {
                REQUIRE(!(globalContainer->gfx->getOptionFlags() & GraphicContext::FULLSCREEN));
                loaded.load(); REQUIRE(loaded.screenFlags==previousFlags);
            }
        }

        {
            // The collapsed control shows the chosen entry, not the scale in use.
            auto scale=screen.row("display.uiscale");
            REQUIRE((scale.kind==SettingsScreen::Kind::Choice && scale.value.rfind("Match the desktop",0)==0));
            int index=-1;for(size_t i=0;i<scale.choices.size();++i)if(scale.choices[i]=="175 %")index=i;
            REQUIRE((index>=0 && screen.changeSetting("display.uiscale",index)));
            REQUIRE(screen.row("display.uiscale").value=="175 %");
            loaded.load();REQUIRE(loaded.uiScale==175);
            // Both renderers change scale without a context restart.
            REQUIRE(!screen.restartRequired());
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
        screen.activateSetting("player.name");screen.key(SDLK_A,SDL_KMOD_CTRL);
        SDL_Event input{};input.type=SDL_EVENT_TEXT_INPUT;input.text.text = "New player";screen.event(input);screen.key(SDLK_RETURN);
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
        Settings saved;saved.load();REQUIRE(saved.highResolutionArtwork==(gl ? !originalArtwork : originalArtwork));
        screen.changeSetting("graphics.artwork",originalArtwork);
        screen.selectCategory(SettingsScreen::Category::Display);
        const bool previousTorus=s.automaticTorus;
        screen.changeSetting("graphics.torus",!previousTorus);
        saved.load();REQUIRE(saved.automaticTorus==(gl ? !previousTorus : previousTorus));
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
    NET_Quit();
}
}

// The configuration matrix the old settings wrapper script drove: four OpenGL window
// sizes, one software renderer and the expanded English wording.
TEST_SUITE("Settings")
{
	TEST_CASE("layout; persistence; live display changes; bindings and localization at 640x480 in OpenGL [display:1600x1400][artifacts][writes-preferences]") { run(640, 480, true, false); }
	TEST_CASE("layout; persistence; live display changes; bindings and localization at 800x600 in OpenGL [display:1600x1400][artifacts][writes-preferences]") { run(800, 600, true, false); }
	TEST_CASE("layout; persistence; live display changes; bindings and localization at 1000x700 in OpenGL [display:1600x1400][artifacts][writes-preferences]") { run(1000, 700, true, false); }
	TEST_CASE("layout; persistence; live display changes; bindings and localization at 1280x900 in OpenGL [display:1600x1400][artifacts][writes-preferences]") { run(1280, 900, true, false); }
	TEST_CASE("layout; persistence; live display changes; bindings and localization at 1000x700 in software rendering [display:1600x1400][artifacts][writes-preferences]") { run(1000, 700, false, false); }
	TEST_CASE("layout; persistence; live display changes; bindings and localization with expanded wording at 640x480 in OpenGL [display:1600x1400][artifacts][writes-preferences]") { run(640, 480, true, true); }
}
