// SPDX-License-Identifier: GPL-3.0-or-later
#include <AssetLoader.h>
#include <FileManager.h>
#include <ThreadSupport.h>
#include <webp/decode.h>
#include <algorithm>
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
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto *source = uploadPixels.empty()
            ? static_cast<const unsigned char*>(surface->pixels) + std::min(y, surface->h - 1) * surface->pitch + std::min(x, surface->w - 1) * 4
            : uploadPixels.data() + (size_t(std::min(y, surface->h - 1)) * surface->w + std::min(x, surface->w - 1)) * 4;
        std::copy_n(source, 4, level.pixels.data() + (size_t(y) * width + x) * 4);
    }
    for (;;) {
        mips.push_back(std::move(level));
        if (width == 1 && height == 1) break;
        const int nextWidth = std::max(1, width / 2), nextHeight = std::max(1, height / 2);
        level = {nextWidth, nextHeight, std::vector<unsigned char>(size_t(nextWidth) * nextHeight * 4)};
        const auto &input = mips.back().pixels;
        for (int y = 0; y < nextHeight; ++y) for (int x = 0; x < nextWidth; ++x) {
            unsigned sum[4] = {};
            for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
                const auto *pixel = input.data() + (size_t(std::min(height - 1, y * 2 + dy)) * width + std::min(width - 1, x * 2 + dx)) * 4;
                sum[3] += pixel[3]; for (int c = 0; c < 3; ++c) sum[c] += pixel[c] * pixel[3];
            }
            auto *pixel = level.pixels.data() + (size_t(y) * nextWidth + x) * 4;
            pixel[3] = (sum[3] + 2) / 4;
            for (int c = 0; c < 3; ++c) pixel[c] = sum[3] ? (sum[c] + sum[3] / 2) / sum[3] : 0;
        }
        width = nextWidth; height = nextHeight;
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
        ready->notify_all();
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
        ready->notify_all();
        cpuWake->notify_one();
        return true;
    }
    void worker(bool read) {
        std::unique_lock lock(mutex);
        auto &wake = read ? ioWake : *cpuWake;
        while (!stopping) {
            auto job = take(read);
            if (!job) { wake.wait_for(lock, std::chrono::milliseconds(10)); continue; }
            // Wake another worker only when this pool has taken actual work.
            // Broadcasting every completion makes large pools contend on the
            // dependency queue even when most jobs are still waiting on reads.
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
std::shared_ptr<AssetLoader::Result> AssetLoader::submit(const std::string& key, std::type_index type,
        std::vector<Dependency> dependencies, std::function<std::shared_ptr<const void>()> work,
        std::function<size_t()> estimate, bool read, Priority priority) {
    impl->refreshDirectories();
    std::lock_guard lock(impl->mutex);
    if (impl->stopping) throw std::runtime_error("Asset loader stopped");
    impl->sweepExpiredCache();
    const auto cacheKey = std::to_string(impl->generation) + ':' + type.name() + ':' + key;
    if (auto result = impl->cache[cacheKey].lock()) {
        if (result->state != State::Failed && result->state != State::Cancelled && result->consumers) {
            for (auto &job : impl->jobs) if (job->result == result && priority < job->priority) job->priority = priority;
            ++result->consumers;
            (read ? impl->ioWake : *impl->cpuWake).notify_one(); return result;
        }
    }
    auto result = std::make_shared<Result>(); result->type = type;
    // Establish demand before unlocking: the returned Handle adds its own
    // subscription immediately below; workers may otherwise see zero consumers.
    ++result->consumers;
    impl->cache[cacheKey] = result;
    impl->jobs.push_back(std::make_unique<Impl::Job>(Impl::Job{result, std::move(dependencies),
        std::move(work), std::move(estimate), read, priority}));
    ++impl->totals.submitted;
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
        return requestRead<Directory>("directory:"+folder,[result] { return result; });
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
        return result;
    });
}
AssetLoader::Handle<AssetLoader::Bytes> AssetLoader::requestBytes(const std::string& path, Priority priority) {
    return requestBytesImpl(path, priority, false);
}
AssetLoader::Handle<AssetLoader::Bytes> AssetLoader::requestBytesImpl(const std::string& path, Priority priority, bool transient) {
    impl->checkOwner();
    if(path.starts_with("community/buildings/")) {
        const auto found=impl->communityFiles.find(path);
        const auto content=found==impl->communityFiles.end() ? std::shared_ptr<const Bytes>() : found->second;
        return requestRead<Bytes>(path,[content,path] {
            if(!content) throw std::runtime_error("Missing verified community asset: "+path);
            return content;
        },priority);
    }
    impl->refreshDirectories();
    const auto directories = impl->directories;
    auto work = [this, directories, path, transient]() -> std::shared_ptr<const void> {
        SDL_IOStream *stream = nullptr;
        if (utf8Path(path).is_absolute())
            stream = SDL_IOFromFile(path.c_str(), "rb");
        else for (const auto &directory : directories)
            if ((stream = SDL_IOFromFile((directory + '/' + path).c_str(), "rb"))) break;
        if (!stream) throw std::runtime_error("Cannot read asset: " + path);
        std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> input(stream, SDL_CloseIO);
        const auto length = SDL_GetIOSize(stream);
        if (length < 0 || length > 256 * 1024 * 1024) throw std::runtime_error("Invalid asset length: " + path);
        impl->acquireRead(size_t(length), transient);
        struct Release {
            Impl *impl; size_t bytes; bool transient; bool transferred = false;
            ~Release() { if (transient && !transferred) *impl->encodedBytes -= bytes; impl->releaseRead(bytes); }
        } release{impl.get(), size_t(length), transient};
        auto *raw = new Bytes(size_t(length));
        release.transferred = true;
        auto bytes = std::shared_ptr<Bytes>(raw, [account = impl->encodedBytes, ready = impl->ready, cpuWake = impl->cpuWake, length, transient](Bytes *value) {
            delete value; if (transient) *account -= size_t(length); ready->notify_all(); cpuWake->notify_one();
        });
        if (SDL_ReadIO(stream, bytes->data(), bytes->size()) != bytes->size())
            throw std::runtime_error("Truncated asset: " + path);
        return bytes;
    };
    auto result = submit(path + (transient ? ":decode-input" : ":resident-input"), typeid(Bytes), {}, std::move(work), [] { return size_t(0); }, true, priority);
    Handle<Bytes> handle(result);
    impl->ready->notify_all();
    return handle;
}
AssetLoader::Handle<AssetImage> AssetLoader::requestImage(const std::string& path, Priority priority, bool highResolution) {
    if (!path.ends_with(".webp"))
        return request<AssetImage>("unsupported-image:" + path, {}, [path]() -> std::shared_ptr<const AssetImage> {
            throw std::runtime_error("Asset images must use WebP: " + path);
        }, 0, priority);
    const auto &name = path;
    auto bytes = requestBytesImpl(name, priority, true);
    auto work = [bytes, highResolution]() -> std::shared_ptr<const void> { return decodeImage(bytes.get(), highResolution); };

    auto result = submit(name + (highResolution ? ":mips" : ":base"), typeid(AssetImage), {bytes.dependency()}, std::move(work), [bytes, highResolution] { return imageWorkingBytes(features(*bytes.get()), highResolution); }, false, priority);
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
void AssetLoader::poll(std::chrono::milliseconds budget) {
    impl->checkOwner();
    if (budget <= std::chrono::milliseconds::zero()) return;
    const auto start = Clock::now();
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
    for (size_t count = impl->finalizers.size(); count && !impl->finalizers.empty() && Clock::now() - start < budget; --count) {
        auto finalizer = std::move(impl->finalizers.front()); impl->finalizers.pop_front();
        if (finalizer.lifetime.expired()) continue;
        if (finalizer.dependency.state() == State::Pending) impl->finalizers.push_back(std::move(finalizer));
        else if (finalizer.dependency.state() != State::Cancelled) finalizer.run();
        if (Clock::now() - start >= budget) break;
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
