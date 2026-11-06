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
    // A cell's palette record, mixed-cell marker (PendingMask, or zero when
    // uniform) and shore calm, from its recipe.
    static void describeCell(Palette &, const TerrainVisual::Catalog &, const TerrainVisual::Recipe &,
                             Record &cell, Uint16 &mask, Pixel &calm);
    static void appendRecords(std::vector<Record> &variants, const TerrainVisual::Material &, Uint16 firstArt);
    Palette palette; // Tables mode only; variants and sources stay empty then.

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
