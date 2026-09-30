// SPDX-License-Identifier: GPL-3.0-or-later
// Real game rendering regression. Run with an isolated GLOB2_USER_DIR and
// either -g (OpenGL) or -G (software). No desktop input is generated.
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "TorusPicking.h"
#include "TorusGeometry.h"
#include "DynamicClouds.h"
#include <SDL.h>
// Expose camera and settings widgets for deterministic integration checks.
#include "TorusView.h"
#include "SettingsScreen.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "gui/GameGUIViewport.h"
#include "Engine.h"
#include "Team.h"
#include "TorusMapFixture.h"
#include "Unit.h"
#include "GameGUIKeyActions.h"
#include "CloudField.h"
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif
#include <cmath>
#include <iostream>

class TorusRenderIntegrationTest
{
public:
static void run(bool gpu, int width, int height)
{
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    // The old harness ran with -F (windowed) -m (mute) -s WxH and -g or -G.
    glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.display = true, .loadStrings = true, .width = width, .height = height,
                                                                 .screenFlags = gpu ? Uint32(GraphicContext::USEGPU) : 0u});
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
        REQUIRE(gui.loadFromHeaders(mapHeader, gameHeader, true, true));
        gui.adjustLocalTeam();
        gui.adjustInitialViewport();
        gui.updateCamera();
        // A drag beginning on empty ground moves the map with the pointer.
        // Find an actual empty visible tile so the fixture can change freely.
        int dragX = -1, dragY = -1;
        for (int y = 64; y < globalContainer->gfx->getH() - 64 && dragX < 0; y += 24)
            for (int x = 64; x < globalContainer->gfx->getW() - GAME_GUI_RIGHT_MENU_WIDTH - 64; x += 24)
            {
                int tileX, tileY;
                gui.game.map.displayToMapCaseAligned(gui.mapMouseX(x), gui.mapMouseY(y),
                    &tileX, &tileY, gui.viewportX, gui.viewportY);
                bool flagHere = false;
                for (const auto *flag : gui.localTeam->virtualBuildings)
                    flagHere |= gui.displayedPosX(*flag) == tileX &&
                        gui.displayedPosY(*flag) == tileY;
                if (gui.game.map.getBuilding(tileX, tileY) == NOGBID &&
                    !flagHere &&
                    !gui.game.map.isResource(tileX, tileY) &&
                    gui.game.map.getGroundUnit(tileX, tileY) == NOGUID &&
                    gui.game.map.getAirUnit(tileX, tileY) == NOGUID)
                {
                    dragX = x;
                    dragY = y;
                    break;
                }
            }
        REQUIRE(dragX >= 0);
        gui.view.mouseUnit = nullptr;
        SDL_MouseButtonEvent down{};
        down.button = SDL_BUTTON_LEFT;
        down.x = dragX;
        down.y = dragY;
        gui.handleMouseButtonDown(down);
        REQUIRE(gui.mapPanPushed);
        const double originX = gui.camera.originX;
        gui.handleMouseMotion(dragX + 12, dragY + 8, SDL_BUTTON(SDL_BUTTON_LEFT));
        REQUIRE(std::abs(gui.camera.originX -
            MapCamera::wrap(originX - 12 / gui.camera.zoom, gui.camera.mapWidth)) < 0.01);
        gui.handleMouseButtonUp(down);
        REQUIRE(!gui.mapPanPushed);
        const double releasedX = gui.camera.originX;
        gui.handleMouseMotion(dragX + 24, dragY + 8, 0);
        REQUIRE(gui.camera.originX == releasedX);
        // Building and resource tiles must accept the same pan and wheel
        // gestures as empty ground, without queuing building orders.
        std::pair<int, int> building{-1, -1}, resource{-1, -1};
        for (int y = 0; y < gui.game.map.getH(); ++y)
            for (int x = 0; x < gui.game.map.getW(); ++x)
            {
                if (building.first < 0 && gui.game.map.getBuilding(x, y) != NOGBID)
                    building = {x, y};
                if (resource.first < 0 && gui.game.map.getBuilding(x, y) == NOGBID &&
                    gui.game.map.isResource(x, y))
                    resource = {x, y};
            }
        REQUIRE((building.first >= 0 && resource.first >= 0));
        for (const auto tile : {building, resource})
        {
            gui.camera.originX = tile.first * 32 + 16 - gui.camera.visibleW() / 2;
            gui.camera.originY = tile.second * 32 + 16 - gui.camera.visibleH() / 2;
            gui.camera.normalize();
            gui.viewportX = gui.camera.tileX();
            gui.viewportY = gui.camera.tileY();
            gui.updateCamera();
            const int x = int(gui.camera.offsetX + gui.camera.width / 2);
            const int y = int(gui.camera.offsetY + gui.camera.height / 2);
            gui.view.mouseUnit = nullptr;
            SDL_MouseButtonEvent press{};
            press.button = SDL_BUTTON_LEFT;
            press.x = x;
            press.y = y;
            const auto ordersBefore = gui.orderQueue.size();
            gui.handleMouseButtonDown(press);
            REQUIRE(gui.mapPanPushed);
            const double beforePan = gui.camera.originX;
            gui.handleMouseMotion(x + 12, y + 8, SDL_BUTTON(SDL_BUTTON_LEFT));
            REQUIRE(std::abs(gui.camera.originX -
                MapCamera::wrap(beforePan - 12 / gui.camera.zoom, gui.camera.mapWidth)) < 0.01);
            SDL_Event release{};
            release.type = SDL_MOUSEBUTTONUP;
            release.button.button = SDL_BUTTON_LEFT;
            release.button.x = x;
            release.button.y = y;
            gui.processEvent(&release);
            REQUIRE(gui.orderQueue.size() == ordersBefore);
            gui.mouseX = x;
            gui.mouseY = y;
            SDL_Event wheel{};
            wheel.type = SDL_MOUSEWHEEL;
            wheel.wheel.y = 1;
#if SDL_VERSION_ATLEAST(2,0,18)
            wheel.wheel.preciseY = 1;
#endif
            const double beforeZoom = gui.camera.zoom;
            gui.processEvent(&wheel);
            REQUIRE(gui.camera.zoom > beforeZoom);
            REQUIRE(gui.orderQueue.size() == ordersBefore);
        }
        // A direct flag grab keeps its existing move gesture.
        if (!gui.localTeam->virtualBuildings.empty())
        {
            auto *flag = gui.localTeam->virtualBuildings.front();
            gui.camera.originX = gui.displayedPosX(*flag) * 32 + 16 - gui.camera.visibleW() / 2;
            gui.camera.originY = gui.displayedPosY(*flag) * 32 + 16 - gui.camera.visibleH() / 2;
            gui.camera.normalize();
            gui.viewportX = gui.camera.tileX();
            gui.viewportY = gui.camera.tileY();
            gui.updateCamera();
            const int x = int(gui.camera.offsetX + gui.camera.width / 2);
            const int y = int(gui.camera.offsetY + gui.camera.height / 2);
            gui.view.mouseUnit = nullptr;
            SDL_MouseButtonEvent press{};
            press.button = SDL_BUTTON_LEFT;
            press.x = x;
            press.y = y;
            gui.handleMouseButtonDown(press);
            REQUIRE(!gui.mapPanPushed);
            const double origin = gui.camera.originX;
            const int flagX = gui.displayedPosX(*flag);
            gui.handleMouseMotion(x + 96, y, SDL_BUTTON(SDL_BUTTON_LEFT));
            REQUIRE(gui.camera.originX == origin);
            REQUIRE(gui.displayedPosX(*flag) != flagX);
            gui.handleMouseButtonUp(press);
        }
        // Units in the last 15 tiles of a full-world capture must keep their
        // visible copy's position, including movement across either seam.
        Unit *explorer = gui.game.addUnit(0, 0, 0, EXPLORER, 0, 128, 1, 1);
        REQUIRE(explorer);
        for (int x : {-1, 0, gui.game.map.getW() - 8, gui.game.map.getW()})
            for (int y : {-1, 0, gui.game.map.getH() - 8, gui.game.map.getH()})
            {
                gui.view.mouseX = x * 32;
                gui.view.mouseY = y * 32;
                gui.view.mouseUnit = nullptr;
                gui.game.drawUnit(x, y, explorer->gid, (-x) & gui.game.map.getMaskW(),
                    (-y) & gui.game.map.getMaskH(), gui.game.map.getW(), gui.game.map.getH(),
                    0, Game::DRAW_WHOLE_MAP, gui.view);
                REQUIRE(gui.view.mouseUnit == explorer);
            }
        gui.view.mouseX = gui.view.mouseY = -1;
        gui.view.mouseUnit = nullptr;
        // Upgrading an old keyboard layout must neither shadow custom keys
        // nor lose the new default when its key is available.
        KeyboardManager keyboard(GameGUIShortcuts);
        KeyboardShortcut custom;
        custom.interpret("<g>=pause game", GameGUIShortcuts);
        keyboard.getKeyboardShortcuts().clear();
        keyboard.getKeyboardShortcuts().push_back(custom);
        keyboard.addMissingDefaults(GameGUIKeyActions::getDefaultConfigurationFile());
        for (const auto &shortcut : keyboard.getKeyboardShortcuts())
            REQUIRE(shortcut.format(GameGUIShortcuts) != "<g>=toggle torus view");
        keyboard.getKeyboardShortcuts().clear();
        keyboard.addMissingDefaults(GameGUIKeyActions::getDefaultConfigurationFile());
        bool hasToggle = false;
        for (const auto &shortcut : keyboard.getKeyboardShortcuts())
            hasToggle |= shortcut.format(GameGUIShortcuts) == "<g>=toggle torus view";
        REQUIRE(hasToggle);
        REQUIRE(!Settings().automaticTorus);
        globalContainer->settings.automaticTorus = false;
        {
            SettingsScreen options;
            const int oldMute = globalContainer->settings.mute;
            const bool oldHighResolution = globalContainer->settings.highResolutionArtwork;
            REQUIRE(options.changeSetting("graphics.torus",1));
            REQUIRE(globalContainer->settings.automaticTorus);
            REQUIRE(globalContainer->settings.mute == oldMute);
            REQUIRE(globalContainer->settings.highResolutionArtwork == oldHighResolution);
            options.done();
        }
        Settings restored;
        restored.load();
        REQUIRE(restored.automaticTorus);
        {
            SettingsScreen options;
            // Discrete settings save immediately, including when the screen
            // closes without a separate Save action.
            REQUIRE(options.changeSetting("graphics.torus",0));
            REQUIRE(!globalContainer->settings.automaticTorus);
            restored.load();
            REQUIRE(!restored.automaticTorus);
            options.done();
            REQUIRE(!globalContainer->settings.automaticTorus);
        }
        restored.load();
        REQUIRE(!restored.automaticTorus);
        TorusView view;
        view.notifyMove();
        REQUIRE(!view.active());
        for (int i = 0; i < 100; ++i)
        {
            view.setViewport(i, i / 2);
            REQUIRE(!view.active());
        }
        const bool gpu = globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU;
        REQUIRE(view.available() == gpu);
        if (!gpu)
        {
            globalContainer->settings.automaticTorus = true;
            view.notifyMove();
            view.toggle();
            REQUIRE(!view.active());
            globalContainer->settings.automaticTorus = false;
            int x = 0, y = 0, px, py;
            REQUIRE(!view.draw(gui.game, 0, 0, x, y, 960, 720));
            REQUIRE(!view.pick(480, 560, px, py));
            gui.drawAll(0);
            std::cout << "Software game rendering and inactive torus controls passed\n";
        }
        else
        {
#ifdef HAVE_OPENGL
            // Variable-size resource batching must preserve pixels, frame bounds,
            // transparency and ordering at native and overview scales.
            for (bool highResolution : {false, true})
            {
                Sprite::setHighResolution(highResolution);
                Sprite resources;
                REQUIRE(resources.load("data/gfx/ressource"));
                GLint viewport[4];
                glGetIntegerv(GL_VIEWPORT, viewport);
                auto captureResources = [&](float scale, Uint8 alpha, bool immediate)
                {
                    globalContainer->gfx->setClipRect();
                    glClearColor(.17f, .29f, .43f, 1);
                    glClear(GL_COLOR_BUFFER_BIT);
                    for (int i = 0; i < resources.getFrameCount(); ++i)
                    {
                        const int frame = i % 2 ? resources.getFrameCount() - 1 - i / 2 : i / 2;
                        const int x = 30 + (i % 8) * 52, y = 30 + (i / 8) * 52;
                        if (scale == 1.f)
                            globalContainer->gfx->drawSprite(x, y, &resources, frame, alpha);
                        else
                            globalContainer->gfx->drawSprite(float(x), float(y), resources.getW(frame) * scale,
                                resources.getH(frame) * scale, &resources, frame, alpha);
                        if (immediate)
                            globalContainer->gfx->finishDrawingSprite(&resources, alpha);
                    }
                    globalContainer->gfx->finishDrawingSprite(&resources, alpha);
                    std::vector<unsigned char> pixels(viewport[2] * viewport[3] * 4);
                    glReadPixels(0, 0, viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                    return pixels;
                };
                std::vector<std::vector<unsigned char>> reference;
                for (float scale : {1.f, .5f, .75f, 2.f})
                    for (Uint8 alpha : {Uint8(255), Uint8(127)})
                        reference.push_back(captureResources(scale, alpha, true));
                std::vector<std::pair<int, int>> sizes;
                for (int i = 0; i < resources.getFrameCount(); ++i)
                    sizes.emplace_back(resources.getW(i), resources.getH(i));
                REQUIRE(resources.createTextureAtlas(true));
                int sample = 0, maximumDifference = 0;
                for (float scale : {1.f, .5f, .75f, 2.f})
                    for (Uint8 alpha : {Uint8(255), Uint8(127)})
                    {
                        auto actual = captureResources(scale, alpha, false);
                        for (size_t i = 0; i < actual.size(); ++i)
                            maximumDifference = std::max(maximumDifference,
                                std::abs(int(actual[i]) - reference[sample][i]));
                        ++sample;
                    }
                for (int i = 0; i < resources.getFrameCount(); ++i)
                    REQUIRE(sizes[i] == std::make_pair(resources.getW(i), resources.getH(i)));
                std::cout << (highResolution ? "HD" : "Original") << " resource atlas maximum pixel difference: " << maximumDifference << "/255\n";
                REQUIRE(maximumDifference <= 1);
            }
            Sprite::setHighResolution(globalContainer->settings.highResolutionArtwork);
            DynamicClouds clouds(&globalContainer->settings);
            std::valarray<unsigned char> pixels;
            int gridW, gridH;
            clouds.computeWorld(256, 256, 250, pixels, gridW, gridH, 128);
            REQUIRE((gridW == 128 && gridH == 128));
            const auto &settings = globalContainer->settings;
            CloudField field(8192, 8192, 250, settings.cloudSize, settings.cloudStability,
                settings.cloudMaxSpeed, settings.cloudWindStability, settings.cloudMaxAlpha);
            for (int row = 0; row < gridH; ++row)
                for (int col = 0; col < gridW; ++col)
                    REQUIRE(pixels[row * gridW + col] == field.opacity(col * 64, row * 64,
                        std::max(.01f, settings.cloudHeight / 100.f)));
            int x = 11, y = 13;
            auto draw = [&](float amount)
            {
                view.amount = amount;
                view.lastFrame = SDL_GetTicks();
                REQUIRE(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
                REQUIRE(glGetError() == GL_NO_ERROR);
            };
            view.toggle();
            REQUIRE(view.active());
            draw(0);
            for (float phase : {.01f, .25f, .5f, .75f, 1.f})
                draw(phase);
            gui.gamePaused = true;
            const int pausedTime = gui.game.mapAnimationTime;
            draw(1);
            const auto pausedClouds = view.cloudPixels;
            draw(1);
            REQUIRE(gui.game.mapAnimationTime == pausedTime);
            REQUIRE(pausedClouds.size() == view.cloudPixels.size());
            for (size_t i = 0; i < pausedClouds.size(); ++i)
                REQUIRE(pausedClouds[i] == view.cloudPixels[i]);
            gui.gamePaused = false;
            draw(1);
            REQUIRE(gui.game.mapAnimationTime > pausedTime);
            // Selection markers are painted into the atlas, which is measured in
            // world pixels. The factor the window stretches the interface by must
            // not reach their line width, and every marker the flat view paints
            // over the map has to be on the ring too.
            {
                const float windowScale = globalContainer->gfx->getRasterScale();
                Building *selected = nullptr;
                for (int i = 0; i < 1024 && !selected; ++i)
                    selected = gui.game.teams[0]->myBuildings[i];
                REQUIRE(selected);
                gui.showUnitWorkingToBuilding = true;
                gui.setSelection(GameGUI::BUILDING_SELECTION, selected);
                REQUIRE(gui.view.selectedBuilding == selected);
                const int worldW = gui.game.map.getW() * 32, worldH = gui.game.map.getH() * 32;
                std::vector<unsigned char> atlas;
                // The atlas holds one upright copy of the world; GL hands rows back bottom-up.
                auto texel = [&](int worldX, int worldY)
                {
                    const int col = ((worldX % worldW) + worldW) % worldW * view.atlasW / worldW;
                    const int row = view.atlasH - 1 -
                        ((worldY % worldH) + worldH) % worldH * view.atlasH / worldH;
                    return &atlas[(size_t(row) * view.atlasW + col) * 4];
                };
                auto capture = [&]()
                {
                    draw(1);
                    // Software GL renders on worker threads, and the readback below has been
                    // seen to return the previous frame's atlas: wait for the frame first.
                    glFinish();
                    atlas.assign(size_t(view.atlasW) * view.atlasH * 4, 0);
                    glBindTexture(GL_TEXTURE_2D, view.texture);
                    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.data());
                    REQUIRE(glGetError() == GL_NO_ERROR);
                };
                // A worker circle is white over whatever it covers, so count the
                // bright pixels its own tile gains rather than trusting a threshold.
                int ux, uy;
                gui.game.map.mapCaseToDisplayable(explorer->posX, explorer->posY, &ux, &uy,
                    view.originX, view.originY);
                auto brightAroundWorker = [&]()
                {
                    int bright = 0;
                    for (int dy = -40; dy <= 72; ++dy)
                        for (int dx = -40; dx <= 72; ++dx)
                        {
                            const unsigned char *p = texel(ux + dx, uy + dy);
                            if (p[0] > 150 && p[1] > 150 && p[2] > 150)
                                ++bright;
                        }
                    return bright;
                };
                capture();
                const int withoutWorker = brightAroundWorker();
                selected->unitsWorking.push_back(explorer);
                capture();
                const int withWorker = brightAroundWorker();
                selected->unitsWorking.clear();
                std::cout << "Torus worker marker bright pixels: " << withoutWorker << " -> "
                          << withWorker << "\n";
                REQUIRE(withWorker > withoutWorker + 16);
                // Walk outwards from the ring's left edge: at one texel per world
                // pixel the selection circle is the two pixels drawCircle asks for.
                int cx, cy;
                gui.game.map.buildingPosToCursor(selected->posX, selected->posY, selected->type->width,
                    selected->type->height, &cx, &cy, view.originX, view.originY);
                int stroke = 0;
                for (int d = -8; d <= 8; ++d)
                {
                    const unsigned char *p = texel(cx - selected->type->width * 16 + d, cy);
                    if (p[2] > 110 && p[0] < 90 && p[1] < 90)
                        ++stroke;
                }
                std::cout << "Torus selection circle: " << stroke << " atlas texels wide at interface scale "
                          << windowScale << "\n";
                REQUIRE((stroke > 0 && stroke <= 3));
                // The window is the drawable again once the atlas pass is over.
                REQUIRE(globalContainer->gfx->getRasterScale() == windowScale);
                gui.clearSelection();
                std::cout << "Torus selection markers passed\n";
            }
            // Test navigation separately from the expensive cloud layer.
            globalContainer->settings.optionFlags |= GlobalContainer::OPTION_LOW_SPEED_GFX;
            for (int i = 0; i < 20; ++i)
            {
                view.setViewport((x + 3) & gui.game.map.getMaskW(), (y + 5) & gui.game.map.getMaskH());
                draw(1);
                int px, py;
                REQUIRE(view.pick(480, 560, px, py));
            }
            view.reset();
            REQUIRE(!view.active());
            int px, py;
            REQUIRE(!view.pick(480, 560, px, py));
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
            REQUIRE(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
            REQUIRE(!view.active());
            // Automatic motion opens slowly and returns quickly after inactivity.
            globalContainer->settings.automaticTorus = true;
            view.notifyMove();
            REQUIRE((view.active() && !view.enabled()));
            draw(0);
            view.amount = .25f;
            view.lastMove = SDL_GetTicks();
            view.lastFrame = SDL_GetTicks() - 100;
            REQUIRE(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
            REQUIRE((view.amount > .25f && view.amount <= .28f));
            // Neither folding nor automatic return changes an active gesture's projection.
            view.setPointerHeld(true);
            float heldAmount = view.amount;
            view.lastMove = SDL_GetTicks() - 300;
            view.lastFrame = SDL_GetTicks() - 100;
            REQUIRE(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
            REQUIRE(view.amount == heldAmount);
            view.setPointerHeld(false);
            view.lastFrame = SDL_GetTicks() - 100;
            REQUIRE(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
            REQUIRE(view.amount < heldAmount - .2f);
            // G pins the overview even when no movement notifications arrive.
            view.toggle();
            view.lastMove = SDL_GetTicks() - 300;
            draw(1);
            REQUIRE((view.enabled() && view.amount == 1));
            view.toggle();
            view.amount = .1f;
            view.lastFrame = SDL_GetTicks() - 100;
            REQUIRE(view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
            REQUIRE(!view.active());
            // Disabling the preference clears an automatic reveal before its first frame.
            view.notifyMove();
            globalContainer->settings.automaticTorus = false;
            REQUIRE(!view.draw(gui.game, 0, Game::DRAW_WHOLE_MAP, x, y, 960, 720));
            REQUIRE(!view.active());
            view.notifyMove();
            REQUIRE(!view.active());
            globalContainer->gfx->setClipRect();
            gui.drawAll(0);
            // Preserve the map focus when entering from shared 2D zoom.
            for (double flatZoom : {.5, 1.0, 2.0})
            {
                gui.camera.zoom = flatZoom;
                gui.camera.originX = gui.viewportX * 32.0 + 7;
                gui.camera.originY = gui.viewportY * 32.0 + 11;
                gui.updateCamera();
                const int cx = (globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)/2;
                const int cy = (globalContainer->gfx->getH()+16)/2;
                const auto center = gui.camera.screenToWorld(cx, cy);
                gui.torusView.reset();
                gui.torusView.toggle();
                for (float phase : {0.f, .5f, 1.f})
                {
                    gui.torusView.amount = phase;
                    gui.torusView.lastFrame = SDL_GetTicks();
                    gui.drawAll(0);
                    int px, py;
                    REQUIRE(gui.torusView.pick(cx, cy, px, py));
                    REQUIRE(std::abs(TorusGeometry::wrappedDelta(px, int(center.first), gui.game.map.getW()*32)) <= 2);
                    REQUIRE(std::abs(TorusGeometry::wrappedDelta(py, int(center.second), gui.game.map.getH()*32)) <= 2);
                    REQUIRE(glGetError() == GL_NO_ERROR);
                }
                gui.torusView.reset();
                gui.drawAll(0);
            }
            std::cout << "Manual/automatic modes, saved option and pointer hold passed\n";
            std::cout << "Navigation, GL state, picking and reload lifecycle passed\n";
            // Reuse the renderer across changing map shapes at one window size.
            // This catches stale fitting/mesh caches as well as tall-map apertures.
            TorusView rectangularView;
            for (const auto &size : {std::make_pair(64, 128), std::make_pair(512, 64),
                                     std::make_pair(64, 512), std::make_pair(128, 128)})
            {
                GameGUI rectangular;
                rectangular.init();
                makeTorusMapFixture(rectangular.game, size.first, size.second);
                rectangular.localPlayer = rectangular.localTeamNo = 0;
                rectangular.adjustLocalTeam();
                rectangularView.reset();
                rectangularView.toggle();
                int vx = 0, vy = 0;
                for (float phase : {0.f, .1f, .5f, 1.f})
                {
                    rectangularView.amount = phase;
                    rectangularView.lastFrame = SDL_GetTicks();
                    REQUIRE(rectangularView.draw(rectangular.game, 0, Game::DRAW_WHOLE_MAP, vx, vy, 960, 720));
                    REQUIRE(glGetError() == GL_NO_ERROR);
                    // Picking the original screen center must still reach the
                    // same map location after each step of the transition.
                    int px, py;
                    REQUIRE(rectangularView.pick(480, 368, px, py));
                    const int expectedX = TorusPicking::worldPixel(rectangularView.pickU, rectangularView.originX, size.first);
                    const int expectedY = TorusPicking::worldPixel(rectangularView.pickV, rectangularView.originY, size.second);
                    REQUIRE(std::abs(TorusGeometry::wrappedDelta(px, expectedX, size.first * 32)) <= 2);
                    REQUIRE(std::abs(TorusGeometry::wrappedDelta(py, expectedY, size.second * 32)) <= 2);
                }
                REQUIRE(rectangularView.ringMapAspect == float(size.first) / size.second);
                for (const auto &vertex : rectangularView.vertices)
                {
                    float x = vertex.position[0] / vertex.position[3];
                    float y = vertex.position[1] / vertex.position[3];
                    REQUIRE((std::isfinite(x) && std::isfinite(y)));
                    REQUIRE((x >= 0 && x <= 960)); // The distant rim may crop vertically.
                }
                rectangularView.setViewport(size.first - 1, size.second - 1);
                REQUIRE(rectangularView.draw(rectangular.game, 0, Game::DRAW_WHOLE_MAP, vx, vy, 960, 720));
                int hits = 0;
                for (int y = 100; y < 700; y += 40)
                    for (int x = 100; x < 900; x += 40)
                    {
                        int px, py;
                        if (rectangularView.pick(x, y, px, py))
                        {
                            REQUIRE((px >= 0 && px < size.first * 32 && py >= 0 && py < size.second * 32));
                            ++hits;
                        }
                    }
                REQUIRE(hits > 10);
            }
            std::cout << "Rectangular-map rendering, fitting, picking and cache changes passed\n";

#endif
        }
    }
}
};

TEST_SUITE("TorusRender")
{
	TEST_CASE("game rendering; picking and cache changes in software rendering [writes-preferences]") { TorusRenderIntegrationTest::run(false, 1120, 720); }
	TEST_CASE("game rendering; picking and cache changes in OpenGL [display][writes-preferences]") { TorusRenderIntegrationTest::run(true, 1120, 720); }
	TEST_CASE("game rendering at triple UI scale in OpenGL [display:1920x1440][writes-preferences]")
	{
		SDL_setenv("GLOB2_UI_SCALE", "3", 1);
		TorusRenderIntegrationTest::run(true, 1920, 1440);
		unsetenv("GLOB2_UI_SCALE");
	}
}
