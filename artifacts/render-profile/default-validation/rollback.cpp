#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#include <ProgrammableGLDevice.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace GAGCore;
static void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
static auto fixture(int width, int height)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    auto *window = SDL_CreateWindow("Frame copy probe", width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    require(window != nullptr, SDL_GetError());
    return std::pair{window, makeProgrammableGLDevice(window, "/Users/bradley/.codex/worktrees/render-optimizations/glob2/artifacts/render-profile/default-direct-shaders")};
}
static DrawPacket fill(int width, int height, SDL_Color color)
{
    DrawPacket packet;
    packet.destination = {0,0,float(width),float(height)};
    packet.color = color;
    packet.opaque = true;
    return packet;
}
static void submit(RenderDevice &device, int w, int h, DrawPacket packet)
{
    RenderFrame frame;
    frame.logicalWidth = w;
    frame.logicalHeight = h;
    frame.packets = {packet};
    require(bool(device.submit(std::move(frame))), "submit");
}
static std::array<Uint8, 4> pixel(RenderDevice &device, int x, int y)
{
    CaptureRequest request;
    request.pixelRegion = SDL_Rect{x,y,1,1};
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(device.captureSynchronously(request), SDL_DestroySurface);
    std::array<Uint8,4> rgba{};
    require(SDL_ReadSurfacePixel(surface.get(),0,0,&rgba[0],&rgba[1],&rgba[2],&rgba[3]),"readback");
    return rgba;
}
int main()
{
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    {
        auto [window, device] = fixture(64,64);
        const std::array<Uint8,4> red{240,20,20,255}, green{20,240,20,255};
        submit(*device,64,64,fill(64,64,{240,20,20,255})); device->present();
        submit(*device,64,64,fill(64,64,{20,20,240,255})); device->discardFrame();
        require(pixel(*device,31,31)==red,"full redraw rollback");
        submit(*device,64,64,fill(16,16,{20,240,20,255})); device->present();
        require(pixel(*device,8,8)==green && pixel(*device,31,31)==red,"partial redraw");
        submit(*device,64,64,fill(16,16,{20,20,240,255})); device->discardFrame();
        require(pixel(*device,8,8)==green && pixel(*device,31,31)==red,"partial redraw rollback");
        auto logicalOverride = fill(64,64,{20,20,240,255});
        logicalOverride.targetLogicalWidth = 128;
        logicalOverride.targetLogicalHeight = 128;
        submit(*device,64,64,logicalOverride); device->present();
        require(pixel(*device,8,8)==std::array<Uint8,4>{20,20,240,255} &&
                pixel(*device,48,48)==red,"packet logical dimensions");
        submit(*device,64,64,fill(16,16,{20,240,20,255})); device->present();
        DrawPacket clear;
        clear.kind=DrawPacket::Kind::Clear;
        clear.clearColor=true;
        clear.clearValue={0,0,1,1};
        submit(*device,64,64,clear); device->discardFrame();
        require(pixel(*device,8,8)==green && pixel(*device,48,48)==red,"clear rollback");
        device.reset(); SDL_DestroyWindow(window);
        std::puts("PASS full, partial and explicit-clear rollback");
    }
    for (const auto [width,height] : {std::pair{1920,1080},std::pair{3840,2160}})
    {
        auto [window,device]=fixture(width,height);
        std::vector<double> times;
        for (int i=-16; i<120; ++i)
        {
            glFinish();
            const auto begin=std::chrono::steady_clock::now();
            submit(*device,width,height,fill(width,height,{Uint8(i),90,120,255}));
            glFinish();
            const auto end=std::chrono::steady_clock::now();
            if (i>=0) times.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
            device->present();
        }
        require(pixel(*device,width/2,height/2)==std::array<Uint8,4>{119,90,120,255},"final frame");
        std::sort(times.begin(),times.end());
        std::printf("%dx%d p50_ms=%.3f p95_ms=%.3f p99_ms=%.3f\n",width,height,times[60],times[113],times[118]);
        device.reset(); SDL_DestroyWindow(window);
    }
    SDL_Quit();
}
