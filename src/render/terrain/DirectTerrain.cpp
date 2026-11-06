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
#include "render/SoftwareTerrainCache.h"
#include "render/scene/SceneMap.h"
#ifdef DIRECT_TERRAIN_LEGACY_GL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif
#include <RenderBatch.h>
#include <DirectTerrainShader.h>
#include <RenderDevice.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <chrono>
#include <unordered_set>
#include <unordered_map>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>
#include <map>
#include <set>

namespace
{
constexpr int TilePixels = DirectTerrainData::TilePixels;
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
#endif

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
#ifdef DIRECT_TERRAIN_LEGACY_GL
    GLuint program = 0;
    std::optional<DirectTerrainData::Build> building;
    struct CoverageStream
    {
        std::vector<TerrainVisual::Recipe> recipes;
        std::vector<Uint16> slots;
        std::vector<bool> ready;
        std::array<int, 4> requested{};
        std::size_t viewCursor = 0, backgroundCursor = 0, completed = 0;
    };
    std::optional<CoverageStream> coverageStream;
    std::array<GLuint, Count> textures{};
    std::array<GLint, 7> uniforms{};
    std::vector<Record> animationCells, animationVariants;
    std::vector<DirectTerrainData::Source> animationSources;
    struct Animation {
        static constexpr int AtlasSize = 2048;
        struct Entry {
            unsigned pair;
            std::array<unsigned, 2> phases{~0u, ~0u};
            std::array<TerrainVisual::Compositor::PhaseSource, 2> stamps{};
        };
        GLuint art = 0, variants = 0;
        std::size_t bytes = 0;
        int pixels = 0;
        bool failed = false;
        std::map<unsigned, Entry> entries;
        std::vector<unsigned> freePairs;
        std::vector<SDL_Rect> regions;
        bool everyCell = false;
    } animation;
    // Experimental full-detail masks for visible mixed cells (GLOB2_TERRAIN_DIRECT_HD).
    struct Hd {
        static constexpr int AtlasSize = 2048;
        static constexpr unsigned FirstUnit = Count;
        GLuint program = 0, index = 0, coverage = 0, edges = 0;
        std::array<GLint, 9> uniforms{};
        int tile = 0;              // Texels per cell side; 0 before the first prepare.
        std::vector<Uint16> slotOf; // Per map cell: atlas slot plus one, or zero.
        std::vector<int> cellOf;    // Per slot: map cell index, or -1.
        std::vector<Uint64> used;   // Per slot: frame last visible.
        Uint64 frame = 0;
        std::size_t pending = 0, bytes = 0;
        bool failed = false;
        // Masks are built by background workers from an immutable material
        // snapshot; the graphics thread only computes recipes and uploads.
        struct Job { int cell; TerrainVisual::Recipe recipe; int scale; unsigned generation; };
        struct Result { int cell; unsigned generation; std::vector<Pixel> weights; std::vector<Uint16> edges; };
        std::mutex mutex;
        std::condition_variable wake;
        std::deque<Job> jobs;
        std::vector<Result> done;
        std::vector<std::thread> workers;
        bool stop = false;
        unsigned generation = 0;
        std::shared_ptr<const TerrainVisual::Compositor::CpuMaterials> materials;
        std::unordered_map<int, TerrainVisual::Recipe> recipes; // Per cell, for this generation.
        std::unordered_set<int> inFlight;                        // Cells queued or being built.
        void stopWorkers()
        {
            {
                std::lock_guard lock(mutex);
                stop = true;
                jobs.clear();
            }
            wake.notify_all();
            for (auto &worker : workers) worker.join();
            workers.clear();
        }
        void work()
        {
            for (;;)
            {
                Job job;
                std::shared_ptr<const TerrainVisual::Compositor::CpuMaterials> snapshot;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [&] { return stop || !jobs.empty(); });
                    if (stop) return;
                    job = std::move(jobs.front());
                    jobs.pop_front();
                    snapshot = materials;
                }
                const auto mask = snapshot->mask(job.recipe, job.scale);
                Result result{job.cell, job.generation, std::vector<Pixel>(mask.samples.size()),
                              std::vector<Uint16>(mask.samples.size())};
                for (std::size_t i = 0; i < mask.samples.size(); ++i)
                {
                    const auto &sample = mask.samples[i];
                    const unsigned largest = sample.slots & 3;
                    unsigned remaining = 65536;
                    auto &w = result.weights[i];
                    for (unsigned k = 0, other = 0; k < 4; ++k)
                        if (k != largest)
                        {
                            const unsigned weight = sample.others[other++];
                            remaining -= weight;
                            w[k] = Uint8((weight * 255 + 32768) / 65536);
                        }
                    w[largest] = Uint8((remaining * 255 + 32768) / 65536);
                    result.edges[i] = Uint16(std::min<unsigned>(sample.margin, 4095) | (unsigned(sample.slots >> 2) << 12));
                }
                std::lock_guard lock(mutex);
                done.push_back(std::move(result));
            }
        }
    } hd;
    std::vector<Uint16> cellMasks; // Native mask index per cell; nonzero for mixed cells.
    int coverageHeight = 0, variantHeight = 0;
