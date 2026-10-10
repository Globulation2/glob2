// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Explicit development import scope for constructed 1024 fixtures. Ordinary
// map contracts, lobby/network readers and generators retain their own bounds.
// The synchronous Headless saved-game import and fixture harness alone opt in;
// MapIO never reads a process environment flag.
class ScopedBenchmarkMapImport
{
    inline static thread_local bool active = false;
    bool previous;
public:
    static constexpr int MaximumExponent = 10;
    explicit ScopedBenchmarkMapImport(bool enabled) : previous(active) { active=enabled; }
    ~ScopedBenchmarkMapImport() { active=previous; }
    ScopedBenchmarkMapImport(const ScopedBenchmarkMapImport&) = delete;
    ScopedBenchmarkMapImport& operator=(const ScopedBenchmarkMapImport&) = delete;
    static bool allows(int width, int height, int minimum) {
        return active && width>=minimum && height>=minimum
            && width<=MaximumExponent && height<=MaximumExponent;
    }
};
