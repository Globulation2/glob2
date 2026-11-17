// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TerrainMaterials.h"
#include <SDL3/SDL_stdinc.h>
#include <array>
#include <map>
#include <coroutine>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

class SceneMap;
namespace TerrainVisual { class Compositor; }

// Map-space source data shared by direct terrain draws. Texel layout is part
// of the shader contract. Camera transforms and animation do not rebuild it.
struct DirectTerrainData
{
    using Pixel = std::array<Uint8, 4>;
    using Record = std::array<Uint16, 4>;
    static constexpr int TilePixels = 32;
    static constexpr int MaskPixels = 16;
    static constexpr Uint16 PendingMask = 65535;
    // Legacy GL: a mixed cell without a built mask holds PendingCorners plus
    // the palette slot of each corner, two bits each (top-left in the low
    // bits, then top-right, bottom-left, bottom-right), so the shader can draw
    // a straight-edged split until the mask lands. Mask atlas slots stay below.
    static constexpr Uint16 PendingCorners = 0xFF00;
    static constexpr int AtlasWidth = 2048;
    static constexpr int VariantWidth = 256;
    static constexpr unsigned VariantRecords = 8;

    // One palette and coverage index per wrapped cell. Index zero is uniform.
    std::vector<Record> cells;
    std::vector<Uint16> masks;
    std::vector<Pixel> art, coverage;
    // Coverage and edges share tile coordinates. Edge texels pack a Q8
    // margin in the low 12 bits and two palette selectors in the high bits.
    std::vector<Uint16> edges;
    std::vector<Record> variants;
    std::vector<Pixel> calm;
    int artHeight, coverageHeight, variantHeight;

    // One entry per variant-table slot, including the unused slot zero.
    // The Legacy GL animation pass streams the visible phase endpoints.
    struct Source { TerrainVisual::MaterialId material; unsigned variant; };
    std::vector<Source> sources;
    std::vector<TerrainVisual::Recipe> pendingCoverage;
    struct CoverageTile
    {
        std::array<Pixel, MaskPixels * MaskPixels> weights;
        std::array<Uint16, MaskPixels * MaskPixels> edges;
    };
    static CoverageTile makeCoverage(const TerrainVisual::Compositor &, const TerrainVisual::Recipe &);

    // Variant records for streamed artwork (Legacy GL): one slot per material
    // variant a cell uses, with art index zero until the art atlas places it.
    // Kept across terrain edits so a re-described cell extends the same table.
    struct Palette
    {
        std::vector<Record> variants = std::vector<Record>(VariantRecords);
        std::vector<Source> sources = std::vector<Source>(1);
        std::map<std::pair<TerrainVisual::MaterialId, unsigned>, Uint16> slots;
        Uint16 slot(const TerrainVisual::Catalog &, const TerrainVisual::Recipe &, TerrainVisual::MaterialId);
    };
    // A cell's palette record, mixed-cell marker (PendingCorners with its
    // corner slots, or zero when uniform) and shore calm, from its recipe.
    static void describeCell(Palette &, const TerrainVisual::Catalog &, const TerrainVisual::Recipe &,
                             Record &cell, Uint16 &mask, Pixel &calm);
    static void appendRecords(std::vector<Record> &variants, const TerrainVisual::Material &, Uint16 firstArt);
    Palette palette; // Tables mode only; variants and sources stay empty then.

    // Tiles of a streamed art atlas, in pairs. An animated variant holds a
    // whole pair (its current and next phase, the first tile even); a static
    // one holds a single tile, sharing its pair with another static variant.
    struct ArtTiles
    {
        std::vector<unsigned> freePairs;  // Pairs with both tiles free.
        std::vector<unsigned> freeHalves; // Free tiles whose partner holds a static variant.
        std::vector<unsigned> owners;     // Each tile's variant slot plus one, or zero.

        void reset(unsigned tiles)
        {
            freeHalves.clear();
            freePairs.clear();
            for (unsigned pair = tiles / 2; pair > 0; --pair) freePairs.push_back(pair - 1);
            owners.assign(tiles / 2 * 2, 0);
        }
        // The first tile for a variant of `span` tiles (1 or 2), or false when
        // no free pair, or for a static variant no free tile, is left.
        bool claim(unsigned slot, unsigned span, unsigned &tile)
        {
            if (span == 1 && !freeHalves.empty())
            {
                tile = freeHalves.back();
                freeHalves.pop_back();
            }
            else if (!freePairs.empty())
            {
                tile = freePairs.back() * 2;
                freePairs.pop_back();
                if (span == 1) freeHalves.push_back(tile + 1);
            }
            else
                return false;
            for (unsigned i = 0; i < span; ++i) owners[tile + i] = slot + 1;
            return true;
        }
        void release(unsigned tile, unsigned span)
        {
            for (unsigned i = 0; i < span; ++i) owners[tile + i] = 0;
            if (span == 2) { freePairs.push_back(tile / 2); return; }
            const unsigned partner = tile ^ 1u;
            if (owners[partner]) { freeHalves.push_back(tile); return; }
            // Both tiles of the pair are free again.
            std::erase(freeHalves, partner);
            freePairs.push_back(tile / 2);
        }
        // When every free tile is the half of a pair whose other half holds a
        // static variant, moves one such variant into another free half,
        // freeing a whole pair. Returns false when fewer than two tiles are
        // free; otherwise `slot` is the variant moved and `tile` its new tile.
        bool freePair(unsigned &slot, unsigned &tile)
        {
            if (!freePairs.empty()) return false;
            if (freeHalves.size() < 2) return false;
            const unsigned from = freeHalves.back() ^ 1u;
            slot = owners[from] - 1;
            release(from, 1);
            return claim(slot, 1, tile);
        }
    };

    class Build;
    // Tables mode builds only the per-cell tables and the palette: no native
    // artwork or coverage, which Legacy GL streams at the display density.
    enum class Mode { Native, NativeStreamedCoverage, Tables };
    static Build begin(SceneMap, std::shared_ptr<TerrainVisual::Compositor>, int maximumTextureSize, Mode);
    static DirectTerrainData build(const SceneMap &, std::shared_ptr<TerrainVisual::Compositor>, int maximumTextureSize);
};

class DirectTerrainData::Build
{
public:
    struct promise_type
    {
        DirectTerrainData data;
        std::exception_ptr error;
        Build get_return_object() { return Build(std::coroutine_handle<promise_type>::from_promise(*this)); }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(int) noexcept { return {}; }
        void return_value(DirectTerrainData value) { data = std::move(value); }
        void unhandled_exception() { error = std::current_exception(); }
    };

    Build(Build &&other) noexcept : handle(std::exchange(other.handle, {})) {}
    ~Build() { if (handle) handle.destroy(); }

    bool advance()
    {
        if (!handle.done()) handle.resume();
        if (handle.promise().error) std::rethrow_exception(handle.promise().error);
        return handle.done();
    }

    DirectTerrainData take() { return std::move(handle.promise().data); }

private:
    explicit Build(std::coroutine_handle<promise_type> handle) : handle(handle) {}
    std::coroutine_handle<promise_type> handle;
};
