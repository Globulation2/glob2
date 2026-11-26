// SPDX-License-Identifier: GPL-3.0-or-later
#include "DirectTerrain.h"
#include <cstdlib>
#include <cstring>
#include <GraphicContext.h>
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif

#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
#define DIRECT_TERRAIN_LEGACY_GL 1
#endif

#include "GlobalContainer.h"
#include "DirectTerrainData.h"
#include "TerrainCompositor.h"
#include "TerrainArtBorder.h"
#include "render/SoftwareTerrainCache.h"
#include "render/BackgroundWork.h"
#include "render/scene/SceneMap.h"
#ifdef DIRECT_TERRAIN_LEGACY_GL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif
#include <RenderBatch.h>
#include <PresentationWork.h>
#include <ThreadSupport.h>
#include <DirectTerrainShader.h>
#include <RenderDevice.h>
#include <RenderResources.h>
#include <RenderMemory.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <utility>
#include <cstdio>
#include <chrono>
#include <unordered_set>
#include <unordered_map>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <functional>
#include <thread>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>
#include <map>
#include <set>
#include <tuple>

namespace
{
constexpr int TilePixels = DirectTerrainData::TilePixels;
// The resident low-resolution art's pixels a cell (LowArtTile in the shader).
constexpr int LowArtTile = 16;
constexpr int LowArtStride = LowArtTile + 2;
using Pixel = DirectTerrainData::Pixel;
using Record = DirectTerrainData::Record;

#ifdef DIRECT_TERRAIN_LEGACY_GL
constexpr const char *Vertex = R"(
#version 120
varying vec2 world;
void main() {
    world = gl_MultiTexCoord0.xy;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)";

struct RestoreGL
{
    GLint program = 0;
    GLint activeTexture = GL_TEXTURE0;
    GLint unpackBuffer = 0;
    RestoreGL()
    {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glPushAttrib(GL_CURRENT_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT | GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT);
        glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
    }
    ~RestoreGL()
    {
        glUseProgram(program);
        glPopClientAttrib();
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, GLuint(unpackBuffer));
        glPopAttrib();
        glActiveTexture(activeTexture);
    }
};

GLuint compile(GLenum type, const char *source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status)
    {
        char error[512]{};
        glGetShaderInfoLog(shader, sizeof(error), nullptr, error);
        glDeleteShader(shader);
        throw std::runtime_error(error);
    }
    return shader;
}
#else
struct RestoreGL {};
#endif

// A texture of the streamed pass: a Legacy GL texture name, or a device
// StreamedTexture written in place. Scalars, records and discovery bytes are
// packed into RGBA8 texels on devices, as the packed shader reads them.
enum class Format { Rgba8, Scalar16, Record16, Luminance8 };
struct Surface
{
#ifdef DIRECT_TERRAIN_LEGACY_GL
    GLuint name = 0;
#endif
    std::shared_ptr<GAGCore::StreamedTexture> streamed;
    int width = 0, height = 0;
    Format format = Format::Rgba8;
    // Device scalar textures written a cell at a time keep their values, to
    // rewrite the texel each pair of cells shares.
    std::vector<Uint16> mirror;
    explicit operator bool() const { return width > 0; }
};

}

struct DirectTerrain::State
{
    using Draw = GAGCore::DirectTerrainDraw;
    using enum Draw::Texture;
    std::shared_ptr<GAGCore::RenderResources> resources;
    std::array<GAGCore::TextureHandle, Count> handles{};
    std::uint64_t visibilityRevision = 0;
    SDL_GLContext context = nullptr;
    unsigned generation = 0;
    Uint64 identity = 0, revision = 0;
    Uint32 seed = 0;
    std::shared_ptr<const MapAssetBundle> assets;
    std::shared_ptr<const TerrainRegistry> registry;
    // The streamed pass: Legacy GL, or a device with streamed textures
    // (streaming), which draws the same pass through terrain-direct-hd.
    bool streaming = false;
    // Per-cell tables, built once per map in slices and then edited in place.
    std::optional<DirectTerrainData::Build> building;
    bool tablesReady = false;
    std::array<Surface, Count> textures{};
    std::vector<Record> cells;
    std::vector<Pixel> calm;
    // Variant records and their sources; art indices fill in as the art atlas
    // places each variant. Grows when an edit introduces a new variant.
    DirectTerrainData::Palette palette;
    std::size_t uploadedVariants = 0;
    int variantRows = 1;
    // Vertex terrain the tables describe, to find what an edit changed.
    std::vector<Uint32> vertices;
    // Both atlases start at 2048. The mask atlas stays there, and the art
    // atlas grows once to 4096 when a view needs more tiles at its density,
    // only under the desktop budget and while the renderer has room for it
    // (prepare); otherwise the view takes coarser tiles. Legacy GL and the
    // devices share these limits so they coarsen alike.
    int maxAtlasSize = 2048, maxArtAtlasSize = 4096;
    // The shaders' animation time wraps at this common period of the
    // catalogue's animations (TerrainVisual::animationPeriod).
    int animationPeriod = 0;
    // The last art admission logged (DIRECT_TERRAIN_ADMISSION), to log changes only.
    std::tuple<int, int, int, const char *> admission{};
    struct Animation {
        int size = 2048;
        // Static variants take one tile rather than a pair
        // (DirectTerrainData::ArtTiles), so a view of them fits twice as many
        // tiles at the display density.
        struct Entry {
            unsigned tile;
            std::array<unsigned, 2> phases{~0u, ~0u};
            std::array<TerrainVisual::Compositor::PhaseSource, 2> stamps{};
            // The endpoint after next of an animated variant, filtered by the
            // workers while the current two draw, so the step that needs it
            // writes it at once instead of drawing a stale endpoint meanwhile.
            struct Ahead {
                unsigned phase = ~0u;
                TerrainVisual::Compositor::PhaseSource stamp{};
                std::vector<Pixel> tile;
            } ahead;
        };
        Surface art, variants;
        std::size_t bytes = 0, variantBytes = 0; // bytes includes the variant table's.
        int pixels = 0;
        unsigned serial = 0; // Which atlas a background filter batch belongs to.
        bool failed = false;
        std::map<unsigned, Entry> entries;
        DirectTerrainData::ArtTiles tiles;
        std::vector<SDL_Rect> regions;
        // Tiles a variant needs: two endpoints when it animates, else one.
        static unsigned span(const TerrainVisual::Material &material)
        { return material.animationFrames > 1 ? 2 : 1; }
    } animation;
    // The atlas a density or size change fills over several frames, a bounded
    // number of tiles each, while `animation` keeps drawing; it replaces
    // `animation` once every visible variant is in it.
    Animation nextArt;
    // Every palette variant at LowArtTile pixels a cell, from its native first
    // frame, resident for the map: the art a cell draws whenever the drawing
    // atlas does not hold its variant (the variant table marks it AbsentTile),
    // as while a zoom fills `nextArt` over several frames. The streamed pass
    // binds it in place of the native mask index, which it does not use.
    struct LowArt {
        Surface surface;
        int width = 0, height = 0;
        std::size_t count = 0, bytes = 0; // Variants written; texture bytes.
    } lowArt;
    // A variant record's tile when the drawing atlas does not hold the variant.
    static constexpr Uint16 AbsentTile = 65535;
    // Whether an atlas entry has art in every tile it draws: both endpoints of
    // an animated variant.
    static bool filledTiles(const Animation::Entry &entry, bool animated)
    { return entry.phases[0] != ~0u && (!animated || entry.phases[1] != ~0u); }
    // The drawing atlas's variant table: each record's tile is its tile in
    // `atlas`, or AbsentTile when the atlas does not hold the variant or has
    // not filled its tiles yet.
    std::vector<Record> variantTable(const Animation &atlas, int rows)
    {
        std::vector<Record> padded(palette.variants);
        padded.resize(std::size_t(rows) * DirectTerrainData::VariantWidth);
        const std::size_t slots = palette.variants.size() / DirectTerrainData::VariantRecords;
        tableTiles.assign(slots, AbsentTile);
        for (std::size_t slot = 1; slot < slots; ++slot)
        {
            const auto found = atlas.entries.find(unsigned(slot));
            auto &record = padded[slot * DirectTerrainData::VariantRecords];
            record[0] = found != atlas.entries.end() && filledTiles(found->second, record[1] > 1)
                ? Uint16(found->second.tile) : AbsentTile;
            tableTiles[slot] = record[0];
        }
        return padded;
    }
#ifdef DIRECT_TERRAIN_LEGACY_GL
    // Legacy GL binds this 1x1 zeroed texture to every unit whose surface has
    // no texture yet (the base before it is built, a retired atlas that is not
    // there): a sampler must never meet a unit without one, even on branches
    // the shader does not take. Created on first use.
    GLuint placeholder = 0;
#endif
    unsigned placeholderBindings = 0; // Units the last legacy draw bound it to.
    // The drawing atlas's uploaded record tiles, per variant slot, mirrored to
    // check that no visible variant draws a tile its atlas does not hold.
    std::vector<Uint16> tableTiles;
    struct ArtStats { std::size_t visible = 0, absent = 0, stale = 0; } artStats;
    // Writes every variant the palette gained since the last call into the
    // low-resolution atlas, growing it as needed.
    void updateLowArt(TerrainVisual::Compositor &compositor)
    {
        const std::size_t count = palette.sources.size();
        if (lowArt.count == count && lowArt.surface) return;
        const auto started = SDL_GetTicksNS();
        const int columns = count <= 4096 ? 64 : count <= 16384 ? 128 : 224;
        const int width = columns * LowArtStride;
        const int rows = std::max(1, int((count + columns - 1) / columns));
        std::size_t from = lowArt.count;
        std::vector<Pixel> initial;
        if (!lowArt.surface || width != lowArt.width || rows * LowArtStride > lowArt.height)
        {
            // Room for a quarter more variants, so edits rarely regrow it.
            const int height = (rows + std::max(1, rows / 4)) * LowArtStride;
            bytes -= lowArt.bytes;
            lowArt.width = width;
            lowArt.height = height;
            lowArt.bytes = std::size_t(width) * height * sizeof(Pixel);
            bytes += lowArt.bytes;
            initial.resize(std::size_t(width) * height);
            from = 0;
        }
        std::vector<Pixel> tile;
        for (std::size_t slot = std::max<std::size_t>(from, 1); slot < count; ++slot)
        {
            const auto source = palette.sources[slot];
            const auto stamp = compositor.phaseSource(source.material, source.variant, 0, true);
            const auto *surface = compositor.phaseSurface(source.material, source.variant, 0, true);
            TerrainVisual::Compositor::MaterialPixels input;
            if (!surface) input = compositor.phasePixels(source.material, source.variant, 0, true);
            if (stamp.size <= 0 || (surface ? surface->w != stamp.size : input.size() != std::size_t(stamp.size) * stamp.size))
                continue; // No art: the tile stays transparent.
            filterTile(LowArtTile, stamp.size, surface ? static_cast<const Uint32 *>(surface->pixels) : nullptr, input,
                       DirectTerrainData::artBorder(compositor, source, 0, true, LowArtTile), tile);
            const int x = int(slot % columns) * LowArtStride, y = int(slot / columns) * LowArtStride;
            if (!initial.empty())
            {
                for (int row = 0; row < LowArtStride; ++row)
                    std::copy_n(tile.data() + row * LowArtStride, LowArtStride,
                                initial.data() + std::size_t(y + row) * width + x);
            }
            else
                write(lowArt.surface, MaskIndex, x, y, LowArtStride, LowArtStride, tile.data());
        }
        if (!initial.empty())
        {
            create(lowArt.surface, MaskIndex, lowArt.width, lowArt.height, Format::Rgba8, true, initial.data());
            std::fprintf(stderr, "DIRECT_TERRAIN_LOW_ART variants=%zu size=%dx%d bytes=%zu ms=%.1f\n", count,
                         lowArt.width, lowArt.height, lowArt.bytes, double(SDL_GetTicksNS() - started) / 1e6);
        }
        lowArt.count = count;
    }
    // Art tiles box-filtered on the background workers, from copies of their
    // sources, for the atlas whose serial they carry; one batch at a time.
    struct ArtBatch
    {
        struct Job
        {
            unsigned slot, first, parity, phase; // first: the entry's first tile.
            TerrainVisual::Compositor::PhaseSource stamp;
            TerrainVisual::Compositor::MaterialPixels input;
            std::vector<Pixel> border, tile;
            bool ahead = false; // Kept with its entry (Entry::ahead), not written.
        };
        std::vector<Job> jobs;
        int pixels = 0;
        unsigned serial = 0;
        std::atomic<std::size_t> remaining{0};
    };
    std::shared_ptr<ArtBatch> artBatch;
    unsigned artSerial = 0;
    static TerrainVisual::Compositor::PhaseSource artStamp(TerrainVisual::Compositor &compositor,
        DirectTerrainData::Source source, unsigned phase, bool native)
    {
        auto stamp = compositor.phaseSource(source.material, source.variant, phase, native);
        const auto &material = compositor.catalog().materials[source.material];
        if (TerrainVisual::hasArtNeighbors(material))
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dy)
                    {
                        const auto variant = TerrainVisual::artNeighbor(material, source.variant, dx, dy);
                        const auto neighbor = compositor.phaseSource(source.material, variant, phase, native);
                        stamp.edgeIdentity = (stamp.edgeIdentity ^ neighbor.identity) * 1099511628211ull;
                        stamp.edgeRevision = (stamp.edgeRevision ^ neighbor.revision) * 1099511628211ull;
                    }
        return stamp;
    }

    static void filterTile(int pixels, int size, const Uint32 *surface,
                           const TerrainVisual::Compositor::MaterialPixels &input,
                           const std::vector<Pixel> &border, std::vector<Pixel> &tile)
    {
        const int stride = pixels + 2;
        tile.resize(std::size_t(stride) * stride);
        for (int y = 0; y < pixels; ++y)
            for (int x = 0; x < pixels; ++x)
                tile[(y + 1) * stride + x + 1] = TerrainVisual::sampleArt(size, pixels, x, y,
                    [&](std::size_t index) { return surface ? TerrainVisual::artPixel(surface[index]) : input[index]; });
        TerrainVisual::extendArtTile(tile.data(), pixels, border);
    }
    // Coverage masks for visible mixed cells at the display density (8 to 128
    // texels a side), built by background workers.
    struct Hd {
        int size = 2048;
        static constexpr unsigned FirstUnit = Count;
        unsigned program = 0;      // Legacy GL program; devices use terrain-direct-hd.
        bool ready = false;
        Surface index, coverage, edges;
        std::array<int, 13> uniforms{};
        int tile = 0;              // Texels per cell side; 0 before the first prepare.
        // The atlas a density change or growth retired. A cell whose new mask
        // has not landed draws its old one, so a zoom never drops a cell back
        // to its corner split; the atlas goes once the view is complete.
        struct Previous {
            Surface index, coverage, edges;
            int size = 0, tile = 0;   // tile is 0 when there is none; the textures stay as a spare.
            std::size_t bytes = 0;
            Uint64 retiredAt = 0;     // SDL ticks when the spare last had nothing to show.
            std::vector<Uint16> slotOf;
        } previous;
        // A low-density mask of every mixed cell, laid out like the map and
        // built for the whole map before the map's first terrain frame (see
        // buildBase). A cell with no mask at this density or the previous one
        // draws its base mask, so it keeps its borders anywhere on the map;
        // edits rebuild their base cells ahead of any view mask.
        struct Base {
            Surface coverage, edges;
            int tile = 0;                // Texels per cell side; 0 before creation or on huge maps.
            std::size_t bytes = 0, remaining = 0;
            std::vector<Uint8> built;    // Per map cell.
            std::vector<Uint16> version; // Per map cell, bumped by edits.
            std::vector<int> order;      // Mixed cells, nearest the first view first.
            std::size_t cursor = 0;
            std::deque<int> redo;        // Edited cells, ahead of the order.
            Uint64 started = 0;
            bool reported = false;
        } base;
        std::vector<Uint16> slotOf; // Per map cell: atlas slot plus one, or zero.
        std::vector<int> cellOf;    // Per slot: map cell index, or -1.
        std::vector<Uint64> used;   // Per slot: frame last visible.
        Uint64 frame = 0;
        // The base mask serves the view: there is no atlas at the view's density.
        bool suspended = false;
        std::size_t pending = 0, bytes = 0, split = 0;
        bool failed = false;
        // Masks are built by background workers from an immutable material
        // snapshot; the graphics thread only computes recipes and uploads.
        // Base jobs carry the cell's base version as their generation.
        struct Job { int cell; TerrainVisual::Recipe recipe; int tile; unsigned generation; bool base = false; };
        struct Result { int cell; unsigned generation; std::vector<Pixel> weights; std::vector<Uint16> edges; bool base = false; };
        std::mutex mutex;
        std::condition_variable wake;
        std::deque<Job> jobs;      // Visible cells, replaced every frame.
        std::deque<Job> baseJobs;  // Base masks (after the first build, edits): taken before jobs.
        std::vector<Result> done;
        std::vector<std::thread> workers;
        bool stop = false;
        unsigned generation = 0;
        std::shared_ptr<const TerrainVisual::Compositor::CpuMaterials> materials;
        std::unordered_map<int, TerrainVisual::Recipe> recipes; // Per cell, for this generation.
        std::unordered_set<int> inFlight;                        // Cells queued or being built at this density.
        std::unordered_set<int> baseInFlight;                    // Cells queued or being built for the base.
        void stopWorkers()
        {
            {
                std::lock_guard lock(mutex);
                stop = true;
                jobs.clear();
                baseJobs.clear();
            }
            wake.notify_all();
            for (auto &worker : workers) worker.join();
            workers.clear();
        }
        // A mask of tile x tile texels. The compositor samples whole multiples
        // of a cell's 32 pixels; smaller tiles average its weights over blocks
        // and keep the margin of each block's centre sample.
        static Result build(const TerrainVisual::Compositor::CpuMaterials &materials, int cell,
                            const TerrainVisual::Recipe &recipe, int tile, unsigned generation)
        {
            const int scale = std::max(1, tile / 32), size = 32 * scale, block = size / tile;
            const auto mask = materials.mask(recipe, scale);
            Result result{cell, generation, std::vector<Pixel>(std::size_t(tile) * tile),
                          std::vector<Uint16>(std::size_t(tile) * tile)};
            for (std::size_t i = 0; i < result.weights.size(); ++i)
            {
                const int tx = int(i % tile), ty = int(i / tile);
                unsigned sum[4] = {};
                for (int dy = 0; dy < block; ++dy)
                    for (int dx = 0; dx < block; ++dx)
                    {
                        const auto &sample = mask.samples[std::size_t(ty * block + dy) * size + tx * block + dx];
                        const unsigned largest = sample.slots & 3;
                        unsigned remaining = 65536;
                        for (unsigned k = 0, other = 0; k < 4; ++k)
                            if (k != largest)
                            {
                                sum[k] += sample.others[other];
                                remaining -= sample.others[other++];
                            }
                        sum[largest] += remaining;
                    }
                const unsigned count = unsigned(block * block);
                auto &w = result.weights[i];
                for (unsigned k = 0; k < 4; ++k)
                    w[k] = Uint8((sum[k] / count * 255 + 32768) / 65536);
                const auto &sample = mask.samples[std::size_t(ty * block + block / 2) * size + tx * block + block / 2];
                result.edges[i] = Uint16(std::min<unsigned>(sample.margin, 4095) | (unsigned(sample.slots >> 2) << 12));
            }
            return result;
        }
        // Without threads (the serial browser runtime) the graphics thread
        // builds queued masks itself until the deadline, base masks first.
        void buildQueued(std::chrono::steady_clock::time_point deadline)
        {
            std::lock_guard lock(mutex);
            while ((!jobs.empty() || !baseJobs.empty()) && std::chrono::steady_clock::now() < deadline)
            {
                auto &from = baseJobs.empty() ? jobs : baseJobs;
                Job job = std::move(from.front());
                from.pop_front();
                auto result = build(*materials, job.cell, job.recipe, job.tile, job.generation);
                result.base = job.base;
                done.push_back(std::move(result));
            }
        }
        void work()
        {
            for (;;)
            {
                Job job;
                std::shared_ptr<const TerrainVisual::Compositor::CpuMaterials> snapshot;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [&] { return stop || !jobs.empty() || !baseJobs.empty(); });
                    if (stop) return;
                    // The base is the low-resolution terrain every view can
                    // always draw: keep it ahead of the view's masks.
                    auto &from = baseJobs.empty() ? jobs : baseJobs;
                    job = std::move(from.front());
                    from.pop_front();
                    snapshot = materials;
                }
                auto result = build(*snapshot, job.cell, job.recipe, job.tile, job.generation);
                result.base = job.base;
                std::lock_guard lock(mutex);
                done.push_back(std::move(result));
            }
        }
    } hd;
    std::vector<Uint16> cellMasks; // PendingCorners code for mixed cells, zero for uniform ones.
    int width = 0, height = 0;
    std::size_t bytes = 0, uploaded = 0;
    unsigned frames = 0;
    bool failed = false;
    // Tables that baked a material's flat preview colour (its sheet was still
    // loading) are built again once a sheet arrives after this count.
    bool bakedFallback = false;
    std::uint64_t sheetArrivals = 0, sheetFallbacks = 0;
    std::vector<Uint8> visibility;
    Uint32 discoveryTeams = 0;
    Uint64 discoveryRevision = 0;
    std::vector<Uint64> discoveryStamps; // The map's discovery sector stamps last applied.
    DirectTerrain::DiscoveryMetrics discoveryStats;

    bool matchesMap(const SceneMap &map) const
    {
        return sameMapSource(map) && revision == map.terrainRevision();
    }
    // The same map, catalogue and seed, whatever its terrain edits.
    bool sameMapSource(const SceneMap &map) const
    {
        return identity == map.identity() && seed == map.terrainSeed() &&
            assets == map.frozenAssetBundle() && registry == map.frozenTerrainRegistry();
    }

    void releaseDeviceTextures()
    {
        if (resources)
            for (auto &handle : handles)
                if (handle) {
                    resources->erase(handle);
                    handle = {};
                }
    }

    std::array<Surface *, 13 + Count> surfaces()
    {
        std::array<Surface *, 13 + Count> all{&animation.art, &animation.variants, &hd.index, &hd.coverage, &hd.edges,
            &hd.previous.index, &hd.previous.coverage, &hd.previous.edges, &hd.base.coverage, &hd.base.edges,
            &nextArt.art, &nextArt.variants, &lowArt.surface};
        for (unsigned i = 0; i < Count; ++i) all[13 + i] = &textures[i];
        return all;
    }

    // Legacy GL deletes its names only while their context is current; a
    // retired context owns its old names.
    void releaseSurfaces()
    {
        for (auto *surface : surfaces()) drop(*surface);
    }

    ~State()
    {
        hd.stopWorkers();
#ifdef DIRECT_TERRAIN_LEGACY_GL
        if (!streaming && context && context == SDL_GL_GetCurrentContext())
        {
            releaseSurfaces();
            if (hd.program) glDeleteProgram(hd.program);
            if (placeholder) glDeleteTextures(1, &placeholder);
        }
#endif
        releaseDeviceTextures();
    }

    // Texels of a surface's backing texture: devices pack scalars two to a
    // texel, records into two texels and discovery bytes four to a texel.
    static int packedWidth(Format format, int w)
    {
        switch (format)
        {
        case Format::Scalar16: return (w + 1) / 2;
        case Format::Record16: return w * 2;
        case Format::Luminance8: return (w + 3) / 4;
        default: return w;
        }
    }

    // A surface of w x h values; Legacy GL binds it to unit while setting it up.
    // Initial values are optional; a device texture starts cleared to zero.
    void create(Surface &surface, unsigned unit, int w, int h, Format format, bool linear,
                const void *initial = nullptr, bool mirror = false)
    {
        drop(surface);
        surface.width = w;
        surface.height = h;
        surface.format = format;
        if (streaming)
        {
            surface.streamed = std::make_shared<GAGCore::StreamedTexture>();
            surface.streamed->width = packedWidth(format, w);
            surface.streamed->height = h;
            surface.streamed->linear = linear;
            if (mirror) surface.mirror.assign(std::size_t(w) * h, 0);
            if (initial) write(surface, unit, 0, 0, w, h, initial);
            return;
        }
#ifdef DIRECT_TERRAIN_LEGACY_GL
        glGenTextures(1, &surface.name);
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, surface.name);
        const GLint filter = linear ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        const auto [internal, layout, type] = glFormat(format);
        glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, layout, type, initial);
        if (initial) uploaded += std::size_t(w) * h * valueBytes(format);
