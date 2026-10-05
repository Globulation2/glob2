// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ScopedEnvironment.h"
#include <string>
#include <set>
#include <cmath>
#include <cstdlib>
#include "Engine.h"
#include "Utilities.h"
#include "ReplayWriter.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "SettingsScreen.h"
#include "Order.h"
#include "Unit.h"
#include <SDL3_image/SDL_image.h>
#include <FileManager.h>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif
#include <algorithm>
#include <iostream>
#include <vector>
#include <chrono>
#include <filesystem>
class SettingsPaintHarness:public SettingsScreen
{
public:
    void draw(GraphicContext *surface){beginExecution(surface);paintFrame(0);endExecute(0);finishExecution();}
};
class HighResolutionIntegrationHarness
{
    static void finishAssets()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        bool complete = false;
        do {
            complete = Toolkit::pollAssets(4);
            if (complete) break;
            SDL_Delay(1);
        } while (std::chrono::steady_clock::now() < deadline);
        REQUIRE(complete);
    }
#ifdef HAVE_OPENGL
    static void capture(const std::string &name)
    {
        glFinish();GLint v[4];glGetIntegerv(GL_VIEWPORT,v);int w=v[2],h=v[3];
        std::vector<unsigned char>a(w*h*4),b(a.size());glReadPixels(v[0],v[1],w,h,GL_RGBA,GL_UNSIGNED_BYTE,a.data());
        for(int y=0;y<h;++y)std::copy_n(a.data()+y*w*4,w*4,b.data()+(h-1-y)*w*4);
        auto s=SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, b.data(), w*4);
        REQUIRE((s&&IMG_SavePNG(s,((glob2test::artifactDir() / "runtime-check" / (name+".png")).string()).c_str())));SDL_DestroySurface(s);
        REQUIRE(glGetError()==GL_NO_ERROR);
    }
    static std::vector<unsigned char> pixels()
    {
        Sprite::flushBatches(globalContainer->gfx);glFinish();
        GLint v[4];glGetIntegerv(GL_VIEWPORT,v);
        std::vector<unsigned char> result(v[2]*v[3]*4);
        glReadPixels(v[0],v[1],v[2],v[3],GL_RGBA,GL_UNSIGNED_BYTE,result.data());
        return result;
    }
    static bool coloredRegion(int x,int y,int w,int h)
    {
        auto gfx=globalContainer->gfx;auto data=pixels();GLint v[4];glGetIntegerv(GL_VIEWPORT,v);
        for(int j=y;j<y+h;++j)for(int i=x;i<x+w;++i)
        {
            int px=(i+.5)*v[2]/gfx->getW(),py=(gfx->getH()-j-.5)*v[3]/gfx->getH();
            auto pixel=&data[(py*v[2]+px)*4];
            if(pixel[0]||pixel[1]||pixel[2])return true;
        }
        return false;
    }
    static void checkCursor()
    {
        auto gfx=globalContainer->gfx;gfx->nextFrame();
        int w,h;auto window=SDL_GL_GetCurrentWindow();
        if(std::string(SDL_GetCurrentVideoDriver())=="cocoa")SDL_GetWindowSize(window,&w,&h);
        else SDL_GetWindowSizeInPixels(window,&w,&h);
        REQUIRE(std::abs(gfx->cursorManager.cacheScale-std::min(float(w)/gfx->getW(),float(h)/gfx->getH()))<.001);
    }
    static void checkWrappedSprites()
    {
        auto gfx=globalContainer->gfx;
        globalContainer->settings.highResolutionArtwork=true;
        MapEdit editor;editor.game.map.setSize(4,4,GRASS);editor.game.map.setGame(&editor.game);editor.game.addTeam(0);
        auto building=editor.game.addBuilding(15,15,globalContainer->buildingsTypes.getFinishedTypeNum("swarm"),0);REQUIRE(building);
        editor.regenerateGameHeader();editor.minimap.setGame(editor.game);editor.updateCamera();
        editor.game.map.displayViewportW=editor.game.map.displayViewportH=512;
        // Setup can destroy a staging GameGUI, which releases shared HD caches.
        Sprite::setHighResolution(true);
        // An incomplete NPOT mip chain samples white without producing a GL
        // error. Verify that both atlas-backed sprite families produce color.
        for(auto sprite:{globalContainer->terrain,globalContainer->resources})
        {
            const std::string prefix = sprite == globalContainer->terrain ? "terrain" : "ressource";
            auto atlas = Toolkit::getFileManager()->openImage("data/highres/v1/" + prefix + "-atlas-mip0.webp");
            if (atlas)
            {
                SDL_CloseIO(atlas);
                REQUIRE(sprite->highResolutionAtlas != nullptr);
            }
            // The bundled pack has HD resource frames but no HD terrain/atlases.
            if (sprite == globalContainer->resources) REQUIRE(sprite->experimentImages[0] != nullptr);
            gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);
            gfx->drawSprite(100,100,128,128,sprite,sprite==globalContainer->terrain?0:9);
            gfx->finishDrawingSprite(sprite,255);auto data=pixels();
            bool colored=false;
            for(size_t k=0;k<data.size();k+=4)
                if((data[k]||data[k+1]||data[k+2]) && !(data[k]==data[k+1]&&data[k+1]==data[k+2])){colored=true;break;}
            REQUIRE(colored);
        }

        std::set<Uint16> visible;
        for(double zoom:{.5,1.})
        {
            auto begin=[&](){gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);gfx->beginMapTransform(zoom,100,100,100,100,512*zoom,512*zoom);};
            begin();
            editor.game.drawMapGroundBuildings(0,0,16,16,512,512,0,0,0,Game::DRAW_WHOLE_MAP,&visible,nullptr, glob2test::sceneOf(editor.game), nullptr);
            gfx->endMapTransform();auto actual=pixels();REQUIRE(visible.size()==1);
            for(int y:{100,int(100+448*zoom)})for(int x:{100,int(100+448*zoom)})REQUIRE(coloredRegion(x,y,64*zoom,64*zoom));
            begin();
            {const Scene &buildingScene=glob2test::sceneOf(editor.game);for(int y:{-32,480})for(int x:{-32,480})editor.game.drawMapBuilding(x,y,building->gid,0,0,0,Game::DRAW_WHOLE_MAP,buildingScene, nullptr);}
            gfx->endMapTransform();REQUIRE(actual==pixels());
        }
        // More than one complete period must repeat geometry without duplicating
        // the visible-building identity used to emit particles.
        gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);
        gfx->beginMapTransform(.5,100,100,100,100,512,512);
        editor.game.drawMapGroundBuildings(0,0,32,32,1024,1024,0,0,0,Game::DRAW_WHOLE_MAP,&visible,nullptr, glob2test::sceneOf(editor.game), nullptr);
        gfx->endMapTransform();auto repeated=pixels();REQUIRE(visible.size()==1);
        gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);
        gfx->beginMapTransform(.5,100,100,100,100,512,512);
        {const Scene &buildingScene=glob2test::sceneOf(editor.game);for(int y:{-32,480,992})for(int x:{-32,480,992})editor.game.drawMapBuilding(x,y,building->gid,0,0,0,Game::DRAW_WHOLE_MAP,buildingScene, nullptr);}
        gfx->endMapTransform();REQUIRE(repeated==pixels());
        int advances=0;
        gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);
        gfx->beginMapTransform(.5,100,100,100,100,512,512);
        gfx->drawMapCopies(512,512,1024,1024,[&](){
            if(!gfx->isPeriodicCopy())++advances;
            gfx->drawFilledRect(80,80,32,32,255,0,0);
        });
        gfx->endMapTransform();REQUIRE(advances==1);
        for(int y:{140,396})for(int x:{140,396})REQUIRE(coloredRegion(x,y,16,16));
        // A moving unit crossing both seams must leave visible pieces in all four corners.
        Game units(nullptr);units.map.setSize(4,4,GRASS);units.map.setGame(&units);units.addTeam(0);
        auto unit=units.addUnit(0,0,0,0,0,128,1,1);REQUIRE(unit);
        unit->action=WALK;unit->dx=unit->dy=1;unit->delta=128;
        gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);
        gfx->beginMapTransform(.5,100,100,100,100,256,256);
        units.drawMapGroundUnits(0,0,16,16,512,512,0,0,0,Game::DRAW_WHOLE_MAP,editor.view, glob2test::sceneOf(units, editor.view));
        gfx->endMapTransform();
        for(int y:{100,340})for(int x:{100,340})REQUIRE(coloredRegion(x,y,16,16));
        editor.camera.setZoom(.5,200,200);editor.viewportX=editor.camera.tileX();editor.viewportY=editor.camera.tileY();
        editor.drawMap(0,0,gfx->getW(),gfx->getH());editor.drawMenu();editor.drawMiniMap();editor.drawWidgets();capture("seam-corners-50");
        // A full-period minimap viewport must have four edges, not a collapsed line.
        gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);
        Minimap mini(false,160,gfx->getW(),8,8,128,128,Minimap::ShowFOW);mini.setGame(editor.game);mini.draw(editor.view.drawnScene(),0,0,0,16,16);
        auto data=pixels();GLint v[4];glGetIntegerv(GL_VIEWPORT,v);
        auto white=[&](int x,int y){int px=(x+.5)*v[2]/gfx->getW(),py=(gfx->getH()-y-.5)*v[3]/gfx->getH();auto p=&data[(py*v[2]+px)*4];return p[0]==255&&p[1]==255&&p[2]==255;};
        const int left=gfx->getW()-160+8;
        REQUIRE((white(left+64,8)&&white(left+64,135)&&white(left,72)&&white(left+127,72)));
        std::cout<<"PASS full-period seam sprite coverage, single building identity, minimap outline and native cursor scale\n";
    }
