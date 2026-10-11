// SPDX-License-Identifier: GPL-3.0-or-later
#include "GraphicContextPrivate.h"
#include <RenderDevice.h>
#include <SkinModel.h>
#include <SkinMesh.h>
#include <SkinShapeModel.h>
#include <glob2/SkinMaterials.h>
#include <map>
#include <algorithm>
#include <span>
#include <stdexcept>

namespace GAGCore {
namespace {
constexpr unsigned TileSize=128, AtlasSize=2048, Columns=16, SlotsPerPage=256;
constexpr float Padding=1.25f;
bool validDeviceSkin(const SkinMeshRequest &r) {
    return r.mesh && r.mesh->identity && r.mesh->vertices && r.frame<r.mesh->frames &&
        r.region<4 && r.texture && r.material && r.texture->getSDLSurface() && r.material->getSDLSurface();
}
auto skinKey(const SkinMeshRequest &r) {
    return SkinAtlasCache::key(r.mesh->identity,r.frame,r.texture->lifetimeIdentity(),r.texture->contentRevision(),
        r.material->lifetimeIdentity(),r.material->contentRevision(),r.region);
}
template<unsigned N> std::array<float,N*N> transposed(const std::array<float,N*N> &matrix) {
    std::array<float,N*N> result{};
    for(unsigned y=0;y<N;++y) for(unsigned x=0;x<N;++x) result[x*N+y]=matrix[y*N+x];
    return result;
}
}
struct DeviceSkinState {
    RenderResources &resources;
    SkinAtlasCache slots;
    std::vector<TextureHandle> pages;
    struct Image {TextureHandle handle;std::uint64_t revision;};
    std::map<std::uint64_t,Image> images;
    std::map<std::pair<std::uint64_t,unsigned>,BufferHandle> meshes;
    explicit DeviceSkinState(RenderResources &r):resources(r) {}
    ~DeviceSkinState() {
        for(auto handle:pages) resources.erase(handle);
        for(auto &[identity,image]:images) resources.erase(image.handle);
        for(auto &[identity,handle]:meshes) resources.erase(handle);
    }
    TextureSnapshot image(DrawableSurface &surface) {
        auto found=images.find(surface.lifetimeIdentity());
        if(found!=images.end() && found->second.revision==surface.contentRevision()) return resources.snapshot(found->second.handle);
        if(found!=images.end()) {resources.erase(found->second.handle);images.erase(found);}
        while(images.size()>=64) {auto oldest=images.begin();resources.erase(oldest->second.handle);images.erase(oldest);}
        const auto handle=resources.create(surface.getSDLSurface(),surface.contentRevision());
        if(!handle) return {};
        images.emplace(surface.lifetimeIdentity(),Image{handle,surface.contentRevision()});
        return resources.snapshot(handle);
    }
    TextureSnapshot page(unsigned index) {
        while(pages.size()<=index) {
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(SDL_CreateSurface(AtlasSize,AtlasSize,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
            if(!pixels) return {};
            SDL_ClearSurface(pixels.get(),0,0,0,0);
            TextureDescriptor descriptor;descriptor.depthAttachment=true;
            descriptor.alpha=TextureDescriptor::Alpha::Premultiplied;
            descriptor.budgetClass=TextureDescriptor::BudgetClass::SkinAtlas;
            auto handle=resources.create(pixels.get(),1,descriptor);
            if(!handle) return {};
            pages.push_back(handle);
        }
        return resources.snapshot(pages[index]);
    }
    GeometrySnapshot geometry(const SkinMesh &mesh,unsigned frame,bool rig) {
        const auto key=std::pair{mesh.identity,rig?SkinClipFrames:frame};
        if(auto found=meshes.find(key);found!=meshes.end()) return resources.snapshot(found->second);
        std::vector<float> evaluated,vertices;
        if(!rig && !mesh.evaluate(frame,evaluated)) return {};
        const unsigned stride=rig?18:10;
        vertices.resize(std::size_t(mesh.vertices)*stride);
        const auto &detail=mesh.detailUV.empty()?mesh.uv:mesh.detailUV;
        if(mesh.uv.size()<mesh.vertices*2 || detail.size()<mesh.vertices*2) return {};
        for(unsigned v=0;v<mesh.vertices;++v) {
            auto *out=vertices.data()+v*stride;
            std::copy_n((rig?mesh.model->rest():evaluated).data()+v*6,6,out);
            std::copy_n(mesh.uv.data()+v*2,2,out+6);std::copy_n(detail.data()+v*2,2,out+8);
            if(rig) for(unsigned j=0;j<4;++j) {out[10+j]=mesh.model->influences()[v].bones[j];out[14+j]=mesh.model->influences()[v].weights[j];}
        }
        while(meshes.size()>=128) {auto oldest=meshes.begin();resources.erase(oldest->second);meshes.erase(oldest);}
        auto handle=resources.createGeometry(rig?GeometryLayout::SkinRig72:GeometryLayout::Skin40,
            std::as_bytes(std::span(vertices)),mesh.indices,1);
        if(!handle) return {};
        meshes.emplace(key,handle);return resources.snapshot(handle);
    }
};
void GraphicContext::prepareDeviceSkinMeshes(const std::vector<SkinMeshRequest> &requests) {
    if(!presentationDevice || renderer != deviceRenderer.get() || !presentationDevice->capabilities().skinMaterials || requests.empty()) return;
    if(!deviceSkinState) deviceSkinState=std::make_shared<DeviceSkinState>(*deviceResources);
    auto &state=*deviceSkinState;
    std::map<SkinAtlasCache::Key,SkinMeshRequest> missing;
    for(const auto &request:requests) if(validDeviceSkin(request)) missing.emplace(skinKey(request),request);
    while(missing.size()>SkinAtlasCache::Capacity) missing.erase(std::prev(missing.end()));
    for(auto it=missing.begin();it!=missing.end();) if(state.slots.touch(it->first)) it=missing.erase(it);else ++it;
    for(const auto &[key,request]:missing) {
        const auto paint=state.image(*request.texture),material=state.image(*request.material);
        const auto &mesh=*request.mesh;
        const bool rig=bool(mesh.model);
        const auto geometry=state.geometry(mesh,request.frame,rig);
        if(!paint || !material || !geometry) continue;
        std::shared_ptr<SkinPose> pose;
        if(rig) {
            SkinPalette palette;
            if(!mesh.model->paletteForFrame(mesh.clip,request.frame,palette)) continue;
            pose=std::make_shared<SkinPose>();
            for(unsigned bone=0;bone<palette.count;++bone) {
                const auto matrix=transposed<4>(palette.positions[bone]);
                std::copy(matrix.begin(),matrix.end(),pose->bones.begin()+bone*16);
            }
            const auto &clip=mesh.model->clips()[mesh.clip];
            pose->view=transposed<4>(clip.modelToClip);pose->normalView=transposed<3>(clip.normalToCamera);
        }
        // Allocate before reserving a cache key: admission failure must not
        // publish a tile that was never rendered.
        unsigned prospective=state.slots.size()<SkinAtlasCache::Capacity?state.slots.size():0;
        if(!state.page(prospective/SlotsPerPage)) continue;
        const auto slot=state.slots.reserve(key),tile=slot%SlotsPerPage;
        const auto target=state.page(slot/SlotsPerPage);
        if(!target || !beginDeviceTarget(target,AtlasSize,AtlasSize)) { deviceSkinState.reset(); return; }
        try {
            const SDL_Rect viewport{int(tile%Columns*TileSize),int(tile/Columns*TileSize),TileSize,TileSize};
            DrawPacket clear;clear.kind=DrawPacket::Kind::Clear;clear.clearDepth=true;clear.clearColor=false;
            submitDevicePacket(std::move(clear));
            clear={};clear.kind=DrawPacket::Kind::Clear;clear.clearColor=true;clear.clearValue={0,0,0,0};clear.clip=viewport;
            submitDevicePacket(std::move(clear));
            const unsigned shells=skinShellRegions(*request.material)[request.region]?SKIN_MATERIAL_SHELLS:0;
            for(unsigned shell=0;shell<=shells;++shell) {
                auto skin=std::make_shared<SkinMaterial>();skin->pose=pose;
                skin->shell=float(shell)/SKIN_MATERIAL_SHELLS;skin->furLength=SKIN_FUR_LENGTH;skin->shellDepth=SKIN_SHELL_DEPTH;
                skin->region={float(request.region&1)*0.5f,float(request.region>>1)*0.5f};
                DrawPacket packet;packet.kind=DrawPacket::Kind::MeshTriangles;
                packet.material=rig?DrawPacket::Material::SkinRig:DrawPacket::Material::Skin;
                packet.skin=skin;packet.geometry=geometry;packet.indexed=true;packet.count=mesh.indices.size();
                packet.texture=paint;packet.secondTexture=material;packet.depth=DrawPacket::Depth::Less;packet.depthWrite=true;
                packet.blend=DrawPacket::Blend::Opaque;packet.viewport=viewport;packet.clip=viewport;
                submitDevicePacket(std::move(packet));
            }
            endDeviceTarget();
        } catch(...) {endDeviceTarget();deviceSkinState.reset();throw;}
    }
}
bool GraphicContext::drawDeviceSkinMesh(const SkinMeshRequest &request,float x,float y,float w,float h,DrawableSurface *underlay,Uint8 alpha) {
    if(!validDeviceSkin(request) || !rendererCapabilities().liveSkins) return false;
    prepareDeviceSkinMeshes({request});
    if(!deviceSkinState) return false;
    auto &state=*deviceSkinState;const auto slot=state.slots.find(skinKey(request));
    if(!slot) return false;
    if(underlay) drawSurface(x,y,w,h,underlay,alpha);
    DrawPacket packet;packet.kind=DrawPacket::Kind::Blit;packet.texture=state.page(*slot/SlotsPerPage);
    const auto tile=*slot%SlotsPerPage;
    packet.source={int(tile%Columns*TileSize),int(tile/Columns*TileSize),TileSize,TileSize};
    packet.destination={x-w*(Padding-1)/2,y-h*(Padding-1)/2,w*Padding,h*Padding};
    packet.alpha=alpha;packet.linear=true;packet.blend=DrawPacket::Blend::Premultiplied;
    return submitDevicePacket(std::move(packet));
}
bool GraphicContext::readDeviceSkinMesh(const SkinMeshRequest &request,std::vector<std::uint8_t> &rgba) {
    // Promise-based WebGPU readback is exposed through asynchronous captures;
    // this legacy synchronous baking entry point reports it as unavailable.
    if(renderer != deviceRenderer.get() || backendStatus.active==RendererBackend::WebGPU) return false;
    prepareDeviceSkinMeshes({request});if(!deviceSkinState) return false;
    const auto slot=deviceSkinState->slots.find(skinKey(request));if(!slot) return false;
    renderer->flush();if(!didRecordDeviceFrame(*renderer)) return false;const auto tile=*slot%SlotsPerPage;
    CaptureRequest capture;capture.target=deviceSkinState->page(*slot/SlotsPerPage);
    capture.pixelRegion=SDL_Rect{int(tile%Columns*TileSize),int(tile/Columns*TileSize),TileSize,TileSize};
    capture.width=capture.height=TileSize;
    std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(presentationDevice->captureSynchronously(std::move(capture)),SDL_DestroySurface);
    if(!pixels) return false;
    std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> converted(SDL_ConvertSurface(pixels.get(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
    if(!converted) return false;
    rgba.resize(TileSize*TileSize*4);
    for(unsigned y=0;y<TileSize;++y) for(unsigned x=0;x<TileSize;++x) {
        const auto *in=static_cast<const Uint8*>(converted->pixels)+y*converted->pitch+x*4;
        auto *out=rgba.data()+(y*TileSize+x)*4;out[3]=in[3];
        for(unsigned c=0;c<3;++c) out[c]=in[3]?std::min(255u,(unsigned(in[c])*255+in[3]/2)/in[3]):0;
    }
    return true;
}
} // namespace GAGCore
