// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <stdexcept>
#include <string>

namespace MobileAssetIndex
{
// Keep the packaging bound in asset_bundle.py in step with this reader.
inline constexpr Sint64 MaximumBytes = 4 * 1024 * 1024;

inline std::string read(SDL_IOStream* stream)
{
    const auto size = SDL_GetIOSize(stream);
    if (size < 0 || size > MaximumBytes)
        throw std::runtime_error("Invalid asset index size");
    std::string contents(static_cast<std::size_t>(size), '\0');
    if (SDL_ReadIO(stream, contents.data(), contents.size()) != contents.size())
        throw std::runtime_error("Cannot read asset index");
    return contents;
}
}
