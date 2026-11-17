#include "ComputeExecutor.h"
#include "sim/snapshot/SnapshotStore.h"
#include "Map.h"
#include "CommandLine.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include <PresentationWork.h>
// Loaded-game benchmark. Run with an isolated profile; no desktop input is generated.
#include "GlobalContainer.h"
#include "TorusPicking.h"
#include "DynamicClouds.h"
#include <any>
#define private public
#include "TorusView.h"
#include "render/MapDrawer.h"
#undef private
#include "GameGUI.h"
#include "Engine.h"
#include "ComputeThreads.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include "BuildingType.h"
#include "render/AnimationClock.h"
#include "Player.h"
#include "AI.h"
#include "Order.h"
#include "Utilities.h"
#include <BinaryStream.h>
#include <Toolkit.h>
#include <MapCamera.h>
#include <RenderBatch.h>
#include <RenderDevice.h>
#include <RendererSelection.h>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <MapGeometryCache.h>
#include <PerformanceTelemetry.h>
#include "TorusMapFixture.h"
#include "render/scene/SceneExtract.h"
#include "render/SoftwareTerrainCache.h"
#include "render/terrain/TerrainCompositor.h"
#include "render/terrain/DirectTerrain.h"
#include "render/RenderMemoryReport.h"
#include "render/OverviewTerrainCache.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <ctime>
#include <Environment.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#include "render/MapDrawer.h"
#endif
#endif

GlobalContainer *globalContainer = nullptr;
class TorusRenderBenchmark
{
    // The camera script's event on the frame being measured (FRAME_FALLBACK).
    static inline const char *cameraEvent = nullptr;
    static inline Uint64 benchmarkStart = 0;
    struct ScriptedCamera
    {
        double zoom;
        double x, y; // Top-left corner, in map pixels.
        const char *event;
    };
    // GLOB2_BENCH_CAMERA_SCRIPT=fallback: what a player does, in cycles of
    // eight seconds at 60 frames a second, each from a new place on the map,
    // starting with the game: a jump (as a minimap click), a hold, a pan at
    // fifteen tiles a second, six wheel steps out to the overview, a hold, six
    // steps back in and a hold.
    static ScriptedCamera scriptedCamera(int frame, double zoom, int mapW, int mapH)
    {
        constexpr int Cycle = 480;
        const int cycle = frame / Cycle, t = frame % Cycle;
        // Places spread over the map, far apart from one cycle to the next.
        const double placeX = std::fmod(0.13 + cycle * 0.618034, 1.0) * mapW * 32;
        const double placeY = std::fmod(0.29 + cycle * 0.414214, 1.0) * mapH * 32;
        ScriptedCamera camera{zoom, placeX, placeY, nullptr};
        if (t == 0)
            camera.event = cycle ? "jump" : "start";
        const int pan = std::clamp(t - 120, 0, 120);
        camera.x += pan * 8.0;
        camera.y += pan * 4.0;
        if (t == 120)
            camera.event = "pan";
        int steps = 0;
        if (t >= 240 && t < 360)
            steps = std::min(6, (t - 240) / 10 + 1);
        else if (t >= 360 && t < 420)
            steps = 6 - std::min(6, (t - 360) / 10 + 1);
        if (t >= 240 && t < 300 && (t - 240) % 10 == 0)
            camera.event = "zoom_out";
        if (t >= 360 && t < 420 && (t - 360) % 10 == 0)
            camera.event = "zoom_in";
        // GLOB2_BENCH_RAPID_ZOOM=N: a player spinning the wheel, one step every
        // N frames, out to six steps and back without pause, over the zoom window.
        static const int rapid = std::getenv("GLOB2_BENCH_RAPID_ZOOM") ? std::atoi(std::getenv("GLOB2_BENCH_RAPID_ZOOM")) : 0;
        if (rapid > 0 && t >= 240 && t < 420)
        {
            static const int depth = std::getenv("GLOB2_BENCH_RAPID_DEPTH") ? std::atoi(std::getenv("GLOB2_BENCH_RAPID_DEPTH")) : 6;
            const int step = (t - 240) / rapid, phase = step % (2 * depth);
            steps = phase <= depth ? phase : 2 * depth - phase;
            camera.event = (t - 240) % rapid ? nullptr : phase < depth ? "zoom_out" : "zoom_in";
            // GLOB2_BENCH_RAPID_HOLD: spin out once and hold the far zoom until
            // frame 360 of the cycle, then spin back in, to watch what the
            // farthest zoom shows on each frame after it arrives.
            static const bool hold = std::getenv("GLOB2_BENCH_RAPID_HOLD");
            if (hold)
            {
                const int in = t < 360 ? 0 : (t - 360) / rapid + 1;
                steps = t < 360 ? std::min(depth, step) : std::max(0, depth - in);
                camera.event = (t - 240) % rapid ? nullptr
                    : t < 360 ? (step < depth ? "zoom_out" : nullptr) : (in <= depth ? "zoom_in" : nullptr);
            }
        }
        // Wheel steps keep the view's centre where it was.
        const double scale = std::pow(0.7, steps);
        camera.zoom = zoom * scale;
        return camera;
    }
    static void prepareScene(GameGUI &gui)
    {
        gui.game.snapshots().invalidateBoundary(); // Synthetic fixtures edit public records.
        const auto request = gui.sceneRequest();
        SceneExtractor().prepare(gui.game.captureReadBoundary(SceneExtractor::captureOptions(request)), request, gui.view.render.ownScene);
        gui.view.scene = &gui.view.render.ownScene;
    }

    // GLOB2_BENCH_FOCUS names what a parity scene frames: tile:X,Y, or the
    // first building (site, upgrade, repair, damaged, complete, flag) or unit
    // (carrying, explorer, warrior, fighting, swimming) of that kind, the
    // local team's first. Returns the world pixel to centre on. Read only.
    static std::pair<double, double> focusPoint(Game &game, int localTeam, std::string_view kind)
    {
        int tileX = 0, tileY = 0;
        if (std::sscanf(std::string(kind).c_str(), "tile:%d,%d", &tileX, &tileY) == 2)
            return {tileX * 32.0 + 16, tileY * 32.0 + 16};
        const auto buildingMatches = [&](const Building &b) {
            const BuildingType &t = *b.type;
            if (kind == "flag") return t.isVirtual != 0;
            if (t.isVirtual) return false;
            if (kind == "site") return t.isBuildingSite && b.constructionResultState == Building::NEW_BUILDING;
            if (kind == "upgrade") return t.isBuildingSite && b.constructionResultState == Building::UPGRADE;
            if (kind == "repair") return t.isBuildingSite && b.constructionResultState == Building::REPAIR;
            if (kind == "damaged") return !t.isBuildingSite && t.hpMax && b.hp < t.hpMax;
            if (kind == "complete") return !t.isBuildingSite && t.maxUnitWorking && b.hp == t.hpMax;
            return false;
        };
        const auto unitMatches = [&](const Unit &u) {
            if (u.movement == Unit::MOV_INSIDE) return false;
            if (kind == "carrying") return u.typeNum == WORKER && u.carriedMaterial >= 0;
            if (kind == "explorer") return u.typeNum == EXPLORER;
            if (kind == "warrior") return u.typeNum == WARRIOR;
            if (kind == "fighting") return u.typeNum == WARRIOR && u.action == ATTACK_SPEED;
            if (kind == "swimming") return u.action == SWIM;
            return false;
        };
        const int teams = game.mapHeader.getNumberOfTeams();
        for (int i = 0; i < teams; ++i)
        {
            Team *team = game.teams[(localTeam + i) % teams];
            for (Building *b : team->liveBuildings.entries())
                if (buildingMatches(*b))
                {
                    std::printf("BENCH_FOCUS kind=%.*s team=%d tile=%d,%d\n", int(kind.size()), kind.data(), team->teamNumber, b->posX, b->posY);
                    return {b->posX * 32.0 + b->type->width * 16.0, b->posY * 32.0 + b->type->height * 16.0};
                }
            for (Unit *u : team->liveUnits.entries())
                if (unitMatches(*u))
                {
                    std::printf("BENCH_FOCUS kind=%.*s team=%d tile=%d,%d\n", int(kind.size()), kind.data(), team->teamNumber, u->posX, u->posY);
                    return {u->posX * 32.0 + 16, u->posY * 32.0 + 16};
                }
        }
        throw std::invalid_argument("GLOB2_BENCH_FOCUS found no " + std::string(kind));
    }

