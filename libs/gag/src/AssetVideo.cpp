// SPDX-License-Identifier: GPL-3.0-or-later
#include <AssetLoader.h>
#include "AssetVideoDecoder.h"
#include <stdexcept>
#ifdef __EMSCRIPTEN__
#include <ApplicationHost.h>
#endif

namespace GAGCore {
#if defined(GLOB2_H264_ASSETS) || defined(__EMSCRIPTEN__)
namespace {
std::shared_ptr<const AssetImage> makeVideoImage(unsigned width, unsigned height,
        const uint8_t *rgba, int pitch) {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurface(int(width), int(height), SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
    if (!surface || !SDL_ConvertPixels(int(width), int(height), SDL_PIXELFORMAT_RGBA32,
            rgba, pitch, surface->format, surface->pixels, surface->pitch))
        throw std::runtime_error(SDL_GetError());
    auto image = std::make_shared<AssetImage>(surface.get());
    surface.release();
    return image;
}
}
#endif

#ifdef __EMSCRIPTEN__
void AssetLoader::pollBrowserVideo() {
    if (browserVideoJobs.empty()) return;
    auto &job = browserVideoJobs.front();
    const auto finish = [&](State state, std::shared_ptr<const Bytes> result = {}) {
        if (state == State::Failed) job.decoded->error = "Browser asset video decode failed";
        job.decoded->value = std::move(result);
        job.decoded->state.store(state, std::memory_order_release);
        browserVideoJobs.pop_front();
    };
    if (!job.started) {
        if (!job.decoded->consumers) return finish(State::Cancelled);
        if (job.encoded.pending()) return;
        const auto bytes = job.encoded.get();
        if (!bytes || bytes->empty() ||
                !ApplicationHost::startAssetVideo(bytes->data(), bytes->size(), job.width, job.height, job.count))
            return finish(State::Failed);
        job.encoded = {};
        job.started = true;
        return;
    }
    const int status = ApplicationHost::assetVideoStatus();
    if (!status) return;
    const auto size = size_t(job.width) * job.height * job.count * 4;
    if (!job.decoded->consumers || status < 0 || size_t(status) != size) {
        ApplicationHost::takeAssetVideo(nullptr, 0);
        return finish(job.decoded->consumers ? State::Failed : State::Cancelled);
    }
    try {
        auto result = std::make_shared<Bytes>(size);
        ApplicationHost::takeAssetVideo(result->data(), size);
        finish(State::Ready, std::move(result));
    } catch (const std::bad_alloc&) {
        ApplicationHost::takeAssetVideo(nullptr, 0);
        finish(State::Failed);
    }
}
#endif

bool AssetLoader::videoFramesSupported() {
#ifdef GLOB2_H264_ASSETS
    return AssetVideoDecoder::supported();
#elif defined(__EMSCRIPTEN__)
    return true;
#else
    return false;
#endif
}

AssetLoader::Handle<AssetVideoFrames> AssetLoader::requestVideoFrames(const std::string& path,
        unsigned width, unsigned height, unsigned frameCount, Priority priority) {
    const auto key = "h264-asset:" + path + ':' + std::to_string(width) + 'x' +
        std::to_string(height) + ':' + std::to_string(frameCount);
    const auto failed = [&](const char *reason) {
        return request<AssetVideoFrames>(key, {}, [reason, path]() -> std::shared_ptr<const AssetVideoFrames> {
            throw std::runtime_error(std::string(reason) + ": " + path);
        }, 0, priority);
    };
    if (!path.ends_with(".h264") || !AssetVideoDecoder::validDimensions(width, height, frameCount))
        return failed("Invalid H.264 asset group description");
    if (!videoFramesSupported()) return failed("H.264 asset decoding is unavailable");
#ifdef GLOB2_H264_ASSETS
    if (auto result = findReady(key, typeid(AssetVideoFrames), priority))
        return Handle<AssetVideoFrames>(result);
    auto bytes = requestBytesImpl(path, priority, true, AssetVideoDecoder::MaxEncodedBytes);
    return requestEstimated<AssetVideoFrames>(key, {bytes.dependency()}, [bytes, width, height, frameCount] {
        auto result = std::make_shared<AssetVideoFrames>();
        result->frames.reserve(frameCount);
        AssetVideoDecoder::decode(*bytes.get(), width, height, frameCount,
            [&](unsigned, const uint8_t *rgba, int pitch) {
                result->frames.push_back(makeVideoImage(width, height, rgba, pitch));
            });
        return result;
    }, [bytes, width, height, frameCount] {
        // Output surfaces, codec reference surfaces, color conversion scratch,
        // and parser / packet copies. The read queue accounts for the original.
        return size_t(width + 64) * (height + 64) * 8 * (frameCount + 2) + bytes.get()->size() * 3;
    }, priority);
#elif defined(__EMSCRIPTEN__)
    if (priority == Priority::Required) return failed("Browser video requires asynchronous delivery");
    if (auto result = findReady(key, typeid(AssetVideoFrames), priority))
        return Handle<AssetVideoFrames>(result);
    auto decoded = std::make_shared<Result>();
    decoded->type = typeid(Bytes);
    ++decoded->consumers;
    const Handle<Bytes> pixels(decoded);
    const size_t frameBytes = size_t(width) * height * 4;
    auto frames = request<AssetVideoFrames>(key, {pixels.dependency()}, [pixels, width, height, frameCount, frameBytes] {
        const auto rgba = pixels.get();
        auto result = std::make_shared<AssetVideoFrames>();
        result->frames.reserve(frameCount);
        for (unsigned index = 0; index < frameCount; ++index)
            result->frames.push_back(makeVideoImage(width, height, rgba->data() + index * frameBytes, int(width * 4)));
        return result;
    }, frameBytes * frameCount * 2, priority);
    // A deduplicated request does not retain this new input.
    if (pixels.subscription.use_count() > 1)
        browserVideoJobs.push_back({requestBytesImpl(path, priority, true, AssetVideoDecoder::MaxEncodedBytes),
                                    std::move(decoded), width, height, frameCount});
    return frames;
#else
    return failed("H.264 asset decoding is unavailable");
#endif
}
}
