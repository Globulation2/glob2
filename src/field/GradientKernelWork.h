// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <array>
#include <memory>
// Optional exact CPU-kernel work counters. Each bound destination is private to
// one search/job; the owner reads job results only after its normal join.
struct GradientKernelWork {
    struct Totals {
        std::atomic<std::uint64_t> queued{0},expanded{0},builds{0},initializedCells{0};
    };
    struct Sink { std::array<Totals,7*32*9> fields; };
    static unsigned key(unsigned family,unsigned team,unsigned movement){return (family*32+team)*9+movement;}
    std::uint64_t queued=0,expanded=0;
    static inline std::atomic<bool> enabled{false};
    static inline thread_local GradientKernelWork* active=nullptr;
    struct Scope {
        GradientKernelWork* previous;
        GradientKernelWork& work;
        std::shared_ptr<Sink> sink;
        unsigned index;
        std::uint64_t queued,expanded;
        explicit Scope(GradientKernelWork& value,std::shared_ptr<Sink> destination={},unsigned key=0):
            previous(active),work(value),sink(std::move(destination)),index(key),queued(value.queued),expanded(value.expanded) {
            active=enabled.load(std::memory_order_relaxed)?&work:nullptr;
        }
        ~Scope(){
            if(sink){sink->fields[index].queued.fetch_add(work.queued-queued,std::memory_order_relaxed);sink->fields[index].expanded.fetch_add(work.expanded-expanded,std::memory_order_relaxed);}
            active=previous;
        }
    };
    static void entries(std::uint64_t n){if(active)active->queued+=n;}
    static void expansion(){if(active)++active->expanded;}
};