#else
        (void)unit; (void)linear; (void)initial; (void)mirror;
        throw std::logic_error("Legacy GL terrain surface without OpenGL");
#endif
    }

    static std::size_t valueBytes(Format format)
    {
        switch (format)
        {
        case Format::Scalar16: return 2;
        case Format::Record16: return 8;
        case Format::Luminance8: return 1;
        default: return 4;
        }
    }

#ifdef DIRECT_TERRAIN_LEGACY_GL
    static std::tuple<GLint, GLenum, GLenum> glFormat(Format format)
    {
        switch (format)
        {
        case Format::Scalar16: return {GL_LUMINANCE16, GL_LUMINANCE, GL_UNSIGNED_SHORT};
        case Format::Record16: return {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT};
        case Format::Luminance8: return {GL_LUMINANCE8, GL_LUMINANCE, GL_UNSIGNED_BYTE};
        default: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
        }
    }
#endif

    // Writes w x h values (in the surface's own format) at x, y.
    void write(Surface &surface, unsigned unit, int x, int y, int w, int h, const void *data)
    {
        if (!surface || w <= 0 || h <= 0) return;
        uploaded += std::size_t(w) * h * valueBytes(surface.format);
        if (!streaming)
        {
#ifdef DIRECT_TERRAIN_LEGACY_GL
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, surface.name);
            const auto [internal, layout, type] = glFormat(surface.format);
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, layout, type, data);
#else
            (void)unit;
