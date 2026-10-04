// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GameGUIViewport.h"
#include "Order.h"
#include "GameGUIKeyActions.h"
#include "GameGUIDialog.h"
#include "LoadSaveDialog.h"
#include "map/edit/MapEditDialog.h"
#include <SDLGraphicContext.h>
#include <SDL3/SDL.h>
#include <set>

namespace
{
Uint64 renderPanel(GameGUI& gui)
{
    auto* gfx=globalContainer->gfx;
    gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);
    gfx->setClipRect();
    gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),GAGCore::Color(0,0,0));
    gui.extractScene(gui.frameScene);
    gui.drawUnitInfos(); gfx->nextFrame();
    auto* frame=gfx->completedFrame(); REQUIRE(frame);
    Uint64 hash=1469598103934665603ull;
    const int left=(gfx->getW()-GAME_GUI_RIGHT_MENU_WIDTH)*frame->w/gfx->getW();
    const auto* pixels=static_cast<const Uint8*>(frame->pixels);
    for (int y=0; y<frame->h; ++y)
        for (int x=left*SDL_BYTESPERPIXEL(frame->format); x<frame->w*SDL_BYTESPERPIXEL(frame->format); ++x)
            hash=(hash^pixels[y*frame->pitch+x])*1099511628211ull;
    return hash;
}
}
TEST_SUITE("GUIInteractionCoverage")
{
    TEST_CASE("unit information reflects damage hunger ownership and abilities without mutating simulation [display][artifacts]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
            .display=true,.width=1024,.height=768,.screenFlags=0});
        glob2test::HeadlessGame world(glob2test::GameOptions{
            .teams=3,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& gui=world.gui; gui.localTeamNo=0; gui.localPlayer=0; gui.localTeam=world.team;
        world.team->allies=3; world.team->enemies=4;
        std::set<Uint64> ownerPanels;
        for (int team=0; team<3; ++team)
            for (int type : {WORKER,EXPLORER,WARRIOR})
            {
                CAPTURE(team); CAPTURE(type);
                auto* unit=world.addUnit(type,3+type*3,4+team*3,team,2);
                gui.setSelection(GameGUI::UNIT_SELECTION,unit);
                const auto before=world.checksum();
                const auto healthy=renderPanel(gui);
                CHECK(world.checksum()==before);
                unit->hp=1; unit->hungry=0;
                if (type==WORKER) unit->carriedResource=WHEAT;
                if (type==WARRIOR) world.game.gameHeader.setGlassCannonLevel(1);
                const auto damagedState=world.checksum();
                const auto damaged=renderPanel(gui);
                CHECK(world.checksum()==damagedState); CHECK(damaged!=healthy);
                if (type==WORKER) ownerPanels.insert(healthy);
                const auto path=glob2test::artifactDir()/(
                    "unit-"+std::to_string(type)+"-team-"+std::to_string(team)+".bmp");
                REQUIRE(SDL_SaveBMP(globalContainer->gfx->completedFrame(),path.string().c_str()));
                world.game.gameHeader.setGlassCannonLevel(0);
            }
        CHECK(ownerPanels.size()==3);
        gui.clearSelection();
    }

    TEST_CASE("desktop building menu clicks queue authoritative worker and priority orders [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{
            .display=true,.width=1024,.height=768,.screenFlags=0});
        glob2test::HeadlessGame world(glob2test::GameOptions{
            .discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& gui=world.gui; gui.localTeamNo=0; gui.localPlayer=0; gui.localTeam=world.team;
        auto* inn=world.addBuilding("inn",4,4);
        gui.init();
        gui.setSelection(GameGUI::BUILDING_SELECTION,inn);
        gui.drawAll(0); // The menu describes the scene currently drawn.
        const int original=inn->maxUnitWorking;
        const int content=(GAME_GUI_RIGHT_MENU_WIDTH-128)/2;
        // Real desktop sidebar coordinates: worker bar at y=292..308,
        // priority checks at y=336..348. The rightmost arrow increments.
        gui.handleMenuClick(content+119,300,SDL_BUTTON_LEFT);
        REQUIRE(gui.orderQueue.size()==1);
        CHECK(inn->maxUnitWorking==original);
        CHECK(gui.displayedMaxUnitWorking(*inn)==original+1);
        gui.handleMenuClick(content+3,342,SDL_BUTTON_LEFT);
        REQUIRE(gui.orderQueue.size()==2);
        CHECK(gui.displayedPriority(*inn)==-1);
        while (!gui.orderQueue.empty())
        {
            auto order=gui.orderQueue.front(); gui.orderQueue.pop_front(); order->sender=0;
            gui.executeOrder(order);
        }
        CHECK(inn->maxUnitWorking==original+1); CHECK(inn->priority==-1);
        CHECK_FALSE(gui.pendingFor(inn->gid).pendingMaxUnitWorking.has_value());
        CHECK_FALSE(gui.pendingFor(inn->gid).pendingPriority.has_value());
        globalContainer->liveSpectating=true;
        gui.handleMenuClick(content+119,300,SDL_BUTTON_LEFT);
        CHECK(gui.orderQueue.empty()); globalContainer->liveSpectating=false;
    }
    TEST_CASE("configured keyboard actions toggle presentation and queue pause without changing simulation [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true,.width=1024,.height=768});
        glob2test::HeadlessGame w(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& gui=w.gui; gui.localTeamNo=0; gui.localPlayer=0; gui.localTeam=w.team;
        auto key=[&](Uint32 action) {
            gui.keyboardManager.getKeyboardShortcuts().clear();
            SDL_KeyboardEvent symbol{}; symbol.key=SDLK_F9;
            KeyboardShortcut shortcut; shortcut.addKeyPress(KeyPress(symbol,true)); shortcut.setAction(action);
            gui.keyboardManager.getKeyboardShortcuts().push_back(shortcut);
            gui.handleKey(symbol,true,false);
        };
        const auto checksum=w.checksum();
        const bool bars=gui.drawHealthFoodBar;
        key(GameGUIKeyActions::ToggleDrawInformation); CHECK(gui.drawHealthFoodBar!=bars);
        key(GameGUIKeyActions::ToggleDrawInformation); CHECK(gui.drawHealthFoodBar==bars);
        const bool aids=gui.drawAccessibilityAids;
        key(GameGUIKeyActions::ToggleDrawAccessibilityAids); CHECK(gui.drawAccessibilityAids!=aids);
        key(GameGUIKeyActions::PauseGame);
        REQUIRE(gui.orderQueue.size()==1);
        CHECK(std::dynamic_pointer_cast<PauseGameOrder>(gui.orderQueue.front())!=nullptr);
        CHECK(w.checksum()==checksum);
        gui.orderQueue.clear();
        gui.swallowSpaceKey=true;
        SDL_KeyboardEvent space{}; space.key=SDLK_SPACE;
        gui.handleKey(space,false,false); CHECK_FALSE(gui.isSpaceSet());
        gui.handleKey(space,true,false); CHECK(gui.isSpaceSet());
        CHECK(gui.orderQueue.empty());
    }

    TEST_CASE("home keyboard action wraps the camera and script messages preserve history [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true,.width=640,.height=480});
        glob2test::HeadlessGame w(glob2test::GameOptions{.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& gui=w.gui; gui.localTeamNo=0; gui.localPlayer=0; gui.localTeam=w.team;
        w.team->startPosX=31; w.team->startPosY=31;
        gui.keyboardManager.getKeyboardShortcuts().clear();
        SDL_KeyboardEvent symbol{}; symbol.key=SDLK_F9;
        KeyboardShortcut shortcut; shortcut.addKeyPress(KeyPress(symbol,true)); shortcut.setAction(GameGUIKeyActions::GoToHome);
        gui.keyboardManager.getKeyboardShortcuts().push_back(shortcut);
        gui.viewportX=0; gui.viewportY=0;
        const auto checksum=w.checksum(); gui.handleKey(symbol,true,false);
        CHECK(gui.viewportX>=0); CHECK(gui.viewportX<32); CHECK(gui.viewportY>=0); CHECK(gui.viewportY<32);
        CHECK(gui.viewportX!=0); CHECK(gui.viewportY!=0);
        gui.showScriptText("first message"); gui.hideScriptText();
        gui.showScriptText("second message"); gui.hideScriptText();
        CHECK(w.checksum()==checksum); CHECK(gui.orderQueue.empty());
    }

    TEST_CASE("match and editor dialogs wear the in-match theme; results dialogs stay on paper [display][artifacts]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true,.loadStrings=true,.width=1024,.height=768});
        const auto& match=Glob2UI::themeFor(Glob2UI::Surface::Match);
        const auto& paper=Glob2UI::themeFor(Glob2UI::Surface::Frontend);
        CHECK(&match==&Glob2UI::inGameTheme());
        CHECK(&paper==&Glob2UI::frontendTheme());
        CHECK(&Glob2UI::themeFor(Glob2UI::Surface::Editor)==&Glob2UI::inGameTheme());
        CHECK(&Glob2UI::themeFor(Glob2UI::Surface::Results)==&Glob2UI::frontendTheme());
        auto capture=[&](Glob2UI::InGameDialog& dialog,const char* name)
        {
            auto* gfx=globalContainer->gfx;
            dialog.attach(*gfx); dialog.update(0);
            gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);
            gfx->setClipRect();
            gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),GAGCore::Color(60,110,50));
            dialog.draw(0); gfx->nextFrame();
            REQUIRE(SDL_SaveBMP(gfx->completedFrame(),(glob2test::artifactDir()/name).string().c_str()));
        };
        InGameMainScreen menu(false,true,false);
        CHECK(&menu.theme()==&match);
        capture(menu,"dialog-ingame-menu.bmp");
        InGameEndOfGameScreen outcome("Victory",true);
        CHECK(&outcome.theme()==&match);
        capture(outcome,"dialog-ingame-outcome.bmp");
        LoadSaveDialog save("games","game",false,"Save game");
        CHECK(&save.theme()==&match);
        capture(save,"dialog-ingame-save.bmp");
        MapEditMenuScreen editor;
        CHECK(&editor.theme()==&Glob2UI::themeFor(Glob2UI::Surface::Editor));
        capture(editor,"dialog-editor-menu.bmp");
        LoadSaveDialog replay("replays","replay",false,"Save replay",nullptr,nullptr,nullptr,Glob2UI::Surface::Results);
        CHECK(&replay.theme()==&paper);
    }

    TEST_CASE("match dialogs accept return and distinguish continuing from ending a game [display]")
    {
        glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display=true,.loadStrings=true,.width=640,.height=480});
        SDL_Event key{}; key.type=SDL_EVENT_KEY_DOWN; key.key.key=SDLK_ESCAPE;
        for(bool replay:{false,true}) {
            InGameMainScreen dialog(replay,false,true); dialog.attach(*globals->gfx); dialog.update(0);
            CHECK(dialog.host().find("save")==nullptr);
            CHECK(dialog.host().bounds("return").w>0);
            dialog.eventLogical(key); REQUIRE(dialog.finished()); CHECK(dialog.result()==InGameMainScreen::RETURN_GAME);
        }
        for(bool canContinue:{false,true}) {
            InGameEndOfGameScreen dialog("Outcome",canContinue); dialog.attach(*globals->gfx); dialog.update(0);
            dialog.eventLogical(key); REQUIRE(dialog.finished());
            CHECK(dialog.result()==(canContinue ? InGameEndOfGameScreen::CONTINUE : InGameEndOfGameScreen::QUIT));
        }
        InGameEndOfGameScreen accepted("Outcome",true); accepted.attach(*globals->gfx); accepted.update(0);
        key.key.key=SDLK_RETURN; accepted.eventLogical(key);
        REQUIRE(accepted.finished()); CHECK(accepted.result()==InGameEndOfGameScreen::QUIT);
    }

}
