// SPDX-License-Identifier: GPL-3.0-or-later
#include <MapGeometryCache.h>
#include <RenderBatch.h>
#include "GraphicContextPrivate.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <unordered_map>

namespace GAGCore
{
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
namespace
{
// Map sprites are white and opaque. Keeping unit hue, colour, team UVs and
// opacity out of this layout halves retained GPU memory versus QueueVertex.
struct MapVertex { float x, y, u, v, layer; };
static_assert(sizeof(MapVertex) == 20);
constexpr std::size_t byteLimit = 32 * 1024 * 1024;
constexpr std::size_t entryLimit = 4096;
struct KeyHash
{
    std::size_t operator()(const MapGeometryCache::Key& key) const
    {
        std::size_t value = std::hash<const void*>{}(key.map);
        for (int component : {key.layer, key.x, key.y, key.width, key.height})
            value ^= std::hash<int>{}(component) + 0x9e3779b9u + (value << 6) + (value >> 2);
        return value;
    }
};
struct Run
{
    QueueKey key;
    int offset, count;
    // One source tile per quad, in traversal order. Only resource rows use
    // ranges: sorting geometry itself would change overlap/painter order.
    std::vector<int> tiles;
};
struct Entry
{
    std::vector<int> frames;
    std::vector<Run> runs;
    unsigned buffer = 0;
    std::size_t bytes = 0;
    std::uint64_t touched = 0;
    // Distinct (array-page?, texture-name) references make invalidation precise
    // and let a cheap union lookup reject unrelated unit texture changes.
    std::vector<std::pair<bool, unsigned>> references;
    bool referencesRegistered = false;
};
struct MatrixScope
{
    MatrixScope(float x, float y) { glPushMatrix(); glTranslatef(x, y, 0); }
    ~MatrixScope() { glPopMatrix(); }
};
}
struct MapGeometryCache::Impl
{
    GraphicContext *gfx;
    RenderBatch *batch;
    std::unordered_map<Key, Entry, KeyHash> entries;
    Stats counters;
    std::uint64_t clock = 0;
    std::unordered_map<unsigned, std::size_t> nativeReferences, arrayReferences;
    bool frame = false;
    unsigned frameBuilds = 0;
    std::chrono::steady_clock::duration frameBuildTime{};
    bool layer = false, prepared = false;
    QueueKey previous;
    bool hasPrevious = false;
    Impl(GraphicContext *gfx, RenderBatch *batch) : gfx(gfx), batch(batch) {}