    // The game's initial view (the local team's colony) at the requested
    // zoom; GLOB2_BENCH_FOCUS centres it elsewhere, and GLOB2_BENCH_PAN_X/Y
    // move its top-left tile by that many tiles.
    static void prepareGuiScreen(GameGUI &gui, double zoom)
    {
        auto &settings = globalContainer->settings;
        settings.unitInterpolation = false;
        settings.buildingParticles = false;
        gui.drawHealthFoodBar = true;
        gui.prepareLocalPresentation();
        gui.updateCamera();
        gui.camera.setZoom(zoom, gui.camera.offsetX + gui.camera.width / 2, gui.camera.offsetY + gui.camera.height / 2);
        if (const char *focus = std::getenv("GLOB2_BENCH_FOCUS"))
        {
            const auto [x, y] = focusPoint(gui.game, gui.localTeamNo, focus);
            gui.camera.originX = x - gui.camera.visibleW() / 2;
            gui.camera.originY = y - gui.camera.visibleH() / 2;
        }
        if (const char *x = std::getenv("GLOB2_BENCH_PAN_X")) gui.camera.originX += 32.0 * std::atoi(x);
        if (const char *y = std::getenv("GLOB2_BENCH_PAN_Y")) gui.camera.originY += 32.0 * std::atoi(y);
        // Whole tiles, so sub-pixel camera offsets do not differ by renderer.
        gui.camera.originX = std::floor(gui.camera.originX / 32) * 32;
        gui.camera.originY = std::floor(gui.camera.originY / 32) * 32;
        gui.camera.normalize();
        gui.viewportX = gui.camera.tileX();
        gui.viewportY = gui.camera.tileY();
        // GLOB2_BENCH_SELECT=unit|building selects the local team's first such
        // entity, so the panel and the selection marker are in the picture.
        if (const char *select = std::getenv("GLOB2_BENCH_SELECT"))
        {
            const std::string_view kind(select);
            Team *team = gui.game.teams[gui.localTeamNo];
            bool selected = false;
            if (kind == "building")
                for (Building *b : team->liveBuildings.entries())
                    if (!selected && !b->type->isVirtual && b->type->maxUnitWorking)
                    {
                        gui.setSelection(GameGUI::BUILDING_SELECTION, b);
                        selected = true;
                    }
            if (kind == "unit")
                for (Unit *u : team->liveUnits.entries())
                    if (!selected)
                    {
                        gui.setSelection(GameGUI::UNIT_SELECTION, u);
                        selected = true;
                    }
            if (!selected) throw std::invalid_argument("GLOB2_BENCH_SELECT found nothing to select");
        }
        gui.game.snapshots().invalidateBoundary();
        gui.prepareLocalPresentation();
        std::printf("BENCH_GUI zoom=%.6f viewport=%d,%d\n", gui.camera.zoom, gui.viewportX, gui.viewportY);
    }

    static void drawGuiScreen(GameGUI &gui)
    {
        // The HUD's tick rate and the map animation follow the wall clock;
        // hold both so a capture does not depend on when it was taken.
        const Uint32 now = Uint32(SDL_GetTicks());
        gui.tickRate = {};
        gui.tickRate.sample(now - TickRateMeter::UPDATE_MS, 0, 0);
        gui.tickRate.sample(now, 0, 0);
        AnimationClock::fixedSteps = 1;
        gui.view.render.pinAnimation(21);
        gui.drawAll(gui.localTeamNo);
    }

    static int population(const Game &game)
    {
        int total = 0;
        for (int team = 0; team < game.mapHeader.getNumberOfTeams(); ++team)
            total += game.teams[team]->liveUnits.size();
        return total;
    }

    static int mappedPopulation(const Game &game)
    {
        int total = 0;
        for (int y = 0; y < game.map.getH(); ++y)
            for (int x = 0; x < game.map.getW(); ++x)
            {
                total += game.map.getGroundUnit(x, y) != NOGUID;
                total += game.map.getAirUnit(x, y) != NOGUID;
            }
        return total;
    }

    static void populateSynthetic(Game &game, int worldW, int worldH)
    {
        const char *requested = std::getenv("GLOB2_BENCH_UNITS");
        // Distinct interior cells avoid seam copies and viewport clipping.
        const int columns = std::max(0, std::min(worldW / 32, game.map.getW()) - 4);
        const int rows = std::max(0, std::min(worldH / 32, game.map.getH()) - 4);
        // The default fills the view with up to 1000 units; a small or close
        // view holds fewer. An explicit count must fit.
        const int count = requested ? std::atoi(requested) : std::min(1000, columns * rows);
        if (count < 0 || count > columns * rows)
            throw std::invalid_argument("GLOB2_BENCH_UNITS=" + std::to_string(count) + " does not fit the " +
                std::to_string(columns) + "x" + std::to_string(rows) + " interior cells of the view");
        for (int team = game.mapHeader.getNumberOfTeams(); team < Team::MAX_COUNT; ++team) game.addTeam();
        GameHeader header;
        header.setRandomSeed(1);
        header.setNumberOfPlayers(Team::MAX_COUNT);
        for (int team = 0; team < Team::MAX_COUNT; ++team)
            header.getBasePlayer(team) = BasePlayer(team, "Benchmark", team, BasePlayer::P_LOCAL);
        game.setGameHeader(header, false);
        // The synthetic flat map takes a unit in every cell; a real map's
        // water or obstacles refuse some, which are left empty.
        int placed = 0;
        for (int i = 0; i < count; ++i)
        {
            const int cell = int(static_cast<long long>(i) * columns * rows / std::max(1, count));
            placed += game.addUnit(2 + cell % columns, 2 + cell / columns,
                i % Team::MAX_COUNT, i % 3, 0, (i * 17) & 255, 0, 0) != nullptr;
        }
        std::printf("BENCH_UNITS requested=%d placed=%d\n", count, placed);
    }

    static void advanceAiTick(GameGUI &gui)
    {
        Game &game = gui.game;
        std::vector<unsigned> actors;
        for(int player=0;player<game.gameHeader.getNumberOfPlayers();++player)
            if(game.players[player]->ai)actors.push_back(unsigned(player));
        const auto& world=game.observeCompletedWorld();
        game.admitAIOrders(actors,false,{},world);
        for(const auto& [actor,scheduled]:game.deliverAIOrders(actors,false)) {
            scheduled->sender=actor;
            gui.executeOrder(game.validateAIOrder(scheduled,actor));
        }
        game.advanceLiveSimulation(gui.localTeamNo);
    }

    static void warmupCheckpoint(GameGUI &gui)
    {
        Game &game = gui.game;
        const int naturalCount = population(game);
        if (const char *minimum = std::getenv("GLOB2_BENCH_MIN_UNITS"))
        {
            int count = naturalCount;
            const int target = std::atoi(minimum);
            for (int cell = 0; cell < game.map.getW() * game.map.getH() && count < target; ++cell)
                if (game.addUnit(cell % game.map.getW(), cell / game.map.getW(),
                    count % game.mapHeader.getNumberOfTeams(), count % 3, 0, 0, 0, 0)) ++count;
            assert(count >= target);
        }
        const char *requestedTicks = std::getenv("GLOB2_BENCH_AI_TICKS");
        const int ticks = requestedTicks ? std::atoi(requestedTicks) : 0;
        for (int tick = 0; tick < ticks; ++tick) advanceAiTick(gui);
        std::printf("AI checkpoint tick=%u units=%d natural_units=%d advanced_ticks=%d\n",
            game.stepCounter, population(game), naturalCount, ticks);
    }

    static int detailForZoom(const Game &game, double zoom)
    {
        return std::getenv("GLOB2_BENCH_NATIVE_CLOUD_DETAIL") ? 0 :
            DynamicClouds::gridLimitForZoom(game.map.getW(), game.map.getH(),
                globalContainer->settings.cloudPatchSize, zoom);
    }