#endif
    int width = 0, height = 0;
    int nativeArtHeight = 0;
    std::size_t bytes = 0, uploaded = 0;
    unsigned frames = 0;
    bool failed = false;
    std::vector<Uint8> visibility;
    Uint32 discoveryTeams = 0;
    Uint64 discoveryRevision = 0;
    DirectTerrain::DiscoveryMetrics discoveryStats;

    bool matchesMap(const SceneMap &map) const
    {
        return identity == map.identity() && revision == map.terrainRevision() &&
            seed == map.terrainSeed() && assets == map.frozenAssetBundle() &&
            registry == map.frozenTerrainRegistry();
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

    ~State()
    {
#ifdef DIRECT_TERRAIN_LEGACY_GL
        hd.stopWorkers();
        if (context && context == SDL_GL_GetCurrentContext()) {
            glDeleteTextures(1, &animation.art);
            glDeleteTextures(1, &animation.variants);
            glDeleteTextures(1, &hd.index);
            glDeleteTextures(1, &hd.coverage);
            glDeleteTextures(1, &hd.edges);
            if (hd.program) glDeleteProgram(hd.program);
        }
#endif
        releaseDeviceTextures();
#ifdef DIRECT_TERRAIN_LEGACY_GL
        if (context && SDL_GL_GetCurrentContext() == context)
        {
            glDeleteProgram(program);
            glDeleteTextures(Count, textures.data());
        }
#endif
    }

#ifdef DIRECT_TERRAIN_LEGACY_GL
    void upload(Draw::Texture id, int w, int h, GLint internal, GLenum format,
                GLenum type, const void *pixels, std::size_t size, GLint filter)
    {
        glActiveTexture(GL_TEXTURE0 + id);
        glBindTexture(GL_TEXTURE_2D, textures[id]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, pixels);
        bytes += size;
        uploaded += size;
    }

#endif

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
        uploadDevice(Art, DirectTerrainData::AtlasWidth, data.artHeight, data.art);
        uploadDevice(Coverage, DirectTerrainData::AtlasWidth, data.coverageHeight, data.coverage);
        uploadIndices(Edges, DirectTerrainData::AtlasWidth, data.coverageHeight, data.edges);
        uploadRecords(Variants, DirectTerrainData::VariantWidth, data.variantHeight, data.variants);
        uploadDeviceVisibility();
        uploadDevice(ShoreCalm, width, height, data.calm);
    }

