// SPDX-License-Identifier: GPL-3.0-or-later
#include "DirectTerrain.h"
#include <GraphicContext.h>
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif

#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
#define DIRECT_TERRAIN_LEGACY_GL 1
#endif

#include "GlobalContainer.h"
#include "DirectTerrainData.h"
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
#include <optional>
#include <stdexcept>
#include <vector>

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
    std::array<GLint, 6> uniforms{};
#endif
    int width = 0, height = 0;
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
    void initialize(const SceneMap &map, const DirectTerrainData &data)
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
        const auto fragment = std::string("#version 120\n") + GAGCore::DirectTerrainFragment;
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
        const char *names[] = {"mapSize", "artSize", "coverageSize", "variantSize", "animationTime", "wholeMap"};
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
                auto &stream = state->coverageStream.emplace();
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

    glUseProgram(state->program);
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
