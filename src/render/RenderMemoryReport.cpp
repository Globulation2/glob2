// SPDX-License-Identifier: GPL-3.0-or-later
#include "RenderMemoryReport.h"
#include "GlobalContainer.h"
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
	// Textures of the portable SDL renderer, which no DrawableSurface accounts
	// for: on phones they share the process's memory.
	const auto backend = globalContainer && globalContainer->gfx ? globalContainer->gfx->portableTextureMemory()
		: std::pair<std::uint64_t, std::uint64_t>{};
	return GAGCore::RenderMemory::fields(GAGCore::RenderMemory::report()) +
		" cpu_high_resolution_bytes=" + std::to_string(GAGCore::Sprite::highResolutionStats().cpuBytes) +
		" cpu_team_color_bytes=" + std::to_string(GAGCore::Sprite::teamColorStats().cpuBytes) +
		" team_color_frames=" + std::to_string(GAGCore::Sprite::teamColorStats().frames) +
		" cpu_compositor_bytes=" + std::to_string(compositorBytes) +
		" cpu_terrain_page_bytes=" + std::to_string(pages ? pages->bytes() : 0) +
		" gpu_terrain_page_bytes=" + std::to_string(pages ? pages->residentTextureBytes() : 0) +
		" gpu_direct_terrain_bytes=" + std::to_string(direct ? direct->textureBytes() : 0) +
		" cpu_terrain_page_pixel_bytes=" + std::to_string(pages ? pages->pixelBytes() : 0) +
		" gpu_terrain_base_bytes=" + std::to_string(pages ? pages->baseTextureBytes() : 0) +
		" cpu_terrain_base_pixel_bytes=" + std::to_string(pages ? pages->basePixelBytes() : 0) +
		" gpu_backend_texture_bytes=" + std::to_string(backend.first) +
		" gpu_backend_peak_texture_bytes=" + std::to_string(backend.second);
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
