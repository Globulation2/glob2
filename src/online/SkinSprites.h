// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SkinDownloads.h"
#include "SkinSpriteManifest.h"
#include <array>
#include <map>
namespace GAGCore { class GraphicContext; class DrawableSurface; }
namespace Online {
// View-owned demand loader: download compressed pages, decode at most one per poll.
class SkinSprites {
public:
    SkinSprites(OnlineStorage &, std::string origin, SkinDownloads::FetchStarter);
    ~SkinSprites();
    void install(const AuthorizedSkin &);
    void remove(int team);
    void poll();
    bool draw(GAGCore::GraphicContext &, int team, unsigned clip, unsigned frame,
              float x,float y,float w,float h, GAGCore::DrawableSurface *shadow=nullptr, unsigned char alpha=255);
    std::size_t decodedBytes() const { return decoded; }
    struct Counters { std::uint64_t decodes=0, evictions=0, hits=0, misses=0; };
    const Counters &counters() const { return metrics; }
private:
    struct Team { AuthorizedSkin skin; SkinSpriteManifest manifest; std::unique_ptr<HttpFetch::Fetch> fetch; bool tried=false; };
    struct Page { SkinSpritePage info; std::string version, bundle; std::unique_ptr<HttpFetch::Fetch> fetch; std::unique_ptr<GAGCore::DrawableSurface> surface; std::uint64_t touched=0; bool requested=false; unsigned failures=0; };
    OnlineStorage &storage;
    std::string origin;
    SkinDownloads::FetchStarter start;
    std::array<Team,32> teams;
    std::map<std::string,Page> pages;
    std::size_t decoded=0,diskBytes=0;
    std::uint64_t clock=0;
    Counters metrics;
    struct DiskPage {std::size_t bytes=0;std::uint64_t touched=0;};
    std::map<std::string,DiskPage> disk;
    void cache(const std::string &path,const std::string &bytes);
};
}