    void unregisterReferences(const Entry& entry) noexcept
    {
        for (const auto& [array, texture] : entry.references)
        {
            auto& references = array ? arrayReferences : nativeReferences;
            auto found = references.find(texture);
            assert(found != references.end());
            if (--found->second == 0) references.erase(found);
        }
    }
    void registerReferences(Entry& entry)
    {
        std::size_t registered = 0;
        try
        {
            for (const auto& [array, texture] : entry.references)
            {
                ++(array ? arrayReferences : nativeReferences)[texture];
                ++registered;
            }
            entry.referencesRegistered = true;
        }
        catch (...)
        {
            // Roll back the partial index without allocating during unwinding.
            for (std::size_t i = 0; i < registered; ++i)
            {
                const auto& [array, texture] = entry.references[i];
                auto& references = array ? arrayReferences : nativeReferences;
                auto found = references.find(texture);
                if (--found->second == 0) references.erase(found);
            }
            throw;
        }
    }
    void erase(std::unordered_map<Key, Entry, KeyHash>::iterator entry) noexcept
    {
        if (entry->second.referencesRegistered) unregisterReferences(entry->second);
        if (entry->second.buffer) glDeleteBuffers(1, &entry->second.buffer);
        counters.bytes -= entry->second.bytes;
        entries.erase(entry);
    }
    void resetLayout() noexcept
    {
        if (prepared) batch->finishReplay();
        prepared = false;
        hasPrevious = false;
    }
    void clear() noexcept
    {
        resetLayout();
        while (!entries.empty()) erase(entries.begin());
        assert(nativeReferences.empty() && arrayReferences.empty());
    }
    void trim(std::size_t incoming)
    {
        // Evict before upload: even temporary live VBO payload stays bounded.
        while (!entries.empty() && (counters.bytes + incoming > byteLimit || entries.size() >= entryLimit))
        {
            auto oldest = entries.begin();
            for (auto it = entries.begin(); it != entries.end(); ++it)
                if (it->second.touched < oldest->second.touched) oldest = it;
            erase(oldest);
        }
    }
    void replay(const Entry& entry, int firstTile, int lastTile)
    {
        glBindBuffer(GL_ARRAY_BUFFER, entry.buffer);
        if (!prepared)
        {
            glEnableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
            glColor4ub(255, 255, 255, 255);
            for (int unit = 0; unit < 4; ++unit)
            {
                glClientActiveTexture(GL_TEXTURE0 + unit);
                if (unit == 0 || unit == 3) glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                else glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            }
            glClientActiveTexture(GL_TEXTURE0);
            glMultiTexCoord2f(GL_TEXTURE2, 0, 1);
            prepared = true;
        }
        glVertexPointer(2, GL_FLOAT, sizeof(MapVertex), reinterpret_cast<void*>(offsetof(MapVertex, x)));
        glClientActiveTexture(GL_TEXTURE0);
        glTexCoordPointer(2, GL_FLOAT, sizeof(MapVertex), reinterpret_cast<void*>(offsetof(MapVertex, u)));
        glClientActiveTexture(GL_TEXTURE3);
        glTexCoordPointer(1, GL_FLOAT, sizeof(MapVertex), reinterpret_cast<void*>(offsetof(MapVertex, layer)));
        glClientActiveTexture(GL_TEXTURE0);
        for (const Run& run : entry.runs)
        {
            int offset = run.offset, count = run.count;
            if (firstTile >= 0)
            {
                auto first = std::lower_bound(run.tiles.begin(), run.tiles.end(), firstTile);
                auto last = std::upper_bound(first, run.tiles.end(), lastTile);
                if (first == last) continue;
                offset += 4 * int(first - run.tiles.begin());
                count = 4 * int(last - first);
            }
            // Filter before binding: a narrow viewport usually touches few of
            // the textures present in a full canonical resource row.
            if (!hasPrevious || previous != run.key)
            {
                batch->bind(run.key);
                previous = run.key;
                hasPrevious = true;
            }
            glDrawArrays(GL_QUADS, offset, count);
            gfx->countRenderBatchDraw();
        }
        if (!layer) resetLayout();
    }
};
#else
struct MapGeometryCache::Impl
{
    Stats counters;
    Impl(GraphicContext*, RenderBatch*) {}
    void clear() noexcept {}
};
#endif

MapGeometryCache::MapGeometryCache(GraphicContext *context, RenderBatch *batch)
    : impl(std::make_unique<Impl>(context, batch)) {}
MapGeometryCache::~MapGeometryCache() { clear(); }
MapGeometryCache::Layer::Layer(MapGeometryCache& cache) : cache(cache) { cache.beginLayer(); }
MapGeometryCache::Layer::~Layer() { cache.endLayer(); }
void MapGeometryCache::Layer::prepareFallback() noexcept
{
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
    cache.impl->resetLayout();
#endif
}
void MapGeometryCache::beginLayer()
{
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
    assert(!impl->layer);
    impl->layer = true;
#endif
}
void MapGeometryCache::endLayer() noexcept
{
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
    impl->resetLayout();
    impl->layer = false;
#endif
}
void MapGeometryCache::beginFrame()
{
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
    assert(!impl->frame);
    impl->frame = true;
    impl->frameBuilds = 0;
    impl->frameBuildTime = {};
#endif
}
void MapGeometryCache::endFrame() noexcept
{
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
    impl->resetLayout();
    impl->frame = false;
#endif
}
void MapGeometryCache::abortFrame() noexcept { endFrame(); }
void MapGeometryCache::invalidateTexture(unsigned sourceID, unsigned oldArrayPage) noexcept
{
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
    auto& cache = *impl;
    const bool native = sourceID && cache.nativeReferences.contains(sourceID);
    const bool array = oldArrayPage && cache.arrayReferences.contains(oldArrayPage);
    if (!native && !array) return;
    cache.resetLayout();
    for (auto entry = cache.entries.begin(); entry != cache.entries.end();)
    {
        bool matches = std::any_of(entry->second.references.begin(), entry->second.references.end(),
            [&](const auto& reference)
            {
                return reference.first ? array && reference.second == oldArrayPage
                                       : native && reference.second == sourceID;
            });
        if (matches) { auto doomed = entry++; cache.erase(doomed); }
        else ++entry;
    }
#endif
}
void MapGeometryCache::clear() noexcept { impl->clear(); }
MapGeometryCache::Stats MapGeometryCache::stats() const
{
    auto result = impl->counters;
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
    result.entries = impl->entries.size();
#endif
    return result;
}

bool MapGeometryCache::draw(const Key& key, const std::vector<int>& frames,
    const std::function<void()>& emit, int firstTile, int lastTile, float translateX, float translateY)
{
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
    auto& cache = *impl;
    if (cache.batch->active()) return false;
    MatrixScope matrix(translateX, translateY);
    auto found = cache.entries.find(key);
    if (found != cache.entries.end() && found->second.frames == frames)
    {
        found->second.touched = ++cache.clock;
        ++cache.counters.hits;
        cache.replay(found->second, firstTile, lastTile);
        return true;
    }
    cache.resetLayout(); // Capturing/fallback paths require ordinary GL state.
    ++cache.counters.misses;
    if (cache.frame && (cache.frameBuilds >= 16 || cache.frameBuildTime >= std::chrono::milliseconds(2)))
    {
        ++cache.counters.deferred;
        return false;
    }
    ++cache.counters.builds;
    ++cache.frameBuilds;
    struct BuildTimer
    {
        Impl& cache;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        ~BuildTimer() { if (cache.frame) cache.frameBuildTime += std::chrono::steady_clock::now() - start; }
    } timer{cache};
    if (found != cache.entries.end()) cache.erase(found);
    const auto captureGeneration = cache.batch->textureGeneration();
    Entry entry;
    std::vector<MapVertex> vertices;
    bool valid = true;
    try
    {
        entry.frames = frames;
        cache.batch->beginCapture([&](const QueueKey& drawKey, const std::vector<QueueVertex>& input)
        {
            // This cache accepts static sprite quads only. Reject rather than
            // silently discarding tint/alpha or changing a primitive's meaning.
            if ((drawKey.kind != QueueKey::Texture && drawKey.kind != QueueKey::ArrayTexture) || input.size() % 4) { valid = false; return; }
            Run run{drawKey, int(vertices.size()), int(input.size()), {}};
            for (const auto& v : input)
            {
                if (v.color.r != 255 || v.color.g != 255 || v.color.b != 255 || v.color.a != 255 || v.alpha != 1)
                    valid = false;
                vertices.push_back({v.x, v.y, v.u, v.v, v.baseLayer});
            }
            for (std::size_t i = 0; i < input.size(); i += 4)
                run.tiles.push_back(int(std::floor((input[i].x + input[i + 2].x) / 64.f)));
            if (firstTile >= 0 && !std::is_sorted(run.tiles.begin(), run.tiles.end())) valid = false;
            entry.references.emplace_back(drawKey.kind == QueueKey::ArrayTexture, drawKey.base);
            entry.runs.push_back(std::move(run));
        });
        emit();
        cache.batch->endCapture();
        entry.bytes = vertices.size() * sizeof(MapVertex);
        if (cache.batch->textureGeneration() != captureGeneration) valid = false;
        if (!valid || entry.bytes > byteLimit || vertices.size() > std::size_t(std::numeric_limits<int>::max())) return false;
        std::sort(entry.references.begin(), entry.references.end());
        entry.references.erase(std::unique(entry.references.begin(), entry.references.end()), entry.references.end());
        cache.trim(entry.bytes);
        if (entry.bytes)
        {
            glGenBuffers(1, &entry.buffer);
            if (!entry.buffer) return false;
            glBindBuffer(GL_ARRAY_BUFFER, entry.buffer);
            glBufferData(GL_ARRAY_BUFFER, entry.bytes, vertices.data(), GL_STATIC_DRAW);
            // Query actual allocation instead of consuming an unrelated GL error
            // left by the caller. An unsuccessful upload never becomes a hit.
            GLint allocated = 0;
            glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &allocated);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            if (allocated != static_cast<GLint>(entry.bytes))
            {
                glDeleteBuffers(1, &entry.buffer);
                return false;
            }
        }
        entry.touched = ++cache.clock;
        auto inserted = cache.entries.emplace(key, std::move(entry));
        entry.buffer = 0; // The installed entry now owns the buffer, including on index failure.
        cache.counters.bytes += inserted.first->second.bytes;
        try { cache.registerReferences(inserted.first->second); }
        catch (...) { cache.erase(inserted.first); throw; }
        cache.replay(inserted.first->second, firstTile, lastTile);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        cache.batch->abortCapture();
        if (entry.buffer) glDeleteBuffers(1, &entry.buffer);
        return false;
    }
    catch (...)
    {
        cache.batch->abortCapture();
        if (entry.buffer) glDeleteBuffers(1, &entry.buffer);
        throw;
    }
#else
    return false;
#endif
}
}