#endif
            return;
        }
        auto &target = *surface.streamed;
        std::vector<std::uint8_t> rgba;
        switch (surface.format)
        {
        case Format::Rgba8:
        {
            const auto *bytes = static_cast<const std::uint8_t *>(data);
            rgba.assign(bytes, bytes + std::size_t(w) * h * 4);
            target.write({x, y, w, h}, std::move(rgba));
            return;
        }
        case Format::Record16:
        {
            const auto *records = static_cast<const Record *>(data);
            rgba.resize(std::size_t(w) * h * 8);
            for (std::size_t i = 0; i < std::size_t(w) * h; ++i)
                for (unsigned channel = 0; channel < 4; ++channel)
                {
                    rgba[i * 8 + channel] = Uint8(records[i][channel]);
                    rgba[i * 8 + 4 + channel] = Uint8(records[i][channel] >> 8);
                }
            target.write({x * 2, y, w * 2, h}, std::move(rgba));
            return;
        }
        case Format::Luminance8:
        {
            // Discovery is written whole.
            if (x || w != surface.width)
                throw std::logic_error("Packed discovery is written in whole rows");
            const auto *values = static_cast<const Uint8 *>(data);
            const int packed = packedWidth(surface.format, w);
            rgba.assign(std::size_t(packed) * h * 4, 0);
            for (int row = 0; row < h; ++row)
                for (int column = 0; column < w; ++column)
                    rgba[(std::size_t(row) * packed + column / 4) * 4 + column % 4] = values[std::size_t(row) * w + column];
            target.write({0, y, packed, h}, std::move(rgba));
            return;
        }
        case Format::Scalar16:
        {
            const auto *values = static_cast<const Uint16 *>(data);
            // A write that does not cover whole texels takes its neighbours from the mirror.
            int first = x, last = x + w; // Columns [first, last).
            const Uint16 *source = values;
            int sourceX = x, sourceWidth = w;
            if (!surface.mirror.empty())
            {
                for (int row = 0; row < h; ++row)
                    std::copy_n(values + std::size_t(row) * w, w,
                                surface.mirror.begin() + std::size_t(y + row) * surface.width + x);
                first &= ~1;
                last = std::min(surface.width, (last + 1) & ~1);
                source = surface.mirror.data() + std::size_t(y) * surface.width;
                sourceX = 0;
                sourceWidth = surface.width;
            }
            else if ((x & 1) || ((w & 1) && last != surface.width))
                throw std::logic_error("Packed scalar write splits a texel");
            const int texels = (last - first + 1) / 2;
            rgba.assign(std::size_t(texels) * h * 4, 0);
            for (int row = 0; row < h; ++row)
                for (int column = first; column < last; ++column)
                {
                    const Uint16 value = source[std::size_t(row) * sourceWidth + (column - sourceX)];
                    auto *texel = &rgba[(std::size_t(row) * texels + (column - first) / 2) * 4 + ((column - first) & 1) * 2];
                    texel[0] = Uint8(value);
                    texel[1] = Uint8(value >> 8);
                }
            target.write({first / 2, y, texels, h}, std::move(rgba));
            return;
        }
        }
    }

    void drop(Surface &surface)
    {
#ifdef DIRECT_TERRAIN_LEGACY_GL
        if (surface.name) glDeleteTextures(1, &surface.name);
        surface.name = 0;
#endif
        surface.streamed.reset();
        surface.mirror.clear();
        surface.width = surface.height = 0;
    }

    // A device dropped a streamed texture, or its writes stopped draining.
    bool streamLost() const
    {
        std::size_t queued = 0;
        for (auto *surface : const_cast<State *>(this)->surfaces())
            if (surface->streamed)
            {
                if (surface->streamed->lost) return true;
                queued += surface->streamed->queuedBytes;
            }
        return queued > (std::size_t(96) << 20);
    }

    void upload(Draw::Texture id, int w, int h, Format format, const void *pixels, std::size_t size)
    {
        create(textures[id], id, w, h, format, false, pixels);
        bytes += size;
    }

    void uploadDevice(Draw::Texture slot, int w, int h, const std::vector<Pixel> &pixels)
    {
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> source(
            SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, const_cast<Pixel *>(pixels.data()), w * sizeof(Pixel)),
            SDL_DestroySurface);
        if (!source)
            throw std::runtime_error(SDL_GetError());
        GAGCore::TextureDescriptor descriptor;
        descriptor.budgetClass = GAGCore::TextureDescriptor::BudgetClass::TerrainPage;
        if (slot == Art || slot == Coverage)
            descriptor.sampling = GAGCore::TextureDescriptor::Sampling::Linear;
        const auto size = pixels.size() * sizeof(Pixel);
        auto &handle = handles[slot];
        if (handle) {
            if (!resources->update(handle, source.get(), ++visibilityRevision, descriptor))
                throw std::runtime_error("Direct terrain visibility exceeds the texture budget");
        } else {
            handle = resources->create(source.get(), 0, descriptor);
            if (!handle)
                throw std::runtime_error("Direct terrain exceeds the device texture budget");
            bytes += size;
        }
        uploaded += size;
    }

    void uploadDeviceVisibility()
    {
        const int packedWidth = (width + 3) / 4;
        std::vector<Pixel> pixels(std::size_t(packedWidth) * height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                pixels[std::size_t(y) * packedWidth + x / 4][x % 4] = visibility[std::size_t(y) * width + x];
        uploadDevice(Discovery, packedWidth, height, pixels);
    }

    void initializeDevice(const SceneMap &map, int maxSize)
    {
        width = map.getW();
        height = map.getH();
        if (width > maxSize / 2 || DirectTerrainData::VariantWidth > maxSize / 2)
            throw std::runtime_error("Packed direct terrain tables exceed the device texture dimensions");
        const auto data = DirectTerrainData::build(map, globalContainer->terrainCompositorOwner(assets), maxSize);
        visibility.resize(data.cells.size());

        // RGBA8 is shared by the device resource path. A record occupies two
        // adjacent texels, and one texel holds two scalar indices.
        const auto uploadRecords = [&](Draw::Texture slot, int w, int h, const std::vector<Record> &records) {
            std::vector<Pixel> pixels(records.size() * 2);
            for (std::size_t i = 0; i < records.size(); ++i)
                for (unsigned channel = 0; channel < 4; ++channel) {
                    pixels[i * 2][channel] = Uint8(records[i][channel]);
                    pixels[i * 2 + 1][channel] = Uint8(records[i][channel] >> 8);
                }
            uploadDevice(slot, w * 2, h, pixels);
        };
        const auto uploadIndices = [&](Draw::Texture slot, int w, int h, const std::vector<Uint16> &indices) {
            const int packedWidth = (w + 1) / 2;
            std::vector<Pixel> pixels(std::size_t(packedWidth) * h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const auto value = indices[std::size_t(y) * w + x];
                    auto &pixel = pixels[std::size_t(y) * packedWidth + x / 2];
                    pixel[(x % 2) * 2] = Uint8(value);
                    pixel[(x % 2) * 2 + 1] = Uint8(value >> 8);
                }
            uploadDevice(slot, packedWidth, h, pixels);
        };
        uploadRecords(Cells, width, height, data.cells);
        uploadIndices(MaskIndex, width, height, data.masks);
        uploadDevice(Art, DirectTerrainData::ArtWidth, data.artHeight, data.art);
        uploadDevice(Coverage, DirectTerrainData::AtlasWidth, data.coverageHeight, data.coverage);
        uploadIndices(Edges, DirectTerrainData::AtlasWidth, data.coverageHeight, data.edges);
        uploadRecords(Variants, DirectTerrainData::VariantWidth, data.variantHeight, data.variants);
        uploadDeviceVisibility();
        uploadDevice(ShoreCalm, width, height, data.calm);
    }

    bool glFailed() const
    {
#ifdef DIRECT_TERRAIN_LEGACY_GL
        return !streaming && glGetError() != GL_NO_ERROR;
#else
        return false;
#endif
    }

    void initialize(const SceneMap &map, DirectTerrainData &data)
    {
        width = map.getW();
        height = map.getH();
#ifdef DIRECT_TERRAIN_LEGACY_GL
        if (!streaming)
        {
            GLint units = 0;
            glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
            if (units < int(Hd::FirstUnit) + 6)
                throw std::runtime_error("Direct terrain requires fourteen texture units");
        }
#endif
        const auto count = data.cells.size();
        visibility.resize(count);
        upload(Cells, width, height, Format::Record16, data.cells.data(), count * sizeof(Record));
        upload(Discovery, width, height, Format::Luminance8, visibility.data(), count);
        upload(ShoreCalm, width, height, Format::Rgba8, data.calm.data(), data.calm.size() * sizeof(Pixel));
        cells = std::move(data.cells);
        calm = std::move(data.calm);
        cellMasks = std::move(data.masks);
        palette = std::move(data.palette);
        vertices.resize(count);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                vertices[std::size_t(y) * width + x] = map.vertexTerrainAt(x, y);
        tablesReady = true;
        if (glFailed())
            throw std::runtime_error("Direct terrain texture setup failed");
    }

    // A terrain edit: re-describe every cell whose recipe reads a changed
    // vertex, and rebuild the masks of those that change.
    // False, changing nothing, while an edited cell needs a material sheet
    // that is still loading: the tables keep drawing the cells' previous
    // materials (never a flat colour) and the edit applies once it arrives.
    bool applyEdits(const SceneMap &map, TerrainVisual::Compositor &compositor)
    {
        constexpr int halo = SoftwareTerrainCache::SourceHalo;
        std::vector<char> affected(cells.size(), 0);
        std::vector<std::size_t> changed;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto vertex = map.vertexTerrainAt(x, y);
                if (vertex == vertices[std::size_t(y) * width + x]) continue;
                changed.push_back(std::size_t(y) * width + x);
                for (int cy = y - halo - 1; cy <= y + halo; ++cy)
                    for (int cx = x - halo - 1; cx <= x + halo; ++cx)
                        affected[map.coordToIndex((cx % width + width) % width, (cy % height + height) % height)] = 1;
            }
        if (changed.empty()) return true;
        bool ready = true;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if (affected[map.coordToIndex(x, y)] && !compositor.sheetsReady(compositor.describe(map, x, y)))
                    ready = false; // Asks for every missing sheet.
        if (!ready) return false;
        for (const auto vertex : changed)
            vertices[vertex] = map.vertexTerrainAt(int(vertex % width), int(vertex / width));
        const auto &catalog = compositor.catalog();
        bool masksChanged = false;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto i = map.coordToIndex(x, y);
                if (!affected[i]) continue;
                Record record;
                Uint16 mask;
                Pixel quiet;
                const auto recipe = compositor.describe(map, x, y);
                DirectTerrainData::describeCell(palette, catalog, recipe, record, mask, quiet);
                if (record != cells[i])
                {
                    cells[i] = record;
                    write(textures[Cells], Cells, x, y, 1, 1, record.data());
                }
                if (quiet != calm[i])
                {
                    calm[i] = quiet;
                    write(textures[ShoreCalm], ShoreCalm, x, y, 1, 1, quiet.data());
                }
                // Any edit near a mixed cell can move its contours: rebuild it.
                cellMasks[i] = mask;
                hd.recipes.erase(int(i));
                if (!hd.slotOf.empty())
                {
                    if (const auto slot = hd.slotOf[i]) hd.cellOf[slot - 1] = -1;
                    setHdIndex(int(i), mask);
                }
                // The base holds the old shape too: clear it and rebuild it first.
                if (hd.base.tile)
                    invalidateBase(int(i), x, y);
                // The retired atlas holds the old shape: never fall back to it.
                if (!hd.previous.slotOf.empty() && hd.previous.slotOf[i])
                {
                    hd.previous.slotOf[i] = 0;
                    const Uint16 pending = mask;
                    write(hd.previous.index, Hd::FirstUnit + 3, x, y, 1, 1, &pending);
                }
                masksChanged = true;
            }
        // Results already queued were built from the old recipes.
        if (masksChanged) ++hd.generation;
        if (glFailed())
            throw std::runtime_error("Direct terrain edit upload failed");
        return true;
    }

    void setHdIndex(int cell, Uint16 value)
    {
        hd.slotOf[cell] = value >= DirectTerrainData::PendingCorners ? 0 : value;
        write(hd.index, Hd::FirstUnit, cell % width, cell / width, 1, 1, &value);
    }

    // A new mask atlas at another density or size. The old one becomes the
    // fallback when it shows more of the view than the existing fallback;
    // otherwise it is discarded, so rapid zooms keep the most complete one.
    // Textures are recycled between the two, so a zoom allocates nothing
    // unless the atlas grows.
    void restartHdAtlas(int tile, int size, bool retireCurrent)
    {
        auto &old = hd.previous;
        if (retireCurrent && hd.tile)
        {
            // The current atlas becomes the fallback; the new one takes the
            // old fallback's (or spare) textures.
            std::swap(hd.index, old.index);
            std::swap(hd.coverage, old.coverage);
            std::swap(hd.edges, old.edges);
            std::swap(hd.size, old.size);
            std::swap(hd.bytes, old.bytes);
            old.tile = hd.tile;
            old.slotOf = std::move(hd.slotOf);
        }
        // Every mixed cell starts pending; uniform cells need no mask.
        if (!hd.index)
            create(hd.index, Hd::FirstUnit, width, height, Format::Scalar16, false, cellMasks.data(), true);
        else
            write(hd.index, Hd::FirstUnit, 0, 0, width, height, cellMasks.data());
        if (!hd.coverage || hd.size != size)
        {
            create(hd.coverage, Hd::FirstUnit + 1, size, size, Format::Rgba8, true);
            create(hd.edges, Hd::FirstUnit + 2, size, size, Format::Scalar16, false);
            hd.size = size;
        }
        bytes -= hd.bytes;
        hd.bytes = std::size_t(size) * size * 6 + cellMasks.size() * sizeof(Uint16);
        bytes += hd.bytes;
        hd.tile = tile;
        hd.suspended = false;
        hd.slotOf.assign(cellMasks.size(), 0);
        const std::size_t slots = std::min<std::size_t>(DirectTerrainData::PendingCorners - 1, std::size_t(size / tile) * (size / tile));
        hd.cellOf.assign(slots, -1);
        hd.used.assign(slots, 0);
        // Results in flight were built for the old tiles. Recipes do not
        // depend on the density and stay. Finished base masks keep their own
        // tile: dropping them would leave their cells in flight, and the base
        // (and with it the streamed view) would never complete.
        ++hd.generation;
        {
            std::lock_guard lock(hd.mutex);
            hd.jobs.clear();
            std::erase_if(hd.done, [](const Hd::Result &result) { return !result.base; });
        }
        hd.inFlight.clear();
    }

    // At the base mask's density the base already holds every mixed cell of
    // the map, so the view draws from it alone. Its own atlas, the largest one
    // at this zoom, and the fallback are freed: every cell reads as pending,
    // which the shader resolves to the base.
    void suspendHdAtlas()
    {
        // A complete first draw at the base's density may suspend an atlas it never started.
        if (!hd.index)
            create(hd.index, Hd::FirstUnit, width, height, Format::Scalar16, false, cellMasks.data(), true);
        else
            write(hd.index, Hd::FirstUnit, 0, 0, width, height, cellMasks.data());
        auto &old = hd.previous;
        for (Surface *texture : {&hd.coverage, &hd.edges, &old.index, &old.coverage, &old.edges})
            drop(*texture);
        bytes -= hd.bytes + old.bytes;
        hd.bytes = cellMasks.size() * sizeof(Uint16);
        bytes += hd.bytes;
        old.bytes = 0;
        old.size = old.tile = 0;
        old.slotOf.clear();
        hd.size = 2048; // The next atlas starts small again.
        hd.slotOf.assign(cellMasks.size(), 0);
        hd.cellOf.clear();
        hd.used.clear();
        ++hd.generation;
        {
            std::lock_guard lock(hd.mutex);
            hd.jobs.clear();
            std::erase_if(hd.done, [](const Hd::Result &result) { return !result.base; });
        }
        hd.inFlight.clear();
        hd.suspended = true;
    }

    // The base atlas: map-shaped, zero (unbuilt) everywhere until workers
    // fill it. Large maps take smaller tiles so it stays within 2048 square.
    // The base's tile for this map, or zero where no base fits.
    int baseTile() const
    {
        int tile = 8;
        while (tile > 2 && std::max(width, height) * tile > 2048) tile /= 2;
        return std::max(width, height) * tile > maxAtlasSize ? 0 : tile;
    }
    void createBase(int centreX, int centreY)
    {
        auto &b = hd.base;
        const int tile = baseTile();
        if (!tile) return;
        b.tile = tile;
        const int w = width * tile, h = height * tile;
        create(b.coverage, Coverage, w, h, Format::Rgba8, true);
        create(b.edges, Edges, w, h, Format::Scalar16, false);
        // Device textures start cleared; Legacy GL clears in strips, since
        // one map-sized buffer of zeros would be 16 MiB.
        if (!streaming)
        {
            constexpr int StripRows = 64;
            const std::vector<Uint8> zeros(std::size_t(w) * StripRows * 4, 0);
            for (int y = 0; y < h; y += StripRows)
            {
                write(b.coverage, Coverage, 0, y, w, std::min(StripRows, h - y), zeros.data());
                write(b.edges, Edges, 0, y, w, std::min(StripRows, h - y), zeros.data());
            }
        }
        b.bytes = std::size_t(w) * h * 6;
        bytes += b.bytes;
        b.built.assign(cellMasks.size(), 0);
        b.version.assign(cellMasks.size(), 0);
        // Nearest the first view first, on the torus.
        std::vector<std::pair<int, int>> ranked;
        for (int cell = 0; cell < int(cellMasks.size()); ++cell)
            if (cellMasks[cell])
            {
                const int dx = std::abs(cell % width - centreX), dy = std::abs(cell / width - centreY);
                ranked.push_back({std::max(std::min(dx, width - dx), std::min(dy, height - dy)), cell});
            }
        std::sort(ranked.begin(), ranked.end());
        b.order.clear();
        b.order.reserve(ranked.size());
        for (const auto &[distance, cell] : ranked) b.order.push_back(cell);
        b.remaining = b.order.size();
        b.reported = b.order.empty(); // No mixed cell: nothing to build.
        b.started = SDL_GetTicks();
    }

    void invalidateBase(int cell, int x, int y)
    {
        auto &b = hd.base;
        ++b.version[cell];
        if (b.built[cell])
        {
            b.built[cell] = 0;
            ++b.remaining;
            const std::vector<Uint8> zeros(std::size_t(b.tile) * b.tile * 4, 0);
            write(b.coverage, Coverage, x * b.tile, y * b.tile, b.tile, b.tile, zeros.data());
            write(b.edges, Edges, x * b.tile, y * b.tile, b.tile, b.tile, zeros.data());
        }
        if (cellMasks[cell]) b.redo.push_back(cell);
    }

    void ensureBase(const SceneMap &map, TerrainVisual::Compositor &compositor, int centreX, int centreY, bool now)
    {
        if (hd.base.tile || !hd.base.built.empty()) return;
        createBase(centreX, centreY);
        if (now) buildBase(map, compositor);
    }
    // The base is still building for the first time (not an edit's rebuild).
    bool baseBuilding() const { return hd.base.tile && !hd.base.reported; }

    // Builds the base mask of every mixed cell now, in parallel on the
    // presentation workers, and writes it in whole-row strips. It runs once a
    // map (and after a device loses its textures), where the game would
    // otherwise draw corner splits for seconds while a camera moves.
    void buildBase(const SceneMap &map, TerrainVisual::Compositor &compositor)
    {
        auto &b = hd.base;
        if (!b.tile || b.order.empty()) return;
        const auto started = SDL_GetTicks();
        if (!hd.materials) hd.materials = compositor.cpuMaterialsForRecipes({});
        const int w = width * b.tile, h = height * b.tile;
        std::vector<Pixel> coverage(std::size_t(w) * h, Pixel{});
        std::vector<Uint16> edges(std::size_t(w) * h, 0);
        const auto materials = hd.materials;
        const int tile = b.tile;
        // Each job describes its cell (const reads of the map and catalogue)
        // and writes its own cell's texels.
        filterTiles(b.order.size(), [&](std::size_t i)
        {
            const int cell = b.order[i];
            const auto recipe = compositor.describe(map, cell % width, cell / width);
            const auto built = Hd::build(*materials, cell, recipe, tile, b.version[cell]);
            const int ox = (cell % width) * tile, oy = (cell / width) * tile;
            for (int row = 0; row < tile; ++row)
            {
                std::copy_n(built.weights.begin() + std::size_t(row) * tile, tile, coverage.begin() + std::size_t(oy + row) * w + ox);
                std::copy_n(built.edges.begin() + std::size_t(row) * tile, tile, edges.begin() + std::size_t(oy + row) * w + ox);
            }
        });
        const auto composed = SDL_GetTicks();
        constexpr int StripRows = 128;
        for (int y = 0; y < h; y += StripRows)
        {
            const int rows = std::min(StripRows, h - y);
            write(b.coverage, Coverage, 0, y, w, rows, coverage.data() + std::size_t(y) * w);
            write(b.edges, Edges, 0, y, w, rows, edges.data() + std::size_t(y) * w);
        }
        std::fill(b.built.begin(), b.built.end(), Uint8(0));
        for (const int cell : b.order) b.built[cell] = 1;
        b.cursor = b.order.size();
        b.remaining = 0;
        b.reported = true;
        std::fprintf(stderr, "DIRECT_TERRAIN_BASE cells=%zu tile=%d ms=%llu build_ms=%llu\n", b.order.size(), b.tile,
            (unsigned long long)(SDL_GetTicks() - started), (unsigned long long)(composed - started));
    }

    // Runs job(0..count-1) on this thread and, for a large batch, on helper
    // threads, returning when all are done. Only density changes and the first
    // view build large batches, so helpers are started for them alone.
    static void filterTiles(std::size_t count, const std::function<void(std::size_t)> &job)
    {
        constexpr std::size_t JobsPerHelper = 32;
        const std::size_t cores = std::max(1u, std::thread::hardware_concurrency());
        const std::size_t helpers = GAGCore::ThreadSupport::available
            ? std::min({cores - 1, std::size_t(7), count / JobsPerHelper}) : 0;
        std::atomic<std::size_t> next{0};
        const auto run = [&]
        {
            for (std::size_t i; (i = next.fetch_add(1, std::memory_order_relaxed)) < count;)
                job(i);
        };
        std::vector<std::thread> threads;
        threads.reserve(helpers);
        for (std::size_t i = 0; i < helpers; ++i) threads.emplace_back(run);
        run();
        for (auto &thread : threads) thread.join();
    }
    // The fallback has nothing left to show. Its textures stay as the spare
    // for the next zoom, so a run of zooms allocates nothing.
    void retirePreviousHdAtlas()
    {
        hd.previous.tile = 0;
        hd.previous.slotOf.clear();
        hd.previous.retiredAt = SDL_GetTicks();
    }
    // A spare the view has not needed for a while gives its memory back, up
    // to 96 MiB; the next zoom allocates textures again, without uploading.
    void releaseIdleSpare(Uint64 lifetimeMs)
    {
        auto &old = hd.previous;
        if (old.tile || !old.coverage || SDL_GetTicks() - old.retiredAt < lifetimeMs) return;
        drop(old.index);
        drop(old.coverage);
        drop(old.edges);
        bytes -= old.bytes;
        old.bytes = 0;
        old.size = 0;
    }

    void updateDiscovery(const SceneMap &map, Uint32 teams)
    {
        const auto revision = map.discoveryRevision();
        if (revision && discoveryTeams == teams && discoveryRevision == revision)
            return;
        // Discovery only grows within the sectors whose stamps moved: revisit
        // those (and the cells a sector's edge reaches) rather than the map.
        constexpr int Sector = 16;
        const auto stamps = map.discoveryStamps();
        const int sectorsWide = (width + Sector - 1) / Sector, sectorsHigh = (height + Sector - 1) / Sector;
        const bool whole = discoveryTeams != teams || !revision || stamps.size() != std::size_t(sectorsWide) * sectorsHigh ||
            discoveryStamps.size() != stamps.size();
        discoveryRevision = revision;
        discoveryTeams = teams;
        bool changed = false;
        int firstRow = height, lastRow = -1;
        const auto refresh = [&](int x, int y)
        {
            auto &pixel = visibility[map.coordToIndex(x, y)];
            const Uint8 value = map.isMapPartiallyDiscovered(x - 1, y - 1, x + 1, y + 1, teams) ? 255 : 0;
            ++discoveryStats.rebuilt;
            if (value == pixel) return;
            pixel = value;
            changed = true;
            const int row = (y % height + height) % height;
            firstRow = std::min(firstRow, row);
            lastRow = std::max(lastRow, row);
        };
        if (whole)
        {
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    refresh(x, y);
            discoveryStamps.assign(stamps.begin(), stamps.end());
        }
        else
            for (std::size_t sector = 0; sector < stamps.size(); ++sector)
            {
                if (stamps[sector] == discoveryStamps[sector]) continue;
                discoveryStamps[sector] = stamps[sector];
                const int sx = int(sector % sectorsWide) * Sector, sy = int(sector / sectorsWide) * Sector;
                // A cell shows when any of its eight neighbours is discovered.
                for (int y = sy - 1; y <= std::min(sy + Sector, height); ++y)
                    for (int x = sx - 1; x <= std::min(sx + Sector, width); ++x)
                        refresh((x + width) % width, (y + height) % height);
            }
        if (!changed) return;
        if (resources && !streaming)
            uploadDeviceVisibility();
        else
        {
            // Whole rows (the packed texture's unit), only those that changed;
            // a wrap from the last row to the first rewrites the whole map.
            write(textures[Discovery], Discovery, 0, firstRow, width, lastRow - firstRow + 1,
                  visibility.data() + std::size_t(firstRow) * width);
            if (glFailed())
                throw std::runtime_error("Direct terrain discovery upload failed");
        }
    }
};

