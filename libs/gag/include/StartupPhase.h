// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>

// The last startup step the application thread entered. Diagnostics read it
// from another thread: the browser's page reports it when the first menu does
// not appear, which tells a stuck step from a slow one.
namespace GAGCore::StartupPhase
{
inline std::atomic<const char *> currentPhase{"launch"};
//! Records a step; name must be a string literal (it is kept, not copied).
inline void mark(const char *name) noexcept { currentPhase.store(name, std::memory_order_release); }
inline const char *current() noexcept { return currentPhase.load(std::memory_order_acquire); }
}
