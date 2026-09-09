// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"
#include "ReplayWriter.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "SettingsScreen.h"
#include "Order.h"
#include "Unit.h"
#include <SDL_image.h>
#include <FileManager.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#include <cassert>
#include <algorithm>
#include <iostream>
#include <vector>
#include <chrono>
#include <filesystem>
GlobalContainer* globalContainer=nullptr;
class SettingsPaintHarness:public SettingsScreen
{
public:
    void draw(GraphicContext *surface){gfx=surface;dispatchInit();paint();for(auto widget:widgets)if(widget->visible)widget->paint();}
};
class HighResolutionIntegrationHarness
{
    static void capture(const std::string &name)
    {
        glFinish();GLint v[4];glGetIntegerv(GL_VIEWPORT,v);int w=v[2],h=v[3];
        std::vector<unsigned char>a(w*h*4),b(a.size());glReadPixels(v[0],v[1],w,h,GL_RGBA,GL_UNSIGNED_BYTE,a.data());
        for(int y=0;y<h;++y)std::copy_n(a.data()+y*w*4,w*4,b.data()+(h-1-y)*w*4);
        auto s=SDL_CreateRGBSurfaceWithFormatFrom(b.data(),w,h,32,w*4,SDL_PIXELFORMAT_RGBA32);
        assert(s&&IMG_SavePNG(s,(".cache/highres-runtime-check/"+name+".png").c_str())==0);SDL_FreeSurface(s);
        assert(glGetError()==GL_NO_ERROR);
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
        else SDL_GL_GetDrawableSize(window,&w,&h);
        assert(std::abs(gfx->cursorManager.cacheScale-std::min(float(w)/gfx->getW(),float(h)/gfx->getH()))<.001);
    }
    static void checkWrappedSprites()
    {
        auto gfx=globalContainer->gfx;
        globalContainer->settings.highResolutionArtwork=true;
        MapEdit editor;editor.game.map.setSize(4,4,GRASS);editor.game.map.setGame(&editor.game);editor.game.addTeam(0);
        auto building=editor.game.addBuilding(15,15,globalContainer->buildingsTypes.getFinishedTypeNum("swarm"),0);assert(building);
        editor.regenerateGameHeader();editor.minimap.setGame(editor.game);editor.updateCamera();
        editor.game.map.displayViewportW=editor.game.map.displayViewportH=512;
        // An incomplete NPOT mip chain samples white without producing a GL
        // error. Verify that both atlas-backed sprite families produce color.
        for(auto sprite:{globalContainer->terrain,globalContainer->resources})
        {
            gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);
            gfx->drawSprite(100,100,128,128,sprite,sprite==globalContainer->terrain?0:9);
            gfx->finishDrawingSprite(sprite,255);auto data=pixels();
            bool colored=false;
            for(size_t k=0;k<data.size();k+=4)
                if((data[k]||data[k+1]||data[k+2]) && !(data[k]==data[k+1]&&data[k+1]==data[k+2])){colored=true;break;}
            assert(colored);
        }

