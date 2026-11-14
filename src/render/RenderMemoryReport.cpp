// SPDX-License-Identifier: GPL-3.0-or-later
#include "RenderMemoryReport.h"
#include "MapRenderState.h"
#include "SoftwareTerrainCache.h"
#include "terrain/DirectTerrain.h"
#include "terrain/TerrainCompositor.h"
#include <RenderMemory.h>
#include <SDLGraphicContext.h>
#include <algorithm>

namespace RenderMemoryReport
{
std::size_t trackedGpuBytes(const MapRenderState &render)
{
	const auto *direct = render.existingDirectTerrain();
	return GAGCore::RenderMemory::trackedGpuBytes() + (direct ? direct->textureBytes() : 0);
}

std::string fields(const MapRenderState &render)
{
	const auto compositor = render.terrainCompositorDrawn.lock();
	const std::size_t compositorBytes = compositor ? compositor->sourceBytes() : 0;
	const auto *direct = render.existingDirectTerrain();
	const auto *pages = render.existingTerrainCache();
	return GAGCore::RenderMemory::fields(GAGCore::RenderMemory::report()) +
		" cpu_high_resolution_bytes=" + std::to_string(GAGCore::Sprite::highResolutionStats().cpuBytes) +
		" cpu_compositor_bytes=" + std::to_string(compositorBytes) +
		" cpu_terrain_page_bytes=" + std::to_string(pages ? pages->bytes() : 0) +
		" gpu_terrain_page_bytes=" + std::to_string(pages ? pages->residentTextureBytes() : 0) +
		" gpu_direct_terrain_bytes=" + std::to_string(direct ? direct->textureBytes() : 0);
}

std::string spriteLines(const char *prefix, std::size_t limit)
{
	std::string text;
	const auto usage = GAGCore::RenderMemory::sprites();
	for (std::size_t i = 0; i < std::min(limit, usage.size()); ++i)
		text += std::string(prefix) + " sprite=" + usage[i].name + " frames=" + std::to_string(usage[i].frames) +
			" native_cpu_bytes=" + std::to_string(usage[i].nativeCpuBytes) +
			" hd_cpu_bytes=" + std::to_string(usage[i].highResolutionCpuBytes) +
			" gpu_bytes=" + std::to_string(usage[i].gpuBytes) +
			" hd_gpu_bytes=" + std::to_string(usage[i].highResolutionGpuBytes) + "\n";
	return text;
}
} // namespace RenderMemoryReport
