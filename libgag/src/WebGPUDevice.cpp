// SPDX-License-Identifier: GPL-3.0-or-later
#include <WebGPUDevice.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <stdexcept>
#include <tuple>
#ifdef __EMSCRIPTEN__
#include <RendererShaderLayouts.h>
extern "C" {
int glob2_webgpu_attach();
int glob2_webgpu_deferred();
int glob2_webgpu_discard();
int glob2_webgpu_upload(unsigned, int, int, const void *, int, int,int,int);
int glob2_webgpu_mip_upload(unsigned,int,int,int,const void*);
void glob2_webgpu_delete(unsigned);
void glob2_webgpu_trim();
int glob2_webgpu_mask_upload(unsigned,int,int,const void*,const void*);
void glob2_webgpu_mask_delete(unsigned);
int glob2_webgpu_geometry_upload(unsigned,const void *,unsigned,const void *,unsigned);
void glob2_webgpu_geometry_delete(unsigned);
unsigned glob2_webgpu_submit(const void *, unsigned, const void *, unsigned, const void *, unsigned, int, int);
unsigned glob2_webgpu_completed();
unsigned glob2_webgpu_issued();
unsigned glob2_webgpu_memory();
int glob2_webgpu_can_begin();
int glob2_webgpu_present();
void glob2_webgpu_reset();
int glob2_webgpu_capture(unsigned, int, int,unsigned,int,int);
int glob2_webgpu_capture_ready();
int glob2_webgpu_capture_poll(void *, int);
void glob2_webgpu_capture_cancel();
}
namespace GAGCore {
namespace {
// Versioned fixed-width transfer records. No pointers/browser objects escape
// this adapter. JS snapshots the complete record/geometry storage on submission.
struct PackedDraw {
    std::uint32_t kind=0, material=0, blend=0, depth=0, depthWrite=0, cull=0, texture=0, target=0;
    std::uint32_t vertexOffset=0, vertexBytes=0, indexOffset=0, indexCount=0, first=0, count=0, linear=0, premultiplied=0;
    std::int32_t viewport[4]{},clip[4]{};
    float logicalWidth=0,logicalHeight=0,alpha=1,textured=0,bounds[4]{0,0,1,1};
    float world[20]{},sun[4]{},clear[4]{},clearDepthValue=1;
    std::uint32_t clearColor=0,clearDepth=0,reserved=0;
};
static_assert(sizeof(PackedDraw)==256 && offsetof(PackedDraw,world)==128 && offsetof(PackedDraw,clear)==224);
class WebGPUDevice final:public RenderDevice {
    using Key=std::tuple<std::uint32_t,std::uint32_t,std::uint64_t,bool>;
    struct Allocation{unsigned identity=0;std::weak_ptr<const TextureRevision> snapshot;std::size_t bytes=0,colorBytes=0;std::uint64_t lastUse=0;bool target=false;std::size_t budgetClass=0;};
    struct Pending{RenderSubmission submission;RenderFrame frame;};
    SDL_Window *window;
    std::map<Key,Allocation> textures;
    struct Mesh{unsigned identity;std::weak_ptr<const GeometryRevision> snapshot;std::size_t bytes;std::uint64_t lastUse;};
    std::map<std::tuple<unsigned,unsigned,std::uint64_t>,Mesh> meshes;
    struct Mask{unsigned identity;std::weak_ptr<const CoverageData> snapshot;std::size_t bytes;std::uint64_t lastUse;};
    std::map<std::pair<std::uint64_t,std::uint64_t>,Mask> masks;
    unsigned nextMask=0;std::size_t maskBytes=0;
    unsigned nextMesh=0;std::size_t meshBytes=0;
    std::deque<Pending> pending;
    std::deque<CaptureRequest> captures;
    std::deque<std::chrono::steady_clock::time_point> captureAdmitted;
    unsigned nextTexture=0,maxTexture=0;
    std::uint64_t epoch=1,issued=0;
    std::size_t textureBytes=0;
    RenderTelemetry counts;
    static constexpr std::size_t TextureBudget=256*1024*1024;
    std::array<std::size_t,4> classBytes{};
    static constexpr std::array<std::size_t,4> ClassBudgets{64*1024*1024,64*1024*1024,128*1024*1024,32*1024*1024};
    [[noreturn]] void fail(){char message[2048]{};glob2_webgpu_error(message,sizeof message);if(glob2_webgpu_deferred())throw RenderAdmissionDeferred(message);throw std::runtime_error(message[0]?message:"WebGPU operation failed");}
    void poll(){const auto done=glob2_webgpu_completed();while(!pending.empty()&&pending.front().submission.identity<=done)pending.pop_front();
        for(auto it=textures.begin();it!=textures.end();){if(it->second.snapshot.expired()&&it->second.lastUse<=done){glob2_webgpu_delete(it->second.identity);textureBytes-=it->second.bytes;classBytes[it->second.budgetClass]-=it->second.colorBytes;it=textures.erase(it);}else ++it;}
        for(auto it=masks.begin();it!=masks.end();){if(it->second.snapshot.expired()&&it->second.lastUse<=done){glob2_webgpu_mask_delete(it->second.identity);maskBytes-=it->second.bytes;it=masks.erase(it);}else ++it;}
        for(auto it=meshes.begin();it!=meshes.end();){if(it->second.snapshot.expired()&&it->second.lastUse<=done){glob2_webgpu_geometry_delete(it->second.identity);meshBytes-=it->second.bytes;it=meshes.erase(it);}else ++it;}}
    Allocation &allocation(TextureSnapshot snapshot,bool premultiplied,bool target){
        if(!snapshot)throw std::invalid_argument("Empty WebGPU resource");
        const auto handle=snapshot->handle;Key key{handle.slot,handle.generation,snapshot->contentRevision,premultiplied};
        // Rendered targets keep their own pixels, even when subsequently sampled.
        for(auto &[existing,a]:textures)if(a.target&&std::get<0>(existing)==handle.slot&&std::get<1>(existing)==handle.generation&&std::get<2>(existing)==snapshot->contentRevision){a.lastUse=issued+1;return a;}
        auto found=textures.find(key);if(found!=textures.end()){
            auto &existing=found->second;
            if(target&&!existing.target){const auto extra=std::size_t(snapshot->descriptor.width)*snapshot->descriptor.height*4;if(extra>TextureBudget-textureBytes)throw RenderAdmissionDeferred("WebGPU target depth admission deferred");existing.target=true;existing.bytes+=extra;textureBytes+=extra;}
            existing.lastUse=issued+1;return existing;
        }
        const auto &d=snapshot->descriptor;std::size_t colorBytes=std::size_t(d.width)*d.height*4;for(const auto&level:snapshot->mips)colorBytes+=level.rgba.size();const auto bytes=colorBytes+(target?std::size_t(d.width)*d.height*4:0);const auto budgetClass=std::size_t(d.budgetClass);
        if(target&&!snapshot->mips.empty())throw std::invalid_argument("WebGPU rendered mip target unsupported");
        int mipWidth=d.width,mipHeight=d.height;std::size_t levels=0;while(mipWidth>1||mipHeight>1){mipWidth=std::max(1,mipWidth/2);mipHeight=std::max(1,mipHeight/2);if(d.mips==TextureDescriptor::Mips::Generated){if(levels>=snapshot->mips.size()||snapshot->mips[levels].width!=mipWidth||snapshot->mips[levels].height!=mipHeight||snapshot->mips[levels].rgba.size()!=std::size_t(mipWidth)*mipHeight*4)throw std::invalid_argument("WebGPU authored mip chain incomplete");++levels;}}
        if(levels!=snapshot->mips.size())throw std::invalid_argument("WebGPU unexpected mip levels");
        if(d.width<=0||d.height<=0||d.width>int(maxTexture)||d.height>int(maxTexture)||budgetClass>=classBytes.size()||colorBytes>ClassBudgets[budgetClass])throw std::length_error("WebGPU texture resource limit");
        // Completed immutable uploads remain reconstructible while their logical
        // snapshots are alive. Keep rendered pixels and this frame's leases.
        const auto done=glob2_webgpu_completed();
        for(auto it=textures.begin();it!=textures.end() &&
            (bytes>TextureBudget-textureBytes || colorBytes>ClassBudgets[budgetClass]-classBytes[budgetClass]);){
            const auto &candidate=it->second;
            if(!candidate.target && candidate.lastUse<=done &&
                (bytes>TextureBudget-textureBytes || candidate.budgetClass==budgetClass)){
                glob2_webgpu_delete(candidate.identity);textureBytes-=candidate.bytes;
                classBytes[candidate.budgetClass]-=candidate.colorBytes;it=textures.erase(it);
            }else ++it;
        }
        if(bytes>TextureBudget-textureBytes||colorBytes>ClassBudgets[budgetClass]-classBytes[budgetClass])throw RenderAdmissionDeferred("WebGPU texture admission deferred");
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(SDL_ConvertSurface(snapshot->pixels.get(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
        if(!pixels)throw std::runtime_error(SDL_GetError());
        std::vector<Uint8> packed(std::size_t(d.width)*d.height*4);
        for(int y=0;y<d.height;++y)std::memcpy(packed.data()+std::size_t(y)*d.width*4,static_cast<const Uint8 *>(pixels->pixels)+std::size_t(y)*pixels->pitch,d.width*4);
        if(premultiplied&&d.alpha!=TextureDescriptor::Alpha::Premultiplied)for(std::size_t i=0;i<packed.size();i+=4)for(int c=0;c<3;++c)packed[i+c]=Uint8((unsigned(packed[i+c])*packed[i+3]+127)/255);
        const unsigned id=++nextTexture;
        if(!glob2_webgpu_upload(id,d.width,d.height,packed.data(),d.addressU==TextureDescriptor::Address::Repeat,d.addressV==TextureDescriptor::Address::Repeat,target,1+snapshot->mips.size()))fail();
        for(std::size_t i=0;i<snapshot->mips.size();++i){const auto&level=snapshot->mips[i];auto rgba=level.rgba;if(premultiplied&&d.alpha!=TextureDescriptor::Alpha::Premultiplied)for(std::size_t n=0;n<rgba.size();n+=4)for(unsigned c=0;c<3;++c)rgba[n+c]=std::byte((unsigned(rgba[n+c])*unsigned(rgba[n+3])+127)/255);if(!glob2_webgpu_mip_upload(id,i+1,level.width,level.height,rgba.data())){glob2_webgpu_delete(id);fail();}counts.uploadBytes+=rgba.size();}
        counts.uploadBytes+=packed.size();textureBytes+=bytes;classBytes[budgetClass]+=colorBytes;
        auto &result=textures.emplace(key,Allocation{id,snapshot,bytes,colorBytes,issued+1,target,budgetClass}).first->second;return result;
    }
    Mesh &mesh(GeometrySnapshot snapshot,GeometryLayout layout){
        const unsigned stride=layout==GeometryLayout::Skin40?40:layout==GeometryLayout::SkinRig72?72:48;
        if(!snapshot||snapshot->layout!=layout||snapshot->stride!=stride||snapshot->vertices.size()%stride)throw std::invalid_argument("WebGPU mesh layout");
        const auto handle=snapshot->handle;const auto key=std::tuple{handle.slot,handle.generation,snapshot->contentRevision};auto found=meshes.find(key);
        if(found!=meshes.end()){found->second.lastUse=issued+1;return found->second;}
        const auto bytes=snapshot->vertices.size()+snapshot->indices.size()*4;if(bytes>32*1024*1024)throw std::length_error("WebGPU geometry resource limit");if(bytes>32*1024*1024-meshBytes)throw RenderAdmissionDeferred("WebGPU geometry admission deferred");
        for(auto index:snapshot->indices)if(std::size_t(index)*stride>=snapshot->vertices.size())throw std::out_of_range("WebGPU mesh index");
        const unsigned identity=++nextMesh;if(!glob2_webgpu_geometry_upload(identity,snapshot->vertices.data(),snapshot->vertices.size(),snapshot->indices.data(),snapshot->indices.size()*4))fail();
        meshBytes+=bytes;counts.uploadBytes+=bytes;return meshes.emplace(key,Mesh{identity,snapshot,bytes,issued+1}).first->second;
    }
    Mask&mask(std::shared_ptr<const CoverageData> snapshot){
        if(!snapshot||!snapshot->width||!snapshot->height||snapshot->width>maxTexture||snapshot->height>maxTexture||snapshot->samples.size()!=std::size_t(snapshot->width)*snapshot->height)throw std::invalid_argument("WebGPU coverage dimensions");
        const auto key=std::pair{snapshot->identity,snapshot->revision};if(auto it=masks.find(key);it!=masks.end()){it->second.lastUse=issued+1;return it->second;}
        const auto bytes=snapshot->samples.size()*9;if(bytes>32*1024*1024)throw std::length_error("WebGPU coverage limit");if(bytes+maskBytes+classBytes[3]>32*1024*1024){const auto done=glob2_webgpu_completed();for(auto it=masks.begin();it!=masks.end();){if(it->second.lastUse<=done){glob2_webgpu_mask_delete(it->second.identity);maskBytes-=it->second.bytes;it=masks.erase(it);}else ++it;}}if(bytes+maskBytes+classBytes[3]>32*1024*1024)throw RenderAdmissionDeferred("WebGPU coverage admission deferred");
        std::vector<std::uint16_t> weights;std::vector<std::uint8_t> selectors;weights.reserve(snapshot->samples.size()*4);selectors.reserve(snapshot->samples.size());
        for(const auto&sample:snapshot->samples){if(unsigned(sample.others[0])+sample.others[1]+sample.others[2]>65536)throw std::invalid_argument("WebGPU coverage weights");weights.insert(weights.end(),sample.others.begin(),sample.others.end());weights.push_back(sample.margin);selectors.push_back(sample.slots);}
        const unsigned id=++nextMask;if(!glob2_webgpu_mask_upload(id,snapshot->width,snapshot->height,weights.data(),selectors.data()))fail();maskBytes+=bytes;counts.uploadBytes+=bytes;return masks.emplace(key,Mask{id,snapshot,bytes,issued+1}).first->second;
    }
    static SDL_Rect clipped(const DrawPacket &p,int w,int h,float logicalW,float logicalH){
        SDL_Rect result{0,0,w,h};const float sx=w/logicalW,sy=h/logicalH;
        const auto intersect=[&](float x,float y,float rw,float rh){const int left=std::floor(x*sx),top=std::floor(y*sy);SDL_Rect rect{left,top,int(std::ceil((x+rw)*sx))-left,int(std::ceil((y+rh)*sy))-top};if(!SDL_GetRectIntersection(&result,&rect,&result))result={0,0,0,0};};
        if(p.transform.bounds){auto r=*p.transform.bounds;intersect(r.x,r.y,r.w,r.h);}
        if(p.clip){auto r=*p.clip;const auto&t=p.transform;intersect(r.x*t.scale+t.x,r.y*t.scale+t.y,r.w*t.scale,r.h*t.scale);}return detail::intersectTargetScissor(result,p.targetScissor);
    }
    static void appendVertices(std::vector<SDL_Vertex> &v,const DrawPacket &p,bool premultiplied){
        const auto begin=v.size();if(p.kind==DrawPacket::Kind::Fill||p.kind==DrawPacket::Kind::Blit){
            const auto&r=p.destination;SDL_FColor color{p.color.r/255.f,p.color.g/255.f,p.color.b/255.f,p.color.a/255.f};float u0=0,v0=0,u1=0,v1=0;
            if(p.texture){const auto&d=p.texture->descriptor;u0=float(p.source.x)/d.width;v0=float(p.source.y)/d.height;u1=float(p.source.x+p.source.w)/d.width;v1=float(p.source.y+p.source.h)/d.height;}
            const SDL_Vertex a{{r.x,r.y},color,{u0,v0}},b{{r.x+r.w,r.y},color,{u1,v0}},c{{r.x+r.w,r.y+r.h},color,{u1,v1}},d{{r.x,r.y+r.h},color,{u0,v1}};v.insert(v.end(),{a,b,c,a,c,d});
        }else v.insert(v.end(),p.vertices.begin(),p.vertices.end());
        for(std::size_t i=begin;i<v.size();++i){auto&vertex=v[i];if(p.kind!=DrawPacket::Kind::ScreenTriangles){vertex.position.x=vertex.position.x*p.transform.scale+p.transform.x;vertex.position.y=vertex.position.y*p.transform.scale+p.transform.y;}if(premultiplied){const float a=vertex.color.a*p.alpha/255.f;vertex.color.r*=a;vertex.color.g*=a;vertex.color.b*=a;}}
    }
  public:
    explicit WebGPUDevice(SDL_Window *borrowed):window(borrowed){maxTexture=glob2_webgpu_attach();if(!maxTexture)fail();issued=glob2_webgpu_issued();}
    ~WebGPUDevice() override{glob2_webgpu_reset();}
    RenderDeviceCapabilities capabilities() const override{return {true,true,false,int(maxTexture),true,true,true,true};}
    bool canBeginFrame() override{poll();const int ready=glob2_webgpu_can_begin();if(ready<0)fail();return ready>0;}
    RenderSubmission submit(RenderFrame frame) override{
        detail::RenderCpuTimer timing(counts.cpuSubmitNanoseconds);
        if(!canBeginFrame())return {};
        if(frame.logicalWidth<=0||frame.logicalHeight<=0||frame.packets.size()>RenderFrame::CommandCapacity)throw std::invalid_argument("Invalid WebGPU frame");
        int width,height;outputSize(width,height);std::vector<PackedDraw> records;std::vector<std::byte> vertices;std::vector<std::byte> uniforms;
        for(std::size_t begin=0;begin<frame.packets.size();){const auto&p=frame.packets[begin];PackedDraw draw;
            draw.material=unsigned(p.material);draw.depth=unsigned(p.depth);draw.depthWrite=p.depthWrite;draw.cull=unsigned(p.cull);draw.linear=p.linear;draw.alpha=p.alpha/255.f;
            int tw=width,th=height;if(p.target){draw.target=allocation(p.target,false,true).identity;tw=p.target->descriptor.width;th=p.target->descriptor.height;}
            bool premultiplied=false;if(p.texture){auto&a=allocation(p.texture,(p.material==DrawPacket::Material::Primitive&&p.linear)||p.texture->descriptor.alpha==TextureDescriptor::Alpha::Premultiplied,false);draw.texture=a.identity;premultiplied=a.target ? p.texture->descriptor.alpha==TextureDescriptor::Alpha::Premultiplied :
                (p.material==DrawPacket::Material::Primitive&&p.linear)||p.texture->descriptor.alpha==TextureDescriptor::Alpha::Premultiplied;draw.textured=1;}
            const bool skin=p.material==DrawPacket::Material::Skin||p.material==DrawPacket::Material::SkinRig;
            if(skin){if(!p.skin||!p.texture||!p.secondTexture)throw std::invalid_argument("WebGPU skin resources missing");draw.premultiplied=allocation(p.secondTexture,false,false).identity;}else draw.premultiplied=premultiplied;draw.blend=p.blend==DrawPacket::Blend::Opaque?3:p.blend==DrawPacket::Blend::Additive?2:(p.blend==DrawPacket::Blend::Premultiplied||(p.blend==DrawPacket::Blend::Automatic&&premultiplied))?1:0;
            draw.logicalWidth=p.targetLogicalWidth>0?p.targetLogicalWidth:float(frame.logicalWidth);draw.logicalHeight=p.targetLogicalHeight>0?p.targetLogicalHeight:float(frame.logicalHeight);
            const auto viewport=p.viewport.value_or(SDL_Rect{0,0,tw,th}),clip=clipped(p,tw,th,draw.logicalWidth,draw.logicalHeight);std::memcpy(draw.viewport,&viewport,sizeof viewport);std::memcpy(draw.clip,&clip,sizeof clip);
            if(viewport.x<0||viewport.y<0||viewport.w<0||viewport.h<0||viewport.x+viewport.w>tw||viewport.y+viewport.h>th)throw std::invalid_argument("WebGPU viewport bounds");
            if(p.texture&&p.target&&p.texture->handle==p.target->handle)throw std::invalid_argument("WebGPU target samples itself");
            if(p.kind==DrawPacket::Kind::Blit&&!p.texture)throw std::invalid_argument("WebGPU blit without texture");
            if(!std::isfinite(p.transform.scale)||p.transform.scale<=0||!std::isfinite(p.transform.x)||!std::isfinite(p.transform.y))throw std::invalid_argument("Invalid WebGPU transform");
            std::size_t end=begin+1;draw.vertexOffset=vertices.size();
            if(p.kind==DrawPacket::Kind::Barrier)draw.kind=3;
            else if(p.kind==DrawPacket::Kind::WeightedCompose){
                if(p.targetScissor)throw std::invalid_argument("Terrain composition cannot inherit target scissor");
                if(!p.target||!p.weightedCompose)throw std::invalid_argument("WebGPU terrain target missing");const auto&compose=*p.weightedCompose;
                const auto size=compose.compositionSize,divisor=compose.reductionDivisor;const auto&r=compose.targetRect;
                if(!size||size%32||size>maxTexture||!divisor||divisor>32||(divisor&(divisor-1))||size%divisor||compose.paletteCount>4||!compose.paletteCount||r.x<0||r.y<0||r.w!=int(size/divisor)||r.h!=int(size/divisor)||r.x+r.w>tw||r.y+r.h>th)throw std::invalid_argument("WebGPU terrain parameters");
                if(compose.paletteCount>1&&!compose.coverage)throw std::invalid_argument("WebGPU terrain coverage missing");
                if(p.target->descriptor.alpha==TextureDescriptor::Alpha::Opaque)throw std::invalid_argument("WebGPU terrain explicit alpha required");
                if(compose.coverage)for(const auto&sample:compose.coverage->samples)if(sample.slots>=64||(sample.slots&3)>=compose.paletteCount||((sample.slots>>2)&3)>=compose.paletteCount||((sample.slots>>4)&3)>=compose.paletteCount)throw std::invalid_argument("WebGPU terrain selector");
                if(compose.coverage&&(compose.coverage->width!=size||compose.coverage->height!=size))throw std::invalid_argument("WebGPU coverage size mismatch");
                draw.kind=4;std::memcpy(draw.viewport,&r,sizeof r);std::array<std::uint32_t,36> params{};params[0]=size;params[1]=compose.paletteCount;params[3]=compose.coverage?1:0;
                const bool pma=p.target->descriptor.alpha==TextureDescriptor::Alpha::Premultiplied;if(divisor==1&&pma)params[3]|=2;
                for(unsigned i=0;i<4;++i){const auto&source=compose.materials[i];if(source&&i<compose.paletteCount){if(source->descriptor.alpha==TextureDescriptor::Alpha::Premultiplied)throw std::invalid_argument("WebGPU terrain straight source required");if(source->handle==p.target->handle)throw std::invalid_argument("WebGPU terrain target alias");auto&a=allocation(source,false,false);if(a.target)throw std::invalid_argument("WebGPU terrain immutable source required");const auto id=a.identity;std::memcpy(draw.world+i,&id,4);params[2]|=1u<<i;}
                    const auto&seam=compose.seams[i];const std::int32_t geometry[]{seam.height,seam.cast,seam.castWidth,seam.fringe},fringe[]{seam.fringeWidth,seam.fringeColor[0],seam.fringeColor[1],seam.fringeColor[2]};std::memcpy(params.data()+4+i*4,geometry,16);std::memcpy(params.data()+20+i*4,fringe,16);
                }
                const std::uint32_t fields[]{compose.coverage?mask(compose.coverage).identity:0,size,divisor,unsigned(pma)};std::memcpy(draw.world+4,fields,sizeof fields);
                const std::uint32_t offset=uniforms.size();std::memcpy(draw.bounds,&offset,4);const auto*data=reinterpret_cast<const std::byte*>(params.data());uniforms.insert(uniforms.end(),data,data+sizeof params);
            }
            else if(p.kind==DrawPacket::Kind::Clear){draw.kind=2;draw.clearColor=p.clearColor;draw.clearDepth=p.clearDepth;draw.clearDepthValue=p.clearDepthValue;std::memcpy(draw.clear,&p.clearValue,sizeof p.clearValue);}
            else if(p.kind==DrawPacket::Kind::MeshTriangles){
                draw.kind=1;const bool rig=p.material==DrawPacket::Material::SkinRig;const auto layout=skin?(rig?GeometryLayout::SkinRig72:GeometryLayout::Skin40):GeometryLayout::World48;draw.reserved=mesh(p.geometry,layout).identity;draw.vertexOffset=0;draw.vertexBytes=p.geometry->vertices.size();draw.first=p.first;draw.count=p.count;
                std::copy(p.world.projection.begin(),p.world.projection.end(),draw.world);std::copy(p.world.uvTransform.begin(),p.world.uvTransform.end(),draw.world+16);std::copy(p.world.sunSpecular.begin(),p.world.sunSpecular.end(),draw.sun);
                if(skin){
                    namespace L=RendererShaderLayouts;const auto&material=*p.skin;
                    for(float value:{material.shell,material.furLength,material.shellDepth,material.region[0],material.region[1]})if(!std::isfinite(value))throw std::invalid_argument("Non-finite WebGPU skin uniform");
                    // region is an atlas quadrant offset; zero is authoritative.
                    std::vector<float> vertexUniform((rig?L::skin_rig_size:L::skin_size)/4);
                    if(rig){if(!material.pose)throw std::invalid_argument("WebGPU skin rig pose missing");const auto&pose=*material.pose;
                        for(const auto values:{std::span<const float>(pose.bones),std::span<const float>(pose.view),std::span<const float>(pose.normalView)})for(float value:values)if(!std::isfinite(value))throw std::invalid_argument("Non-finite WebGPU rig pose");
                        std::copy(pose.bones.begin(),pose.bones.end(),vertexUniform.begin()+L::skin_rig_bones_offset/4);std::copy(pose.view.begin(),pose.view.end(),vertexUniform.begin()+L::skin_rig_view_offset/4);
                        for(int column=0;column<3;++column)std::copy_n(pose.normalView.begin()+column*3,3,vertexUniform.begin()+L::skin_rig_normalView_offset/4+column*4);
                        vertexUniform[L::skin_rig_shell_offset/4]=material.shell;vertexUniform[L::skin_rig_furLength_offset/4]=material.furLength;vertexUniform[L::skin_rig_shellDepth_offset/4]=material.shellDepth;
                    }else{vertexUniform[L::skin_shell_offset/4]=material.shell;vertexUniform[L::skin_furLength_offset/4]=material.furLength;vertexUniform[L::skin_shellDepth_offset/4]=material.shellDepth;}
                    std::array<float,L::skin_fragment_size/4> fragmentUniform{};fragmentUniform[L::skin_fragment_region_offset/4]=material.region[0];fragmentUniform[L::skin_fragment_region_offset/4+1]=material.region[1];fragmentUniform[L::skin_fragment_shell_offset/4]=material.shell;
                    const std::uint32_t offsets[4]{std::uint32_t(uniforms.size()),std::uint32_t(vertexUniform.size()*4),std::uint32_t(uniforms.size()+vertexUniform.size()*4),L::skin_fragment_size};std::memcpy(draw.bounds,offsets,sizeof offsets);
                    for(const auto values:{std::span<const float>(vertexUniform),std::span<const float>(fragmentUniform)}){const auto*bytes=reinterpret_cast<const std::byte*>(values.data());uniforms.insert(uniforms.end(),bytes,bytes+values.size_bytes());}
                }
                if(p.indexed){if(std::size_t(p.first)+p.count>p.geometry->indices.size())throw std::out_of_range("WebGPU mesh index range");draw.indexOffset=p.first*4;draw.indexCount=p.count;draw.first=0;}
                else if((std::size_t(p.first)+p.count)*p.geometry->stride>p.geometry->vertices.size())throw std::out_of_range("WebGPU mesh vertex range");
            }else{
                end=frame.batchEnd(begin);std::vector<SDL_Vertex> geometry;
                for(auto i=begin;i<end;++i){if(frame.packets[i].vertices.size()%3)throw std::invalid_argument("Incomplete WebGPU triangles");appendVertices(geometry,frame.packets[i],premultiplied);}
                if(geometry.size()>RenderFrame::VertexCapacity)throw std::length_error("WebGPU vertex capacity");draw.count=geometry.size();draw.vertexBytes=geometry.size()*sizeof(SDL_Vertex);
                const auto*bytes=reinterpret_cast<const std::byte *>(geometry.data());vertices.insert(vertices.end(),bytes,bytes+draw.vertexBytes);
                if(p.linear&&p.kind==DrawPacket::Kind::Blit){const auto&d=p.texture->descriptor;draw.bounds[0]=(p.source.x+.5f)/d.width;draw.bounds[1]=(p.source.y+.5f)/d.height;draw.bounds[2]=(p.source.x+p.source.w-.5f)/d.width;draw.bounds[3]=(p.source.y+p.source.h-.5f)/d.height;}
            }
            if(vertices.size()>32*1024*1024||uniforms.size()>8*1024*1024)throw std::length_error("WebGPU frame geometry budget");
            records.push_back(draw);counts.packets+=end-begin;counts.batches+=draw.kind<2;begin=end;
        }
        const auto identity=glob2_webgpu_submit(records.data(),records.size(),vertices.data(),vertices.size(),uniforms.data(),uniforms.size(),width,height);if(!identity)fail();issued=identity;RenderSubmission submission{identity,epoch};pending.push_back({submission,std::move(frame)});return submission;
    }
    void trimRetiredResources() override {
        poll(); const auto done=glob2_webgpu_completed();
        for(auto it=textures.begin();it!=textures.end();) {
            const auto &allocation=it->second;
            if(!allocation.target && allocation.lastUse<=done) {
                glob2_webgpu_delete(allocation.identity);textureBytes-=allocation.bytes;
                classBytes[allocation.budgetClass]-=allocation.colorBytes;it=textures.erase(it);
            }else ++it;
        }
        for(auto it=meshes.begin();it!=meshes.end();) {
            if(it->second.lastUse<=done){glob2_webgpu_geometry_delete(it->second.identity);meshBytes-=it->second.bytes;it=meshes.erase(it);}else ++it;
        }
        for(auto it=masks.begin();it!=masks.end();) {
            if(it->second.lastUse<=done){glob2_webgpu_mask_delete(it->second.identity);maskBytes-=it->second.bytes;it=masks.erase(it);}else ++it;
        }
        glob2_webgpu_trim();
    }
    RenderCompletion completion(RenderSubmission submission) override{poll();if(!submission||submission.deviceGeneration!=epoch||submission.identity>issued)return RenderCompletion::Cancelled;return submission.identity<=glob2_webgpu_completed()?RenderCompletion::Complete:RenderCompletion::Pending;}
    void present() override{detail::RenderCpuTimer timing(counts.cpuPresentNanoseconds);if(!glob2_webgpu_present())fail();}
    void discardFrame() override{const auto identity=glob2_webgpu_discard();if(!identity)fail();issued=identity;}
    void reset() override{cancelCaptures();glob2_webgpu_reset();textures.clear();meshes.clear();masks.clear();pending.clear();textureBytes=meshBytes=maskBytes=0;classBytes.fill(0);++epoch;maxTexture=glob2_webgpu_attach();if(!maxTexture)fail();issued=glob2_webgpu_issued();}
    std::uint64_t generation() const override{return epoch;}
    RenderTelemetry telemetry() const override{auto result=counts;result.resourceBytes=glob2_webgpu_memory();return result;}
    bool capture(CaptureRequest request) override{
        detail::RenderCpuTimer timing(counts.cpuCaptureNanoseconds);
        unsigned target=0;int width,height;outputSize(width,height);if(request.target){auto&a=allocation(request.target,false,true);target=a.identity;width=request.target->descriptor.width;height=request.target->descriptor.height;}
        const auto region=request.pixelRegion.value_or(SDL_Rect{0,0,width,height});
        if(region.x<0||region.y<0||region.w<=0||region.h<=0||region.x+region.w>width||region.y+region.h>height||region.w!=request.width||region.h!=request.height)throw std::invalid_argument("WebGPU capture region");
        if(captures.size()>=3||!glob2_webgpu_capture(unsigned(request.identity),request.width,request.height,target,region.x,region.y)){++counts.captureDrops;return false;}captures.push_back(std::move(request));captureAdmitted.push_back(std::chrono::steady_clock::now());return true;
    }
    bool pollCapture(CaptureResult&result) override{detail::RenderCpuTimer timing(counts.cpuCaptureNanoseconds);if(captures.empty()||!glob2_webgpu_capture_ready())return false;const auto&r=captures.front();std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(r.width,r.height,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);if(!surface)throw std::runtime_error(SDL_GetError());const int ready=glob2_webgpu_capture_poll(surface->pixels,surface->pitch);if(!ready)return false;result.request=std::move(captures.front());captures.pop_front();result.cancelled=ready<0;if(ready>0){counts.captureLatencyNanoseconds+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-captureAdmitted.front()).count();++counts.captureCompletions;}captureAdmitted.pop_front();result.pixels=ready>0?std::move(surface):decltype(surface)(nullptr,SDL_DestroySurface);return true;}
    void cancelCaptures() override{counts.captureDrops+=captures.size();captures.clear();captureAdmitted.clear();glob2_webgpu_capture_cancel();}
    void outputSize(int&w,int&h) override{if(!SDL_GetWindowSizeInPixels(window,&w,&h))throw std::runtime_error(SDL_GetError());}
};
}
std::unique_ptr<RenderDevice> makeWebGPURenderDevice(SDL_Window*window){return std::make_unique<WebGPUDevice>(window);}
}
#else
namespace GAGCore {std::unique_ptr<RenderDevice> makeWebGPURenderDevice(SDL_Window*){throw std::runtime_error("WebGPU requires browser build");}}
extern "C" int glob2_webgpu_prepare(const char*){return -1;}
extern "C" int glob2_webgpu_poll(){return -1;}
extern "C" void glob2_webgpu_error(char*destination,int capacity){if(capacity>0){const char*text="WebGPU requires browser build";std::strncpy(destination,text,capacity-1);destination[capacity-1]=0;}}
#endif
