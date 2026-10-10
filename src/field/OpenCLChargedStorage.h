// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OpenCLGradient.h"
#include <memory>
#include <utility>
#include <new>
#include <array>

namespace gradient_kernel
{
class GradientCpuCapture;
class GradientCpuCaptureGroup;
class CaptureProbeReplay;
class CaptureBatchProbeReplay;
class PrechargedProbeCostOwner;

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
    friend class PrechargedProbeCostOwner;
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

// Trusted adapters alone can wrap an immutable captured graph whose complete
// storage already has real credits. Native planes share those credits rather
// than reserving the graph again. This wrapper owns no mutable execution state.
class PrechargedProbeCostOwner
{
    friend class CaptureProbeReplay;
    friend class CaptureBatchProbeReplay;
    std::shared_ptr<PrechargedProbeStorage> metadataCredit; // Destroyed last.
    std::array<std::shared_ptr<PrechargedProbeStorage>,2> payloadCredits;
    std::shared_ptr<const void> payload;
    PrechargedProbeCostOwner()=default;
    static std::shared_ptr<const PrechargedProbeCostOwner> create(std::shared_ptr<const void> owner,
        std::shared_ptr<PrechargedProbeStorage> captured,std::shared_ptr<PrechargedProbeStorage> replay) {
        if(!owner || !captured || !replay || !captured->bytes() || !replay->bytes())throw std::bad_alloc{};
        struct Reservation {std::size_t bytes=0;~Reservation(){if(bytes)releaseOpenCLProbeBytes(bytes);}} reserved;
        reserved.bytes=metadataBytes();
        if(!reserveOpenCLProbeBytes(reserved.bytes)){reserved.bytes=0;throw std::bad_alloc{};}
        auto credit=PrechargedProbeStorage::adopt(reserved.bytes);
        auto allocation=std::unique_ptr<PrechargedProbeCostOwner>(new PrechargedProbeCostOwner);
        allocation->metadataCredit=credit;
        allocation->payloadCredits={std::move(captured),std::move(replay)};
        allocation->payload=std::move(owner);
        // A generic escaped weak wrapper must retain its control metadata too.
        return std::shared_ptr<const PrechargedProbeCostOwner>(allocation.release(),
            [credit](const auto* value){delete value;});
    }
public:
    PrechargedProbeCostOwner(const PrechargedProbeCostOwner&)=delete;
    PrechargedProbeCostOwner& operator=(const PrechargedProbeCostOwner&)=delete;
    ~PrechargedProbeCostOwner()=default;
    static constexpr std::size_t metadataBytes() noexcept {
        return sizeof(PrechargedProbeCostOwner)+256+PrechargedProbeStorage::metadataBytes();
    }
    static bool matches(const std::shared_ptr<const void>& owner,
                        const std::shared_ptr<const PrechargedProbeCostOwner>& trusted) noexcept {
        return trusted && owner && owner.get()==trusted.get() &&
            !owner.owner_before(trusted) && !trusted.owner_before(owner);
    }
};
}
