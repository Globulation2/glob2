// SPDX-License-Identifier: GPL-3.0-or-later
// Real game rendering regression. Run with an isolated GLOB2_USER_DIR and
// either -g (OpenGL) or -G (software). No desktop input is generated.
#include "GlobalContainer.h"
#include "TorusPicking.h"
#include "DynamicClouds.h"
#include <SDL.h>
// Expose camera and settings widgets for deterministic integration checks.
#define private public
#include "TorusView.h"
#include "SettingsScreen.h"
#undef private
#include "GameGUI.h"
#include "Engine.h"
#include "Team.h"
#include "Unit.h"
#include "GameGUIKeyActions.h"
#include "CloudField.h"
#include "GUIButton.h"
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
        // Units in the last 15 tiles of a full-world capture must keep their
        // visible copy's position, including movement across either seam.
        Unit *explorer = gui.game.addUnit(0, 0, 0, EXPLORER, 0, 128, 1, 1);
        assert(explorer);
        for (int x : {-1, 0, gui.game.map.getW() - 8, gui.game.map.getW()})
            for (int y : {-1, 0, gui.game.map.getH() - 8, gui.game.map.getH()})
            {
                gui.game.mouseX = x * 32;
                gui.game.mouseY = y * 32;
                gui.game.mouseUnit = nullptr;
                gui.game.drawUnit(x, y, explorer->gid, (-x) & gui.game.map.getMaskW(),
                    (-y) & gui.game.map.getMaskH(), gui.game.map.getW(), gui.game.map.getH(),
                    0, Game::DRAW_WHOLE_MAP);
                assert(gui.game.mouseUnit == explorer);
            }
        gui.game.mouseX = gui.game.mouseY = -1;
        gui.game.mouseUnit = nullptr;
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
        assert(!Settings().automaticTorus);
        globalContainer->settings.automaticTorus = false;
        {
            SettingsScreen options;
            const int oldMute = globalContainer->settings.mute;
            assert(!options.automaticTorus->getState());
            options.automaticTorus->setState(true);
            options.onAction(options.automaticTorus, BUTTON_STATE_CHANGED, SettingsScreen::AUTOMATIC_TORUS, 0);
            assert(globalContainer->settings.automaticTorus);
            assert(globalContainer->settings.mute == oldMute);
            options.onAction(nullptr, BUTTON_RELEASED, SettingsScreen::OK, 0);
        }
        Settings restored;
        restored.load();
        assert(restored.automaticTorus);
        {
            SettingsScreen options;
            assert(options.automaticTorus->getState());
            options.automaticTorus->setState(false);
            options.onAction(options.automaticTorus, BUTTON_STATE_CHANGED, SettingsScreen::AUTOMATIC_TORUS, 0);
            assert(!globalContainer->settings.automaticTorus);
            options.onAction(nullptr, BUTTON_RELEASED, SettingsScreen::CANCEL, 0);
            assert(globalContainer->settings.automaticTorus);
        }
        {
            SettingsScreen options;
            options.automaticTorus->setState(false);
            options.onAction(options.automaticTorus, BUTTON_STATE_CHANGED, SettingsScreen::AUTOMATIC_TORUS, 0);
            options.onAction(nullptr, BUTTON_RELEASED, SettingsScreen::OK, 0);
        }
        restored.load();
        assert(!restored.automaticTorus);
        TorusView view;
        view.notifyMove();
        assert(!view.active());
        for (int i = 0; i < 100; ++i)
        {
            view.setViewport(i, i / 2);
            assert(!view.active());
        }
        const bool gpu = globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU;
        assert(view.available() == gpu);
        if (!gpu)
        {
            globalContainer->settings.automaticTorus = true;
            view.notifyMove();
            view.toggle();
            assert(!view.active());
            globalContainer->settings.automaticTorus = false;
            int x = 0, y = 0, px, py;
            assert(!view.draw(gui.game, 0, 0, x, y, 960, 720));
            assert(!view.pick(480, 560, px, py));
            gui.drawAll(0);
            std::cout << "Software game rendering and inactive torus controls passed\n";
        }
        else
        {
#ifdef HAVE_OPENGL
            // Variable-size resource batching must preserve pixels, frame bounds,
            // transparency and ordering at native and overview scales.
            Sprite resources;
            assert(resources.load("data/gfx/ressource"));
            GLint viewport[4];
            glGetIntegerv(GL_VIEWPORT, viewport);
            auto captureResources = [&](float scale, Uint8 alpha)
            {
                globalContainer->gfx->setClipRect();
                glClearColor(.17f, .29f, .43f, 1);
                glClear(GL_COLOR_BUFFER_BIT);
                for (int i = 0; i < resources.getFrameCount(); ++i)
                    globalContainer->gfx->drawSprite(30.f + (i % 8) * 52, 30.f + (i / 8) * 52,
                        resources.getW(i) * scale, resources.getH(i) * scale, &resources, i, alpha);
                globalContainer->gfx->finishDrawingSprite(&resources, alpha);
                std::vector<unsigned char> pixels(viewport[2] * viewport[3] * 4);
                glReadPixels(0, 0, viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                return pixels;
            };
            std::vector<std::vector<unsigned char>> reference;
            for (float scale : {1.f, .5f, .75f})
                for (Uint8 alpha : {Uint8(255), Uint8(127)})
                    reference.push_back(captureResources(scale, alpha));
            std::vector<std::pair<int, int>> sizes;
            for (int i = 0; i < resources.getFrameCount(); ++i)
                sizes.emplace_back(resources.getW(i), resources.getH(i));
            assert(resources.createTextureAtlas(true));
            int sample = 0, maximumDifference = 0;
            for (float scale : {1.f, .5f, .75f})
                for (Uint8 alpha : {Uint8(255), Uint8(127)})
                {
                    auto actual = captureResources(scale, alpha);
                    for (size_t i = 0; i < actual.size(); ++i)
                        maximumDifference = std::max(maximumDifference,
                            std::abs(int(actual[i]) - reference[sample][i]));
                    ++sample;
                }
            for (int i = 0; i < resources.getFrameCount(); ++i)
                assert(sizes[i] == std::make_pair(resources.getW(i), resources.getH(i)));
            std::cout << "Resource atlas maximum pixel difference: " << maximumDifference << "/255\n";
            assert(maximumDifference <= 1);
            DynamicClouds clouds(&globalContainer->settings);
            std::valarray<unsigned char> pixels;
            int gridW, gridH;
            clouds.computeWorld(256, 256, 250, pixels, gridW, gridH, 128);
            assert(gridW == 128 && gridH == 128);
            const auto &settings = globalContainer->settings;
            CloudField field(8192, 8192, 250, settings.cloudSize, settings.cloudStability,
                settings.cloudMaxSpeed, settings.cloudWindStability, settings.cloudMaxAlpha);
            for (int row = 0; row < gridH; ++row)
                for (int col = 0; col < gridW; ++col)
                    assert(pixels[row * gridW + col] == field.opacity(col * 64, row * 64,
                        std::max(.01f, settings.cloudHeight / 100.f)));
            int x = 11, y = 13;
            auto draw = [&](float amount)
            {
                view.amount = amount;
                view.lastFrame = SDL_GetTicks();
                assert(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
                assert(glGetError() == GL_NO_ERROR);
            };
            view.toggle();
            assert(view.active());
            draw(0);
            for (float phase : {.01f, .25f, .5f, .75f, 1.f})
                draw(phase);
            // Test navigation separately from the expensive cloud layer.
            globalContainer->settings.optionFlags |= GlobalContainer::OPTION_LOW_SPEED_GFX;
            for (int i = 0; i < 20; ++i)
            {
                view.setViewport((x + 3) & gui.game.map.getMaskW(), (y + 5) & gui.game.map.getMaskH());
                draw(1);
                int px, py;
                assert(view.pick(480, 560, px, py));
            }
            view.reset();
            assert(!view.active());
            int px, py;
            assert(!view.pick(480, 560, px, py));
            for (int i = 0; i < 3; ++i)
            {
                view.toggle();
                draw(0);
                draw(1);
                view.reset();
            }
            view.toggle();
            draw(0);
            view.toggle();
            view.lastFrame = SDL_GetTicks() - 100;
            view.amount = .04f;
            assert(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
            assert(!view.active());
            // Automatic motion opens slowly and returns quickly after inactivity.
            globalContainer->settings.automaticTorus = true;
            view.notifyMove();
            assert(view.active() && !view.enabled());
            draw(0);
            view.amount = .25f;
            view.lastMove = SDL_GetTicks();
            view.lastFrame = SDL_GetTicks() - 100;
            assert(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
            assert(view.amount > .25f && view.amount <= .28f);
            // Neither folding nor automatic return changes an active gesture's projection.
            view.setPointerHeld(true);
            float heldAmount = view.amount;
            view.lastMove = SDL_GetTicks() - 300;
            view.lastFrame = SDL_GetTicks() - 100;
            assert(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
            assert(view.amount == heldAmount);
            view.setPointerHeld(false);
            view.lastFrame = SDL_GetTicks() - 100;
            assert(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
            assert(view.amount < heldAmount - .2f);
            // G pins the overview even when no movement notifications arrive.
            view.toggle();
            view.lastMove = SDL_GetTicks() - 300;
            draw(1);
            assert(view.enabled() && view.amount == 1);
            view.toggle();
            view.amount = .1f;
            view.lastFrame = SDL_GetTicks() - 100;
            assert(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
            assert(!view.active());
            // Disabling the preference clears an automatic reveal before its first frame.
            view.notifyMove();
            globalContainer->settings.automaticTorus = false;
            assert(!view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
            assert(!view.active());
            view.notifyMove();
            assert(!view.active());
            globalContainer->gfx->setClipRect();
            gui.drawAll(0);
            std::cout << "Manual/automatic modes, saved option and pointer hold passed\n";
            std::cout << "Navigation, GL state, picking and reload lifecycle passed\n";
#endif
        }
    }
    delete globalContainer;
}