DirectTerrain::DirectTerrain() = default;
DirectTerrain::~DirectTerrain() = default;

bool DirectTerrain::draw(GAGCore::GraphicContext &gfx, const SceneMap &map, int left, int top,
                         int right, int bottom, int viewportX, int viewportY,
                         Uint32 visibleTeams, bool wholeMap, AnimationTime animationTime)
{
    return drawInternal(gfx, map, left, top, right, bottom, viewportX, viewportY,
                        visibleTeams, wholeMap, animationTime, false, false);
}

bool DirectTerrain::drawInternal(GAGCore::GraphicContext &gfx, const SceneMap &map, int left, int top,
                         int right, int bottom, int viewportX, int viewportY,
                         Uint32 visibleTeams, bool wholeMap, AnimationTime animationTime,
                         bool complete, bool prepareOnly)
{
#ifdef DIRECT_TERRAIN_LEGACY_GL
    const auto context = SDL_GL_GetCurrentContext();
    const bool legacy = context && gfx.isLegacyGLScreen();
#else
    constexpr SDL_GLContext context = nullptr;
    constexpr bool legacy = false;
#endif
    const auto *device = gfx.renderDevice();
    const auto capabilities = device ? device->capabilities() : GAGCore::RenderDeviceCapabilities{};
    const auto resources = capabilities.directTerrain ? gfx.renderResourceOwner() : nullptr;
    if (!resources && !legacy)
        return false;
    // Devices with streamed textures draw Legacy GL's streamed pass.
    const bool streaming = resources && capabilities.directTerrainStreaming;
    const auto generation = gfx.getGLContextGeneration();
    const auto assets = map.frozenAssetBundle();
    const auto registry = map.frozenTerrainRegistry();
    const bool sameContext = state && state->context == (resources ? nullptr : context) &&
        state->resources == resources && state->generation == generation && state->streaming == streaming;
    auto &sheets = globalContainer->terrainCompositor(assets);
    const bool staleSheets = state && state->bakedFallback && state->sheetArrivals != sheets.materialArrivals();
    // The streamed pass edits its tables in place; anything else about the
    // map, or the context, starts again.
    bool edited = !staleSheets && (legacy || streaming) && sameContext && state->tablesReady &&
        state->sameMapSource(map) && state->revision != map.terrainRevision();
    // A device that dropped a streamed texture (a reset, or its budget) or
    // stopped draining its writes: start this map's textures again rather
    // than leave the streamed pass. Nothing blocks: the tables build in
    // slices and the base masks on the workers, while the view draws the
    // overview (its always-resident whole-map base beneath), and the direct
    // pass returns once every base mask is back.
    if (streaming && sameContext && state->streamLost())
    {
        std::fprintf(stderr, "DIRECT_TERRAIN_RESTART streamed textures lost or stopped draining\n");
        ++restarts;
        edited = false;
        state.reset();
    }
    // The map's material sheets load on demand: tables wait for them (pages
    // draw meanwhile), or a complete draw waits here.
    const auto sheetsReady = [&] {
        if (sheets.materialsReady(map)) return true;
        if (!complete) return false;
        sheets.finishMaterials(map);
        return true;
    };
    const auto beginBake = [&] {
        state->sheetArrivals = sheets.materialArrivals();
        state->sheetFallbacks = sheets.materialFallbacks();
    };
    const auto endBake = [&] {
        state->bakedFallback |= sheets.materialFallbacks() != state->sheetFallbacks;
    };
    if (!edited && (!state || !sameContext || !state->matchesMap(map) || staleSheets))
    {
        if (state && !sameContext)
            state->context = nullptr; // The retired context owns its old names.
        state = std::make_unique<State>();
        state->context = resources ? nullptr : context;
        state->resources = resources;
        state->generation = generation;
        state->identity = map.identity();
        state->revision = map.terrainRevision();
        state->seed = map.terrainSeed();
        state->assets = assets;
        state->registry = registry;
        state->streaming = streaming;
        state->animationPeriod = TerrainVisual::animationPeriod(sheets.catalog());
        // The art atlas grows under the desktop budget alone, on every
        // renderer, so a view draws its variants at the same density on each
        // (see prepare). A mobile budget, or a device without 4096 textures,
        // keeps it at 2048.
        const int textureLimit = streaming ? capabilities.maximumTextureSize : 4096;
        state->maxArtAtlasSize = textureLimit >= 4096 &&
            GAGCore::platformRenderBudget().total >= GAGCore::RenderBudget::desktop().total ? 4096 : 2048;
    }
    if (state->failed)
        return false;

    if (resources && !streaming)
    {
        if (prepareOnly) return false;
        try
        {
            if (!state->handles[State::Cells])
            {
                if (!sheetsReady()) return false;
                beginBake();
                state->initializeDevice(map, capabilities.maximumTextureSize);
                endBake();
            }
            if (!wholeMap)
                state->updateDiscovery(map, visibleTeams);
        }
        catch (const std::exception &error)
        {
            std::fprintf(stderr, "DIRECT_TERRAIN_FALLBACK %s\n", error.what());
            state->releaseDeviceTextures();
            state->bytes = 0;
            state->failed = true;
            return false;
        }
        auto data = std::make_shared<GAGCore::DirectTerrainDraw>();
        for (unsigned i = 0; i < data->textures.size(); ++i)
            data->textures[i] = resources->snapshot(state->handles[i]);
        data->wholeMap = wholeMap;
        data->animationTime = animationTime.wrapped(state->animationPeriod);

        GAGCore::DrawPacket packet;
        packet.kind = GAGCore::DrawPacket::Kind::DirectTerrain;
        packet.blend = GAGCore::DrawPacket::Blend::Premultiplied;
        packet.directTerrain = std::move(data);
        const auto corner = [&](int x, int y) {
            return SDL_Vertex{{float(x * TilePixels), float(y * TilePixels)}, {1, 1, 1, 1},
                              {float(x + viewportX), float(y + viewportY)}};
        };
        const auto a = corner(left, top), b = corner(right + 1, top);
        const auto c = corner(right + 1, bottom + 1), d = corner(left, bottom + 1);
        packet.vertices = {a, b, c, a, c, d};
        GAGCore::Sprite::flushBatches(&gfx);
        if (!gfx.submitDevicePacket(std::move(packet)))
            return false;
        gfx.countRenderBatchDraw();
        ++state->frames;
        return true;
    }

    // The streamed pass draws through prepare/drawPrepared; here it only
    // builds and edits the per-cell tables. A complete draw (editor, capture)
    // builds them at once.
    std::optional<RestoreGL> restore;
#ifdef DIRECT_TERRAIN_LEGACY_GL
    if (!streaming)
    {
        GAGCore::Sprite::flushBatches(&gfx);
        if (auto *batch = gfx.getRenderBatch())
            batch->stateChange();
        restore.emplace();
    }
#endif
    constexpr auto slice = std::chrono::milliseconds(3);
    const auto deadline = std::chrono::steady_clock::now() + slice;
    try
    {
        if (!state->tablesReady)
        {
            if (!state->building)
            {
                if (!sheetsReady()) return false;
                beginBake();
                int maxSize = capabilities.maximumTextureSize;
#ifdef DIRECT_TERRAIN_LEGACY_GL
                if (!streaming)
                {
                    GLint glMax = 0;
                    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &glMax);
                    maxSize = glMax;
                }
#endif
                if (streaming && map.getW() * 2 > maxSize)
                    throw std::runtime_error("Packed direct terrain tables exceed the device texture dimensions");
                state->building.emplace(DirectTerrainData::begin(
                    map, globalContainer->terrainCompositorOwner(assets), maxSize, DirectTerrainData::Mode::Tables));
            }
            bool ready;
            do
                ready = state->building->advance();
            while (!ready && (complete || std::chrono::steady_clock::now() < deadline));
            if (!ready)
                return false;
            auto data = state->building->take();
            state->building.reset();
            endBake();
            state->initialize(map, data);
        }
        else if (edited)
        {
            // An edit that paints a material whose sheet is still loading
            // waits for it; the tables keep the previous terrain meanwhile.
            if (!state->bakedFallback) beginBake();
            if (state->applyEdits(map, sheets))
                state->revision = map.terrainRevision();
            endBake();
        }
        if (!wholeMap)
            state->updateDiscovery(map, visibleTeams);
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "DIRECT_TERRAIN_FALLBACK %s\n", error.what());
        state->failed = true;
        state->building.reset();
        state->hd.stopWorkers();
        state->releaseSurfaces();
        state->tablesReady = false;
        state->bytes = 0;
        return false;
    }
    return prepareOnly;
}

