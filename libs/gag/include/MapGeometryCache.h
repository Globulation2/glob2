// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>
#include <functional>
#include <memory>
#include <vector>

namespace GAGCore
{
class GraphicContext;
class RenderBatch;

// Presentation-only geometry retained by one graphics context. Keys describe
// canonical map coordinates, while the caller's model-view translation places
// them in the current torus view. Exact frame vectors (including visibility)
// validate entries. A caller may supply a unique revision that identifies the
// same exact frames and artwork to skip the vector comparison on hits.
// artworkRevision names the textures the frames resolve to (a sprite's HD
// layers). An entry with the same frames under an older artwork revision is
// still drawable: it is re-recorded within part of the frame's build budget
// and replayed as recorded until then, and while a capture is rejected for
// uploading a texture (retried next time), so new artwork swaps in without a
// direct draw in between. A capture that fails for any other reason would
// fail again, so it drops the entry and the caller draws the new artwork.
class MapGeometryCache
{
public:
    struct Key
    {
        std::variant<const void*, std::uint64_t> map;
        int layer, x, y, width, height;
        bool operator==(const Key&) const = default;
    };
    struct Stats
    {
        std::size_t bytes = 0, entries = 0;
        unsigned long long hits = 0, misses = 0, builds = 0, deferred = 0;
        // Replays of an entry recorded under an older artwork revision, while
        // its rebuild waits for budget or after its capture uploaded a texture;
        // counted in hits too. A rebuild that does run counts as a miss and a
        // build.
        unsigned long long staleArtwork = 0;
    };
    MapGeometryCache(GraphicContext *context, RenderBatch *batch);
    ~MapGeometryCache();
    MapGeometryCache(const MapGeometryCache&) = delete;
    MapGeometryCache& operator=(const MapGeometryCache&) = delete;

    // emit draws opaque static sprites in local coordinates. It must not alter
    // clip/transform or texture contents. A false result means nothing was drawn:
    // the caller must render its ordinary visible range. Resource row ranges
    // select SOURCE tiles, including their complete overhanging artwork.
    bool draw(const Key& key, const std::vector<int>& frames,
              const std::function<void()>& emit, int firstTile = -1, int lastTile = -1,
              float translateX = 0, float translateY = 0, std::uint64_t frameRevision = 0,
              std::uint64_t artworkRevision = 0);
    // Amortize client-array and texture state across one terrain/resource pass.
    // Keep this scope on the stack; scopes must not nest. Draw failures restore
    // the ordinary baseline before the caller takes its fallback path.
    class Layer
    {
        MapGeometryCache& cache;
    public:
        explicit Layer(MapGeometryCache& cache);
        ~Layer();
        // Restore baseline before an ordinary draw when callback construction
        // fails before draw() can perform its own fallback cleanup.
        void prepareFallback() noexcept;
        Layer(const Layer&) = delete;
        Layer& operator=(const Layer&) = delete;
    };
    // A scene limits cold geometry builds to 16 attempts and a soft 2 ms of
    // elapsed build work; rebuilds for newer artwork stop at half of each.
    // Hits remain unlimited; one driver call can overshoot the time limit.
    // Standalone draws outside a frame have no warming budget.
    void beginFrame();
    void endFrame() noexcept;
    void abortFrame() noexcept;
    // Promotion invalidates native source references. Mutation/deletion also
    // invalidates the old array page conservatively, without flushing unrelated
    // terrain or resource rows (unit texture changes usually match nothing).
    void invalidateTexture(unsigned sourceID, unsigned oldArrayPage = 0) noexcept;
    void clear() noexcept;
    Stats stats() const;
private:
    void beginLayer();
    void endLayer() noexcept;
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
