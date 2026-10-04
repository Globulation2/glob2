// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <tuple>

namespace GAGCore
{
// Owns no mesh/surface pointers. Touch every visible hit before reserving misses
// so a batch of at most Capacity requests cannot evict its own visible tiles.
class SkinAtlasCache
{
public:
    static constexpr unsigned Capacity = 1024;
    // Mesh identity, pose, paint lifetime and revision, material lifetime and
    // revision, atlas region. Any change rasterizes a fresh tile.
    using Key = std::tuple<std::uint64_t, unsigned, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint8_t>;
    static Key key(std::uint64_t mesh, unsigned frame, std::uint64_t paint, std::uint64_t paintRevision,
                   std::uint64_t material, std::uint64_t materialRevision, std::uint8_t region)
    {
        return Key{mesh, frame, paint, paintRevision, material, materialRevision, region};
    }
    SkinAtlasCache() = default;
    SkinAtlasCache(const SkinAtlasCache&) = delete;
    SkinAtlasCache& operator=(const SkinAtlasCache&) = delete;
    SkinAtlasCache(SkinAtlasCache&&) = default;
    SkinAtlasCache& operator=(SkinAtlasCache&&) = default;
    std::optional<unsigned> find(const Key &key) const
    {
        auto found = slots.find(key);
        if (found == slots.end()) return {};
        return found->second.slot;
    }
    bool touch(const Key &key)
    {
        auto found = slots.find(key);
        if (found == slots.end()) return false;
        recent.splice(recent.end(), recent, found->second.position);
        return true;
    }
    unsigned reserve(const Key &key)
    {
        if (touch(key)) return *find(key);
        unsigned slot = slots.size();
        if (slot == Capacity)
        {
            auto oldest = slots.find(recent.front());
            slot = oldest->second.slot;
            slots.erase(oldest);
            recent.pop_front();
        }
        recent.push_back(key);
        slots.emplace(key, Entry{slot, std::prev(recent.end())});
        return slot;
    }
    std::size_t size() const { return slots.size(); }
private:
    struct Entry { unsigned slot; std::list<Key>::iterator position; };
    std::list<Key> recent;
    std::map<Key, Entry> slots;
};
}
