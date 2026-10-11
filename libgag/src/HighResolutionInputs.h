// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <AssetLoader.h>
#include <array>
#include <unordered_map>

namespace GAGCore {
// Presentation-thread staging shared by initial sprite loads and HD reloads.
// Register remaining uses before requesting the existing bounded decode window.
class HighResolutionInputs {
    struct Entry {
        AssetLoader::Handle<AssetImage> handle;
        size_t uses = 0;
        bool requested = false;
    };
    // Supplied atlas mips and standalone generated-mip images use distinct requests.
    std::array<std::unordered_map<std::string, Entry>, 2> entries;
public:
    struct Image {
        std::shared_ptr<const AssetImage> value;
        bool exclusive = false;
    };
    HighResolutionInputs() = default;
    HighResolutionInputs(const HighResolutionInputs&) = delete;
    HighResolutionInputs& operator=(const HighResolutionInputs&) = delete;
    HighResolutionInputs(HighResolutionInputs&&) = default;
    HighResolutionInputs& operator=(HighResolutionInputs&&) = default;

    void add(const std::string& path, bool mipmaps = true) {
        ++entries[mipmaps][path].uses;
    }
    void request(AssetLoader& loader, const std::string& path, bool mipmaps = true) {
        auto& group = entries[mipmaps];
        auto it = group.find(path);
        if (it == group.end() || it->second.requested) return;
        it->second.handle = loader.requestImage(path, AssetLoader::Priority::Required, mipmaps);
        it->second.requested = true;
    }
    void add(AssetLoader& loader, const std::string& path, bool mipmaps = true) {
        add(path, mipmaps); request(loader, path, mipmaps);
    }
    AssetLoader::State state(const std::string& path, bool mipmaps = true) const {
        const auto& group = entries[mipmaps];
        auto it = group.find(path);
        return it == group.end() ? AssetLoader::State::Cancelled : it->second.handle.state();
    }
    bool pending(const std::string& path, bool mipmaps = true) const {
        return state(path, mipmaps) == AssetLoader::State::Pending;
    }
    bool pending() const {
        for (const auto& group : entries)
            for (const auto& [path, entry] : group)
                if (entry.handle.pending()) return true;
        return false;
    }
    Image consume(const std::string& path, bool mipmaps = true) {
        auto& group = entries[mipmaps];
        auto it = group.find(path);
        if (it == group.end()) return {};
        auto& entry = it->second;
        Image image;
        // Earlier repeated uses copy pixels. Transfer only on the last use,
        // and fall back to copying while an independent consumer still owns them.
        if (entry.uses == 1) {
            image.value = entry.handle.take();
            image.exclusive = bool(image.value);
        }
        if (!image.value) image.value = entry.handle.get();
        if (!--entry.uses) group.erase(it);
        return image;
    }
    size_t size() const {
        size_t uses = 0;
        for (const auto& group : entries)
            for (const auto& [path, entry] : group) uses += entry.uses;
        return uses;
    }
    void clear() { for (auto& group : entries) group.clear(); }
};
}