bool DirectTerrain::prepare(GAGCore::GraphicContext &gfx, const SceneMap &map,
    int left, int top, int right, int bottom, int viewportX, int viewportY,
    Uint32 visibleTeams, bool wholeMap, AnimationTime animationTime, bool complete)
{
    if (left > right || top > bottom) return false;
    if (!drawInternal(gfx, map, left, top, right, bottom, viewportX, viewportY,
                      visibleTeams, wholeMap, animationTime, complete, true))
        return false;
    auto &out = state->animation;
    out.regions.clear();
    if (out.failed) return false;
    auto &compositor = globalContainer->terrainCompositor(map.frozenAssetBundle());
    // Match the page compositor's streaming window. Narrowing it between the
    // two passes would evict and decode the fringe variants every frame.
    constexpr int pageTiles = SoftwareTerrainCache::ChunkTiles;
    const auto page = [](int cell) { return int(std::floor(double(cell) / pageTiles)); };
    // Artwork is sampled near the display density, 8 to 128 pixels a cell.
    // Native material and decor frames are the HD frames reduced to 32 pixels
    // a cell, so HD frames only help above that: a zoomed-out view, which
    // shows the most variants, streams none of them.
    int pixels = 8;
    const double density = gfx.mapTransformScale() * gfx.getRasterScale();
    while (pixels < 128 && pixels < 32 * density) pixels *= 2;
    const bool highResolutionFrames = pixels > 32;
    const auto declare = [&] {
        compositor.declareVisible(animationTime.steps, map,
            page(left + viewportX) * pageTiles, page(top + viewportY) * pageTiles,
            (page(right + viewportX) + 1) * pageTiles - 1,
            (page(bottom + viewportY) + 1) * pageTiles - 1, highResolutionFrames);
    };
    declare();
    // A complete draw is shown once, so it waits for streamed HD artwork.
    if (complete)
    {
        GAGCore::Sprite::finishHighResolution();
        declare();
    }
    const auto &catalog = compositor.catalog();
    std::set<unsigned> wanted;
    // Every discovered visible cell, in row runs, and the variants they use.
    for (int y = top; y <= bottom; ++y)
    {
        int run = -1;
        for (int x = left; x <= right + 1; ++x)
        {
            const bool shown = x <= right && (wholeMap || map.isMapPartiallyDiscovered(
                x + viewportX - 1, y + viewportY - 1, x + viewportX + 1, y + viewportY + 1, visibleTeams));
            if (shown)
                for (auto slot : state->cells[map.coordToIndex(x + viewportX, y + viewportY)])
                    if (slot) wanted.insert(slot);
            if (shown && run < 0) run = x;
            if (!shown && run >= 0)
            {
                out.regions.push_back({run, y, x - run, 1});
                run = -1;
            }
        }
    }
    if (wanted.empty()) return true;

    // Each visible variant at that density, in one 16 MiB atlas: two
    // endpoint tiles for an animated variant, one for a static one.
    std::size_t tilesWanted = 0;
    for (auto slot : wanted)
        tilesWanted += State::Animation::span(catalog.materials[state->palette.sources[slot].material]);
    const auto restartAtlas = [&](int size)
    {
        // The atlas and its variant table start again at the new size.
        state->drop(out.art);
        state->drop(out.variants);
        state->bytes -= out.bytes;
        out.bytes = out.variantBytes = 0;
        out.size = size;
        out.entries.clear();
        out.tiles = {};
        out.pixels = 0;
        state->uploadedVariants = 0;
    };
    constexpr int SmallAtlasSize = 2048;
    const auto tilesAt = [&](int size) { return std::size_t(size / (pixels + 2)) * (size / (pixels + 2)); };
    // The atlas this view wants: a density change refilters every tile
    // anyway, so take the small atlas again when the view fits it twice over
    // (a pan does not grow it straight back), and grow once when it is full.
    int wantedSize = out.size;
    if (out.size > SmallAtlasSize && out.pixels != pixels && tilesWanted * 2 <= tilesAt(SmallAtlasSize))
        wantedSize = SmallAtlasSize;
    const int densityPixels = pixels;
    const char *coarser = "";
    if (wantedSize < state->maxArtAtlasSize && tilesWanted > tilesAt(wantedSize))
    {
        // The new atlas is charged to the platform's combined budget beside
        // the one still drawing; grow only while that leaves the budget a
        // margin. A device counts its resident resources. Legacy GL has no
        // device budget, so it counts as one would hold it: its direct terrain
        // and its other textures up to the General class a device keeps them in.
        const auto *device = gfx.renderDevice();
        const auto &budget = GAGCore::platformRenderBudget();
        const std::size_t grown = std::size_t(state->maxArtAtlasSize) * state->maxArtAtlasSize * sizeof(Pixel);
        const std::size_t resident = state->streaming
            ? (device ? device->telemetry().resourceBytes : 0)
            : state->bytes + std::min(GAGCore::RenderMemory::trackedGpuBytes(),
                                      budget.classLimit(GAGCore::TextureDescriptor::BudgetClass::General));
        const std::size_t total = budget.total;
        // An atlas already growing (filled over frames in nextArt) is counted
        // in `resident`: keep growing it rather than drop it half filled.
        const bool growing = state->nextArt.size == state->maxArtAtlasSize;
        if (growing || resident + 2 * grown + total / 8 <= total)
            wantedSize = state->maxArtAtlasSize;
        else
            coarser = "device budget";
    }
    while (pixels > 8 && tilesWanted > tilesAt(wantedSize))
    {
        pixels /= 2;
        if (!*coarser) coarser = wantedSize < state->maxArtAtlasSize ? "atlas size" : "atlas full";
    }
    // One line whenever the view's art admission (atlas, tile density or the
    // reason it is coarser than the display wants) changes, so a renderer
    // that draws coarser terrain than another says why.
    const std::tuple<int, int, int, const char *> admission{wantedSize, densityPixels, pixels, coarser};
    if (admission != state->admission)
    {
        state->admission = admission;
        std::fprintf(stderr, "DIRECT_TERRAIN_ADMISSION renderer=%s atlas=%d wanted_tile=%d tile=%d tiles=%zu "
                     "capacity=%zu max_atlas=%d coarser=%s\n",
                     state->streaming ? "device" : "legacy-gl", wantedSize, densityPixels, pixels, tilesWanted,
                     tilesAt(wantedSize), state->maxArtAtlasSize, *coarser ? coarser : "-");
    }
    // An interactive view whose atlas already draws builds the new one in
    // `nextArt` over several frames and keeps drawing the old one meanwhile;
    // a complete draw, or a view with nothing yet, rebuilds it at once.
    auto &next = state->nextArt;
    const auto dropNext = [&]
    {
        if (!next.art && !next.variants && next.entries.empty()) return;
        state->drop(next.art);
        state->drop(next.variants);
        state->bytes -= next.bytes;
        next = {};
    };
    const bool transition = !complete && out.art && out.pixels && (out.pixels != pixels || out.size != wantedSize);
    if (!transition)
    {
        if (next.pixels) // The view came back to the drawing atlas: its records again.
        {
            for (const auto &[slot, entry] : out.entries)
                state->palette.variants[slot * DirectTerrainData::VariantRecords][0] = Uint16(entry.tile);
        }
        dropNext();
        if (out.size != wantedSize) restartAtlas(wantedSize);
    }
    else if (next.size != wantedSize || (next.pixels && next.pixels != pixels))
    {
        dropNext();
        next.size = wantedSize;
    }
    auto &art = transition ? next : out;
    const int atlasSize = art.size, stride = pixels + 2;
    // Whole pairs: an odd last tile is never claimed. Records hold sixteen-bit
    // tile numbers below AbsentTile.
    const unsigned tiles = std::min(unsigned((atlasSize / stride) * (atlasSize / stride)), unsigned(State::AbsentTile)) / 2 * 2;
    if (tilesWanted > tiles) { out.regions.clear(); return false; }
    std::optional<RestoreGL> restore;
#ifdef DIRECT_TERRAIN_LEGACY_GL
    if (!state->streaming)
    {
        GAGCore::Sprite::flushBatches(&gfx);
        if (auto *batch = gfx.getRenderBatch()) batch->stateChange();
        restore.emplace();
    }
#endif
    try
    {
        state->updateLowArt(compositor);
        if (!art.art)
        {
            state->create(art.art, State::Art, atlasSize, atlasSize, Format::Rgba8, true);
            art.bytes = std::size_t(atlasSize) * atlasSize * sizeof(Pixel);
            art.variantBytes = 0;
            state->bytes += art.bytes;
        }
        // The drawing atlas's variant table, again whenever an edit added
        // variants, during a transition too: every visible cell's variant must
        // have a record, its tile here or AbsentTile (a filling atlas gets its
        // own table when it takes over).
        auto &variants = state->palette.variants;
        if (out.art && state->uploadedVariants != variants.size())
        {
            const int rows = int((variants.size() + DirectTerrainData::VariantWidth - 1) / DirectTerrainData::VariantWidth);
            const auto padded = state->variantTable(out, rows);
            state->create(out.variants, State::Variants, DirectTerrainData::VariantWidth, rows, Format::Record16,
                          false, padded.data());
            const auto variantBytes = padded.size() * sizeof(Record);
            state->bytes += variantBytes - out.variantBytes;
            out.bytes += variantBytes - out.variantBytes;
            out.variantBytes = variantBytes;
            state->variantRows = rows;
            state->uploadedVariants = variants.size();
        }
        if (art.pixels != pixels)
        {
            art.entries.clear();
            art.tiles.reset(tiles);
            art.pixels = pixels;
            art.serial = ++state->artSerial;
        }
        // Tiles the workers finished: into the atlas they were filtered for,
        // if it still holds their variants at that density.
        if (state->artBatch && !state->artBatch->remaining.load(std::memory_order_acquire))
        {
            auto batch = std::move(state->artBatch);
            auto *target = out.serial == batch->serial ? &out : next.serial == batch->serial ? &next : nullptr;
            if (target && target->art && target->pixels == batch->pixels)
            {
                const int targetStride = batch->pixels + 2;
                const int targetColumns = target->size / targetStride;
                for (auto &job : batch->jobs)
                {
                    auto found = target->entries.find(job.slot);
                    if (found == target->entries.end() || found->second.tile != job.first) continue;
                    if (job.ahead)
                    {
                        found->second.ahead = {job.phase, job.stamp, std::move(job.tile)};
                        continue;
                    }
                    const unsigned index = job.first + job.parity;
                    state->write(target->art, State::Art, int(index % targetColumns) * targetStride,
                                 int(index / targetColumns) * targetStride, targetStride, targetStride, job.tile.data());
                    found->second.phases[job.parity] = job.phase;
                    found->second.stamps[job.parity] = job.stamp;
                }
            }
        }
        const bool batchBusy = bool(state->artBatch);
        auto background = std::make_shared<State::ArtBatch>();
        background->pixels = pixels;
        background->serial = art.serial;
        // Three passes: claim atlas tiles and gather each tile's source here,
        // box-filter the tiles on worker threads, then upload them here. A
        // density change refilters every visible variant (over a thousand
        // tiles from 128-pixel HD frames), which one thread took 110 ms for.
        struct TileJob
        {
            State::Animation::Entry *entry;
            unsigned parity, phase;
            TerrainVisual::Compositor::PhaseSource stamp;
            const Uint32 *surfacePixels; // The frame's own pixels, or null and:
            TerrainVisual::Compositor::MaterialPixels input; // a prepared copy.
            std::vector<Pixel> border, tile;
        };
        std::vector<TileJob> jobs;
        std::vector<std::pair<unsigned, State::Animation::Entry *>> added;
        std::unordered_set<unsigned> fresh; // The slots in `added`.
        // A filling atlas takes a bounded number of tiles a frame, and so do
        // animation steps of variants the atlas already holds (they draw their
        // previous phase a frame longer); a variant new to the drawing atlas
        // gets its tiles at once, since its record is written with them.
        constexpr std::size_t TransitionTiles = 192, PhaseTiles = 192;
        bool filled = true;
        const auto spanOf = [&](unsigned slot)
        { return State::Animation::span(catalog.materials[state->palette.sources[slot].material]); };
        const auto markAbsent = [&](unsigned slot)
        {
            auto record = state->palette.variants[slot * DirectTerrainData::VariantRecords];
            record[0] = State::AbsentTile;
            if (slot < state->tableTiles.size()) state->tableTiles[slot] = State::AbsentTile;
            const auto recordIndex = slot * DirectTerrainData::VariantRecords;
            state->write(out.variants, State::Variants, int(recordIndex % DirectTerrainData::VariantWidth),
                         int(recordIndex / DirectTerrainData::VariantWidth), 1, 1, record.data());
        };
        // A visible variant the drawing atlas holds without tiles yet (it was
        // claimed while not visible) gets them now, with its record, as a new one.
        if (!transition)
            for (auto slot : wanted)
                if (const auto found = out.entries.find(slot);
                    found != out.entries.end() && !State::filledTiles(found->second, spanOf(slot) == 2) &&
                    fresh.insert(slot).second)
                    added.push_back({slot, &found->second});
        // Tiles for variants new to the atlas, animated ones first since they
        // need whole pairs, evicting variants the view no longer shows.
        for (const unsigned span : {2u, 1u})
            for (auto slot : wanted)
            {
                if (spanOf(slot) != span || art.entries.contains(slot)) continue;
                unsigned tile = 0;
                while (!art.tiles.claim(slot, span, tile))
                {
                    const auto idle = std::find_if(art.entries.begin(), art.entries.end(),
                        [&](const auto &item) { return !wanted.contains(item.first); });
                    if (idle != art.entries.end())
                    {
                        art.tiles.release(idle->second.tile, spanOf(idle->first));
                        // The drawing atlas's record leaves the tile it no longer owns.
                        if (!transition && out.variants)
                            markAbsent(idle->first);
                        art.entries.erase(idle);
                        continue;
                    }
                    // Every free tile is the half of a pair whose other half
                    // holds a static variant: move one such variant next to
                    // another, freeing its pair, and refilter it. The tile
                    // count check leaves at least two free tiles for a pair
                    // still to claim.
                    unsigned moved = 0;
                    if (!art.tiles.freePair(moved, tile))
                        throw std::logic_error("Animated terrain atlas has no room");
                    auto &entry = art.entries.at(moved);
                    entry.tile = tile;
                    entry.phases = {~0u, ~0u};
                    entry.stamps = {};
                    if (fresh.insert(moved).second)
                        added.push_back({moved, &entry});
                }
                auto &entry = art.entries.emplace(slot, State::Animation::Entry{tile}).first->second;
                fresh.insert(slot);
                added.push_back({slot, &entry});
            }
        for (auto slot : wanted)
        {
            auto &entry = art.entries.at(slot);
            const bool isFresh = fresh.contains(slot);
            const auto source = state->palette.sources[slot];
            const auto &material = catalog.materials[source.material];
            const unsigned absolutePhase = unsigned(animationTime.steps) / material.animationTicks;
            const unsigned endpoints = material.animationFrames > 1 ? 2 : 1;
            for (unsigned ahead = 0; ahead < endpoints; ++ahead)
            {
                const unsigned phase = (absolutePhase + ahead) % material.animationFrames;
                const unsigned parity = endpoints == 2 ? (absolutePhase + ahead) % 2 : 0;
                // Below the HD threshold the native frame is the source even while
                // an HD frame is still resident: sixteen times fewer pixels to
                // filter, and a stamp that outlives the HD frame's eviction.
                const bool native = !highResolutionFrames;
                const auto stamp = State::artStamp(compositor, source, phase, native);
                if (entry.phases[parity] == phase && entry.stamps[parity] == stamp) continue;
                // An animation step whose endpoint the workers filtered ahead:
                // written now, so the crossfade never draws a stale endpoint.
                if (entry.ahead.phase == phase && entry.ahead.stamp == stamp &&
                    entry.ahead.tile.size() == std::size_t(stride) * stride)
                {
                    jobs.push_back({&entry, parity, phase, stamp, nullptr, {}, {}, std::move(entry.ahead.tile)});
                    entry.ahead = {};
                    continue;
                }
                // A variant new to the drawing atlas gets its tiles now, with
                // its record; animation steps and a filling atlas go to the
                // workers, one batch at a time, the old tiles drawing meanwhile.
                const bool async = !complete && (transition || !isFresh);
                if (async && (batchBusy || background->jobs.size() >= (transition ? TransitionTiles : PhaseTiles)))
                {
                    filled = false;
                    continue;
                }
                // Shipped materials are sampled in place; art that takes its
                // border from another variant comes as a prepared copy.
                const auto *surface = compositor.phaseSurface(source.material, source.variant, phase, native);
                TerrainVisual::Compositor::MaterialPixels input;
                if (!surface)
                {
                    input = compositor.phasePixels(source.material, source.variant, phase, native);
                    if (input.size() != std::size_t(stamp.size) * stamp.size)
                        throw std::runtime_error("Animated terrain source changed dimensions");
                }
                else if (surface->w != stamp.size)
                    throw std::runtime_error("Animated terrain source changed dimensions");
                if (async)
                {
                    // The workers read a copy: the frame may go before they run.
                    if (surface)
                    {
                        input.resize(std::size_t(stamp.size) * stamp.size);
                        const auto *argb = static_cast<const Uint32 *>(surface->pixels);
                        for (std::size_t i = 0; i < input.size(); ++i)
                            input[i] = TerrainVisual::artPixel(argb[i]);
                    }
                    background->jobs.push_back({slot, entry.tile, parity, phase, stamp, std::move(input),
                        DirectTerrainData::artBorder(compositor, source, phase, native, pixels), {}});
                    filled = false;
                    continue;
                }
                jobs.push_back({&entry, parity, phase, stamp,
                                surface ? static_cast<const Uint32 *>(surface->pixels) : nullptr, std::move(input),
                                DirectTerrainData::artBorder(compositor, source, phase, native, pixels), {}});
            }
            // While the current endpoints draw, the workers filter the one the
            // next animation step needs (it replaces the current phase's tile).
            if (endpoints == 2 && !complete && !transition && !batchBusy && background->jobs.size() < PhaseTiles)
            {
                const unsigned phase = (absolutePhase + 2) % material.animationFrames;
                const unsigned parity = absolutePhase % 2;
                const auto stamp = State::artStamp(compositor, source, phase, !highResolutionFrames);
                const bool held = entry.phases[parity] == phase && entry.stamps[parity] == stamp;
                const bool fetched = entry.ahead.phase == phase && entry.ahead.stamp == stamp;
                if (!held && !fetched)
                {
                    const auto *surface = compositor.phaseSurface(source.material, source.variant, phase, !highResolutionFrames);
                    TerrainVisual::Compositor::MaterialPixels input;
                    if (!surface)
                        input = compositor.phasePixels(source.material, source.variant, phase, !highResolutionFrames);
                    else if (surface->w == stamp.size)
                    {
                        input.resize(std::size_t(stamp.size) * stamp.size);
                        const auto *argb = static_cast<const Uint32 *>(surface->pixels);
                        for (std::size_t i = 0; i < input.size(); ++i)
                            input[i] = TerrainVisual::artPixel(argb[i]);
                    }
                    if (input.size() == std::size_t(stamp.size) * stamp.size)
                        background->jobs.push_back({slot, entry.tile, parity, phase, stamp, std::move(input),
                            DirectTerrainData::artBorder(compositor, source, phase, !highResolutionFrames, pixels), {}, true});
                }
            }
        }
        // Box-filter each block in premultiplied colour. Point samples at a
        // block's corner would read a tile's two edges asymmetrically, so
        // neighbouring variants, blended to meet at their borders, would not.
        // Workers only read the gathered sources and write their own tiles.
        const auto filter = [pixels](TileJob &job)
        {
            if (job.tile.empty()) // Not filtered ahead (Entry::ahead).
                State::filterTile(pixels, job.stamp.size, job.surfacePixels, job.input, job.border, job.tile);
        };
        state->filterTiles(jobs.size(), [&](std::size_t i) { filter(jobs[i]); });
        if (!background->jobs.empty())
        {
            constexpr std::size_t JobsPerTask = 16;
            const std::size_t tasks = (background->jobs.size() + JobsPerTask - 1) / JobsPerTask;
            background->remaining.store(tasks, std::memory_order_relaxed);
            state->artBatch = background;
            for (std::size_t task = 0; task < tasks; ++task)
                BackgroundWork::submit([background, task]
                {
                    const std::size_t last = std::min(background->jobs.size(), (task + 1) * JobsPerTask);
                    for (std::size_t i = task * JobsPerTask; i < last; ++i)
                    {
                        auto &job = background->jobs[i];
                        State::filterTile(background->pixels, job.stamp.size, nullptr, job.input, job.border, job.tile);
                    }
                    background->remaining.fetch_sub(1, std::memory_order_acq_rel);
                });
        }
        const int columns = atlasSize / stride;
        for (auto &job : jobs)
        {
            const unsigned index = job.entry->tile + job.parity;
            state->write(art.art, State::Art, int(index % columns) * stride, int(index / columns) * stride,
                         stride, stride, job.tile.data());
            job.entry->phases[job.parity] = job.phase;
            job.entry->stamps[job.parity] = job.stamp;
        }
        for (const auto &[slot, entry] : added)
        {
            const auto source = state->palette.sources[slot];
            const auto &material = catalog.materials[source.material];
            const auto recordIndex = slot * DirectTerrainData::VariantRecords;
            auto &record = state->palette.variants[recordIndex];
            record[0] = Uint16(entry->tile);
            // Parity addresses the streamed current/next endpoints independently
            // of the material's original loop length, including odd-length loops.
            record[1] = Uint16(material.animationFrames > 1 ? 2 : 1);
            if (!transition)
            {
                state->write(out.variants, State::Variants, int(recordIndex % DirectTerrainData::VariantWidth),
                             int(recordIndex / DirectTerrainData::VariantWidth), 1, 1, record.data());
                if (slot < state->tableTiles.size()) state->tableTiles[slot] = record[0];
            }
        }
        // Every visible variant is in the filling atlas: it takes over, with
        // a variant table of its records.
        if (transition && filled)
        {
            state->drop(out.art);
            state->drop(out.variants);
            state->bytes -= out.bytes;
            auto regions = std::move(out.regions);
            out = std::move(next);
            next = {};
            out.regions = std::move(regions);
            const int rows = int((variants.size() + DirectTerrainData::VariantWidth - 1) / DirectTerrainData::VariantWidth);
            const auto padded = state->variantTable(out, rows);
            state->create(out.variants, State::Variants, DirectTerrainData::VariantWidth, rows, Format::Record16,
                          false, padded.data());
            out.variantBytes = padded.size() * sizeof(Record);
            out.bytes += out.variantBytes;
            state->bytes += out.variantBytes;
            state->variantRows = rows;
            state->uploadedVariants = variants.size();
        }
        // What the visible variants draw: the drawing atlas's tile, or the
        // low-resolution art. A stale record (a tile the atlas does not hold
        // for it) would draw another variant's art: never.
        state->artStats = {wanted.size(), 0, 0};
        for (auto slot : wanted)
        {
            const Uint16 drawn = slot < state->tableTiles.size() ? state->tableTiles[slot] : State::AbsentTile;
            const auto found = out.entries.find(slot);
            const bool holds = found != out.entries.end() && State::filledTiles(found->second, spanOf(slot) == 2);
            if (drawn == State::AbsentTile) ++state->artStats.absent;
            else if (!holds || drawn != found->second.tile) ++state->artStats.stale;
        }
        if (state->glFailed())
            throw std::runtime_error("Animated terrain source upload failed");
        // Masks for every visible mixed cell at the display density.
        if (state->hd.failed) { out.regions.clear(); return false; }
        int tile = 8;
        while (tile < 128 && tile < 32 * density) tile *= 2;
        prepareHdMasks(map, left, top, right, bottom, viewportX, viewportY, visibleTeams, wholeMap, tile, complete);
        if (state->hd.failed) { out.regions.clear(); return false; }
        // Streamed devices never draw a mixed cell as a corner split for want of a
        // base mask: until the base is back the view draws the overview.
        if (state->streaming && state->baseBuilding()) { out.regions.clear(); state->hd.split = 0; return false; }
        return true;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "ANIMATED_TERRAIN_FALLBACK %s\n", error.what());
        state->drop(state->nextArt.art);
        state->drop(state->nextArt.variants);
        state->bytes -= state->nextArt.bytes;
        state->nextArt = {};
        state->drop(out.art);
        state->drop(out.variants);
        state->bytes -= out.bytes;
        out = {};
        out.failed = true;
        out.regions.clear();
        return false;
    }
}

