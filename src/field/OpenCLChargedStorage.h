// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OpenCLGradient.h"
#include <memory>
#include <utility>
#include <new>

namespace gradient_kernel
{
class GradientCpuCapture;
class GradientCpuCaptureGroup;
class CaptureProbeReplay;
class CaptureBatchProbeReplay;

// Trusted capture holders transfer existing real host/probe reservations here.
// This object owns no payload: holders declare it before the payload so its
// reservation survives destruction, and cost-owner wrappers share the credit.
// Generic callers cannot manufacture a precharged graph from a byte claim.
class PrechargedProbeStorage
{
    friend class GradientCpuCapture;
    friend class GradientCpuCaptureGroup;
    friend class CaptureProbeReplay;
    friend class CaptureBatchProbeReplay;
    struct ControlCredit {
        std::size_t bytes=0;
        ~ControlCredit(){if(bytes)releaseOpenCLProbeBytes(bytes);}
    };
    std::shared_ptr<ControlCredit> control;
    std::size_t payloadBytes=0;
    PrechargedProbeStorage()=default;
    // A separate deleter-held credit survives an escaped weak credit pointer.
    // Metadata admission precedes both allocations; failures leave the caller's
    // original reservation untouched. No new budget is silently reserved here.
    static std::shared_ptr<PrechargedProbeStorage> adopt(std::size_t& reservedBytes) {
        if(reservedBytes<metadataBytes())throw std::bad_alloc{};
        auto retained=std::make_shared<ControlCredit>();
        auto* allocation=new PrechargedProbeStorage;
        std::shared_ptr<PrechargedProbeStorage> result(allocation,[retained](auto* value){delete value;});
        result->control=retained;
        constexpr auto weakBytes=sizeof(ControlCredit)+512;
        result->payloadBytes=reservedBytes-weakBytes;
        retained->bytes=weakBytes;
        reservedBytes=0;
        return result;
    }
public:
    PrechargedProbeStorage(const PrechargedProbeStorage&)=delete;
    PrechargedProbeStorage& operator=(const PrechargedProbeStorage&)=delete;
    ~PrechargedProbeStorage(){if(payloadBytes)releaseOpenCLProbeBytes(payloadBytes);}
    static constexpr std::size_t metadataBytes() noexcept {
        return sizeof(PrechargedProbeStorage)+sizeof(ControlCredit)+512;
    }
    std::size_t bytes() const noexcept {return payloadBytes+(control?control->bytes:0);}
};
}