#ifdef DIRECT_TERRAIN_LEGACY_GL
    void initialize(const SceneMap &map, DirectTerrainData &data)
    {
        GLint units = 0;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
        width = map.getW();
        height = map.getH();
        if (units < Count)
            throw std::runtime_error("Direct terrain requires eight texture units");

        const auto count = data.cells.size();
        visibility.resize(count);

        program = glCreateProgram();
        const auto attach = [&](GLenum type, const char *source) {
            const GLuint shader = compile(type, source);
            glAttachShader(program, shader);
            glDeleteShader(shader);
        };
        attach(GL_VERTEX_SHADER, Vertex);
        const auto fragment = std::string("#version 120\n#define DIRECT_TERRAIN_ANIMATION\n") + GAGCore::DirectTerrainFragment;
        attach(GL_FRAGMENT_SHADER, fragment.c_str());
        glLinkProgram(program);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            char error[512]{};
            glGetProgramInfoLog(program, sizeof(error), nullptr, error);
            throw std::runtime_error(error);
        }
        glUseProgram(program);
        glGenTextures(Count, textures.data());
        for (unsigned unit = 0; unit < Count; ++unit)
            glUniform1i(glGetUniformLocation(program, Draw::Samplers[unit]), unit);
        const char *names[] = {"mapSize", "artSize", "coverageSize", "variantSize", "animationTime", "wholeMap", "artTilePixels"};
        for (unsigned i = 0; i < uniforms.size(); ++i)
            uniforms[i] = glGetUniformLocation(program, names[i]);
        glUniform2f(uniforms[0], width, height);
        glUniform2f(uniforms[1], DirectTerrainData::AtlasWidth, data.artHeight);
        glUniform2f(uniforms[2], DirectTerrainData::AtlasWidth, data.coverageHeight);
        glUniform2f(uniforms[3], DirectTerrainData::VariantWidth, data.variantHeight);
        upload(Cells, width, height, GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT, data.cells.data(), count * sizeof(Record), GL_NEAREST);
        const bool streaming = !data.pendingCoverage.empty();
        auto masks = streaming ? data.masks : std::vector<Uint16>{};
        for (auto &slot : masks)
            if (slot) slot = DirectTerrainData::PendingMask;
        upload(MaskIndex, width, height, GL_LUMINANCE16, GL_LUMINANCE, GL_UNSIGNED_SHORT,
               streaming ? masks.data() : data.masks.data(), count * sizeof(Uint16), GL_NEAREST);
        upload(Art, DirectTerrainData::AtlasWidth, data.artHeight, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, data.art.data(), data.art.size() * sizeof(Pixel), GL_LINEAR);
        upload(Coverage, DirectTerrainData::AtlasWidth, data.coverageHeight, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, streaming ? nullptr : data.coverage.data(), std::size_t(DirectTerrainData::AtlasWidth) * data.coverageHeight * sizeof(Pixel), GL_LINEAR);
        upload(Edges, DirectTerrainData::AtlasWidth, data.coverageHeight, GL_LUMINANCE16, GL_LUMINANCE, GL_UNSIGNED_SHORT, streaming ? nullptr : data.edges.data(), std::size_t(DirectTerrainData::AtlasWidth) * data.coverageHeight * sizeof(Uint16), GL_NEAREST);
        upload(Variants, DirectTerrainData::VariantWidth, data.variantHeight, GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT, data.variants.data(), data.variants.size() * sizeof(Record), GL_NEAREST);
        upload(Discovery, width, height, GL_LUMINANCE8, GL_LUMINANCE, GL_UNSIGNED_BYTE, visibility.data(), count, GL_NEAREST);
        upload(ShoreCalm, width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, data.calm.data(), data.calm.size() * sizeof(Pixel), GL_NEAREST);
        nativeArtHeight = data.artHeight;
        coverageHeight = data.coverageHeight;
        variantHeight = data.variantHeight;
        cellMasks = data.masks;
        animationCells = std::move(data.cells);
        animationVariants = std::move(data.variants);
        animationSources = std::move(data.sources);
        if (streaming)
        {
            const auto uninitializedBytes = std::size_t(DirectTerrainData::AtlasWidth) * data.coverageHeight *
                (sizeof(Pixel) + sizeof(Uint16));
            uploaded -= uninitializedBytes;
        }
        if (glGetError() != GL_NO_ERROR)
            throw std::runtime_error("Direct terrain texture setup failed");
    }

    void advanceCoverage(const SceneMap &map, const TerrainVisual::Compositor &compositor,
                         const std::array<int, 4> &view,
                         std::chrono::steady_clock::time_point deadline)
    {
        if (!coverageStream) return;
        auto &stream = *coverageStream;
        if (stream.requested != view)
        {
            stream.requested = view;
            stream.viewCursor = 0;
        }
        const auto columns = std::min(view[2] - view[0] + 1, width);
        const auto rows = std::min(view[3] - view[1] + 1, height);
        const auto visibleCount = std::size_t(columns) * rows;
        constexpr int size = DirectTerrainData::MaskPixels;
        constexpr int perRow = DirectTerrainData::AtlasWidth / size;
        const auto uploadTile = [&](std::size_t index) {
            const auto &recipe = stream.recipes[index];
            const auto tile = DirectTerrainData::makeCoverage(compositor, recipe);
            const int x = int(index % perRow) * size, y = int(index / perRow) * size;
            glActiveTexture(GL_TEXTURE0 + Coverage);
            glBindTexture(GL_TEXTURE_2D, textures[Coverage]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, size, size, GL_RGBA, GL_UNSIGNED_BYTE, tile.weights.data());
            glActiveTexture(GL_TEXTURE0 + Edges);
            glBindTexture(GL_TEXTURE_2D, textures[Edges]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, size, size, GL_LUMINANCE, GL_UNSIGNED_SHORT, tile.edges.data());
            glActiveTexture(GL_TEXTURE0 + MaskIndex);
            glBindTexture(GL_TEXTURE_2D, textures[MaskIndex]);
            // Publish the index only after both sampled textures contain this tile.
            const auto slot = Uint16(index + 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, recipe.x, recipe.y, 1, 1, GL_LUMINANCE, GL_UNSIGNED_SHORT, &slot);
            uploaded += sizeof(tile) + sizeof(slot);
            stream.ready[index] = true;
            ++stream.completed;
        };
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (stream.viewCursor < visibleCount)
            {
                const int x = view[0] + int(stream.viewCursor % columns), y = view[1] + int(stream.viewCursor / columns);
                ++stream.viewCursor;
                const auto slot = stream.slots[map.coordToIndex(x, y)];
                if (slot && !stream.ready[slot - 1])
                    uploadTile(slot - 1);
            }
            else if (stream.backgroundCursor < stream.recipes.size())
            {
                const auto index = stream.backgroundCursor++;
                if (!stream.ready[index]) uploadTile(index);
            }
            else break;
        }
        if (glGetError() != GL_NO_ERROR)
            throw std::runtime_error("Direct terrain coverage upload failed");
        if (stream.completed == stream.recipes.size())
            coverageStream.reset();
    }

#endif

    void updateDiscovery(const SceneMap &map, Uint32 teams)
    {
        const auto revision = map.discoveryRevision();
        if (revision && discoveryTeams == teams && discoveryRevision == revision)
            return;
        discoveryRevision = revision;
        discoveryTeams = teams;
        discoveryStats.rebuilt += visibility.size();
        bool changed = false;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                auto &pixel = visibility[map.coordToIndex(x, y)];
                const Uint8 value = map.isMapPartiallyDiscovered(x - 1, y - 1, x + 1, y + 1, teams) ? 255 : 0;
                changed |= value != pixel;
                pixel = value;
            }
        if (changed && resources)
            uploadDeviceVisibility();
#ifdef DIRECT_TERRAIN_LEGACY_GL
        else if (changed)
        {
            glActiveTexture(GL_TEXTURE0 + Discovery);
            glBindTexture(GL_TEXTURE_2D, textures[Discovery]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_LUMINANCE, GL_UNSIGNED_BYTE, visibility.data());
            if (glGetError() != GL_NO_ERROR)
                throw std::runtime_error("Direct terrain discovery upload failed");
            uploaded += visibility.size();
        }
#endif
    }
};

DirectTerrain::DirectTerrain() = default;
DirectTerrain::~DirectTerrain() = default;

