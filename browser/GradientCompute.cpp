// SPDX-License-Identifier: GPL-3.0-or-later
// Browser platform primitives for the independently owned gradient device.
// The shared adapter owns tasks, budgets and retirement; this file owns the
// browser execution constraint and bounded futex wait. No owner callback waits.
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
#include <emscripten/threading.h>

extern "C" int glob2_gradient_worker_context()
{
    return !emscripten_is_main_browser_thread();
}

extern "C" void glob2_gradient_worker_wait(int *completion)
{
    // Admission checked worker context before submitting asynchronous work.
    // A finite wait lets the adapter observe cancellation and retain tasks
    // without waiting for callbacks that the browser owner must still service.
    emscripten_futex_wait(completion, 0, 1);
}
#endif
