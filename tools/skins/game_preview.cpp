// SPDX-License-Identifier: GPL-3.0-or-later
// Capture a saved game's real Scene renderer, with the preview env enabled or off.
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "Team.h"
#include "Unit.h"
#include "Race.h"
#include "Building.h"
#include "render/ColonySkinPreview.h"
#include "map/io/MapHeader.h"
#include <BinaryStream.h>
#include <Toolkit.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "online/SkinDownloads.h"
#include "online/SkinSprites.h"
#include "online/OnlineStorage.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <vector>
#include <SDL3/SDL.h>
#include <PerformanceTelemetry.h>
#ifdef HAVE_OPENGL
#include <SDL3/SDL_opengl.h>
#endif

GlobalContainer *globalContainer = nullptr;

int main(int argc, char **argv)
{
    const char *save = std::getenv("SKIN_PREVIEW_SAVE");
    const char *output = std::getenv("SKIN_PREVIEW_CAPTURE");
    if (!save || !output) { std::cerr << "Set SKIN_PREVIEW_SAVE and SKIN_PREVIEW_CAPTURE\n"; return 2; }
    try
    {
        globalContainer = new GlobalContainer;
        globalContainer->parseArgs(argc,argv);
            globalContainer->settings.mute = 1;
        globalContainer->settings.autosaveGames = false;
        globalContainer->load();
        {
            std::unique_ptr<Online::OnlineStorage> skinStorage;
            GameGUI gui;
            const auto drawScene = [&] {
                // Match Application's frame pump so shared asset finalizers run.
                GAGCore::Toolkit::pollAssets();
                gui.drawAll(0);
            };
            GAGCore::BinaryInputStream stream(glob2OpenMapOrSaveInputStreamBackend(*GAGCore::Toolkit::getFileManager(), save));
            if (!gui.load(&stream,true)) throw std::runtime_error("Cannot load preview save");
            gui.localPlayer = gui.localTeamNo = 0;
            gui.adjustLocalTeam();
            gui.adjustInitialViewport();
            if (const char *adaptive = std::getenv("SKIN_PREVIEW_ADAPTIVE"))
            {
                const std::string value(adaptive);
                if (value != "0" && value != "1") throw std::runtime_error("Invalid adaptive preview setting");
                globalContainer->settings.adaptiveZoomDetail = value == "1";
            }
            if (const char *zoom = std::getenv("SKIN_PREVIEW_ZOOM"))
            {
                const double value = std::stod(zoom);
                if (!(value >= 0.02 && value <= 5.0)) throw std::runtime_error("Invalid preview zoom");
                gui.camera.zoom = value;
            }
            // A tick-zero map may have no units yet. Place a diagnostic row of
            // workers beside the colony to exercise both draw paths together.
            if (gui.game.mapHeader.getNumberOfTeams() < 2) throw std::runtime_error("Preview needs two colonies");
            const auto *team = gui.game.teams[0];
            for (int i=0; i<8; ++i)
            {
                int x = (team->startPosX - 4 + i) & (gui.game.map.getW()-1);
                int y = (team->startPosY + 4) & (gui.game.map.getH()-1);
                auto *unit = gui.game.addUnit(x,y,i<4 ? 0 : 1,WORKER,0,0,0,0);
                if(!unit && !std::getenv("SKIN_PREVIEW_BENCHMARK")) {
                    const int startX=x,startY=y;
                    for(int offset=1;offset<64 && !unit;++offset) {
                        x=(startX+offset%8)&(gui.game.map.getW()-1);y=(startY+offset/8)&(gui.game.map.getH()-1);
                        unit=gui.game.addUnit(x,y,i<4 ? 0 : 1,WORKER,0,0,0,0);
                    }
                }
                if (unit)
                { unit->direction = i; gui.game.map.setMapDiscovered(x,y,Team::teamNumberToMask(0)); }
                else if (!std::getenv("SKIN_PREVIEW_BENCHMARK")) throw std::runtime_error("Diagnostic worker placement failed");
            }
            const char *moderationCapture = std::getenv("SKIN_PREVIEW_MODERATION_CAPTURE");
            if (moderationCapture)
            {
                globalContainer->settings.clouds = false;
                globalContainer->settings.cloudShadows = false;
                const int bx = (team->startPosX + 4) & (gui.game.map.getW()-1);
                const int by = team->startPosY;
                if (!gui.game.addBuilding(bx, by, globalContainer->buildingsTypes.getFinishedTypeNum("inn"), 0))
                    throw std::runtime_error("Diagnostic inn placement failed");
                for (int y=by; y<by+4; ++y)
                    for (int x=bx; x<bx+4; ++x)
                        gui.game.map.setMapDiscovered(x,y,Team::teamNumberToMask(0));
            }
            if(const char *assignmentPath=std::getenv("SKIN_PREVIEW_ASSIGNMENT"))
            {
                const char *cache=std::getenv("SKIN_PREVIEW_CACHE");
                if(!cache)throw std::runtime_error("Set SKIN_PREVIEW_CACHE for authorized rendering");
                std::ifstream input(assignmentPath);
                const auto assignment=nlohmann::json::parse(input);
                std::vector<Online::SkinDownloads::Ticket> tickets;
                for(const auto &skin:assignment.at("colonySkins"))tickets.push_back({skin.at("team"),skin.at("assertion")});
                skinStorage=Online::makeDirectoryStorage(cache);
                gui.setColonySkins(std::make_unique<Online::SkinDownloads>(*skinStorage,assignment.at("origin"),assignment.at("matchId"),std::move(tickets)));
                for(int frame=0;frame<180;++frame){drawScene();globalContainer->gfx->nextFrame();SDL_Delay(16);}
            }
            if (const char *benchmark = std::getenv("SKIN_PREVIEW_BENCHMARK"))
            {
                const bool forceMiss = std::getenv("SKIN_BENCH_FORCE_MISS") && std::string(std::getenv("SKIN_BENCH_FORCE_MISS")) == "1";
                const bool uncapped = std::getenv("SKIN_BENCH_UNCAPPED") && std::string(std::getenv("SKIN_BENCH_UNCAPPED")) == "1";
                if (uncapped) {
                    globalContainer->gfx->setTargetRenderFps(0);
                    if (globalContainer->gfx->context && !SDL_GL_SetSwapInterval(0)) throw std::runtime_error(SDL_GetError());
                }
                const char *assets = std::getenv("GLOB2_SKIN_PREVIEW_DIR");
                if ((!assets || !*assets) && !std::getenv("SKIN_PREVIEW_ASSIGNMENT")) throw std::runtime_error("Benchmark requires preview assets or an authorized assignment");
                auto &appearance = gui.view.render.skinPreview();
                if (!appearance.ready && !appearance.sprites) throw std::runtime_error("Benchmark appearance unavailable");
                if(!std::getenv("SKIN_PREVIEW_ASSIGNMENT")) {
                // Team 1 reuses the preview atlas with the left half of every
                // model quadrant repainted, and the preview material map.
                auto paint = std::make_unique<GAGCore::DrawableSurface>(std::string(assets)+"/paint.webp");
                for (int quadrant : {0,256}) paint->drawFilledRect(quadrant,0,128,512,GAGCore::Color(100,190,80));
                auto material = GAGCore::loadSkinMaterialMap(std::string(assets)+"/material.webp");
                if (!material) material = std::make_unique<GAGCore::DrawableSurface>(512,512);
                if (!appearance.install(1, std::move(paint), std::move(material)))
                    throw std::runtime_error("Benchmark needs a 512x512 paint.webp (and material.webp when present)");
                }
                globalContainer->settings.clouds = false;
                globalContainer->settings.cloudShadows = false;
                globalContainer->settings.unitInterpolation = false;
                const unsigned skinTeams=std::getenv("SKIN_BENCH_TEAMS")?std::stoul(std::getenv("SKIN_BENCH_TEAMS")):2;
                if(!skinTeams || skinTeams>unsigned(gui.game.mapHeader.getNumberOfTeams()))throw std::runtime_error("Invalid benchmark colony count");
                std::vector<Unit *> crowd;
                for (int y=-8; y<8; ++y)
                    for (int x=-8; x<8; ++x)
                    {
                        const int mx=(team->startPosX+x)&(gui.game.map.getW()-1);
                        const int my=(team->startPosY+y)&(gui.game.map.getH()-1);
                        gui.game.map.setMapDiscovered(mx,my,Team::teamNumberToMask(0));
                        const int index=(y+8)*16+x+8;
                        for (int type : {index%3 ? WORKER : WARRIOR, EXPLORER})
                            if (auto *unit=gui.game.addUnit(mx,my,(index/3)%skinTeams,type,0,0,0,0))
                            {
                                unit->direction=index%8;
                                crowd.push_back(unit);
                            }
                    }
                if (crowd.size()<400) throw std::runtime_error("Crowded scene did not fit the fixture");
                // The crowd changes visibility without advancing the simulation.
                // Refresh the presentation fade after the authorization warmup.
                gui.view.render.fogFade.reset();
                const unsigned frames = std::getenv("SKIN_BENCH_FRAMES") ? std::stoul(std::getenv("SKIN_BENCH_FRAMES")) : 45;
                const unsigned warmup = std::getenv("SKIN_BENCH_WARMUP") ? std::stoul(std::getenv("SKIN_BENCH_WARMUP")) : 5;
                if (frames > 10000 || warmup >= frames) throw std::runtime_error("Invalid benchmark frame counts");
                std::vector<Uint32> classicChecksums;
                for (bool skinned : {false,true})
                {
                    globalContainer->settings.showColonySkins=skinned;
                    std::vector<double> times;
                    unsigned long draws=0;
                    double coldMs=0;
                    Online::SkinSprites::Counters cacheStart;
                    auto &profile = PerformanceTelemetry::collector();
                    for (unsigned frame=0; frame<frames; ++frame)
                    {
                        SDL_PumpEvents();
                        for (unsigned i=0; i<crowd.size(); ++i) crowd[i]->delta=(i*13+frame*8)%256;
                        const auto checksum=gui.game.checkSum();
                        if (!skinned) classicChecksums.push_back(checksum);
                        else if (classicChecksums.at(frame)!=checksum)
                            throw std::runtime_error("Classic and skinned frame states diverged");
                        if (frame == warmup) {
                            profile.reset();
                            if(appearance.sprites)cacheStart=appearance.sprites->counters();
                        }
                        globalContainer->gfx->resetDrawCallCount();
                        // Preserve uploaded geometry and paint while forcing pose rasterization.
                        if (skinned && forceMiss) globalContainer->gfx->skinResources.slots = {};
                        const auto start=SDL_GetPerformanceCounter();
                        drawScene();
                        const auto count=globalContainer->gfx->getDrawCallCount();
                        globalContainer->gfx->nextFrame();
                        const double elapsedMs=1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency();
                        if (frame==0) coldMs=elapsedMs;
                        if (frame>=warmup)
                        {
                            times.push_back(elapsedMs);
                            draws+=count;
                        }
                        if (gui.game.checkSum()!=checksum)
                            throw std::runtime_error("Drawing changed simulation state");
                    }
                    double sum=0;for(double elapsed:times)sum+=elapsed;
                    nlohmann::json cacheMetrics=nullptr;
                    if(appearance.sprites) {
                        const auto &counts=appearance.sprites->counters();
                        cacheMetrics={{"decodes",counts.decodes},{"evictions",counts.evictions},{"hits",counts.hits},{"misses",counts.misses},
                            {"measuredDecodes",counts.decodes-cacheStart.decodes},{"measuredEvictions",counts.evictions-cacheStart.evictions},
                            {"measuredHits",counts.hits-cacheStart.hits},{"measuredMisses",counts.misses-cacheStart.misses}};
                    }
                    const auto samples = times;
                    std::sort(times.begin(),times.end());
                    nlohmann::json scopes=nlohmann::json::object();
                    using PerformanceTelemetry::Id;
                    for (auto [id,name] : {std::pair{Id::Render,"render"}, {Id::SkinPrepare,"prepare"}, {Id::SkinGeometry,"geometry"}, {Id::SkinRaster,"raster"}, {Id::SkinComposite,"composite"}, {Id::Present,"present"}})
                    {
                        auto metric=profile.total[static_cast<unsigned>(id)];
                        metric.merge(profile.window[static_cast<unsigned>(id)]);
                        scopes[name]={{"calls",metric.calls},
                            {"totalMs",metric.time.total/1e6},{"selfMs",metric.self/1e6}};
                    }
                    if (skinned && gui.view.render.detail.unitSprite == 0 && gui.view.render.detail.buildingSprite == 0)
                        for (const char *scope : {"geometry", "raster", "composite"})
                            if (scopes[scope]["calls"].get<unsigned long>() != 0)
                                throw std::runtime_error("Overview prepared or drew hidden skin meshes");
                    nlohmann::json backend = {{"videoDriver",SDL_GetCurrentVideoDriver()}};
#ifdef HAVE_OPENGL
                    if (globalContainer->gfx->context) {
                        backend["glRenderer"] = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
                        backend["glVersion"] = reinterpret_cast<const char *>(glGetString(GL_VERSION));
                        int interval = -1; SDL_GL_GetSwapInterval(&interval); backend["swapInterval"] = interval;
                    }
#endif
                    std::cout << nlohmann::json{{"zoom",gui.camera.zoom},
                        {"backend",backend},{"forcedMiss",forceMiss},{"uncapped",uncapped},
                        {"workerRig",bool(appearance.clips[0].model||appearance.clips[0].shapes)},{"gpuRig",bool(globalContainer->gfx->skinResources.rigProgram)},
                        {"frameMs",samples},{"stateChecksums",classicChecksums},
                        {"adaptiveZoom",globalContainer->settings.adaptiveZoomDetail},
                        {"unitSprite",gui.view.render.detail.unitSprite},{"buildingSprite",gui.view.render.detail.buildingSprite},
                        {"profile",scopes},{"coldMs",coldMs},{"warmup",warmup},{"mode",skinned?"skinned":"classic"},{"addedUnits",crowd.size()},
                        {"width",globalContainer->gfx->getW()},{"height",globalContainer->gfx->getH()},
                        {"decodedSpriteBytes",appearance.sprites?appearance.sprites->decodedBytes():0},{"spriteCache",cacheMetrics},{"frames",times.size()},{"checksumFrames",classicChecksums.size()},{"meanMs",sum/times.size()},{"p95Ms",times[std::min(times.size()-1,std::size_t(times.size()*0.95))]},
                        {"drawsPerFrame",double(draws)/times.size()}}.dump() << std::endl;
                    drawScene();
                    globalContainer->gfx->printScreen(std::string(benchmark)+(skinned?"-skinned.bmp":"-classic.bmp"));
                    globalContainer->gfx->nextFrame();
                    if(skinned)if(const char *framesPrefix=std::getenv("SKIN_BENCH_FRAME_PREFIX")) {
                        for(unsigned frame=0;frame<32;++frame) {
                            for(unsigned i=0;i<crowd.size();++i)crowd[i]->delta=(i*13+frame*8)%256;
                            const auto checksum=gui.game.checkSum();drawScene();
                            if(gui.game.checkSum()!=checksum)throw std::runtime_error("Animation capture changed simulation state");
                            globalContainer->gfx->printScreen(std::string(framesPrefix)+"-"+std::to_string(frame)+".bmp");
                            globalContainer->gfx->nextFrame();
                        }
                    }
                }
            }
            if (moderationCapture)
            {
                const char *progress = std::getenv("SKIN_PREVIEW_PROGRESS");
                if (!progress) throw std::runtime_error("Set SKIN_PREVIEW_PROGRESS for moderation capture");
                const auto start = SDL_GetTicks();
                int stage = 0;
                std::optional<std::uint32_t> originalColor;
                while (stage < 3 && SDL_GetTicks()-start < 180000)
                {
                    drawScene();
                    const auto color = gui.view.render.skinPreview().buildingColor(0);
                    if ((stage==0 && color) || (stage==1 && !color) || (stage==2 && color))
                    {
                        if (stage==0) originalColor=color;
                        if (stage==2 && color!=originalColor) throw std::runtime_error("Restored building color changed");
                        const char *names[]={"authorized","removed","restored"};
                        globalContainer->gfx->printScreen(std::string(moderationCapture)+"-"+names[stage]+".bmp");
                        nlohmann::json status={{"stage",names[stage]},{"elapsedMs",SDL_GetTicks()-start}};
                        if(color)status["buildingColor"]=*color;
                        { std::ofstream state(progress); state << status.dump() << '\n'; }
                        std::cout << status.dump() << std::endl;
                        ++stage;
                    }
                    globalContainer->gfx->nextFrame();
                    SDL_Delay(33);
                }
                if(stage!=3)throw std::runtime_error("Timed out waiting for moderation removal and restoration");
            }
            drawScene();
            globalContainer->gfx->printScreen(output);
            globalContainer->gfx->nextFrame();
            if (const char *hidden = std::getenv("SKIN_PREVIEW_HIDDEN_CAPTURE"))
            {
                globalContainer->settings.showColonySkins = false;
                drawScene();
                globalContainer->gfx->printScreen(hidden);
                globalContainer->gfx->nextFrame();
                globalContainer->settings.showColonySkins = true;
                drawScene();
                globalContainer->gfx->printScreen(std::string(hidden) + ".restored.bmp");
                globalContainer->gfx->nextFrame();
            }
        }
        delete globalContainer;
        globalContainer = nullptr;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