    struct ContextAccess : GAGCore::GraphicContext
    {
        // Obtain a base member pointer through the derived protected-access
        // rule; the actual GraphicContext object remains its original type.
        static GAGCore::RenderBackend *backend(GAGCore::GraphicContext &context) {
            auto member=&ContextAccess::renderer;return context.*member;
        }
    };
    static bool finishRendering()
    {
        auto *gfx=globalContainer->gfx;
        if(ContextAccess::backend(*gfx))ContextAccess::backend(*gfx)->flush();
        if(auto *device=gfx->renderDevice()) {
            if(!GAGCore::didRecordDeviceFrame(*ContextAccess::backend(*gfx)))return false;
            GAGCore::CaptureRequest request;request.width=request.height=1;request.pixelRegion=SDL_Rect{0,0,1,1};
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> completion(device->captureSynchronously(request),SDL_DestroySurface);
            if(!completion)throw std::runtime_error("Benchmark completion readback failed");
            return true;
        }
        // Portable SDL batches its own commands: submit them first.
        if(auto *sdlRenderer=SDL_GetRenderer(SDL_GetWindowFromID(gfx->windowID())))SDL_FlushRenderer(sdlRenderer);
#ifdef HAVE_OPENGL
        // Legacy GL, and portable SDL over OpenGL, complete with glFinish.
        // Portable SDL over Metal or Direct3D, and the software renderer,
        // have no current GL context: their flush above submitted the frame.
        if(SDL_GL_GetCurrentContext()){glFinish();assert(glGetError()==GL_NO_ERROR);}
#endif
        return true;
    }
    static void captureFramebuffer(const char *path = nullptr)
    {
        if (!path) path = std::getenv("GLOB2_BENCH_CAPTURE");
        if (!path) return;
        auto *gfx=globalContainer->gfx;
        if(auto *device=gfx->renderDevice()) {
            ContextAccess::backend(*gfx)->flush();
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> original(device->captureSynchronously({}),SDL_DestroySurface);
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(SDL_ConvertSurface(original.get(),SDL_PIXELFORMAT_RGB24),SDL_DestroySurface);
            if(!pixels)throw std::runtime_error("Benchmark full capture failed");
            std::ofstream image(path,std::ios::binary);image<<"P6\n"<<pixels->w<<" "<<pixels->h<<"\n255\n";
            for(int y=0;y<pixels->h;++y)image.write(static_cast<const char *>(pixels->pixels)+y*pixels->pitch,pixels->w*3);
            if(!image.good())throw std::runtime_error("Benchmark capture write failed");
            return;
        }
        if(gfx->backendSwitchStatus().active==GAGCore::RendererBackend::PortableSDL) {
            // SDL's renderer can leave a render-target texture bound, so read
            // its window through the backend rather than the current GL framebuffer.
            auto *backend=ContextAccess::backend(*gfx);
            backend->flush();
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> original(backend->capture(),SDL_DestroySurface);
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(
                original?SDL_ConvertSurface(original.get(),SDL_PIXELFORMAT_RGB24):nullptr,SDL_DestroySurface);
            if(!pixels)throw std::runtime_error("Benchmark portable capture failed");
            std::ofstream image(path,std::ios::binary);image<<"P6\n"<<pixels->w<<" "<<pixels->h<<"\n255\n";
            for(int y=0;y<pixels->h;++y)image.write(static_cast<const char *>(pixels->pixels)+y*pixels->pitch,pixels->w*3);
            if(!image.good())throw std::runtime_error("Benchmark capture write failed");
            return;
        }
        if(!gfx->rendererCapabilities().accelerated && gfx->getSDLSurface()) {
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(
                SDL_ConvertSurface(gfx->getSDLSurface(),SDL_PIXELFORMAT_RGB24),SDL_DestroySurface);
            if(!pixels)throw std::runtime_error("Benchmark software capture failed");
            std::ofstream image(path,std::ios::binary);image<<"P6\n"<<pixels->w<<" "<<pixels->h<<"\n255\n";
            for(int y=0;y<pixels->h;++y)image.write(static_cast<const char *>(pixels->pixels)+y*pixels->pitch,pixels->w*3);
            if(!image.good())throw std::runtime_error("Benchmark capture write failed");
            return;
        }
#ifdef HAVE_OPENGL
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        std::vector<unsigned char> pixels(viewport[2] * viewport[3] * 3);
        GLint alignment;
        glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, viewport[2], viewport[3], GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        glPixelStorei(GL_PACK_ALIGNMENT, alignment);
        std::ofstream image(path, std::ios::binary);
        image << "P6\n" << viewport[2] << " " << viewport[3] << "\n255\n";
        for (int row = viewport[3] - 1; row >= 0; --row)
            image.write(reinterpret_cast<const char *>(pixels.data() + row * viewport[2] * 3), viewport[2] * 3);
        assert(image.good());
#else
        throw std::runtime_error("Benchmark capture requires a renderer");
#endif
    }

#ifdef HAVE_OPENGL
    static void profileArrayChurn(GAGCore::GraphicContext &gfx)
    {
        auto *batch = gfx.getRenderBatch();
        if (!batch) throw std::runtime_error("Array churn requires legacy OpenGL");
        constexpr int side = 256;
        std::array<GLuint, 16> textures{};
        std::vector<Uint8> pixels(side * side * 4, 255);
        glGenTextures(textures.size(), textures.data());
        for (int cycle = 0; cycle <= 20; ++cycle)
        {
            unsigned promoted = 0;
            for (const auto texture : textures)
            {
                batch->textureChanged(texture);
                GLint previous = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
                glBindTexture(GL_TEXTURE_2D, texture);
                if (cycle == 0)
                {
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                }
                else
                {
                    pixels.front() = Uint8(cycle);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                }
                glBindTexture(GL_TEXTURE_2D, previous);
                promoted += batch->pack(texture).texture != 0;
            }
            std::printf("GL_ARRAY_CHURN cycle=%d promoted=%u sources=%zu\n", cycle, promoted, textures.size());
            batch->stats();
        }
        for (const auto texture : textures) batch->textureChanged(texture);
        glDeleteTextures(textures.size(), textures.data());
    }
#endif

public:
static int run(int argc, char **argv)
{
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
#ifdef __APPLE__
    // Create the window hidden and never activate it; GLOB2_BENCH_VISIBLE shows it.
    if (!std::getenv("GLOB2_BENCH_VISIBLE"))
        GAGCore::setProcessEnvironment("GLOB2_TEST_HIDDEN_WINDOWS", "1", 0);
#endif
    globalContainer = new GlobalContainer;
    std::vector<std::string> launchArgs{"play"};
        launchArgs.insert(launchArgs.end(),argv+1,argv+argc);
        globalContainer->applyCommand(Cli::parse(launchArgs));
    globalContainer->load();
    // GLOB2_BENCH_HD=0|1 pins HD artwork instead of the device's Auto default,
    // so renderer comparisons get identical inputs.
    if (const char *hd = std::getenv("GLOB2_BENCH_HD"))
        globalContainer->settings.setHighResolutionArtworkChoice(std::atoi(hd)
            ? Settings::AdaptiveChoice::On : Settings::AdaptiveChoice::Off);
    // Finish deferred HD publication before comparing renderer workloads.
    GAGCore::Sprite::setHighResolution(globalContainer->settings.highResolutionArtwork);
    auto *gfx=globalContainer->gfx;
    const auto active=GAGCore::rendererBackendName(gfx->backendSwitchStatus().active);
    const auto requested=GAGCore::rendererBackendName(gfx->backendSwitchStatus().requested);
    std::printf("BACKEND requested=%.*s active=%.*s fallback_reason=%s\n",int(requested.size()),requested.data(),int(active.size()),active.data(),gfx->backendSwitchStatus().reason.c_str());
    const auto capabilities=gfx->rendererCapabilities();
    const auto deviceCapabilities=gfx->renderDevice()?gfx->renderDevice()->capabilities():GAGCore::RenderDeviceCapabilities{};
    std::printf("BENCH_SETTINGS high_resolution_artwork=%d adaptive_zoom_detail=%d smooth_fog=%d window=%dx%d\n",
        int(globalContainer->settings.highResolutionArtwork), int(globalContainer->settings.adaptiveZoomDetail),
        int(globalContainer->settings.smoothFog), gfx->getW(), gfx->getH());
    std::printf("BENCH_CAPABILITIES torus=%d skin_materials=%d async_capture=%d terrain_composition=%d gpu_timing=%d max_texture=%d\n",
        capabilities.torus,capabilities.liveSkins,capabilities.asynchronousCapture,deviceCapabilities.terrainComposition,
        deviceCapabilities.gpuTiming,deviceCapabilities.maximumTextureSize);
    // A backend without the torus view (portable SDL, software) measures the
    // flat view instead of stopping.
    const bool flatOnly = !std::getenv("GLOB2_BENCH_FLAT") && !gfx->rendererCapabilities().torus;
    if(flatOnly)
        std::printf("BENCH_FLAT_FALLBACK backend has no torus view; measuring the flat view\n");
    if(!std::getenv("GLOB2_BENCH_VISIBLE")) {
        int count=0;auto **windows=SDL_GetWindows(&count);
        if(windows){for(int i=0;i<count;++i)SDL_HideWindow(windows[i]);SDL_free(windows);}
    }
#ifdef HAVE_OPENGL
    if(SDL_GL_GetCurrentContext())SDL_GL_SetSwapInterval(0);
#endif
#ifdef HAVE_OPENGL
    if (std::getenv("GLOB2_BENCH_ARRAY_CHURN"))
    {
        profileArrayChurn(*gfx);
        delete globalContainer;
        return 0;
    }
#endif
    if(gfx->renderDevice() && std::getenv("GLOB2_BENCH_COMPARE_RENDERER"))
        throw std::runtime_error("The immediate-versus-batched comparison requires legacy GL");
    {
        GameGUI gui;
        GAGCore::PresentationWorkScope cpuWork(gui.game.map.computeExecutor());
        const char *path = std::getenv("GLOB2_BENCH_MAP");
        const char *savedGame = std::getenv("GLOB2_BENCH_GAME");
        if (savedGame)
        {
            GAGCore::BinaryInputStream stream(glob2OpenMapOrSaveInputStreamBackend(*GAGCore::Toolkit::getFileManager(), savedGame));
            assert(gui.load(&stream));
            path = savedGame;
        }
        else if (std::getenv("GLOB2_BENCH_SIZE") || (std::getenv("GLOB2_BENCH_FLAT") && !path))
        {
            const char *size = std::getenv("GLOB2_BENCH_SIZE");
            if (!size) size = "256x256";
            int w = 0, h = 0;
            assert(std::sscanf(size, "%dx%d", &w, &h) == 2);
            gui.init();
            const bool sparseTerrain = std::getenv("GLOB2_BENCH_SPARSE_TERRAIN");
            makeTorusMapFixture(gui.game, w, h, sparseTerrain ? 32 : 4);
            if (std::getenv("GLOB2_BENCH_RESOURCE_ROWS"))
            {
                gui.game.map.worldRandom.initialize(1);
                for (int row = 0; row < h; ++row)
                    for (int col = (row % 2) * 4; col < w; col += 8)
                        gui.game.map.setResourceByIndex(col, row, WOOD, 1);
            }
            path = sparseTerrain ? "synthetic sparse checkerboard" : "synthetic checkerboard";
        }
        else
        {
            auto mapHeader = Engine::loadMapHeader(path ? path : "maps/Oazis.map");
            GameHeader gameHeader;
            gameHeader.setRandomSeed(1);
            for (int i = 0; i < mapHeader.getNumberOfTeams(); ++i)
                gameHeader.getBasePlayer(i) = BasePlayer(i, "Benchmark", i, BasePlayer::P_LOCAL);
            gameHeader.setNumberOfPlayers(mapHeader.getNumberOfTeams());
            assert(gui.loadFromHeaders(mapHeader, gameHeader, true, true));
        }
        gui.game.map.configureCompute(resolveComputeThreadCount(globalContainer->computeThreads));
        // GLOB2_BENCH_TEAM=N views the game as team N's player (its fog,
        // colour and panel), so a scene can frame another team's units.
        gui.localPlayer = gui.localTeamNo = std::getenv("GLOB2_BENCH_TEAM") ? std::atoi(std::getenv("GLOB2_BENCH_TEAM")) : 0;
        gui.adjustLocalTeam();
        gui.adjustInitialViewport();
        gui.loadTerrainArtwork(); // As Engine::finalAdjustments does.
        gui.prepareDirectTerrain(); // As Engine::finalAdjustments does.
        prepareScene(gui);
        int width = globalContainer->gfx->getW() - 160, height = globalContainer->gfx->getH();
        int x = 0, y = 0;
        TorusView view;
        const int frames = std::max(1, std::getenv("GLOB2_BENCH_FRAMES") ? std::atoi(std::getenv("GLOB2_BENCH_FRAMES")) : 100);
        const char *driver="SDL GPU (external capability inventory identifies physical device)";
#ifdef HAVE_OPENGL
        if(SDL_GL_GetCurrentContext())driver=reinterpret_cast<const char *>(glGetString(GL_RENDERER));
#endif
        std::printf("GPU=%s map=%s %dx%d viewport=%dx%d frames=%d\n", driver,
            path ? path : "maps/Oazis.map", gui.game.map.getW(), gui.game.map.getH(), width, height, frames);
        std::printf("BENCH_TIMING wall=assets_draw_flush_completion_with_readback_overhead process_cpu=completion_inclusive submission_cpu=assets_draw_flush gpu_timestamp=unavailable final_presentation=outside_timing retry_discard=inside_timing retry_yield_ms=1\n");
        std::fflush(stdout);
        auto measure = [&](const char *label, auto draw)
        {
            const char *mode = std::getenv("GLOB2_BENCH_MODE");
            if (mode && std::strcmp(mode, label)) return;
            std::vector<double> times, cpuTimes, submissionCpuTimes;
            std::uint64_t measuredAttempts=0, measuredHolds=0, measuredAdmissionDefers=0;
            double measuredWallMs=0;
            std::uint64_t warmupAttempts=0,warmupHolds=0,warmupAdmissionDefers=0;
            const int warmupFrames = std::max(0, std::getenv("GLOB2_BENCH_WARMUP_FRAMES")
                ? std::atoi(std::getenv("GLOB2_BENCH_WARMUP_FRAMES")) : 2);
            auto &compositor = globalContainer->terrainCompositor(gui.game.map.frozenAssetBundle());
            TerrainVisual::Compositor::HighResolutionCoverage initialCoverage;
            // Streaming is asynchronous: unpaced frames outrun the asset workers,
            // so memory measurements pace frames like a display would.
            const int frameIntervalMs = std::max(0, std::getenv("GLOB2_BENCH_FRAME_INTERVAL_MS")
                ? std::atoi(std::getenv("GLOB2_BENCH_FRAME_INTERVAL_MS")) : 0);
            // GLOB2_BENCH_ANIMATION_FRAME_US: map animation advances by exactly this
            // many microseconds each frame instead of the wall clock, as a display
            // at that rate would, so frame sequences are reproducible.
            if (const char *animationFrame = std::getenv("GLOB2_BENCH_ANIMATION_FRAME_US"))
                AnimationClock::simulatedFrameNs = std::uint64_t(std::max(0.0, std::atof(animationFrame) * 1000.0));
            benchmarkStart = SDL_GetPerformanceCounter();
            // A low-memory trim at one measured frame (GLOB2_BENCH_TRIM_AT=frame).
            // Unset means never: warmup frames are negative, so -1 would trim
            // (and reset the device) on the last warmup frame of every run.
            constexpr int Never = std::numeric_limits<int>::min();
            const int trimAt = std::getenv("GLOB2_BENCH_TRIM_AT") ? std::atoi(std::getenv("GLOB2_BENCH_TRIM_AT")) : Never;
            // A device reset at one measured frame (GLOB2_BENCH_DEVICE_RESET_AT=frame):
            // the device drops every texture, streamed direct terrain included.
            const int resetAt = std::getenv("GLOB2_BENCH_DEVICE_RESET_AT") ? std::atoi(std::getenv("GLOB2_BENCH_DEVICE_RESET_AT")) : Never;
            for (int i = -warmupFrames; i < frames; ++i)
            {
                if (i == 0) initialCoverage = compositor.highResolutionCoverage();
                const auto snapshotBytes = [&] { return gfx->renderResources() ? gfx->renderResources()->retainedBytes() : 0; };
                const auto deviceBytes = [&] { return gfx->renderDevice() ? gfx->renderDevice()->telemetry().resourceBytes : 0; };
                if (i == trimAt) {
                    std::printf("PRESENTATION_TRIM frame=%d reason=low-memory snapshot_bytes=%zu device_bytes=%zu\n", i,
                        snapshotBytes(), deviceBytes());
                    gfx->trimPresentationMemory(GAGCore::PresentationTrim::LowMemory);
                }
                if (i == resetAt && gfx->renderDevice()) {
                    std::printf("DEVICE_RESET frame=%d\n", i);
                    gfx->renderDevice()->reset();
                }
                if (trimAt >= 0 && i > trimAt && i <= trimAt + 60 && (i - trimAt) % 10 == 0)
                    std::printf("PRESENTATION_TRIM_AFTER frame=%d frames=%d pending=%d snapshot_bytes=%zu device_bytes=%zu\n", i,
                        i - trimAt, int(gfx->presentationTrimPending()), snapshotBytes(), deviceBytes());
                if(!finishRendering())ContextAccess::backend(*gfx)->present();
                // Frames present through the backend, not nextFrame: start the
                // per-frame device upload allowances as a game frame does, or
                // after the first few uploads nothing new (overview pages) shows.
                gfx->resetFrameAllowances();
                const auto telemetryBefore=gfx->renderDevice()?gfx->renderDevice()->telemetry():GAGCore::RenderTelemetry{};
                const auto scopesBefore = PerformanceTelemetry::collector().window;
                Uint64 start = SDL_GetPerformanceCounter();
                const auto cpuStart = std::clock();
                // As in the application loop, let installed sprites requested
                // by earlier draws become available before recording this one.
                GAGCore::Toolkit::pollAssets();
                std::clock_t submissionCPU{};unsigned deferredAttempts=0,intentionalHolds=0;
                for(;;) {
                    // Admission retries must redraw the same camera state.
                    // Advance only after a completed benchmark frame.
                    draw(i + warmupFrames);
                    if(ContextAccess::backend(*gfx))ContextAccess::backend(*gfx)->flush();
                    submissionCPU=std::clock();
                    if(finishRendering())break;
                    // Admission retry cost remains inside this frame's wall/CPU
                    // interval. No incomplete frame contributes a timing sample.
                    ContextAccess::backend(*gfx)->present();
                    if(GAGCore::didIntentionallyDeferDeviceFrame(*ContextAccess::backend(*gfx)))++intentionalHolds;
                    if(++deferredAttempts>=1000)throw std::runtime_error("Benchmark resource admission did not progress");
                    SDL_Delay(1);
                }
                const auto completedCPU=std::clock();
                double ms = 1000.0 * (SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency();
                const double cpuMs = 1000.0 * (completedCPU - cpuStart) / CLOCKS_PER_SEC;
                if(deferredAttempts)std::printf("FRAME_DEFERRED mode=%s frame=%d attempts=%u intentional_holds=%u admission_deferrals=%u included_in_timing=1\n",label,i,deferredAttempts,intentionalHolds,deferredAttempts-intentionalHolds);
                if (std::getenv("GLOB2_BENCH_FRAME_TIMES"))
                {
                    if(auto *device=gfx->renderDevice()) {
                        const auto after=device->telemetry();
                        std::printf("FRAME_DEVICE frame=%d packets=%llu batches=%llu upload_bytes=%llu resource_bytes=%zu submit_cpu_ns=%llu capture_cpu_ns=%llu capture_drops=%llu\n",i,
                            static_cast<unsigned long long>(after.packets-telemetryBefore.packets),static_cast<unsigned long long>(after.batches-telemetryBefore.batches),
                            static_cast<unsigned long long>(after.uploadBytes-telemetryBefore.uploadBytes),after.resourceBytes,
                            static_cast<unsigned long long>(after.cpuSubmitNanoseconds-telemetryBefore.cpuSubmitNanoseconds),
                            static_cast<unsigned long long>(after.cpuCaptureNanoseconds-telemetryBefore.cpuCaptureNanoseconds),static_cast<unsigned long long>(after.captureDrops-telemetryBefore.captureDrops));
                    }
#ifdef _WIN32
                    // MSVC clock() measures elapsed time rather than process CPU.
                    std::printf("FRAME mode=%s frame=%d elapsed_ms=%.6f\n", label, i, ms);
#else
                    std::printf("FRAME mode=%s frame=%d elapsed_ms=%.6f process_cpu_ms=%.6f\n", label, i, ms, cpuMs);
#endif
                    const auto &scopes = PerformanceTelemetry::collector().window;
                    for (auto id : {PerformanceTelemetry::Id::TerrainCache, PerformanceTelemetry::Id::Terrain,
                        PerformanceTelemetry::Id::Resources, PerformanceTelemetry::Id::GroundUnits})
                        std::printf("FRAME_SCOPE frame=%d scope=%u elapsed_ms=%.6f\n", i, unsigned(id),
                            (scopes[unsigned(id)].time.total - scopesBefore[unsigned(id)].time.total) / 1e6);
                    // Texture uploads since the previous frame's line (the portable
                    // renderer counts them; other backends report zero).
                    if (auto *backend = ContextAccess::backend(*gfx))
                    {
                        static GAGCore::RenderOperations previous{};
                        const auto ops = backend->operations();
                        std::printf("FRAME_UPLOADS frame=%d uploads=%llu upload_bytes=%llu\n", i,
                            static_cast<unsigned long long>(ops.textureUploads - previous.textureUploads),
                            static_cast<unsigned long long>(ops.textureUploadBytes - previous.textureUploadBytes));
                        previous = ops;
                    }
                    if (auto *direct = gui.view.render.existingDirectTerrain())
                        std::printf("FRAME_HD frame=%d pending=%zu texture_bytes=%zu\n", i, direct->pendingHdMasks(),
                            direct->textureBytes());
                    if (auto *cache = gui.view.render.existingTerrainCache())
                        std::printf("FRAME_CACHE frame=%d bytes=%zu texture_bytes=%zu mask_bytes=%zu rebuilds=%llu hits=%llu reductions=%llu stand_ins=%llu uncovered=%llu resolution=%d downsample=%d\n", i,
                            cache->bytes(), cache->residentTextureBytes(), cache->maskBytes(), static_cast<unsigned long long>(cache->cacheRebuilds()),
                            static_cast<unsigned long long>(cache->cacheHits()),
                            static_cast<unsigned long long>(cache->cacheReductions()),
                            static_cast<unsigned long long>(cache->cacheStandIns()),
                            static_cast<unsigned long long>(cache->cacheUncovered()),
                            cache->samplingResolution(), cache->samplingReduction());
                    const auto *pages = gui.view.render.existingTerrainCache();
                    const auto *atlas = gui.view.render.existingDirectTerrain();
                    std::printf("FRAME_TERRAIN_RESIDENCY frame=%d direct_bytes=%zu page_bytes=%zu page_retained_bytes=%zu peak_texture_bytes=%zu page_pixel_bytes=%zu\n",
                        i, atlas ? atlas->textureBytes() : 0, pages ? pages->residentTextureBytes() : 0,
                        pages ? pages->bytes() : 0, gui.view.render.peakTerrainTextureBytes,
                        pages ? pages->pixelBytes() : 0);
                    std::printf("FRAME_RENDER_MEMORY frame=%d %s\n", i, RenderMemoryReport::fields(gui.view.render).c_str());
                    {
                        const auto hd = GAGCore::Sprite::highResolutionDrawCounts();
                        std::printf("FRAME_HD_SPRITES frame=%d drawn=%zu not_loaded=%zu first_draw_cap=%zu room=%zu\n",
                            i, hd.drawn, hd.notLoaded, hd.firstDrawCap, hd.room);
                    }
                    const auto &rows = gui.view.render.resourceRowMetrics;
                    std::printf("FRAME_RESOURCE_ROWS frame=%d eligibility_cells=%llu frame_cells=%llu animated_cells=%llu\n", i,
                        static_cast<unsigned long long>(rows.eligibilityCells), static_cast<unsigned long long>(rows.frameCells),
                        static_cast<unsigned long long>(rows.animatedCells));
                    if (const auto *direct = gui.view.render.existingDirectTerrain())
                    {
                        std::printf("FRAME_DIRECT_TERRAIN frame=%d draws=%u texture_bytes=%zu uploaded_bytes=%zu split_cells=%zu base_pending=%zu restarts=%u\n",
                            i, direct->draws(), direct->textureBytes(), direct->uploadBytes(), direct->splitCells(),
                            direct->pendingBaseMasks(), direct->restartCount());
                        const auto art = direct->artMetrics();
                        std::printf("FRAME_DIRECT_ART frame=%d visible=%zu absent=%zu stale=%zu low_art_bytes=%zu\n",
                            i, art.visible, art.absent, art.stale, art.lowArtBytes);
                        const auto fog = direct->discoveryMetrics();
                        std::printf("FRAME_DIRECT_DISCOVERY frame=%d rebuilt_cells=%zu\n",
                            i, fog.rebuilt);
                    }
                    if (gui.view.render.overviewCache)
                        std::printf("FRAME_OVERVIEW frame=%d composed_cells=%llu uploaded_ahead=%llu\n", i,
                            static_cast<unsigned long long>(gui.view.render.overviewCache->composedCells()),
                            static_cast<unsigned long long>(gui.view.render.overviewCache->pagesUploadedAhead()));
                    {
                        const auto &fallback = gui.view.render.fallback;
                        std::printf("FRAME_FALLBACK frame=%d time_ms=%.3f event=%s page_cells=%llu stand_in_cells=%llu "
                            "backdrop_cells=%llu fading_cells=%llu overview_cells=%llu overview_other_cells=%llu "
                            "overview_base_cells=%llu overview_hole_cells=%llu overview_fading_cells=%llu\n",
                            i, 1000.0 * (SDL_GetPerformanceCounter() - benchmarkStart) / SDL_GetPerformanceFrequency(),
                            cameraEvent ? cameraEvent : "-",
                            static_cast<unsigned long long>(fallback.pageCells),
                            static_cast<unsigned long long>(fallback.standInCells),
                            static_cast<unsigned long long>(fallback.backdropCells),
                            static_cast<unsigned long long>(fallback.fadingCells),
                            static_cast<unsigned long long>(fallback.overviewCells),
                            static_cast<unsigned long long>(fallback.overviewOtherCells),
                            static_cast<unsigned long long>(fallback.overviewBaseCells),
                            static_cast<unsigned long long>(fallback.overviewHoleCells),
                            static_cast<unsigned long long>(fallback.overviewFadingCells));
                        cameraEvent = nullptr;
                    }
                }
                // GLOB2_BENCH_CAPTURE_FRAMES lists frames (comma separated) to save
                // as <GLOB2_BENCH_CAPTURE_PREFIX>-<frame>.ppm, for frame sequences,
                // with or without GLOB2_BENCH_FRAME_TIMES.
                if (const char *list = std::getenv("GLOB2_BENCH_CAPTURE_FRAMES"))
                {
                    const std::string wanted = std::string(",") + list + ",";
                    if (wanted.find("," + std::to_string(i) + ",") != std::string::npos)
                    {
                        const char *prefix = std::getenv("GLOB2_BENCH_CAPTURE_PREFIX");
                        const std::string path = std::string(prefix ? prefix : "frame") + "-" + std::to_string(i) + ".ppm";
                        captureFramebuffer(path.c_str());
                    }
                }
                if (i < 0 && i >= -warmupFrames && i < -warmupFrames + 8)
                    std::printf("WARMUP frame=%d elapsed_ms=%.3f process_cpu_ms=%.3f\n", i + warmupFrames, ms,
                        1000.0 * (completedCPU - cpuStart) / CLOCKS_PER_SEC);
                if (i >= 0)
                {
                    times.push_back(ms);
                    measuredAttempts+=deferredAttempts+1;
                    measuredHolds+=intentionalHolds;
                    measuredAdmissionDefers+=deferredAttempts-intentionalHolds;
                    measuredWallMs+=ms;
                    if (cpuStart != std::clock_t(-1))
                        cpuTimes.push_back(1000.0 * (completedCPU - cpuStart) / CLOCKS_PER_SEC);
                    if(cpuStart!=std::clock_t(-1))submissionCpuTimes.push_back(1000.0*(submissionCPU-cpuStart)/CLOCKS_PER_SEC);
                }
                if(i<0) {
                    warmupAttempts+=deferredAttempts+1;
                    warmupHolds+=intentionalHolds;
                    warmupAdmissionDefers+=deferredAttempts-intentionalHolds;
                }
                // Close each device frame after measurement. Otherwise recorder
                // identity/backup work would be shared by the whole benchmark.
                if(gfx->renderDevice())ContextAccess::backend(*gfx)->present();
                if (frameIntervalMs) SDL_Delay(frameIntervalMs);
            }
            std::printf("COMPLETION_SUMMARY mode=%s completed=%zu attempts=%llu intentional_holds=%llu admission_deferrals=%llu completed_per_second=%.6f\n",label,times.size(),
                static_cast<unsigned long long>(measuredAttempts),static_cast<unsigned long long>(measuredHolds),
                static_cast<unsigned long long>(measuredAdmissionDefers),1000.0*times.size()/measuredWallMs);
            std::printf("WARMUP_COMPLETION_SUMMARY mode=%s completed=%d attempts=%llu intentional_holds=%llu admission_deferrals=%llu\n",label,warmupFrames,
                static_cast<unsigned long long>(warmupAttempts),static_cast<unsigned long long>(warmupHolds),static_cast<unsigned long long>(warmupAdmissionDefers));
            // Streaming fallback: a prepared view drew native art for a material
            // whose HD frames had already appeared. Warmup is reported separately.
            const auto coverage = compositor.highResolutionCoverage();
            std::printf("terrain_hd mode=%s prepares=%llu fallback_prepares=%llu warmup_prepares=%llu warmup_fallback_prepares=%llu\n", label,
                static_cast<unsigned long long>(coverage.prepares - initialCoverage.prepares),
                static_cast<unsigned long long>(coverage.fallbacks - initialCoverage.fallbacks),
                static_cast<unsigned long long>(initialCoverage.prepares),
                static_cast<unsigned long long>(initialCoverage.fallbacks));
            std::printf("RENDER_MEMORY mode=%s %s\n", label, RenderMemoryReport::fields(gui.view.render).c_str());
            std::fputs(RenderMemoryReport::spriteLines("RENDER_MEMORY_SPRITE", 48).c_str(), stdout);
            std::sort(times.begin(), times.end());
            std::printf("%s median=%.3f ms p95=%.3f ms\n", label, times[times.size()/2], times[(times.size()*95+99)/100-1]);
            std::printf("%s p99=%.3f ms max=%.3f ms\n", label,
                times[(times.size()*99+99)/100-1], times.back());
            if(!submissionCpuTimes.empty()) {
                std::sort(submissionCpuTimes.begin(),submissionCpuTimes.end());
                std::printf("%s submission_cpu_median=%.3f ms submission_cpu_p95=%.3f ms\n",label,
                    submissionCpuTimes[submissionCpuTimes.size()/2],submissionCpuTimes[(submissionCpuTimes.size()*95+99)/100-1]);
            }
#ifndef _WIN32
            if (!cpuTimes.empty())
            {
                std::sort(cpuTimes.begin(), cpuTimes.end());
                std::printf("%s process_cpu_median=%.3f ms process_cpu_p95=%.3f ms\n",
                    label, cpuTimes[cpuTimes.size()/2], cpuTimes[(cpuTimes.size()*95+99)/100-1]);
            }
#endif
            std::fflush(stdout);
            if (std::strncmp(label, "Torus", 5) == 0) captureFramebuffer();
        };
        if (std::getenv("GLOB2_BENCH_FLAT") || flatOnly)
        {
            if (const char *density = std::getenv("GLOB2_BENCH_TERRAIN_PIXELS_PER_TILE"))
            {
                const std::string_view pixels(density);
                if ((pixels != "16" && pixels != "32") || !capabilities.accelerated)
                    throw std::invalid_argument("Fixed terrain density requires a GPU backend and 16 or 32 pixels per tile");
                gui.view.render.terrainCache(gui.game.map.identity()).benchmarkDownsample = pixels == "16" ? 2 : 1;
                std::printf("BENCH_TERRAIN_PIXELS_PER_TILE %s\n", density);
            }
            MapCamera camera;
            camera.resize(width, height, gui.game.map.getW()*32, gui.game.map.getH()*32);
            camera.zoom = camera.minimumZoom();
            // As the game's own view does, so the detail matches what a player sees.
            gui.view.render.minimumZoom = camera.minimumZoom();
            if (std::getenv("GLOB2_BENCH_FULL_MAP"))
                camera.zoom = std::min(double(width)/(gui.game.map.getW()*32), double(height)/(gui.game.map.getH()*32));
            // GLOB2_BENCH_ZOOM measures one zoom level instead of the minimum;
            // GLOB2_BENCH_ADAPTIVE_ZOOM=0 draws it with uniform scaling.
            if (const char *zoom = std::getenv("GLOB2_BENCH_ZOOM"))
                camera.zoom = std::clamp(std::atof(zoom), camera.minimumZoom(), MapCamera::MAX_ZOOM);
            if (const char *adaptive = std::getenv("GLOB2_BENCH_ADAPTIVE_ZOOM"))
                globalContainer->settings.adaptiveZoomDetail = std::atoi(adaptive) != 0;
            const int worldW = int(std::ceil(camera.visibleW()));
            const int worldH = int(std::ceil(camera.visibleH()));
            if (savedGame) warmupCheckpoint(gui);
            else populateSynthetic(gui.game, worldW, worldH);
            const bool discoveryCrossing = std::getenv("GLOB2_BENCH_DISCOVERY_CROSSING");
            if (discoveryCrossing)
            {
                if (!std::getenv("GLOB2_BENCH_FOG"))
                    throw std::invalid_argument("Discovery fixture requires GLOB2_BENCH_FOG");
                gui.game.map.unsetMapDiscovered();
                for (int y = 0; y < 15; ++y)
                    for (int x = 0; x < 15; ++x)
                        gui.game.map.setMapDiscovered(x, y, 1);
            }
            // Interactive drawing consumes a published scene. Keep extraction
            // outside the renderer timing interval rather than recapturing the
            // complete simulation through the fixture adapter on every draw.
            prepareScene(gui);
            const int count = population(gui.game);
            const int cloudGridLimit = detailForZoom(gui.game, camera.zoom);
            // GLOB2_BENCH_FOG=1 draws the first team's fog of war instead of the whole map;
            // GLOB2_BENCH_SMOOTH_FOG=0 draws it without the smooth fog fade.
            if (const char *smoothFog = std::getenv("GLOB2_BENCH_SMOOTH_FOG"))
                globalContainer->settings.smoothFog = std::atoi(smoothFog) != 0;
            const Uint32 options = (std::getenv("GLOB2_BENCH_FOG") ? 0 : Game::DRAW_WHOLE_MAP) | (std::getenv("GLOB2_BENCH_BARS") ? Game::DRAW_HEALTH_FOOD_BAR : 0)
                | (std::getenv("GLOB2_BENCH_AREAS") ? Game::DRAW_AREA : 0);
            // GLOB2_BENCH_GUI=1 draws the whole in-game screen (GameGUI::drawAll:
            // map, HUD panel, minimap, top bar, health bars and selection) at a
            // fixed camera instead of the bare map, with presentation-only
            // motion pinned so every renderer draws the same image.
            const bool guiScreen = std::getenv("GLOB2_BENCH_GUI");
            if (guiScreen)
                prepareGuiScreen(gui, camera.zoom);
            std::printf("mapped_units=%d (excludes units inside buildings)\n", mappedPopulation(gui.game));
            std::printf("flat zoom=%.6f world=%dx%d total_units=%d shader=%d cloud_grid_limit=%d\n", camera.zoom, worldW, worldH, count, globalContainer->gfx->hasUnitShader(), cloudGridLimit);
            for (bool clouds : {false, true})
            {
                globalContainer->settings.clouds = clouds;
                globalContainer->settings.cloudShadows = clouds;
                const Uint32 initialChecksum = gui.game.checkSum(nullptr, nullptr, nullptr, true);
                const char *mode = std::getenv("GLOB2_BENCH_MODE");
                if (mode && std::strcmp(mode, clouds ? "2D clouds" : "2D no clouds")) continue;
                PerformanceTelemetry::collector().reset();
                if (!clouds && std::getenv("GLOB2_BENCH_COMPARE_RENDERER"))
                    globalContainer->gfx->setRenderBatchEnabled(false);
                bool crossedDiscovery = false;
                bool switchedBackend = false;
                measure(clouds ? "2D clouds" : "2D no clouds", [&](int cameraFrame) {
                    // GLOB2_BENCH_SETTLE_FRAMES first holds one view, as a game does
                    // before the player moves: background work such as the whole-map
                    // base mask finishes before the camera motion. It is the motion's
                    // sixtieth frame, the closest zoom of the cycle, because the
                    // farthest draws the overview and prepares no terrain.
                    static const int settleFrames = std::max(0, std::getenv("GLOB2_BENCH_SETTLE_FRAMES")
                        ? std::atoi(std::getenv("GLOB2_BENCH_SETTLE_FRAMES")) : 0);
                    cameraFrame = cameraFrame < settleFrames ? 60 : cameraFrame - settleFrames;
                    if (discoveryCrossing && !crossedDiscovery && cameraFrame >= 4)
                    {
                        gui.game.map.setMapDiscovered(15, 15, 1);
                        gui.game.map.setMapDiscovered(16, 16, 1);
                        prepareScene(gui);
                        crossedDiscovery = true;
                    }
                    Uint32 frameOptions = options;
                    // GLOB2_BENCH_COMPLETE_TERRAIN: every frame finishes its terrain
                    // first (as captures do), the reference a moving view is
                    // compared with (tools/rendering/motion_parity.py).
                    static const bool completeTerrain = std::getenv("GLOB2_BENCH_COMPLETE_TERRAIN");
                    if (completeTerrain) frameOptions |= Game::DRAW_COMPLETE_TERRAIN;
                    if (const auto *terrainSwitch = std::getenv("GLOB2_BENCH_TERRAIN_SWITCH"))
                    {
                        const bool switchToHD = std::string_view(terrainSwitch) == "hd";
                        const bool switchToTiled = std::string_view(terrainSwitch) == "tiled";
                        if (std::string_view(terrainSwitch) == "backend" && cameraFrame >= 68 && !switchedBackend)
                        {
                            using GAGCore::RendererBackend;
                            if (!gfx->requestBackendSwitch(RendererBackend::LegacyGL))
                                throw std::runtime_error("Could not request legacy OpenGL for terrain switch");
                            gfx->nextFrame();
                            if (gfx->backendSwitchStatus().active != RendererBackend::LegacyGL)
                                throw std::runtime_error("Terrain switch did not activate legacy OpenGL");
                            std::printf("BENCH_TERRAIN_SWITCH frame=%d active=legacy-gl\n", cameraFrame);
                            switchedBackend = true;
                        }
                        if (switchToTiled && cameraFrame >= 68)
                            frameOptions |= Game::DRAW_TILED_CAPTURE;
                        if (switchToHD && cameraFrame >= 68 && !globalContainer->settings.highResolutionArtwork)
                        {
                            globalContainer->settings.highResolutionArtwork = true;
                            GAGCore::Sprite::setHighResolution(true);
                        }
                    }
                    gfx->resetDrawCallCount();
                    if (guiScreen)
                    {
                        drawGuiScreen(gui);
                        return;
                    }
                    gfx->setClipRect();
                    gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), GAGCore::Color(0, 0, 0));
                    const bool sweep = std::getenv("GLOB2_BENCH_CAMERA_SWEEP");
                    const bool motion = std::getenv("GLOB2_BENCH_CAMERA_MOTION");
                    const bool pan = motion || std::getenv("GLOB2_BENCH_CAMERA_PAN");
                    const bool zoomCycle = motion || std::getenv("GLOB2_BENCH_CAMERA_ZOOM");
                    // A repeatable wheel-like zoom cycle with sub-tile scrolling.
                    // Unlike the seam stress sweep this does not teleport the view.
                    const double zoom = zoomCycle ? std::min(MapCamera::MAX_ZOOM,
                        camera.zoom * std::pow(4.0, (1 - std::cos(cameraFrame * 6.283185307179586 / 120)) / 2))
                        : sweep ? camera.zoom * (1 + cameraFrame%8) : camera.zoom;
                    // GLOB2_BENCH_PAN_X/Y place a fixed camera's top-left tile, so a
                    // zoomed-in measurement can look at a colony instead of open water.
                    const int fixedPanX = std::getenv("GLOB2_BENCH_PAN_X") ? std::atoi(std::getenv("GLOB2_BENCH_PAN_X")) : 0;
                    const int fixedPanY = std::getenv("GLOB2_BENCH_PAN_Y") ? std::atoi(std::getenv("GLOB2_BENCH_PAN_Y")) : 0;
                    const int panX = pan ? (fixedPanX + cameraFrame / 8)%gui.game.map.getW()
                        : sweep ? (cameraFrame*37)%gui.game.map.getW() : fixedPanX;
                    const int panY = pan ? (fixedPanY + cameraFrame / 16)%gui.game.map.getH()
                        : sweep ? (cameraFrame*19)%gui.game.map.getH() : fixedPanY;
                    if (const char *script = std::getenv("GLOB2_BENCH_CAMERA_SCRIPT"))
                    {
                        if (std::string_view(script) != "fallback")
                            throw std::invalid_argument("GLOB2_BENCH_CAMERA_SCRIPT supports only fallback");
                        static const double scriptZoom = std::getenv("GLOB2_BENCH_ZOOM") ? camera.zoom : 1.0;
                        auto scripted = scriptedCamera(cameraFrame, scriptZoom, gui.game.map.getW(), gui.game.map.getH());
                        scripted.zoom = std::max(scripted.zoom, camera.minimumZoom());
                        cameraEvent = scripted.event;
                        // The centre stays put across zoom steps.
                        const double centreX = scripted.x + width / scriptZoom / 2, centreY = scripted.y + height / scriptZoom / 2;
                        const double left = centreX - width / scripted.zoom / 2, top = centreY - height / scripted.zoom / 2;
                        const double mapPixelsW = gui.game.map.getW() * 32.0, mapPixelsH = gui.game.map.getH() * 32.0;
                        const double wrappedX = left - std::floor(left / mapPixelsW) * mapPixelsW;
                        const double wrappedY = top - std::floor(top / mapPixelsH) * mapPixelsH;
                        const int tileX = int(wrappedX / 32), tileY = int(wrappedY / 32);
                        const float fractionX = float(wrappedX - tileX * 32.0), fractionY = float(wrappedY - tileY * 32.0);
                        const int scriptW = int(std::ceil(width / scripted.zoom)) + 32, scriptH = int(std::ceil(height / scripted.zoom)) + 32;
                        globalContainer->gfx->beginMapTransform(scripted.zoom, -fractionX * scripted.zoom,
                            -fractionY * scripted.zoom, 0, 0, width, height);
                        MapDrawer::drawMap(0, 0, scriptW, scriptH, 0, 0, tileX, tileY, 0, gui.view, frameOptions,
                            nullptr, nullptr, false, detailForZoom(gui.game, scripted.zoom));
                        globalContainer->gfx->endMapTransform();
                        return;
                    }
                    int drawW = int(std::ceil(width/zoom)), drawH = int(std::ceil(height/zoom));
                    if (!sweep && std::getenv("GLOB2_BENCH_FULL_MAP"))
                    {
                        drawW = std::min(drawW, gui.game.map.getW()*32);
                        drawH = std::min(drawH, gui.game.map.getH()*32);
                    }
                    // GLOB2_BENCH_FRACTION shifts the map by that many map pixels, as a
                    // camera between tiles does; seams between tiles only show then.
                    const float fraction = std::getenv("GLOB2_BENCH_FRACTION") ? float(std::atof(std::getenv("GLOB2_BENCH_FRACTION"))) : 0.f;
                    globalContainer->gfx->beginMapTransform(zoom,
                        -(pan ? cameraFrame % 8 * 4.f : fraction)*zoom,
                        -(pan ? cameraFrame % 16 * 2.f : fraction)*zoom, 0, 0, width, height);
                    const bool pausePresentation = std::getenv("GLOB2_BENCH_PAUSE_PRESENTATION");
                    if (pausePresentation) gui.view.render.pinAnimation(22);
                    MapDrawer::drawMap(0, 0, drawW, drawH, 0, 0,
                        panX, panY, 0, gui.view, frameOptions, nullptr, nullptr, pausePresentation,
                        detailForZoom(gui.game, zoom));
                    globalContainer->gfx->endMapTransform();
                });
                std::printf("draw_calls=%lu texture_bytes=%zu\n", globalContainer->gfx->getDrawCallCount(), GAGCore::DrawableSurface::allocatedTextureBytes());
                if (auto *batch = globalContainer->gfx->getRenderBatch())
                {
                    const auto geometry = batch->geometryCache().stats();
                    const auto textures = batch->stats();
                    std::printf("STEADY_CACHE pending=%zu attempts=%llu promotions=%llu array_bytes=%zu geometry_bytes=%zu builds=%llu deferred=%llu stale_artwork=%llu\n",
                        textures.pendingTextures, static_cast<unsigned long long>(textures.warmAttempts), static_cast<unsigned long long>(textures.warmPromotions),
                        textures.textureBytes, geometry.bytes, geometry.builds, geometry.deferred, geometry.staleArtwork);
                }
                PerformanceTelemetry::collector().write(std::cout, "BENCH_SCOPE", 0, false);
                assert(discoveryCrossing || gui.game.checkSum(nullptr, nullptr, nullptr, true) == initialChecksum);
                if (discoveryCrossing)
                    std::printf("discovery_checksum=%08x\n", gui.game.checkSum(nullptr, nullptr, nullptr, true));
                std::printf("simulation_checksum=%08x\n", initialChecksum);
                // Optional same-process comparisons hold camera and simulation
                // fixed within each pair. Readback/checksum work is deliberately
                // outside the timing interval. Cloud animation has separate
                // mutable presentation state, so compare only the no-cloud pass.
#ifdef HAVE_OPENGL
                if (!clouds && std::getenv("GLOB2_BENCH_COMPARE_RENDERER"))
                {
                    const bool advance = std::getenv("GLOB2_BENCH_COMPARE_AI");
                    std::vector<double> pairedCpu[2];
                    for (int pair = -8; pair < frames; ++pair)
                    {
                        if (advance) advanceAiTick(gui);
                        if (advance) prepareScene(gui);
                        const auto checksum = gui.game.checkSum(nullptr, nullptr, nullptr, true);
                        const int cameraIndex = pair + 8;
                        const bool sweep = std::getenv("GLOB2_BENCH_CAMERA_SWEEP");
                        const double zoom = sweep ? camera.zoom * (1 + cameraIndex % 8) : camera.zoom;
                        const int panX = sweep ? (cameraIndex * 37) % gui.game.map.getW() : 0;
                        const int panY = sweep ? (cameraIndex * 19) % gui.game.map.getH() : 0;
                        int drawW = int(std::ceil(width / zoom)), drawH = int(std::ceil(height / zoom));
                        if (!sweep && std::getenv("GLOB2_BENCH_FULL_MAP"))
                        {
                            drawW = std::min(drawW, gui.game.map.getW() * 32);
                            drawH = std::min(drawH, gui.game.map.getH() * 32);
                        }
                        std::vector<unsigned char> reference;
                        for (int variant = 0; variant < 2; ++variant)
                        {
                            auto *gfx = globalContainer->gfx;
                            gfx->setRenderBatchEnabled(variant != 0);
                            gfx->setClipRect();
                            gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), GAGCore::Color(0, 0, 0));
                            gfx->resetDrawCallCount();
                            gfx->beginMapTransform(zoom, 0, 0, 0, 0, width, height);
                            gui.view.render.pinAnimation(22);
                            glFinish();
                            const auto cpuStart = std::clock();
                            MapDrawer::drawMap(0, 0, drawW, drawH, 0, 0, panX, panY, 0,
                                gui.view, options, nullptr, nullptr, true, detailForZoom(gui.game, zoom));
                            gfx->endMapTransform();
                            glFinish();
                            const double cpu = 1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC;
                            assert(glGetError() == GL_NO_ERROR);
                            assert(gui.game.checkSum(nullptr, nullptr, nullptr, true) == checksum);
                            if (pair >= 0) pairedCpu[variant].push_back(cpu);
                            GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
                            std::vector<unsigned char> pixels(size_t(viewport[2]) * viewport[3] * 4);
                            glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3],
                                GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                            if (!variant) reference = std::move(pixels);
                            else
                            {
                                int maximumDelta = 0; size_t changed = 0;
                                for (size_t i = 0; i < pixels.size(); ++i)
                                {
                                    const int delta = std::abs(int(reference[i]) - int(pixels[i]));
                                    maximumDelta = std::max(maximumDelta, delta);
                                    changed += delta != 0;
                                }
                                std::printf("COMPARE pair=%d tick=%u checksum=%08x max_delta=%d changed_channels=%zu\n",
                                    pair, gui.game.stepCounter, checksum, maximumDelta, changed);
                                std::fflush(stdout);
                                assert(maximumDelta <= 1 && changed <= 100);
                            }
                            if (pair == frames - 1)
                                if (const char *prefix = std::getenv("GLOB2_BENCH_COMPARE_CAPTURE_PREFIX"))
                                {
                                    const std::string output = std::string(prefix) +
                                        (variant ? "-optimized.ppm" : "-immediate.ppm");
                                    captureFramebuffer(output.c_str());
                                }
                            std::printf("PAIRED pair=%d impl=%s cpu_ms=%.6f draws=%lu\n",
                                pair, variant ? "optimized" : "immediate", cpu, gfx->getDrawCallCount());
                        }
                    }
                    globalContainer->gfx->setRenderBatchEnabled(true);
                    for (int variant = 0; variant < 2; ++variant)
                    {
                        auto& values = pairedCpu[variant];
                        std::sort(values.begin(), values.end());
                        std::printf("PAIRED_SUMMARY impl=%s cpu_median=%.6f frames=%zu\n",
                            variant ? "optimized" : "immediate", values[values.size() / 2], values.size());
                    }
                    if (auto *batch = globalContainer->gfx->getRenderBatch())
                    {
                        const auto stats = batch->geometryCache().stats();
                        std::printf("RENDER_CACHE array_bytes=%zu geometry_bytes=%zu entries=%zu hits=%llu misses=%llu\n",
                            batch->textureBytes(), stats.bytes, stats.entries, stats.hits, stats.misses);
                    }
                    std::printf("COMPARE_FINAL tick=%u checksum=%08x\n", gui.game.stepCounter,
                        gui.game.checkSum(nullptr, nullptr, nullptr, true));
                }
#endif
                captureFramebuffer();
                if (std::getenv("GLOB2_BENCH_VISIBLE")) globalContainer->gfx->nextFrame();
            }
            gui.view.scene = nullptr;
        }
        else
        for (bool clouds : {false, true})
        {
            const bool pausePresentation=std::getenv("GLOB2_BENCH_PAUSE_PRESENTATION");
            gui.gamePaused=pausePresentation;
            if(pausePresentation)gui.view.render.pinAnimation(22);
            const Uint32 initialChecksum = gui.game.checkSum(nullptr, nullptr, nullptr, true);
            globalContainer->settings.clouds = clouds;
            globalContainer->settings.cloudShadows = clouds;
            measure(clouds ? "2D clouds" : "2D no clouds", [&](int) {
                globalContainer->gfx->setClipRect();
                if(pausePresentation)gui.view.render.pinAnimation(22);
                MapDrawer::drawMap(0, 0, width, height, 0, 0, x, y, 0, gui.view, Game::DRAW_WHOLE_MAP,
                    nullptr,nullptr,pausePresentation);
            });
            view.reset();
            view.toggle();
            // The ring is drawn at the flat camera's zoom: fully zoomed out,
            // the whole ring, unless GLOB2_BENCH_ZOOM names another.
            float ringZoom = std::min(1.f, std::max(float(width) / (gui.game.map.getW() * 32), float(height) / (gui.game.map.getH() * 32)));
            gui.view.render.minimumZoom = ringZoom;
            if (const char *zoom = std::getenv("GLOB2_BENCH_ZOOM"))
                ringZoom = std::clamp(float(std::atof(zoom)), ringZoom, float(MapCamera::MAX_ZOOM));
            assert(view.draw(gui.view.drawnScene(),gui.game.gui, 0, Game::DRAW_WHOLE_MAP, x, y, width, height, ringZoom));
            measure(clouds ? "Torus clouds" : "Torus no clouds", [&](int) {
                if(pausePresentation)gui.view.render.pinAnimation(22);
                view.amount = 1;
                view.lastFrame = SDL_GetTicks();
                view.setViewport((x + 1) & gui.game.map.getMaskW(), (y + 1) & gui.game.map.getMaskH());
                assert(view.draw(gui.view.drawnScene(),gui.game.gui, 0, Game::DRAW_WHOLE_MAP, x, y, width, height, ringZoom));
            });
            size_t bytes = 0;
            for (const auto &tile : view.tiles) bytes += size_t(tile.textureW) * tile.textureH * 4;
            std::printf("capture=%dx%d tiles=%zu pixels/cell=%d texture_bytes=%zu cloud=%dx%d\n",
                        view.worldW * view.pixelsPerCell, view.worldH * view.pixelsPerCell,
                        view.tiles.size(), view.pixelsPerCell, bytes, view.cloudW, view.cloudH);
            view.reset();
            assert(gui.game.checkSum(nullptr, nullptr, nullptr, true) == initialChecksum);
            std::printf("simulation_checksum=%08x\n", initialChecksum);
        }
    }
    delete globalContainer;
    return 0;
}
};

int main(int argc, char **argv)
{
    return TorusRenderBenchmark::run(argc, argv);
}
