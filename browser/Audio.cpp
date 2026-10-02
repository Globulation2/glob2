// SPDX-License-Identifier: GPL-3.0-or-later
// WebAudio requests PCM on the UI thread. Mix on the owning application thread;
// the UI consumes the previous block and supplies silence while work is pending.
#include <SDL3/SDL.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

extern "C" SDL_AudioStream* __real_SDL_OpenAudioDeviceStream(SDL_AudioDeviceID, const SDL_AudioSpec*, SDL_AudioStreamCallback, void*);
extern "C" void __real_SDL_DestroyAudioStream(SDL_AudioStream*);
extern "C" bool __real_SDL_PutAudioStreamData(SDL_AudioStream*, const void*, int);
extern "C" bool __real_SDL_LockAudioStream(SDL_AudioStream*);
extern "C" bool __real_SDL_UnlockAudioStream(SDL_AudioStream*);
extern "C" bool __real_SDL_PauseAudioDevice(SDL_AudioDeviceID);
extern "C" bool __real_SDL_ResumeAudioDevice(SDL_AudioDeviceID);
namespace {
auto* queue = em_proxying_queue_create();
struct Audio : std::enable_shared_from_this<Audio> {
    SDL_AudioSpec spec{};
    SDL_AudioStream* stream = nullptr;
    SDL_AudioStreamCallback callback = nullptr;
    void* userdata = nullptr;
    SDL_AudioDeviceID device = 0;
    pthread_t owner{};
    std::atomic<bool> enabled{true}, paused{true}, pending{false};
    std::mutex mutex;
    std::vector<Uint8> ready;
};
thread_local std::shared_ptr<Audio> audio;
// SDL3 mixer callbacks feed the stream themselves. Capture their writes while
// producing on the owner, then submit the completed PCM block on the UI thread.
thread_local std::vector<Uint8>* producing = nullptr;
struct Mix { std::shared_ptr<Audio> audio; int additional, total; };
void produce(void* opaque) {
    std::unique_ptr<Mix> request(static_cast<Mix*>(opaque));
    auto& device = *request->audio;
    if (device.enabled && !device.paused) {
        std::vector<Uint8> bytes;
        producing = &bytes;
        device.callback(device.userdata, device.stream, request->additional, request->total);
        producing = nullptr;
        std::lock_guard lock(device.mutex);
        device.ready = std::move(bytes);
    }
    device.pending = false;
}
void SDLCALL consume(void* opaque, SDL_AudioStream* stream, int additional, int total) {
    auto& device = *static_cast<Audio*>(opaque);
    if (!device.enabled || additional <= 0) return;
    std::vector<Uint8> bytes;
    {
        // Never wait for application state or its mixer on the browser UI.
        std::unique_lock lock(device.mutex, std::try_to_lock);
        if (lock.owns_lock()) bytes.swap(device.ready);
    }
    if (bytes.empty()) {
        const int frameBytes = SDL_AUDIO_BYTESIZE(device.spec.format) * device.spec.channels;
        const int size = additional + (frameBytes - additional % frameBytes) % frameBytes;
        bytes.assign(size, device.spec.format == SDL_AUDIO_U8 ? 0x80 : 0);
    }
    __real_SDL_PutAudioStreamData(stream, bytes.data(), int(bytes.size()));
    if (device.enabled && !device.paused && !device.pending.exchange(true)) {
        auto* request = new Mix{device.shared_from_this(), additional, total};
        if (!emscripten_proxy_async(queue, device.owner, produce, request)) {
            delete request;
            device.pending = false;
        }
    }
}
struct Open { Audio* audio; SDL_AudioDeviceID device; SDL_AudioStream* result = nullptr; };
void open(void* opaque) {
    auto& request = *static_cast<Open*>(opaque);
    request.result = __real_SDL_OpenAudioDeviceStream(request.device, &request.audio->spec, consume, request.audio);
}
void close(void* opaque) { __real_SDL_DestroyAudioStream(static_cast<SDL_AudioStream*>(opaque)); }
}
extern "C" SDL_AudioStream* __wrap_SDL_OpenAudioDeviceStream(SDL_AudioDeviceID id, const SDL_AudioSpec* spec,
                                                            SDL_AudioStreamCallback callback, void* userdata) {
    if (!spec || !callback || spec->channels <= 0 || SDL_AUDIO_BYTESIZE(spec->format) == 0 || audio) {
        SDL_SetError("Invalid or duplicate browser mixer stream");
        return nullptr;
    }
    auto device = std::make_shared<Audio>();
    device->spec = *spec;
    device->callback = callback;
    device->userdata = userdata;
    device->owner = pthread_self();
    Open request{device.get(), id};
    if (!emscripten_proxy_sync(queue, emscripten_main_runtime_thread_id(), open, &request)) {
        SDL_SetError("Unable to open browser audio on the UI thread");
        return nullptr;
    }
    if (request.result) {
        device->stream = request.result;
        device->device = SDL_GetAudioStreamDevice(request.result);
        audio = std::move(device);
    }
    return request.result;
}
extern "C" void __wrap_SDL_DestroyAudioStream(SDL_AudioStream* stream) {
    if (!audio || audio->stream != stream) { __real_SDL_DestroyAudioStream(stream); return; }
    audio->enabled = false;
    emscripten_proxy_sync(queue, emscripten_main_runtime_thread_id(), close, stream);
    audio.reset(); // queued mixes retain the disabled state, never callback-owned mixer data
}
extern "C" bool __wrap_SDL_PutAudioStreamData(SDL_AudioStream* stream, const void* data, int size) {
    if (!producing || !audio || audio->stream != stream)
        return __real_SDL_PutAudioStreamData(stream, data, size);
    if (size < 0 || (!data && size)) return SDL_SetError("Invalid browser audio data");
    if (size) {
        const auto* bytes = static_cast<const Uint8*>(data);
        producing->insert(producing->end(), bytes, bytes + size);
    }
    return true;
}
// Mixer writes and callbacks are serialized on their owner. Other streams retain
// SDL synchronization; the UI callback touches only the completed PCM buffer.
extern "C" bool __wrap_SDL_LockAudioStream(SDL_AudioStream* stream) {
    return audio && audio->stream == stream ? true : __real_SDL_LockAudioStream(stream);
}
extern "C" bool __wrap_SDL_UnlockAudioStream(SDL_AudioStream* stream) {
    return audio && audio->stream == stream ? true : __real_SDL_UnlockAudioStream(stream);
}
extern "C" bool __wrap_SDL_PauseAudioDevice(SDL_AudioDeviceID id) {
    if (audio && audio->device == id) audio->paused = true;
    return __real_SDL_PauseAudioDevice(id);
}
extern "C" bool __wrap_SDL_ResumeAudioDevice(SDL_AudioDeviceID id) {
    const bool result = __real_SDL_ResumeAudioDevice(id);
    if (result && audio && audio->device == id) audio->paused = false;
    return result;
}
