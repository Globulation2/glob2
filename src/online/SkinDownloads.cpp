// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkinDownloads.h"
#include "OnlineStorage.h"
#include "Sha256.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <nlohmann/json.hpp>
namespace Online {
namespace {
constexpr std::size_t limit=256*1024;
const std::string directory="online/skins/";
bool validTexture(const std::string &bytes,const std::string &hash)
{
    // Only canonical-size PNGs reach SDL_image. The decoder still validates CRCs
    // and complete chunk structure; this check bounds its allocation first.
    const unsigned char header[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,1,0,0,0,1,0};
    return bytes.size()>=33&&bytes.size()<=limit&&
        std::memcmp(bytes.data(),header,sizeof(header))==0&&Sha256::hex(bytes)==hash;
}
}
struct SkinDownloads::Impl {
    struct Entry {
        Ticket ticket;
        std::unique_ptr<SkinAuthorization> authorization;
        std::unique_ptr<HttpFetch::Fetch> download;
        bool finished=false;
    };
    OnlineStorage &storage;
    std::string origin,match,keys;
    FetchStarter fetch;
    std::unique_ptr<HttpFetch::Fetch> keyRequest;
    std::vector<Entry> entries;
    std::vector<Ready> ready;
    bool failed=false;
    bool refreshEnabled=true;
    std::int64_t nextRefresh=0;
    std::array<std::int64_t,32> installed{};
    std::vector<int> removed;
    std::unique_ptr<HttpFetch::Fetch> refreshRequest;
    std::unique_ptr<SkinDownloads> refreshDownloads;
    void remove(int team) {
        if(installed[team]) { removed.push_back(team); installed[team]=0; }
    }
    void refresh(std::int64_t now) {
        for(int team=0;team<32;++team)
            if(installed[team] && now>=installed[team])remove(team);
        if(!refreshEnabled)return;
        if(!nextRefresh)nextRefresh=now+60;
        // Finish the initial batch before starting another snapshot so older
        // downloads cannot reinstall a team removed by a newer response.
        if(keyRequest || !std::all_of(entries.begin(),entries.end(),[](const auto &e){return e.finished;}))return;
        if(refreshDownloads) {
            refreshDownloads->poll(now);
            for(auto &entry:refreshDownloads->takeReady())ready.push_back(std::move(entry));
            if(refreshDownloads->done())refreshDownloads.reset();
        }
        if(refreshRequest) {
            const auto state=refreshRequest->state();
            if(state==HttpFetch::State::Pending)return;
            if(state==HttpFetch::State::Done && refreshRequest->response().status==200 &&
               refreshRequest->response().body.size()<=512*1024) {
                try {
                    const auto response=nlohmann::json::parse(refreshRequest->response().body);
                    const auto &snapshot=response.at("colonySkins");
                    if(!snapshot.is_array() || snapshot.size()>32)throw std::runtime_error("invalid skin snapshot");
                    std::array<bool,32> present{};
                    std::vector<Ticket> tickets;
                    for(const auto &entry:snapshot) {
                        const auto &teamValue=entry.at("team");
                        if(!teamValue.is_number_integer() || teamValue<0 || teamValue>31)throw std::runtime_error("invalid team");
                        const int team=teamValue.get<int>();
                        const auto assertion=entry.at("assertion").get<std::string>();
                        if(present[team] || assertion.empty() || assertion.size()>8192)throw std::runtime_error("invalid ticket");
                        present[team]=true;
                        tickets.push_back({team,assertion});
                    }
                    // Absence is authoritative only in a complete, valid response
                    // from the configured HTTPS origin. Additions still need signatures.
                    for(int team=0;team<32;++team)if(!present[team])remove(team);
                    refreshDownloads=std::make_unique<SkinDownloads>(storage,origin,match,std::move(tickets),fetch,false);
                } catch(const std::exception &) {}
            }
            refreshRequest.reset();
            nextRefresh=now+60;
        }
        if(now>=nextRefresh && !refreshDownloads) {
            HttpFetch::Request request;
            request.url=origin+"/api/v1/matches/"+match+"/skins";
            request.responseLimit=512*1024;
            request.timeout=std::chrono::seconds(10);
            request.headers.emplace_back("Cache-Control","no-cache");
            refreshRequest=fetch(std::move(request));
            nextRefresh=now+60;
        }
    }
    Impl(OnlineStorage &s,std::string o,std::string m,FetchStarter f):storage(s),origin(std::move(o)),match(std::move(m)),fetch(std::move(f)){}
    void trim()
    {
        auto names=storage.list("online/skins");
        std::vector<std::string> files;
        for(const auto &name:names)if(name.size()==68&&name.substr(64)==".png"&&Sha256::isHexDigest(name.substr(0,64)))files.push_back(name);
        std::sort(files.begin(),files.end());
        std::set<std::string> retained;
        for(const auto &entry:entries)if(entry.authorization)
            if(const auto *skin=entry.authorization->skin())retained.insert(skin->textureHash+".png");
        std::size_t count=files.size();
        for(const auto &file:files)if(count>64&&!retained.count(file)){storage.remove(directory+file);--count;}
    }
};
SkinDownloads::SkinDownloads(OnlineStorage &storage,std::string origin,std::string match,std::vector<Ticket> tickets,FetchStarter fetch,bool refresh)
    :impl(std::make_unique<Impl>(storage,std::move(origin),std::move(match),std::move(fetch)))
{
    impl->refreshEnabled=refresh;
    if(tickets.empty())impl->nextRefresh=1;
    try {
        const auto parsed=HttpFetch::parseUrl(impl->origin);
        if(parsed.target!="/"||tickets.size()>32||impl->match.size()!=36||
           !std::all_of(impl->match.begin(),impl->match.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||c=='-';})){impl->failed=true;return;}
        while(!impl->origin.empty()&&impl->origin.back()=='/')impl->origin.pop_back();
        std::array<bool,32> used{};
        for(auto &ticket:tickets){
            if(ticket.team<0||ticket.team>=32||used[ticket.team]||ticket.assertion.size()>8192){impl->failed=true;return;}
            used[ticket.team]=true;
            impl->entries.push_back({std::move(ticket),nullptr,nullptr,false});
        }
        if(!tickets.empty()) {
            HttpFetch::Request request;request.url=impl->origin+"/.well-known/jwks.json";request.responseLimit=65536;
            request.timeout=std::chrono::seconds(10);
            impl->keyRequest=impl->fetch(std::move(request));
            if(!impl->keyRequest)impl->failed=true;
        }
    } catch(const std::exception &){impl->failed=true;}
}
SkinDownloads::~SkinDownloads()=default;
const std::string &SkinDownloads::origin()const{return impl->origin;}
const std::string &SkinDownloads::matchId()const{return impl->match;}
void SkinDownloads::poll(std::int64_t now)
{
    auto &p=*impl;
    if(p.failed || now<0 || now>INT64_MAX-60)return;
    p.refresh(now);
    if(p.keyRequest){
        const auto state=p.keyRequest->state();
        if(state==HttpFetch::State::Pending)return;
        if(state!=HttpFetch::State::Done||p.keyRequest->response().status!=200||p.keyRequest->response().body.size()>65536){for(auto &entry:p.entries)entry.finished=true;p.keyRequest.reset();return;}
        p.keys=p.keyRequest->response().body;p.keyRequest.reset();
        for(auto &entry:p.entries)entry.authorization=std::make_unique<SkinAuthorization>(entry.ticket.assertion,p.keys,p.origin,p.match,entry.ticket.team,now);
    }
    unsigned active=0;
    for(const auto &entry:p.entries)if(entry.download)++active;
    for(auto &entry:p.entries){
        if(entry.finished||!entry.authorization)continue;
        if(entry.authorization->state()==SkinAuthorization::State::Pending)continue;
        const auto *skin=entry.authorization->skin();
        if(!skin || now>=skin->expiresAt){if(entry.download)--active;entry.download.reset();entry.finished=true;continue;}
        const std::string path=directory+skin->textureHash+".png";
        if(entry.download){
            const auto state=entry.download->state();
            if(state==HttpFetch::State::Pending)continue;
            if(state==HttpFetch::State::Done&&entry.download->response().status==200&&validTexture(entry.download->response().body,skin->textureHash)&&p.storage.write(path,entry.download->response().body)){
                p.ready.push_back({*skin,p.storage.location(path)});p.trim();p.storage.persist();
            }
            entry.download.reset();--active;entry.finished=true;
        } else {
            std::string cached;
            if(p.storage.read(path,cached)){
                if(validTexture(cached,skin->textureHash)){p.ready.push_back({*skin,p.storage.location(path)});entry.finished=true;continue;}
                p.storage.remove(path);
            }
            if(active>=4)continue;
            HttpFetch::Request request;request.url=p.origin+"/api/v1/skins/versions/"+skin->versionId+"/texture";
            request.responseLimit=limit;request.timeout=std::chrono::seconds(15);
            entry.download=p.fetch(std::move(request));
            if(entry.download)++active;else entry.finished=true;
        }
    }
}
std::vector<SkinDownloads::Ready> SkinDownloads::takeReady(){
    auto ready=std::move(impl->ready);impl->ready.clear();
    for(const auto &entry:ready)impl->installed[entry.skin.team]=entry.skin.expiresAt;
    return ready;
}
std::vector<int> SkinDownloads::takeRemoved(){auto removed=std::move(impl->removed);impl->removed.clear();return removed;}
bool SkinDownloads::done()const{return impl->failed||std::all_of(impl->entries.begin(),impl->entries.end(),[](const auto &e){return e.finished;});}
}