bool DirectTerrain::draw(GAGCore::GraphicContext &gfx, const SceneMap &map, int left, int top,
                         int right, int bottom, int viewportX, int viewportY,
                         Uint32 visibleTeams, bool wholeMap, int animationTime, std::span<const SDL_Rect> regions)
{
    return drawInternal(gfx, map, left, top, right, bottom, viewportX, viewportY,
                        visibleTeams, wholeMap, animationTime, regions, false);
}

bool DirectTerrain::drawInternal(GAGCore::GraphicContext &gfx, const SceneMap &map, int left, int top,
                         int right, int bottom, int viewportX, int viewportY,
                         Uint32 visibleTeams, bool wholeMap, int animationTime,
                         std::span<const SDL_Rect> regions, bool prepareOnly)
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
    const auto generation = gfx.getGLContextGeneration();
    const auto assets = map.frozenAssetBundle();
    const auto registry = map.frozenTerrainRegistry();
    const bool sameContext = state && state->context == (resources ? nullptr : context) &&
        state->resources == resources && state->generation == generation;
    if (!sameContext || !state->matchesMap(map))
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
    }
    if (state->failed)
        return false;

    if (resources)
    {
        if (prepareOnly) return false;
        try
        {
            if (!state->handles[State::Cells])
                state->initializeDevice(map, capabilities.maximumTextureSize);
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
        data->animationTime = float(animationTime);

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

#ifdef DIRECT_TERRAIN_LEGACY_GL
    GAGCore::Sprite::flushBatches(&gfx);
    if (auto *batch = gfx.getRenderBatch())
        batch->stateChange();
    RestoreGL restore;
    // Check the deadline between work items. Driver allocation and the first
    // metadata upload are indivisible and can take longer than the slice.
    constexpr auto coverageSlice = std::chrono::milliseconds(3);
    const auto deadline = std::chrono::steady_clock::now() + coverageSlice;
    try
    {
        if (!state->program)
        {
            if (!state->building)
            {
                GLint maxSize = 0;
                glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
                state->building.emplace(DirectTerrainData::begin(
                    map, globalContainer->terrainCompositorOwner(assets), maxSize, true));
            }
            bool ready;
            do
            {
                ready = state->building->advance();
            } while (!ready && std::chrono::steady_clock::now() < deadline);
            if (!ready)
                return false;
            auto data = state->building->take();
            state->building.reset();
            state->initialize(map, data);
            if (!data.pendingCoverage.empty())
            {
                auto &stream = state->coverageStream.emplace(State::CoverageStream{});
                stream.recipes = std::move(data.pendingCoverage);
                stream.slots = std::move(data.masks);
                stream.ready.resize(stream.recipes.size());
            }
        }
        if (state->coverageStream)
            state->advanceCoverage(map, globalContainer->terrainCompositor(assets),
                {left + viewportX, top + viewportY, right + viewportX, bottom + viewportY}, deadline);
        if (!wholeMap)
            state->updateDiscovery(map, visibleTeams);
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "DIRECT_TERRAIN_FALLBACK %s\n", error.what());
        state->failed = true;
        state->building.reset();
        state->coverageStream.reset();
        glDeleteProgram(state->program);
        glDeleteTextures(State::Count, state->textures.data());
        state->program = 0;
        state->textures.fill(0);
        state->bytes = 0;
        return false;
    }

    if (prepareOnly) return true;
    glUseProgram(state->program);
    glUniform1f(state->uniforms[6], 32.f);
    for (unsigned unit = 0; unit < State::Count; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, state->textures[unit]);
    }
    glUniform1f(state->uniforms[4], float(animationTime));
    glUniform1i(state->uniforms[5], wholeMap);
    glEnable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    const auto corner = [&](int x, int y) {
        glMultiTexCoord2f(GL_TEXTURE0, float(x + viewportX), float(y + viewportY));
        glVertex2f(float(x * TilePixels), float(y * TilePixels));
    };
    const auto quad = [&](int x, int y, int endX, int endY) {
        corner(x, y);
        corner(endX, y);
        corner(endX, endY);
        corner(x, endY);
    };
    glBegin(GL_QUADS);
    if (regions.empty())
        quad(left, top, right + 1, bottom + 1);
    else
        for (const auto &region : regions)
            quad(region.x, region.y, region.x + region.w, region.y + region.h);
    glEnd();
    gfx.countRenderBatchDraw();
    ++state->frames;
    return true;
#else
    return false;
#endif
}

