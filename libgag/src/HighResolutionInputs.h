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
        AssetLoader::Handle<AssetImage> upgraded;
        bool upgradeRequested = false;
        const AssetLoader::Handle<AssetImage>& current() const {
            // A failed optional upgrade must not discard a valid base image.
            const auto state = upgraded.state();
            const bool fallback = requested &&
                (state == AssetLoader::State::Failed || state == AssetLoader::State::Cancelled);
            return upgradeRequested && !fallback ? upgraded : handle;
        }
        AssetLoader::Handle<AssetImage>& current() {
            // A failed optional upgrade must not discard a valid base image.
            const auto state = upgraded.state();
            const bool fallback = requested &&
                (state == AssetLoader::State::Failed || state == AssetLoader::State::Cancelled);
            return upgradeRequested && !fallback ? upgraded : handle;
        }
    };
    // Supplied atlas mips and standalone generated-mip images use distinct requests.
    std::array<std::unordered_map<std::string, Entry>, 2> entries;
    Entry* find(const std::string& path, bool& mipmaps) {
        auto it = entries[mipmaps].find(path);
        if (it != entries[mipmaps].end()) return &it->second;
        mipmaps = !mipmaps;
        it = entries[mipmaps].find(path);
        return it == entries[mipmaps].end() ? nullptr : &it->second;
    }
    const Entry* find(const std::string& path, bool& mipmaps) const {
        auto it = entries[mipmaps].find(path);
        if (it != entries[mipmaps].end()) return &it->second;
        mipmaps = !mipmaps;
        it = entries[mipmaps].find(path);
        return it == entries[mipmaps].end() ? nullptr : &it->second;
    }
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
        bool registered = mipmaps;
        auto* entry = find(path, registered);
        if (!entry) return;
        // A downgrade can consume the immutable higher-quality request. An
        // upgrade gets its own decode; never retarget an in-flight subscription.
        if (mipmaps && !registered) {
            if (!entry->upgradeRequested) {
                entry->upgraded = loader.requestImage(path, AssetLoader::Priority::Required, true);
                entry->upgradeRequested = true;
            }
        } else if (!entry->requested && !entry->upgradeRequested) {
            entry->handle = loader.requestImage(path, AssetLoader::Priority::Required, registered);
            entry->requested = true;
        }
    }
    void add(AssetLoader& loader, const std::string& path, bool mipmaps = true) {
        add(path, mipmaps); request(loader, path, mipmaps);
    }
    AssetLoader::State state(const std::string& path, bool mipmaps = true) const {
        const auto* entry = find(path, mipmaps);
        return entry ? entry->current().state() : AssetLoader::State::Cancelled;
    }
    bool pending(const std::string& path, bool mipmaps = true) const {
        return state(path, mipmaps) == AssetLoader::State::Pending;
    }
    bool pending() const {
        for (const auto& group : entries)
            for (const auto& [path, entry] : group)
                if (entry.current().pending()) return true;
        return false;
    }
    Image consume(const std::string& path, bool mipmaps = true) {
        auto* selected = find(path, mipmaps);
        if (!selected || selected->current().pending()) return {};
        auto& entry = *selected;
        auto& group = entries[mipmaps];
        Image image;
        // Earlier repeated uses copy pixels. Transfer only on the last use,
        // and fall back to copying while an independent consumer still owns them.
        if (entry.uses == 1) {
            image.value = entry.current().take();
            image.exclusive = bool(image.value);
        }
        if (!image.value) image.value = entry.current().get();
        if (!--entry.uses) group.erase(path);
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
