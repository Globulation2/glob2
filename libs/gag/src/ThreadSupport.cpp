// SPDX-License-Identifier: GPL-3.0-or-later
#include "ThreadSupport.h"
#include <cstdlib>
#include <cstring>

#if defined(__EMSCRIPTEN__)
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace GAGCore::ThreadSupport {
namespace {
bool enabled() noexcept
{
    const char *value = std::getenv("GLOB2_THREAD_PRIORITY");
    return !value || (std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0);
}
}

bool setCurrentThreadRole(ThreadRole role) noexcept
{
    if (!enabled())
        return false;
#if defined(__EMSCRIPTEN__)
    // Browsers schedule workers themselves.
    (void)role;
    return false;
#elif defined(_WIN32)
    const int priority = role == ThreadRole::Interactive ? THREAD_PRIORITY_ABOVE_NORMAL
                         : role == ThreadRole::Background ? THREAD_PRIORITY_BELOW_NORMAL
                                                          : THREAD_PRIORITY_NORMAL;
    return SetThreadPriority(GetCurrentThread(), priority) != 0;
#elif defined(__APPLE__)
    // QoS also chooses performance or efficiency cores. Workers sit one class
    // below the core loop and are not biased toward efficiency cores.
    const qos_class_t qos = role == ThreadRole::Interactive ? QOS_CLASS_USER_INTERACTIVE
                            : role == ThreadRole::Background ? QOS_CLASS_UTILITY
                                                             : QOS_CLASS_USER_INITIATED;
    return pthread_set_qos_class_self_np(qos, 0) == 0;
#elif defined(__linux__)
    // Linux nice values belong to the thread id. Raising one is always allowed;
    // lowering it needs CAP_SYS_NICE or RLIMIT_NICE (Android grants apps the
    // display priority, -4), so Interactive, and Worker after Background, may
    // be refused. Workers at +2 then still trail a core loop left at 0.
    const int nice = role == ThreadRole::Interactive ? -4 : role == ThreadRole::Background ? 5 : 2;
    return setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), nice) == 0;
#else
    (void)role;
    return false;
#endif
}
}
