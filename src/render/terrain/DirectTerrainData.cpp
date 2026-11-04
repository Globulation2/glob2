// SPDX-License-Identifier: GPL-3.0-or-later
#include "DirectTerrainData.h"
#include "TerrainCompositor.h"
#include "render/scene/SceneMap.h"
#include <PresentationWork.h>
#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

namespace
{
using Pixel = DirectTerrainData::Pixel;
using Record = DirectTerrainData::Record;
constexpr int TilePixels = DirectTerrainData::TilePixels;
constexpr int AtlasColumns = DirectTerrainData::AtlasWidth / TilePixels;
constexpr std::size_t TextureBudget = 64u * 1024 * 1024;

template<class Texel>
struct Atlas
{
    int rows = 1;
    unsigned count = 0;
    std::vector<Texel> pixels = std::vector<Texel>(AtlasColumns * TilePixels * TilePixels);
    static constexpr int Width = AtlasColumns * TilePixels;
    int height() const { return rows * TilePixels; }

    void reserveTiles(unsigned tiles, int maxSize, std::size_t &remainingBytes)
    {
        const int neededRows = std::max(1u, (tiles + AtlasColumns - 1) / AtlasColumns);
        if (neededRows <= rows)
            return;
        const auto size = std::size_t(Width) * neededRows * TilePixels;
        const auto extra = (size - pixels.size()) * sizeof(Texel);
        if (neededRows * TilePixels > maxSize || extra > remainingBytes)
            throw std::runtime_error("Direct terrain atlas exceeds the prototype budget");
        pixels.resize(size);
        rows = neededRows;
        remainingBytes -= extra;
    }

    template<class Write>
    void writeTile(unsigned index, Write write)
    {
        const int x = (index % AtlasColumns) * TilePixels;
        const int y = (index / AtlasColumns) * TilePixels;
        for (int iy = 0; iy < TilePixels; ++iy)
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

DirectTerrainData DirectTerrainData::build(const SceneMap &map, TerrainVisual::Compositor &compositor,
                                           int maxSize)
{
    const int width = map.getW(), height = map.getH();
    if (width <= 0 || height <= 0 || maxSize < AtlasWidth || width > maxSize || height > maxSize)
        throw std::runtime_error("Direct terrain map exceeds the texture limits");

    const auto &catalog = compositor.catalog();
    const auto count = std::size_t(width) * height;
    Atlas<Pixel> art, coverage;
    Atlas<Uint16> edges;
    const auto mapBytes = count * (sizeof(Record) + sizeof(Uint16) + sizeof(Pixel) + sizeof(Uint8));
    const auto initialAtlasBytes = (art.pixels.size() + coverage.pixels.size()) * sizeof(Pixel) +
        edges.pixels.size() * sizeof(Uint16);
    if (mapBytes > TextureBudget - initialAtlasBytes)
        throw std::runtime_error("Direct terrain map tables exceed the texture budget");
    auto remainingBytes = TextureBudget - initialAtlasBytes - mapBytes;
    // Keep this stride in sync with variantRecord() in the shader.
    constexpr unsigned VariantRecords = 8;
    std::vector<Record> variants(VariantRecords); // Slot zero represents an unused palette entry.
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
        variants.push_back({surf.foamColor[0], surf.foamColor[1], surf.foamColor[2], 0});
        const auto &seam = material.seam;
        const auto cast = material.decor.connected.empty() ? seam.cast : 0;
        variants.push_back({Uint16(seam.height), Uint16(cast), Uint16(seam.castWidth), Uint16(seam.fringe)});
        variants.push_back({seam.fringeColor[0], seam.fringeColor[1], seam.fringeColor[2], Uint16(seam.fringeWidth)});
        const auto &mean = material.calmMean;
        variants.push_back({mean[0], mean[1], mean[2],
            Uint16(material.shoreCalm ? 256 - TerrainVisual::ShoreCalmContrastQ8 : 0)});
        variantSlots.emplace(key, slot);
        return slot;
    };

    std::vector<Record> cells(count);
    std::vector<Uint16> masks(count);
    std::vector<Pixel> calm(count);
    struct MixedCell
    {
        std::size_t cell;
        TerrainVisual::Recipe recipe;
        std::array<TerrainVisual::MaterialId, 4> palette{};
        unsigned colors = 0;
    };
    std::vector<MixedCell> mixed;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto i = map.coordToIndex(x, y);
            const auto recipe = compositor.describe(map, x, y);
            calm[i] = recipe.calm;
            if (std::all_of(recipe.corners.begin(), recipe.corners.end(),
                            [&](auto id) { return id == recipe.corners.front(); }))
            {
                cells[i][0] = variantSlot(recipe, recipe.corners.front());
                continue;
            }
            mixed.push_back({i, recipe});
        }

