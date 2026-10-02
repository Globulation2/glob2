// SPDX-License-Identifier: GPL-3.0-or-later
// Compare every direction and sampled phase through the production draw paths.
#include <Toolkit.h>
#include <GraphicContext.h>
#include <SkinMesh.h>
#include <SDL3/SDL.h>
#include <cstdlib>
#include <iostream>
#include <string>
using namespace GAGCore;
int main(int argc, char **argv)
{
    if (argc != 3) { std::cerr << "skin-preview ASSET_DIRECTORY OUTPUT_PREFIX\n"; return 2; }
    Toolkit::init("glob2-skin-preview");
    auto *gfx = Toolkit::initGraphic(1024, 960, GraphicContext::USEGPU, "Colony skin feasibility");
    {
        DrawableSurface paint(std::string(argv[1])+"/paint.png");
        if (paint.getW() != 256 || paint.getH() != 256) return 3;
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
                    if (!gfx->drawSkinMesh(mesh,frame,paint,x+inset,y+126,size,size))
                    { std::cerr << "GPU mesh draw unavailable\n"; return 5; }
                }
            gfx->printScreen(std::string(argv[2])+"-"+names[clip]+".bmp");
            gfx->nextFrame();
        }
    }
    Toolkit::close();
    return 0;
}
