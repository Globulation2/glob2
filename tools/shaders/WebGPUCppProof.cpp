// SPDX-License-Identifier: GPL-3.0-or-later
// Browser correctness fixture for the actual C++ packet/owned-resource adapter.
#include <WebGPUDevice.h>
#include <emscripten.h>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
using namespace GAGCore;
namespace {
std::unique_ptr<RenderDevice> device;
SDL_Window *window;
std::string error;
unsigned nextTexture=0;
TextureSnapshot pressureSource, secondSource;
TextureSnapshot image(int width,int height,SDL_Color color,TextureDescriptor::Alpha alpha=TextureDescriptor::Alpha::Straight) {
    auto value=std::make_shared<TextureRevision>();value->handle={++nextTexture,1};value->contentRevision=1;
    value->descriptor.width=width;value->descriptor.height=height;value->descriptor.alpha=alpha;
    value->pixels={SDL_CreateSurface(width,height,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface};
    if(!value->pixels)throw std::runtime_error(SDL_GetError());
    SDL_FillSurfaceRect(value->pixels.get(),nullptr,SDL_MapSurfaceRGBA(value->pixels.get(),color.r,color.g,color.b,color.a));
    value->bytes=std::size_t(width)*height*4;return value;
}
DrawPacket clear() {DrawPacket packet;packet.kind=DrawPacket::Kind::Clear;packet.clearColor=true;packet.clearValue={0,0,0,1};return packet;}
DrawPacket blit(TextureSnapshot source,SDL_FRect destination,bool linear=false) {
    DrawPacket packet;packet.kind=DrawPacket::Kind::Blit;packet.texture=source;packet.source={0,0,source->descriptor.width,source->descriptor.height};packet.destination=destination;packet.linear=linear;return packet;
}
void submit(std::vector<DrawPacket> packets,bool present=true){RenderFrame frame;frame.logicalWidth=frame.logicalHeight=64;frame.packets=std::move(packets);if(!device->submit(std::move(frame)))throw std::runtime_error("Proof frame rejected");if(present)device->present();}
template<class F> int checked(F action){try{action();return 1;}catch(const std::exception&e){error=e.what();return 0;}}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE const char *proof_error(){return error.c_str();}
EMSCRIPTEN_KEEPALIVE int proof_init(){return checked([]{if(!SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());window=SDL_CreateWindow("C++ WebGPU proof",64,64,0);if(!window)throw std::runtime_error(SDL_GetError());device=makeWebGPURenderDevice(window);});}
EMSCRIPTEN_KEEPALIVE int proof_skin(int quadrant){return checked([&]{
    auto paint=image(4,4,{0,0,0,255});
    const SDL_Color colors[]{{180,30,20,255},{20,180,30,255},{30,20,180,255},{180,140,20,255}};
    for(int y=0;y<4;++y)for(int x=0;x<4;++x){auto c=colors[(y/2)*2+x/2];c.r+=x%2*8;c.g+=y%2*8;SDL_WriteSurfacePixel(paint->pixels.get(),x,y,c.r,c.g,c.b,c.a);}
    auto materials=image(4,4,{0,0,0,255});auto geometry=std::make_shared<GeometryRevision>();geometry->handle={1,1};geometry->contentRevision=1;geometry->layout=GeometryLayout::Skin40;geometry->stride=40;
    const float vertices[]{-1,-1,0,0,0,1,0,0,0,0, 1,-1,0,0,0,1,1,0,1,0, 1,1,0,0,0,1,1,1,1,1, -1,-1,0,0,0,1,0,0,0,0, 1,1,0,0,0,1,1,1,1,1, -1,1,0,0,0,1,0,1,0,1};
    geometry->vertices.resize(sizeof vertices);std::memcpy(geometry->vertices.data(),vertices,sizeof vertices);geometry->bytes=sizeof vertices;
    auto uniforms=std::make_shared<SkinMaterial>();uniforms->region={float(quadrant&1)*.5f,float(quadrant>>1)*.5f};
    DrawPacket packet;packet.kind=DrawPacket::Kind::MeshTriangles;packet.material=DrawPacket::Material::Skin;packet.geometry=geometry;packet.count=6;packet.texture=paint;packet.secondTexture=materials;packet.skin=uniforms;packet.blend=DrawPacket::Blend::Opaque;
    submit({clear(),packet});
});}
EMSCRIPTEN_KEEPALIVE int proof_terrain(int premultiplied){return checked([&]{
    auto target=image(4,4,{0,0,0,0},premultiplied?TextureDescriptor::Alpha::Premultiplied:TextureDescriptor::Alpha::Straight);
    auto compose=std::make_shared<WeightedComposeData>();compose->paletteCount=1;compose->compositionSize=32;compose->reductionDivisor=8;compose->targetRect={0,0,4,4};compose->materials[0]=image(1,1,{0,255,0,128});
    DrawPacket packet;packet.kind=DrawPacket::Kind::WeightedCompose;packet.target=target;packet.weightedCompose=compose;
    submit({packet,clear(),blit(target,{0,0,64,64})});
});}
EMSCRIPTEN_KEEPALIVE int proof_pressure(int stage){return checked([&]{
    if(stage==0){device->reset();nextTexture=0;pressureSource=image(4096,2048,{180,30,20,255});secondSource=image(2048,2048,{20,180,30,255});submit({clear(),blit(pressureSource,{0,0,64,64})});}
    else if(stage==1)submit({blit(pressureSource,{0,0,64,64},true)});
    else if(stage==2)submit({blit(pressureSource,{0,0,32,64},true)},false);
    else submit({blit(secondSource,{32,0,32,64})});
});}
}
