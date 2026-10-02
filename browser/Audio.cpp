// SPDX-License-Identifier: GPL-3.0-or-later
// The pinned SDL WebAudio driver assumes its device and mixer share the UI
// thread. Keep device creation there, but run the engine mixer on its owning
// application thread. The UI consumes the previous block without waiting.
#include <SDL.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

extern "C" int __real_SDL_OpenAudio(SDL_AudioSpec*, SDL_AudioSpec*);
extern "C" void __real_SDL_CloseAudio();
extern "C" void __real_SDL_PauseAudio(int);
namespace {
auto* queue = em_proxying_queue_create();
struct Audio : std::enable_shared_from_this<Audio> {
    SDL_AudioSpec spec{};
    pthread_t owner{};
    std::atomic<bool> enabled{true}, paused{true}, pending{false};
    std::mutex mutex;
    std::vector<Uint8> ready;
};
thread_local std::shared_ptr<Audio> audio;
struct Mix { std::shared_ptr<Audio> audio; int size; };
void produce(void* opaque) {
    std::unique_ptr<Mix> request(static_cast<Mix*>(opaque));
    auto& device = *request->audio;
    if (device.enabled && !device.paused) {
        std::vector<Uint8> bytes(request->size);
        device.spec.callback(device.spec.userdata, bytes.data(), request->size);
        std::lock_guard lock(device.mutex);
        device.ready = std::move(bytes);
    }
    device.pending = false;
}
void consume(void* opaque, Uint8* bytes, int size) {
    auto& device = *static_cast<Audio*>(opaque);
    SDL_memset(bytes, device.spec.silence, size);
    {
        // The browser UI never waits for the application or a mixer lock.
        std::unique_lock lock(device.mutex, std::try_to_lock);
        if (lock.owns_lock() && device.ready.size() == size) {
            SDL_memcpy(bytes, device.ready.data(), size);
            device.ready.clear();
        }
    }
    if (device.enabled && !device.pending.exchange(true)) {
        auto* request = new Mix{device.shared_from_this(), size};
        if (!emscripten_proxy_async(queue, device.owner, produce, request)) {
            delete request;
            device.pending = false;
        }
    }
}
struct Open { SDL_AudioSpec desired; SDL_AudioSpec* obtained; int result; };
void open(void* opaque) {
    auto& request = *static_cast<Open*>(opaque);
    request.result = __real_SDL_OpenAudio(&request.desired, request.obtained);
}
void close(void*) { __real_SDL_CloseAudio(); }
}
extern "C" int __wrap_SDL_OpenAudio(SDL_AudioSpec* desired, SDL_AudioSpec* obtained) {
    auto device = std::make_shared<Audio>();
    device->spec = *desired;
    device->spec.silence = desired->format == AUDIO_U8 ? 0x80 : 0;
    device->owner = pthread_self();
    Open request{*desired, obtained, -1};
    request.desired.callback = consume;
    request.desired.userdata = device.get();
    if (!emscripten_proxy_sync(queue, emscripten_main_runtime_thread_id(), open, &request))
        return SDL_SetError("Unable to open browser audio on the UI thread");
    if (request.result == 0) audio = std::move(device);
    return request.result;
}
extern "C" void __wrap_SDL_CloseAudio() {
    if (audio) audio->enabled = false;
    emscripten_proxy_sync(queue, emscripten_main_runtime_thread_id(), close, nullptr);
    audio.reset(); // queued mixes retain the disabled device until they finish
}
extern "C" void __wrap_SDL_PauseAudio(int paused) {
    if (audio) audio->paused = paused != 0;
    // Pinned SDL pause only updates the shared device's atomic paused flag;
    // unlike open/close, it does not touch WebAudio or another JS realm.
    __real_SDL_PauseAudio(paused);
}
// Engine mutations and mixer callbacks are serialized on the application thread.
extern "C" void __wrap_SDL_LockAudio() {}
extern "C" void __wrap_SDL_UnlockAudio() {}