bool DirectTerrain::prepareAnimation(GAGCore::GraphicContext &gfx, const SceneMap &map,
    int left, int top, int right, int bottom, int viewportX, int viewportY,
    Uint32 visibleTeams, bool wholeMap, int animationTime, bool everyCell)
{
#ifdef DIRECT_TERRAIN_LEGACY_GL
    if (!gfx.isLegacyGLScreen() || left > right || top > bottom) return false;
    if (!drawInternal(gfx, map, left, top, right, bottom, viewportX, viewportY,
                      visibleTeams, wholeMap, animationTime, {}, true) ||
        needsBackdrop(gfx, map, left + viewportX, top + viewportY,
                      right + viewportX, bottom + viewportY))
        return false;
    auto &out = state->animation;
    out.regions.clear();
    if (out.failed) return false;
    auto &compositor = globalContainer->terrainCompositor(map.frozenAssetBundle());
    // Match the page compositor's streaming window. Narrowing it between the
    // two passes would evict and decode the fringe variants every frame.
    constexpr int pageTiles = SoftwareTerrainCache::ChunkTiles;
    const auto page = [](int cell) { return int(std::floor(double(cell) / pageTiles)); };
    compositor.prepareVisible(true, animationTime, map,
        page(left + viewportX) * pageTiles, page(top + viewportY) * pageTiles,
        (page(right + viewportX) + 1) * pageTiles - 1,
        (page(bottom + viewportY) + 1) * pageTiles - 1);
    const auto &catalog = compositor.catalog();
    std::set<unsigned> wanted;
    // The immutable map table supplies the same palette as the native shader.
    // Read only the current view; panning does not reconstruct terrain recipes.
    for (int y = top; y <= bottom; ++y)
    {
        int run = -1;
        for (int x = left; x <= right + 1; ++x)
        {
            bool animated = false;
            if (x <= right && (wholeMap || map.isMapPartiallyDiscovered(
                    x + viewportX - 1, y + viewportY - 1,
                    x + viewportX + 1, y + viewportY + 1, visibleTeams)))
            {
                const auto &cell = state->animationCells[map.coordToIndex(x + viewportX, y + viewportY)];
                bool wet = false, dry = false;
                for (auto slot : cell)
                {
                    if (!slot) continue;
                    const auto &material = catalog.materials[state->animationSources[slot].material];
                    animated |= material.animationFrames > 1;
                    wet |= material.surf.period > 0;
                    dry |= material.surf.period == 0;
                }
                animated |= wet && dry;
                animated |= everyCell;
                if (animated)
                    for (auto slot : cell)
                        if (slot) wanted.insert(slot);
            }
            if (animated && run < 0) run = x;
            if (!animated && run >= 0)
            {
                out.regions.push_back({run, y, x - run, 1});
                run = -1;
            }
        }
    }
    out.everyCell = everyCell;
    if (wanted.empty()) return true;

    // Two endpoints per visible material variant, sampled near the display
    // density. This independent 16 MiB allocation replaces repeated page
    // composition; it never expands the terrain page cache.
    constexpr int atlasSize = State::Animation::AtlasSize;
    int pixels = 32;
    const double density = gfx.mapTransformScale() * gfx.getRasterScale();
    while (pixels < 128 && pixels < 32 * density) pixels *= 2;
    while (pixels > 32 && wanted.size() * 2 > std::size_t(atlasSize / pixels) * (atlasSize / pixels))
        pixels /= 2;
    const unsigned pairs = (atlasSize / pixels) * (atlasSize / pixels) / 2;
    if (wanted.size() > pairs) { out.regions.clear(); return false; }
    GAGCore::Sprite::flushBatches(&gfx);
    if (auto *batch = gfx.getRenderBatch()) batch->stateChange();
    RestoreGL restore;
    try
    {
        if (!out.art)
        {
            glGenTextures(1, &out.art);
            glActiveTexture(GL_TEXTURE0 + State::Art);
            glBindTexture(GL_TEXTURE_2D, out.art);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlasSize, atlasSize, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glGenTextures(1, &out.variants);
            glActiveTexture(GL_TEXTURE0 + State::Variants);
            glBindTexture(GL_TEXTURE_2D, out.variants);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, DirectTerrainData::VariantWidth,
                int(state->animationVariants.size() / DirectTerrainData::VariantWidth), 0,
                GL_RGBA, GL_UNSIGNED_SHORT, state->animationVariants.data());
            out.bytes = std::size_t(atlasSize) * atlasSize * sizeof(Pixel) +
                state->animationVariants.size() * sizeof(Record);
            state->bytes += out.bytes;
        }
        if (out.pixels != pixels)
        {
            out.entries.clear();
            out.freePairs.clear();
            for (unsigned pair = pairs; pair > 0; --pair) out.freePairs.push_back(pair - 1);
            out.pixels = pixels;
        }
        for (auto slot : wanted)
        {
            auto it = out.entries.find(slot);
            const bool added = it == out.entries.end();
            if (it == out.entries.end())
            {
                if (out.freePairs.empty())
                {
                    const auto idle = std::find_if(out.entries.begin(), out.entries.end(),
                        [&](const auto &item) { return !wanted.contains(item.first); });
                    out.freePairs.push_back(idle->second.pair);
                    out.entries.erase(idle);
                }
                const auto pair = out.freePairs.back();
                out.freePairs.pop_back();
                it = out.entries.emplace(slot, State::Animation::Entry{pair}).first;
            }
            auto &entry = it->second;
            const auto source = state->animationSources[slot];
            const auto &material = catalog.materials[source.material];
            const unsigned absolutePhase = unsigned(animationTime) / material.animationTicks;
            const unsigned endpoints = material.animationFrames > 1 ? 2 : 1;
            for (unsigned ahead = 0; ahead < endpoints; ++ahead)
            {
                const unsigned phase = (absolutePhase + ahead) % material.animationFrames;
                const unsigned parity = endpoints == 2 ? (absolutePhase + ahead) % 2 : 0;
                const auto stamp = compositor.phaseSource(source.material, source.variant, phase);
                if (entry.phases[parity] == phase && entry.stamps[parity] == stamp) continue;
                const auto input = compositor.phasePixels(source.material, source.variant, phase);
                if (input.size() != std::size_t(stamp.size) * stamp.size)
                    throw std::runtime_error("Animated terrain source changed dimensions");
                // Box-filter each block in premultiplied colour. Point samples at a
                // block's corner would read a tile's two edges asymmetrically, so
                // neighbouring variants, blended to meet at their borders, would not.
                std::vector<Pixel> tile(std::size_t(pixels) * pixels);
                const int block = std::max(1, stamp.size / pixels);
                for (int y = 0; y < pixels; ++y)
                    for (int x = 0; x < pixels; ++x)
                    {
                        unsigned sum[4] = {};
                        for (int dy = 0; dy < block; ++dy)
                            for (int dx = 0; dx < block; ++dx)
                            {
                                const auto &p = input[std::size_t(y * stamp.size / pixels + dy) * stamp.size +
                                                      x * stamp.size / pixels + dx];
                                for (int k = 0; k < 3; ++k) sum[k] += unsigned(p[k]) * p[3];
                                sum[3] += p[3];
                            }
                        auto &out = tile[y * pixels + x];
                        const unsigned count = unsigned(block * block);
                        for (int k = 0; k < 3; ++k) out[k] = Uint8(sum[3] ? (sum[k] + sum[3] / 2) / sum[3] : 0);
                        out[3] = Uint8((sum[3] + count / 2) / count);
                    }
                const unsigned index = entry.pair * 2 + parity;
                const int columns = atlasSize / pixels;
                glActiveTexture(GL_TEXTURE0 + State::Art);
                glBindTexture(GL_TEXTURE_2D, out.art);
                glTexSubImage2D(GL_TEXTURE_2D, 0, (index % columns) * pixels,
                    (index / columns) * pixels, pixels, pixels, GL_RGBA, GL_UNSIGNED_BYTE, tile.data());
                state->uploaded += tile.size() * sizeof(Pixel);
                entry.phases[parity] = phase;
                entry.stamps[parity] = stamp;
            }
            if (added)
            {
                const auto recordIndex = slot * DirectTerrainData::VariantRecords;
                auto record = state->animationVariants[recordIndex];
                record[0] = Uint16(entry.pair * 2);
                // Parity addresses the streamed current/next endpoints independently
                // of the material's original loop length, including odd-length loops.
                record[1] = Uint16(endpoints);
                glActiveTexture(GL_TEXTURE0 + State::Variants);
                glBindTexture(GL_TEXTURE_2D, out.variants);
                glTexSubImage2D(GL_TEXTURE_2D, 0, recordIndex % DirectTerrainData::VariantWidth,
                    recordIndex / DirectTerrainData::VariantWidth, 1, 1, GL_RGBA, GL_UNSIGNED_SHORT, record.data());
                state->uploaded += sizeof(Record);
            }
        }
        if (glGetError() != GL_NO_ERROR)
            throw std::runtime_error("Animated terrain source upload failed");
        if (everyCell)
        {
            if (state->hd.failed) { out.regions.clear(); return false; }
            int tile = 32;
            while (tile < 128 && tile < 32 * density) tile *= 2;
            prepareHdMasks(map, left, top, right, bottom, viewportX, viewportY, visibleTeams, wholeMap, tile);
        }
        return true;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "ANIMATED_TERRAIN_FALLBACK %s\n", error.what());
        glDeleteTextures(1, &out.art);
        glDeleteTextures(1, &out.variants);
        state->bytes -= out.bytes;
        out = {};
        out.failed = true;
        out.regions.clear();
        return false;
    }
