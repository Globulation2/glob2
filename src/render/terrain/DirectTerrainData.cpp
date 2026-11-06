// SPDX-License-Identifier: GPL-3.0-or-later
#include "DirectTerrainData.h"
#include "TerrainCompositor.h"
#include "render/scene/SceneMap.h"
#include <PresentationWork.h>
#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
using Pixel = DirectTerrainData::Pixel;
using Record = DirectTerrainData::Record;
constexpr int TilePixels = DirectTerrainData::TilePixels;
constexpr int MaskPixels = DirectTerrainData::MaskPixels;
// Map tables, native art and a native coverage mask for every mixed cell. A
// 512x512 map holds tens of thousands of mixed cells; desktop GPUs keep them
// all resident, so large maps never fall back to the page compositor.
#if defined(GLOB2_MOBILE) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)
constexpr std::size_t TextureBudget = 64u * 1024 * 1024;
#else
constexpr std::size_t TextureBudget = 256u * 1024 * 1024;
#endif

template<class Texel, int Pixels = TilePixels>
struct Atlas
{
    static constexpr int AtlasColumns = DirectTerrainData::AtlasWidth / Pixels;
    int rows = 1;
    unsigned count = 0;
    std::vector<Texel> pixels = std::vector<Texel>(AtlasColumns * Pixels * Pixels);
    static constexpr int Width = AtlasColumns * Pixels;
    int height() const { return rows * Pixels; }

    void reserveTiles(unsigned tiles, int maxSize, std::size_t &remainingBytes, bool allocate = true)
    {
        const int neededRows = std::max(1u, (tiles + AtlasColumns - 1) / AtlasColumns);
        if (neededRows <= rows)
            return;
        const auto size = std::size_t(Width) * neededRows * Pixels;
        const auto extra = (size - pixels.size()) * sizeof(Texel);
        const int height = neededRows * Pixels;
        if (height > maxSize || extra > remainingBytes)
        {
            const auto dimensions = std::to_string(Width) + "x" + std::to_string(height);
            if (height > maxSize)
                throw std::runtime_error("Direct terrain atlas " + dimensions +
                                         " exceeds device texture limit " + std::to_string(maxSize));
            throw std::runtime_error("Direct terrain atlas " + dimensions + " needs " +
                                     std::to_string(extra) + " additional bytes with " +
                                     std::to_string(remainingBytes) + " bytes left in the terrain budget");
        }
        if (allocate) pixels.resize(size);
        rows = neededRows;
        remainingBytes -= extra;
    }

    template<class Write>
    void writeTile(unsigned index, Write write)
    {
        const int x = (index % AtlasColumns) * Pixels;
        const int y = (index / AtlasColumns) * Pixels;
        for (int iy = 0; iy < Pixels; ++iy)
            write(pixels.data() + std::size_t(y + iy) * Width + x, iy);
    }

    template<class Write>
    void append(int maxSize, std::size_t &remainingBytes, Write write)
    {
        reserveTiles(count + 1, maxSize, remainingBytes);
        writeTile(count++, write);
    }
};
}

