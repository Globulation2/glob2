// SPDX-License-Identifier: GPL-3.0-or-later
// SDK preloads belong to main, while GPU preparation must run in a pthread.
#include <emscripten.h>
#include <pthread.h>
#include <atomic>
namespace {
std::atomic<int> status{0}, localMetadata{0}, owner{0};
extern "C" int glob2_webgpu_prepare(const char *);
extern "C" int glob2_webgpu_poll();
void poll(void *) {
    const int ready=glob2_webgpu_poll();
    if (ready) {status.store(ready>0?2:-1);emscripten_cancel_main_loop();}
}
void *prepare(void *) {
    owner.store(EM_ASM_INT({return ENVIRONMENT_IS_PTHREAD?1:0;}));
    localMetadata.store(EM_ASM_INT({try{FS.readFile('/shaders/metadata.json');return 1;}catch(_){return 0;}}));
    status.store(1);
    const int ready=glob2_webgpu_prepare("/shaders/webgpu");
    if (ready)status.store(ready>0?2:-1);
    else emscripten_set_main_loop_arg(poll,nullptr,0,true);
    return nullptr;
}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int proof_thread_prepare(){pthread_t thread;return pthread_create(&thread,nullptr,prepare,nullptr)==0;}
EMSCRIPTEN_KEEPALIVE int proof_thread_status(){return status.load();}
EMSCRIPTEN_KEEPALIVE int proof_thread_local_metadata(){return localMetadata.load();}
EMSCRIPTEN_KEEPALIVE int proof_thread_owner(){return owner.load();}
}
