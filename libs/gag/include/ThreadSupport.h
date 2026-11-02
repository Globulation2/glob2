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
inline std::atomic<unsigned> activeWorkers{0};
inline std::thread launch(std::function<void()> function) {
    return std::thread([function = std::move(function)] {
        struct Count {
            Count() { ++activeWorkers; }
            ~Count() { --activeWorkers; }
        } count;
        function();
    });
}
}
