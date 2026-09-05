// SPDX-License-Identifier: GPL-3.0-or-later
// Real game rendering regression. Run with an isolated GLOB2_USER_DIR and
// either -g (OpenGL) or -G (software). No desktop input is generated.
#include "GlobalContainer.h"
#include "TorusPicking.h"
#include "DynamicClouds.h"
#include <SDL.h>
// Expose only camera state to advance transitions deterministically in tests.
#define private public
#include "TorusView.h"
#undef private
#include "GameGUI.h"
#include "Engine.h"
#include "Team.h"
#include "GameGUIKeyActions.h"
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif
#include <cassert>
#include <iostream>

GlobalContainer *globalContainer = nullptr;
int main(int argc, char **argv)
{
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    globalContainer = new GlobalContainer;
    globalContainer->parseArgs(argc, argv);
    globalContainer->load();
    if (SDL_GL_GetCurrentWindow())
        SDL_HideWindow(SDL_GL_GetCurrentWindow());
    {
        GameGUI gui;
        auto mapHeader = Engine::loadMapHeader("maps/Island_of_the_Renfur.map");
        GameHeader gameHeader;
        for (int i = 0; i < mapHeader.getNumberOfTeams(); ++i)
            gameHeader.getBasePlayer(i) =
                BasePlayer(i, "Test", i, i == 0 ? BasePlayer::P_LOCAL : BasePlayer::P_AI);
        gameHeader.setNumberOfPlayers(mapHeader.getNumberOfTeams());
        gui.localPlayer = gui.localTeamNo = 0;
        assert(gui.loadFromHeaders(mapHeader, gameHeader, true, true));
        gui.adjustLocalTeam();
        gui.adjustInitialViewport();
        // Upgrading an old keyboard layout must neither shadow custom keys
        // nor lose the new default when its key is available.
        KeyboardManager keyboard(GameGUIShortcuts);
        KeyboardShortcut custom;
        custom.interpret("<g>=pause game", GameGUIShortcuts);
        keyboard.getKeyboardShortcuts().clear();
        keyboard.getKeyboardShortcuts().push_back(custom);
        keyboard.addMissingDefaults(GameGUIKeyActions::getDefaultConfigurationFile());
        for (const auto &shortcut : keyboard.getKeyboardShortcuts())
            assert(shortcut.format(GameGUIShortcuts) != "<g>=toggle torus view");
        keyboard.getKeyboardShortcuts().clear();
        keyboard.addMissingDefaults(GameGUIKeyActions::getDefaultConfigurationFile());
        bool hasToggle = false;
        for (const auto &shortcut : keyboard.getKeyboardShortcuts())
            hasToggle |= shortcut.format(GameGUIShortcuts) == "<g>=toggle torus view";
        assert(hasToggle);
        TorusView view;
        const bool gpu = globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU;
        assert(view.available() == gpu);
        if (!gpu)
        {
            view.notifyMove();
            view.setHeld(true);
            view.toggle();
            assert(!view.active());
            int x = 0, y = 0, px, py;
            assert(!view.draw(gui.game, 0, 0, x, y, 960, 720));
            assert(!view.pick(480, 560, px, py));
            gui.drawAll(0);
            std::cout << "Software game rendering and inactive torus controls passed\n";
        }
        else
        {
#ifdef HAVE_OPENGL
            int x = 11, y = 13;
            auto draw = [&](float amount)
            {
                view.amount = amount;
                view.lastFrame = SDL_GetTicks();
                assert(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
                assert(glGetError() == GL_NO_ERROR);
            };
            view.notifyMove();
            assert(view.active());
            draw(0);
            for (float phase : {.01f, .25f, .5f, .75f, 1.f})
                draw(phase);
            // Test navigation separately from the expensive cloud layer.
            globalContainer->settings.optionFlags |= GlobalContainer::OPTION_LOW_SPEED_GFX;
            for (int i = 0; i < 20; ++i)
            {
                view.notifyMove();
                view.setViewport((x + 3) & gui.game.map.getMaskW(), (y + 5) & gui.game.map.getMaskH());
                draw(1);
                int px, py;
                assert(view.pick(480, 560, px, py));
            }
            view.setHeld(true);
            view.reset();
            assert(!view.active());
            int px, py;
            assert(!view.pick(480, 560, px, py));
            for (int i = 0; i < 3; ++i)
            {
                view.notifyMove();
                draw(0);
                draw(1);
                view.reset();
            }
            view.notifyMove();
            draw(0);
            view.lastMove = SDL_GetTicks() - 1000;
            view.lastFrame = SDL_GetTicks() - 100;
            view.amount = .1f;
            view.setPointerHeld(true);
            assert(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
            assert(view.amount == .1f);
            view.setPointerHeld(false);
            view.lastFrame = SDL_GetTicks() - 100;
            assert(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
            assert(!view.active());
            globalContainer->gfx->setClipRect();
            gui.drawAll(0);
            std::cout << "Navigation, GL state, picking and reload lifecycle passed\n";
#endif
        }
    }
    delete globalContainer;
}