std::size_t DirectTerrain::pendingHdMasks() const
{
    return state ? state->hd.pending : 0;
}

void DirectTerrain::preload(GAGCore::GraphicContext &gfx, const SceneMap &map)
{
    const bool legacy = gfx.isLegacyGLScreen() && legacyPassAvailable();
    if (!legacy && !streamedPassAvailable(gfx)) return;
    const auto started = SDL_GetTicks();
    // The base build below creates and fills textures on the terrain's own
    // units. Legacy GL draws sprites and fills with fixed-function state
    // that assumes unit 0 is active and the cached bindings are current, so
    // leave every unit, binding and program as found, as each draw does.
    std::optional<RestoreGL> restore;
#ifdef DIRECT_TERRAIN_LEGACY_GL
    if (legacy)
    {
        GAGCore::Sprite::flushBatches(&gfx);
        if (auto *batch = gfx.getRenderBatch()) batch->stateChange();
        restore.emplace();
    }
#endif
    // A complete prepare-only pass builds the tables at once; then the base.
    if (!drawInternal(gfx, map, 0, 0, 0, 0, 0, 0, 0, true, 0, true, true) || !state || !state->tablesReady) return;
    auto &compositor = globalContainer->terrainCompositor(map.frozenAssetBundle());
    state->ensureBase(map, compositor, state->width / 2, state->height / 2, true);
    // Every variant's low-resolution art, so no visible frame builds it.
    state->updateLowArt(compositor);
    std::fprintf(stderr, "DIRECT_TERRAIN_PRELOAD ms=%llu\n", (unsigned long long)(SDL_GetTicks() - started));
}

