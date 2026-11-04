// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace GAGCore::AssetVideoDecoder {
inline constexpr std::size_t MaxEncodedBytes = 16u * 1024u * 1024u;

inline bool validDimensions(unsigned width, unsigned height, unsigned count) {
    return width && height && width <= 2048 && height <= 2048 && count && count <= 16 &&
        std::size_t(width) * height * 4 * count <= 64u * 1024u * 1024u;
}

// Pixels are byte-ordered RGBA with opaque alpha. Each frame remains valid
// only for the duration of the callback.
using FrameSink = std::function<void(unsigned index, const uint8_t *rgba, int pitch)>;
bool supported();
void decode(std::span<const uint8_t> bytes, unsigned width, unsigned height,
    unsigned frameCount, const FrameSink& output);
}
