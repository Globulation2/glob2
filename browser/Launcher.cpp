// SPDX-License-Identifier: GPL-3.0-or-later
#include "Runtime.h"
#include <ThreadSupport.h>
#include <emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/proxying.h>
#include <memory>
#include <string>
#include <vector>
#include <cstdio>
#include <pthread.h>

int glob2ApplicationMain(int argc, char **argv);
namespace Glob2Browser {
thread_local bool hosted = false;
#ifdef __EMSCRIPTEN_PTHREADS__
pthread_t applicationThread;
#endif
void contextAction(void *opaque) {
    EM_ASM({
        if (typeof GLctx === 'undefined' || !GLctx) return;
        Module.contextLoss ||= GLctx.getExtension('WEBGL_lose_context');
        if ($0) Module.contextLoss?.restoreContext();
        else Module.contextLoss?.loseContext();
    }, reinterpret_cast<intptr_t>(opaque));
}
void completed(int result) {
    MAIN_THREAD_EM_ASM({ Module.glob2Complete?.($0); }, result);
}
void releaseApplicationThread() {
#ifdef __EMSCRIPTEN_PTHREADS__
    pthread_exit(nullptr);
#endif
}
}
namespace {
struct Arguments {
    std::vector<std::string> strings;
    std::vector<char*> pointers;
    Arguments(int argc, char **argv) {
        for (int i = 0; i < argc; ++i) strings.emplace_back(argv[i]);
        for (auto &value : strings) pointers.push_back(value.data());
        pointers.push_back(nullptr);
    }
};
void *application(void *opaque) {
    std::unique_ptr<Arguments> args(static_cast<Arguments*>(opaque));
#ifdef __EMSCRIPTEN_PTHREADS__
    Glob2Browser::applicationThread = pthread_self();
#endif
    MAIN_THREAD_EM_ASM({ Module.glob2ApplicationStarted = true; });
    int result = 1;
    try { result = glob2ApplicationMain(int(args->strings.size()), args->pointers.data()); }
    catch (const std::exception &error) { std::fprintf(stderr, "Browser application: %s\n", error.what()); }
    args.reset();
    MAIN_THREAD_EM_ASM({ Module.onApplicationStarted?.(); });
    if (Glob2Browser::hosted) emscripten_exit_with_live_runtime();
    Glob2Browser::completed(result);
    return nullptr;
}
}
extern "C" EMSCRIPTEN_KEEPALIVE unsigned glob2_worker_count() {
    return GAGCore::ThreadSupport::activeWorkers.load();
}
extern "C" EMSCRIPTEN_KEEPALIVE void glob2_context_action(int restore) {
#ifdef __EMSCRIPTEN_PTHREADS__
    static auto *queue = em_proxying_queue_create();
    emscripten_proxy_async(queue, Glob2Browser::applicationThread,
        Glob2Browser::contextAction, reinterpret_cast<void*>(intptr_t(restore)));
#else
    Glob2Browser::contextAction(reinterpret_cast<void*>(intptr_t(restore)));
#endif
}
int main(int argc, char **argv) {
    auto args = std::make_unique<Arguments>(argc, argv);
#ifdef __EMSCRIPTEN_PTHREADS__
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attributes, 8 * 1024 * 1024);
    if (EM_ASM_INT({ return Module.renderer === 'webgl2'; }))
        emscripten_pthread_attr_settransferredcanvases(&attributes, "#canvas");
    pthread_t thread;
    const int error = pthread_create(&thread, &attributes, application, args.get());
    pthread_attr_destroy(&attributes);
    if (error) {
        EM_ASM({ Module.glob2LaunchFailed?.($0); }, error);
        return error;
    }
    args.release();
#else
    application(args.release());
#endif
    return 0;
}