#else
    return false;
#endif
}

bool DirectTerrain::directHdRequested()
{
    static const bool requested = []
    {
        const char *value = std::getenv("GLOB2_TERRAIN_DIRECT_HD");
        return value && *value && std::strcmp(value, "0") != 0;
    }();
    return requested;
}

std::size_t DirectTerrain::pendingHdMasks() const
{
#ifdef DIRECT_TERRAIN_LEGACY_GL
    return state ? state->hd.pending : 0;
#else
    return 0;
#endif
}

void DirectTerrain::prepareHdMasks(const SceneMap &map, int left, int top, int right, int bottom,
    int viewportX, int viewportY, Uint32 visibleTeams, bool wholeMap, int tile)
{
#ifdef DIRECT_TERRAIN_LEGACY_GL
    auto &hd = state->hd;
    const int width = state->width, height = state->height;
    constexpr int atlasSize = State::Hd::AtlasSize;
    if (!hd.program)
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
        const char *names[] = {"mapSize", "artSize", "coverageSize", "variantSize", "animationTime",
                               "wholeMap", "artTilePixels", "hdCoverageSize", "hdTilePixels"};
        for (unsigned i = 0; i < hd.uniforms.size(); ++i)
            hd.uniforms[i] = glGetUniformLocation(hd.program, names[i]);
        glUniform2f(hd.uniforms[0], float(width), float(height));
        glUniform2f(hd.uniforms[2], DirectTerrainData::AtlasWidth, float(state->coverageHeight));
        glUniform2f(hd.uniforms[3], DirectTerrainData::VariantWidth, float(state->variantHeight));
        glUniform2f(hd.uniforms[7], float(atlasSize), float(atlasSize));
        const auto make = [&](GLuint &texture, unsigned unit, int w, int h, GLint internal, GLenum format,
                              GLenum type, GLint filter)
        {
            glGenTextures(1, &texture);
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, nullptr);
        };
        const std::vector<Uint16> zero(std::size_t(width) * height, 0);
        make(hd.index, State::Hd::FirstUnit, width, height, GL_LUMINANCE16, GL_LUMINANCE, GL_UNSIGNED_SHORT, GL_NEAREST);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_LUMINANCE, GL_UNSIGNED_SHORT, zero.data());
        make(hd.coverage, State::Hd::FirstUnit + 1, atlasSize, atlasSize, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
        make(hd.edges, State::Hd::FirstUnit + 2, atlasSize, atlasSize, GL_LUMINANCE16, GL_LUMINANCE, GL_UNSIGNED_SHORT, GL_NEAREST);
        hd.bytes = std::size_t(atlasSize) * atlasSize * 6 + zero.size() * sizeof(Uint16);
        state->bytes += hd.bytes;
        hd.slotOf.assign(zero.size(), 0);
    }
    const auto setIndex = [&](int cell, Uint16 value)
    {
        hd.slotOf[cell] = value;
        glActiveTexture(GL_TEXTURE0 + State::Hd::FirstUnit);
        glBindTexture(GL_TEXTURE_2D, hd.index);
        glTexSubImage2D(GL_TEXTURE_2D, 0, cell % width, cell / width, 1, 1, GL_LUMINANCE, GL_UNSIGNED_SHORT, &value);
    };
    // Every visible mixed cell must fit in the atlas with headroom for a pan,
    // so a wide view takes coarser tiles rather than leaving some cells native.
    {
        std::size_t mixed = 0;
        for (int y = top; y <= bottom; ++y)
            for (int x = left; x <= right; ++x)
                mixed += state->cellMasks[map.coordToIndex(x + viewportX, y + viewportY)] != 0;
        while (tile > 32 && std::size_t(atlasSize / tile) * (atlasSize / tile) < mixed + mixed / 4)
            tile /= 2;
    }
    if (hd.tile != tile)
    {
        // A new density invalidates every tile; the index resets with it.
        for (int cell = 0; cell < int(hd.slotOf.size()); ++cell)
            if (hd.slotOf[cell]) setIndex(cell, 0);
        hd.tile = tile;
        ++hd.generation;
        hd.recipes.clear();
        {
            std::lock_guard lock(hd.mutex);
            hd.jobs.clear();
            hd.done.clear();
        }
        hd.inFlight.clear();
        const std::size_t slots = std::min<std::size_t>(65534, std::size_t(atlasSize / tile) * (atlasSize / tile));
        hd.cellOf.assign(slots, -1);
        hd.used.assign(slots, 0);
    }
    ++hd.frame;
    // Visible mixed cells: keep resident ones, and queue the rest nearest the
    // view centre first, so a zoom or pan fills in from the middle outwards.
    struct Want { int distance, cell, x, y; };
    std::vector<Want> missing;
    const int cx = (left + right) / 2, cy = (top + bottom) / 2;
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x)
        {
            if (!wholeMap && !map.isMapPartiallyDiscovered(x + viewportX - 1, y + viewportY - 1,
                    x + viewportX + 1, y + viewportY + 1, visibleTeams))
                continue;
            const int cell = map.coordToIndex(x + viewportX, y + viewportY);
            if (!state->cellMasks[cell])
                continue; // One material: no mask needed.
            if (const auto slot = hd.slotOf[cell])
                hd.used[slot - 1] = hd.frame;
            else
                missing.push_back({std::abs(x - cx) + std::abs(y - cy), cell, x + viewportX, y + viewportY});
        }
    std::sort(missing.begin(), missing.end(), [](const Want &a, const Want &b) { return a.distance < b.distance; });
    hd.pending = missing.size();
    auto &compositor = globalContainer->terrainCompositor(map.frozenAssetBundle());
    const int scale = tile / 32, columns = atlasSize / tile;
    if (hd.workers.empty())
    {
        hd.materials = compositor.cpuMaterialsForRecipes({});
        const unsigned cores = std::max(2u, std::thread::hardware_concurrency());
        for (unsigned i = 0; i < std::min(3u, cores - 1); ++i)
            hd.workers.emplace_back([&hd] { hd.work(); });
    }
    // Queue the nearest missing cells, replacing what is no longer wanted, so
    // workers follow the camera instead of finishing a view already left.
    {
        constexpr std::size_t QueueDepth = 64;
        std::vector<State::Hd::Job> queue;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
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
            queue.push_back({want.cell, found->second, scale, hd.generation});
        }
        std::lock_guard lock(hd.mutex);
        for (const auto &job : hd.jobs) hd.inFlight.erase(job.cell); // Requeued below or dropped.
        hd.jobs.assign(std::make_move_iterator(queue.begin()), std::make_move_iterator(queue.end()));
        for (const auto &job : hd.jobs) hd.inFlight.insert(job.cell);
    }
    hd.wake.notify_all();
    // Upload finished masks within a per-frame byte budget.
    std::vector<State::Hd::Result> finished;
    {
        std::lock_guard lock(hd.mutex);
        finished.swap(hd.done);
    }
    constexpr std::size_t UploadBudget = 6u << 20;
    std::size_t uploaded = 0, nextFree = 0;
    std::vector<State::Hd::Result> later;
    for (auto &result : finished)
    {
        hd.inFlight.erase(result.cell);
        if (result.generation != hd.generation || hd.slotOf[result.cell])
            continue;
        if (uploaded >= UploadBudget) { hd.inFlight.insert(result.cell); later.push_back(std::move(result)); continue; }
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
            setIndex(hd.cellOf[slot], 0);
        }
        const int ox = int(slot % columns) * tile, oy = int(slot / columns) * tile;
        glActiveTexture(GL_TEXTURE0 + State::Hd::FirstUnit + 1);
        glBindTexture(GL_TEXTURE_2D, hd.coverage);
        glTexSubImage2D(GL_TEXTURE_2D, 0, ox, oy, tile, tile, GL_RGBA, GL_UNSIGNED_BYTE, result.weights.data());
        glActiveTexture(GL_TEXTURE0 + State::Hd::FirstUnit + 2);
        glBindTexture(GL_TEXTURE_2D, hd.edges);
        glTexSubImage2D(GL_TEXTURE_2D, 0, ox, oy, tile, tile, GL_LUMINANCE, GL_UNSIGNED_SHORT, result.edges.data());
        const auto bytes = result.weights.size() * sizeof(Pixel) + result.edges.size() * sizeof(Uint16);
        uploaded += bytes;
        state->uploaded += bytes;
        hd.cellOf[slot] = result.cell;
        hd.used[slot] = hd.frame;
        setIndex(result.cell, Uint16(slot + 1));
        if (hd.pending) --hd.pending;
    }
    if (!later.empty())
    {
        std::lock_guard lock(hd.mutex);
        for (auto &result : later) hd.done.push_back(std::move(result));
    }
