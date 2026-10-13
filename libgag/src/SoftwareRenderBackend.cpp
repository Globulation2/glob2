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
#include <limits>
#include <array>
#include <exception>
#include "SoftwareRasterSources.h"

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
    static constexpr std::size_t MaximumCommands = 1024;
    static constexpr std::size_t MaximumBins = 4096, MaximumReferences = 65536;
    RenderWorkReservation metadataReservation;
    bool metadataAdmitted = false;
    bool admitMetadata()
    {
        // Covers command/source-cache control storage, maximum bins/references,
        // two bounded tile/serial sampling scratch slots.
        if (!metadataAdmitted) metadataAdmitted = metadataReservation.reserve(2u*1024u*1024u);
        return metadataAdmitted;
    }
    std::vector<SurfaceRaster::RasterCommand> commands;
    detail::RasterSourceCache sourceCache;
    std::vector<detail::RasterSourceCache::Lease> snapshots;
    bool failedRaster = false;
    struct Tile
    {
        SDL_Rect region;
        std::size_t begin = 0, end = 0;
        std::uint64_t work = 0;
    };
    // Owner-only workspaces retain bounded capacity between synchronous batches.
    std::vector<Tile> tiles;
    std::vector<std::size_t> references;
    std::vector<Tile> baseTiles;
    std::vector<std::size_t> baseReferences;
    std::vector<SDL_Rect> visibleCommands;
    struct RasterJob { std::size_t begin = 0, end = 0; };
    std::vector<RasterJob> jobs;

    SDL_Rect visibleBounds(SDL_Rect destination, SDL_Rect clip) const
    {
        SDL_Rect bounds{}, visible{};
        const SDL_Rect full{0, 0, target->w, target->h};
        if (SDL_GetRectIntersection(&destination, &clip, &bounds))
            SDL_GetRectIntersection(&bounds, &full, &visible);
        return visible;
    }
    void clearSegment() noexcept
    {
        // Commands borrow snapshot pixels; clear them before releasing inputs.
        commands.clear();
        snapshots.clear();

        tiles.clear();
        references.clear();
        baseTiles.clear();
        baseReferences.clear();
        visibleCommands.clear();
        jobs.clear();
    }

    // Any failed owner operation abandons the frame, including capture admission
    // or binning after an earlier ordered prefix has already modified pixels.
    // Synchronous dispatch drains callbacks before unwinding into this guard.
    class FrameFailureGuard
    {
        SoftwareRenderBackend &backend;
        const int exceptions = std::uncaught_exceptions();
    public:
        explicit FrameFailureGuard(SoftwareRenderBackend &value) : backend(value)
        {
            if (backend.failedRaster)
                throw std::runtime_error("Software drawing frame was abandoned");
        }
        ~FrameFailureGuard() noexcept
        {
            if (std::uncaught_exceptions() > exceptions)
            {
                backend.failedRaster = true;
                backend.clearSegment();
            }
        }
        FrameFailureGuard(const FrameFailureGuard &) = delete;
        FrameFailureGuard &operator=(const FrameFailureGuard &) = delete;
    };

    bool portableTarget() const
    {
        const auto *format = SDL_GetPixelFormatDetails(target->format);
        return format->bytes_per_pixel == 4 && format->Rbits == 8 &&
            format->Gbits == 8 && format->Bbits == 8 &&
            (format->Abits == 0 || format->Abits == 8);
    }
    void executeOwner(const SurfaceRaster::RasterCommand &command)
    {
        flush();
        if (!SDL_LockSurface(target)) throw std::runtime_error(SDL_GetError());
        struct Unlock { SDL_Surface *surface; ~Unlock() { SDL_UnlockSurface(surface); } } unlock{target};
        SurfaceRaster::executeCommand(command,
            {static_cast<Uint8 *>(target->pixels), target->w, target->h, target->pitch,
             *SDL_GetPixelFormatDetails(target->format)}, command.clip);
    }
    bool submitFill(SDL_Rect dst, SDL_Color color)
    {
        if (!portableTarget()) return false;
        const auto clip = outputClip();
        if (color.a == 0 || SDL_RectEmpty(&dst) || SDL_RectEmpty(&clip)) return true;
        const auto visible = visibleBounds(dst, clip);
        if (SDL_RectEmpty(&visible)) return true;
        SurfaceRaster::RasterCommand command;
        command.destination = dst;
        command.clip = clip;
        command.opacity = color.a;
        command.color = SDL_MapSurfaceRGBA(target, color.r, color.g, color.b, 255);
        command.fillBlend = SurfaceRaster::FillBlend::PortableSourceOver;
        if (execution == RenderExecution::Immediate || !admitMetadata())
            executeOwner(command);
        else
        {
            if (commands.size() == MaximumCommands) flush();
            commands.push_back(command);
        }
        return true;
    }
    bool submitBlit(SDL_Surface *source, SDL_Rect src, SDL_Rect dst, Uint8 alpha, bool opaque,
                    const void *identity = nullptr, std::uint64_t revision = 0,
                    const SDL_FRect *linearDestination = nullptr, bool sdlNearest = false,
                    SDL_Color tint = {255,255,255,255})
    {
        if (!portableTarget() || SDL_ISPIXELFORMAT_INDEXED(source->format) ||
            SDL_ISPIXELFORMAT_FOURCC(source->format)) return false;
        // SDL texture creation converts non-sRGB inputs. Raw snapshots preserve
        // bytes, so retain that color conversion on the geometry path.
        if (sdlNearest && SDL_GetSurfaceColorspace(source) != SDL_COLORSPACE_SRGB) return false;
        Uint32 colorKey;
        if (SDL_GetSurfaceColorKey(source, &colorKey)) return false;
        const auto clip = outputClip();
        const auto visible = visibleBounds(dst, clip);
        if (!alpha || SDL_RectEmpty(&visible)) return true;
        SDL_Rect retained{};
        const SDL_Rect available{0, 0, source->w, source->h};
        if (!SDL_GetRectIntersection(&src, &available, &retained)) return true;
        // Linear filtering samples a complete valid rectangle, as the owner
        // skin path did; partial sources must not stretch their surviving edge.
        if (linearDestination && (retained.x != src.x || retained.y != src.y ||
            retained.w != src.w || retained.h != src.h)) return true;
        const auto *format = SDL_GetPixelFormatDetails(source->format);
        const auto range = [](SDL_Surface *surface) {
            const auto first = reinterpret_cast<std::uintptr_t>(surface->pixels);
            const auto row = reinterpret_cast<std::uintptr_t>(
                static_cast<const Uint8 *>(surface->pixels) +
                std::ptrdiff_t(surface->h - 1) * surface->pitch);
            return std::pair{std::min(first, row), std::max(first, row) +
                std::size_t(surface->w) * SDL_GetPixelFormatDetails(surface->format)->bytes_per_pixel};
        };
        const auto [first, last] = range(source);
        const auto [targetFirst, targetLast] = range(target);
        const bool aliased = first < targetLast && targetFirst < last;
        if (aliased) { flush(); sourceCache.erase(identity); }
        SurfaceRaster::RasterCommand command;
        command.kind = SurfaceRaster::RasterCommand::Kind::Blit;
        command.sourceRect = src;
        command.destination = dst;
        command.clip = clip;
        command.opacity = alpha;
        command.opaque = opaque;
        command.red = tint.r; command.green = tint.g; command.blue = tint.b;
        command.blend = SurfaceRaster::RasterCommand::Blending::Portable;
        if (sdlNearest) command.sampling = SurfaceRaster::RasterCommand::Sampling::SDLNearest;
        // SDL's native-size RLE blitter forces destination alpha, but RGB tint
        // disables RLE. Preserve that distinction independently of RGB rounding.
        command.forceOpaqueAlpha = !linearDestination && alpha == 255 &&
            tint.r == 255 && tint.g == 255 && tint.b == 255 &&
            src.w == dst.w && src.h == dst.h &&
            (SDL_GetPixelFormatDetails(target->format)->Amask == 0xff000000 &&
             (sdlNearest || source->format == target->format));
        if (linearDestination)
        {
            command.sampling = SurfaceRaster::RasterCommand::Sampling::LinearPremultiplied;
            command.destinationFloat = *linearDestination;
        }
        const bool direct = !aliased && format->bytes_per_pixel == 4 &&
            format->Rbits == 8 && format->Gbits == 8 && format->Bbits == 8 &&
            (format->Abits == 0 || format->Abits == 8);
        const bool owner = execution == RenderExecution::Immediate || !admitMetadata();
        if (owner && direct)
        {
            flush();
            if (!SDL_LockSurface(source)) throw std::runtime_error(SDL_GetError());
            struct Unlock { SDL_Surface *surface; ~Unlock() { SDL_UnlockSurface(surface); } } unlock{source};
            command.source = {static_cast<const Uint8 *>(source->pixels), source->w, source->h,
                              source->pitch, *format};
            executeOwner(command);
            return true;
        }
        if (commands.size() == MaximumCommands) flush();
        const bool reusable = identity && !aliased && source->pitch > 0 &&
            std::size_t(retained.w) * retained.h * 4 <= detail::RasterSourceCache::RetainedBytes;
        const auto cacheIdentity = reusable ? identity : nullptr;
        auto captured = sourceCache.capture(cacheIdentity, revision, *source, retained);
        if (!captured)
        {
            flush(); // Release queued leases before retrying admission.
            captured = sourceCache.capture(cacheIdentity, revision, *source, retained);
            if (!captured)
            {
                // A large packed source needs no snapshot for synchronous owner
                // execution. Use the same kernel, rather than changing rounding.
                if (!direct) return false;
                if (!SDL_LockSurface(source)) throw std::runtime_error(SDL_GetError());
                struct Unlock { SDL_Surface *surface; ~Unlock() { SDL_UnlockSurface(surface); } } unlock{source};
                command.source = {static_cast<const Uint8 *>(source->pixels), source->w, source->h,
                                  source->pitch, *format};
                executeOwner(command);
                return true;
            }
        }
        command.source = {reinterpret_cast<const Uint8 *>(captured->pixels.get()),
                          captured->region.w, captured->region.h, captured->region.w * 4,
                          *SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_ARGB8888)};
        command.sourceRect.x -= captured->region.x;
        command.sourceRect.y -= captured->region.y;
        if (owner) executeOwner(command);
        else
        {
            if (commands.size() == commands.capacity())
                commands.reserve(std::min(MaximumCommands, std::max(std::size_t(32), commands.capacity()*2)));
            if (snapshots.size() == snapshots.capacity())
                snapshots.reserve(std::min(MaximumCommands, std::max(std::size_t(32), snapshots.capacity()*2)));
            snapshots.push_back(std::move(captured));
            commands.push_back(command);
        }
        return true;
    }
    void drainGeometry()
    {
        if (geometry) {
            PERF_SCOPE_TIME(RasterFallback);
            geometry->flush();
        }
    }
    void executeCommands(std::span<const SurfaceRaster::RasterCommand> segment)
    {
        tiles.clear();
        references.clear();
        jobs.clear();
        constexpr std::uint64_t maximumJobPixels = 16u * 128u * 128u;
        bool tiled = execution == RenderExecution::Parallel && PresentationWork::current;
        bool capacityExceeded = false;
        std::uint64_t jobPixelLimit = maximumJobPixels;
        {
            PERF_SCOPE_TIME(RasterBinning);
            if (tiled)
            {
                const std::size_t columns = (std::size_t(target->w)+127)/128;
                const std::size_t rows = (std::size_t(target->h)+127)/128;
                capacityExceeded = columns*rows > MaximumBins;
                visibleCommands.resize(segment.size());
                std::uint64_t totalWork = 0;
                for (std::size_t index = 0; index < segment.size(); ++index)
                {
                    visibleCommands[index] = visibleBounds(segment[index].destination, segment[index].clip);
                    const auto visible = visibleCommands[index];
                    const unsigned samples = segment[index].sampling ==
                        SurfaceRaster::RasterCommand::Sampling::LinearPremultiplied ? 4 : 1;
                    totalWork += std::uint64_t(visible.w)*visible.h*samples;
                }
                // A few jobs per participant expose parallelism even when the
                // whole segment fits one worst-case job; retain the hard ceiling.
                const auto desiredJobs = std::max(std::size_t(1), 2*renderConcurrency());
                jobPixelLimit = std::max(std::uint64_t(1), std::min(maximumJobPixels,
                    (totalWork+desiredJobs-1)/desiredJobs));
                if (!capacityExceeded)
                {
                    baseTiles.assign(columns*rows, {});
                    for (std::size_t index = 0; index < baseTiles.size(); ++index)
                    {
                        const int x = int(index%columns)*128, y = int(index/columns)*128;
                        baseTiles[index].region = {x,y,std::min(128,target->w-x),std::min(128,target->h-y)};
                    }
                    const auto visit = [&](SDL_Rect visible, const auto &append) {
                        if (SDL_RectEmpty(&visible)) return;
                        for (int y = visible.y/128; y <= (visible.y+visible.h-1)/128; ++y)
                            for (int x = visible.x/128; x <= (visible.x+visible.w-1)/128; ++x)
                                append(std::size_t(y)*columns+x);
                    };
                    for (const auto visible : visibleCommands)
                        visit(visible, [&](std::size_t index) { ++baseTiles[index].end; });
                    std::size_t total = 0;
                    for (auto &tile : baseTiles)
                    {
                        const auto count = tile.end;
                        tile.begin = tile.end = total;
                        total += count;
                    }
                    capacityExceeded = total > MaximumReferences;
                    if (!capacityExceeded)
                    {
                        baseReferences.resize(total);
                        for (std::size_t index = 0; index < segment.size(); ++index)
                            visit(visibleCommands[index], [&](std::size_t tile) {
                                baseReferences[baseTiles[tile].end++] = index;
                            });
                    }
                }
                // Initial bins resolve small draws once. Dense regions refine
                // only their own intersecting commands, keeping owner work near
                // the number of references rather than commands times screen tiles.
                const auto append = [&](auto &&self, SDL_Rect region,
                                        std::span<const std::size_t> candidates) -> bool
                {
                    std::uint64_t work = 0;
                    std::size_t count = 0;
                    for (const auto index : candidates)
                    {
                        SDL_Rect intersection{};
                        if (!SDL_GetRectIntersection(&visibleCommands[index], &region, &intersection)) continue;
                        ++count;
                        const unsigned samples = segment[index].sampling ==
                            SurfaceRaster::RasterCommand::Sampling::LinearPremultiplied ? 4 : 1;
                        work += std::uint64_t(intersection.w)*intersection.h*samples;
                    }
                    if (!count) return true;
                    if (work > jobPixelLimit && (region.w > 1 || region.h > 1))
                    {
                        SDL_Rect first = region, second = region;
                        if (region.w >= region.h)
                        {
                            first.w = region.w/2; second.x += first.w; second.w -= first.w;
                        }
                        else
                        {
                            first.h = region.h/2; second.y += first.h; second.h -= first.h;
                        }
                        return self(self,first,candidates) && self(self,second,candidates);
                    }
                    if (tiles.size() == MaximumBins || count > MaximumReferences-references.size())
                        return false;
                    const auto begin = references.size();
                    for (const auto index : candidates)
                        if (SDL_HasRectIntersection(&visibleCommands[index], &region)) references.push_back(index);
                    tiles.push_back({region,begin,references.size(),work});
                    return true;
                };
                if (!capacityExceeded)
                    for (const auto &tile : baseTiles)
                        if (!append(append,tile.region,std::span<const std::size_t>(baseReferences).subspan(
                                tile.begin,tile.end-tile.begin)))
                        { capacityExceeded = true; break; }
                if (!capacityExceeded)
                {
                    std::size_t first = 0, packedReferences = 0;
                    std::uint64_t packedWork = 0;
                    for (std::size_t index = 0; index < tiles.size(); ++index)
                    {
                        const auto &tile = tiles[index];
                        const auto count = tile.end - tile.begin;
                        if (index != first && (packedWork + tile.work > jobPixelLimit ||
                            packedReferences + count > 16))
                        {
                            jobs.push_back({first, index});
                            first = index; packedReferences = 0; packedWork = 0;
                        }
                        packedReferences += count; packedWork += tile.work;
                    }
                    if (first < tiles.size()) jobs.push_back({first, tiles.size()});
                }
            }
        }
        if (capacityExceeded)
        {
            // Keep metadata bounded by completing ordered prefixes. No new pool
            // or dependency graph is needed, and ordinary dense draws stay tiled.
            if (segment.size() > 1)
            {
                const auto middle = segment.size()/2;
                executeCommands(segment.first(middle));
                executeCommands(segment.subspan(middle));
                return;
            }
            tiled = false;
            tiles.clear(); references.clear(); jobs.clear();
        }
        ++counts.rasterBatches;
        counts.rasterTiles += tiled ? tiles.size() : 0;
        counts.rasterReferences += tiled ? references.size() : 0;
        counts.rasterJobs += tiled ? jobs.size() : 0;
        counts.rasterSerialBatches += !tiled;
        counts.rasterPeakRetainedBytes = std::max(
            counts.rasterPeakRetainedBytes,
            std::uint64_t(sourceCache.bytes() + commands.capacity() * sizeof(SurfaceRaster::RasterCommand) +
                          snapshots.capacity() * sizeof(decltype(snapshots)::value_type) +
                          tiles.capacity() * sizeof(Tile) +
                          references.capacity() * sizeof(std::size_t) +
                          baseTiles.capacity()*sizeof(Tile) +
                          baseReferences.capacity()*sizeof(std::size_t) +
                          visibleCommands.capacity()*sizeof(SDL_Rect) +
                          jobs.capacity() * sizeof(RasterJob) +
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
        {
            PERF_SCOPE_TIME(RasterBatch);
            if (tiled)
                executePresentationJobs(jobs.size(),
                                        [&](std::size_t index)
                                        {
                                            const auto &job = jobs[index];
                                            for (std::size_t tileIndex = job.begin; tileIndex < job.end; ++tileIndex)
                                            {
                                                const auto &tile = tiles[tileIndex];
                                                for (std::size_t i = tile.begin; i < tile.end; ++i)
                                                    SurfaceRaster::executeCommand(
                                                        segment[references[i]], output, tile.region);
                                            }
                                        });
            else
            {
                // Captured inputs are immutable, so each command can traverse
                // narrow regions while retaining painter order. This bounds
                // sampling scratch even on unusually wide offscreen targets.
                for (const auto &command : segment)
                {
                    const auto visible = visibleBounds(command.destination, command.clip);
                    const int right = visible.x + visible.w;
                    for (int x = visible.x; x < right;)
                    {
                        const int width = std::min(128, right - x);
                        SurfaceRaster::executeCommand(
                            command, output, {x, visible.y, width, visible.h});
                        x += width;
                    }
                }
            }
        }
    }
    void executeSegment()
    {
        FrameFailureGuard guard(*this);
        if (commands.empty()) return;
        drainGeometry();
        executeCommands(commands);
        clearSegment();
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
        const auto edge = [](double value) {
            value = std::floor(value + .5);
            if (!std::isfinite(value) || value < std::numeric_limits<int>::min() ||
                value > std::numeric_limits<int>::max())
                throw std::invalid_argument("Invalid software destination");
            return int(value);
        };
        const int x = edge((rect.x*scale+offsetX)*nativeX);
        const int y = edge((rect.y*scale+offsetY)*nativeY);
        const auto w = std::int64_t(edge(((rect.x+rect.w)*scale+offsetX)*nativeX)) - x;
        const auto h = std::int64_t(edge(((rect.y+rect.h)*scale+offsetY)*nativeY)) - y;
        if (w < std::numeric_limits<int>::min() || w > std::numeric_limits<int>::max() ||
            h < std::numeric_limits<int>::min() || h > std::numeric_limits<int>::max())
            throw std::invalid_argument("Oversized software destination");
        return {x,y,int(w),int(h)};
    }
    // SDL recognizes a uniformly colored quad as an integer rectangle/copy.
    // Preserve its endpoint subtraction and truncation before changing only
    // channel arithmetic; opaque direct draws retain shared rounded endpoints.
    SDL_Rect quadPixels(const SDL_FRect &rect) const
    {
        float left = rect.x*scale+offsetX, top = rect.y*scale+offsetY;
        float right = (rect.x+rect.w)*scale+offsetX, bottom = (rect.y+rect.h)*scale+offsetY;
        if (physicalClip)
        {
            left *= nativeX; right *= nativeX; top *= nativeY; bottom *= nativeY;
        }
        const auto truncate = [](float value) {
            const double wide = value;
            if (!std::isfinite(wide) || wide < std::numeric_limits<int>::min() ||
                wide > std::numeric_limits<int>::max())
                throw std::invalid_argument("Invalid software quad");
            return int(value);
        };
        return physicalClip ? SDL_Rect{truncate(left),truncate(top),truncate(right-left),truncate(bottom-top)} :
            SDL_Rect{truncate(left*nativeX),truncate(top*nativeY),
                     truncate((right-left)*nativeX),truncate((bottom-top)*nativeY)};
    }
    static SDL_Rect quadSource(SDL_Rect src, SDL_Surface *source)
    {
        const float u0 = float(src.x)/source->w, v0 = float(src.y)/source->h;
        const float u1 = float(std::int64_t(src.x)+src.w)/source->w;
        const float v1 = float(std::int64_t(src.y)+src.h)/source->h;
        const float x = u0*source->w, y = v0*source->h;
        const SDL_FRect requested{x,y,u1*source->w-x,v1*source->h-y};
        const SDL_FRect available{0,0,float(source->w),float(source->h)};
        SDL_FRect clipped{};
        if (!SDL_GetRectIntersectionFloat(&requested, &available, &clipped)) return {};
        return {int(clipped.x),int(clipped.y),int(clipped.w),int(clipped.h)};
    }
    struct QuadCorners
    {
        SDL_Vertex first, last, third, fourth;
    };
    std::optional<QuadCorners> triangleQuad(std::span<const SDL_Vertex,6> input,
                                            bool textured, bool applyTransform,
                                            std::array<int,3> &preceding, int firstIndex) const
    {
        std::array<SDL_Vertex,6> vertices;
        std::copy(input.begin(), input.end(), vertices.begin());
        for (auto &vertex : vertices)
        {
            if (applyTransform && (scale != 1 || offsetX != 0 || offsetY != 0))
            {
                vertex.position.x = vertex.position.x*scale+offsetX;
                vertex.position.y = vertex.position.y*scale+offsetY;
            }
            if (physicalClip)
            {
                vertex.position.x *= nativeX;
                vertex.position.y *= nativeY;
            }
        }
        // Mirror SDL's greedy duplicate remapping and pair recognition. Even a
        // recognized quad that remains on SDL must consume both triangles:
        // otherwise its second triangle could be paired with the following draw.
        const auto equal = [textured](const SDL_Vertex &a, const SDL_Vertex &b) {
            return a.position.x == b.position.x && a.position.y == b.position.y &&
                std::memcmp(&a.color,&b.color,sizeof(a.color)) == 0 &&
                (!textured || (a.tex_coord.x == b.tex_coord.x && a.tex_coord.y == b.tex_coord.y));
        };
        const auto previousKeys=preceding;
        std::array<int,3> current{firstIndex+3,firstIndex+4,firstIndex+5};
        for (int index=0;index<3;++index)
            for (int previous=0;previous<3;++previous)
                if (equal(vertices[previous],vertices[index+3])) { current[index]=previousKeys[previous]; break; }
        preceding=current;
        const auto vertex = [&](int key) -> const SDL_Vertex & {
            for (int previous=0;previous<3;++previous)
                if (previousKeys[previous]==key) return vertices[previous];
            return vertices[key-firstIndex];
        };
        int shared=0;
        for (int previous : previousKeys)
            if (std::find(current.begin(),current.end(),previous)!=current.end()) ++shared;
        if (shared!=2) return {};
        const auto &p0=vertex(current[0]).position;
        const auto &p1=vertex(current[1]).position;
        const auto &p2=vertex(current[2]).position;
        const int first = p0.x<=p1.x && p0.y<=p1.y ?
            (p0.x<=p2.x && p0.y<=p2.y ? current[0] : current[2]) :
            (p1.x<=p2.x && p1.y<=p2.y ? current[1] : current[2]);
        const int last = p0.x>=p1.x && p0.y>=p1.y ?
            (p0.x>=p2.x && p0.y>=p2.y ? current[0] : current[2]) :
            (p1.x>=p2.x && p1.y>=p2.y ? current[1] : current[2]);
        const int third=current[0]!=first && current[0]!=last ? current[0] :
            current[1]!=first && current[1]!=last ? current[1] : current[2];
        const int fourth=previousKeys[0]!=first && previousKeys[0]!=last ? previousKeys[0] :
            previousKeys[1]!=first && previousKeys[1]!=last ? previousKeys[1] : previousKeys[2];
        QuadCorners quad{vertex(first),vertex(last),vertex(third),vertex(fourth)};
        const auto corner = [&](const SDL_Vertex &value) {
            return ((quad.first.position.x==value.position.x && quad.last.position.y==value.position.y) ||
                    (quad.first.position.y==value.position.y && quad.last.position.x==value.position.x)) &&
                std::memcmp(&quad.first.color,&value.color,sizeof(value.color))==0;
        };
        if (!corner(quad.third) || !corner(quad.fourth) ||
            std::memcmp(&quad.first.color,&quad.last.color,sizeof(quad.last.color))!=0) return {};
        if (textured)
            for (const auto *vertex : {&quad.first,&quad.last,&quad.third,&quad.fourth})
                if (!(vertex->tex_coord.x>=0 && vertex->tex_coord.x<=1)) return {};
        return quad;
    }
    std::optional<SDL_Rect> triangleQuadPixels(const QuadCorners &quad) const
    {
        const float x=quad.first.position.x,y=quad.first.position.y;
        const float width=quad.last.position.x-x,height=quad.last.position.y-y;
        const std::array<float,4> values = physicalClip ? std::array{x,y,width,height} :
            std::array{x*nativeX,y*nativeY,width*nativeX,height*nativeY};
        for (float value : values)
            if (!std::isfinite(value) || double(value)<std::numeric_limits<int>::min() ||
                double(value)>std::numeric_limits<int>::max()) return {};
        return SDL_Rect{int(values[0]),int(values[1]),int(values[2]),int(values[3])};
    }
    struct TriangleQuadCommand
    {
        SDL_Rect destination{}, source{};
        SDL_Color color{};
    };
    std::optional<TriangleQuadCommand> resolveTriangleQuad(const QuadCorners &quad,
                                                          const void *key, SDL_Surface *source)
    {
        // Require four distinct axis-aligned corners for the portable path.
        // SDL recognizes some degenerate/fan pairs too; those stay on its owner.
        if (!(quad.first.position.x<quad.last.position.x && quad.first.position.y<quad.last.position.y) ||
            (quad.third.position.x==quad.fourth.position.x && quad.third.position.y==quad.fourth.position.y)) return {};
        const auto &color=quad.first.color;
        if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) ||
            !std::isfinite(color.a)) return {};
        const auto byte=[](float value) { return Uint8(std::round(std::clamp(value,0.f,1.f)*255.f)); };
        const SDL_Color tint{byte(color.r),byte(color.g),byte(color.b),byte(color.a)};
        const auto mapped=triangleQuadPixels(quad);
        if (!mapped) return {};
        const SDL_Rect destination=*mapped;
        if (!portableTarget()) return {};
        if (!key) return TriangleQuadCommand{destination,{},tint};
        const auto &uv0=quad.first.tex_coord,&uv1=quad.last.tex_coord;
        if (!source || !(uv0.x<uv1.x && uv0.y<uv1.y)) return {};
        for (const auto *vertex : {&quad.first,&quad.last,&quad.third,&quad.fourth})
        {
            const auto &uv=vertex->tex_coord;
            if (!(uv.x>=0 && uv.x<=1 && uv.y>=0 && uv.y<=1) ||
                uv.x != (vertex->position.x==quad.first.position.x ? uv0.x : uv1.x) ||
                uv.y != (vertex->position.y==quad.first.position.y ? uv0.y : uv1.y)) return {};
        }
        const float x=uv0.x*source->w,y=uv0.y*source->h;
        const SDL_FRect requested{x,y,uv1.x*source->w-x,uv1.y*source->h-y};
        const SDL_FRect available{0,0,float(source->w),float(source->h)};
        SDL_FRect clipped{};
        if (!SDL_GetRectIntersectionFloat(&requested,&available,&clipped)) return {};
        for (float value : {clipped.x,clipped.y,clipped.w,clipped.h})
            if (!std::isfinite(value) || value<0 || double(value)>std::numeric_limits<int>::max()) return {};
        const SDL_Rect sampled{int(clipped.x),int(clipped.y),int(clipped.w),int(clipped.h)};
        if (sampled.w>=512 && sampled.h>=512 && (destination.w>512 || destination.h>512) &&
            !(source->flags & SDL_SURFACE_PREALLOCATED)) return {};
        Uint32 colorKey;
        if (SDL_ISPIXELFORMAT_INDEXED(source->format) || SDL_ISPIXELFORMAT_FOURCC(source->format) ||
            SDL_GetSurfaceColorKey(source,&colorKey)) return {};
        return TriangleQuadCommand{destination,sampled,tint};
    }
    void submitTriangleStream(std::span<const SDL_Vertex> vertices, const void *key,
                              SDL_Surface *source, std::uint64_t revision, bool applyTransform)
    {
        if (vertices.empty()) return;
        if (vertices.size()%3 || vertices.size()>std::size_t(std::numeric_limits<int>::max()))
            throw std::invalid_argument("Invalid triangle batch size");
        std::size_t index=0,fallbackBegin=0;
        std::array<int,3> preceding{0,1,2};
        const auto fallbackRange = [&](std::size_t end) {
            if (fallbackBegin==end) return;
            const auto segment=vertices.subspan(fallbackBegin,end-fallbackBegin);
            if (applyTransform) fallback().triangles(segment,key,source,revision);
            else fallback().screenTriangles(segment);
        };
        while (index+6<=vertices.size())
        {
            const auto quad=triangleQuad(std::span<const SDL_Vertex,6>(vertices.data()+index,6),
                                         key!=nullptr,applyTransform,preceding,int(index));
            if (!quad) { index+=3; continue; }
            const auto command=resolveTriangleQuad(*quad,key,source);
            if (command)
            {
                // A fallback fence completes older painter operations before
                // this command enters the same ordered stream as direct draws.
                fallbackRange(index);
                fallbackBegin=index;
                const bool submitted=key ?
                    submitBlit(source,command->source,command->destination,command->color.a,false,
                               key,revision,nullptr,true,command->color) :
                    submitFill(command->destination,command->color);
                if (submitted) fallbackBegin=index+6;
            }
            index+=6;
            if (index+3<=vertices.size()) preceding={int(index),int(index+1),int(index+2)};
        }
        fallbackRange(vertices.size());
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
        FrameFailureGuard guard(*this);
		++counts.blits;
        if (!alpha) return;
        const bool quad = !opaque || alpha != 255 || source->format != target->format;
        // The geometry facade treats a null key as untextured. Keep that legacy
        // contract for draws that previously entered SDL's triangle path.
        if (quad && !key)
        {
            fallback().blit(key, source, revision, opaque, sourceRect, destination, alpha);
            return;
        }
        const SDL_Rect mapped = quad ? quadPixels(destination) : pixels(destination);
        // Retain the separately qualified legacy large-triangle appearance.
        if ((sourceRect.w >= 512 && sourceRect.h >= 512 &&
            (mapped.w > 512 || mapped.h > 512) && !(source->flags & SDL_SURFACE_PREALLOCATED)) ||
            (quad && (destination.w < 0 || destination.h < 0 || sourceRect.w < 0 || sourceRect.h < 0 ||
                sourceRect.x < 0 || sourceRect.y < 0 ||
                std::int64_t(sourceRect.x)+sourceRect.w > source->w ||
                std::int64_t(sourceRect.y)+sourceRect.h > source->h)))
        {
            fallback().blit(key, source, revision, opaque, sourceRect, destination, alpha);
            return;
        }
        const SDL_Rect sampled = quad ? quadSource(sourceRect, source) : sourceRect;
        {
            PERF_SCOPE_TIME(RasterCapture);
            if (submitBlit(source, sampled, mapped, alpha, opaque, key, revision, nullptr, quad)) return;
        }
        fallback().blit(key, source, revision, opaque, sourceRect, destination, alpha);
    }
    bool fillPixels(const SDL_Rect &rect, SDL_Color color) override
    {
        FrameFailureGuard guard(*this);
        ++counts.fills;
        {
            PERF_SCOPE_TIME(RasterCapture);
            if (submitFill(rect, color)) return true;
        }
        flush();
        TargetClip clip(target, outputClip());
        SurfaceRaster::fill(target, rect, SDL_MapSurfaceRGBA(target, color.r, color.g, color.b, 255),
            color.a, SurfaceRaster::FillBlend::SourceOver);
        return true;
    }
    bool blitPixels(const void *key, SDL_Surface *source, std::uint64_t revision,
                    const SDL_Rect &src, const SDL_Rect &dst, Uint8 alpha) override
    {
        FrameFailureGuard guard(*this);
        ++counts.blits;
        {
            PERF_SCOPE_TIME(RasterCapture);
            if (submitBlit(source, src, dst, alpha, false, key, revision)) return true;
        }
        flush();
        TargetClip clip(target, outputClip());
        SurfaceRaster::blit(target, source, src, dst, alpha, false, SurfaceRaster::BlitBlend::Triangle);
        return true;
    }
    bool blitPixels(SDL_Surface *source, const SDL_Rect &src, const SDL_Rect &dst, Uint8 alpha) override
    {
        return blitPixels(nullptr, source, 0, src, dst, alpha);
    }

    void blitLinear(const void *key, SDL_Surface *source, std::uint64_t revision,
                    const SDL_Rect &src, const SDL_FRect &dst, Uint8 alpha) override
    {
        FrameFailureGuard guard(*this);
        ++counts.blits;
        const SDL_FRect mapped{float((dst.x*scale+offsetX)*nativeX),
            float((dst.y*scale+offsetY)*nativeY), float(dst.w*scale*nativeX), float(dst.h*scale*nativeY)};
        if (!alpha || mapped.w <= 0 || mapped.h <= 0) return;
        const auto endpoint = [](double value) {
            value = std::ceil(value);
            if (!std::isfinite(value) || value < std::numeric_limits<int>::min() ||
                value > std::numeric_limits<int>::max())
                throw std::invalid_argument("Invalid linear destination");
            return int(value);
        };
        const int left = endpoint(mapped.x-.5f), top = endpoint(mapped.y-.5f);
        const auto width = std::int64_t(endpoint(mapped.x + mapped.w-.5f)) - left;
        const auto height = std::int64_t(endpoint(mapped.y + mapped.h-.5f)) - top;
        if (width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max())
            throw std::invalid_argument("Oversized linear destination");
        const SDL_Rect coverage{left, top, int(width), int(height)};
        if (submitBlit(source, src, coverage, alpha, false, key, revision, &mapped)) return;
        flush();
        TargetClip clip(target, outputClip());
        SurfaceRaster::skinBlitFloat(target, source, src, mapped, alpha);
    }
    void fill(const SDL_FRect &rect, SDL_Color color) override
    {
        FrameFailureGuard guard(*this);
        ++counts.fills;
        if (!color.a) return;
        if (color.a != 255 && (rect.w < 0 || rect.h < 0)) { fallback().fill(rect,color); return; }
        if (submitFill(color.a == 255 ? pixels(rect) : quadPixels(rect), color)) return;
        fallback().fill(rect, color);
    }
	void triangles(std::span<const SDL_Vertex> vertices, const void *key, SDL_Surface *source,
				   std::uint64_t revision) override
	{
        FrameFailureGuard guard(*this);
		++counts.triangles;
        submitTriangleStream(vertices,key,source,revision,true);
	}
	void screenTriangles(std::span<const SDL_Vertex> vertices) override
	{
        FrameFailureGuard guard(*this);
		++counts.triangles;
        submitTriangleStream(vertices,nullptr,nullptr,0,false);
	}
	RenderOperations operations() const override {
        auto result = counts;
        result.rasterSourceRetainedBytes = sourceCache.bytes();
        result.rasterSnapshotBytes = sourceCache.copiedBytes();
        result.rasterSourceReuses = sourceCache.reuses();
        return result;
    }
	void forget(const void *key) override
	{
        flush();
        sourceCache.erase(key);
		if (geometry)
			geometry->forget(key);
	}
	void reset() override
	{
		flush();
        sourceCache.clear();
		geometry.reset();
	}
	void flush() override
	{
        FrameFailureGuard guard(*this);
        executeSegment();
        drainGeometry();
	}
    void discardFrame() noexcept override
    {
        clearSegment();
        sourceCache.clear();
        geometry.reset();
        failedRaster = false;
	}
    void present() override {
        flush();
        sourceCache.trim();
    }
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
