// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkinSprites.h"
#include "OnlineStorage.h"
#include "Sha256.h"
#include <GraphicContext.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
namespace Online {
namespace {
const std::string directory="online/skin-sprites/";
constexpr std::size_t MemoryLimit=64*1024*1024,DiskLimit=256*1024*1024;
}
SkinSprites::SkinSprites(OnlineStorage &s,std::string o,SkinDownloads::FetchStarter f):storage(s),origin(std::move(o)),start(std::move(f)) {
    for (const auto &name:storage.list("online/skin-sprites")) {
        if (name.size()!=69 || name.substr(64)!=".webp" || !Sha256::isHexDigest(name.substr(0,64))) continue;
        const auto size=storage.size(directory+name);
        if (size<=2*1024*1024) { disk[directory+name]={size,0}; diskBytes+=size; }
        else storage.remove(directory+name);
    }
    while (diskBytes>DiskLimit && !disk.empty()) { auto it=disk.begin(); diskBytes-=it->second.bytes;storage.remove(it->first);disk.erase(it); }
}
SkinSprites::~SkinSprites()=default;
void SkinSprites::install(const AuthorizedSkin &skin) {
    if (skin.team<0 || skin.team>=32) return;
    auto &t=teams[skin.team];
    if (t.skin.manifestHash==skin.manifestHash && t.skin.spriteManifestHash==skin.spriteManifestHash) {t.skin=skin;if(t.manifest.pages.empty())t.tried=false;for(const auto &page:t.manifest.pages)if(auto it=pages.find(page.hash);it!=pages.end())it->second.failures=0;return;}
    remove(skin.team); t.skin=skin;
}
void SkinSprites::remove(int team) {
    if (team<0 || team>=32) return;
    auto &t=teams[team]; if (t.fetch)t.fetch->cancel(); t=Team{};
    // Pages belong to hashes rather than teams. Retain only pages still authorized.
    for (auto it=pages.begin();it!=pages.end();) {
        const bool retained=std::any_of(teams.begin(),teams.end(),[&](const auto &entry){return std::any_of(entry.manifest.pages.begin(),entry.manifest.pages.end(),[&](const auto &p){return p.hash==it->first;});});
        if (retained) {++it;continue;}
        if (it->second.surface) decoded-=it->second.cost;
        if (it->second.fetch) it->second.fetch->cancel();
        it=pages.erase(it);
    }
}
void SkinSprites::cache(const std::string &path,const std::string &bytes) {
    if (!storage.write(path,bytes))return;
    if (const auto found=disk.find(path);found!=disk.end())diskBytes-=found->second.bytes;
    disk[path]={bytes.size(),clock};diskBytes+=bytes.size();
    while(diskBytes>DiskLimit && !disk.empty()) {
        auto it=std::min_element(disk.begin(),disk.end(),[](const auto &a,const auto &b){return a.second.touched<b.second.touched;});
        if(it->first==path && disk.size()==1)break;
        diskBytes-=it->second.bytes;storage.remove(it->first);disk.erase(it);
    }
    storage.persist();
}
void SkinSprites::poll() {
    ++clock;
    unsigned active=0;
    for(const auto &t:teams)if(t.fetch)++active;
    for(const auto &[hash,p]:pages)if(p.fetch)++active;
    for(auto &t:teams) {
        if(t.skin.spriteManifestHash.empty() || !t.manifest.pages.empty() || t.tried)continue;
        if(t.fetch) {
            if(t.fetch->state()==HttpFetch::State::Pending)continue;
            if(t.fetch->state()==HttpFetch::State::Done && t.fetch->response().status==200)t.manifest.parse(t.fetch->response().body,t.skin);
            t.fetch.reset();--active;t.tried=true;
        } else if(active<4) {
            HttpFetch::Request request;
            request.url=origin+"/api/v1/skins/versions/"+t.skin.versionId+"/sprites/"+t.skin.spriteManifestHash+"/manifest";
            request.responseLimit=65536;request.timeout=std::chrono::seconds(15);
            t.fetch=start(std::move(request));if(t.fetch)++active;else t.tried=true;
        }
    }
    bool decodedOne=false;
    // Prioritize pages requested by the most recent scene to prevent cache churn
    // from drawing causing repeated decode/eviction inside the draw loop.
    std::vector<Page*> requested;
    for(auto &[hash,p]:pages)if(p.requested && !p.surface && p.failures<3)requested.push_back(&p);
    std::sort(requested.begin(),requested.end(),[](auto *a,auto *b){return a->touched>b->touched;});
    for(auto *ptr:requested) {
        auto &p=*ptr;std::string bytes;
        const auto path=directory+p.info.hash+".webp";
        if(p.fetch) {
            if(p.fetch->state()==HttpFetch::State::Pending)continue;
            if(decodedOne)continue;
            if(p.fetch->state()==HttpFetch::State::Done && p.fetch->response().status==200)bytes=p.fetch->response().body;
            p.fetch.reset();--active;
            if(!validSkinSpritePage(bytes,p.info)){++p.failures;continue;}
            cache(path,bytes);
        } else {
            if(decodedOne)continue;
            if(storage.size(path)==p.info.bytes && storage.read(path,bytes) && validSkinSpritePage(bytes,p.info)) {if(auto entry=disk.find(path);entry!=disk.end())entry->second.touched=clock;}
            else {
                storage.remove(path);
                if(active<4) {
                    HttpFetch::Request request;
                    request.url=origin+"/api/v1/skins/versions/"+p.version+"/sprites/"+p.bundle+"/pages/"+p.info.hash;
                    request.responseLimit=p.info.bytes;request.timeout=std::chrono::seconds(15);
                    p.fetch=start(std::move(request));if(p.fetch)++active;else ++p.failures;
                }
                continue;
            }
        }
        decodedOne=true;
        auto *loaded=IMG_Load_IO(SDL_IOFromConstMem(bytes.data(),bytes.size()),true);
        if(!loaded || loaded->w!=int(p.info.size) || loaded->h!=int(p.info.size)) {if(loaded)SDL_DestroySurface(loaded);++p.failures;continue;}
        auto *rgba=SDL_ConvertSurface(loaded,SDL_PIXELFORMAT_ARGB8888);SDL_DestroySurface(loaded);
        if(!rgba){++p.failures;continue;}
        // Keep full-resolution pixels, but discard unused transparent tile
        // margins. A one-pixel transparent guard preserves linear filtering.
        p.cellW=p.cellH=1;
        const unsigned columns=p.info.size==128?1:8,rows=p.info.size==128?1:8;
        for(unsigned frame=0;frame<p.info.frames;++frame) {
            unsigned left=128,top=128,right=0,bottom=0;
            for(unsigned y=0;y<128;++y) {
                const auto *pixels=reinterpret_cast<const uint32_t*>(static_cast<const char*>(rgba->pixels)+((frame/columns)*128+y)*rgba->pitch)+(frame%columns)*128;
                for(unsigned x=0;x<128;++x)if(pixels[x]>>24) {
                    left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);
                }
            }
            auto &bounds=p.frames[frame];bounds=Frame{};
            if(left<right) {
                bounds.x=left?left-1:0;bounds.y=top?top-1:0;
                bounds.w=std::min(128u,right+1)-bounds.x;bounds.h=std::min(128u,bottom+1)-bounds.y;
            }
            p.cellW=std::max(p.cellW,bounds.w);p.cellH=std::max(p.cellH,bounds.h);
        }
        const std::size_t cost=std::size_t(p.cellW)*columns*p.cellH*rows*4;
        while(decoded+cost>MemoryLimit) {
            auto victim=pages.end();
            for(auto it=pages.begin();it!=pages.end();++it)if(it->second.surface && (victim==pages.end() || it->second.touched<victim->second.touched))victim=it;
            if(victim==pages.end())break;
            decoded-=victim->second.cost;victim->second.surface.reset();++metrics.evictions;
        }
        p.surface=std::make_unique<GAGCore::DrawableSurface>(p.cellW*columns,p.cellH*rows);
        SDL_FillSurfaceRect(p.surface->getSDLSurface(),nullptr,0);
        SDL_SetSurfaceBlendMode(rgba,SDL_BLENDMODE_NONE);
        for(unsigned frame=0;frame<p.info.frames;++frame) {
            const auto &bounds=p.frames[frame];
            SDL_Rect source{int((frame%columns)*128+bounds.x),int((frame/columns)*128+bounds.y),int(bounds.w),int(bounds.h)};
            SDL_Rect destination{int((frame%columns)*p.cellW),int((frame/columns)*p.cellH),int(bounds.w),int(bounds.h)};
            SDL_BlitSurface(rgba,&source,p.surface->getSDLSurface(),&destination);
        }
        SDL_DestroySurface(rgba);
        p.surface->markPixelsChanged();p.cost=cost;decoded+=cost;++metrics.decodes;
    }
    for(auto &[hash,p]:pages)p.requested=false;
}
bool SkinSprites::draw(GAGCore::GraphicContext &gfx,int team,unsigned clip,unsigned frame,float x,float y,float w,float h,GAGCore::DrawableSurface *shadow,unsigned char alpha) {
    if(team<0 || team>=32 || clip>=8)return false;
    const auto &t=teams[team];
    const auto found=std::find_if(t.manifest.pages.begin(),t.manifest.pages.end(),[&](const auto &p){return p.clip==clip && frame>=p.first && frame<p.first+p.frames;});
    if(found==t.manifest.pages.end())return false;
    auto &p=pages[found->hash];p.info=*found;p.version=t.skin.versionId;p.bundle=t.skin.spriteManifestHash;p.touched=clock;p.requested=true;
    if(!p.surface){++metrics.misses;return false;}
    ++metrics.hits;
    if(!alpha)return true;
    if(shadow)gfx.drawSurface(x,y,w,h,shadow,alpha);
    x-=w*0.125f;y-=h*0.125f;w*=1.25f;h*=1.25f;
    const unsigned cell=frame-found->first;
    const auto &bounds=p.frames[cell];
    x+=w*bounds.x/128.f;y+=h*bounds.y/128.f;w*=bounds.w/128.f;h*=bounds.h/128.f;
    // SDL's linear scaler restores source modulation and clips after scaling.
    gfx.drawSkinSprite(x,y,w,h,p.surface.get(),(cell%8)*p.cellW,(cell/8)*p.cellH,bounds.w,bounds.h,alpha);
    return true;
}
}
