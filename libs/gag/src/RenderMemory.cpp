// SPDX-License-Identifier: GPL-3.0-or-later
#include "GraphicContextPrivate.h"
#include <RenderMemory.h>
#include <AssetLoader.h>
#include <RenderBatch.h>
#include <SDLGraphicContext.h>
#include <algorithm>
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <sys/resource.h>
#endif

namespace GAGCore::RenderMemory
{
namespace
{
std::array<std::size_t, unsigned(Auxiliary::Count)> auxiliaryBytes{};
constexpr const char *auxiliaryNames[unsigned(Auxiliary::Count)]{
	"gpu_frame_cache_bytes", "gpu_skin_atlas_bytes", "gpu_view_tile_bytes"};

std::size_t peakResident()
{
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
	return 0; // Not measured here; use the platform's own tools.
#else
	rusage usage{};
	if (getrusage(RUSAGE_SELF, &usage)) return 0;
#ifdef __APPLE__
	return std::size_t(usage.ru_maxrss);
#else
	return std::size_t(usage.ru_maxrss) * 1024;
#endif
#endif
}
} // namespace

void setAuxiliary(Auxiliary kind, std::size_t bytes) { auxiliaryBytes[unsigned(kind)] = bytes; }

std::size_t trackedGpuBytes()
{
	auto bytes = DrawableSurface::allocatedTextureBytes();
	for (const auto auxiliary : auxiliaryBytes) bytes += auxiliary;
	return bytes;
}

Report report()
{
	Report result;
	const auto pixels = DrawableSurface::residentPixels();
	result.cpuSurfaces = pixels.surfaces;
	result.cpuSurfaceBytes = pixels.surfaceBytes;
	result.cpuPreparedBytes = pixels.preparedBytes;
#ifdef HAVE_OPENGL
	if (_gc)
		if (const auto *batch = _gc->getRenderBatch())
			result.gpuArrayBytes = batch->textureBytes();
#endif
	const auto textures = DrawableSurface::allocatedTextureBytes();
	result.gpuSurfaceBytes = textures - std::min(textures, result.gpuArrayBytes);
	result.gpuAuxiliaryBytes = auxiliaryBytes;
	const auto decodes = AssetLoader::decodeTotals();
	result.imageDecodes = decodes.images;
	result.imageDecodedBytes = decodes.bytes;
	result.peakResidentBytes = peakResident();
	return result;
}

std::string fields(const Report &report)
{
	std::string text = "cpu_surfaces=" + std::to_string(report.cpuSurfaces) +
		" cpu_surface_bytes=" + std::to_string(report.cpuSurfaceBytes) +
		" cpu_prepared_bytes=" + std::to_string(report.cpuPreparedBytes) +
		" gpu_surface_bytes=" + std::to_string(report.gpuSurfaceBytes) +
		" gpu_array_bytes=" + std::to_string(report.gpuArrayBytes);
	for (unsigned i = 0; i < unsigned(Auxiliary::Count); ++i)
		text += std::string(" ") + auxiliaryNames[i] + "=" + std::to_string(report.gpuAuxiliaryBytes[i]);
	text += " image_decodes=" + std::to_string(report.imageDecodes) +
		" image_decoded_bytes=" + std::to_string(report.imageDecodedBytes) +
		" peak_rss_bytes=" + std::to_string(report.peakResidentBytes);
	return text;
}

std::vector<SpriteUsage> sprites()
{
	std::vector<SpriteUsage> usage;
	Sprite::memoryUsage(usage);
	std::sort(usage.begin(), usage.end(), [](const SpriteUsage &a, const SpriteUsage &b)
	{
		const auto total = [](const SpriteUsage &u) { return u.nativeCpuBytes + u.highResolutionCpuBytes + u.gpuBytes; };
		return total(a) != total(b) ? total(a) > total(b) : a.name < b.name;
	});
	return usage;
}
} // namespace GAGCore::RenderMemory
