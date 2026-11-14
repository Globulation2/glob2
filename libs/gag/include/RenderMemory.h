// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Renderer memory accounting for the paths without RenderDevice telemetry
// (legacy OpenGL above all). Everything here is tracked bytes: what the
// renderer asked for, not what the driver or allocator actually holds.
// Call from the graphics thread.
namespace GAGCore::RenderMemory
{
// GPU allocations that no DrawableSurface owns.
enum class Auxiliary : unsigned { FrameCache, SkinAtlas, ViewTiles, Count };
//! Replace one class's resident bytes; the owner calls it when it allocates or frees.
void setAuxiliary(Auxiliary kind, std::size_t bytes);

struct Report
{
	//! Decoded pixels live DrawableSurfaces still hold, and how many hold some.
	std::size_t cpuSurfaceBytes = 0, cpuSurfaces = 0;
	//! Upload staging (converted pixels, mip chains, compressed blocks) not yet released.
	std::size_t cpuPreparedBytes = 0;
	//! Surface and atlas textures, without the array pages below.
	std::size_t gpuSurfaceBytes = 0;
	//! Texture-array pages, which duplicate their source textures.
	std::size_t gpuArrayBytes = 0;
	std::array<std::size_t, unsigned(Auxiliary::Count)> gpuAuxiliaryBytes{};
	//! Image decodes since launch, and the pixels they produced.
	std::uint64_t imageDecodes = 0, imageDecodedBytes = 0;
	//! The process's peak resident set, or zero where it is not measured.
	std::size_t peakResidentBytes = 0;
};
Report report();
//! Tracked GPU bytes in constant time: surface textures, array pages and auxiliary targets.
std::size_t trackedGpuBytes();
//! The report as space-separated key=value fields, without a line prefix.
std::string fields(const Report &report);

struct SpriteUsage
{
	std::string name;
	std::size_t frames = 0, nativeCpuBytes = 0, highResolutionCpuBytes = 0, gpuBytes = 0, highResolutionGpuBytes = 0;
};
//! Loaded sprites, largest first. Private sprites still loading are left out.
std::vector<SpriteUsage> sprites();
} // namespace GAGCore::RenderMemory
