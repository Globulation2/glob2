// SPDX-License-Identifier: GPL-3.0-or-later
#include <RenderBackend.h>
#include <RenderFrame.h>
#include <SurfaceRaster.h>
#include <PresentationWork.h>
#include <PerformanceTelemetry.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace GAGCore
{
// Private factory: arbitrary geometry retains SDL's rasterizer. Sprites and
// rectangles do not create textures or enter its command queue.
std::unique_ptr<RenderBackend> makeSDLSoftwareGeometryBackend(SDL_Surface *surface);
void setSDLSoftwareTargetScissor(RenderBackend &, const SDL_Rect *);
namespace
{
class TargetClip
{
	SDL_Surface *surface;
	SDL_Rect previous;

  public:
	TargetClip(SDL_Surface *value, const SDL_Rect &rect)
		: surface(value)
	{
		SDL_GetSurfaceClipRect(surface, &previous);
		SDL_SetSurfaceClipRect(surface, &rect);
	}
	~TargetClip() { SDL_SetSurfaceClipRect(surface, &previous); }
};
class SoftwareRenderBackend final : public RenderBackend
{
	SDL_Surface *target; // Borrowed; the context/presenter outlives this backend.
	float scale = 1, offsetX = 0, offsetY = 0;
	int logicalW = 0, logicalH = 0;
	float nativeX = 1, nativeY = 1;
	std::optional<SDL_Rect> bounds, localClip, physicalClip;
	std::unique_ptr<RenderBackend> geometry;
	RenderOperations counts;
    // Development execution controls are fixed for this backend lifetime.
    const RenderExecution execution = renderExecution();
    static constexpr std::size_t MaximumCommands = 1024, MaximumSourceBytes = 4u * 1024u * 1024u;
    static constexpr std::size_t MaximumBins = 4096, MaximumReferences = 65536;
    RenderWorkReservation metadataReservation, sourceReservation;
    bool metadataAdmitted = false;
    bool admitMetadata()
    {
        // Covers command/source control storage, maximum bins/references,
        // two tile scratch slots and the owner's wide-column scratch.
        if (!metadataAdmitted) metadataAdmitted = metadataReservation.reserve(2u*1024u*1024u);
        return metadataAdmitted;
    }
    std::vector<SurfaceRaster::RasterCommand> commands;
    std::vector<std::vector<Uint32>> snapshots;
    std::size_t sourceBytes = 0;
    bool failedRaster = false;

    bool recordFill(SDL_Rect dst, SDL_Color color)
    {
        if (execution == RenderExecution::Auto || !admitMetadata())
            return false;
        if (commands.size() == MaximumCommands)
            flush();
        SurfaceRaster::RasterCommand command;
        command.destination = dst;
        command.clip = outputClip();
        command.opacity = color.a;
        command.color = SDL_MapSurfaceRGBA(target, color.r, color.g, color.b, 255);
        commands.push_back(command);
        return true;
    }
    bool recordBlit(SDL_Surface *source, SDL_Rect src, SDL_Rect dst, Uint8 alpha, bool opaque)
    {
        if (execution == RenderExecution::Auto || source->format != target->format ||
            source->format != SDL_PIXELFORMAT_ARGB8888 || !admitMetadata())
            return false;
        Uint32 key;
        if (SDL_GetSurfaceColorKey(source, &key))
            return false;
        // Snapshot every borrowed source at capture time. This also freezes
        // aliased surface views; allocation identity cannot affect sampling.
        SDL_Rect available{0, 0, source->w, source->h}, retained;
        if (!SDL_GetRectIntersection(&src, &available, &retained))
            return true;
        const std::size_t bytes = std::size_t(retained.w) * retained.h * 4;
        if (bytes > MaximumSourceBytes)
            return false;
        if (commands.size() == MaximumCommands || sourceBytes + bytes > MaximumSourceBytes)
            flush();
        // A read from our target must observe earlier recorded and SDL draws.
        const auto first = reinterpret_cast<std::uintptr_t>(source->pixels);
        const auto last = first + std::size_t(source->pitch) * source->h;
        const auto targetFirst = reinterpret_cast<std::uintptr_t>(target->pixels);
        const auto targetLast = targetFirst + std::size_t(target->pitch) * target->h;
        if (first < targetLast && targetFirst < last)
            flush();
        // Failed allocation/locking must release only this uncommitted capture.
        RenderWorkReservation captureReservation;
        if (!captureReservation.reserve(bytes)) {
            flush();
            if (!captureReservation.reserve(bytes)) return false;
        }
        PERF_SCOPE_TIME(RasterSnapshot);
        std::vector<Uint32> pixels(std::size_t(retained.w) * retained.h);
        if (!SDL_LockSurface(source))
            throw std::runtime_error(SDL_GetError());
        for (int y = 0; y < retained.h; ++y)
            std::memcpy(pixels.data() + y * retained.w,
                        static_cast<const Uint8 *>(source->pixels) +
                            (retained.y + y) * source->pitch + retained.x * 4,
                        retained.w * 4);
        SDL_UnlockSurface(source);
        SurfaceRaster::RasterCommand command;
        command.kind = SurfaceRaster::RasterCommand::Kind::Blit;
        command.source = {reinterpret_cast<const Uint8 *>(pixels.data()), retained.w, retained.h,
                          retained.w * 4, *SDL_GetPixelFormatDetails(source->format)};
        command.sourceRect = {src.x - retained.x, src.y - retained.y, src.w, src.h};
        command.destination = dst;
        command.clip = outputClip();
        command.opacity = alpha;
        command.opaque = opaque;
        counts.rasterSnapshotBytes += bytes;
        // Reserve both slots before changing recorder ownership.
        if (commands.size() == commands.capacity())
            commands.reserve(std::min(MaximumCommands, std::max(std::size_t(32), commands.capacity()*2)));
        if (snapshots.size() == snapshots.capacity())
            snapshots.reserve(std::min(MaximumCommands, std::max(std::size_t(32), snapshots.capacity()*2)));
        snapshots.push_back(std::move(pixels));
        commands.push_back(command);
        sourceReservation.absorb(captureReservation);
        sourceBytes += bytes;
        return true;
    }
    void drainGeometry()
    {
        if (geometry) {
            PERF_SCOPE_TIME(RasterFallback);
            geometry->flush();
        }
    }
    void executeSegment()
    {
        if (failedRaster)
            throw std::runtime_error("Software drawing frame was abandoned");
        if (commands.empty())
            return;
        ++counts.rasterBatches;
        drainGeometry();
        const SDL_Rect full{0, 0, target->w, target->h};
        struct Tile
        {
            SDL_Rect region;
            std::size_t begin = 0, end = 0;
        };
        std::vector<Tile> tiles;
        std::vector<std::size_t> references;
        bool tiled = execution == RenderExecution::Parallel && PresentationWork::current;
        {
            PERF_SCOPE_TIME(RasterBinning);
            constexpr int edge = 128;
            const std::size_t columns = (std::size_t(target->w) + edge - 1) / edge;
            const std::size_t rows = (std::size_t(target->h) + edge - 1) / edge;
            if (columns * rows > MaximumBins)
                tiled = false;
            if (tiled)
            {
                tiles.resize(columns * rows);
                for (std::size_t i = 0; i < tiles.size(); ++i)
                    tiles[i].region = {int(i % columns) * edge, int(i / columns) * edge,
                                       std::min(edge, target->w - int(i % columns) * edge),
                                       std::min(edge, target->h - int(i / columns) * edge)};
                const auto visit = [&](const auto &command, const auto &append)
                {
                    SDL_Rect bounds, visible;
                    if (!SDL_GetRectIntersection(&command.destination, &command.clip, &bounds) ||
                        !SDL_GetRectIntersection(&bounds, &full, &visible))
                        return;
                    for (int y = visible.y / edge; y <= (visible.y + visible.h - 1) / edge; ++y)
                        for (int x = visible.x / edge; x <= (visible.x + visible.w - 1) / edge; ++x)
                            append(std::size_t(y) * columns + x);
                };
                for (const auto &command : commands)
                    visit(command, [&](std::size_t tile) { ++tiles[tile].end; });
                std::size_t total = 0;
                for (auto &tile : tiles)
                {
                    const auto count = tile.end;
                    // Bound pixel work, not just references: even a tile with
                    // many tiny overlapping commands must yield to simulation.
                    if (count > 16)
                        tiled = false;
                    tile.begin = tile.end = total;
                    total += count;
                }
                if (total > MaximumReferences)
                    tiled = false;
                if (tiled)
                {
                    references.resize(total);
                    for (std::size_t i = 0; i < commands.size(); ++i)
                        visit(commands[i],
                              [&](std::size_t tile) { references[tiles[tile].end++] = i; });
                    std::erase_if(tiles, [](const auto &tile) { return tile.begin == tile.end; });
                }
            }
        }
        counts.rasterTiles += tiled ? tiles.size() : 0;
        counts.rasterReferences += tiled ? references.size() : 0;
        counts.rasterSerialBatches += !tiled;
        counts.rasterPeakRetainedBytes = std::max(
            counts.rasterPeakRetainedBytes,
            std::uint64_t(sourceBytes + commands.capacity() * sizeof(SurfaceRaster::RasterCommand) +
                          snapshots.capacity() * sizeof(std::vector<Uint32>) +
                          tiles.capacity() * sizeof(Tile) +
                          references.capacity() * sizeof(std::size_t) +
                          2*128*sizeof(std::int64_t)));
        // Lock exactly once on the owner, before exposing stable target pixels.
        if (!SDL_LockSurface(target))
            throw std::runtime_error(SDL_GetError());
        struct Unlock
        {
            SDL_Surface *surface;
            ~Unlock() { SDL_UnlockSurface(surface); }
        } unlock{target};
        SurfaceRaster::WritablePixelView output{static_cast<Uint8 *>(target->pixels), target->w,
                                                target->h, target->pitch,
                                                *SDL_GetPixelFormatDetails(target->format)};
        try
        {
            PERF_SCOPE_TIME(RasterBatch);
            if (tiled)
                executePresentationJobs(tiles.size(),
                                        [&](std::size_t index)
                                        {
                                            const auto &tile = tiles[index];
                                            for (std::size_t i = tile.begin; i < tile.end; ++i)
                                                SurfaceRaster::executeCommand(
                                                    commands[references[i]], output, tile.region);
                                        });
            else
            {
                // Captured inputs are immutable, so each command can traverse
                // narrow regions while retaining painter order. This bounds
                // sampling scratch even on unusually wide offscreen targets.
                for (const auto &command : commands)
                    for (int x = 0; x < full.w;)
                    {
                        const int width = std::min(128, full.w - x);
                        SurfaceRaster::executeCommand(command, output, {x, 0, width, full.h});
                        x += width;
                    }
            }
        }
        catch (...)
        {
            // A partial working target must never reach capture or presentation.
            failedRaster = true;
            commands.clear();
            snapshots.clear();
            sourceBytes = 0;
            sourceReservation.release();
            throw;
        }
        commands.clear();
        snapshots.clear();
        sourceBytes = 0;
        sourceReservation.release();
    }

	SDL_Rect nativeClip(SDL_Rect rect) const
	{
		const int x = int(std::floor(rect.x * nativeX));
		const int y = int(std::floor(rect.y * nativeY));
		return {x, y, int(std::ceil((rect.x + rect.w) * nativeX)) - x,
			int(std::ceil((rect.y + rect.h) * nativeY)) - y};
	}
	SDL_Rect outputClip() const
	{
		SDL_Rect result{0, 0, target->w, target->h};
		if (bounds)
		{
			const auto mapped = nativeClip(*bounds);
			SDL_GetRectIntersection(&result, &mapped, &result);
		}
		if (localClip)
		{
			const int x = int(std::floor(localClip->x * scale + offsetX));
			const int y = int(std::floor(localClip->y * scale + offsetY));
			const SDL_Rect transformed{
				x, y, int(std::ceil((localClip->x + localClip->w) * scale + offsetX)) - x,
				int(std::ceil((localClip->y + localClip->h) * scale + offsetY)) - y};
			const auto mapped = nativeClip(transformed);
			SDL_GetRectIntersection(&result, &mapped, &result);
		}
		return detail::intersectTargetScissor(result, physicalClip);
	}
    void syncGeometryClip() {
        if (!geometry) return;
        const auto rect = outputClip();
        setSDLSoftwareTargetScissor(*geometry, physicalClip ? &rect : nullptr);
        if (!physicalClip) geometry->clip(localClip ? &*localClip : nullptr);
    }
	SDL_Rect pixels(const SDL_FRect &rect) const
	{
		// Round shared endpoints, never origin and width independently. Adjacent
		// tiles share exactly one edge even at 33% zoom and negative offsets.
		const auto edge = [](double value) { return int(std::floor(value + 0.5)); };
		const int x = edge((rect.x * scale + offsetX) * nativeX),
			y = edge((rect.y * scale + offsetY) * nativeY);
		return {x, y, edge(((rect.x + rect.w) * scale + offsetX) * nativeX) - x,
				edge(((rect.y + rect.h) * scale + offsetY) * nativeY) - y};
	}
	RenderBackend &fallback()
	{
        executeSegment();
        ++counts.rasterFallbacks;
		if (!geometry)
		{
			geometry = makeSDLSoftwareGeometryBackend(target);
			if (logicalW)
				geometry->nativeLogicalSize(logicalW, logicalH);
			geometry->transform(scale, offsetX, offsetY, bounds ? &*bounds : nullptr);
			geometry->clip(localClip ? &*localClip : nullptr);
		}
        syncGeometryClip();
		return *geometry;
	}

  public:
	explicit SoftwareRenderBackend(SDL_Surface *value) : target(value) {}
    void targetScissor(const SDL_Rect *rect) {
        flush();
        physicalClip = rect ? std::optional<SDL_Rect>(*rect) : std::nullopt;
        detail::intersectTargetScissor({0,0,target->w,target->h}, physicalClip);
        syncGeometryClip();
    }
	void bindTarget(SDL_Surface *value) override
	{
		if (target == value)
			return;
		flush();
		geometry.reset(); // SDL's software renderer borrows its creation surface.
		target = value;
		if (logicalW)
		{
			nativeX = float(target->w) / logicalW;
			nativeY = float(target->h) / logicalH;
		}
	}
	void transform(float value, float x, float y, const SDL_Rect *clipBounds) override
	{
		if (!std::isfinite(value) || value <= 0 || !std::isfinite(x) || !std::isfinite(y))
			throw std::invalid_argument("Invalid software transform");
		scale = value;
		offsetX = x;
		offsetY = y;
		bounds = clipBounds ? std::optional<SDL_Rect>(*clipBounds) : std::nullopt;
		localClip.reset();
		if (geometry)
			geometry->transform(value, x, y, clipBounds);
        syncGeometryClip();
	}
	void clip(const SDL_Rect *rect) override
	{
		localClip = rect ? std::optional<SDL_Rect>(*rect) : std::nullopt;
		if (geometry)
			geometry->clip(rect);
        syncGeometryClip();
	}
	void blit(const void *key, SDL_Surface *source, std::uint64_t revision, bool opaque,
			  const SDL_Rect &sourceRect, const SDL_FRect &destination, Uint8 alpha) override
	{
		++counts.blits;
		// SDL's alpha blitters have platform-specific rounding, and format
		// conversion can bake source modulation into the pixels. Retain the
		// geometry reference for these cases; verified same-format opaque
		// images still use the direct row-copy and nearest-sampling paths.
		if (!opaque || alpha != 255 || source->format != target->format)
		{
			fallback().blit(key, source, revision, opaque, sourceRect, destination, alpha);
			return;
		}
		// SDL's large textured triangles have observable fixed-point overflow
		// behavior (notably the 512-pixel water asset). Preserve that behavior
		// for existing images; changing it needs separate visual acceptance.
		// PREALLOC surfaces are borrowed terrain-run views: their pixels replace
		// many small tile blits and must not acquire large-triangle artifacts.
		const SDL_Rect mapped = pixels(destination);
		if (sourceRect.w >= 512 && sourceRect.h >= 512 &&
			(mapped.w > 512 || mapped.h > 512) && !(source->flags & SDL_SURFACE_PREALLOCATED))
		{
			fallback().blit(key, source, revision, opaque, sourceRect, destination, alpha);
			return;
		}
        if (execution != RenderExecution::Auto)
        {
            PERF_SCOPE_TIME(RasterCapture);
            if (recordBlit(source, sourceRect, mapped, alpha, opaque))
                return;
        }
		// Direct writes must follow earlier queued triangles, not overtake them.
		flush();
		TargetClip clip(target, outputClip());
		SurfaceRaster::blit(target, source, sourceRect, mapped, alpha, opaque,
							SurfaceRaster::BlitBlend::Triangle);
	}
    bool fillPixels(const SDL_Rect &rect, SDL_Color color) override
    {
        ++counts.fills;
        if (execution != RenderExecution::Auto)
        {
            PERF_SCOPE_TIME(RasterCapture);
            if (recordFill(rect, color))
                return true;
        }
        flush();
        TargetClip clip(target, outputClip());
        SurfaceRaster::fill(target, rect, SDL_MapSurfaceRGBA(target, color.r, color.g, color.b, 255),
            color.a, SurfaceRaster::FillBlend::SourceOver);
        return true;
    }
    bool blitPixels(SDL_Surface *source, const SDL_Rect &src, const SDL_Rect &dst, Uint8 alpha) override
    {
        ++counts.blits;
        if (execution != RenderExecution::Auto)
        {
            PERF_SCOPE_TIME(RasterCapture);
            if (recordBlit(source, src, dst, alpha, false))
                return true;
        }
        flush();
        TargetClip clip(target, outputClip());
        SurfaceRaster::blit(target, source, src, dst, alpha, false, SurfaceRaster::BlitBlend::Triangle);
        return true;
    }
    void blitLinear(const void *,SDL_Surface *source,std::uint64_t,const SDL_Rect &src,const SDL_FRect &dst,Uint8 alpha) override {
        ++counts.blits;flush();TargetClip clip(target,outputClip());
        const SDL_FRect mapped{float((dst.x*scale+offsetX)*nativeX),float((dst.y*scale+offsetY)*nativeY),float(dst.w*scale*nativeX),float(dst.h*scale*nativeY)};
        SurfaceRaster::skinBlitFloat(target,source,src,mapped,alpha);
    }
	void fill(const SDL_FRect &rect, SDL_Color color) override
	{
		++counts.fills;
		if (color.a == 0) return;
		// Match SDL's separate-term/SIMD rounding on each supported platform.
		if (color.a != 255)
		{
			fallback().fill(rect, color);
			return;
		}
        if (execution != RenderExecution::Auto)
        {
            PERF_SCOPE_TIME(RasterCapture);
            if (recordFill(pixels(rect), color))
			return;
		}
		flush();
		TargetClip clip(target, outputClip());
		SurfaceRaster::fill(target, pixels(rect),
							SDL_MapSurfaceRGBA(target, color.r, color.g, color.b, 255), color.a,
							SurfaceRaster::FillBlend::SourceOver);
	}
	void triangles(std::span<const SDL_Vertex> vertices, const void *key, SDL_Surface *source,
				   std::uint64_t revision) override
	{
		++counts.triangles;
		fallback().triangles(vertices, key, source, revision);
	}
	void screenTriangles(std::span<const SDL_Vertex> vertices) override
	{
		++counts.triangles;
		fallback().screenTriangles(vertices);
	}
	RenderOperations operations() const override { return counts; }
	void forget(const void *key) override
	{
        flush();
		if (geometry)
			geometry->forget(key);
	}
	void reset() override
	{
		flush();
		geometry.reset();
	}
	void flush() override
	{
        executeSegment();
        drainGeometry();
	}
    void discardFrame() noexcept override
    {
        commands.clear();
        snapshots.clear();
        sourceBytes = 0;
        sourceReservation.release();
        geometry.reset();
        failedRaster = false;
	}
	void present() override { flush(); }
	void logicalSize(int, int) override { reset(); }
	void nativeLogicalSize(int width, int height) override
	{
		if (width <= 0 || height <= 0)
			throw std::invalid_argument("Invalid software logical size");
		flush();
		logicalW = width;
		logicalH = height;
		// Native display scaling is independent of the map/UI transform.
		nativeX = float(target->w) / width;
		nativeY = float(target->h) / height;
		if (!SDL_FillSurfaceRect(target, nullptr, SDL_MapSurfaceRGBA(target, 0, 0, 0, 255)))
			throw std::runtime_error(SDL_GetError());
		if (geometry)
			geometry->nativeLogicalSize(width, height);
	}
	void outputSize(int &w, int &h) override
	{
		w = target->w;
		h = target->h;
	}
	SDL_Surface *capture() override
	{
		flush();
		auto *copy = SDL_ConvertSurface(target, SDL_PIXELFORMAT_RGBA32);
		if (!copy)
			throw std::runtime_error(SDL_GetError());
		return copy;
	}
};
} // namespace
void setSoftwareTargetScissor(RenderBackend &backend, const SDL_Rect *rect)
{
    auto *value = dynamic_cast<SoftwareRenderBackend *>(&backend);
    if (!value) throw std::logic_error("Software backend missing");
    value->targetScissor(rect);
}
std::unique_ptr<RenderBackend> makeSoftwareRenderBackend(SDL_Surface *surface)
{
	if (!surface || SDL_GetPixelFormatDetails(surface->format)->bytes_per_pixel != 4)
		throw std::invalid_argument("Software rendering requires a 32-bit target");
	return std::make_unique<SoftwareRenderBackend>(surface);
}
} // namespace GAGCore
