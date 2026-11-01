// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

namespace GAGCore {
class FileManager;

// CPU data only: destroying this object never calls a rendering API.
struct AssetImage {
    struct Mip { int width, height; std::vector<unsigned char> pixels; };
    mutable SDL_Surface *surface = nullptr;
    mutable std::vector<unsigned char> uploadPixels;
    mutable std::vector<Mip> mips;
    void prepareUpload(bool highResolution);
    explicit AssetImage(SDL_Surface *value) : surface(value) {}
    ~AssetImage() { SDL_DestroySurface(surface); }
    AssetImage(const AssetImage&) = delete;
    AssetImage& operator=(const AssetImage&) = delete;
    // Only after Handle::take() grants exclusive ownership.
    SDL_Surface *releaseSurface() const { return std::exchange(surface, nullptr); }
};

// Requests/polling/publication belong to the service's owner thread. Preparation
// functions run on workers, own their inputs, and must not wait or touch renderers.
class AssetLoader {
public:
    enum class State { Pending, Ready, Failed, Cancelled };
    enum class Priority { Required, Interactive, Background };
    using Bytes = std::vector<unsigned char>;
    struct Directory { std::vector<std::string> names; };
    struct Options {
        unsigned cpuThreads = 0, ioThreads = 0; // explicit zero = cooperative
        size_t memoryBytes = 64u * 1024u * 1024u;
        static Options environment();
    };
    struct Metrics {
        size_t submitted = 0, completed = 0, failed = 0, cancelled = 0;
        size_t activeCpu = 0, activeReads = 0, peakCpu = 0, peakReads = 0;
        size_t workingBytes = 0, peakWorkingBytes = 0, oversizedJobs = 0;
        size_t bufferedEncodedBytes = 0, cachedKeys = 0;
        std::uint64_t readNs = 0, prepareNs = 0;
        unsigned cpuThreads = 0, ioThreads = 0;
    };
    struct Result {
        std::atomic<State> state{State::Pending};
        std::atomic<unsigned> consumers{0};
        std::shared_ptr<const void> value;
        std::string error;
        std::type_index type{typeid(void)};
    };
    struct Subscription {
        std::shared_ptr<Result> result;
        std::atomic<bool> cancelled{false};
        explicit Subscription(std::shared_ptr<Result> result) : result(std::move(result)) { ++this->result->consumers; }
        void cancel() { if (!cancelled.exchange(true)) --result->consumers; }
        ~Subscription() { cancel(); }
    };
    struct Dependency {
        std::shared_ptr<Subscription> subscription;
        bool required = true;
        State state() const {
            if (!subscription || subscription->cancelled) return State::Cancelled;
            return subscription->result->state.load(std::memory_order_acquire);
        }
    };
    template<class T> class Handle {
        friend class AssetLoader;
        std::shared_ptr<Subscription> subscription;
        explicit Handle(std::shared_ptr<Result> result) : subscription(std::make_shared<Subscription>(result)) { --result->consumers; }
    public:
        Handle() = default;
        State state() const { return dependency().state(); }
        bool pending() const { return state() == State::Pending; }
        std::shared_ptr<const T> get() const {
            if (state() != State::Ready) return {};
            return std::static_pointer_cast<const T>(subscription->result->value);
        }
        std::shared_ptr<const T> take() {
            if (state() != State::Ready || subscription->result->consumers != 1 || subscription.use_count() != 1 || subscription->result->value.use_count() != 1) return {};
            auto result = std::static_pointer_cast<const T>(std::move(subscription->result->value));
            subscription->result->state.store(State::Cancelled, std::memory_order_release);
            return result;
        }
        std::string error() const {
            const auto current = state();
            return current == State::Failed ? subscription->result->error : std::string();
        }
        // Copies share cancellation. Retain an independent subscription before
        // capturing caller-owned inputs in a continuation, so cancelling the
        // caller cannot invalidate an input while preparation is running.
        Handle retain() const {
            if (!subscription || subscription->cancelled) return {};
            Handle retained;
            retained.subscription = std::make_shared<Subscription>(subscription->result);
            return retained;
        }
        void cancel() { if (subscription) subscription->cancel(); }
        Dependency dependency(bool required = true) const { return {subscription, required}; }
    };
    AssetLoader(const FileManager& files, Options options);
    ~AssetLoader();
    AssetLoader(const AssetLoader&) = delete;
    AssetLoader& operator=(const AssetLoader&) = delete;

