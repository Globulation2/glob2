// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OpenCLGradient.h"
#include "Grid.h"
#include <atomic>
#include <array>

namespace gradient_kernel
{
class GradientCpuCaptureGroup;
class CaptureBatchProbeReplay;
class CaptureProbeAccounting;
class PrechargedProbeStorage;
class OpenCLProbeBudgetLease;
struct OpenCLProbeBatchStorageTests;

// A payload-free promise backed by a real paired host/probe reservation and a
// real device reservation. Trusted group capture admits this BEFORE its first
// seed clone. Claims only subdivide the promise: neither claims nor returns
// change the process-wide budget, so there is no release/reserve race.
class OpenCLProbeBatchStorage
{
    friend class GradientCpuCaptureGroup;
    friend class CaptureBatchProbeReplay;
    friend class CaptureProbeAccounting;
    friend class PrechargedProbeStorage;
    friend class OpenCLProbeBudgetLease;
    friend class OpenCLProbe;
    friend struct OpenCLProbeBatchStorageTests;
public:
    struct Estimate {
        std::size_t hostBytes=0,deviceBytes=0;
        unsigned planOrdinal=0;
        explicit operator bool() const noexcept {return hostBytes && deviceBytes;}
    };
    // This is a conservative fixed graph allowance, with compile-time bounds in
    // the batch adapter/accounting integration. Array and native fixed sizes are
    // derived separately from the actual implementation. Extra injected owner
    // graphs must be included explicitly, never inferred from a borrowed pointer.
    static constexpr std::size_t AdapterAllowance=64*1024;
    static Estimate estimate(field::Grid,unsigned count,Plan,
        std::size_t retainedDiagnosticBytes=0) noexcept;
    OpenCLProbeBatchStorage(const OpenCLProbeBatchStorage&)=delete;
    OpenCLProbeBatchStorage& operator=(const OpenCLProbeBatchStorage&)=delete;
    ~OpenCLProbeBatchStorage() {
        // Fixed holder/control admission is not available to payload claims.
        // All payload claims retain this holder and must be returned first.
        if(claimed[0].load()!=metadataBytes() || claimed[1].load()!=metadataBytes() || claimed[2].load())std::terminate();
    }
    static constexpr std::size_t metadataBytes() noexcept {return sizeof(OpenCLProbeBatchStorage)+512;}
    std::size_t hostBytes() const noexcept {return limits[0];}
    std::size_t deviceBytes() const noexcept {return limits[2];}
    std::size_t claimedHostBytes() const noexcept {return claimed[0].load(std::memory_order_relaxed);}
    std::size_t claimedProbeBytes() const noexcept {return claimed[1].load(std::memory_order_relaxed);}
    std::size_t claimedDeviceBytes() const noexcept {return claimed[2].load(std::memory_order_relaxed);}
private:
    // Declared first so real credits survive all claim metadata. The separate
    // control deleter also retains this credit through escaped weak holders.
    std::shared_ptr<const void> realCredit;
    std::array<std::size_t,3> limits{};
    std::array<std::atomic<std::size_t>,3> claimed{};
    explicit OpenCLProbeBatchStorage(Estimate value):limits{value.hostBytes,value.hostBytes,value.deviceBytes} {
        claimed[0]=metadataBytes();claimed[1]=metadataBytes();
    }
    // Only Group can transfer its real future paired admission. Device admission
    // is performed on the initialized Device before either holder allocation;
    // any failure leaves the caller's paired reservation intact.
    static std::shared_ptr<OpenCLProbeBatchStorage> adopt(std::size_t& futurePairedBytes,Estimate) noexcept;
    bool reserve(unsigned kind,std::size_t bytes) noexcept {
        if(kind>=limits.size())return false;
        auto previous=claimed[kind].load(std::memory_order_relaxed);
        do {if(bytes>limits[kind] || previous>limits[kind]-bytes)return false;}
        while(!claimed[kind].compare_exchange_weak(previous,previous+bytes,std::memory_order_relaxed));
        return true;
    }
    void release(unsigned kind,std::size_t bytes) noexcept {
        if(kind>=limits.size())std::terminate();
        auto previous=claimed[kind].load(std::memory_order_relaxed);
        do {if(bytes>previous)std::terminate();}
        while(!claimed[kind].compare_exchange_weak(previous,previous-bytes,std::memory_order_relaxed));
    }
    bool reservePaired(std::size_t bytes) noexcept {
        if(!reserve(0,bytes))return false;
        if(reserve(1,bytes))return true;
        release(0,bytes);return false;
    }
    void releasePaired(std::size_t bytes) noexcept {release(1,bytes);release(0,bytes);}
};
}