#endif
}

std::span<const SDL_Rect> DirectTerrain::animationRegions() const
{
#ifdef DIRECT_TERRAIN_LEGACY_GL
    if (state) return state->animation.regions;
#endif
    return {};
}

void DirectTerrain::drawAnimation(GAGCore::GraphicContext &gfx, int viewportX, int viewportY,
                                  bool wholeMap, int animationTime)
{
#ifdef DIRECT_TERRAIN_LEGACY_GL
    if (!state || state->animation.regions.empty()) return;
    GAGCore::Sprite::flushBatches(&gfx);
    if (auto *batch = gfx.getRenderBatch()) batch->stateChange();
    RestoreGL restore;
    const bool hd = state->animation.everyCell && state->hd.program && !state->hd.failed;
    const auto &uniforms = hd ? state->hd.uniforms : decltype(state->hd.uniforms){};
    glUseProgram(hd ? state->hd.program : state->program);
    for (unsigned unit = 0; unit < State::Count; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        const auto texture = unit == State::Art ? state->animation.art
            : unit == State::Variants ? state->animation.variants : state->textures[unit];
        glBindTexture(GL_TEXTURE_2D, texture);
    }
    if (hd)
    {
        const GLuint extra[] = {state->hd.index, state->hd.coverage, state->hd.edges};
        for (unsigned i = 0; i < 3; ++i)
        {
            glActiveTexture(GL_TEXTURE0 + State::Hd::FirstUnit + i);
            glBindTexture(GL_TEXTURE_2D, extra[i]);
        }
        glUniform2f(uniforms[1], State::Animation::AtlasSize, State::Animation::AtlasSize);
        glUniform1f(uniforms[4], float(animationTime));
        glUniform1i(uniforms[5], wholeMap);
        glUniform1f(uniforms[6], float(state->animation.pixels));
        glUniform1f(uniforms[8], float(state->hd.tile));
    }
    else
    {
        glUniform2f(state->uniforms[1], State::Animation::AtlasSize, State::Animation::AtlasSize);
        glUniform1f(state->uniforms[4], float(animationTime));
        glUniform1i(state->uniforms[5], wholeMap);
        glUniform1f(state->uniforms[6], float(state->animation.pixels));
    }
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
    // The ordinary atlas uses its own dimensions on its next draw.
    if (!hd)
        glUniform2f(state->uniforms[1], DirectTerrainData::AtlasWidth, float(state->nativeArtHeight));
    gfx.countRenderBatchDraw();
#endif
}

