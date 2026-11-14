// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <string>

struct MapRenderState;

// The renderer memory report for one map view: GAGCore::RenderMemory plus the
// view's terrain. Tracked bytes only; see docs/development/rendering-benchmarks.md.
namespace RenderMemoryReport
{
//! Tracked GPU bytes in constant time: textures, array pages, auxiliary targets and direct terrain.
std::size_t trackedGpuBytes(const MapRenderState &render);
//! Space-separated key=value fields. Walks every live surface, so call it about
//! once a second, not every frame.
std::string fields(const MapRenderState &render);
//! One "name frames=... native_cpu_bytes=... hd_cpu_bytes=... gpu_bytes=..." line per sprite, largest first.
std::string spriteLines(const char *prefix, std::size_t limit);
} // namespace RenderMemoryReport