    // Each job owns distinct atlas tiles. Material preparation for mixed
    // cells runs on the owner after all coverage jobs have joined.
    if (mixed.size() > 65535)
        throw std::runtime_error("Direct terrain coverage indices exceed sixteen bits");
    coverage.reserveTiles(mixed.size(), maxSize, remainingBytes);
    edges.reserveTiles(mixed.size(), maxSize, remainingBytes);
    const std::size_t cellsPerJob = GAGCore::PresentationWork::current &&
        GAGCore::renderExecution() == GAGCore::RenderExecution::Parallel
        ? 16 : std::max(std::size_t(1), mixed.size());
    const auto &coverageCompositor = compositor;
    GAGCore::executePresentationJobs((mixed.size() + cellsPerJob - 1) / cellsPerJob,
        [&](std::size_t job) {
            TerrainVisual::Compositor::CellMask mask;
            const auto end = std::min(mixed.size(), (job + 1) * cellsPerJob);
            for (auto i = job * cellsPerJob; i < end; ++i)
            {
                auto &cell = mixed[i];
                coverageCompositor.mask(cell.recipe, 1, mask);
                cell.palette = mask.palette;
                cell.colors = mask.colors;
                coverage.writeTile(i, [&](Pixel *out, int row) {
                    for (int col = 0; col < TilePixels; ++col)
                    {
                        const auto &sample = mask.samples[row * TilePixels + col];
                        const unsigned largest = sample.slots & 3;
                        unsigned remaining = 65536;
                        for (unsigned slot = 0, other = 0; slot < 4; ++slot)
                            if (slot != largest)
                            {
                                const unsigned weight = sample.others[other++];
                                remaining -= weight;
                                out[col][slot] = Uint8((weight * 255 + 32768) / 65536);
                            }
                        out[col][largest] = Uint8((remaining * 255 + 32768) / 65536);
                    }
                });
                edges.writeTile(i, [&](Uint16 *out, int row) {
                    for (int col = 0; col < TilePixels; ++col)
                    {
                        const auto &sample = mask.samples[row * TilePixels + col];
                        // Authored contact and surf bands end by Q8 margin 2048.
                        // Saturating at 4095 preserves every affected sample.
                        out[col] = Uint16(std::min<unsigned>(sample.margin, 4095) |
                                          (unsigned(sample.slots >> 2) << 12));
                    }
                });
            }
        });
    for (std::size_t i = 0; i < mixed.size(); ++i)
    {
        const auto &cell = mixed[i];
        for (unsigned slot = 0; slot < cell.colors; ++slot)
            cells[cell.cell][slot] = variantSlot(cell.recipe, cell.palette[slot]);
        masks[cell.cell] = Uint16(i + 1);
    }

    const int variantHeight = int((variants.size() + DirectTerrainData::VariantWidth - 1) /
                                  DirectTerrainData::VariantWidth);
    variants.resize(DirectTerrainData::VariantWidth * variantHeight);
    if (variants.size() * sizeof(Record) > remainingBytes || variantHeight > maxSize)
        throw std::runtime_error("Direct terrain variant table exceeds the texture limits");
    return {std::move(cells), std::move(masks), std::move(art.pixels),
            std::move(coverage.pixels), std::move(edges.pixels), std::move(variants),
            std::move(calm), art.height(), coverage.height(), variantHeight};
}
