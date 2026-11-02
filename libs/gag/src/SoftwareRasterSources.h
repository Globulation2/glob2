// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <PresentationWork.h>
#include <PerformanceTelemetry.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace GAGCore::detail
{
// Private owner-side storage for immutable raster inputs. The backend controls
// draw ordering and keeps segment leases until its synchronous jobs finish.
// Workers borrow pixel views only: reservation destruction stays on the owner.
class RasterSourceCache
{
    struct Ledger { std::size_t bytes = 0; };
public:
    static constexpr std::size_t MaximumBytes = 4u * 1024u * 1024u;
    static constexpr std::size_t RetainedBytes = 1u * 1024u * 1024u;
    struct Snapshot
    {
        std::shared_ptr<Ledger> ledger;
        RenderWorkReservation reservation;
        std::unique_ptr<Uint32[]> pixels;
        SDL_Rect region{};
        std::size_t bytes = 0;
        ~Snapshot() { if (ledger) ledger->bytes -= bytes; }
    };
    using Lease = std::shared_ptr<const Snapshot>;
private:
    static constexpr std::size_t MaximumEntries = 1024;
    struct Entry
    {
        std::uint64_t revision = 0, lastUse = 0;
        int width = 0, height = 0, pitch = 0;
        SDL_PixelFormat format = SDL_PIXELFORMAT_UNKNOWN;
        const void *pixels = nullptr;
        Lease snapshot;
    };
    std::shared_ptr<Ledger> ledger = std::make_shared<Ledger>();
    std::unordered_map<const void *, Entry> entries;
    std::uint64_t clock = 0, copied = 0, reused = 0;
    void evict()
    {
        const auto oldest = std::min_element(entries.begin(), entries.end(),
            [](const auto &a, const auto &b) { return a.second.lastUse < b.second.lastUse; });
        entries.erase(oldest);
    }
public:
    RasterSourceCache() = default;
    RasterSourceCache(const RasterSourceCache &) = delete;
    RasterSourceCache &operator=(const RasterSourceCache &) = delete;
    std::size_t bytes() const { return ledger->bytes; }
    std::uint64_t copiedBytes() const { return copied; }
    std::uint64_t reuses() const { return reused; }
    void erase(const void *identity) { entries.erase(identity); }
    void clear() noexcept { entries.clear(); }
    void trim()
    {
        // Called after the backend releases all segment leases at presentation.
        // A small warm set leaves headroom for the next terrain preparation.
        while (!entries.empty() && bytes() > RetainedBytes) evict();
    }
    // A stable identity/revision reuses a snapshot covering the requested region.
    // Capture only needed pixels; null identities always take a fresh snapshot. Admission
    // failure returns no lease: the backend may flush and retry on the owner.
    Lease capture(const void *identity, std::uint64_t revision,
                  SDL_Surface &source, SDL_Rect retained)
    {
        if (identity)
        {
            auto found = entries.find(identity);
            if (found != entries.end())
            {
                auto &entry = found->second;
                const auto &region = entry.snapshot->region;
                const bool covered = retained.x >= region.x && retained.y >= region.y &&
                    retained.x + retained.w <= region.x + region.w &&
                    retained.y + retained.h <= region.y + region.h;
                if (covered && entry.revision == revision && entry.width == source.w &&
                    entry.height == source.h && entry.pitch == source.pitch &&
                    entry.format == source.format && entry.pixels == source.pixels)
                {
                    entry.lastUse = ++clock;
                    ++reused;
                    return entry.snapshot;
                }
                entries.erase(found); // Segment leases preserve queued revisions.
            }
        }
        const std::size_t needed = std::size_t(retained.w) * retained.h * 4;
        if (needed > MaximumBytes) return {};
        // Cache and queued revisions share one bound. Erasing an entry cannot
        // reclaim its pixels while a segment still owns a lease.
        while (!entries.empty() && (bytes() + needed > MaximumBytes ||
               RenderWorkReservation::available() < needed || entries.size() == MaximumEntries))
            evict();
        if (bytes() + needed > MaximumBytes || RenderWorkReservation::available() < needed)
            return {};
        auto snapshot = std::make_shared<Snapshot>();
        if (!snapshot->reservation.reserve(needed)) return {};
        PERF_SCOPE_TIME(RasterSnapshot);
        snapshot->pixels = std::make_unique_for_overwrite<Uint32[]>(std::size_t(retained.w) * retained.h);
        if (!SDL_LockSurface(&source)) throw std::runtime_error(SDL_GetError());
        struct Unlock
        {
            SDL_Surface *surface;
            ~Unlock() { SDL_UnlockSurface(surface); }
        } unlock{&source};
        const auto *format = SDL_GetPixelFormatDetails(source.format);
        for (int y = 0; y < retained.h; ++y)
        {
            const auto *input = static_cast<const Uint8 *>(source.pixels) +
                std::ptrdiff_t(retained.y + y) * source.pitch +
                retained.x * format->bytes_per_pixel;
            auto *output = snapshot->pixels.get() + std::size_t(y) * retained.w;
            if (source.format == SDL_PIXELFORMAT_ARGB8888)
                std::memcpy(output, input, std::size_t(retained.w) * 4);
            else if (!SDL_ConvertPixels(retained.w, 1, source.format, input,
                                        retained.w * format->bytes_per_pixel,
                                        SDL_PIXELFORMAT_ARGB8888, output, retained.w * 4))
                throw std::runtime_error(SDL_GetError());
        }
        snapshot->region = retained;
        snapshot->bytes = needed;
        snapshot->ledger = ledger;
        ledger->bytes += needed;
        copied += needed;
        if (identity)
            entries.insert_or_assign(identity, Entry{revision, ++clock, source.w, source.h,
                source.pitch, source.format, source.pixels, snapshot});
        return snapshot;
    }
};
} // namespace GAGCore::detail