        std::set<Building*> visible;
        for(double zoom:{.5,1.})
        {
            auto begin=[&](){gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);gfx->beginMapTransform(zoom,100,100,100,100,512*zoom,512*zoom);};
            begin();
            editor.game.drawMapGroundBuildings(0,0,16,16,512,512,0,0,0,Game::DRAW_WHOLE_MAP,&visible,nullptr);
            gfx->endMapTransform();auto actual=pixels();assert(visible.size()==1);
            for(int y:{100,int(100+448*zoom)})for(int x:{100,int(100+448*zoom)})assert(coloredRegion(x,y,64*zoom,64*zoom));
            begin();
            for(int y:{-32,480})for(int x:{-32,480})editor.game.drawMapBuilding(x,y,building->gid,0,0,0,Game::DRAW_WHOLE_MAP);
            gfx->endMapTransform();assert(actual==pixels());
        }
        // More than one complete period must repeat geometry without duplicating
        // the visible-building identity used to emit particles.
        gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);
        gfx->beginMapTransform(.5,100,100,100,100,512,512);
        editor.game.drawMapGroundBuildings(0,0,32,32,1024,1024,0,0,0,Game::DRAW_WHOLE_MAP,&visible,nullptr);
        gfx->endMapTransform();auto repeated=pixels();assert(visible.size()==1);
        gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);
        gfx->beginMapTransform(.5,100,100,100,100,512,512);
        for(int y:{-32,480,992})for(int x:{-32,480,992})editor.game.drawMapBuilding(x,y,building->gid,0,0,0,Game::DRAW_WHOLE_MAP);
        gfx->endMapTransform();assert(repeated==pixels());
        int advances=0;
        gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);
        gfx->beginMapTransform(.5,100,100,100,100,512,512);
        gfx->drawMapCopies(512,512,1024,1024,[&](){
            if(!gfx->isPeriodicCopy())++advances;
            gfx->drawFilledRect(80,80,32,32,255,0,0);
        });
        gfx->endMapTransform();assert(advances==1);
        for(int y:{140,396})for(int x:{140,396})assert(coloredRegion(x,y,16,16));
        // A moving unit crossing both seams must leave visible pieces in all four corners.
        Game units(nullptr);units.map.setSize(4,4,GRASS);units.map.setGame(&units);units.addTeam(0);
        auto unit=units.addUnit(0,0,0,0,0,128,1,1);assert(unit);
        unit->action=WALK;unit->dx=unit->dy=1;unit->delta=128;
        gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);
        gfx->beginMapTransform(.5,100,100,100,100,256,256);
        units.drawMapGroundUnits(0,0,16,16,512,512,0,0,0,Game::DRAW_WHOLE_MAP,editor.view);
        gfx->endMapTransform();
        for(int y:{100,340})for(int x:{100,340})assert(coloredRegion(x,y,16,16));
        editor.camera.setZoom(.5,200,200);editor.viewportX=editor.camera.tileX();editor.viewportY=editor.camera.tileY();
        editor.drawMap(0,0,gfx->getW(),gfx->getH());editor.drawMenu();editor.drawMiniMap();editor.drawWidgets();capture("seam-corners-50");
        // A full-period minimap viewport must have four edges, not a collapsed line.
        gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);
        Minimap mini(false,160,gfx->getW(),8,8,128,128,Minimap::ShowFOW);mini.setGame(editor.game);mini.draw(0,0,0,16,16);
        auto data=pixels();GLint v[4];glGetIntegerv(GL_VIEWPORT,v);
        auto white=[&](int x,int y){int px=(x+.5)*v[2]/gfx->getW(),py=(gfx->getH()-y-.5)*v[3]/gfx->getH();auto p=&data[(py*v[2]+px)*4];return p[0]==255&&p[1]==255&&p[2]==255;};
        const int left=gfx->getW()-160+8;
        assert(white(left+64,8)&&white(left+64,135)&&white(left,72)&&white(left+127,72));
        std::cout<<"PASS full-period seam sprite coverage, single building identity, minimap outline and native cursor scale\n";
    }
