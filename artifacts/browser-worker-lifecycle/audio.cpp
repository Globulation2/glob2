#include <cassert>
#include <emscripten.h>
#include <emscripten/proxying.h>
#include <pthread.h>
#include "../../browser/Audio.cpp"
SDL_AudioSpec callbackSpec;
extern "C" int __real_SDL_OpenAudio(SDL_AudioSpec* desired, SDL_AudioSpec*) { callbackSpec = *desired; return 0; }
extern "C" void __real_SDL_CloseAudio() { callbackSpec.callback = nullptr; }
extern "C" void __real_SDL_PauseAudio(int) {}
int mixes = 0;
void mix(void*, Uint8*, int) { ++mixes; }
void enqueue(void*) { Uint8 buffer[16]; callbackSpec.callback(callbackSpec.userdata, buffer, sizeof(buffer)); }
void* test(void*) {
    SDL_AudioSpec desired{};
    desired.format = AUDIO_S16SYS;
    desired.callback = mix;
    assert(__wrap_SDL_OpenAudio(&desired, nullptr) == 0);
    __wrap_SDL_PauseAudio(0);
    std::weak_ptr<Audio> device = audio;
    assert(emscripten_proxy_sync(queue, emscripten_main_runtime_thread_id(), enqueue, nullptr));
    assert(audio->pending); // the application has not yielded to execute produce
    __wrap_SDL_CloseAudio();
    assert(device.expired()); // Mix ownership must be reclaimed before pthread exit
    assert(mixes == 0); // disabled requests never call the destroyed mixer
    MAIN_THREAD_EM_ASM({ window.result = 0; console.log('PASS pending audio reclaimed before pthread exit'); });
    return nullptr;
}
int main() {
    pthread_t owner;
    assert(pthread_create(&owner, nullptr, test, nullptr) == 0);
    emscripten_exit_with_live_runtime();
}
