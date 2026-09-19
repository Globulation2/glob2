// Exercise diagnostic rendering through a live, scaled graphics context.
#include "GlobalContainer.h"
#include "Game.h"
#include "team/Team.h"
#include "MapRender.h"
#include <Toolkit.h>
#include <FileManager.h>
#include <SDL_image.h>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>

GlobalContainer* globalContainer=nullptr;

int main(int argc, char** argv)
{
    GlobalContainer globals(argc>1 ? argv[1] : "glob2-map-render-test");
    if(argc>2) GAGCore::Toolkit::getFileManager()->addDir(argv[2]);
    globalContainer=&globals;
    globals.runNoX=true;
    globals.load();
    SDL_setenv("GLOB2_UI_SCALE","1.5",1);
    const bool useGL=argc>4 && std::string(argv[4])=="--gl";
    const Uint32 mode=GAGCore::GraphicContext::RESIZABLE
        | (useGL ? GAGCore::GraphicContext::USEGPU : 0);
    if(useGL) globals.gfx=GAGCore::Toolkit::initGraphic(1200,900,mode,"Map render regression","glob2");
    MapRender::ensureAssets();
    auto* gfx=globals.gfx;
    assert(gfx->setRes(1200,900,mode));
    gfx->setClipRect(11,12,300,200);
    auto* surface=gfx->getSDLSurface();
    const Uint32 flags=gfx->getOptionFlags();
    const unsigned generation=gfx->getGLContextGeneration();
    const auto verify=[&]() {
        assert(gfx==globals.gfx && gfx->getSDLSurface()==surface);
        assert(gfx->getRequestedW()==1200 && gfx->getRequestedH()==900);
        assert(gfx->getW()==800 && gfx->getH()==600);
        assert(gfx->getUiScale()==1.5f && gfx->getOptionFlags()==flags);
        assert(gfx->getGLContextGeneration()==generation);
        assert(std::string(SDL_getenv("GLOB2_UI_SCALE"))=="1.5");
        int x,y,w,h; gfx->getClipRect(&x,&y,&w,&h);
        assert(x==11 && y==12 && w==300 && h==200);
    };
    Game game(nullptr);
    game.map.setSize(5,5,GRASS);
    game.map.setGame(&game);
    game.addTeam();
    game.teams[0]->race.loadDefault();
    assert(game.addBuilding(8,8,globals.buildingsTypes.getTypeNum("inn",0,false),0));
    assert(game.addUnit(12,12,0,WORKER,0,0,0,0));
    game.map.setResource(16,16,WHEAT,1);
    const Uint32 checksum=game.checkSum();
    const std::string path=argc>3 ? std::string(argv[3])+"/render.png" : "render.png";
    MapRender::toPng(game,path,256);
    verify();
    SDL_Surface* png=IMG_Load(path.c_str());
    assert(png && png->w==256 && png->h==256);
    SDL_FreeSurface(png);
    assert(game.checkSum()==checksum);
    // The error occurs inside the borrowed surface callback.
    MapRender::Field wrong;
    wrong.width=1; wrong.height=1; wrong.values={1};
    bool rejected=false;
    try { MapRender::toPng(game,path,256,&wrong); }
    catch(const std::runtime_error&) { rejected=true; }
    assert(rejected); verify();
    // Output failures occur after drawing; these must leave the window intact too.
    rejected=false;
    try { MapRender::toPng(game,path+"/invalid.png",256); }
    catch(const std::exception&) { rejected=true; }
    assert(rejected); verify();
    std::cout<<"Map render preserves resolution, scale, surface, flags and clip on success/failure\n";
}