#endif
public:
    static void runSoftware()
    {
        globalContainer->settings.highResolutionArtwork=true;
        // HD layers load with the sprites, so a byte count taken after loading and before
        // drawing shows whether software rendering picked them up. Drawing then adds only
        // the CPU team-colour cache, which software rendering uses for native art too.
        {
            Engine engine;REQUIRE(engine.initCustom("games/gd-small-2ai.game")==Engine::EE_NO_ERROR);
            finishAssets();
            REQUIRE(Sprite::highResolutionStats().cpuBytes==0);
            auto &gui=engine.gui;gui.updateCamera();gui.zoomMap(10,300,300);gui.drawAll(0);
            const auto checksum = gui.game.checkSum(nullptr, nullptr, nullptr, true);
            for (double zoom : {.5, 1., 2.})
            {
                gui.camera.setZoom(zoom, 300, 300);
                gui.viewportX = gui.camera.tileX(); gui.viewportY = gui.camera.tileY();
                gui.drawAll(0); globalContainer->gfx->nextFrame();
                gui.drawAll(0); globalContainer->gfx->nextFrame();
                REQUIRE(gui.game.checkSum(nullptr, nullptr, nullptr, true) == checksum);
                REQUIRE(Sprite::highResolutionStats().cpuBytes == 0);
            }
            // The game zooms its software map; the editor only with a stretching renderer.
            REQUIRE(gui.camera.zoom>1);
        }
        {
            const size_t loaded=Sprite::highResolutionStats().cpuBytes;
            MapEdit editor;REQUIRE(editor.load("maps/Archipelago.map"));editor.minimap.setGame(editor.game);
            finishAssets();
            REQUIRE(Sprite::highResolutionStats().cpuBytes==loaded);
            editor.updateCamera();editor.zoomMap(10,300,300);
            editor.drawMap(0,0,globalContainer->gfx->getW(),globalContainer->gfx->getH());
            editor.drawMenu();editor.drawMiniMap();editor.drawWidgets();
            REQUIRE((editor.camera.zoom==1)==!globalContainer->gfx->canDrawStretchedSprite());
        }
        std::cout<<"PASS software game/editor rendering, original artwork, software map zoom, editor zoom only with a stretching renderer\n";
    }