    // Verified, content-addressed community artwork only; cannot shadow installed files.
    using CommunityFiles = std::map<std::string, std::shared_ptr<const Bytes>>;
    void setCommunityFiles(CommunityFiles files);
    Handle<Directory> requestDirectory(const std::string& folder);
    Handle<Bytes> requestBytes(const std::string& path, Priority priority = Priority::Required);
    Handle<AssetImage> requestImage(const std::string& path, Priority priority = Priority::Required, bool highResolution = false);
    Handle<AssetImage> requestImageBytes(const std::string& key, std::shared_ptr<const Bytes> bytes, Priority priority = Priority::Interactive);
    template<class T> void onReady(Handle<T> handle, std::weak_ptr<void> lifetime,
            std::function<void(std::shared_ptr<const T>)> finalize) {
        scheduleFinalizer(handle.dependency(), std::move(lifetime),
            [handle, finalize = std::move(finalize)] { finalize(handle.get()); });
    }
    SDL_Surface *loadImageSurface(const std::string& path); // owning CPU surface, synchronous adapter
    // Dependencies keep their subscriptions alive until this task finishes.
    template<class T> Handle<T> request(const std::string& key, std::vector<Dependency> dependencies,
            std::function<std::shared_ptr<const T>()> prepare, size_t workingBytes = 0,
            Priority priority = Priority::Required) {
        return Handle<T>(submit(key, typeid(T), std::move(dependencies),
            [prepare = std::move(prepare)] { return std::static_pointer_cast<const void>(prepare()); },
            [workingBytes] { return workingBytes; }, false, priority));
    }
    // Estimation runs under the scheduler lock after dependencies finish. It
    // must be quick, must not wait, and must not call back into this service.
    template<class T> Handle<T> requestEstimated(const std::string& key, std::vector<Dependency> dependencies,
            std::function<std::shared_ptr<const T>()> prepare, std::function<size_t()> estimate,
            Priority priority = Priority::Required) {
        return Handle<T>(submit(key, typeid(T), std::move(dependencies),
            [prepare = std::move(prepare)] { return std::static_pointer_cast<const void>(prepare()); },
            std::move(estimate), false, priority));
    }
    template<class T> Handle<T> requestRead(const std::string& key,
            std::function<std::shared_ptr<const T>()> read, Priority priority = Priority::Required) {
        return Handle<T>(submit(key, typeid(T), {},
            [read = std::move(read)] { return std::static_pointer_cast<const void>(read()); },
            [] { return size_t(0); }, true, priority));
    }
    // Non-blocking even in the browser. Runs cooperative jobs only when no pool
    // exists; a single codec invocation can exceed this scheduling budget.
    void poll(std::chrono::milliseconds budget = std::chrono::milliseconds(2));
    template<class T> std::shared_ptr<const T> wait(const Handle<T>& handle,
            const std::function<void()>& progress = {}) {
        while (handle.pending()) { poll(); if (progress) progress(); yieldWhileWaiting(); }
        return handle.get();
    }
    size_t sourceGeneration(); // refreshes mounted directories on the owner thread
    void invalidate(); // future requests get a new source generation
    Metrics metrics() const;
    void shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::shared_ptr<Result> submit(const std::string&, std::type_index,
        std::vector<Dependency>, std::function<std::shared_ptr<const void>()>,
        std::function<size_t()>, bool read, Priority);
    Handle<Bytes> requestBytesImpl(const std::string&, Priority, bool transient);
    void yieldWhileWaiting();
    void scheduleFinalizer(Dependency, std::weak_ptr<void>, std::function<void()>);
};
}
