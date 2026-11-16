// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <atomic>
#include <thread>
#include <utility>

// Platform capability lives here; engine pools retain standard C++ ownership.
namespace GAGCore::ThreadSupport {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
inline constexpr bool available = false;
#else
inline constexpr bool available = true;
#endif

// How the operating system should schedule a thread against the others.
// Interactive: the core loop (the graphics and simulation owner threads) and
// the audio mixer. Worker: every other thread, including pools the core loop
// waits on (simulation barriers, presentation batches, asset streaming), so
// the core loop runs first under contention. Background: work nothing waits
// on, such as composition collected on a later frame and file writes.
enum class ThreadRole { Interactive, Worker, Background };

// Asks the operating system to schedule the calling thread for its role:
// QoS classes on Apple platforms, thread priorities on Windows and per-thread
// nice values on Linux and Android. Best effort and never fatal: Linux raises
// priority only within RLIMIT_NICE, so there Interactive usually stays at nice
// 0, still ahead of Worker (+2) and Background (+5). GLOB2_THREAD_PRIORITY=0
// turns roles off. Returns whether the request took effect.
bool setCurrentThreadRole(ThreadRole role) noexcept;

inline std::atomic<unsigned> activeWorkers{0};
// A launched thread takes its role first, whatever role its launcher holds.
inline std::thread launch(std::function<void()> function, ThreadRole role = ThreadRole::Worker) {
    return std::thread([function = std::move(function), role] {
        struct Count {
            Count() { ++activeWorkers; }
            ~Count() { --activeWorkers; }
        } count;
        setCurrentThreadRole(role);
        function();
    });
}
}
