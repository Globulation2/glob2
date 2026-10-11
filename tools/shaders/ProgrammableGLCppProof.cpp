// SPDX-License-Identifier: GPL-3.0-or-later
// Pinned-SDK fixture for the actual C++ programmable GL resource/packet path.
#include <ProgrammableGLDevice.h>
#include <emscripten.h>
#include <memory>
#include <cstring>
#include <stdexcept>
#include <string>
using namespace GAGCore;
namespace {
std::unique_ptr<RenderDevice> device;
SDL_Window *window=nullptr;
std::string error;
unsigned nextTexture=0;
std::uint64_t frameIdentity=0;
TextureSnapshot target,source;
TextureSnapshot image(int width,int height,SDL_Color color,TextureDescriptor::Alpha alpha=TextureDescriptor::Alpha::Straight){
    auto value=std::make_shared<TextureRevision>();value->handle={++nextTexture,1};value->contentRevision=1;
    value->descriptor.width=width;value->descriptor.height=height;value->descriptor.alpha=alpha;
    value->pixels={SDL_CreateSurface(width,height,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface};
    if(!value->pixels)throw std::runtime_error(SDL_GetError());
    SDL_FillSurfaceRect(value->pixels.get(),nullptr,SDL_MapSurfaceRGBA(value->pixels.get(),color.r,color.g,color.b,color.a));
    value->bytes=std::size_t(width)*height*4;return value;
}
template<class F>int checked(F action){try{action();error.clear();return 1;}catch(const std::exception&e){error=e.what();return 0;}}
void submit(std::vector<DrawPacket> packets){RenderFrame frame;frame.identity=++frameIdentity;frame.logicalWidth=frame.logicalHeight=64;frame.packets=std::move(packets);if(!device->submit(std::move(frame)))throw std::runtime_error("Proof frame admission rejected");device->present();}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE const char *proof_error(){return error.c_str();}
EMSCRIPTEN_KEEPALIVE int proof_init(){return checked([]{
    if(!SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
    window=SDL_CreateWindow("GL CPP proof",64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    if(!window)throw std::runtime_error(SDL_GetError());
    device=makeProgrammableGLDevice(window,"/shaders");
});}
EMSCRIPTEN_KEEPALIVE int proof_software(){return checked([]{
    if(!SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION,"0");
    window=SDL_CreateWindow("Software CPP proof",64,64,0);
    if(!window)throw std::runtime_error(SDL_GetError());
    auto *surface=SDL_GetWindowSurface(window);if(!surface)throw std::runtime_error(SDL_GetError());
    if(!SDL_FillSurfaceRect(surface,nullptr,SDL_MapSurfaceRGBA(surface,80,90,100,255)) ||
        !SDL_UpdateWindowSurface(window))throw std::runtime_error(SDL_GetError());
});}
// Keep SDL initialized between devices, as a running game does.
EMSCRIPTEN_KEEPALIVE int proof_shutdown(){return checked([]{target.reset();source.reset();device.reset();SDL_DestroyWindow(window);window=nullptr;});}
EMSCRIPTEN_KEEPALIVE int proof_terrain(int reduction,int mixed){return checked([&]{
    target=image(32/reduction,32/reduction,{0,0,0,0},TextureDescriptor::Alpha::Premultiplied);
    source=image(4,4,{0,255,0,128});
    auto data=std::make_shared<WeightedComposeData>();data->paletteCount=mixed?2:1;data->compositionSize=32;
    data->reductionDivisor=reduction;data->targetRect={0,0,32/reduction,32/reduction};data->materials[0]=source;
    if(mixed){data->materials[1]=source;auto mask=std::make_shared<CoverageData>();mask->identity=++nextTexture;mask->revision=1;mask->width=mask->height=32;mask->samples.resize(1024);
        for(auto &sample:mask->samples){sample.others[0]=32768;sample.slots=0;sample.margin=0;}
        data->coverage=mask;}
    DrawPacket compose;compose.kind=DrawPacket::Kind::WeightedCompose;compose.target=target;compose.weightedCompose=data;
    DrawPacket clear;clear.kind=DrawPacket::Kind::Clear;clear.clearColor=true;clear.clearValue={0,0,0,1};
    DrawPacket blit;blit.kind=DrawPacket::Kind::Blit;blit.texture=target;blit.source={0,0,32/reduction,32/reduction};blit.destination={0,0,64,64};blit.blend=DrawPacket::Blend::Premultiplied;
    submit({compose,clear,blit});
});}
EMSCRIPTEN_KEEPALIVE int proof_world(){return checked([]{
    if(!target)return;
    const std::array<WorldVertex,6> vertices{{
        {{-1,-1,0,1},{1,1,1},{0,0},{0,0,1}},
        {{1,-1,0,1},{1,1,1},{1,0},{0,0,1}},
        {{-1,1,0,1},{1,1,1},{0,1},{0,0,1}},
        {{-1,1,0,1},{1,1,1},{0,1},{0,0,1}},
        {{1,-1,0,1},{1,1,1},{1,0},{0,0,1}},
        {{1,1,0,1},{1,1,1},{1,1},{0,0,1}}
    }};
    auto geometry=std::make_shared<GeometryRevision>();geometry->handle={++nextTexture,1};
    geometry->contentRevision=1;geometry->layout=GeometryLayout::World48;geometry->stride=48;
    geometry->vertices.resize(sizeof(vertices));std::memcpy(geometry->vertices.data(),vertices.data(),sizeof(vertices));geometry->bytes=sizeof(vertices);
    DrawPacket packet;packet.kind=DrawPacket::Kind::MeshTriangles;packet.material=DrawPacket::Material::WorldLit;
    packet.texture=target;packet.geometry=geometry;packet.count=6;packet.blend=DrawPacket::Blend::Opaque;
    packet.world.projection[0]=packet.world.projection[5]=packet.world.projection[10]=packet.world.projection[15]=1;
    packet.world.uvTransform={1,1,0,0};packet.world.sunSpecular={0,0,1,0};submit({packet});
});}
EMSCRIPTEN_KEEPALIVE int proof_capture(){return checked([]{CaptureRequest request;request.identity=1;request.width=request.height=64;if(!device->capture(request))throw std::runtime_error("Capture rejected");});}
EMSCRIPTEN_KEEPALIVE int proof_poll(){try{CaptureResult result;if(!device->pollCapture(result))return 0;if(result.cancelled)throw std::runtime_error("Capture cancelled");Uint8 r,g,b,a;if(!SDL_ReadSurfacePixel(result.pixels.get(),32,32,&r,&g,&b,&a))throw std::runtime_error(SDL_GetError());EM_ASM({globalThis.proofPixel=Array.of($0,$1,$2,$3);},r,g,b,a);return 1;}catch(const std::exception&e){error=e.what();return -1;}}
EMSCRIPTEN_KEEPALIVE int proof_resize(int width,int height){return checked([&]{if(!SDL_SetWindowSize(window,width,height))throw std::runtime_error(SDL_GetError());});}
EMSCRIPTEN_KEEPALIVE int proof_trim(){return checked([]{target.reset();source.reset();device->trimRetiredResources();});}
}
