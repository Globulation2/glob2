// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OpenCLGradient.h"
#include "OpenCLProbeBatchStorage.h"
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
class CaptureProbeAccounting;
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
    friend class CaptureProbeAccounting;
    friend class PrechargedProbeCostOwner;
    friend struct OpenCLProbeBatchStorageTests;
    struct ControlCredit {
        std::shared_ptr<OpenCLProbeBatchStorage> future;
        std::size_t bytes=0;
        ~ControlCredit(){if(bytes){if(future)future->releasePaired(bytes);else releaseOpenCLProbeBytes(bytes);}}
    };
    std::shared_ptr<ControlCredit> control;
    std::shared_ptr<OpenCLProbeBatchStorage> future;
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
    // Batch Replay/Accounting draw their real pre-admitted future ledger. A
    // failed allocation returns only the local claim; the global promise stays
    // reserved by the group. No public byte claim can manufacture this credit.
    static std::shared_ptr<PrechargedProbeStorage> claim(std::shared_ptr<OpenCLProbeBatchStorage> backing,
        std::size_t count) {
        if(!backing || count<metadataBytes() || !backing->reservePaired(count))throw std::bad_alloc{};
        struct Return {std::shared_ptr<OpenCLProbeBatchStorage> owner;std::size_t bytes;
            ~Return(){if(bytes)owner->releasePaired(bytes);}} reserved{backing,count};
        auto retained=std::make_shared<ControlCredit>();
        auto allocation=std::unique_ptr<PrechargedProbeStorage>(new PrechargedProbeStorage);
        auto result=std::shared_ptr<PrechargedProbeStorage>(allocation.release(),[retained](auto* value){delete value;});
        result->control=retained;result->future=backing;retained->future=std::move(backing);
        constexpr auto weakBytes=sizeof(ControlCredit)+512;
        result->payloadBytes=count-weakBytes;retained->bytes=weakBytes;reserved.bytes=0;
        return result;
    }
public:
    PrechargedProbeStorage(const PrechargedProbeStorage&)=delete;
    PrechargedProbeStorage& operator=(const PrechargedProbeStorage&)=delete;
    ~PrechargedProbeStorage(){if(payloadBytes){if(future)future->releasePaired(payloadBytes);else releaseOpenCLProbeBytes(payloadBytes);}}
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
        std::shared_ptr<PrechargedProbeStorage> captured,std::shared_ptr<PrechargedProbeStorage> replay,
        std::shared_ptr<OpenCLProbeBatchStorage> future={}) {
        if(!owner || !captured || !replay || !captured->bytes() || !replay->bytes())throw std::bad_alloc{};
        struct Reservation {std::size_t bytes=0;~Reservation(){if(bytes)releaseOpenCLProbeBytes(bytes);}} reserved;
        std::shared_ptr<PrechargedProbeStorage> credit;
        if(future)credit=PrechargedProbeStorage::claim(std::move(future),metadataBytes());
        else {
            reserved.bytes=metadataBytes();
            if(!reserveOpenCLProbeBytes(reserved.bytes)){reserved.bytes=0;throw std::bad_alloc{};}
            credit=PrechargedProbeStorage::adopt(reserved.bytes);
        }
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
