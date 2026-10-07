// SPDX-License-Identifier: GPL-3.0-or-later
// Compare every direction and sampled phase through the production draw paths.
#include <Toolkit.h>
#include <GraphicContext.h>
#include <SkinMesh.h>
#include <SDL3/SDL.h>
#ifdef HAVE_OPENGL
#include <SDL3/SDL_opengl.h>
#endif
#include <cstdlib>
#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <array>
#include <cstring>
#include <new>
using namespace GAGCore;
int main(int argc, char **argv)
{
    if (argc != 3 && argc != 4) { std::cerr << "skin-preview ASSET_DIRECTORY OUTPUT_PREFIX [MODE]\n"
            << "skin-preview MESH.gsr|MESH.gsk OUTPUT_PREFIX --rig-review\n"; return 2; }
    Toolkit::init("glob2-skin-preview");
    struct CloseToolkit { ~CloseToolkit() { Toolkit::close(); } } closeToolkit;
    const bool rigReview = argc == 4 && std::string(argv[3]) == "--rig-review";
    auto *gfx = Toolkit::initGraphic(rigReview ? 320 : 1024, rigReview ? 240 : 960,
        GraphicContext::USEGPU | GraphicContext::NOAUDIO, "Colony skin feasibility");
    if (rigReview)
    {
#ifdef HAVE_OPENGL
        std::cout << "renderer=" << glGetString(GL_RENDERER)
                  << " version=" << glGetString(GL_VERSION) << '\n';
#endif
        // Offscreen production readback avoids desktop resizing and captures
        // every mapped pose at the actual atlas resolution, without resampling.
        SkinMesh mesh; std::string error;
        if (!mesh.load(argv[1], error)) { std::cerr << error << '\n'; return 4; }
        DrawableSurface paint(512,512), material(512,512);
        for (unsigned style = 0; style < 6; ++style)
        {
            paint.drawFilledRect(0,0,512,512,Color(160,195,125));
            const unsigned id = style < 4 ? style : 1;
            material.drawFilledRect(0,0,512,512,Color(id,id,id));
            if (style >= 4)
                for (int y=0; y<512; y+=16)
                    for (int x=0; x<512; x+=16)
                        if ((x/16+y/16)%2)
                            paint.drawFilledRect(x,y,16,16,Color(45,85,140));
            if (style == 5)
                for (int x=0; x<512; x+=64)
                    material.drawFilledRect(x,0,64,512,Color((x/64)%4,(x/64)%4,(x/64)%4));
            std::vector<std::uint8_t> sheet(2048*2048*4), rgba;
            for (unsigned frame=0; frame<256; ++frame)
            {
                if (!gfx->readSkinMesh({&mesh,frame,&paint,&material,SkinRegionWorker},rgba)
                    || rgba.size()!=128*128*4) return 5;
                for (unsigned y=0; y<128; ++y)
                    std::memcpy(sheet.data()+((frame/16*128+y)*2048+frame%16*128)*4,
                        rgba.data()+y*128*4,128*4);
            }
            auto *surface=SDL_CreateSurfaceFrom(2048,2048,SDL_PIXELFORMAT_RGBA32,sheet.data(),2048*4);
            if (!surface) return 6;
            const bool saved=SDL_SaveBMP(surface,(std::string(argv[2])+"-"+std::to_string(style)+".bmp").c_str());
            SDL_DestroySurface(surface);
            if (!saved) return 7;
        }
        std::cout << "Captured 256 frames with four materials, checker paint and mixed materials\n";
        return 0;
    }
    // Let the desktop map and present the window before recording captures.
    // Hardware readback can otherwise capture the compositor's opening animation.
    for (int frame = 0; frame < 25; ++frame)
    {
        SDL_PumpEvents();
        gfx->beginFrame(GraphicContext::FrameMode::FullRedraw);
        gfx->drawFilledRect(0,0,1024,960,Color(45,50,60));
        gfx->nextFrame();
        SDL_Delay(20);
    }
    {
        const char *mode = std::getenv("GLOB2_SKIN_RIGS");
        const bool useRig = mode && std::string(mode) == "1";
		auto meshPath = [&](const std::string &name)
		{
			const auto extension = useRig && name != "swarm" ? ".gsr" : ".gsk";
			return std::string(argv[1]) + "/" + name + extension;
		};
		// colony-v2: a 512x512 colour atlas and an optional 512x512 material-id
		// map (absent means all glossy), one 256x256 quadrant per model.
		DrawableSurface paint(std::string(argv[1]) + "/paint.webp");
		if (paint.getW() != 512 || paint.getH() != 512)
			return 3;
		auto loadedMaterial = loadSkinMaterialMap(std::string(argv[1]) + "/material.webp");
		DrawableSurface glossy(512,512);
        if (loadedMaterial && (loadedMaterial->getW() != 512 || loadedMaterial->getH() != 512)) return 3;
        DrawableSurface &material = loadedMaterial ? *loadedMaterial : glossy;
        if (argc == 4 && std::string(argv[3]) == "--validate-opacity")
        {
            SkinMesh mesh; std::string error;
            if (!mesh.load(meshPath("worker-walk"),error)) return 4;
            gfx->prepareSkinMeshes({{&mesh,0,&paint,&material,SkinRegionWorker}});
            DrawableSurface shadow(256,256);
            shadow.drawFilledRect(0,0,256,256,Color(90,70,50));
            for (bool underlay : {false,true})
                for (unsigned alpha : {255u,128u,0u})
                {
                    gfx->beginFrame(GraphicContext::FrameMode::FullRedraw);
                    gfx->drawFilledRect(0,0,1024,960,Color(45,50,60));
                    gfx->resetDrawCallCount();
                    if (!gfx->drawSkinMesh(mesh,0,paint,material,SkinRegionWorker,80,80,256,256,
                                          underlay ? &shadow : nullptr, Uint8(alpha))) return 13;
                    // Opacity changes only the composite, never the cached pose.
                    if (!underlay && gfx->getDrawCallCount() != (alpha ? 1u : 0u)) return 14;
                    if (!alpha && gfx->getDrawCallCount() != 0) return 15;
                    gfx->printScreen(std::string(argv[2])+(underlay ? "-shadow-" : "-mesh-")+
                                     std::to_string(alpha)+".bmp");
                    gfx->nextFrame();
                }
            std::cout << "Opacity composites reuse cached geometry; zero alpha draws nothing\n";
            return 0;
        }
        if (argc == 4 && std::string(argv[3]) == "--validate-cache")
        {
            SkinMesh mesh; std::string error;
            if (!mesh.load(meshPath("worker-walk"),error)) return 4;
            alignas(DrawableSurface) unsigned char storage[sizeof(DrawableSurface)];
            auto *reused = new(storage) DrawableSurface(512,512);
            DrawableSurface materials(512,512);
            std::uint8_t region = SkinRegionWorker;
            auto draw = [&](const char *name, unsigned expected) {
                gfx->beginFrame(GraphicContext::FrameMode::FullRedraw);
                gfx->drawFilledRect(0,0,1024,960,Color(45,50,60));
                gfx->resetDrawCallCount();
                gfx->prepareSkinMeshes({{&mesh,0,reused,&materials,region}});
                if (!gfx->drawSkinMesh(mesh,0,*reused,materials,region,80,80,256,256)
                    || gfx->getDrawCallCount()!=expected) return false;
                gfx->printScreen(std::string(argv[2])+"-"+name+".bmp");
                gfx->nextFrame();
                return true;
            };
            reused->drawFilledRect(0,0,512,512,Color(220,30,30));
            if (!draw("cold",2) || !draw("hit",1)) return 8;
            reused->drawFilledRect(0,0,512,512,Color(30,220,30));
            if (!draw("repaint",2) || !draw("repaint-hit",1)) return 9;
            // Material edits and a different quadrant are new rasterizations too.
            materials.drawFilledRect(0,0,512,512,Color(2,2,2));
            if (!draw("material",2) || !draw("material-hit",1)) return 16;
            region = SkinRegionWarrior;
            if (!draw("region",2) || !draw("region-hit",1)) return 17;
            region = SkinRegionWorker;
            const auto identity = reused->lifetimeIdentity();
            reused->~DrawableSurface();
            reused = new(storage) DrawableSurface(512,512);
            reused->drawFilledRect(0,0,512,512,Color(30,30,220));
            if (reused->lifetimeIdentity()==identity || !draw("reused-address",2)) return 10;
            // Exceed the four-page bound and then revisit a replaced tile.
            std::array<std::unique_ptr<DrawableSurface>,5> paints;
            std::vector<SkinMeshRequest> requests;
            for (unsigned i=0; i<paints.size(); ++i)
            {
                paints[i]=std::make_unique<DrawableSurface>(512,512);
                paints[i]->drawFilledRect(0,0,512,512,Color(40+i*40,100,180));
                for (unsigned frame=0; frame<256; ++frame) requests.push_back({&mesh,frame,paints[i].get(),&materials,SkinRegionWorker});
            }
            gfx->prepareSkinMeshes(requests);
            for (const auto &request : requests)
                if (!gfx->drawSkinMesh(mesh,request.frame,*request.texture,*request.material,request.region,0,0,32,32)) return 11;
            if (!draw("after-eviction",2) || !draw("after-eviction-hit",1)) return 12;
            reused->~DrawableSurface();
            std::cout << "Cache hits, paint and material updates, regions, address reuse and overflow passed\n";
            return 0;
        }
        if (argc == 4 && (std::string(argv[3]) == "--benchmark" || std::string(argv[3]) == "--benchmark-pages"))
        {
#ifdef HAVE_OPENGL
            std::cout << "video_driver=" << SDL_GetCurrentVideoDriver()
                      << " gl_vendor=" << glGetString(GL_VENDOR)
                      << " gl_renderer=" << glGetString(GL_RENDERER)
                      << " gl_version=" << glGetString(GL_VERSION) << std::endl;
#endif
            const unsigned phases = std::string(argv[3]) == "--benchmark-pages" ? 128 : 32;
            SkinMesh mesh; std::string error;
            if (!mesh.load(meshPath("worker-walk"),error)) return 4;
            struct MissingPaint : DrawableSurface { MissingPaint() : DrawableSurface() {} } missingPaint;
            gfx->resetDrawCallCount();
            gfx->prepareSkinMeshes({{&mesh,0,&missingPaint,&material,SkinRegionWorker},{&mesh,0,&paint,&missingPaint,SkinRegionWorker}});
            if (gfx->drawSkinMesh(mesh,0,missingPaint,material,SkinRegionWorker,0,0,32,32,&paint)
                || gfx->drawSkinMesh(mesh,0,paint,missingPaint,SkinRegionWorker,0,0,32,32,&paint)
                || gfx->getDrawCallCount() != 0) return 7;
            std::array<std::unique_ptr<DrawableSurface>,4> paints;
            for (unsigned i=0; i<paints.size(); ++i)
            {
                paints[i] = std::make_unique<DrawableSurface>(std::string(argv[1])+"/paint.webp");
                paints[i]->drawFilledRect(0,0,512,512,Color(50+i*50,180-i*30,70+i*35));
            }
            for (bool atlas : {false,true})
            {
                std::uint64_t elapsed = 0; unsigned long draws = 0;
                for (int iteration=0; iteration<45; ++iteration)
                {
                    std::vector<SkinMeshRequest> requests;
                    for (unsigned i=0; i<512; ++i)
                        requests.push_back({&mesh,((i/4)%phases+iteration)%256,paints[i%4].get(),&material,SkinRegionWorker});
                    const auto start = SDL_GetPerformanceCounter();
                    gfx->beginFrame(GraphicContext::FrameMode::FullRedraw);
                    gfx->drawFilledRect(0,0,1024,960,Color(45,50,60));
                    gfx->resetDrawCallCount();
                    if (atlas) gfx->prepareSkinMeshes(requests);
                    for (unsigned i=0; i<requests.size(); ++i)
                    {
                        const auto &request = requests[i];
                        if (!gfx->drawSkinMesh(*request.mesh,request.frame,*request.texture,*request.material,request.region,
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
        const std::uint8_t regions[] = {SkinRegionWorker,SkinRegionWorker,SkinRegionWorker,SkinRegionWarrior,
                                        SkinRegionWarrior,SkinRegionWarrior,SkinRegionExplorer,SkinRegionSwarm};
        for (int clip = 0; clip < 8; ++clip)
        {
            SkinMesh mesh; std::string error;
            if (!mesh.load(meshPath(names[clip]),error))
            { std::cerr << error << '\n'; return 4; }
            const bool allPhases = argc == 4 && std::string(argv[3]) == "--all-phases";
            const int pages = allPhases && clip != 7 ? 8 : 1;
            for (int page=0; page<pages; ++page)
            {
                gfx->beginFrame(GraphicContext::FrameMode::FullRedraw);
                gfx->drawFilledRect(0,0,1024,960,Color(45,50,60));
                for (int direction=0; direction<8; ++direction)
                    for (int sample=0; sample<4; ++sample)
                    {
                        int x=direction*128, y=sample*240, frame=mesh.frames == 1 ? 0 : direction*32+(allPhases ? page*4+sample : sample*8);
                        const int size = clip == 7 ? 114 : mesh.logicalSize * 3, inset = (128-size)/2;
                        if (clip != 7) gfx->drawSprite(x+inset,y+6,size,size,classic,bases[clip]*4+frame);
                        if (!gfx->drawSkinMesh(mesh,frame,paint,material,regions[clip],x+inset,y+126,size,size,
                            clip == 7 ? nullptr : classic->baseFrame(bases[clip]*4+frame)))
                        { std::cerr << "GPU mesh draw unavailable\n"; return 5; }
                    }
                gfx->printScreen(std::string(argv[2])+"-"+names[clip]+
                    (allPhases ? "-phase-"+std::to_string(page*4) : "")+".bmp");
                gfx->nextFrame();
            }
        }
    }
    return 0;
}
