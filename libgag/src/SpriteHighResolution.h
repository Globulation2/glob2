// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <span>
#include <AssetLoader.h>

namespace GAGCore {
std::vector<AssetLoader::Handle<AssetImage>> prefetchHighResolutionIncremental(
    const std::string& name, size_t frames);
// Adopt ready HD frames into a private sprite while other frames decode.
// Publication still waits for the complete sprite and its optional atlas.
bool highResolutionFrameReady(const std::string& name, size_t index,
    std::span<AssetLoader::Handle<AssetImage>> inputs);
bool highResolutionAtlasReady(const std::string& name,
    std::span<AssetLoader::Handle<AssetImage>> inputs);
}