DirectTerrainData::Build DirectTerrainData::begin(SceneMap map, std::shared_ptr<TerrainVisual::Compositor> owner,
                                                  int maxSize, bool streamCoverage)
{
    const int width = map.getW(), height = map.getH();
    if (width <= 0 || height <= 0 || maxSize < AtlasWidth || width > maxSize || height > maxSize)
        throw std::runtime_error("Direct terrain map exceeds the texture limits");

    // The scene and compositor remain owned across suspensions.
    auto &compositor = *owner;
    const auto &catalog = compositor.catalog();
    const auto count = std::size_t(width) * height;
    Atlas<Pixel> art;
    Atlas<Pixel, MaskPixels> coverage;
    Atlas<Uint16, MaskPixels> edges;
    const auto mapBytes = count * (sizeof(Record) + sizeof(Uint16) + sizeof(Pixel) + sizeof(Uint8));
    const auto initialAtlasBytes = (art.pixels.size() + coverage.pixels.size()) * sizeof(Pixel) +
        edges.pixels.size() * sizeof(Uint16);
    if (mapBytes > TextureBudget - initialAtlasBytes)
        throw std::runtime_error("Direct terrain map tables exceed the texture budget");
    auto remainingBytes = TextureBudget - initialAtlasBytes - mapBytes;
    // Keep this stride in sync with variantRecord() in the shader.
    constexpr unsigned VariantRecords = DirectTerrainData::VariantRecords;
    std::vector<Record> variants(VariantRecords); // Slot zero represents an unused palette entry.
    std::vector<DirectTerrainData::Source> sources(1);
    std::map<std::pair<TerrainVisual::MaterialId, unsigned>, Uint16> variantSlots;
    const auto variantSlot = [&](const TerrainVisual::Recipe &recipe, TerrainVisual::MaterialId id) {
        const auto variant = catalog.variantIndex(id, recipe.x, recipe.y, recipe.seed);
        const auto key = std::pair{id, variant};
        if (const auto found = variantSlots.find(key); found != variantSlots.end())
            return found->second;
        const auto &material = catalog.materials.at(id);
        if (variants.size() / VariantRecords >= 65536 || art.count + material.animationFrames >= 65536 ||
            material.animationTicks > 65535 || material.animationBlendSteps > 65535)
            throw std::runtime_error("Direct terrain variant metadata exceeds sixteen bits");
        const auto first = static_cast<Uint16>(art.count);
        for (int phase = 0; phase < material.animationFrames; ++phase)
        {
            const auto pixels = compositor.nativeMaterialPixels(id, variant, phase);
            if (pixels.size() != TilePixels * TilePixels)
                throw std::runtime_error("Direct terrain requires 32-pixel native material frames");
            art.append(maxSize, remainingBytes, [&](Pixel *out, int y) {
                std::copy_n(pixels.data() + y * TilePixels, TilePixels, out);
            });
        }
        const auto slot = static_cast<Uint16>(variants.size() / VariantRecords);
        const int blendSteps = material.animationFrames > 1 && material.animationTicks > 1
            ? (material.animationBlendSteps ? material.animationBlendSteps : material.animationTicks) : 0;
        variants.push_back({first, Uint16(material.animationFrames), Uint16(material.animationTicks), Uint16(blendSteps)});
        const auto &surf = material.surf;
        variants.push_back({Uint16(surf.period), Uint16(surf.reach), Uint16(surf.foam), Uint16(surf.foamWidth)});
        variants.push_back({Uint16(surf.shallow), Uint16(surf.shallowWidth), Uint16(surf.wet), Uint16(surf.wetWidth)});
        variants.push_back({surf.shallowColor[0], surf.shallowColor[1], surf.shallowColor[2], Uint16(surf.ticks)});
        // The fourth field damps a liquid's wash against this material as land;
        // zero keeps the full wash.
        variants.push_back({surf.foamColor[0], surf.foamColor[1], surf.foamColor[2], Uint16(256 - material.shoreWash)});
        const auto &seam = material.seam;
        const auto cast = material.decor.connected.empty() ? seam.cast : 0;
        variants.push_back({Uint16(seam.height), Uint16(cast), Uint16(seam.castWidth), Uint16(seam.fringe)});
        variants.push_back({seam.fringeColor[0], seam.fringeColor[1], seam.fringeColor[2], Uint16(seam.fringeWidth)});
        const auto &mean = material.calmMean;
        variants.push_back({mean[0], mean[1], mean[2],
            Uint16(material.shoreCalm ? 256 - TerrainVisual::ShoreCalmContrastQ8 : 0)});
        variantSlots.emplace(key, slot);
        sources.push_back({id, variant});
        return slot;
    };

    std::vector<Record> cells(count);
    std::vector<Uint16> masks(count);
    std::vector<Pixel> calm(count);
    std::vector<TerrainVisual::Recipe> mixed;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto i = map.coordToIndex(x, y);
            if (streamCoverage && i % 64 == 0) co_yield 0;
            const auto recipe = compositor.describe(map, x, y);
            calm[i] = recipe.calm;
            if (recipe.uniform())
            {
                cells[i][0] = variantSlot(recipe, recipe.corners.front());
                continue;
            }
            mixed.push_back(recipe);
        }

    // Each job owns distinct atlas tiles. Material preparation for mixed
    // cells runs on the owner after all coverage jobs have joined.
    if (mixed.size() >= PendingMask)
        throw std::runtime_error("Direct terrain coverage indices exceed sixteen bits");
    if (streamCoverage) co_yield 0;
    coverage.reserveTiles(mixed.size(), maxSize, remainingBytes, !streamCoverage);
    if (streamCoverage) co_yield 0;
    edges.reserveTiles(mixed.size(), maxSize, remainingBytes, !streamCoverage);
    const std::size_t cellsPerJob = GAGCore::PresentationWork::current &&
        GAGCore::renderExecution() == GAGCore::RenderExecution::Parallel
        ? 16 : std::max(std::size_t(1), mixed.size());
    for (std::size_t i = 0; i < mixed.size(); ++i)
    {
        if (streamCoverage && i % 64 == 0) co_yield 0;
        const auto &recipe = mixed[i];
        const auto cell = map.coordToIndex(recipe.x, recipe.y);
        auto &record = cells[cell];
        // The record's slots follow the coverage palette: corners, then stones.
        std::array<TerrainVisual::MaterialId, 4> palette;
        for (unsigned slot = 0, n = recipe.palette(palette); slot < n; ++slot)
            record[slot] = variantSlot(recipe, palette[slot]);
        masks[cell] = Uint16(i + 1);
    }
    if (!streamCoverage)
        GAGCore::executePresentationJobs((mixed.size() + cellsPerJob - 1) / cellsPerJob,
            [&](std::size_t job) {
                const auto end = std::min(mixed.size(), (job + 1) * cellsPerJob);
                for (auto i = job * cellsPerJob; i < end; ++i)
                {
                    const auto tile = makeCoverage(compositor, mixed[i]);
                    coverage.writeTile(i, [&](Pixel *out, int row) {
                        std::copy_n(tile.weights.data() + row * MaskPixels, MaskPixels, out);
                    });
                    edges.writeTile(i, [&](Uint16 *out, int row) {
                        std::copy_n(tile.edges.data() + row * MaskPixels, MaskPixels, out);
                    });
                }
            });

    const int variantHeight = int((variants.size() + DirectTerrainData::VariantWidth - 1) /
                                  DirectTerrainData::VariantWidth);
    variants.resize(DirectTerrainData::VariantWidth * variantHeight);
    if (variants.size() * sizeof(Record) > remainingBytes || variantHeight > maxSize)
        throw std::runtime_error("Direct terrain variant table exceeds the texture limits");
    const auto coverageHeight = coverage.height();
    if (streamCoverage)
    {
        std::vector<Pixel>().swap(coverage.pixels);
        std::vector<Uint16>().swap(edges.pixels);
    }
    co_return DirectTerrainData{std::move(cells), std::move(masks), std::move(art.pixels),
            std::move(coverage.pixels), std::move(edges.pixels), std::move(variants),
            std::move(calm), art.height(), coverageHeight, variantHeight,
            std::move(sources),
            streamCoverage ? std::move(mixed) : std::vector<TerrainVisual::Recipe>{}};
}

