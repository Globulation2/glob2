// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Explicit development import scope for constructed stress fixtures. Ordinary
// map contracts, lobby/network readers and generators retain their own bounds.
// The synchronous Headless saved-game import and fixture harness alone opt in;
// MapIO never reads a process environment flag.
class ScopedBenchmarkMapImport
{
    inline static thread_local int activeMaximumExponent = 0;
    int previous;
public:
    static constexpr int MaximumExponent = 10;
    static constexpr int StressMaximumExponent = 11;
    explicit ScopedBenchmarkMapImport(bool enabled, int maximumExponent=MaximumExponent)
        : previous(activeMaximumExponent) {
        activeMaximumExponent=enabled && (maximumExponent==MaximumExponent || maximumExponent==StressMaximumExponent)
            ? maximumExponent : 0;
    }
    ~ScopedBenchmarkMapImport() { activeMaximumExponent=previous; }
    ScopedBenchmarkMapImport(const ScopedBenchmarkMapImport&) = delete;
    ScopedBenchmarkMapImport& operator=(const ScopedBenchmarkMapImport&) = delete;
    static bool allows(int width, int height, int minimum) {
        return activeMaximumExponent && width>=minimum && height>=minimum
            && width<=activeMaximumExponent && height<=activeMaximumExponent;
    }
};
