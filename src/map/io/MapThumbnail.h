// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "SDL_net.h"

namespace GAGCore { class DrawableSurface; class OutputStream; class InputStream; }
class Map;

// Immutable, shared terrain pixels. The legacy wire image remains 128x128;
// local previews retain up to 512 pixels along the longest map dimension.
class MapThumbnail
{
public:
    static constexpr int MaxResolution = 512;
    static constexpr unsigned MaxEncodedBytes = 60000;
    struct Image { int width, height; std::vector<Uint8> rgb; };
    void loadFromMap(const std::string& filename);
    void loadFromMap(const Map& map);
    void encodeData(GAGCore::OutputStream* stream) const;
    void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
    void loadIntoSurface(GAGCore::DrawableSurface* surface) const;
    int getMapWidth() const { return lastW; }
    int getMapHeight() const { return lastH; }
    bool isLoaded() const { return bool(image); }
    const std::shared_ptr<const Image>& pixels() const { return image; }
private:
    std::shared_ptr<const Image> image;
    int lastW = 0, lastH = 0;
};