std::size_t DirectTerrain::splitCells() const
{
    return state ? state->hd.split : 0;
}

std::size_t DirectTerrain::pendingBaseMasks() const
{
    if (!state || !state->hd.base.tile) return 0;
    return state->hd.base.remaining + state->hd.base.redo.size();
}

void DirectTerrain::prepareHdMasks(const SceneMap &map, int left, int top, int right, int bottom,
    int viewportX, int viewportY, Uint32 visibleTeams, bool wholeMap, int tile, bool complete)
{
    auto &hd = state->hd;
    const int width = state->width, height = state->height;
#ifdef DIRECT_TERRAIN_LEGACY_GL
    if (!state->streaming && !hd.program)
    {
        hd.program = glCreateProgram();
        const auto attach = [&](GLenum type, const char *source) {
            const GLuint shader = compile(type, source);
            glAttachShader(hd.program, shader);
            glDeleteShader(shader);
        };
        attach(GL_VERTEX_SHADER, Vertex);
        const auto fragment = std::string("#version 120\n#define DIRECT_TERRAIN_ANIMATION\n#define DIRECT_TERRAIN_HD_MASKS\n") +
            GAGCore::DirectTerrainFragment;
        attach(GL_FRAGMENT_SHADER, fragment.c_str());
        glLinkProgram(hd.program);
        GLint linked = GL_FALSE;
        glGetProgramiv(hd.program, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            char error[512]{};
            glGetProgramInfoLog(hd.program, sizeof(error), nullptr, error);
            std::fprintf(stderr, "DIRECT_HD_FALLBACK %s\n", error);
            hd.failed = true;
            return;
        }
        glUseProgram(hd.program);
        for (unsigned unit = 0; unit < State::Count; ++unit)
            glUniform1i(glGetUniformLocation(hd.program, State::Draw::Samplers[unit]), unit);
        glUniform1i(glGetUniformLocation(hd.program, "hdIndex"), State::Hd::FirstUnit);
        glUniform1i(glGetUniformLocation(hd.program, "hdCoverage"), State::Hd::FirstUnit + 1);
        glUniform1i(glGetUniformLocation(hd.program, "hdEdges"), State::Hd::FirstUnit + 2);
        glUniform1i(glGetUniformLocation(hd.program, "hdPreviousIndex"), State::Hd::FirstUnit + 3);
        glUniform1i(glGetUniformLocation(hd.program, "hdPreviousCoverage"), State::Hd::FirstUnit + 4);
        glUniform1i(glGetUniformLocation(hd.program, "hdPreviousEdges"), State::Hd::FirstUnit + 5);
        // The base atlas takes the units the native masks leave unused here.
        glUniform1i(glGetUniformLocation(hd.program, "hdBaseCoverage"), State::Coverage);
        glUniform1i(glGetUniformLocation(hd.program, "hdBaseEdges"), State::Edges);
        const char *names[] = {"mapSize", "artSize", "coverageSize", "variantSize", "animationTime",
                               "wholeMap", "artTilePixels", "hdCoverageSize", "hdTilePixels",
                               "hdPreviousSize", "hdPreviousTile", "hdBaseSize", "hdBaseTile"};
        for (unsigned i = 0; i < hd.uniforms.size(); ++i)
            hd.uniforms[i] = glGetUniformLocation(hd.program, names[i]);
        glUniform2f(hd.uniforms[0], float(width), float(height));
    }
#endif
    hd.ready = true;
    // Back to pending: the cell draws its old mask, or else its base mask or
    // corner split, until a new mask lands.
    const auto release = [&](int cell) { state->setHdIndex(cell, state->cellMasks[cell]); };
    // Every visible mixed cell must fit in the atlas with headroom for a pan,
    // so a wide view takes coarser tiles rather than leaving some cells native.
    int size = hd.size;
    auto &compositor = globalContainer->terrainCompositor(map.frozenAssetBundle());
    const int centreX = ((left + right) / 2 + viewportX) % width, centreY = ((top + bottom) / 2 + viewportY) % height;
    // A complete draw at the base's density or below draws from the base:
    // build it first, rather than this view's masks and then the base too.
    const int wholeMapTile = state->baseTile();
    if (complete && wholeMapTile && tile <= wholeMapTile)
        state->ensureBase(map, compositor, centreX, centreY, true);
    // Once the base mask covers the map it serves any view at its density or
    // below. Edits there rebuild their base cells; the atlas stays away.
    const bool baseServes = hd.base.tile && tile <= hd.base.tile &&
        (hd.suspended || (!hd.base.remaining && hd.base.redo.empty()));
    if (baseServes)
    {
        if (!hd.suspended) state->suspendHdAtlas();
    }
    else
    {
        std::size_t mixed = 0, current = 0, previous = 0;
        for (int y = top; y <= bottom; ++y)
            for (int x = left; x <= right; ++x)
            {
                const int cell = map.coordToIndex(x + viewportX, y + viewportY);
                if (!state->cellMasks[cell]) continue;
                ++mixed;
                current += !hd.slotOf.empty() && hd.slotOf[cell];
                previous += !hd.previous.slotOf.empty() && hd.previous.slotOf[cell];
            }
        const auto fits = [&](int atlas) { return std::size_t(atlas / tile) * (atlas / tile) >= mixed + mixed / 4; };
        // A density change rebuilds the atlas anyway: take the small one again
        // when the view fits it twice over, so a pan does not grow it straight back.
        constexpr int SmallAtlasSize = 2048;
        if (size > SmallAtlasSize && hd.tile != tile &&
            std::size_t(SmallAtlasSize / tile) * (SmallAtlasSize / tile) >= mixed * 2)
            size = SmallAtlasSize;
        // While a cell is under 32 pixels wide, cells that do not fit simply
        // draw the complete base mask: coarser texels, but edges come from the
        // mask's distance channel, and only the view's fringe is affected, since
        // the nearest cells take slots first. That is not worth the large
        // atlas, or coarser tiles for every cell.
        // A complete draw (a capture) always takes masks at its density.
        const bool baseTakesOverflow = !complete && hd.base.tile && tile <= 4 * hd.base.tile &&
            !hd.base.remaining && hd.base.redo.empty();
        if (size < state->maxAtlasSize && !fits(size) && !baseTakesOverflow)
            size = state->maxAtlasSize; // Grow once rather than draw coarser.
        while (tile > 8 && !fits(size) && !baseTakesOverflow)
            tile /= 2;
            if (hd.tile != tile || hd.size != size || !hd.coverage)
            state->restartHdAtlas(tile, size, current > 0 && current >= previous);
    }
    const int atlasSize = hd.size;
    // The low-resolution masks of the whole map. Game loading builds them
    // (DirectTerrain::preload), and a complete draw at their density builds
    // them now; otherwise (a restart after a device loss, a view without that
    // load) the workers build them, ahead of any view mask. A complete draw
    // finer than the base (a capture's own view) builds every visible mask
    // at its density and would draw no base texel: it does not wait for one.
    if (!complete || tile <= wholeMapTile)
        state->ensureBase(map, compositor, centreX, centreY, complete);
    ++hd.frame;
    // Visible mixed cells: keep resident ones, and queue the rest nearest the
    // view centre first, so a zoom or pan fills in from the middle outwards.
    struct Want { int distance, cell, x, y; };
    std::vector<Want> missing;
    std::size_t split = 0, resident = 0;
    const int cx = (left + right) / 2, cy = (top + bottom) / 2;
    for (int y = top; y <= bottom && !hd.suspended; ++y)
        for (int x = left; x <= right; ++x)
        {
            if (!wholeMap && !map.isMapPartiallyDiscovered(x + viewportX - 1, y + viewportY - 1,
                    x + viewportX + 1, y + viewportY + 1, visibleTeams))
                continue;
            const int cell = map.coordToIndex(x + viewportX, y + viewportY);
            if (!state->cellMasks[cell])
                continue; // One material: no mask needed.
            if (const auto slot = hd.slotOf[cell])
            {
                hd.used[slot - 1] = hd.frame;
                ++resident;
            }
            else
            {
                missing.push_back({std::abs(x - cx) + std::abs(y - cy), cell, x + viewportX, y + viewportY});
                split += !(hd.base.tile && hd.base.built[cell]) &&
                    !(!hd.previous.slotOf.empty() && hd.previous.slotOf[cell]);
            }
        }
    hd.split = split;
    std::sort(missing.begin(), missing.end(), [](const Want &a, const Want &b) { return a.distance < b.distance; });
    // Where the base takes the overflow, more cells can be visible than the
    // atlas has slots: the farthest keep their base masks rather than being
    // built every frame only to find every slot visible.
    // They count as pending only until the base that draws them is complete.
    const std::size_t free = hd.cellOf.size() - std::min(hd.cellOf.size(), resident);
    const std::size_t wantedMasks = missing.size();
    // A complete draw finer than the base that still overflows the atlas
    // draws its farthest cells from the base after all.
    if (complete && missing.size() > free)
        state->ensureBase(map, compositor, centreX, centreY, true);
    if (missing.size() > free) missing.resize(free);
    const bool baseComplete = hd.base.tile && hd.base.reported && !hd.base.remaining && hd.base.redo.empty();
    hd.pending = baseComplete ? missing.size() : wantedMasks;
    const int columns = atlasSize / tile;
    if (!GAGCore::ThreadSupport::available)
    {
        // Hosts without threads build masks inline, below; std::thread would throw
        // and take every material with it (ANIMATED_TERRAIN_FALLBACK).
        if (!hd.materials) hd.materials = compositor.cpuMaterialsForRecipes({});
    }
    else if (hd.workers.empty())
    {
        hd.materials = compositor.cpuMaterialsForRecipes({});
        // Half the cores (two to six): masks keep pace with a zoom without
        // crowding out the simulation and asset threads.
        const unsigned cores = std::max(2u, std::thread::hardware_concurrency());
        for (unsigned i = 0; i < std::min(6u, std::max(2u, cores / 2)); ++i)
            hd.workers.emplace_back([&hd] { hd.work(); });
    }
    // A complete draw builds every missing visible mask now, in parallel.
    if (complete && !missing.empty())
    {
        std::vector<TerrainVisual::Recipe> recipes(missing.size());
        for (std::size_t i = 0; i < missing.size(); ++i)
            recipes[i] = compositor.describe(map, missing[i].x & map.getMaskW(), missing[i].y & map.getMaskH());
        std::vector<State::Hd::Result> built(missing.size());
        const auto materials = hd.materials;
        const auto generation = hd.generation;
        GAGCore::executePresentationJobs(missing.size(), [&](std::size_t i)
            { built[i] = State::Hd::build(*materials, missing[i].cell, recipes[i], tile, generation); });
        std::lock_guard lock(hd.mutex);
        for (auto &result : built) hd.done.push_back(std::move(result));
    }
    // Queue the nearest missing cells, replacing what is no longer wanted, so
    // workers follow the camera instead of finishing a view already left.
    else
    {
        // Enough queued that no worker idles until the next frame refills it:
        // the queue bounds how many masks a frame can finish.
        const std::size_t QueueDepth = 64 * std::max<std::size_t>(1, hd.workers.size());
        std::vector<State::Hd::Job> queue;
        const auto now = std::chrono::steady_clock::now();
        const auto deadline = now + std::chrono::milliseconds(2);
        for (const auto &want : missing)
        {
            if (queue.size() >= QueueDepth) break;
            if (hd.inFlight.contains(want.cell)) continue;
            auto found = hd.recipes.find(want.cell);
            if (found == hd.recipes.end())
            {
                if (std::chrono::steady_clock::now() > deadline) break;
                found = hd.recipes.emplace(want.cell,
                    compositor.describe(map, want.x & map.getMaskW(), want.y & map.getMaskH())).first;
            }
            queue.push_back({want.cell, found->second, tile, hd.generation});
        }
        // Base cells an edit invalidated (the first build is complete): a
        // quarter millisecond of recipes each frame, ahead of the view's
        // masks, whatever the view still waits for. Base recipes are not kept,
        // since most cells are never seen.
        auto &base = hd.base;
        std::vector<State::Hd::Job> baseQueue;
        if (base.tile && (base.remaining || !base.redo.empty()))
        {
            std::size_t queued;
            {
                std::lock_guard lock(hd.mutex);
                queued = hd.baseJobs.size();
            }
            // Two milliseconds while the base first builds (the view shows the
            // overview meanwhile), a quarter after.
            const auto baseDeadline = std::chrono::steady_clock::now() +
                std::chrono::microseconds(state->baseBuilding() ? 2000 : 250);
            while (queued + baseQueue.size() < QueueDepth && std::chrono::steady_clock::now() < baseDeadline)
            {
                int cell = -1;
                if (!base.redo.empty()) { cell = base.redo.front(); base.redo.pop_front(); }
                else if (base.cursor < base.order.size()) cell = base.order[base.cursor++];
                else break;
                if (base.built[cell] || !state->cellMasks[cell] || hd.baseInFlight.contains(cell)) continue;
                const int x = cell % width, y = cell / width;
                baseQueue.push_back({cell, compositor.describe(map, x, y), base.tile, base.version[cell], true});
            }
        }
        std::lock_guard lock(hd.mutex);
        for (const auto &job : hd.jobs) hd.inFlight.erase(job.cell); // Requeued below or dropped.
        hd.jobs.assign(std::make_move_iterator(queue.begin()), std::make_move_iterator(queue.end()));
        for (const auto &job : hd.jobs) hd.inFlight.insert(job.cell);
        for (auto &job : baseQueue)
        {
            hd.baseInFlight.insert(job.cell);
            hd.baseJobs.push_back(std::move(job));
        }
    }
    hd.wake.notify_all();
    if constexpr (!GAGCore::ThreadSupport::available)
        hd.buildQueued(std::chrono::steady_clock::now() + std::chrono::milliseconds(4));
    // Upload finished masks within a per-frame byte budget.
    std::vector<State::Hd::Result> finished;
    {
        std::lock_guard lock(hd.mutex);
        finished.swap(hd.done);
    }
    // A time budget, not a fixed count: a fast frame uploads every finished
    // mask, a slow one stops after a few milliseconds (or 16 MiB) and leaves
    // the rest, nearest cells first, to the next frames.
    const std::size_t UploadBudget = complete ? ~std::size_t(0) : std::size_t(16) << 20;
    const auto uploadDeadline = std::chrono::steady_clock::now() + std::chrono::microseconds(complete ? 1000000000 : 3000);
    std::size_t uploaded = 0, nextFree = 0, checked = 0;
    bool spent = false;
    const auto overBudget = [&] {
        // Reading the clock every eighth upload keeps its cost out of the loop.
        // Past the first 2 MiB (some 300 masks at 32 texels), which every
        // frame takes however slow its uploads.
        spent = spent || uploaded >= UploadBudget ||
            (uploaded >= (std::size_t(2) << 20) && (++checked & 7) == 0 && std::chrono::steady_clock::now() > uploadDeadline);
        return spent;
    };
    std::vector<State::Hd::Result> later;
    std::size_t baseUploads = 0;
    for (auto &result : finished)
    {
        if (result.base)
        {
            auto &base = hd.base;
            // A tiny tile each: bound the calls, not just the bytes.
            if (baseUploads >= 1024 || overBudget()) { later.push_back(std::move(result)); continue; }
            hd.baseInFlight.erase(result.cell);
            if (base.built[result.cell] || !state->cellMasks[result.cell]) continue;
            // Built from the recipe before an edit: build it again.
            if (result.generation != base.version[result.cell]) { base.redo.push_back(result.cell); continue; }
            const int ox = (result.cell % width) * base.tile, oy = (result.cell / width) * base.tile;
            state->write(base.coverage, State::Coverage, ox, oy, base.tile, base.tile, result.weights.data());
            state->write(base.edges, State::Edges, ox, oy, base.tile, base.tile, result.edges.data());
            uploaded += result.weights.size() * sizeof(Pixel) + result.edges.size() * sizeof(Uint16);
            ++baseUploads;
            base.built[result.cell] = 1;
            if (base.remaining) --base.remaining;
            if (!base.remaining && !base.reported)
            {
                base.reported = true;
                std::fprintf(stderr, "DIRECT_TERRAIN_BASE cells=%zu tile=%d ms=%llu\n", base.order.size(), base.tile,
                    (unsigned long long)(SDL_GetTicks() - base.started));
            }
            continue;
        }
        hd.inFlight.erase(result.cell);
        if (result.generation != hd.generation || hd.slotOf[result.cell])
            continue;
        if (overBudget()) { hd.inFlight.insert(result.cell); later.push_back(std::move(result)); continue; }
        std::size_t slot = hd.cellOf.size();
        for (; nextFree < hd.cellOf.size(); ++nextFree)
            if (hd.cellOf[nextFree] < 0) { slot = nextFree++; break; }
        if (slot == hd.cellOf.size())
        {
            Uint64 oldest = hd.frame;
            for (std::size_t s = 0; s < hd.cellOf.size(); ++s)
                if (hd.used[s] < oldest) { oldest = hd.used[s]; slot = s; }
            if (slot == hd.cellOf.size())
                continue; // Every slot is visible: this cell keeps its native mask.
            release(hd.cellOf[slot]);
        }
        const int ox = int(slot % columns) * tile, oy = int(slot / columns) * tile;
        state->write(hd.coverage, State::Hd::FirstUnit + 1, ox, oy, tile, tile, result.weights.data());
        state->write(hd.edges, State::Hd::FirstUnit + 2, ox, oy, tile, tile, result.edges.data());
        uploaded += result.weights.size() * sizeof(Pixel) + result.edges.size() * sizeof(Uint16);
        hd.cellOf[slot] = result.cell;
        hd.used[slot] = hd.frame;
        state->setHdIndex(result.cell, Uint16(slot + 1));
        if (hd.pending) --hd.pending;
    }
    if (!later.empty())
    {
        std::lock_guard lock(hd.mutex);
        for (auto &result : later) hd.done.push_back(std::move(result));
    }
    // The new atlas covers the view: the retired one has nothing left to show.
    if (!hd.pending && hd.previous.tile)
        state->retirePreviousHdAtlas();
    state->releaseIdleSpare(spareLifetimeMs);
}

