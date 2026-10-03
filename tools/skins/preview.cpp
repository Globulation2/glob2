// SPDX-License-Identifier: GPL-3.0-or-later
// Compare every direction and sampled phase through the production draw paths.
#include <Toolkit.h>
#include <GraphicContext.h>
#include <SkinMesh.h>
#include <SDL3/SDL.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <array>
#include <new>
using namespace GAGCore;
int main(int argc, char **argv)
{
    if (argc != 3 && argc != 4) { std::cerr << "skin-preview ASSET_DIRECTORY OUTPUT_PREFIX\n"; return 2; }
    Toolkit::init("glob2-skin-preview");
    struct CloseToolkit { ~CloseToolkit() { Toolkit::close(); } } closeToolkit;
    auto *gfx = Toolkit::initGraphic(1024, 960, GraphicContext::USEGPU, "Colony skin feasibility");
    {
        DrawableSurface paint(std::string(argv[1])+"/paint.png");
        if (paint.getW() != 256 || paint.getH() != 256) return 3;
        if (argc == 4 && std::string(argv[3]) == "--validate-cache")
        {
            SkinMesh mesh; std::string error;
            if (!mesh.load(std::string(argv[1])+"/worker-walk.gsk",error)) return 4;
            alignas(DrawableSurface) unsigned char storage[sizeof(DrawableSurface)];
            auto *reused = new(storage) DrawableSurface(256,256);
            auto draw = [&](const char *name, unsigned expected) {
                gfx->beginFrame(GraphicContext::FrameMode::FullRedraw);
                gfx->drawFilledRect(0,0,1024,960,Color(45,50,60));
                gfx->resetDrawCallCount();
                gfx->prepareSkinMeshes({{&mesh,0,reused}});
                if (!gfx->drawSkinMesh(mesh,0,*reused,80,80,256,256)
                    || gfx->getDrawCallCount()!=expected) return false;
                gfx->printScreen(std::string(argv[2])+"-"+name+".bmp");
                gfx->nextFrame();
                return true;
            };
            reused->drawFilledRect(0,0,256,256,Color(220,30,30));
            if (!draw("cold",2) || !draw("hit",1)) return 8;
            reused->drawFilledRect(0,0,256,256,Color(30,220,30));
            if (!draw("repaint",2) || !draw("repaint-hit",1)) return 9;
            const auto identity = reused->lifetimeIdentity();
            reused->~DrawableSurface();
            reused = new(storage) DrawableSurface(256,256);
            reused->drawFilledRect(0,0,256,256,Color(30,30,220));
            if (reused->lifetimeIdentity()==identity || !draw("reused-address",2)) return 10;
            // Exceed the four-page bound and then revisit a replaced tile.
            std::array<std::unique_ptr<DrawableSurface>,5> paints;
            std::vector<SkinMeshRequest> requests;
            for (unsigned i=0; i<paints.size(); ++i)
            {
                paints[i]=std::make_unique<DrawableSurface>(256,256);
                paints[i]->drawFilledRect(0,0,256,256,Color(40+i*40,100,180));
                for (unsigned frame=0; frame<256; ++frame) requests.push_back({&mesh,frame,paints[i].get()});
            }
            gfx->prepareSkinMeshes(requests);
            for (const auto &request : requests)
                if (!gfx->drawSkinMesh(mesh,request.frame,*request.texture,0,0,32,32)) return 11;
            if (!draw("after-eviction",2) || !draw("after-eviction-hit",1)) return 12;
            reused->~DrawableSurface();
            std::cout << "Cache hits, paint updates, address reuse and overflow passed\n";
            return 0;
        }
        if (argc == 4 && (std::string(argv[3]) == "--benchmark" || std::string(argv[3]) == "--benchmark-pages"))
        {
            const unsigned phases = std::string(argv[3]) == "--benchmark-pages" ? 128 : 32;
            SkinMesh mesh; std::string error;
            if (!mesh.load(std::string(argv[1])+"/worker-walk.gsk",error)) return 4;
            struct MissingPaint : DrawableSurface { MissingPaint() : DrawableSurface() {} } missingPaint;
            gfx->resetDrawCallCount();
            gfx->prepareSkinMeshes({{&mesh,0,&missingPaint}});
            if (gfx->drawSkinMesh(mesh,0,missingPaint,0,0,32,32,&paint)
                || gfx->getDrawCallCount() != 0) return 7;
            std::array<std::unique_ptr<DrawableSurface>,4> paints;
            for (unsigned i=0; i<paints.size(); ++i)
            {
                paints[i] = std::make_unique<DrawableSurface>(std::string(argv[1])+"/paint.png");
                paints[i]->drawFilledRect(0,0,256,256,Color(50+i*50,180-i*30,70+i*35));
            }
            for (bool atlas : {false,true})
            {
                std::uint64_t elapsed = 0; unsigned long draws = 0;
                for (int iteration=0; iteration<45; ++iteration)
                {
                    std::vector<SkinMeshRequest> requests;
                    for (unsigned i=0; i<512; ++i)
                        requests.push_back({&mesh,((i/4)%phases+iteration)%256,paints[i%4].get()});
                    const auto start = SDL_GetPerformanceCounter();
                    gfx->beginFrame(GraphicContext::FrameMode::FullRedraw);
                    gfx->drawFilledRect(0,0,1024,960,Color(45,50,60));
                    gfx->resetDrawCallCount();
                    if (atlas) gfx->prepareSkinMeshes(requests);
                    for (unsigned i=0; i<requests.size(); ++i)
                    {
                        const auto &request = requests[i];
                        if (!gfx->drawSkinMesh(*request.mesh,request.frame,*request.texture,
                            (i%32)*32,(i/32)*48,32,32)) return 5;
                    }
                    const auto count = gfx->getDrawCallCount();
                    if (count < 512u || count > 512u+phases*4)
                    { std::cerr << "Unexpected mesh draw count " << count << '\n'; return 6; }
                    if (iteration==44) gfx->printScreen(std::string(argv[2])+(atlas?"-atlas.bmp":"-immediate.bmp"));
                    gfx->nextFrame();
                    if (iteration>=5) { elapsed += SDL_GetPerformanceCounter()-start; draws += count; }
                }
                std::cout << (atlas?"atlas":"immediate") << " sprites=512 unique=" << phases*4 << " frames=40 mean_ms="
                    << (1000.0*elapsed/SDL_GetPerformanceFrequency()/40) << " draws_per_frame=" << draws/40 << '\n';
            }
            return 0;
        }
        auto *classic = Toolkit::getSprite("data/gfx/unit");
        classic->setBaseColor(Color(60,180,80));
        const char *names[] = {"worker-walk", "worker-swim", "worker-harvest",
                               "warrior-walk", "warrior-swim", "warrior-fight", "explorer-fly", "swarm"};
        const int bases[] = {64,128,192,256,320,384,0,0};
        for (int clip = 0; clip < 8; ++clip)
        {
            SkinMesh mesh; std::string error;
            if (!mesh.load(std::string(argv[1])+"/"+names[clip]+".gsk",error))
            { std::cerr << error << '\n'; return 4; }
            gfx->beginFrame(GraphicContext::FrameMode::FullRedraw);
            gfx->drawFilledRect(0,0,1024,960,Color(45,50,60));
            for (int direction=0; direction<8; ++direction)
                for (int sample=0; sample<4; ++sample)
                {
                    int x=direction*128, y=sample*240, frame=mesh.frames == 1 ? 0 : direction*32+sample*8;
                    const int size = clip == 7 ? 114 : mesh.logicalSize * 3, inset = (128-size)/2;
                    if (clip != 7) gfx->drawSprite(x+inset,y+6,size,size,classic,bases[clip]*4+frame);
                    if (!gfx->drawSkinMesh(mesh,frame,paint,x+inset,y+126,size,size,
                        clip == 7 ? nullptr : classic->baseFrame(bases[clip]*4+frame)))
                    { std::cerr << "GPU mesh draw unavailable\n"; return 5; }
                }
            gfx->printScreen(std::string(argv[2])+"-"+names[clip]+".bmp");
            gfx->nextFrame();
        }
    }
    return 0;
}
