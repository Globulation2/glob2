// SPDX-License-Identifier: GPL-3.0-or-later
#include <SpriteLoad.h>
#include "GraphicContextPrivate.h"
#include <Toolkit.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace GAGCore {
namespace {
struct Source {
    std::string path;
    bool rotated = false;
    unsigned first = 0, count = 1;
    int width = 0, height = 0;
};
struct Sources { std::vector<Source> entries; size_t frames = 0; bool sheets = false; };
std::shared_ptr<const Sources> discover(const AssetLoader::Directory& directory, std::shared_ptr<const AssetLoader::Bytes> index, const std::string& name, bool sheets) {
    auto result = std::make_shared<Sources>();
    if (sheets) {
        if (index && index->size() <= 1024 * 1024) {
            std::istringstream input(std::string(index->begin(), index->end()));
            result->sheets = true;
            std::string line;
            size_t manifestBytes = 0;
            while (std::getline(input, line)) {
                if (line.ends_with('\r')) line.pop_back();
                manifestBytes += line.size();
                if (manifestBytes > 1024 * 1024) break;
                if (line.empty() || line[0] == '#') continue;
                Source entry; std::string layer;
                std::istringstream fields(line);
                fields >> entry.path >> layer >> entry.first >> entry.count >> entry.width >> entry.height;
                if (!fields || !entry.path.ends_with(".webp") || (layer != "image" && layer != "rotated") ||
                    entry.path.find_first_of("/\\:") != std::string::npos || !entry.count ||
                    entry.first > 65536 || entry.count > 65536 - entry.first ||
                    entry.width <= 0 || entry.height <= 0 || entry.width > 4096 || entry.height > 4096) {
                    result->entries.clear(); break;
                }
                entry.path = name.substr(0, name.rfind('/') + 1) + entry.path;
                entry.rotated = layer == "rotated";
                result->frames = std::max(result->frames, size_t(entry.first) + entry.count);
                result->entries.push_back(std::move(entry));
            }
            if (!result->entries.empty() && manifestBytes <= 1024 * 1024) return result;
            result->entries.clear(); result->frames = 0; result->sheets = false;
        }
    }
    // Enumerate once per directory rather than opening the next frame and
    // decoding it before discovering subsequent work. Earlier directories win.
    std::unordered_set<std::string> names;
    const auto slash = name.rfind('/');
    const auto prefix = name.substr(slash == std::string::npos ? 0 : slash + 1);
    for (const auto &file : directory.names) names.insert(file);
    for (unsigned frame = 0; frame < 65536; ++frame) {
        bool found = false;
        for (bool rotated : {false, true}) {
            const auto file = prefix + std::to_string(frame) + (rotated ? "r.webp" : ".webp");
            if (!names.contains(file)) continue;
            found = true;
            result->entries.push_back({name + std::to_string(frame) + (rotated ? "r.webp" : ".webp"), rotated, frame});
        }
        if (!found) break;
        result->frames = frame + 1;
    }
    return result;
}
struct Prepared {
    std::vector<std::shared_ptr<const AssetImage>> images, rotated;
    std::shared_ptr<AssetImage> atlas;
    int cellWidth = 0, cellHeight = 0, columns = 0;
};
// Compute the allocation plan once for both admission and preparation. Variable
// frame atlases use the largest frame in every cell, not the sum of input areas.
struct PreparedLayout {
    int width = 0, height = 0, columns = 0, rows = 0;
    size_t workingBytes = 0;
    bool atlas = false;
};
PreparedLayout preparedLayout(const Sources& sources, const std::vector<AssetLoader::Handle<AssetImage>>& handles,
        int maxTextureSize, bool variableAtlas) {
    PreparedLayout layout;
    layout.workingBytes = sources.frames * 2 * sizeof(std::shared_ptr<const AssetImage>);
    size_t imageFrames = 0;
    bool eligible = maxTextureSize != 0;
#if defined(GLOB2_WEBGL2) || SDL_BYTEORDER == SDL_BIG_ENDIAN
    constexpr size_t pixelBytes = 8; // CPU surface and upload copy
#else
    constexpr size_t pixelBytes = 4;
#endif
    for (size_t i = 0; i < sources.entries.size(); ++i) {
        const auto& entry = sources.entries[i];
        const auto image = handles[i].get();
        if (!image) throw std::runtime_error("Sprite image unavailable: " + entry.path);
        const int width = sources.sheets ? entry.width : image->surface->w;
        const int height = sources.sheets ? entry.height : image->surface->h;
        if (sources.sheets)
            layout.workingBytes += size_t(width) * height * entry.count * pixelBytes + entry.count * sizeof(AssetImage);
        if (entry.rotated) { eligible = false; continue; }
        imageFrames += entry.count;
        if (layout.width && !variableAtlas && (layout.width != width || layout.height != height)) eligible = false;
        layout.width = std::max(layout.width, width);
        layout.height = std::max(layout.height, height);
    }
    eligible = eligible && imageFrames == sources.frames;
    layout.columns = int(std::ceil(std::sqrt(sources.frames)));
    layout.rows = layout.columns ? (int(sources.frames) + layout.columns - 1) / layout.columns : 0;
    layout.atlas = eligible && (layout.width + 2) * layout.columns <= maxTextureSize &&
        (layout.height + 2) * layout.rows <= maxTextureSize;
    if (layout.atlas)
        layout.workingBytes += size_t(layout.width + 2) * layout.columns * (layout.height + 2) * layout.rows * pixelBytes;
    return layout;
}
std::shared_ptr<const Prepared> cut(const Sources& sources, const std::vector<AssetLoader::Handle<AssetImage>>& handles, int maxTextureSize, bool variableAtlas) {
    const auto layout = preparedLayout(sources, handles, maxTextureSize, variableAtlas);
    auto result = std::make_shared<Prepared>();
    result->images.resize(sources.frames); result->rotated.resize(sources.frames);
    for (size_t s = 0; s < sources.entries.size(); ++s) {
        const auto &entry = sources.entries[s];
        auto image = handles[s].get();
        if (!image) throw std::runtime_error("Sprite image unavailable: " + entry.path);
        const auto *sheet = image->surface;
        const auto columns = entry.width ? sheet->w / entry.width : 1;
        if (sources.sheets && (!columns || sheet->w % entry.width ||
            std::uint64_t((entry.count + columns - 1) / columns) * entry.height > unsigned(sheet->h)))
            throw std::runtime_error("Invalid sprite sheet: " + entry.path);
        for (unsigned i = 0; i < entry.count; ++i) {
            auto &target = entry.rotated ? result->rotated[entry.first + i] : result->images[entry.first + i];
            if (target) throw std::runtime_error("Duplicate sprite frame: " + entry.path);
            if (!sources.sheets) { target = image; continue; }
            auto *tile = SDL_CreateSurface(entry.width, entry.height, SDL_PIXELFORMAT_ARGB8888);
            if (!tile) throw std::runtime_error(SDL_GetError());
            auto owned = std::make_shared<AssetImage>(tile);
            for (int row = 0; row < entry.height; ++row)
                std::memcpy(static_cast<unsigned char*>(tile->pixels) + row * tile->pitch,
                    static_cast<unsigned char*>(sheet->pixels) + ((i / columns) * entry.height + row) * sheet->pitch +
                        (i % columns) * entry.width * 4, entry.width * 4);
            owned->prepareUpload(false);
            target = std::move(owned);
        }
    }
    for (size_t i = 0; i < sources.frames; ++i)
        if (!result->images[i] && !result->rotated[i]) throw std::runtime_error("Missing sprite frame");
    if (layout.atlas) {
        const auto width = layout.width, height = layout.height;
        const auto columns = layout.columns, rows = layout.rows;
        auto *atlas = SDL_CreateSurface((width + 2) * columns, (height + 2) * rows, SDL_PIXELFORMAT_ARGB8888);
        if (!atlas) throw std::runtime_error(SDL_GetError());
        result->atlas = std::make_shared<AssetImage>(atlas);
        result->cellWidth = width + 2; result->cellHeight = height + 2; result->columns = columns;
        for (size_t i = 0; i < result->images.size(); ++i) {
            const auto *input = result->images[i]->surface;
            const int x = int(i % columns) * (width + 2) + 1, y = int(i / columns) * (height + 2) + 1;
            for (int row = -1; row <= input->h; ++row) {
                const auto *source = static_cast<const unsigned char*>(input->pixels) + std::clamp(row, 0, input->h - 1) * input->pitch;
                auto *dest = static_cast<unsigned char*>(atlas->pixels) + (y + row) * atlas->pitch + (x - 1) * 4;
                std::memcpy(dest, source, 4);
                std::memcpy(dest + 4, source, input->w * 4);
                std::memcpy(dest + (input->w + 1) * 4, source + (input->w - 1) * 4, 4);
            }
        }
    }
    if (result->atlas) result->atlas->prepareUpload(false);
    return result;
}
}
struct SpriteLoad::Impl {
    std::string name, failure;
    bool variableAtlas = false, retried = false, complete = false;
    AssetLoader::Handle<Sources> discovery;
    AssetLoader::Handle<Prepared> prepared;
    std::vector<AssetLoader::Handle<AssetImage>> images, highResolution;
    std::shared_ptr<const Sources> sources;
    std::shared_ptr<const Prepared> pixels;
    std::unique_ptr<Sprite> sprite;
    size_t frame = 0;
    int maxTextureSize = 0;
};
SpriteLoad::SpriteLoad(std::string name, bool variableAtlas) : impl(std::make_unique<Impl>()) {
    impl->name = std::move(name); impl->variableAtlas = variableAtlas;
#ifdef HAVE_OPENGL
    if (_gc && (_gc->getOptionFlags() & GraphicContext::USEGPU))
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &impl->maxTextureSize);
#endif

    auto &loader = Toolkit::assets();
    auto directory = loader.requestDirectory((impl->name.find('/') == std::string::npos ? std::string() : impl->name.substr(0, impl->name.rfind('/'))));
    auto index = loader.requestBytes(impl->name + ".sheet");
    impl->discovery = loader.request<Sources>("sprite-index:" + impl->name, {directory.dependency(), index.dependency(false)},
        [directory, index, name = impl->name] { return discover(*directory.get(), index.get(), name, true); });
}
SpriteLoad::~SpriteLoad() = default;
bool SpriteLoad::poll(std::chrono::milliseconds budget) {
    return pollUntil(std::chrono::steady_clock::now() + budget);
}
bool SpriteLoad::pollUntil(std::chrono::steady_clock::time_point deadline) {
    if (impl->complete) return true;
    if (std::chrono::steady_clock::now() >= deadline) return false;
    auto &loader = Toolkit::assets();
    loader.poll(std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
    if (std::chrono::steady_clock::now() >= deadline) return false;
    if (!impl->sources) {
        if (impl->discovery.pending()) return false;
        impl->sources = impl->discovery.get();
        if (!impl->sources || !impl->sources->frames) {
            impl->failure = "No frames for sprite: " + impl->name; impl->complete = true; return true;
        }
        impl->highResolution = Sprite::prefetchHighResolution(impl->name, impl->sources->frames);
        std::vector<AssetLoader::Dependency> dependencies;
        for (const auto &source : impl->sources->entries) {
            impl->images.push_back(loader.requestImage(source.path));
            dependencies.push_back(impl->images.back().dependency());
        }
        impl->prepared = loader.requestEstimated<Prepared>("sprite-pixels:" + impl->name + (impl->retried ? ":frames" : ":sheets") + ":" + std::to_string(impl->maxTextureSize) + (impl->variableAtlas ? ":variable" : ":equal"),
            std::move(dependencies), [sources = impl->sources, images = impl->images, maximum = impl->maxTextureSize, variable = impl->variableAtlas] { return cut(*sources, images, maximum, variable); },
            [sources = impl->sources, images = impl->images, maximum = impl->maxTextureSize, variable = impl->variableAtlas] {
                return preparedLayout(*sources, images, maximum, variable).workingBytes;
            });
    }
    if (!impl->pixels) {
        if (impl->prepared.pending()) return false;
        impl->pixels = impl->prepared.take();
        if (!impl->pixels) impl->pixels = impl->prepared.get();
        if (!impl->pixels) {
            if (impl->sources->sheets && !impl->retried) {
                impl->retried = true; impl->sources.reset(); impl->images.clear(); impl->prepared = {};
                auto directory = loader.requestDirectory((impl->name.find('/') == std::string::npos ? std::string() : impl->name.substr(0, impl->name.rfind('/'))));
                impl->discovery = loader.request<Sources>("sprite-frame-index:" + impl->name, {directory.dependency()},
                    [directory, name = impl->name] { return discover(*directory.get(), {}, name, false); });
                return false;
            }
            impl->failure = "Cannot prepare sprite: " + impl->name + ": " + impl->prepared.error();
            impl->complete = true; return true;
        }
        impl->images.clear(); impl->prepared = {};
        impl->sprite = std::make_unique<Sprite>();
        impl->sprite->fileName = impl->name;
        impl->sprite->dynamicTeamColor = impl->name == "data/gfx/unit";
    }
    if (std::any_of(impl->highResolution.begin(), impl->highResolution.end(), [](const auto& handle) { return handle.pending(); })) return false;
    while (impl->frame < impl->sources->frames && std::chrono::steady_clock::now() < deadline) {
        auto adopt = [&](const std::shared_ptr<const AssetImage>& image) -> DrawableSurface* {
            if (!image) return nullptr;
            // Prepared frames can be shared by several requests; isolate mutable
            // live pixels. The copy happens once, with no format conversion.
            const bool exclusive = impl->pixels.use_count() == 1 && image.use_count() == 1;
            auto result = DrawableSurface::fromAssetImage(*image, exclusive, !impl->pixels->atlas);
            if (!impl->pixels->atlas) result->prepareTexture();
            return result.release();
        };
        const auto frame = impl->frame++;
        impl->sprite->images.push_back(adopt(impl->pixels->images[frame]));
        auto *rotated = adopt(impl->pixels->rotated[frame]);
        impl->sprite->rotated.push_back(rotated ? new Sprite::RotatedImage(rotated) : nullptr);
        impl->sprite->appendHighResolutionFrame(frame, *impl->sprite);
    }
    if (impl->frame != impl->sources->frames || std::chrono::steady_clock::now() >= deadline) return false;
    const bool uncolored = std::all_of(impl->sprite->rotated.begin(), impl->sprite->rotated.end(), [](auto p) { return !p; });
#ifdef HAVE_OPENGL
    if (impl->pixels->atlas) {
        auto atlas = DrawableSurface::fromAssetImage(*impl->pixels->atlas, impl->pixels.use_count() == 1);
        atlas->uploadToTexture();
        for (size_t i = 0; i < impl->sprite->images.size(); ++i) {
            auto *image = impl->sprite->images[i];
            image->textureInfo = TextureInfo{impl->sprite.get(), int(i % impl->pixels->columns) * impl->pixels->cellWidth + 1,
                int(i / impl->pixels->columns) * impl->pixels->cellHeight + 1, image->getW(), image->getH()};
            image->texMultX = atlas->texMultX; image->texMultY = atlas->texMultY;
        }
        impl->sprite->atlas = std::move(atlas);
#ifndef GLOB2_WEBGL2
        glGenBuffers(1, &impl->sprite->vbo); glGenBuffers(1, &impl->sprite->texCoordBuffer);
#endif
    } else
#endif
    if (uncolored) impl->sprite->createTextureAtlas(impl->variableAtlas);
    impl->sprite->recomputeBlockCompleteHD(); impl->sprite->createHighResolutionAtlas();
    impl->sprite->registerLoaded();
    impl->pixels.reset(); impl->sources.reset(); impl->prepared = {}; impl->images.clear(); impl->highResolution.clear();
    impl->complete = true;
    return true;
}
bool SpriteLoad::failed() const { return !impl->failure.empty(); }
std::string SpriteLoad::error() const { return impl->failure; }
std::unique_ptr<Sprite> SpriteLoad::take() { return impl->complete ? std::move(impl->sprite) : nullptr; }
size_t SpriteLoad::completed() const { return impl->frame; }
size_t SpriteLoad::total() const { return impl->sources ? impl->sources->frames : impl->frame; }
}
