// SPDX-License-Identifier: GPL-3.0-or-later
#include "GlobalContainer.h"
#include "CustomGameScreen.h"
#include "SettingsScreen.h"
#include "LobbyControls.h"
#include <ScreenStack.h>
#include <SDL_net.h>
#include <cstdio>
#include <stdexcept>

GlobalContainer* globalContainer=nullptr;
static void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
struct MobilePresentationHarness
{
    static void run()
    {
        auto* gfx=globalContainer->gfx;
        Uint32 tick=0;
        for(auto [width,height]:{std::pair{360,640},std::pair{640,360}}) {
            SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()),width,height);
            SDL_Event resize{};resize.type=SDL_WINDOWEVENT;resize.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;
            GAGCore::GraphicContext::translateMouseEvent(&resize);
            const std::string orientation=width<height?"portrait":"landscape";
            GAGGUI::ScreenStack stack(*gfx);
            auto owned=std::make_unique<CustomGameScreen>(stack);auto* lobby=owned.get();
            stack.push(std::move(owned));stack.frame(tick+=40,{});
            auto tap=[&](int x,int y) {
                SDL_Event down{};down.type=SDL_FINGERDOWN;down.tfinger.touchId=1;down.tfinger.fingerId=1;
                down.tfinger.x=float(x)/gfx->getW();down.tfinger.y=float(y)/gfx->getH();
                auto up=down;up.type=SDL_FINGERUP;
                stack.frame(tick+=40,{down,up});
            };
            auto hit=[&](const std::string& id) {
                for(auto& item:lobby->controls->hits)if(item.id==id)return item.box;
                throw std::runtime_error("Missing touch target: "+id);
            };
            auto press=[&](const std::string& id) {auto r=hit(id);tap(r.x+r.w/2,r.y+r.h/2);};
            auto snapshot=[&](const std::string& name) {gfx->printScreen(name+"-"+orientation+".bmp");stack.frame(tick+=40,{});};
            require(gfx->getW()==width && gfx->getH()==height,"Setup must use phone dimensions");
            snapshot("setup-map");
            auto profile=std::make_unique<CustomGameChoiceScreen>("AI profile",std::vector<std::string>{"One","Two"},0,false,std::vector<bool>{});
            auto* profileScreen=profile.get();stack.push(std::move(profile));stack.frame(tick+=40,{});
            auto profileHit=[&](const std::string& id) {
                for(const auto& item:profileScreen->controls->hits)if(item.id==id)return item.box;
                throw std::runtime_error("Missing profile control: "+id);
            };
            const auto back=profileHit("profile/back"),use=profileHit("profile/use");
            require(back.x+back.w<=use.x,"Phone profile Back and Use must not overlap");
            require(back.h>=48 && use.h>=48,"Phone profile actions need touch-sized targets");
            snapshot("ai-profile");
            tap(back.x+back.w/2,back.y+back.h/2);stack.frame(tick+=40,{});
            lobby->setMapMode(true);stack.frame(tick+=40,{});
            press("landscape");stack.frame(tick+=40,{});
            require(gfx->getW()==width && gfx->getH()==height,"Landscape picker retains phone dimensions");
            snapshot("landscape-picker");tap(40,height-32);stack.frame(tick+=40,{});

            press("tab/1");require(lobby->currentTab==lobby->groups[1],"Touch opens Players tab");
            snapshot("setup-players");
            press("colony/0/controller");
            auto* controls=lobby->controls;
            require(controls->popup.open,"Controller dropdown opens by touch");
            auto popup=controls->popupRect();
            require(popup.y>=0 && popup.y+popup.h<=height,"Dropdown stays on screen in both orientations");
            require(controls->popupRowHeight()>=48,"Dropdown has phone-sized touch rows");
            snapshot("setup-dropdown");
            tap(2,2);
            press("tab/2");require(lobby->currentTab==lobby->groups[2],"Touch opens Rules tab");
            press("ruleset/1");require(lobby->setup.ruleset!="Standard","Touch rules preset updates shared setup");
            snapshot("setup-rules");
            // A swipe must scroll without activating the preset beneath its release.
            const auto rules=lobby->setup.ruleset;
            SDL_Event down{};down.type=SDL_FINGERDOWN;down.tfinger.touchId=1;down.tfinger.fingerId=2;
            down.tfinger.x=.5f;down.tfinger.y=.60f;
            auto move=down;move.type=SDL_FINGERMOTION;move.tfinger.y=.30f;
            auto up=move;up.type=SDL_FINGERUP;
            stack.frame(tick+=40,{down,move,up});
            require(lobby->setup.ruleset==rules,"Scrolling must not select a rules preset");
            auto settings=std::make_unique<SettingsScreen>();auto* form=settings.get();stack.push(std::move(settings));
            stack.frame(tick+=40,{});
            for(int category=0;category<6;++category) {
                form->selectCategory(SettingsScreen::Category(category));stack.frame(tick+=40,{});
                for(const auto& row:form->rows())if(row.kind!=SettingsScreen::Kind::Section && row.kind!=SettingsScreen::Kind::Info) {
                    require(row.control.x>=0 && row.control.x+row.control.w<=width,"Settings control extends outside screen");
                    require(row.control.h>=48,"Settings touch control is too short");
                }
                snapshot("settings-"+std::to_string(category));
            }
            form->selectCategory(SettingsScreen::Category::Audio);stack.frame(tick+=40,{});
            for(const auto& row:form->rows())if(row.id=="audio.mute") {
                const auto r=row.control;const bool muted=globalContainer->settings.mute;
                tap(r.x+r.w/2,r.y+r.h/2);
                require(globalContainer->settings.mute!=muted,"Phone toggle updates shared settings exactly once");
                tap(r.x+r.w/2,r.y+r.h/2);
                require(globalContainer->settings.mute==muted,"Phone toggle can be restored");
                break;
            }
            form->selectCategory(SettingsScreen::Category::Gameplay);stack.frame(tick+=40,{});
            snapshot("settings-gameplay");
            form->endExecute(0);stack.frame(tick+=40,{});
            require(gfx->getW()==width && gfx->getH()==height,"Returning to setup preserves phone viewport");
        }
    }
};
int main()
{
    if(!SDL_getenv("GLOB2_USER_DATA_DIR"))return 2;
    SDL_setenv("GLOB2_MOBILE_UI","1",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    try {
        globalContainer=new GlobalContainer("glob2-phone-presentation-test");
        auto& settings=globalContainer->settings;settings.screenWidth=800;settings.screenHeight=600;
        settings.screenFlags=GAGCore::GraphicContext::PORTABLEGPU|GAGCore::GraphicContext::RESIZABLE;settings.mute=true;
        globalContainer->load();require(SDLNet_Init()==0,"SDL networking failed");
        MobilePresentationHarness::run();delete globalContainer;globalContainer=nullptr;SDLNet_Quit();
        std::puts("PASS phone presentation: setup touch dispatch, scrolling, dropdown bounds, settings categories, modal viewport restoration in both orientations");
    } catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