std::span<const SDL_Rect> DirectTerrain::preparedRegions() const
{
    if (state) return state->animation.regions;
    return {};
}

void DirectTerrain::drawPrepared(GAGCore::GraphicContext &gfx, int viewportX, int viewportY,
                                 bool wholeMap, AnimationTime animationTime)
{
    if (!state || state->animation.regions.empty() || !state->hd.ready || state->hd.failed) return;
    const auto &previous = state->hd.previous;
    const auto &base = state->hd.base;
    if (state->streaming)
    {
        // One packet over the regions' bounds: the shader discards undiscovered cells.
        using Draw = GAGCore::DirectTerrainDraw;
        auto data = std::make_shared<Draw>();
        const auto bind = [&](unsigned slot, const Surface &surface) { data->streamed[slot] = surface.streamed; };
        bind(Draw::Cells, state->textures[Draw::Cells]);
        bind(Draw::MaskIndex, state->lowArt.surface); // The low-resolution art (see State::LowArt).
        bind(Draw::Discovery, state->textures[Draw::Discovery]);
        bind(Draw::ShoreCalm, state->textures[Draw::ShoreCalm]);
        bind(Draw::Art, state->animation.art);
        bind(Draw::Variants, state->animation.variants);
        bind(Draw::Coverage, base.coverage);
        bind(Draw::Edges, base.edges);
        bind(Draw::HdIndex, state->hd.index);
        bind(Draw::HdCoverage, state->hd.coverage);
        bind(Draw::HdEdges, state->hd.edges);
        if (previous.tile)
        {
            bind(Draw::HdPreviousIndex, previous.index);
            bind(Draw::HdPreviousCoverage, previous.coverage);
            bind(Draw::HdPreviousEdges, previous.edges);
        }
        auto &u = data->hd;
        const auto pair = [](float (&out)[2], float x, float y) { out[0] = x; out[1] = y; };
        pair(u.mapSize, float(state->width), float(state->height));
        pair(u.artSize, float(state->animation.size), float(state->animation.size));
        pair(u.variantSize, float(DirectTerrainData::VariantWidth), float(state->variantRows));
        pair(u.hdCoverageSize, float(state->hd.size), float(state->hd.size));
        pair(u.hdPreviousSize, float(previous.size), float(previous.size));
        pair(u.hdBaseSize, float(state->width * base.tile), float(state->height * base.tile));
        pair(u.lowArtSize, float(std::max(1, state->lowArt.width)), float(std::max(1, state->lowArt.height)));
        u.artTilePixels = float(state->animation.pixels);
        u.hdTilePixels = float(state->hd.tile);
        u.hdPreviousTile = float(previous.tile); // Zero: no fallback atlas.
        u.hdBaseTile = float(base.tile);         // Zero: no base atlas.
        data->wholeMap = wholeMap;
        data->animationTime = animationTime.wrapped(state->animationPeriod);
        SDL_Rect bounds = state->animation.regions.front();
        for (const auto &rect : state->animation.regions)
            SDL_GetRectUnion(&bounds, &rect, &bounds);
        GAGCore::DrawPacket packet;
        packet.kind = GAGCore::DrawPacket::Kind::DirectTerrain;
        packet.blend = GAGCore::DrawPacket::Blend::Premultiplied;
        packet.directTerrain = std::move(data);
        const auto corner = [&](int x, int y) {
            return SDL_Vertex{{float(x * TilePixels), float(y * TilePixels)}, {1, 1, 1, 1},
                              {float(x + viewportX), float(y + viewportY)}};
        };
        const auto a = corner(bounds.x, bounds.y), b = corner(bounds.x + bounds.w, bounds.y);
        const auto c = corner(bounds.x + bounds.w, bounds.y + bounds.h), d = corner(bounds.x, bounds.y + bounds.h);
        packet.vertices = {a, b, c, a, c, d};
        GAGCore::Sprite::flushBatches(&gfx);
        if (!gfx.submitDevicePacket(std::move(packet)))
            return;
        gfx.countRenderBatchDraw();
        ++state->frames;
        return;
    }
#ifdef DIRECT_TERRAIN_LEGACY_GL
    GAGCore::Sprite::flushBatches(&gfx);
    if (auto *batch = gfx.getRenderBatch()) batch->stateChange();
    RestoreGL restore;
    const auto &uniforms = state->hd.uniforms;
    glUseProgram(state->hd.program);
    state->placeholderBindings = 0;
    // Binds to the active unit; a surface without a texture gets the placeholder.
    const auto bind = [&](GLuint name)
    {
        if (!name)
        {
            if (!state->placeholder)
            {
                const std::uint8_t zero[4] = {};
                glGenTextures(1, &state->placeholder);
                glBindTexture(GL_TEXTURE_2D, state->placeholder);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);
            }
            name = state->placeholder;
            ++state->placeholderBindings;
        }
        glBindTexture(GL_TEXTURE_2D, name);
    };
    for (unsigned unit = 0; unit < State::Count; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        const auto &texture = unit == State::Art ? state->animation.art
            : unit == State::Variants ? state->animation.variants
            : unit == State::Coverage ? state->hd.base.coverage
            : unit == State::Edges ? state->hd.base.edges
            : unit == State::MaskIndex ? state->lowArt.surface : state->textures[unit];
        bind(texture.name);
    }
    const GLuint extra[] = {state->hd.index.name, state->hd.coverage.name, state->hd.edges.name,
                            previous.index.name, previous.coverage.name, previous.edges.name};
    for (unsigned i = 0; i < std::size(extra); ++i)
    {
        glActiveTexture(GL_TEXTURE0 + State::Hd::FirstUnit + i);
        bind(extra[i]);
    }
    glUniform2f(uniforms[1], float(state->animation.size), float(state->animation.size));
    glUniform2f(uniforms[2], float(std::max(1, state->lowArt.width)), float(std::max(1, state->lowArt.height)));
    glUniform2f(uniforms[7], float(state->hd.size), float(state->hd.size));
    glUniform2f(uniforms[3], DirectTerrainData::VariantWidth, float(state->variantRows));
    glUniform1f(uniforms[4], animationTime.wrapped(state->animationPeriod));
    glUniform1i(uniforms[5], wholeMap);
    glUniform1f(uniforms[6], float(state->animation.pixels));
    glUniform1f(uniforms[8], float(state->hd.tile));
    glUniform2f(uniforms[9], float(previous.size), float(previous.size));
    glUniform1f(uniforms[10], float(previous.tile)); // Zero: no fallback atlas.
    glUniform2f(uniforms[11], float(state->width * base.tile), float(state->height * base.tile));
    glUniform1f(uniforms[12], float(base.tile)); // Zero: no base atlas.
    glEnable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBegin(GL_QUADS);
    for (const auto &rect : state->animation.regions)
    {
        const auto corner = [&](int x, int y) {
            glMultiTexCoord2f(GL_TEXTURE0, float(x + viewportX), float(y + viewportY));
            glVertex2f(float(x * TilePixels), float(y * TilePixels));
        };
        corner(rect.x, rect.y);
        corner(rect.x + rect.w, rect.y);
        corner(rect.x + rect.w, rect.y + rect.h);
        corner(rect.x, rect.y + rect.h);
    }
    glEnd();
    gfx.countRenderBatchDraw();
    ++state->frames;
#else
    (void)gfx; (void)viewportX; (void)viewportY; (void)wholeMap; (void)animationTime;
#endif
}

bool DirectTerrain::needsBackdrop(GAGCore::GraphicContext &gfx, const SceneMap &map) const
{
    const auto *device = gfx.renderDevice();
    const bool streaming = device && device->capabilities().directTerrainStreaming;
#ifdef DIRECT_TERRAIN_LEGACY_GL
    const auto context = streaming ? nullptr : SDL_GL_GetCurrentContext();
#else
    constexpr SDL_GLContext context = nullptr;
#endif
    if (!streaming && !context) return false;
    // Until the map's tables are ready nothing draws; edits keep drawing.
    if (!state || state->streaming != streaming || state->context != context ||
        (streaming && state->resources != gfx.renderResourceOwner()) ||
        state->generation != gfx.getGLContextGeneration() || !state->sameMapSource(map))
        return true;
    return !state->tablesReady && !state->failed;
}

bool DirectTerrain::legacyPassAvailable()
{
#ifdef DIRECT_TERRAIN_LEGACY_GL
    return true;
#else
    return false;
#endif
}

bool DirectTerrain::streamedPassAvailable(GAGCore::GraphicContext &gfx)
{
    const auto *device = gfx.renderDevice();
    return device && device->capabilities().directTerrainStreaming && gfx.renderResourceOwner();
}

bool DirectTerrain::failed() const
{
    return state && state->failed;
}

bool DirectTerrain::failedFor(const SceneMap &map) const
{
    return state && state->failed && state->sameMapSource(map);
}

bool DirectTerrain::preparing() const
{
    // Only the streamed pass builds its tables over several frames.
    return state && (state->streaming || state->context) && !state->tablesReady && !state->failed;
}
std::size_t DirectTerrain::textureBytes() const { return state ? state->bytes : 0; }
std::size_t DirectTerrain::uploadBytes() const { return state ? state->uploaded : 0; }
unsigned DirectTerrain::draws() const { return state ? state->frames : 0; }
unsigned DirectTerrain::restartCount() const { return restarts; }
unsigned DirectTerrain::placeholderBindings() const { return state ? state->placeholderBindings : 0; }
DirectTerrain::ArtMetrics DirectTerrain::artMetrics() const
{
    if (!state) return {};
    return {state->artStats.visible, state->artStats.absent, state->artStats.stale, state->lowArt.bytes};
}
DirectTerrain::DiscoveryMetrics DirectTerrain::discoveryMetrics() const
{ return state ? state->discoveryStats : DiscoveryMetrics{}; }