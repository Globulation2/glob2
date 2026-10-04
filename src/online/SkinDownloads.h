// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HttpFetch.h"
#include "SkinAuthorization.h"
#include <functional>
#include <vector>
namespace Online {
class OnlineStorage;
// Presentation-only loader. Every remote byte is bounded and checked before it
// reaches the image decoder. Missing or rejected cosmetics leave classic art.
class SkinDownloads {
public:
    struct Ticket { int team; std::string assertion; };
    // colony-v2 skins install only once both the 512x512 colour atlas (path)
    // and the 512x512 material-id map (materialPath) are cached and verified.
    struct Ready { AuthorizedSkin skin; std::string path, materialPath; };
    using FetchStarter=std::function<std::unique_ptr<HttpFetch::Fetch>(HttpFetch::Request)>;
    SkinDownloads(OnlineStorage &, std::string origin, std::string match,
                  std::vector<Ticket>, FetchStarter fetch=HttpFetch::start, bool refresh=true);
    ~SkinDownloads();
    void poll(std::int64_t nowSeconds);
    std::vector<Ready> takeReady();
    std::vector<int> takeRemoved();
    // Initial batch completion; keep polling for moderation and expiry updates.
    bool done() const;
    const std::string &origin() const;
    const std::string &matchId() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
