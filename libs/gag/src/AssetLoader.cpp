// SPDX-License-Identifier: GPL-3.0-or-later
#include "MipReduction.h"
#include <AssetLoader.h>
#include <chrono>
#include <FileManager.h>
#include <ThreadSupport.h>
#include <webp/decode.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <regex>

namespace GAGCore {
namespace {
using Clock = std::chrono::steady_clock;
std::atomic<size_t> nextSourceGeneration{1};
std::uint64_t elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
unsigned setting(const char *name, unsigned fallback, unsigned maximum) {
    const auto *text = std::getenv(name);
    if (!text || !*text) return fallback;
    char *end = nullptr;
    const auto value = std::strtoul(text, &end, 10);
    if (*end || value > maximum) throw std::invalid_argument(std::string("Invalid ") + name);
    return static_cast<unsigned>(value);
}
std::filesystem::path utf8Path(const std::string& value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
WebPBitstreamFeatures features(const AssetLoader::Bytes& bytes) {
    WebPBitstreamFeatures result{};
    if (WebPGetFeatures(bytes.data(), bytes.size(), &result) != VP8_STATUS_OK ||
        result.width <= 0 || result.height <= 0 || result.has_animation ||
        std::uint64_t(result.width) * result.height > 64u * 1024u * 1024u)
        throw std::runtime_error("Invalid or unsupported WebP image");
    return result;
}
size_t imageWorkingBytes(const WebPBitstreamFeatures& info, bool highResolution) {
    size_t bytes = size_t(info.width) * info.height * 8; // surface and codec scratch
#if defined(GLOB2_WEBGL2) || SDL_BYTEORDER == SDL_BIG_ENDIAN
    bytes += size_t(info.width) * info.height * 4;
#endif
    if (highResolution) {
        size_t w = 1, h = 1;
        while (w < size_t(info.width)) w *= 2;
        while (h < size_t(info.height)) h *= 2;
        for (;;) {
            bytes += w * h * 4;
            if (w == 1 && h == 1) break;
            w = std::max(size_t(1), w / 2); h = std::max(size_t(1), h / 2);
        }
    }
    return bytes;
}
std::shared_ptr<const AssetImage> decodeImage(std::shared_ptr<const AssetLoader::Bytes> input, bool highResolution = false) {
    const auto info = features(*input);
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurface(info.width, info.height, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
    if (!surface) throw std::runtime_error(SDL_GetError());
    WebPDecoderConfig config{};
    if (!WebPInitDecoderConfig(&config)) throw std::runtime_error("WebP decoder ABI mismatch");
    config.output.colorspace = SDL_BYTEORDER == SDL_LIL_ENDIAN ? MODE_BGRA : MODE_ARGB;
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = static_cast<uint8_t*>(surface->pixels);
    config.output.u.RGBA.stride = surface->pitch;
    config.output.u.RGBA.size = size_t(surface->pitch) * surface->h;
    const auto status = WebPDecode(input->data(), input->size(), &config);
    WebPFreeDecBuffer(&config.output);
    if (status != VP8_STATUS_OK) throw std::runtime_error("WebP decode failed: " + std::to_string(status));
    auto result = std::make_shared<AssetImage>(surface.release());
    result->prepareUpload(highResolution);
    return result;
}


}
namespace {
// BC3 (DXT5) block encoder for worker threads: a bounding box inset by a
// sixteenth of its range, its diagonal chosen by each channel's covariance
// with the widest one (the real-time encoder of van Waveren), then nearest
// palette entries. Pixels are B, G, R, A bytes.
std::uint16_t to565(int r, int g, int b) {
    return std::uint16_t(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255));
}
void from565(std::uint16_t c, int &r, int &g, int &b) {
    r = (c >> 11) & 31; g = (c >> 5) & 63; b = c & 31;
    r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
}
void encodeBC3Block(const unsigned char *pixels, int width, unsigned char *out) {
    int px[16][4];
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const auto *p = pixels + (size_t(y) * width + x) * 4;
            auto *q = px[y * 4 + x];
            q[0] = p[2]; q[1] = p[1]; q[2] = p[0]; q[3] = p[3];
        }
    // Alpha: endpoints max and min, eight levels between them.
    int amin = 255, amax = 0;
    for (auto &p : px) { amin = std::min(amin, p[3]); amax = std::max(amax, p[3]); }
    out[0] = std::uint8_t(amax); out[1] = std::uint8_t(amin);
    std::uint64_t alphaBits = 0;
    if (amax > amin) {
        int levels[8] = {amax, amin};
        for (int i = 1; i < 7; ++i) levels[i + 1] = ((7 - i) * amax + i * amin + 3) / 7;
        for (int i = 0; i < 16; ++i) {
            int best = 0, bestError = 256;
            for (int k = 0; k < 8; ++k)
                if (const int e = std::abs(px[i][3] - levels[k]); e < bestError) { bestError = e; best = k; }
            alphaBits |= std::uint64_t(best) << (3 * i);
        }
    }
    for (int i = 0; i < 6; ++i) out[2 + i] = std::uint8_t(alphaBits >> (8 * i));
    // Colour: inset bounding box, diagonal by covariance with the widest channel.
    int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0}, mean[3] = {0, 0, 0};
    for (auto &p : px)
        for (int c = 0; c < 3; ++c) { lo[c] = std::min(lo[c], p[c]); hi[c] = std::max(hi[c], p[c]); mean[c] += p[c]; }
    int axis = 0;
    for (int c = 1; c < 3; ++c) if (hi[c] - lo[c] > hi[axis] - lo[axis]) axis = c;
    for (int c = 0; c < 3; ++c) mean[c] = (mean[c] + 8) / 16;
    for (int c = 0; c < 3; ++c) {
        if (c != axis) {
            long covariance = 0;
            for (auto &p : px) covariance += long(p[c] - mean[c]) * (p[axis] - mean[axis]);
            if (covariance < 0) std::swap(lo[c], hi[c]);
        }
    }
    for (int c = 0; c < 3; ++c) {
        const int inset = (hi[c] - lo[c]) / 16;
        hi[c] -= inset; lo[c] += inset;
    }
    // Nearest palette entries for a pair of endpoints, and their total error.
    struct Fit { std::uint16_t c0, c1; std::uint32_t bits; long error; };
    const auto fit = [&](const int *a, const int *b)
    {
        Fit result{to565(a[0], a[1], a[2]), to565(b[0], b[1], b[2]), 0, 0};
        if (result.c0 < result.c1) std::swap(result.c0, result.c1);
        int palette[4][3];
        from565(result.c0, palette[0][0], palette[0][1], palette[0][2]);
        from565(result.c1, palette[1][0], palette[1][1], palette[1][2]);
        for (int c = 0; c < 3; ++c) {
            palette[2][c] = (2 * palette[0][c] + palette[1][c] + 1) / 3;
            palette[3][c] = (palette[0][c] + 2 * palette[1][c] + 1) / 3;
        }
        const int entries = result.c0 == result.c1 ? 1 : 4;
        for (int i = 0; i < 16; ++i) {
            int best = 0, bestError = 1 << 30;
            for (int k = 0; k < entries; ++k) {
                int e = 0;
                for (int c = 0; c < 3; ++c) { const int d = px[i][c] - palette[k][c]; e += d * d; }
                if (e < bestError) { bestError = e; best = k; }
            }
            result.bits |= std::uint32_t(best) << (2 * i);
            result.error += bestError;
        }
        return result;
    };
    Fit best = fit(hi, lo);
    // One least-squares refinement of both endpoints for the chosen indices.
    if (best.c0 != best.c1) {
        static constexpr float weight[4] = {1, 0, 2.f / 3, 1.f / 3};
        float aa = 0, ab = 0, bb = 0, x0[3] = {}, x1[3] = {};
        for (int i = 0; i < 16; ++i) {
            const float w = weight[(best.bits >> (2 * i)) & 3], v = 1 - w;
            aa += w * w; ab += w * v; bb += v * v;
            for (int c = 0; c < 3; ++c) { x0[c] += w * px[i][c]; x1[c] += v * px[i][c]; }
        }
        const float det = aa * bb - ab * ab;
        if (det > 1e-3f) {
            int e0[3], e1[3];
            for (int c = 0; c < 3; ++c) {
                e0[c] = std::clamp(int(std::lround((bb * x0[c] - ab * x1[c]) / det)), 0, 255);
                e1[c] = std::clamp(int(std::lround((aa * x1[c] - ab * x0[c]) / det)), 0, 255);
            }
            if (const Fit refined = fit(e0, e1); refined.error < best.error) best = refined;
        }
    }
    out[8] = std::uint8_t(best.c0); out[9] = std::uint8_t(best.c0 >> 8);
    out[10] = std::uint8_t(best.c1); out[11] = std::uint8_t(best.c1 >> 8);
    const std::uint32_t colorBits = best.bits;
    for (int i = 0; i < 4; ++i) out[12 + i] = std::uint8_t(colorBits >> (8 * i));
}
void compressBC3(AssetImage::Mip &level) {
    if (level.width < 4 || level.height < 4) return;
    level.compressed.resize(size_t(level.width / 4) * (level.height / 4) * 16);
    auto *out = level.compressed.data();
    for (int y = 0; y < level.height; y += 4)
        for (int x = 0; x < level.width; x += 4, out += 16)
            encodeBC3Block(level.pixels.data() + (size_t(y) * level.width + x) * 4, level.width, out);
}
}
void AssetImage::prepareUpload(bool highResolution) {
#if defined(GLOB2_WEBGL2) || SDL_BYTEORDER == SDL_BIG_ENDIAN
    uploadPixels.resize(size_t(surface->w) * surface->h * 4);
    for (int y = 0; y < surface->h; ++y) {
        const auto *source = reinterpret_cast<const Uint32*>(static_cast<const unsigned char*>(surface->pixels) + y * surface->pitch);
        auto *dest = uploadPixels.data() + size_t(y) * surface->w * 4;
        for (int x = 0; x < surface->w; ++x) {
            dest[x*4] = source[x] >> 16; dest[x*4+1] = source[x] >> 8;
            dest[x*4+2] = source[x]; dest[x*4+3] = source[x] >> 24;
        }
    }
#endif
    if (!highResolution) return;
    mips.clear();
    auto power = [](int value) { int result = 1; while (result < value) result *= 2; return result; };
    int width = power(surface->w), height = power(surface->h);
    Mip level{width, height, std::vector<unsigned char>(size_t(width) * height * 4)};
    for (int y = 0; y < height; ++y) {
        const int sy = std::min(y, surface->h - 1);
        const auto *source = uploadPixels.empty()
            ? static_cast<const unsigned char*>(surface->pixels) + sy * surface->pitch
            : uploadPixels.data() + size_t(sy) * surface->w * 4;
        auto *dest = level.pixels.data() + size_t(y) * width * 4;
        std::copy_n(source, size_t(surface->w) * 4, dest);
        for (int x = surface->w; x < width; ++x)
            std::copy_n(source + (surface->w - 1) * 4, 4, dest + x * 4);
    }
    for (;;) {
        mips.push_back(std::move(level));
        if (width == 1 && height == 1) break;
        const int nextWidth = std::max(1, width / 2), nextHeight = std::max(1, height / 2);
        level = {nextWidth, nextHeight, std::vector<unsigned char>(size_t(nextWidth) * nextHeight * 4)};
        downsampleMip(mips.back().pixels.data(), width, height, level.pixels.data());
        width = nextWidth; height = nextHeight;
    }
#if !defined(GLOB2_WEBGL2) && SDL_BYTEORDER == SDL_LIL_ENDIAN
    // The driver would otherwise compress each level inside glTexImage2D on
    // the graphics thread: tens of milliseconds for a 2048-pixel HD sheet.
    if (compressHighResolution.load(std::memory_order_relaxed))
        for (auto &mip : mips) compressBC3(mip);
#endif
}
void downsampleMip(const unsigned char *source, int width, int height,
                               unsigned char *destination) {
    const int nextWidth = std::max(1, width / 2), nextHeight = std::max(1, height / 2);
    for (int y = 0; y < nextHeight; ++y) for (int x = 0; x < nextWidth; ++x) {
        const auto *top = source + (size_t(y * 2) * width + x * 2) * 4;
        const auto *bottom = top + (height > 1 ? width * 4 : 0);
        const int right = width > 1 ? 4 : 0;
        const unsigned alpha = top[3] + top[right + 3] + bottom[3] + bottom[right + 3];
        // Most generated terrain footprints are opaque or undiscovered.
        // Keep exactly the weighted filter's rounding without multiplying
        // every channel by 255 or processing invisible RGB.
        auto *pixel = destination + (size_t(y) * nextWidth + x) * 4;
        if (!alpha) {
            std::fill_n(pixel, 4, 0);
            continue;
        }
        if (alpha == 4u * 255) {
            for (int c = 0; c < 3; ++c)
                pixel[c] = (unsigned(top[c]) + top[right + c] + bottom[c] + bottom[right + c] + 2) / 4;
            pixel[3] = 255;
            continue;
        }
        unsigned sum[3] = {};
        for (const auto *pixel : {top, top + right, bottom, bottom + right}) {
            for (int c = 0; c < 3; ++c) sum[c] += pixel[c] * pixel[3];
        }
        for (int c = 0; c < 3; ++c)
            pixel[c] = (sum[c] + alpha / 2) / alpha;
        pixel[3] = (alpha + 2) / 4;
    }
}
AssetLoader::Options AssetLoader::Options::environment() {
    Options result;
    const auto cores = std::max(1u, std::thread::hardware_concurrency());
    // Bundled artwork consists mostly of small independent images. Beyond eight
    // decoders, queue contention and memory traffic outweighed extra compute in
    // the asset benchmark. Keep a core free and allow explicit hardware tuning.
    result.cpuThreads = setting("GLOB2_ASSET_THREADS", std::min(8u, cores > 1 ? cores - 1 : 1), 1024);
#if defined(__EMSCRIPTEN__) || defined(GLOB2_MOBILE)
    result.ioThreads = setting("GLOB2_ASSET_IO_THREADS", 1, 64);
    const unsigned fallback = 64;
#else
    result.ioThreads = setting("GLOB2_ASSET_IO_THREADS", 2, 64);
    const unsigned fallback = 256;
#endif
    const auto ram = SDL_GetSystemRAM(); // MiB; zero means unavailable
    const unsigned automatic = ram > 0 ? std::clamp(unsigned(ram) / 8, 64u,
#if defined(__EMSCRIPTEN__) || defined(GLOB2_MOBILE)
        128u
#else
        512u
#endif
    ) : fallback;
    result.memoryBytes = size_t(setting("GLOB2_ASSET_MEMORY_MB", automatic, 16384)) * 1024 * 1024;
    if (!result.memoryBytes) throw std::invalid_argument("GLOB2_ASSET_MEMORY_MB must be positive");
    if (!ThreadSupport::available || !result.cpuThreads) result.cpuThreads = result.ioThreads = 0;
#ifdef __EMSCRIPTEN__
    // MEMFS is owned by the application/browser host. Read there, hand immutable
    // bytes to decode workers, and never make a worker wait on filesystem proxies.
    result.ioThreads = 0;
#endif
    return result;
}
struct DeferredAssetRead {};
struct AssetLoader::Impl {
    CommunityFiles communityFiles;
    struct Job {
        std::shared_ptr<Result> result;
        std::vector<Dependency> dependencies;
        std::function<std::shared_ptr<const void>()> work;
        std::function<size_t()> estimate;
        bool read;
        Priority priority;
        size_t reserved = 0;
    };
    std::vector<std::string> directories;
    Options options;
    std::thread::id owner = std::this_thread::get_id();
    mutable std::mutex mutex;
    std::shared_ptr<std::condition_variable> ready = std::make_shared<std::condition_variable>();
    std::shared_ptr<std::condition_variable> cpuWake = std::make_shared<std::condition_variable>();
    // Bumped whenever the queue, a job's state or the memory budget changes, so
    // idle workers rescan only after something could have become runnable. The
    // owner thread runs browser reads under the same mutex; constant rescans of
    // a long queue starved it and stalled startup on slow machines.
    std::shared_ptr<std::atomic<std::uint64_t>> changes = std::make_shared<std::atomic<std::uint64_t>>(0);
    void changed() { ++*changes; }
    std::condition_variable ioWake;
    std::shared_ptr<std::atomic<size_t>> encodedBytes = std::make_shared<std::atomic<size_t>>(0);
    std::deque<std::unique_ptr<Job>> jobs;
    struct Finalizer { Dependency dependency; std::weak_ptr<void> lifetime; std::function<void()> run; };
    std::deque<Finalizer> finalizers;
    std::unordered_map<std::string, std::weak_ptr<Result>> cache;
    size_t submissionsUntilSweep = 128;
    std::vector<std::thread> cpu, io;
    bool stopping = false;
    size_t generation = nextSourceGeneration.fetch_add(1);
    Metrics totals;
    const FileManager *files = nullptr;
    void checkOwner() const { if (owner != std::this_thread::get_id()) throw std::logic_error("Asset requests and polling require the owner thread"); }
    // The caller holds mutex. Retain demand and promote queued work exactly as
    // submit does, without constructing dependencies for an already cached job.
    std::shared_ptr<Result> cached(const std::string& cacheKey, Priority priority, bool read, bool readyOnly = false) {
        if (auto result = cache[cacheKey].lock()) {
            if (readyOnly && result->state != State::Ready) return {};
            if (result->state != State::Failed && result->state != State::Cancelled && result->consumers) {
                for (auto &job : jobs) if (job->result == result && priority < job->priority) job->priority = priority;
                ++result->consumers;
                changed();
                (read ? ioWake : *cpuWake).notify_one();
                return result;
            }
        }
        return {};
    }
    void refreshDirectories() {
        checkOwner();
        std::vector<std::string> mounted;
        for (unsigned i = 0; i < files->getDirCount(); ++i) mounted.push_back(files->getDir(i));
        if (mounted == directories) return;
        std::lock_guard lock(mutex);
        directories = std::move(mounted);
        generation = nextSourceGeneration.fetch_add(1);
        cache.clear();
    }
    void sweepExpiredCache() {
        // Unique preview/streaming keys must not retain metadata for the entire
        // session. Sweep periodically; live subscriptions remain deduplicated.
        if (--submissionsUntilSweep) return;
        submissionsUntilSweep = 128;
        std::erase_if(cache, [](const auto& entry) { return entry.second.expired(); });
    }
    bool fits(size_t bytes) const {
        const size_t used = totals.workingBytes + encodedBytes->load();
        return bytes <= options.memoryBytes - std::min(options.memoryBytes, used) ||
            (!totals.workingBytes && bytes > options.memoryBytes - std::min(options.memoryBytes, encodedBytes->load()));
    }
    void reserve(size_t bytes) {
        totals.workingBytes += bytes;
        totals.peakWorkingBytes = std::max(totals.peakWorkingBytes, totals.workingBytes);
        if (bytes > options.memoryBytes) ++totals.oversizedJobs;
    }
    // Only compressed image inputs are queue credits. Fonts and metadata may
    // deliberately retain their source bytes for the application's lifetime.
    void acquireRead(size_t bytes, bool transient) {
        std::unique_lock lock(mutex);
        auto admitted = [&] {
            const auto queued = encodedBytes->load();
            const auto limit = options.memoryBytes / 4;
            return stopping || (fits(bytes) && (!transient || !queued ||
                (queued <= limit && bytes <= limit - queued)));
        };
        // A cooperative reader must yield to the decoder, not wait on itself.
        if (io.empty() && !admitted()) throw DeferredAssetRead{};
        ready->wait(lock, admitted);
        if (stopping) throw std::runtime_error("Asset loader stopped");
        reserve(bytes);
        if (transient) *encodedBytes += bytes;
    }
    void releaseRead(size_t bytes) {
        { std::lock_guard lock(mutex); totals.workingBytes -= bytes; }
        changed();
        ready->notify_all();
        cpuWake->notify_one();
    }
    std::unique_ptr<Job> take(bool read) {
        auto best = jobs.end();
        for (auto it = jobs.begin(); it != jobs.end();) {
            auto &job = **it;
            State state = State::Ready;
            for (const auto &dependency : job.dependencies) {
                const auto current = dependency.state();
                if (!dependency.required && (current == State::Failed || current == State::Cancelled)) continue;
                if (current == State::Failed || current == State::Cancelled) { state = current; break; }
                if (current == State::Pending) state = State::Pending;
            }
            if (!job.result->consumers || state == State::Failed || state == State::Cancelled) {
                job.result->error = state == State::Failed ? "Asset dependency failed" : "Asset request cancelled";
                const auto terminal = state == State::Failed ? State::Failed : State::Cancelled;
                job.result->state.store(terminal, std::memory_order_release);
                if (terminal == State::Failed) ++totals.failed; else ++totals.cancelled;
                it = jobs.erase(it);
                best = jobs.end();
                continue;
            }
            if (job.read == read && state == State::Ready &&
                (best == jobs.end() || job.priority < (*best)->priority)) {
                try {
                    job.reserved = job.estimate();
                    if (fits(job.reserved)) {
                        best = it;
                        if (job.priority == Priority::Required) break;
                    }
                } catch (const std::exception& error) {
                    job.result->error = error.what();
                    job.result->state.store(State::Failed, std::memory_order_release);
                    ++totals.failed;
                    it = jobs.erase(it);
                    best = jobs.end();
                    continue;
                }
            }
            ++it;
        }
        if (best == jobs.end()) return {};
        auto result = std::move(*best);
        jobs.erase(best);
        reserve(result->reserved);
        auto &active = read ? totals.activeReads : totals.activeCpu;
        auto &peak = read ? totals.peakReads : totals.peakCpu;
        peak = std::max(peak, ++active);
        return result;
    }
    bool execute(std::unique_ptr<Job> job) {
        const auto start = Clock::now();
        bool succeeded = false;
        try {
            auto value = job->work();
            if (!value) throw std::runtime_error("Asset preparation returned no data");
            job->result->value = std::move(value);
            succeeded = true;
        } catch (const DeferredAssetRead&) {
            std::lock_guard lock(mutex);
            totals.workingBytes -= job->reserved;
            --totals.activeReads;
            jobs.push_back(std::move(job));
            changed();
            return false;
        } catch (const std::exception& error) {
            job->result->error = error.what();
        } catch (...) {
            job->result->error = "Unknown asset preparation failure";
        }
        // Drop inputs and preparation captures before announcing released memory.
        job->dependencies.clear(); job->work = {}; job->estimate = {};
        {
            std::lock_guard lock(mutex);
            totals.workingBytes -= job->reserved;
            --(job->read ? totals.activeReads : totals.activeCpu);
            (job->read ? totals.readNs : totals.prepareNs) += elapsed(start);
            if (succeeded) ++totals.completed; else ++totals.failed;
            // Readiness also means input credits and preparation scratch have
            // been released. Owners can consume immediately after this store.
            job->result->state.store(succeeded ? State::Ready : State::Failed, std::memory_order_release);
        }
        changed();
        ready->notify_all();
        cpuWake->notify_one();
        return true;
    }
    void worker(bool read) {
        std::unique_lock lock(mutex);
        auto &wake = read ? ioWake : *cpuWake;
        while (!stopping) {
            auto job = take(read);
            if (!job) {
                // A fallback timeout covers budget changes made outside the lock.
                const auto seen = changes->load();
                wake.wait_for(lock, std::chrono::milliseconds(100), [&] { return stopping || changes->load() != seen; });
                continue;
            }
            // Wake another worker only when this pool has taken actual work.
            // Broadcasting every completion makes large pools contend on the
            // dependency queue even when most jobs are still waiting on reads.
            changed();
            wake.notify_one();
            lock.unlock(); execute(std::move(job)); lock.lock();
        }
    }
};
AssetLoader::AssetLoader(const FileManager& files, Options options) : impl(std::make_unique<Impl>()) {
    if (!options.memoryBytes) throw std::invalid_argument("Asset working budget must be positive");
    if (!ThreadSupport::available || !options.cpuThreads) options.cpuThreads = options.ioThreads = 0;
#ifdef __EMSCRIPTEN__
    options.ioThreads = 0;
#endif
    impl->options = options;
    impl->files = &files;
    for (unsigned i = 0; i < files.getDirCount(); ++i) impl->directories.push_back(files.getDir(i));
    try {
        for (unsigned i = 0; i < options.cpuThreads; ++i)
            impl->cpu.push_back(ThreadSupport::launch([this] { impl->worker(false); }));
        for (unsigned i = 0; i < options.ioThreads; ++i)
            impl->io.push_back(ThreadSupport::launch([this] { impl->worker(true); }));
    } catch (...) {
        { std::lock_guard lock(impl->mutex); impl->stopping = true; }
        impl->ready->notify_all();
        impl->cpuWake->notify_all(); impl->ioWake.notify_all();
        for (auto &thread : impl->cpu) thread.join();
        for (auto &thread : impl->io) thread.join();
        impl->cpu.clear(); impl->io.clear(); impl->stopping = false;
    }
    impl->totals.cpuThreads = impl->cpu.size(); impl->totals.ioThreads = impl->io.size();
}
AssetLoader::~AssetLoader() { shutdown(); }
std::shared_ptr<AssetLoader::Result> AssetLoader::findReady(const std::string& key, std::type_index type,
        Priority priority) {
    impl->refreshDirectories();
    std::lock_guard lock(impl->mutex);
    if (impl->stopping) throw std::runtime_error("Asset loader stopped");
    return impl->cached(std::to_string(impl->generation) + ':' + type.name() + ':' + key,
        priority, false, true);
}
std::shared_ptr<AssetLoader::Result> AssetLoader::submit(const std::string& key, std::type_index type,
        std::vector<Dependency> dependencies, std::function<std::shared_ptr<const void>()> work,
        std::function<size_t()> estimate, bool read, Priority priority) {
    impl->refreshDirectories();
    std::lock_guard lock(impl->mutex);
    if (impl->stopping) throw std::runtime_error("Asset loader stopped");
    impl->sweepExpiredCache();
    const auto cacheKey = std::to_string(impl->generation) + ':' + type.name() + ':' + key;
    if (auto result = impl->cached(cacheKey, priority, read)) return result;
    auto result = std::make_shared<Result>(); result->type = type;
    // Establish demand before unlocking: the returned Handle adds its own
    // subscription immediately below; workers may otherwise see zero consumers.
    ++result->consumers;
    impl->cache[cacheKey] = result;
    impl->jobs.push_back(std::make_unique<Impl::Job>(Impl::Job{result, std::move(dependencies),
        std::move(work), std::move(estimate), read, priority}));
    ++impl->totals.submitted;
    impl->changed();
    (read ? impl->ioWake : *impl->cpuWake).notify_one();
    return result;
}
void AssetLoader::setCommunityFiles(CommunityFiles files) {
    impl->checkOwner();
    static const std::regex path("^community/buildings/[0-9a-f]{64}/sprite[0-9]+r?\\.webp$");
    size_t bytes=0;
    for(const auto& [name, content] : files) {
        if(!std::regex_match(name,path) || !content) throw std::invalid_argument("Invalid community asset path");
        bytes+=content->size();
        if(bytes>64u*1024u*1024u) throw std::invalid_argument("Community assets exceed 64 MiB");
        const auto old=impl->communityFiles.find(name);
        if(old!=impl->communityFiles.end() && *old->second!=*content) throw std::invalid_argument("Community asset identity changed");
    }
    impl->communityFiles=std::move(files);
    invalidate();
}
AssetLoader::Handle<AssetLoader::Directory> AssetLoader::requestDirectory(const std::string& folder) {
    impl->checkOwner();
    if(folder.starts_with("community/buildings/")) {
        auto result=std::make_shared<Directory>();
        const auto prefix=folder+'/';
        for(const auto& [name,content] : impl->communityFiles)
            if(name.starts_with(prefix) && name.find('/',prefix.size())==std::string::npos)
                result->names.push_back(name.substr(prefix.size()));
        return requestRead<Directory>("directory:"+folder,[result] {
            // Publish one immutable membership index, shared by sprite requests.
            std::sort(result->names.begin(), result->names.end());
            result->names.erase(std::unique(result->names.begin(), result->names.end()), result->names.end());
            return result;
        });
    }
    impl->refreshDirectories();
    const auto directories = impl->directories;
    return requestRead<Directory>("directory:" + folder, [directories, folder] {
        auto result = std::make_shared<Directory>();
        const bool absolute = utf8Path(folder).is_absolute();
        for (const auto &root : directories) {
            std::error_code error;
            for (std::filesystem::directory_iterator it(utf8Path(absolute ? folder : root + '/' + folder), error), end; !error && it != end; it.increment(error))
                { const auto name = it->path().filename().u8string(); result->names.emplace_back(name.begin(), name.end()); }
            if (absolute) break;
        }
        // Discovery only tests membership. Sort/deduplicate once per directory
        // snapshot; file reads still resolve the original mounted-root order.
        std::sort(result->names.begin(), result->names.end());
        result->names.erase(std::unique(result->names.begin(), result->names.end()), result->names.end());
        return result;
    });
}
AssetLoader::Handle<AssetLoader::Bytes> AssetLoader::requestBytes(const std::string& path, Priority priority) {
    return requestBytesImpl(path, priority, false);
}
AssetLoader::Handle<AssetLoader::Bytes> AssetLoader::requestBytesImpl(const std::string& path, Priority priority,
        bool transient, size_t maxBytes) {
    impl->checkOwner();
    if(path.starts_with("community/buildings/")) {
        const auto found=impl->communityFiles.find(path);
        const auto content=found==impl->communityFiles.end() ? std::shared_ptr<const Bytes>() : found->second;
        return requestRead<Bytes>(path + ':' + std::to_string(maxBytes),[content,path,maxBytes] {
            if(!content) throw std::runtime_error("Missing verified community asset: "+path);
            if(content->size() > maxBytes) throw std::runtime_error("Invalid asset length: " + path);
            return content;
        },priority);
    }
    impl->refreshDirectories();
    const auto directories = impl->directories;
    auto work = [this, directories, path, transient, maxBytes]() -> std::shared_ptr<const void> {
        SDL_IOStream *stream = nullptr;
        if (utf8Path(path).is_absolute())
            stream = SDL_IOFromFile(path.c_str(), "rb");
        else for (const auto &directory : directories)
            if ((stream = SDL_IOFromFile((directory + '/' + path).c_str(), "rb"))) break;
        if (!stream) throw std::runtime_error("Cannot read asset: " + path);
        std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> input(stream, SDL_CloseIO);
        const auto length = SDL_GetIOSize(stream);
        if (length < 0 || uint64_t(length) > maxBytes) throw std::runtime_error("Invalid asset length: " + path);
        impl->acquireRead(size_t(length), transient);
        struct Release {
            Impl *impl; size_t bytes; bool transient; bool transferred = false;
            ~Release() { if (transient && !transferred) *impl->encodedBytes -= bytes; impl->releaseRead(bytes); }
        } release{impl.get(), size_t(length), transient};
        auto *raw = new Bytes(size_t(length));
        release.transferred = true;
        auto bytes = std::shared_ptr<Bytes>(raw, [account = impl->encodedBytes, ready = impl->ready, cpuWake = impl->cpuWake,
                                                  changes = impl->changes, length, transient](Bytes *value) {
            delete value; if (transient) *account -= size_t(length); ++*changes; ready->notify_all(); cpuWake->notify_one();
        });
        if (SDL_ReadIO(stream, bytes->data(), bytes->size()) != bytes->size())
            throw std::runtime_error("Truncated asset: " + path);
        return bytes;
    };
    auto result = submit(path + (transient ? ":decode-input:" : ":resident-input:") + std::to_string(maxBytes),
        typeid(Bytes), {}, std::move(work), [] { return size_t(0); }, true, priority);
    Handle<Bytes> handle(result);
    impl->ready->notify_all();
    return handle;
}
AssetLoader::Handle<AssetImage> AssetLoader::requestImage(const std::string& path, Priority priority, bool highResolution) {
    if (!path.ends_with(".webp"))
        return request<AssetImage>("unsupported-image:" + path, {}, [path]() -> std::shared_ptr<const AssetImage> {
            throw std::runtime_error("Asset images must use WebP: " + path);
        }, 0, priority);
    const auto key = path + (highResolution ? ":mips" : ":base");
    // Pending images still take the ordinary path, which promotes their byte
    // input as well as their decode when request priority increases.
    if (auto result = findReady(key, typeid(AssetImage), priority)) return Handle<AssetImage>(result);
    auto bytes = requestBytesImpl(path, priority, true);
    auto work = [bytes, highResolution]() -> std::shared_ptr<const void> { return decodeImage(bytes.get(), highResolution); };

    auto result = submit(key, typeid(AssetImage), {bytes.dependency()}, std::move(work), [bytes, highResolution] { return imageWorkingBytes(features(*bytes.get()), highResolution); }, false, priority);
    Handle<AssetImage> handle(result);
    impl->ready->notify_all();
    return handle;
}
AssetLoader::Handle<AssetImage> AssetLoader::requestImageBytes(const std::string& key, std::shared_ptr<const Bytes> bytes, Priority priority) {
    return requestEstimated<AssetImage>("memory-image:" + key, {}, [bytes] { return decodeImage(bytes); },
        [bytes] { return imageWorkingBytes(features(*bytes), false); }, priority);
}
void AssetLoader::scheduleFinalizer(Dependency dependency, std::weak_ptr<void> lifetime, std::function<void()> run) {
    impl->checkOwner();
    impl->finalizers.push_back({std::move(dependency), std::move(lifetime), std::move(run)});
}
SDL_Surface *AssetLoader::loadImageSurface(const std::string& path) {
    auto handle = requestImage(path);
    if (!wait(handle)) return nullptr;
    if (auto image = handle.take()) return image->releaseSurface();
    auto image = handle.get();
    return image ? SDL_DuplicateSurface(image->surface) : nullptr;
}
void AssetLoader::poll(std::int64_t budgetMilliseconds) {
    const std::chrono::milliseconds budget(budgetMilliseconds);
    impl->checkOwner();
    if (budget <= std::chrono::milliseconds::zero()) return;
    const auto start = Clock::now();
#ifdef __EMSCRIPTEN__
    pollBrowserVideo();
#endif
    do {
        std::unique_ptr<Impl::Job> job;
        { std::lock_guard lock(impl->mutex);
          if (impl->stopping) return;
          if (impl->cpu.empty()) job = impl->take(false);
          if (!job && impl->io.empty()) job = impl->take(true);
        }
        if (!job) break;
        if (!impl->execute(std::move(job))) break;
    } while (Clock::now() - start < budget);
    impl->ready->notify_all();
    // Like jobs above, publication always progresses: at least one ready
    // finalizer runs per poll even when jobs spent the budget, so a steady
    // stream of loads cannot starve owners waiting for results.
    bool ran = false;
    for (size_t count = impl->finalizers.size(); count && !impl->finalizers.empty() && (!ran || Clock::now() - start < budget); --count) {
        auto finalizer = std::move(impl->finalizers.front()); impl->finalizers.pop_front();
        if (finalizer.lifetime.expired()) continue;
        if (finalizer.dependency.state() == State::Pending) impl->finalizers.push_back(std::move(finalizer));
        else if (finalizer.dependency.state() != State::Cancelled) { finalizer.run(); ran = true; }
        if (ran && Clock::now() - start >= budget) break;
    }
}
void AssetLoader::yieldWhileWaiting() {
    if (!impl->cpu.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
size_t AssetLoader::sourceGeneration() {
    impl->refreshDirectories();
    return impl->generation;
}
void AssetLoader::invalidate() {
    impl->checkOwner(); std::lock_guard lock(impl->mutex); impl->generation = nextSourceGeneration.fetch_add(1); impl->cache.clear();
}
AssetLoader::Metrics AssetLoader::metrics() const {
    std::lock_guard lock(impl->mutex);
    auto totals = impl->totals;
    totals.bufferedEncodedBytes = impl->encodedBytes->load();
    totals.cachedKeys = impl->cache.size();
    return totals;
}
void AssetLoader::shutdown() {
    impl->checkOwner();
    { std::lock_guard lock(impl->mutex); impl->stopping = true; }
    impl->ready->notify_all();
    impl->cpuWake->notify_all(); impl->ioWake.notify_all();
    for (auto &thread : impl->cpu) if (thread.joinable()) thread.join();
    for (auto &thread : impl->io) if (thread.joinable()) thread.join();
    impl->cpu.clear(); impl->io.clear();
    std::lock_guard lock(impl->mutex);
    for (const auto &job : impl->jobs) job->result->state.store(State::Cancelled, std::memory_order_release);
    impl->jobs.clear(); impl->cache.clear(); impl->finalizers.clear();
}
}