bool DirectTerrain::needsBackdrop(GAGCore::GraphicContext &gfx, const SceneMap &map,
                                  int left, int top, int right, int bottom) const
{
#ifdef DIRECT_TERRAIN_LEGACY_GL
    const auto context = SDL_GL_GetCurrentContext();
    if (!context) return false;
    if (!state || state->context != context || state->generation != gfx.getGLContextGeneration() ||
        !state->matchesMap(map))
        return true;
    if (!state->program) return !state->failed;
    if (!state->coverageStream) return false;
    const auto &stream = *state->coverageStream;
    return stream.requested != std::array<int, 4>{left, top, right, bottom} ||
        stream.viewCursor < std::size_t(std::min(right - left + 1, state->width)) *
            std::min(bottom - top + 1, state->height);
#else
    return false;
#endif
}

bool DirectTerrain::preparing() const
{
#ifdef DIRECT_TERRAIN_LEGACY_GL
    return state && state->building.has_value();
#else
    return false;
#endif
}
std::size_t DirectTerrain::textureBytes() const { return state ? state->bytes : 0; }
std::size_t DirectTerrain::uploadBytes() const { return state ? state->uploaded : 0; }
unsigned DirectTerrain::draws() const { return state ? state->frames : 0; }
DirectTerrain::DiscoveryMetrics DirectTerrain::discoveryMetrics() const
{ return state ? state->discoveryStats : DiscoveryMetrics{}; }
