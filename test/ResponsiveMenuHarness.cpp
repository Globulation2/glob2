// SPDX-License-Identifier: GPL-3.0-or-later
#include "GlobalContainer.h"
#include "MainMenuScreen.h"
#include <ScreenStack.h>
#include <Toolkit.h>
#include <cstdio>
#include <stdexcept>

GlobalContainer* globalContainer = nullptr;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Menu : MainMenuScreen {
    std::vector<int> selected;
    void verifyWrapping() {
        for (auto* widget : widgets) if (auto* button=dynamic_cast<GAGGUI::TextButton*>(widget)) {
            button->setText("Très longue description traduite du menu et de toutes les possibilités disponibles");
            const auto lines=button->wrappedLines(180);
            require(lines.size()>1 && button->wrappedHeight(204)>48,"Long translated labels must wrap and grow");
            for (const auto& line : lines)
                require(GAGCore::Toolkit::getFont("menu")->getStringWidth(line)<=180,"Wrapped translation exceeds its button");
            break;
        }
    }
    void onAction(GAGGUI::Widget*, GAGGUI::Action action, int choice, int) override {
        if (action == GAGGUI::BUTTON_RELEASED) selected.push_back(choice);
    }
};
SDL_Event finger(Uint32 type, float x, float y, SDL_FingerID id = 1) {
    SDL_Event event{}; event.type=type; event.tfinger.touchId=10; event.tfinger.fingerId=id;
    event.tfinger.x=x/320; event.tfinger.y=y/568; return event;
}
int main()
{
    if (!SDL_getenv("GLOB2_USER_DATA_DIR")) return 2;
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    SDL_setenv("GLOB2_RESPONSIVE_UI","1",1);
    try {
        globalContainer = new GlobalContainer("glob2-responsive-menu-test");
        globalContainer->settings.screenWidth=800; globalContainer->settings.screenHeight=600;
        globalContainer->settings.screenFlags=GAGCore::GraphicContext::PORTABLEGPU | GAGCore::GraphicContext::RESIZABLE;
        globalContainer->settings.mute=true; globalContainer->load();
        auto* window=SDL_GetWindowFromID(globalContainer->gfx->windowID());
        require(window,"Fixture must own its only SDL window");
        SDL_SetWindowSize(window,320,568);
        SDL_Event resize{}; resize.type=SDL_WINDOWEVENT; resize.window.event=SDL_WINDOWEVENT_SIZE_CHANGED;
        GAGCore::GraphicContext::translateMouseEvent(&resize);
        {
            GAGGUI::ScreenStack stack(*globalContainer->gfx);
            auto owned=std::make_unique<Menu>(); auto* menu=owned.get(); stack.push(std::move(owned));
            stack.frame(0,{});
            require(globalContainer->gfx->getW()==320 && globalContainer->gfx->getH()==568,"Menu did not adopt portrait dimensions");
            stack.frame(40,{finger(SDL_FINGERDOWN,160,80),finger(SDL_FINGERUP,160,80)});
            require(menu->selected==std::vector<int>{MainMenuScreen::CAMPAIGN},"Touch tap did not select campaign exactly once");
            SDL_Event mouse{}; mouse.type=SDL_MOUSEBUTTONUP; mouse.button.which=SDL_TOUCH_MOUSEID;
            mouse.button.button=SDL_BUTTON_LEFT; mouse.button.x=160; mouse.button.y=80;
            stack.frame(80,{mouse});
            require(menu->selected.size()==1,"Synthesized mouse event duplicated selection");
            stack.frame(120,{finger(SDL_FINGERDOWN,160,180),finger(SDL_FINGERMOTION,160,100),finger(SDL_FINGERUP,160,100)});
            require(menu->selected.size()==1,"Scroll drag selected a button");
            stack.frame(160,{finger(SDL_FINGERDOWN,160,80)});
            SDL_Event lost{}; lost.type=SDL_WINDOWEVENT; lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
            stack.frame(200,{lost}); stack.frame(240,{finger(SDL_FINGERUP,160,80)});
            require(menu->selected.size()==1,"Canceled touch selected a button");
            globalContainer->gfx->printScreen("responsive-menu.bmp"); stack.frame(280,{});
            struct Child : GAGGUI::Screen {
                void onAction(GAGGUI::Widget*,GAGGUI::Action,int,int) override {}
                void drawExecution() override {}
            };
            auto child=std::make_unique<Child>(); auto* childProbe=child.get();
            stack.push(std::move(child)); stack.frame(320,{});
            require(globalContainer->gfx->getW()==800 && globalContainer->gfx->getH()==600,"Child needs legacy coordinates before initialization");
            childProbe->endExecute(0); stack.frame(360,{});
            require(globalContainer->gfx->getW()==320 && globalContainer->gfx->getH()==568,"Resumed menu needs responsive coordinates");
            SDL_SetWindowSize(window,568,320); stack.frame(400,{resize});
            require(globalContainer->gfx->getW()==568 && globalContainer->gfx->getH()==320,"Menu rotation did not reflow");
            menu->verifyWrapping();
        }
        globalContainer->gfx->setResponsiveViewport(false);
        require(globalContainer->gfx->getW()==800 && globalContainer->gfx->getH()==600,"Legacy viewport was not restored");
        delete globalContainer; globalContainer=nullptr;
        std::puts("PASS responsive menu: portrait layout, actual touch dispatch, duplicate suppression, scrolling, cancellation, legacy restoration");
    } catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
}
