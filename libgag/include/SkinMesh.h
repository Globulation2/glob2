// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
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
    bool load(const std::string &path, std::string &error);
    bool load(StreamBackend &input, std::string &error);
};
struct SkinMeshRequest
{
    const SkinMesh *mesh;
    unsigned frame;
    DrawableSurface *texture;
};
}
