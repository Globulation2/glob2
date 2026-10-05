// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace GAGCore
{
class StreamBackend;
class DrawableSurface;
// Experimental GSK1: one UV/index topology, one static pose or 8 x 32 animated poses.
// Positions are in the original orthographic camera's clip space; normals are
// in camera space. Loading is transactional and bounded, including on failure.
struct SkinMesh
{
    std::uint64_t identity = 0; // fresh on every successful load
    std::uint32_t vertices = 0, frames = 0, logicalSize = 0;
    std::vector<float> uv;
    std::vector<std::uint32_t> indices;
    std::vector<float> poses; // xyz, normal xyz; frame-major
    // Cosmetic camera rotation; returns a separately identified static mesh.
    SkinMesh rotatedView(unsigned angle, const std::array<float, 16> &inverse,
                         const std::array<float, 16> &projection, const std::array<float, 9> &normals) const;
    bool load(const std::string &path, std::string &error);
    bool load(StreamBackend &input, std::string &error);
};
// colony-v2 skins share one 512x512 colour atlas and one material-id map per
// team; each model's UVs map into its own quadrant (see SkinRegion).
enum SkinRegion : std::uint8_t { SkinRegionWorker = 0, SkinRegionWarrior = 1, SkinRegionExplorer = 2, SkinRegionSwarm = 3 };
// Loads a colony-v2 material-id map. Each texel's id (0..255) is written as
// an opaque grey (r=g=b=id) so the GPU sampler reads it back exactly.
// Greyscale PNGs decode to INDEX8 surfaces whose generated palette is not an
// exact identity ramp in every SDL_image build, so grey-ramp palettes use the
// pixel index itself. Returns null when the file is missing or undecodable.
std::unique_ptr<DrawableSurface> loadSkinMaterialMap(const std::string &path);
struct SkinMeshRequest
{
    const SkinMesh *mesh;
    unsigned frame;
    DrawableSurface *texture;  // colour atlas
    DrawableSurface *material; // material ids (0 glossy, 1 matte, 2 metallic, 3 hairy)
    std::uint8_t region;       // SkinRegion: the atlas quadrant this mesh samples
};
}