DirectTerrainData DirectTerrainData::build(const SceneMap &map, std::shared_ptr<TerrainVisual::Compositor> compositor, int maxSize)
{
    auto build = begin(map, std::move(compositor), maxSize, false);
    build.advance();
    return build.take();
}

DirectTerrainData::CoverageTile DirectTerrainData::makeCoverage(
    const TerrainVisual::Compositor &compositor, const TerrainVisual::Recipe &recipe)
{
    CoverageTile tile;
    const auto mask = compositor.mask(recipe, 1);
    for (int row = 0; row < MaskPixels; ++row)
        for (int col = 0; col < MaskPixels; ++col)
        {
            const auto &sample = mask.samples[(row * 2 + 1) * TilePixels + col * 2 + 1];
            auto &weights = tile.weights[row * MaskPixels + col];
            const unsigned largest = sample.slots & 3;
            unsigned remaining = 65536;
            for (unsigned slot = 0, other = 0; slot < 4; ++slot)
                if (slot != largest)
                {
                    const unsigned weight = sample.others[other++];
                    remaining -= weight;
                    weights[slot] = Uint8((weight * 255 + 32768) / 65536);
                }
            weights[largest] = Uint8((remaining * 255 + 32768) / 65536);
            tile.edges[row * MaskPixels + col] =
                Uint16(std::min<unsigned>(sample.margin, 4095) | (unsigned(sample.slots >> 2) << 12));
        }
    return tile;
}
