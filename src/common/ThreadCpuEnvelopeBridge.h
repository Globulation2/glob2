// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ThreadCpuEnvelope.h"
#include <cstdlib>
#include <cstring>
#include <memory>

namespace glob2
{
// Installed by the native owned-gradient service. A standalone executor keeps
// its header-only contract and never needs to link the optional backend.
inline bool (*cpuEnvelopeRoleRegistrar)(CpuThreadRole,bool) noexcept=nullptr;
inline std::shared_ptr<CpuClockRegistry> (*cpuEnvelopeRegistryLookup)() noexcept=nullptr;
inline bool cpuEnvelopeRequested() noexcept {
    const auto* value=std::getenv("GLOB2_GRADIENT_CPU_ENVELOPE");return value && std::strcmp(value,"1")==0;
}
// True means this thread made its one registration attempt, even when that
// native clock is unavailable. False permits O(1) retry when the background
// registry has not yet been initialized. Every role lease retires on TLS exit.
inline bool registerCpuEnvelopeThread(CpuThreadRole role,bool backgroundInitialize=false) noexcept {
    return cpuEnvelopeRoleRegistrar && cpuEnvelopeRoleRegistrar(role,backgroundInitialize);
}
inline std::shared_ptr<CpuClockRegistry> cpuEnvelopeRegistry() noexcept {
    return cpuEnvelopeRegistryLookup ? cpuEnvelopeRegistryLookup() : nullptr;
}
}