#ifdef HAVE_OPENGL
    static void run()
    {
        auto gfx=globalContainer->gfx;
        {SettingsPaintHarness settings;settings.draw(gfx);capture("settings");}
        checkCursor();checkWrappedSprites();
        std::vector<Uint32> simulationChecksums;
        for(bool hd:{false,true})
        {
            globalContainer->settings.highResolutionArtwork=hd;
            Engine engine;REQUIRE(engine.initCustom("games/gd-small-2ai.game")==Engine::EE_NO_ERROR);
            finishAssets();
            auto &gui=engine.gui;gui.updateCamera();
            const auto checksum=gui.game.checkSum(nullptr,nullptr,nullptr,true);
            for(double zoom:{gui.camera.minimumZoom(),.5,1.,2.,MapCamera::MAX_ZOOM})
            {
                gui.camera.setZoom(zoom,300,300);gui.viewportX=gui.camera.tileX();gui.viewportY=gui.camera.tileY();
                gui.mouseX=300;gui.mouseY=300;gui.updateCamera();
                auto w=gui.camera.screenToWorld(300,300);int x,y;
                gui.game.map.displayToMapCaseAligned(gui.mapMouseX(300),gui.mapMouseY(300),&x,&y,gui.viewportX,gui.viewportY);
                REQUIRE(x==int(MapCamera::wrap(w.first,gui.camera.mapWidth)/32));REQUIRE(y==int(MapCamera::wrap(w.second,gui.camera.mapHeight)/32));
				const auto orders=gui.orderQueue.size();
                SDL_Event wheel{};wheel.type=SDL_EVENT_MOUSE_WHEEL;wheel.wheel.y=1;
#if SDL_VERSION_ATLEAST(2,0,18)
                wheel.wheel.y=.25f;
#endif
				gui.processEvent(&wheel);REQUIRE(gui.orderQueue.size()==orders);
                gui.camera.setZoom(zoom,300,300);gui.viewportX=gui.camera.tileX();gui.viewportY=gui.camera.tileY();
                const auto randomState=syncRandEngine();const auto gameRandom=gui.game.syncRandom;
                gfx->resetDrawCallCount();auto start=std::chrono::steady_clock::now();
                for(int i=0;i<10;++i){gui.drawAll(0);glFinish();}
                auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/10;
                std::cout<<(hd?"HD":"original")<<" gameplay "<<zoom*100<<"%: "<<ms<<" ms, "<<gfx->getDrawCallCount()/10<<" calls, "<<DrawableSurface::allocatedTextureBytes()<<" GPU bytes\n";
                capture(std::string(hd?"game-hd-":"game-original-")+std::to_string(int(zoom*100)));
                REQUIRE(gui.game.checkSum(nullptr,nullptr,nullptr,true)==checksum);
                REQUIRE((syncRandEngine()==randomState && gui.game.syncRandom==gameRandom));
                if(hd && zoom==1.)
                {
                    const Settings previous=globalContainer->settings;
                    auto &settings=globalContainer->settings;
                    settings.clouds=false; settings.cloudShadows=true;
                    settings.buildingParticles=true; settings.translucentPanels=false;
                    settings.fullMagicEffects=false; settings.smoothProgressIndicators=false;
                    gui.drawAll(0); capture("game-hd-independent-effects");
                    REQUIRE(gui.game.checkSum(nullptr,nullptr,nullptr,true)==checksum);
                    REQUIRE((syncRandEngine()==randomState && gui.game.syncRandom==gameRandom));
                    settings=previous;
                }
                gui.selectionMode=GameGUI::TOOL_SELECTION;gui.toolManager.activateBuildingTool("explorationflag");
                gui.handleMapClick(300,300,SDL_BUTTON_LEFT);
                SDL_MouseButtonEvent placement{};placement.button=SDL_BUTTON_LEFT;placement.x=300;placement.y=300;
                gui.handleMouseButtonUp(placement);
                auto create=std::dynamic_pointer_cast<OrderCreate>(gui.toolManager.getOrder());
                REQUIRE((create&&create->posX==x&&create->posY==y));
                gui.ghostManager.removeBuilding(x,y);gui.toolManager.deactivateTool();gui.selectionMode=GameGUI::NO_SELECTION;
            }
            // Reset button must act only on press, and must not place a building on release.
            const auto orders=gui.orderQueue.size();SDL_MouseButtonEvent button{};button.button=SDL_BUTTON_LEFT;button.x=gfx->getW()-160+8+44;button.y=gfx->getH()-15;
            gui.handleMouseButtonDown(button);double after=gui.camera.zoom;gui.handleMouseButtonUp(button);
            REQUIRE((after==1&&gui.camera.zoom==after&&gui.orderQueue.size()==orders));
            if (glob2test::fullscreenEnabled())
            {
                auto center=gui.camera.screenToWorld(gui.camera.width/2.0,gui.camera.height/2.0);
                REQUIRE(gfx->toggleFullscreen());gui.updateCamera();gui.drawAll(0);capture(hd?"fullscreen-hd":"fullscreen-original");
                auto fullscreenCenter=gui.camera.screenToWorld(gui.camera.width/2.0,gui.camera.height/2.0);
                // Camera origins normalize on the torus when the larger native view
                // crosses a map seam; compare the same world location modulo its period.
                const auto centerDelta = [](double before,double after,double period) {
                    return std::abs(MapCamera::wrap(after-before+period/2,period)-period/2);
                };
                REQUIRE_MESSAGE((centerDelta(center.first,fullscreenCenter.first,gui.camera.mapWidth)<1e-6 &&
                                 centerDelta(center.second,fullscreenCenter.second,gui.camera.mapHeight)<1e-6),
                                "Fullscreen moved map center from "<<center.first<<","<<center.second
                                <<" to "<<fullscreenCenter.first<<","<<fullscreenCenter.second);
                REQUIRE(gfx->toggleFullscreen());
            }
            else std::cout<<"SKIP fullscreen camera continuity: enable with --fullscreen\n";
            for(int tick=0;tick<50;++tick)
            {
                gui.game.syncStep(0);auto sum=gui.game.checkSum(nullptr,nullptr,nullptr,true);
                if(!hd)simulationChecksums.push_back(sum);else REQUIRE(simulationChecksums[tick]==sum);
            }
        }
        globalContainer->settings.highResolutionArtwork=true;
        {
            // Record with this build's version: master intentionally rejects
            // historical replays after simulation/pathfinding changes.
            Engine fixture;REQUIRE(fixture.initCustom("games/gd-small-2ai.game")==Engine::EE_NO_ERROR);
            ReplayWriter writer;
            writer.init((glob2test::artifactDir() / "replay-fixture/replays/current.replay").string(),fixture.gui);
            writer.advanceStep();
        }
        {
            Engine replay;REQUIRE(replay.loadReplay("replays/current.replay")==Engine::EE_NO_ERROR);
            finishAssets();
            auto &gui=replay.gui;gui.updateCamera();gui.zoomMap(5,300,300);gui.drawAll(0);capture("replay-hd");
        }
        globalContainer->replaying=false;
        {
            MapEdit editor;REQUIRE(editor.load("maps/Archipelago.map"));editor.minimap.setGame(editor.game);editor.updateCamera();
            finishAssets();
            auto& settings = globalContainer->settings;
            const bool previousEdgeScroll = settings.edgeScrollWindowed;
            REQUIRE_FALSE(bool(globalContainer->gfx->getOptionFlags() & GraphicContext::FULLSCREEN));
            editor.mouseX = editor.mouseY = 1;
            settings.edgeScrollWindowed = false;
            editor.handleMapScroll();
            REQUIRE(editor.xSpeed == 0); REQUIRE(editor.ySpeed == 0);
            settings.edgeScrollWindowed = true;
            editor.handleMapScroll();
            REQUIRE(editor.xSpeed == -1); REQUIRE(editor.ySpeed == -1);
            settings.edgeScrollWindowed = false;
            editor.handleMapScroll();
            REQUIRE(editor.xSpeed == 0); REQUIRE(editor.ySpeed == 0);
            settings.edgeScrollWindowed = previousEdgeScroll;
            editor.mouseX = editor.mouseY = 300;
            const auto checksum=editor.game.checkSum(nullptr,nullptr,nullptr,true);
            for(double zoom:{editor.camera.minimumZoom(),.5,1.,2.,MapCamera::MAX_ZOOM})
            {
                editor.camera.setZoom(zoom,300,300);editor.viewportX=editor.camera.tileX();editor.viewportY=editor.camera.tileY();
                editor.mouseX=300;editor.mouseY=300;editor.updateCamera();
                editor.drawMap(0,0,gfx->getW(),gfx->getH());editor.drawMenu();editor.drawMiniMap();editor.drawWidgets();
                capture("editor-hd-"+std::to_string(int(zoom*100)));
                REQUIRE(editor.game.checkSum(nullptr,nullptr,nullptr,true)==checksum);
                auto w=editor.camera.screenToWorld(300,300);int x,y;
                editor.game.map.displayToMapCaseAligned(editor.mapMouseX(300),editor.mapMouseY(300),&x,&y,editor.viewportX,editor.viewportY);
                REQUIRE(x==int(MapCamera::wrap(w.first,editor.camera.mapWidth)/32));REQUIRE(y==int(MapCamera::wrap(w.second,editor.camera.mapHeight)/32));
                bool old=editor.game.map.canResourcesGrow(x,y);
                editor.brush.setFigure(0);editor.brush.setType(BrushTool::MODE_ADD);
                editor.resetPlacementTracking();editor.performAction("no ressource growth area drag start");
                REQUIRE(!editor.game.map.canResourcesGrow(x,y));
                editor.performAction("no ressource growth area drag end");
                editor.game.map.getTile(x,y).canResourcesGrow=old;

            }
            editor.camera.setZoom(1,300,300);
            editor.viewportX=editor.camera.tileX();editor.viewportY=editor.camera.tileY();
            editor.mouseX=300;editor.mouseY=300;
            SDL_Event wheel{};wheel.type=SDL_EVENT_MOUSE_WHEEL;wheel.wheel.y=1;
#if SDL_VERSION_ATLEAST(2,0,18)
            wheel.wheel.y=1;
#endif
            editor.processEvent(wheel);
            REQUIRE(editor.camera.zoom>1);
            SDL_Event down{};down.type=SDL_EVENT_MOUSE_BUTTON_DOWN;down.button.button=SDL_BUTTON_LEFT;
            down.button.x=300;down.button.y=300;
            editor.processEvent(down);
            REQUIRE((editor.isLeftScrollDragging && editor.isScrollDragging));
            editor.mouseX=1;editor.handleMapScroll();REQUIRE(editor.xSpeed==0);
            editor.mouseX=300;
            const double beforePan=editor.camera.originX;
            SDL_Event motion{};motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.x=312;motion.motion.y=308;
            motion.motion.xrel=12;motion.motion.yrel=8;motion.motion.state=SDL_BUTTON_MASK(SDL_BUTTON_LEFT);
            editor.processEvent(motion);
            REQUIRE(std::abs(editor.camera.originX-
                MapCamera::wrap(beforePan-12/editor.camera.zoom,editor.camera.mapWidth))<0.01);
            SDL_Event up=down;up.type=SDL_EVENT_MOUSE_BUTTON_UP;up.button.x=312;up.button.y=308;
            editor.processEvent(up);
            REQUIRE((!editor.isLeftScrollDragging && !editor.isScrollDragging));
            editor.performAction("select forbidden zone");
            const double paintingZoom=editor.camera.zoom;
            editor.processEvent(wheel);
            REQUIRE(editor.camera.zoom>paintingZoom);
            editor.processEvent(down);
            REQUIRE((editor.isDraggingZone && !editor.isLeftScrollDragging));
            editor.processEvent(motion);
            editor.processEvent(up);
            REQUIRE((!editor.isDraggingZone && !editor.isScrollDragging));
        }
        {
            MapEdit tinyEditor;tinyEditor.game.map.setSize(4,4,GRASS);tinyEditor.game.map.setGame(&tinyEditor.game);tinyEditor.game.addTeam(0);
            tinyEditor.regenerateGameHeader();tinyEditor.minimap.setGame(tinyEditor.game);
            tinyEditor.game.addBuilding(5,5,globalContainer->buildingsTypes.getFinishedTypeNum("swarm"),0);
            tinyEditor.updateCamera();tinyEditor.camera.setZoom(0.25,300,300);tinyEditor.viewportX=tinyEditor.camera.tileX();tinyEditor.viewportY=tinyEditor.camera.tileY();
            tinyEditor.drawMap(0,0,gfx->getW(),gfx->getH());tinyEditor.drawMenu();tinyEditor.drawMiniMap();tinyEditor.drawWidgets();capture("small-map-repeated");
            REQUIRE((tinyEditor.camera.visibleW()>512&&tinyEditor.camera.visibleH()>512));
            REQUIRE(tinyEditor.camera.contains(0,0));
            for(int px:{80,592})for(int py:{80,592})
            {
                int tx,ty;tinyEditor.game.map.displayToMapCaseAligned(tinyEditor.mapMouseX(px),tinyEditor.mapMouseY(py),&tx,&ty,tinyEditor.viewportX,tinyEditor.viewportY);
                int firstX,firstY;tinyEditor.game.map.displayToMapCaseAligned(tinyEditor.mapMouseX(80),tinyEditor.mapMouseY(80),&firstX,&firstY,tinyEditor.viewportX,tinyEditor.viewportY);
                REQUIRE((tx==firstX&&ty==firstY));
            }

        }
        for(bool hd:{false,true})
        {
            globalContainer->settings.highResolutionArtwork=hd;
            MapEdit dense;dense.game.map.setSize(6,6,GRASS);dense.game.map.setGame(&dense.game);
            finishAssets();
            for(int team=0;team<4;++team)dense.game.addTeam(team);
            const char *types[]={"swarm","inn","hospital","school","swimmingpool","barracks"};
            for(int y=4;y<60;y+=7)for(int x=4;x<60;x+=7)
            {
                const int team=(x+y)%4;
                dense.game.addBuilding(x,y,globalContainer->buildingsTypes.getFinishedTypeNum(types[(x+y)%6]),team);
                for(int i=0;i<3;++i)dense.game.addUnit(x+i,y+5,team,i,0,0,0,0);
            }
            for(int y=0;y<64;++y)for(int x=0;x<3;++x)dense.game.map.setUMatPos(x,y,WATER,1);
            dense.regenerateGameHeader();dense.minimap.setGame(dense.game);dense.updateCamera();
            for(double zoom:{dense.camera.minimumZoom(),.5,MapCamera::MAX_ZOOM})
            {
                dense.camera.setZoom(zoom,0,0);dense.viewportX=dense.camera.tileX();dense.viewportY=dense.camera.tileY();
                gfx->resetDrawCallCount();auto start=std::chrono::steady_clock::now();
                for(int i=0;i<20;++i){dense.drawMap(0,0,gfx->getW(),gfx->getH());glFinish();}
                auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/20;
                auto stats=Sprite::highResolutionStats();
                std::cout<<(hd?"HD":"original")<<" dense map "<<zoom*100<<"%: "<<ms<<" ms, "<<gfx->getDrawCallCount()/20<<" calls, "<<DrawableSurface::allocatedTextureBytes()<<" GPU bytes, "<<stats.cpuBytes<<" HD CPU bytes, "<<stats.coloredFrames<<" colored frames\n";
                dense.drawMenu();dense.drawMiniMap();dense.drawWidgets();capture(std::string(hd?"dense-hd-":"dense-original-")+std::to_string(int(zoom*100)));
            }
        }
        // GUI teardown schedules optional layer release; exercise the same
        // owner polling that the application performs before checking residency.
        finishAssets();
        REQUIRE(Sprite::highResolutionStats().cpuBytes==0);
        std::cout<<"PASS gameplay/editor conversions, wheel zoom, zoom controls, replay drawing, stable simulation checksums and resource release\n";
    }
