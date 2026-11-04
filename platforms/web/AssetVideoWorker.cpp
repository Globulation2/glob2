// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../libs/gag/src/AssetVideoDecoder.h"
#include <cstring>

extern "C" int glob2_decode_asset_video(const uint8_t *data, size_t size,
        unsigned width, unsigned height, unsigned count, uint8_t *output, size_t capacity)
{
    using namespace GAGCore::AssetVideoDecoder;
    if (!data || !output || !validDimensions(width, height, count) ||
            capacity != size_t(width) * height * 4 * count)
        return 0;
    try {
        const size_t rowBytes = size_t(width) * 4;
        decode({data, size}, width, height, count, [&](unsigned index, const uint8_t *rgba, int pitch) {
            for (unsigned row = 0; row < height; ++row)
                std::memcpy(output + (size_t(index) * height + row) * rowBytes,
                            rgba + size_t(row) * pitch, rowBytes);
        });
        return 1;
    } catch (...) {
        return 0;
    }
}
