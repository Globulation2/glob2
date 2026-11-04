// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <AssetLoader.h>

namespace GAGCore {
// Version 2 HD index tokens describe one logical frame. The directory comes
// from the index location, while all referenced files are local basenames.
struct PackedSpriteFrame {
    std::string file, alpha;
    unsigned width = 0, height = 0, count = 0, frame = 0;
    unsigned x = 0, y = 0, cropWidth = 0, cropHeight = 0;

    static bool parse(const std::string& token, PackedSpriteFrame& out);
    AssetLoader::Handle<AssetImage> request(AssetLoader& loader, const std::string& path,
        bool mipmaps, AssetLoader::Priority priority = AssetLoader::Priority::Interactive) const;
};
}