#endif
};
namespace
{
void highResolution(bool software)
{
	// This fixture exercises the desktop sidebar's controls and hit coordinates.
	glob2test::ScopedEnvironment desktopUI("GLOB2_MOBILE_UI", "0");
	std::filesystem::create_directories(glob2test::artifactDir() / "runtime-check");
	std::filesystem::create_directories(glob2test::artifactDir() / "replay-fixture/replays");
	glob2test::GlobalsOptions options{.display = true, .loadStrings = true, .width = 1024, .height = 768,
	                                  .screenFlags = software ? 0u : Uint32(GraphicContext::USEGPU | GraphicContext::CUSTOMCURSOR)};
	options.beforeLoad = [](GlobalContainer& globals) { globals.fileManager->addDir((glob2test::artifactDir() / "replay-fixture").string()); };
	glob2test::HeadlessGlobals globals(options);
	if (software) HighResolutionIntegrationHarness::runSoftware();
#ifdef HAVE_OPENGL
    else HighResolutionIntegrationHarness::run();
#endif
}
}

TEST_SUITE("HighResolutionIntegration")
{
	TEST_CASE("HD artwork; cursor scaling and replay fixture in software rendering [display:1024x768][artifacts]") { highResolution(true); }
#ifdef HAVE_OPENGL
	TEST_CASE("HD artwork; cursor scaling and replay fixture in OpenGL [display:1024x768][artifacts][writes-preferences]") { highResolution(false); }
#endif
}
