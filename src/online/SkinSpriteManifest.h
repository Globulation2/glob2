// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SkinAuthorization.h"
#include <array>
#include <string>
#include <vector>
namespace Online {
inline constexpr std::array<const char *,8> SkinSpriteClips{{"worker-walk","worker-swim","worker-harvest","warrior-walk","warrior-swim","warrior-fight","explorer-fly","swarm"}};
inline constexpr std::array<unsigned,8> SkinSpriteLogicalSizes{{38,38,38,40,40,40,32,96}};
struct SkinSpritePage {
    std::string hash;
    unsigned bytes=0, clip=0, first=0, frames=0, size=0;
};
struct SkinSpriteManifest {
    std::vector<SkinSpritePage> pages;
    bool parse(const std::string &bytes, const AuthorizedSkin &skin);
};
// Bounds WebP before SDL_image sees it. Reject animation and unknown RGB formats.
bool validSkinSpritePage(const std::string &bytes, const SkinSpritePage &page);
}