public:
    static void runSoftware()
    {
        globalContainer->settings.highResolutionArtwork=true;
        {
            Engine engine;assert(engine.initCustom("games/gd-small-2ai.game")==Engine::EE_NO_ERROR);
            auto &gui=engine.gui;gui.updateCamera();gui.zoomMap(10,300,300);gui.drawAll(0);
            assert(gui.camera.zoom==1&&Sprite::highResolutionStats().cpuBytes==0);
        }
        {
            MapEdit editor;assert(editor.load("maps/Archipelago.map"));editor.minimap.setGame(editor.game);
            editor.updateCamera();editor.zoomMap(10,300,300);
            editor.drawMap(0,0,globalContainer->gfx->getW(),globalContainer->gfx->getH());
            editor.drawMenu();editor.drawMiniMap();editor.drawWidgets();
            assert(editor.camera.zoom==1&&Sprite::highResolutionStats().cpuBytes==0);
        }
        std::cout<<"PASS software game/editor rendering, original artwork, disabled zoom\n";
    }
    static void run()
    {
        auto gfx=globalContainer->gfx;
        {SettingsPaintHarness settings;settings.draw(gfx);capture("settings");}
        checkCursor();checkWrappedSprites();
        std::vector<Uint32> simulationChecksums;
        for(bool hd:{false,true})
        {
            globalContainer->settings.highResolutionArtwork=hd;
            Engine engine;assert(engine.initCustom("games/gd-small-2ai.game")==Engine::EE_NO_ERROR);
            auto &gui=engine.gui;gui.updateCamera();
            const auto checksum=gui.game.checkSum(nullptr,nullptr,nullptr,true);
            for(double zoom:{.5,1.,2.,3.})
            {
                gui.camera.setZoom(zoom,300,300);gui.viewportX=gui.camera.tileX();gui.viewportY=gui.camera.tileY();
                gui.mouseX=300;gui.mouseY=300;gui.updateCamera();
                auto w=gui.camera.screenToWorld(300,300);int x,y;
                gui.game.map.displayToMapCaseAligned(gui.mapMouseX(300),gui.mapMouseY(300),&x,&y,gui.viewportX,gui.viewportY);
                assert(x==int(MapCamera::wrap(w.first,gui.camera.mapWidth)/32));assert(y==int(MapCamera::wrap(w.second,gui.camera.mapHeight)/32));
                const auto orders=gui.orderQueue.size();SDL_SetModState(KMOD_ALT);
                SDL_Event wheel{};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.y=1;
#if SDL_VERSION_ATLEAST(2,0,18)
                wheel.wheel.preciseY=.25f;
#endif
                gui.processEvent(&wheel);assert(gui.orderQueue.size()==orders);SDL_SetModState(KMOD_NONE);
                gui.camera.setZoom(zoom,300,300);gui.viewportX=gui.camera.tileX();gui.viewportY=gui.camera.tileY();
                gfx->resetDrawCallCount();auto start=std::chrono::steady_clock::now();
                for(int i=0;i<10;++i){gui.drawAll(0);glFinish();}
                auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/10;
                std::cout<<(hd?"HD":"original")<<" gameplay "<<zoom*100<<"%: "<<ms<<" ms, "<<gfx->getDrawCallCount()/10<<" calls, "<<DrawableSurface::allocatedTextureBytes()<<" GPU bytes\n";
                capture(std::string(hd?"game-hd-":"game-original-")+std::to_string(int(zoom*100)));
                assert(gui.game.checkSum(nullptr,nullptr,nullptr,true)==checksum);
                gui.selectionMode=GameGUI::TOOL_SELECTION;gui.toolManager.activateBuildingTool("explorationflag");
                gui.handleMapClick(300,300,SDL_BUTTON_LEFT);
                SDL_MouseButtonEvent placement{};placement.button=SDL_BUTTON_LEFT;placement.x=300;placement.y=300;
                gui.handleMouseButtonUp(placement);
                auto create=std::dynamic_pointer_cast<OrderCreate>(gui.toolManager.getOrder());
                assert(create&&create->posX==x&&create->posY==y);
                gui.ghostManager.removeBuilding(x,y);gui.toolManager.deactivateTool();gui.selectionMode=GameGUI::NO_SELECTION;
            }
            // Reset button must act only on press, and must not place a building on release.
            const auto orders=gui.orderQueue.size();SDL_MouseButtonEvent button{};button.button=SDL_BUTTON_LEFT;button.x=gfx->getW()-160+8+44;button.y=gfx->getH()-15;
            gui.handleMouseButtonDown(button);double after=gui.camera.zoom;gui.handleMouseButtonUp(button);
            assert(after==1&&gui.camera.zoom==after&&gui.orderQueue.size()==orders);
            auto center=gui.camera.screenToWorld(gui.camera.width/2,gui.camera.height/2);
            assert(gfx->toggleFullscreen());gui.updateCamera();gui.drawAll(0);capture(hd?"fullscreen-hd":"fullscreen-original");
            auto fullscreenCenter=gui.camera.screenToWorld(gui.camera.width/2,gui.camera.height/2);
            assert(center==fullscreenCenter);assert(gfx->toggleFullscreen());
            for(int tick=0;tick<50;++tick)
            {
                gui.game.syncStep(0);auto sum=gui.game.checkSum(nullptr,nullptr,nullptr,true);
                if(!hd)simulationChecksums.push_back(sum);else assert(simulationChecksums[tick]==sum);
            }
        }
        globalContainer->settings.highResolutionArtwork=true;
        {
            // Record with this build's version: master intentionally rejects
            // historical replays after simulation/pathfinding changes.
            Engine fixture;assert(fixture.initCustom("games/gd-small-2ai.game")==Engine::EE_NO_ERROR);
            ReplayWriter writer;
            writer.init(std::filesystem::absolute(".cache/highres-replay-fixture/replays/current.replay").string(),fixture.gui);
            writer.advanceStep();
        }
        {
            Engine replay;assert(replay.loadReplay("replays/current.replay")==Engine::EE_NO_ERROR);
            auto &gui=replay.gui;gui.updateCamera();gui.zoomMap(5,300,300);gui.drawAll(0);capture("replay-hd");
        }
        globalContainer->replaying=false;
        {
            MapEdit editor;assert(editor.load("maps/Archipelago.map"));editor.minimap.setGame(editor.game);editor.updateCamera();
            const auto checksum=editor.game.checkSum(nullptr,nullptr,nullptr,true);
            for(double zoom:{.5,1.,2.,3.})
            {
                editor.camera.setZoom(zoom,300,300);editor.viewportX=editor.camera.tileX();editor.viewportY=editor.camera.tileY();
                editor.mouseX=300;editor.mouseY=300;editor.updateCamera();
                editor.drawMap(0,0,gfx->getW(),gfx->getH());editor.drawMenu();editor.drawMiniMap();editor.drawWidgets();
                capture("editor-hd-"+std::to_string(int(zoom*100)));
                assert(editor.game.checkSum(nullptr,nullptr,nullptr,true)==checksum);
                auto w=editor.camera.screenToWorld(300,300);int x,y;
                editor.game.map.displayToMapCaseAligned(editor.mapMouseX(300),editor.mapMouseY(300),&x,&y,editor.viewportX,editor.viewportY);
                assert(x==int(MapCamera::wrap(w.first,editor.camera.mapWidth)/32));assert(y==int(MapCamera::wrap(w.second,editor.camera.mapHeight)/32));
                bool old=editor.game.map.canResourcesGrow(x,y);
                editor.brush.setFigure(0);editor.brush.setType(BrushTool::MODE_ADD);
                editor.resetPlacementTracking();editor.performAction("no ressource growth area drag start");
                assert(!editor.game.map.canResourcesGrow(x,y));
                editor.performAction("no ressource growth area drag end");
                editor.game.map.getTile(x,y).canResourcesGrow=old;

            }
        }
        {
            MapEdit small;small.game.map.setSize(4,4,GRASS);small.game.map.setGame(&small.game);small.game.addTeam(0);
            small.regenerateGameHeader();small.minimap.setGame(small.game);
            small.game.addBuilding(5,5,globalContainer->buildingsTypes.getFinishedTypeNum("swarm"),0);
            small.updateCamera();small.camera.setZoom(.5,300,300);small.viewportX=small.camera.tileX();small.viewportY=small.camera.tileY();
            small.drawMap(0,0,gfx->getW(),gfx->getH());small.drawMenu();small.drawMiniMap();small.drawWidgets();capture("small-map-repeated");
            assert(small.camera.visibleW()>512&&small.camera.visibleH()>512);
            assert(small.camera.contains(0,0));
            for(int px:{80,336,592})for(int py:{80,336,592})
            {
                int tx,ty;small.game.map.displayToMapCaseAligned(small.mapMouseX(px),small.mapMouseY(py),&tx,&ty,small.viewportX,small.viewportY);
                int firstX,firstY;small.game.map.displayToMapCaseAligned(small.mapMouseX(80),small.mapMouseY(80),&firstX,&firstY,small.viewportX,small.viewportY);
                assert(tx==firstX&&ty==firstY);
            }

        }
        for(bool hd:{false,true})
        {
            globalContainer->settings.highResolutionArtwork=hd;
            MapEdit dense;dense.game.map.setSize(6,6,GRASS);dense.game.map.setGame(&dense.game);
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
            for(double zoom:{.5,3.})
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
        assert(Sprite::highResolutionStats().cpuBytes==0);
        std::cout<<"PASS gameplay/editor conversions, Alt-wheel isolation, zoom controls, replay drawing, stable simulation checksums and resource release\n";
    }
};
int main(int argc,char **argv)
{
    std::filesystem::create_directories(".cache/highres-runtime-check");
    std::filesystem::create_directories(".cache/highres-replay-fixture/replays");
    GlobalContainer globals("glob2-hd-integration-test");globalContainer=&globals;
    globals.settings.screenWidth=1024;globals.settings.screenHeight=768;globals.settings.screenFlags=GraphicContext::USEGPU|GraphicContext::CUSTOMCURSOR;
    globals.settings.rememberUnit=false;globals.settings.mute=1;
    globals.fileManager->addDir(".cache/highres-replay-fixture");
    const bool software=argc>1&&std::string(argv[1])=="software";
    if(software)globals.settings.screenFlags=0;
    globals.load();
    if(software)HighResolutionIntegrationHarness::runSoftware();else HighResolutionIntegrationHarness::run();
}
